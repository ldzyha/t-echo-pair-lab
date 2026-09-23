#pragma once
#include <cstdint>

namespace GPSFixValidity
{
// Validate before gm_mktime indexes its month table. This is a calendar check,
// not a guess at the user's current date or location.
inline bool calendar(unsigned year, unsigned month, unsigned day, unsigned hour, unsigned minute, unsigned second)
{
    if (year < 1970 || year > 2105 || month < 1 || month > 12 || hour > 23 || minute > 59 || second > 59)
        return false;
    constexpr unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    const unsigned maximum = days[month - 1] + (month == 2 && leap ? 1 : 0);
    return day >= 1 && day <= maximum;
}

inline bool expiredAlwaysOn(bool fixed, uint32_t intervalSeconds, bool hasFix, uint32_t now, uint32_t lastFix)
{
    return !fixed && intervalSeconds > 0 && intervalSeconds <= 10 && hasFix && static_cast<uint32_t>(now - lastFix) >= 30000;
}
} // namespace GPSFixValidity
