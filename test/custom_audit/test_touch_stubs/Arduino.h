#pragma once
#include <cstdint>
constexpr int LOW = 0, HIGH = 1, RISING = 4, INPUT_PULLDOWN_SENSE = 9;
uint32_t millis();
void pinMode(int pin, int mode);
int digitalRead(int pin);
void digitalWrite(int pin, int value);
int attachInterrupt(uint32_t pin, void (*callback)(), uint32_t mode);
