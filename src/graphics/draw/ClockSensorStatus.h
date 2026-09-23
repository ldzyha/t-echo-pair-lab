#pragma once

#include <cmath>
#include <cstdio>

inline void formatClockSensorStatus(char *out, size_t length, bool usb, unsigned batteryPercent, float dieC)
{
    char power[8];
    if (usb)
        std::snprintf(power, sizeof(power), "USB");
    else if (batteryPercent <= 100)
        std::snprintf(power, sizeof(power), "%u%%", batteryPercent);
    else
        std::snprintf(power, sizeof(power), "--%%");
    if (std::isfinite(dieC) && dieC >= -40 && dieC <= 85)
        std::snprintf(out, length, "%s/%.0fC", power, dieC);
    else
        std::snprintf(out, length, "%s/--C", power);
}
