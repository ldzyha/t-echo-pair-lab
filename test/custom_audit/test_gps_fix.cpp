#include "gps/GPSFixValidity.h"
#include <cassert>
#include <cstdint>
#include <iostream>

int main()
{
    using GPSFixValidity::calendar;
    using GPSFixValidity::expiredAlwaysOn;

    assert(calendar(2026, 9, 22, 23, 59, 59));
    assert(calendar(1970, 1, 1, 0, 0, 0));
    assert(calendar(2105, 12, 31, 23, 59, 59));
    assert(calendar(2000, 2, 29, 0, 0, 0));
    assert(calendar(2024, 2, 29, 0, 0, 0));
    assert(!calendar(2100, 2, 29, 0, 0, 0));
    assert(!calendar(2026, 2, 29, 0, 0, 0));
    assert(!calendar(2026, 4, 31, 0, 0, 0));
    assert(!calendar(2026, 0, 1, 0, 0, 0));
    assert(!calendar(2026, 13, 1, 0, 0, 0));
    assert(!calendar(2026, 1, 0, 0, 0, 0));
    assert(!calendar(2026, 1, 32, 0, 0, 0));
    assert(!calendar(1969, 12, 31, 0, 0, 0));
    assert(!calendar(2106, 1, 1, 0, 0, 0));
    assert(!calendar(2026, 1, 1, 24, 0, 0));
    assert(!calendar(2026, 1, 1, 0, 60, 0));
    assert(!calendar(2026, 1, 1, 0, 0, 60));
    assert(!calendar(UINT32_MAX, UINT32_MAX, UINT32_MAX, 0, 0, 0));

    // The current mobile configuration samples every 8 seconds. Only its
    // always-on fix expires at 30 seconds; sleeping/fixed modes retain theirs.
    assert(!expiredAlwaysOn(false, 8, true, 30999, 1000));
    assert(expiredAlwaysOn(false, 8, true, 31000, 1000));
    assert(expiredAlwaysOn(false, 10, true, 31000, 1000));
    assert(!expiredAlwaysOn(false, 11, true, 31000, 1000));
    assert(!expiredAlwaysOn(false, 0, true, 31000, 1000));
    assert(!expiredAlwaysOn(true, 8, true, 31000, 1000));
    assert(!expiredAlwaysOn(false, 8, false, 31000, 1000));

    // Real uptime rollover: fix just before millis wraps, expiration after it.
    const uint32_t last = UINT32_MAX - 9999;
    assert(!expiredAlwaysOn(false, 8, true, 19999, last));
    assert(expiredAlwaysOn(false, 8, true, 20000, last));
    std::cout << "GPS fix validity: calendar, operating modes, expiry and rollover passed\n";
}
