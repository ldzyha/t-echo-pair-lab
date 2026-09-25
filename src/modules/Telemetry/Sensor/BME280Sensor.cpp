#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME280.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "BME280Reading.h"
#include "BME280Sensor.h"
#include "BME280Setup.h"
#include "TelemetrySensor.h"
#if defined(TTGO_T_ECHO_PLUS)
#include "NodeDB.h"
#include "PowerStatus.h"
#include <nrf_sdm.h>
#include <nrf_soc.h>
#endif
#include <Adafruit_BME280.h>
#include <Wire.h>
#include <typeinfo>

#if defined(TTGO_T_ECHO_PLUS)
float BME280Sensor::readDieTemperature()
{
    uint8_t enabled = 0;
    int32_t quarters = 0;
    if (sd_softdevice_is_enabled(&enabled) != NRF_SUCCESS || !enabled || sd_temp_get(&quarters) != NRF_SUCCESS)
        return NAN;
    return quarters / 4.0f;
}

#endif

BME280Sensor::BME280Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_BME280, "BME280") {}

bool BME280Sensor::writeRegister(uint8_t reg, uint8_t value)
{
    sensorBus->beginTransmission(sensorAddress);
    sensorBus->write(reg);
    sensorBus->write(value);
    return sensorBus->endTransmission() == 0;
}

bool BME280Sensor::readRegister(uint8_t reg, uint8_t &value)
{
    sensorBus->beginTransmission(sensorAddress);
    sensorBus->write(reg);
    if (sensorBus->endTransmission(false) != 0 || sensorBus->requestFrom(sensorAddress, static_cast<uint8_t>(1)) != 1)
        return false;
    value = sensorBus->read();
    return true;
}

bool BME280Sensor::initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev)
{
    LOG_INFO("Init sensor: %s", sensorName);
    sensorBus = bus;
    sensorAddress = dev->address.address;
    status = bme280.begin(dev->address.address, bus);
    if (!status) {
        return status;
    }

    const auto setup = configureBme280Forced(
        [this](uint8_t reg, uint8_t &value) { return readRegister(reg, value); },
        [this](uint8_t reg, uint8_t value) { return writeRegister(reg, value); },
        [this]() {
            bme280.setSampling(Adafruit_BME280::MODE_FORCED, Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::SAMPLING_X1,
                               Adafruit_BME280::SAMPLING_X1, Adafruit_BME280::FILTER_OFF, Adafruit_BME280::STANDBY_MS_1000);
        },
        [](uint32_t ms) { delay(ms); });
    if (setup.error != Bme280SetupError::NONE) {
        LOG_WARN("BME280 setup failed: error=%u id=%02x hum=%02x ctrl=%02x cfg=%02x", static_cast<unsigned>(setup.error),
                 setup.chipId, setup.humidityControl, setup.measurementControl, setup.configuration);
        status = false;
        return false;
    }
    LOG_INFO("BME280 verified forced X1: id=%02x hum=%02x ctrl=%02x cfg=%02x", setup.chipId, setup.humidityControl,
             setup.measurementControl, setup.configuration);
#if defined(TTGO_T_ECHO_PLUS)
    thermalProfile = bme280ProfileForNode(nodeDB ? nodeDB->getNodeNum() : 0);
    if (thermalProfile) {
        thermalModel = Bme280ThermalModel(thermalProfile->points, thermalProfile->count);
        LOG_INFO("BME280 %s: experimental board profile, die filter=%.0fs, no boot heat ramp", thermalProfile->name,
                 Bme280ThermalModel::DIE_FILTER_MS / 1000.0f);
        LOG_INFO("BME280 calibration: T_offset=%.2f RH_gain=%.4f RH_offset=%.2f", thermalProfile->calibration.temperatureOffsetC,
                 thermalProfile->calibration.humidityGain, thermalProfile->calibration.humidityOffsetPercent);
    } else {
        LOG_INFO("BME280 TH4-RAW: no calibration for this node; reporting raw sensor values");
    }
#endif

    initI2CSensor();
    return status;
}

bool BME280Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    LOG_DEBUG("BME280 getMetrics");
    const uint32_t started = millis();
    Bme280RawReading raw{};
    bool valid = readBme280Sample(bme280, raw);
    if (valid) {
#if defined(TTGO_T_ECHO_PLUS)
        const bool powerKnown = powerStatus && powerStatus->knowsUSB();
        const bool hasUsb = powerStatus && powerStatus->getHasUSB();
        const float dieC = readDieTemperature();
        const float heatC = thermalProfile ? thermalModel.update(millis(), dieC) : 0.0f;
        auto calibration = thermalProfile ? thermalProfile->calibration : Bme280SensorCalibration{};
        if (thermalProfile)
            calibration.humidityGain *= thermalModel.getHumidityGain();
        valid = correctBme280Sample(raw, measurement->variant.environment_metrics, heatC, calibration,
                                    thermalProfile ? thermalModel.getHumidityCorrection() : 0.0f);
        if (valid) {
            const auto &metrics = measurement->variant.environment_metrics;
            LOG_INFO("BME280 %s: rawT=%.2f rawRH=%.2f die=%.2f heat=%.2f T=%.2f RH=%.2f P=%.2f USB=%u known=%u",
                     thermalProfile ? thermalProfile->name : "TH4-RAW", raw.temperature, raw.relativeHumidity, dieC, heatC,
                     metrics.temperature, metrics.relative_humidity, raw.pressureHpa, hasUsb, powerKnown);
            if (thermalProfile)
                LOG_INFO("BME280 curve: dieLP=%.3f range=%d RH_gain=%.6f T_offset=%.2f RH_offset=%.2f",
                         thermalModel.getFilteredDieC(), thermalModel.getRange(), calibration.humidityGain,
                         calibration.temperatureOffsetC,
                         calibration.humidityOffsetPercent + thermalModel.getHumidityCorrection());
        }
#else
        valid = correctBme280Sample(raw, measurement->variant.environment_metrics);
#endif
    }
    LOG_DEBUG("BME280 read completed: %lu ms valid=%u", (unsigned long)(millis() - started), valid);
    if (!valid)
        LOG_WARN("BME280 conversion failed or readings invalid");
    return valid;
}
#endif
