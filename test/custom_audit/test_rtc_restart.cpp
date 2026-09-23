#include "FSCommon.h"
#include "NodeDB.h"
#include "gps/RTC.h"
#include "gps/TrustedRTCRecord.h"
#include "main.h"
#include "modules/NodeInfoModule.h"
#include <cassert>
#include <iostream>

TestConfig config;
TestNodeInfoModule *nodeInfoModule = nullptr;
TestRtcAddress rtc_found;
TwoWire Wire;
FakeRtcFilesystem fakeRtcFilesystem;
bool failRtcMarkerWrite = false;
static uint32_t nowMs = 1000;
uint32_t fakeRtcEpoch = 1836273934, fakeRtcAtMs = nowMs;
uint32_t millis()
{
    return nowMs;
}

int main()
{
    constexpr uint32_t correct = 1790078400, bad = 1836273934;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityDevice && getTime() == bad);
    assert(setRTCFromLocalClient(correct) == RTCSetResultSuccess);
    assert(fakeRtcFilesystem.files.size() == 1);
    assert(fakeRtcEpoch == correct);

    nowMs += 23100;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultSuccess);
    assert(getTime() == correct + 23);
    assert(getRTCQuality() == RTCQualityDevice);
    assert(!isGPSTimeAcceptable(bad));
    timeval badTime = {bad, 0};
    assert(perhapsSetRTC(RTCQualityGPS, &badTime) == RTCSetResultInvalidTime);
    assert(perhapsSetRTC(RTCQualityNTP, &badTime) == RTCSetResultInvalidTime);
    assert(perhapsSetRTC(RTCQualityFromNet, &badTime) == RTCSetResultInvalidTime);
    assert(getTime() == correct + 23 && fakeRtcEpoch == correct);

    assert(setRTCFromLocalClient(correct + 23) == RTCSetResultSuccess);
    assert(fakeRtcFilesystem.files.size() == 1); // No repeated flash write for ordinary sync.
    assert(setRTCFromLocalClient(correct - 30) == RTCSetResultSuccess);
    assert(fakeRtcFilesystem.files.size() == 2);

    // Partial alternate-slot write preserves the earlier valid slot.
    failRtcMarkerWrite = true;
    assert(setRTCFromLocalClient(correct - 60) == RTCSetResultSuccess);
    failRtcMarkerWrite = false;
    fakeRtcEpoch = correct + 100;
    fakeRtcAtMs = nowMs;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultSuccess);
    assert(getTime() == correct + 100);
    assert(!isGPSTimeAcceptable(bad));

    // Corrupt both markers; neither the RTC nor GPS silently establishes trust.
    for (auto &entry : fakeRtcFilesystem.files)
        if (!entry.second.empty())
            entry.second[0] ^= 0x80;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultInvalidTime);
    assert(getValidTime(RTCQualityDevice) == 0);
    assert(!isGPSTimeAcceptable(bad));
    assert(setRTCFromLocalClient(correct + 120) == RTCSetResultSuccess);
    assert(!isGPSTimeAcceptable(bad));

    Wire.voltageLow = true;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultInvalidTime);
    assert(getValidTime(RTCQualityDevice) == 0);
    assert(!isGPSTimeAcceptable(bad));
    assert(setRTCFromLocalClient(correct + 125) == RTCSetResultSuccess);
    assert(!Wire.voltageLow);

    Wire.stopped = true;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultInvalidTime);
    assert(!isGPSTimeAcceptable(bad));
    Wire.stopped = false;
    Wire.responds = false;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultInvalidTime);
    assert(!isGPSTimeAcceptable(bad));
    Wire.responds = true;

    fakeRtcEpoch = correct - 500;
    fakeRtcAtMs = nowMs;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultInvalidTime);
    assert(!isGPSTimeAcceptable(bad));
    assert(setRTCFromLocalClient(correct + 130) == RTCSetResultSuccess);
    nowMs += 60000;
    resetRTCStateForTests();
    assert(readFromRTC() == RTCSetResultSuccess);
    assert(getTime() == correct + 190);
    assert(!isGPSTimeAcceptable(bad));
    std::cout << "Actual RTC.cpp commissioned restart, CRC slots, power loss and repair tests passed\n";
}
