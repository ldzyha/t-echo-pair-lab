#pragma once
#include "Arduino.h"
#define TTGO_T_ECHO_PLUS 1
#define BUTTON_PIN_TOUCH 11
#define PIN_EINK_EN 43
template <typename... Args> inline void testLog(const char *, Args...) {}
#define LOG_INFO(...) testLog(__VA_ARGS__)
using BaseType_t = int;
