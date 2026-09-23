#include "graphics/draw/ClockSensorStatus.h"
#include <cassert>
#include <cstring>

int main()
{
    char text[24];
    formatClockSensorStatus(text, sizeof(text), true, 100, 37.25f);
    assert(std::strcmp(text, "USB/37C") == 0);
    formatClockSensorStatus(text, sizeof(text), false, 85, 31.0f);
    assert(std::strcmp(text, "85%/31C") == 0);
    formatClockSensorStatus(text, sizeof(text), false, 100, -40);
    assert(std::strcmp(text, "100%/-40C") == 0);
    formatClockSensorStatus(text, sizeof(text), true, 100, NAN);
    assert(std::strcmp(text, "USB/--C") == 0);
    formatClockSensorStatus(text, sizeof(text), false, 255, 1000);
    assert(std::strcmp(text, "--%/--C") == 0);
    std::puts("PASS: clock battery/USB/MCU labels and invalid readings");
}
