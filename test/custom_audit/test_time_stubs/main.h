#pragma once
#ifdef TEST_HARDWARE_RTC
struct TestRtcAddress {
    unsigned address = 0x51;
};
extern TestRtcAddress rtc_found;
#endif
