#include "modules/Telemetry/Sensor/BME280Setup.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

struct RegisterDevice {
    uint8_t humidity = 5;
    uint8_t control = 0xb7;
    uint8_t configuration = 0;
    unsigned busyWaits = 8;
    unsigned waitCount = 0;
    unsigned configureCount = 0;
    unsigned ignoredHumidityWrites = 0;
    bool sleepRequested = false;
    bool stuck = false;
    bool failRead = false;
    bool failWrite = false;
    bool incorrectSetup = false;

    bool write(uint8_t reg, uint8_t value)
    {
        if (failWrite)
            return false;
        if (reg == 0xf4) {
            if ((value & 3) == 0 && busyWaits != 0) {
                sleepRequested = true;
            } else if (busyWaits == 0) {
                control = value;
                if ((value & 3) != 0)
                    busyWaits = 2;
            }
        } else if (reg == 0xf2) {
            if (busyWaits != 0)
                ++ignoredHumidityWrites;
            else
                humidity = value;
        } else if (reg == 0xf5 && busyWaits == 0) {
            configuration = value;
        }
        return true;
    }

    bool read(uint8_t reg, uint8_t &value)
    {
        if (failRead)
            return false;
        switch (reg) {
        case 0xf3:
            value = busyWaits ? 8 : 0;
            break;
        case 0xf4:
            value = control;
            break;
        case 0xf2:
            value = humidity;
            break;
        case 0xf5:
            value = configuration;
            break;
        case 0xd0:
            value = 0x60;
            break;
        default:
            return false;
        }
        return true;
    }

    void wait(uint32_t ms)
    {
        assert(ms == 5);
        ++waitCount;
        if (busyWaits && !stuck) {
            --busyWaits;
            if (busyWaits == 0) {
                control &= 0xfc;
                sleepRequested = false;
            }
        }
    }

    void configure()
    {
        ++configureCount;
        write(0xf4, 0);
        write(0xf2, incorrectSetup ? 5 : 1);
        write(0xf5, 0xa0);
        write(0xf4, 0x25);
    }

    Bme280SetupResult setup()
    {
        return configureBme280Forced([this](uint8_t reg, uint8_t &value) { return read(reg, value); },
                                     [this](uint8_t reg, uint8_t value) { return write(reg, value); }, [this]() { configure(); },
                                     [this](uint32_t ms) { wait(ms); });
    }
};

int main()
{
    RegisterDevice oldSequence;
    oldSequence.configure();
    assert(oldSequence.humidity == 5 && oldSequence.ignoredHumidityWrites == 1);

    RegisterDevice device;
    auto result = device.setup();
    assert(result.error == Bme280SetupError::NONE);
    assert(device.configureCount == 1 && device.ignoredHumidityWrites == 0);
    assert(device.waitCount == 10);
    assert(result.chipId == 0x60 && result.humidityControl == 1);
    assert(result.measurementControl == 0x24 && result.configuration == 0xa0);

    RegisterDevice busy;
    busy.stuck = true;
    assert(busy.setup().error == Bme280SetupError::TIMEOUT);
    assert(busy.waitCount == 50 && busy.configureCount == 0);

    RegisterDevice brokenRead;
    brokenRead.failRead = true;
    assert(brokenRead.setup().error == Bme280SetupError::IO);
    assert(brokenRead.waitCount == 0 && brokenRead.configureCount == 0);

    RegisterDevice brokenWrite;
    brokenWrite.failWrite = true;
    assert(brokenWrite.setup().error == Bme280SetupError::IO);
    assert(brokenWrite.waitCount == 0 && brokenWrite.configureCount == 0);

    RegisterDevice mismatch;
    mismatch.incorrectSetup = true;
    assert(mismatch.setup().error == Bme280SetupError::CONFIGURATION);
    assert(mismatch.configureCount == 1);
    std::puts("PASS: BME setup (active conversion, verified X1, timeout, read/write failure, mismatch)");
}
