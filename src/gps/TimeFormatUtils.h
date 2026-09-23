#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <ctime>

namespace TimeFormatUtils
{
inline time_t utcFromBrokenDown(const tm &value)
{
    if (value.tm_mon < 0 || value.tm_mon > 11)
        return -1;
    const int64_t year = 1900 + value.tm_year;
    const int64_t previousYear = year - 1;
    int64_t days = previousYear * 365 + previousYear / 4 - previousYear / 100 + previousYear / 400 - 719162;
    static constexpr int daysBeforeMonth[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    days += daysBeforeMonth[value.tm_mon] + value.tm_mday - 1;
    if (value.tm_mon >= 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
        ++days;
    return static_cast<time_t>(days * 86400 + value.tm_hour * 3600 + value.tm_min * 60 + value.tm_sec);
}

inline int32_t timezoneOffset(time_t utc)
{
    const tm *local = localtime(&utc);
    if (!local)
        return 0;
    const tm localCopy = *local;
    return static_cast<int32_t>(static_cast<int64_t>(utcFromBrokenDown(localCopy)) - utc);
}

struct ClockTime {
    bool valid = false;
    bool isPM = false;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

inline ClockTime formatClock(char *out, size_t length, uint32_t localEpoch, bool twelveHour)
{
    ClockTime result;
    result.valid = localEpoch != 0;
    if (!result.valid) {
        snprintf(out, length, "--:--");
        return result;
    }
    const uint32_t daySeconds = localEpoch % 86400;
    result.hour = daySeconds / 3600;
    result.minute = daySeconds % 3600 / 60;
    result.second = daySeconds % 60;
    result.isPM = result.hour >= 12;
    if (twelveHour) {
        result.hour %= 12;
        if (!result.hour)
            result.hour = 12;
    }
    snprintf(out, length, twelveHour ? "%d:%02d" : "%02d:%02d", result.hour, result.minute);
    return result;
}
} // namespace TimeFormatUtils
