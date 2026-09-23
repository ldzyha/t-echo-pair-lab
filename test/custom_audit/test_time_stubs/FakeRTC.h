#pragma once
#include "Arduino.h"
#include <cstddef>
#include <ctime>

class TwoWire
{
  public:
    bool responds = true, stopped = false, voltageLow = false;
    unsigned index = 0;
    void beginTransmission(unsigned) { index = 0; }
    void write(unsigned) {}
    unsigned endTransmission() { return responds ? 0 : 1; }
    unsigned requestFrom(uint8_t, uint8_t count) { return responds ? count : 0; }
    int read()
    {
        ++index;
        return index == 1 ? (stopped ? 0x20 : 0) : index == 3 ? (voltageLow ? 0x80 : 0) : 0;
    }
};
extern TwoWire Wire;
extern uint32_t fakeRtcEpoch, fakeRtcAtMs;
struct RTC_DateTime {
    time_t epoch;
    tm toUnixTime() { return *gmtime(&epoch); }
};
class SensorPCF8563
{
  public:
    bool begin(TwoWire &wire) { return wire.responds; }
    RTC_DateTime getDateTime() { return {fakeRtcEpoch + (millis() - fakeRtcAtMs) / 1000}; }
    void setDateTime(const tm &value)
    {
        if (!Wire.responds)
            return;
        tm copy = value;
        fakeRtcEpoch = timegm(&copy);
        fakeRtcAtMs = millis();
        Wire.voltageLow = false;
    }
    const char *getChipName() { return "FakePCF8563"; }
};
