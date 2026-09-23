#include "gps/TimeFormatUtils.h"
#include "gps/TrustedTime.h"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

static time_t utc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0)
{
    tm value = {};
    value.tm_year = year - 1900;
    value.tm_mon = month - 1;
    value.tm_mday = day;
    value.tm_hour = hour;
    value.tm_min = minute;
    value.tm_sec = second;
    return TimeFormatUtils::utcFromBrokenDown(value);
}

static void testLocalSource()
{
    assert(isTrustedLocalTimeSource(0, 123, false, true));
    assert(isTrustedLocalTimeSource(123, 123, false, true));
    assert(!isTrustedLocalTimeSource(456, 123, false, true));
    assert(!isTrustedLocalTimeSource(0, 123, true, true));
    assert(!isTrustedLocalTimeSource(0, 123, false, false));
    assert(!isTrustedLocalTimeSource(123, 123, false, false));
}

static void testTimeAnchor()
{
    constexpr uint32_t epoch = 1790078400;
    TrustedTimeAnchor anchor;
    assert(anchor.accepts(epoch + 2 * 365 * 86400, 100));
    anchor.set(epoch, 100);
    assert(anchor.accepts(epoch, 100));
    assert(anchor.accepts(epoch - 300, 100));
    assert(anchor.accepts(epoch + 300, 100));
    assert(!anchor.accepts(epoch - 301, 100));
    assert(!anchor.accepts(epoch + 301, 100));
    assert(!anchor.accepts(epoch + 2 * 365 * 86400, 1100));
    assert(anchor.accepts(epoch + 3600, 3600100));
    assert(!anchor.accepts(epoch + 3600 + 301, 3600100));

    anchor.set(epoch + 20, 5000);
    assert(anchor.accepts(epoch + 21, 6000));
    assert(!anchor.accepts(epoch + 3600, 6000));

    const uint32_t start = std::numeric_limits<uint32_t>::max() - 500;
    anchor.set(epoch, start);
    assert(anchor.accepts(epoch + 1, 499));
    assert(!anchor.accepts(epoch + 302, 499));

    anchor.set(epoch, 0);
    uint64_t elapsed = 0;
    for (unsigned i = 0; i < 12; ++i) {
        elapsed += 1000000000ULL;
        assert(anchor.accepts(epoch + static_cast<uint32_t>(elapsed / 1000), static_cast<uint32_t>(elapsed)));
    }
    anchor = TrustedTimeAnchor{};
    assert(anchor.accepts(epoch + 2 * 365 * 86400, 100));
}

static void testCalendarConversion()
{
    assert(utc(1970, 1, 1) == 0);
    assert(utc(2000, 2, 29) == 951782400);
    assert(utc(2026, 9, 22, 12) == 1790078400);
    for (int year : {1970, 2000, 2024, 2026, 2028, 2037}) {
        for (int month = 1; month <= 12; ++month) {
            tm value = {};
            value.tm_year = year - 1900;
            value.tm_mon = month - 1;
            value.tm_mday = 15;
            value.tm_hour = 23;
            value.tm_min = 59;
            value.tm_sec = 59;
            const time_t converted = TimeFormatUtils::utcFromBrokenDown(value);
            assert(converted == timegm(&value));
        }
    }
    tm invalid = {};
    invalid.tm_mon = -1;
    assert(TimeFormatUtils::utcFromBrokenDown(invalid) == -1);
    invalid.tm_mon = 12;
    assert(TimeFormatUtils::utcFromBrokenDown(invalid) == -1);
}

static void useTimezone(const char *zone)
{
    assert(setenv("TZ", zone, 1) == 0);
    tzset();
}

static void testTimezone()
{
    using TimeFormatUtils::timezoneOffset;
    useTimezone("EET-2EEST,M3.5.0/3,M10.5.0/4");
    assert(timezoneOffset(utc(2026, 1, 22, 12)) == 7200);
    assert(timezoneOffset(utc(2026, 9, 22, 12)) == 10800);
    assert(timezoneOffset(utc(2026, 3, 29, 0, 59, 59)) == 7200);
    assert(timezoneOffset(utc(2026, 3, 29, 1)) == 10800);
    assert(timezoneOffset(utc(2026, 10, 25, 0, 59, 59)) == 10800);
    assert(timezoneOffset(utc(2026, 10, 25, 1)) == 7200);
    useTimezone("GMT0");
    assert(timezoneOffset(utc(2026, 9, 22, 12)) == 0);
    useTimezone("IST-5:30");
    assert(timezoneOffset(utc(2026, 9, 22, 12)) == 19800);
    useTimezone("EST5");
    assert(timezoneOffset(utc(2026, 9, 22, 0)) == -18000);
}

static void testClockFormat()
{
    char text[16];
    auto value = TimeFormatUtils::formatClock(text, sizeof(text), 0, false);
    assert(!value.valid && std::strcmp(text, "--:--") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 0, true);
    assert(!value.valid && std::strcmp(text, "--:--") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 86400, false);
    assert(value.valid && !value.isPM && std::strcmp(text, "00:00") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 86400, true);
    assert(value.valid && !value.isPM && std::strcmp(text, "12:00") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 86400 + 43200, true);
    assert(value.valid && value.isPM && std::strcmp(text, "12:00") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 2 * 86400 - 1, false);
    assert(value.valid && value.second == 59 && std::strcmp(text, "23:59") == 0);
    value = TimeFormatUtils::formatClock(text, sizeof(text), 2 * 86400 - 1, true);
    assert(value.isPM && std::strcmp(text, "11:59") == 0);
}

int main()
{
    testLocalSource();
    testTimeAnchor();
    testCalendarConversion();
    testTimezone();
    testClockFormat();
    std::cout << "Time policy, source gate, rollover, calendar, DST and display tests passed\n";
}
