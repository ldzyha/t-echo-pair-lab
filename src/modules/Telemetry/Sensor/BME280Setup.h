#pragma once

#include <cstdint>

enum class Bme280SetupError { NONE, IO, TIMEOUT, CONFIGURATION };

struct Bme280SetupResult {
    Bme280SetupError error = Bme280SetupError::NONE;
    uint8_t chipId = 0;
    uint8_t humidityControl = 0;
    uint8_t measurementControl = 0;
    uint8_t configuration = 0;
};

template <typename Read, typename Write, typename Configure, typename Wait>
Bme280SetupResult configureBme280Forced(Read read, Write write, Configure configure, Wait wait)
{
    Bme280SetupResult result;
    auto waitForSleep = [&]() {
        for (unsigned attempt = 0; attempt < 50; ++attempt) {
            uint8_t status = 0;
            uint8_t control = 0;
            if (!read(0xf3, status) || !read(0xf4, control))
                return Bme280SetupError::IO;
            if ((status & 0x09) == 0 && (control & 0x03) == 0)
                return Bme280SetupError::NONE;
            wait(5);
        }
        return Bme280SetupError::TIMEOUT;
    };

    if (!write(0xf4, 0x00)) {
        result.error = Bme280SetupError::IO;
        return result;
    }
    // Bosch 3.3.1: ctrl_hum writes can be ignored until an active mode transition finishes.
    result.error = waitForSleep();
    if (result.error != Bme280SetupError::NONE)
        return result;

    configure();
    result.error = waitForSleep();
    if (result.error != Bme280SetupError::NONE)
        return result;

    if (!read(0xd0, result.chipId) || !read(0xf2, result.humidityControl) || !read(0xf4, result.measurementControl) ||
        !read(0xf5, result.configuration)) {
        result.error = Bme280SetupError::IO;
        return result;
    }
    if (result.chipId != 0x60 || (result.humidityControl & 0x07) != 0x01 || result.measurementControl != 0x24 ||
        result.configuration != 0xa0)
        result.error = Bme280SetupError::CONFIGURATION;
    return result;
}
