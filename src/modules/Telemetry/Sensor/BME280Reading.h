#pragma once

#include <cmath>

struct Bme280RawReading {
    float temperature;
    float relativeHumidity;
    float pressureHpa;
};

struct Bme280SensorCalibration {
    float temperatureOffsetC = 0.0f;
    float humidityGain = 1.0f;
    float humidityOffsetPercent = 0.0f;
};

inline bool validBme280Sample(const Bme280RawReading &raw)
{
    return std::isfinite(raw.temperature) && std::isfinite(raw.relativeHumidity) && std::isfinite(raw.pressureHpa) &&
           raw.temperature >= -40.0f && raw.temperature <= 85.0f && raw.relativeHumidity >= 0.0f &&
           raw.relativeHumidity <= 100.0f && raw.pressureHpa >= 300.0f && raw.pressureHpa <= 1100.0f;
}

template <typename Sensor> bool readBme280Sample(Sensor &sensor, Bme280RawReading &raw)
{
    if (!sensor.takeForcedMeasurement())
        return false;

    const Bme280RawReading sample{sensor.readTemperature(), sensor.readHumidity(), sensor.readPressure() / 100.0f};
    if (!validBme280Sample(sample))
        return false;
    raw = sample;
    return true;
}

template <typename Metrics>
bool correctBme280Sample(const Bme280RawReading &raw, Metrics &metrics, float heatC = 0.0f,
                         const Bme280SensorCalibration &calibration = {}, float humidityHeatPercent = 0.0f)
{
    if (!validBme280Sample(raw) || !std::isfinite(heatC) || heatC < 0 || !std::isfinite(calibration.temperatureOffsetC) ||
        !std::isfinite(calibration.humidityGain) || calibration.humidityGain <= 0 ||
        !std::isfinite(calibration.humidityOffsetPercent) || !std::isfinite(humidityHeatPercent))
        return false;

    const float adjustment = calibration.temperatureOffsetC - heatC;
    const float temperature = raw.temperature + adjustment;
    if (!std::isfinite(temperature) || temperature < -40.0f || temperature > 85.0f)
        return false;
    // Thermal RH conversion assumes unchanged water-vapor pressure; sensor calibration is separate.
    float humidity = raw.relativeHumidity * std::exp(17.625f * raw.temperature / (243.04f + raw.temperature) -
                                                     17.625f * temperature / (243.04f + temperature));
    humidity = humidity * calibration.humidityGain + calibration.humidityOffsetPercent + humidityHeatPercent;
    if (!std::isfinite(humidity))
        return false;
    humidity = std::fmin(100.0f, std::fmax(0.0f, humidity));

    metrics.temperature = temperature;
    metrics.relative_humidity = humidity;
    metrics.barometric_pressure = raw.pressureHpa;
    metrics.has_temperature = true;
    metrics.has_relative_humidity = true;
    metrics.has_barometric_pressure = true;
    return true;
}

template <typename Sensor, typename Metrics>
bool readBme280Metrics(Sensor &sensor, Metrics &metrics, float heatC = 0.0f, Bme280RawReading *raw = nullptr)
{
    Bme280RawReading sample{};
    if (!readBme280Sample(sensor, sample) || !correctBme280Sample(sample, metrics, heatC))
        return false;
    if (raw)
        *raw = sample;
    return true;
}
