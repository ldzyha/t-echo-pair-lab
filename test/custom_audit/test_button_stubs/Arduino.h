#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
using std::max;
constexpr int LOW = 0;
constexpr int HIGH = 1;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 2;
unsigned long millis();
inline void pinMode(int, int) {}
inline int digitalRead(int)
{
    return HIGH;
}
