#pragma once
#include <cstdint>
template <typename... Args> inline void testLog(const char *, Args...) {}
#define LOG_INFO(...) testLog(__VA_ARGS__)
#define LOG_WARN(...) testLog(__VA_ARGS__)
#define LOG_DEBUG(...) testLog(__VA_ARGS__)
#define MESHTASTIC_EXCLUDE_TZ 0
#define BUILD_EPOCH 1790035200L
#define TTGO_T_ECHO_PLUS 1
#ifdef TEST_HARDWARE_RTC
#define PCF8563_RTC 0x51
#include "FakeRTC.h"
#endif
