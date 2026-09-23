#include "NodeDB.h"
#include "gps/RTC.h"
#include "modules/NodeInfoModule.h"
#include <cassert>
#include <cstring>
#include <iostream>

TestConfig config;
TestNodeInfoModule *nodeInfoModule = nullptr;
static uint32_t nowMs = 1000;
uint32_t millis()
{
    return nowMs;
}

int main()
{
    constexpr uint32_t currentEpoch = 1790078400;
    constexpr uint32_t badGpsEpoch = 1836273934;
    timeval gps = {badGpsEpoch, 0};
    resetRTCStateForTests();
    assert(getRTCQuality() == RTCQualityNone);
    assert(getValidTime(RTCQualityDevice) == 0);
    assert(perhapsSetRTC(RTCQualityGPS, &gps) == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityGPS && getTime() == badGpsEpoch);

    timeval local = {currentEpoch, 0};
    assert(perhapsSetRTC(RTCQualityNTP, &local) == RTCSetResultNotSet);
    assert(setRTCFromLocalClient(currentEpoch) == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityNTP && getTime() == currentEpoch);
    assert(!isGPSTimeAcceptable(badGpsEpoch));
    assert(perhapsSetRTC(RTCQualityGPS, &gps) == RTCSetResultInvalidTime);
    assert(getRTCQuality() == RTCQualityNTP && getTime() == currentEpoch);

    nowMs += 15000;
    assert(getTime() == currentEpoch + 15);
    gps.tv_sec = currentEpoch + 15;
    assert(isGPSTimeAcceptable(gps.tv_sec));
    assert(perhapsSetRTC(RTCQualityGPS, &gps) == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityGPS);
    assert(setRTCFromLocalClient(currentEpoch + 16) == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityNTP && getTime() == currentEpoch + 16);

    assert(setRTCFromLocalClient(BUILD_EPOCH - 1) == RTCSetResultInvalidTime);
    assert(getTime() == currentEpoch + 16);
    assert(!isGPSTimeAcceptable(badGpsEpoch));

    std::strcpy(config.device.tzdef, "EET-2EEST,M3.5.0/3,M10.5.0/4");
    applyConfiguredTimezone();
    assert(getTime(true) == getTime() + 10800);
    std::strcpy(config.device.tzdef, "GMT0");
    applyConfiguredTimezone();
    assert(getTime(true) == getTime());
    config.device.tzdef[0] = '\0';
    applyConfiguredTimezone();
    assert(getTime(true) == getTime());

    resetRTCStateForTests();
    assert(getRTCQuality() == RTCQualityNone);
    assert(getValidTime(RTCQualityDevice) == 0);
    assert(isGPSTimeAcceptable(badGpsEpoch));
    gps.tv_sec = badGpsEpoch;
    assert(perhapsSetRTC(RTCQualityGPS, &gps) == RTCSetResultSuccess);
    assert(setRTCFromLocalClient(currentEpoch + 17) == RTCSetResultSuccess);
    assert(getRTCQuality() == RTCQualityNTP);
    assert(!isGPSTimeAcceptable(badGpsEpoch));
    std::cout << "Actual RTC.cpp local authority, GPS rejection, timezone apply and quality reset tests passed\n";
}
