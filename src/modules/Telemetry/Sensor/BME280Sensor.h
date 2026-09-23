#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR && __has_include(<Adafruit_BME280.h>)

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include <Adafruit_BME280.h>
#if defined(TTGO_T_ECHO_PLUS)
#include "BME280ThermalProfile.h"
#endif

class BME280Sensor : public TelemetrySensor
{
  private:
    Adafruit_BME280 bme280;
    TwoWire *sensorBus = nullptr;
    uint8_t sensorAddress = 0;
#if defined(TTGO_T_ECHO_PLUS)
    const Bme280ThermalProfile *thermalProfile = nullptr;
    Bme280ThermalModel thermalModel{nullptr, 0};
#endif
    bool readRegister(uint8_t reg, uint8_t &value);
    bool writeRegister(uint8_t reg, uint8_t value);

  public:
    BME280Sensor();
#if defined(TTGO_T_ECHO_PLUS)
    static float readDieTemperature();
#endif
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual bool initDevice(TwoWire *bus, ScanI2C::FoundDevice *dev) override;
};

#endif
