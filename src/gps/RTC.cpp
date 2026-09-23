#include "RTC.h"
#include "NodeDB.h"
#include "TimeFormatUtils.h"
#include "TrustedTime.h"
#include "configuration.h"
#include "detect/ScanI2C.h"
#include "main.h"
#include "modules/NodeInfoModule.h"
#include <Throttle.h>
#include <cstdlib>
#include <sys/time.h>
#include <time.h>
#if defined(TTGO_T_ECHO_PLUS) && defined(PCF8563_RTC)
#include "FSCommon.h"
#include "SPILock.h"
#include "TrustedRTCRecord.h"
#endif

static RTCQuality currentQuality = RTCQualityNone;
#if defined(TTGO_T_ECHO_PLUS)
static TrustedTimeAnchor trustedLocalTime;
static bool needsTrustedLocalTime = false;
#endif
uint32_t lastSetFromPhoneNtpOrGps = 0;

static uint32_t lastTimeValidationWarning = 0;
static const uint32_t TIME_VALIDATION_WARNING_INTERVAL_MS = 15000; // 15 seconds

#if defined(TTGO_T_ECHO_PLUS) && defined(PCF8563_RTC)
static uint32_t lastHardwareRtcEpoch = 0;
static TrustedRTCRecord trustedRtcRecord;
static int trustedRtcSlot = -1;
static bool trustedRtcRecordLoaded = false;
static const char *const trustedRtcPaths[] = {"/prefs/trusted-rtc.0", "/prefs/trusted-rtc.1"};

static bool readTrustedRtcSlot(int slot, TrustedRTCRecord &record)
{
    File file = FSCom.open(trustedRtcPaths[slot], FILE_O_READ);
    if (!file)
        return false;
    const bool complete = file.size() == sizeof(record) && file.read((uint8_t *)&record, sizeof(record)) == sizeof(record);
    file.close();
    return complete && record.valid();
}

static void loadTrustedRtcRecord()
{
    if (trustedRtcRecordLoaded)
        return;
    trustedRtcRecordLoaded = true;
    concurrency::LockGuard guard(spiLock);
    for (int slot = 0; slot < 2; ++slot) {
        if (FSCom.exists(trustedRtcPaths[slot]))
            needsTrustedLocalTime = true;
        TrustedRTCRecord candidate;
        if (readTrustedRtcSlot(slot, candidate) && (trustedRtcSlot < 0 || candidate.generation > trustedRtcRecord.generation)) {
            trustedRtcRecord = candidate;
            trustedRtcSlot = slot;
        }
    }
}

static bool saveTrustedRtcRecord(uint32_t epoch)
{
    loadTrustedRtcRecord();
    if (trustedRtcSlot >= 0 && epoch >= trustedRtcRecord.minimumEpoch)
        return true;
    TrustedRTCRecord record;
    record.generation = trustedRtcSlot < 0 ? 1 : trustedRtcRecord.generation + 1;
    record.minimumEpoch = epoch;
    record.seal();
    const int slot = trustedRtcSlot == 0 ? 1 : 0;
    concurrency::LockGuard guard(spiLock);
    FSCom.remove(trustedRtcPaths[slot]);
    File file = FSCom.open(trustedRtcPaths[slot], FILE_O_WRITE);
    if (!file)
        return false;
    const bool complete = file.write((const uint8_t *)&record, sizeof(record)) == sizeof(record);
    file.flush();
    file.close();
    TrustedRTCRecord verified;
    if (!complete || !readTrustedRtcSlot(slot, verified) || verified.generation != record.generation)
        return false;
    trustedRtcRecord = verified;
    trustedRtcSlot = slot;
    return true;
}

static bool rtcIntegrityGood(TwoWire &wire)
{
    wire.beginTransmission(PCF8563_RTC);
    wire.write(0x00);
    if (wire.endTransmission() != 0 || wire.requestFrom((uint8_t)PCF8563_RTC, (uint8_t)3) != 3)
        return false;
    const uint8_t control = wire.read();
    wire.read();
    const uint8_t seconds = wire.read();
    return !(control & 0x20) && !(seconds & 0x80); // STOP and voltage-low invalidate the clock.
}
#endif

static void triggerNodeInfoCheckOnTimeSource(RTCQuality oldQuality, RTCQuality newQuality)
{
    if (oldQuality == RTCQualityNone && newQuality > RTCQualityNone && nodeInfoModule) {
        LOG_DEBUG("Time source acquired (%s -> %s), triggering NodeInfo recheck", RtcName(oldQuality), RtcName(newQuality));
        nodeInfoModule->triggerImmediateNodeInfoCheck();
    }
}

RTCQuality getRTCQuality()
{
    return currentQuality;
}

void applyConfiguredTimezone()
{
#if !MESHTASTIC_EXCLUDE_TZ
    setenv("TZ", config.device.tzdef[0] ? config.device.tzdef : "GMT0", 1);
    tzset();
#endif
}

bool isGPSTimeAcceptable(uint32_t epochSeconds)
{
#if defined(TTGO_T_ECHO_PLUS)
    return !needsTrustedLocalTime && trustedLocalTime.accepts(epochSeconds, millis());
#else
    (void)epochSeconds;
    return true;
#endif
}

RTCSetResult setRTCFromLocalClient(uint32_t epochSeconds)
{
    timeval tv = {};
    tv.tv_sec = epochSeconds;
#if defined(TTGO_T_ECHO_PLUS)
    const auto result = perhapsSetRTC(RTCQualityNTP, &tv, true);
    if (result == RTCSetResultSuccess) {
        trustedLocalTime.set(epochSeconds, millis());
        needsTrustedLocalTime = false;
#if defined(PCF8563_RTC)
        if (lastHardwareRtcEpoch >= epochSeconds && lastHardwareRtcEpoch - epochSeconds <= 2) {
            if (saveTrustedRtcRecord(epochSeconds))
                LOG_INFO("Trusted RTC commissioned for offline restart");
            else
                LOG_WARN("Trusted RTC marker write failed; offline trust unavailable");
        } else {
            LOG_WARN("Local time set in RAM; hardware RTC verification failed");
        }
#endif
        LOG_INFO("Trusted local clock set: epoch=%lu; GPS tolerance=%lus", (unsigned long)epochSeconds,
                 (unsigned long)TrustedTimeAnchor::GPS_TOLERANCE_SECONDS);
    }
    return result;
#else
    return perhapsSetRTC(RTCQualityNTP, &tv, false);
#endif
}

// stuff that really should be in in the instance instead...
static uint32_t
    timeStartMsec; // Once we have a GPS lock, this is where we hold the initial msec clock that corresponds to that time
static uint64_t zeroOffsetSecs; // GPS based time in secs since 1970 - only updated once on initial lock

#ifdef PIO_UNIT_TESTING
// Test seam: unit tests can inject a fake system clock (e.g. the uptime seconds that
// gettimeofday() returns on boards without a real RTC, like RP2040) and force readFromRTC()
// down the no-hardware-RTC fallback even when a hardware-RTC branch is compiled in.
static bool hasMockSystemTime = false;
static bool forceSystemTimeFallback = false;
static struct timeval mockSystemTime = {};
#endif

// Reads the platform system clock (or the injected mock during unit tests). Used only by the
// no-hardware-RTC fallback below, so it may be unused on builds with a hardware RTC.
[[maybe_unused]] static bool readSystemTime(struct timeval *tv)
{
#ifdef PIO_UNIT_TESTING
    if (hasMockSystemTime) {
        *tv = mockSystemTime;
        return true;
    }
#endif
    return gettimeofday(tv, NULL) == 0;
}

// Seeds the clock from the system time on boards without a hardware RTC. gettimeofday() can
// return uptime rather than wall-clock time there (e.g. RP2040), so only adopt it when we have
// nothing better yet -- never clobber a higher-quality GPS/NTP/phone source (issue #9828).
[[maybe_unused]] static RTCSetResult readFromSystemTimeFallback()
{
    struct timeval tv;
    if (readSystemTime(&tv)) {
        uint32_t now = millis();
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
        if (currentQuality == RTCQualityNone) {
            LOG_DEBUG("Seed time from system clock: %lu", (unsigned long)printableEpoch);
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
        } else {
            LOG_DEBUG("Ignore system clock fallback (%lu); current RTC quality is %s", (unsigned long)printableEpoch,
                      RtcName(currentQuality));
        }
        return RTCSetResultSuccess;
    }
    return RTCSetResultNotSet;
}

/**
 * Reads date/time from the RTC module (or system-time fallback) and seeds internal timekeeping.
 * @return RTCSetResultSuccess if a time source was read successfully (even if an existing higher-quality time is retained).
 */
RTCSetResult readFromRTC()
{
#if defined(TTGO_T_ECHO_PLUS) && defined(PCF8563_RTC)
    loadTrustedRtcRecord();
    lastHardwareRtcEpoch = 0;
#endif
#ifdef PIO_UNIT_TESTING
    if (forceSystemTimeFallback) {
        return readFromSystemTimeFallback();
    }
#endif

    [[maybe_unused]] struct timeval tv; /* btw settimeofday() is helpful here too*/
#ifdef RV3028_RTC
    if (rtc_found.address == RV3028_RTC) {
        uint32_t now = millis();
        Melopero_RV3028 rtc;
#if WIRE_INTERFACES_COUNT == 2
        rtc.initI2C(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
        rtc.initI2C();
#endif
        tm t;
        t.tm_year = rtc.getYear() - 1900;
        t.tm_mon = rtc.getMonth() - 1;
        t.tm_mday = rtc.getDate();
        t.tm_hour = rtc.getHour();
        t.tm_min = rtc.getMinute();
        t.tm_sec = rtc.getSecond();
        tv.tv_sec = gm_mktime(&t);
        tv.tv_usec = 0;
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms

#ifdef BUILD_EPOCH
        if (tv.tv_sec < BUILD_EPOCH) {
            if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                LOG_WARN("Ignore time (%ld) before build epoch (%ld)!", printableEpoch, BUILD_EPOCH);
            }
            return RTCSetResultInvalidTime;
        }
#endif

        LOG_DEBUG("Read RTC time from RV3028 getTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900, t.tm_mon + 1,
                  t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
        if (currentQuality == RTCQualityNone) {
            RTCQuality oldQuality = currentQuality;
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
            currentQuality = RTCQualityDevice;
            triggerNodeInfoCheckOnTimeSource(oldQuality, currentQuality);
        }
        return RTCSetResultSuccess;
    } else {
        LOG_WARN("RTC not found (found address 0x%02X)", rtc_found.address);
    }
#elif defined(PCF8563_RTC) || defined(PCF85063_RTC)
#if defined(PCF8563_RTC)
    if (rtc_found.address == PCF8563_RTC) {
        SensorPCF8563 rtc;
#elif defined(PCF85063_RTC)
    if (rtc_found.address == PCF85063_RTC) {
        SensorPCF85063 rtc;

#endif
        uint32_t now = millis();

#if defined(TTGO_T_ECHO_PLUS)
        if (!rtc.begin(Wire) || !rtcIntegrityGood(Wire)) {
            LOG_WARN("RTC clock invalid: read failed, stopped or lost power");
            return RTCSetResultInvalidTime;
        }
#else
#if WIRE_INTERFACES_COUNT == 2
        rtc.begin(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
        rtc.begin(Wire);
#endif
#endif

        RTC_DateTime datetime = rtc.getDateTime();
        tm t = datetime.toUnixTime();
        tv.tv_sec = gm_mktime(&t);
        tv.tv_usec = 0;
        uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms

#if defined(TTGO_T_ECHO_PLUS)
        if (t.tm_mon < 0 || t.tm_mon > 11 || t.tm_mday < 1 || t.tm_mday > 31 || t.tm_hour < 0 || t.tm_hour > 23 || t.tm_min < 0 ||
            t.tm_min > 59 || t.tm_sec < 0 || t.tm_sec > 59)
            return RTCSetResultInvalidTime;
#endif

#ifdef BUILD_EPOCH
        if (tv.tv_sec < BUILD_EPOCH) {
            if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                LOG_WARN("Ignore time (%ld) before build epoch (%ld)!", printableEpoch, BUILD_EPOCH);
                lastTimeValidationWarning = millis();
            }
            return RTCSetResultInvalidTime;
        }
#endif

        LOG_DEBUG("Read RTC time from %s getDateTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", rtc.getChipName(), t.tm_year + 1900,
                  t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
#if defined(TTGO_T_ECHO_PLUS)
        lastHardwareRtcEpoch = printableEpoch;
        if (currentQuality == RTCQualityNone && needsTrustedLocalTime) {
            if (trustedRtcSlot < 0 || !trustedRtcRecord.acceptsRTC(printableEpoch, true)) {
                LOG_WARN("RTC not trusted after restart; connect a local client to set time");
                return RTCSetResultInvalidTime;
            }
            trustedLocalTime.set(printableEpoch, now);
            needsTrustedLocalTime = false;
            LOG_INFO("Restored trusted advancing hardware RTC: epoch=%lu", (unsigned long)printableEpoch);
        }
#endif
        if (currentQuality == RTCQualityNone) {
            RTCQuality oldQuality = currentQuality;
            timeStartMsec = now;
            zeroOffsetSecs = tv.tv_sec;
            currentQuality = RTCQualityDevice;
            triggerNodeInfoCheckOnTimeSource(oldQuality, currentQuality);
        }
        return RTCSetResultSuccess;
    } else {
        LOG_WARN("RTC not found (found address 0x%02X)", rtc_found.address);
    }
#elif defined(RX8130CE_RTC)
    if (rtc_found.address == RX8130CE_RTC) {
        uint32_t now = millis();
#ifdef MUZI_BASE
        ArtronShop_RX8130CE rtc(&Wire1);
#else
        ArtronShop_RX8130CE rtc(&Wire);
#endif
        tm t;
        if (rtc.getTime(&t)) {
            tv.tv_sec = gm_mktime(&t);
            tv.tv_usec = 0;

            uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
            LOG_DEBUG("Read RTC time from RX8130CE getDateTime as %02d-%02d-%02d %02d:%02d:%02d (%ld)", t.tm_year + 1900,
                      t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec, printableEpoch);
#ifdef BUILD_EPOCH
            if (tv.tv_sec < BUILD_EPOCH) {
                if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
                    LOG_WARN("Ignore time (%ld) before build epoch (%ld)!", printableEpoch, BUILD_EPOCH);
                    lastTimeValidationWarning = millis();
                }
                return RTCSetResultInvalidTime;
            }
#endif
            if (currentQuality == RTCQualityNone) {
                RTCQuality oldQuality = currentQuality;
                timeStartMsec = now;
                zeroOffsetSecs = tv.tv_sec;
                currentQuality = RTCQualityDevice;
                triggerNodeInfoCheckOnTimeSource(oldQuality, currentQuality);
            }
            return RTCSetResultSuccess;
        }
    }
#else
    return readFromSystemTimeFallback();
#endif
    return RTCSetResultNotSet;
}

/**
 * Sets the RTC (Real-Time Clock) if the provided time is of higher quality than the current RTC time.
 *
 * @param q The quality of the provided time.
 * @param tv A pointer to a timeval struct containing the time to potentially set the RTC to.
 * @return RTCSetResult
 *
 * If we haven't yet set our RTC this boot, set it from a GPS derived time
 */
RTCSetResult perhapsSetRTC(RTCQuality q, const struct timeval *tv, bool forceUpdate)
{
    static uint32_t lastSetMsec = 0;
    uint32_t now = millis();
    uint32_t printableEpoch = tv->tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
#if defined(TTGO_T_ECHO_PLUS)
    if ((q == RTCQualityGPS || (!forceUpdate && q >= RTCQualityFromNet)) && !isGPSTimeAcceptable(printableEpoch)) {
        if (!Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS)) {
            LOG_WARN("Ignore %s time (%lu): conflicts with trusted local clock", RtcName(q), (unsigned long)printableEpoch);
            lastTimeValidationWarning = now;
        }
        return RTCSetResultInvalidTime;
    }
#endif
#ifdef BUILD_EPOCH
    if (tv->tv_sec < BUILD_EPOCH) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            LOG_WARN("Ignore time (%ld) before build epoch (%ld)!", printableEpoch, BUILD_EPOCH);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    } else if ((uint64_t)tv->tv_sec > ((uint64_t)BUILD_EPOCH + FORTY_YEARS)) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            // Calculate max allowed time safely to avoid overflow in logging
            uint64_t maxAllowedTime = (uint64_t)BUILD_EPOCH + FORTY_YEARS;
            uint32_t maxAllowedPrintable = (maxAllowedTime > UINT32_MAX) ? UINT32_MAX : (uint32_t)maxAllowedTime;
            LOG_WARN("Ignore time (%ld) too far in the future (build epoch: %ld, max allowed: %ld)!", printableEpoch,
                     (uint32_t)BUILD_EPOCH, maxAllowedPrintable);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    }
#endif

    bool shouldSet;
    if (forceUpdate) {
        shouldSet = true;
        LOG_DEBUG("Override current RTC quality (%s) with incoming time of RTC quality of %s", RtcName(currentQuality),
                  RtcName(q));
    } else if (q > currentQuality) {
        shouldSet = true;
        LOG_DEBUG("Upgrade time to quality %s", RtcName(q));
    } else if (q == RTCQualityGPS) {
        shouldSet = true;
        LOG_DEBUG("Reapply GPS time: %ld secs", printableEpoch);
    } else if (q == RTCQualityNTP && !Throttle::isWithinTimespanMs(lastSetMsec, (30 * 60 * 1000UL))) {
        // Every 30 minutes we will slam in a new NTP or Phone GPS / NTP time, to correct for local RTC clock drift
        shouldSet = true;
        LOG_DEBUG("Reapply external time to correct clock drift %ld secs", printableEpoch);
    } else {
        shouldSet = false;
        LOG_DEBUG("Current RTC quality: %s. Ignore time of RTC quality of %s", RtcName(currentQuality), RtcName(q));
    }

    if (shouldSet) {
        RTCQuality oldQuality = currentQuality;
        currentQuality = q;
        lastSetMsec = now;
        if (currentQuality >= RTCQualityNTP) {
            lastSetFromPhoneNtpOrGps = now;
        }

        // This delta value works on all platforms
        timeStartMsec = now;
        zeroOffsetSecs = tv->tv_sec;
        // If this platform has a settable RTC, set it
#ifdef RV3028_RTC
        if (rtc_found.address == RV3028_RTC) {
            Melopero_RV3028 rtc;
#if WIRE_INTERFACES_COUNT == 2
            rtc.initI2C(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
            rtc.initI2C();
#endif
            tm *t = gmtime(&tv->tv_sec);
            rtc.setTime(t->tm_year + 1900, t->tm_mon + 1, t->tm_wday, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
            LOG_DEBUG("RV3028_RTC setTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                      t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
        } else {
            LOG_WARN("RTC not found (found address 0x%02X)", rtc_found.address);
        }
#elif defined(PCF8563_RTC) || defined(PCF85063_RTC)
#if defined(PCF8563_RTC)
        if (rtc_found.address == PCF8563_RTC) {
            SensorPCF8563 rtc;
#elif defined(PCF85063_RTC)
        if (rtc_found.address == PCF85063_RTC) {
            SensorPCF85063 rtc;

#endif

#if WIRE_INTERFACES_COUNT == 2
            rtc.begin(rtc_found.port == ScanI2C::I2CPort::WIRE1 ? Wire1 : Wire);
#else
            rtc.begin(Wire);
#endif
            tm *t = gmtime(&tv->tv_sec);
            rtc.setDateTime(*t);
            LOG_DEBUG("%s setDateTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", rtc.getChipName(), t->tm_year + 1900, t->tm_mon + 1,
                      t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
        } else {
            LOG_WARN("RTC not found (found address 0x%02X)", rtc_found.address);
        }
#elif defined(RX8130CE_RTC)
        if (rtc_found.address == RX8130CE_RTC) {
#ifdef MUZI_BASE
            ArtronShop_RX8130CE rtc(&Wire1);
#else
            ArtronShop_RX8130CE rtc(&Wire);
#endif
            tm *t = gmtime(&tv->tv_sec);
            if (rtc.setTime(*t)) {
                LOG_DEBUG("RX8130CE setDateTime %02d-%02d-%02d %02d:%02d:%02d (%ld)", t->tm_year + 1900, t->tm_mon + 1,
                          t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec, printableEpoch);
            } else {
                LOG_WARN("Failed to set time for RX8130CE");
            }
        }
#elif defined(ARCH_ESP32) || defined(ARCH_RP2040)
        settimeofday(tv, NULL);
#endif

        readFromRTC();
        triggerNodeInfoCheckOnTimeSource(oldQuality, currentQuality);
        return RTCSetResultSuccess;
    } else {
        return RTCSetResultNotSet; // RTC was already set with a higher quality time
    }
}

const char *RtcName(RTCQuality quality)
{
    switch (quality) {
    case RTCQualityNone:
        return "None";
    case RTCQualityDevice:
        return "Device";
    case RTCQualityFromNet:
        return "Net";
    case RTCQualityNTP:
        return "NTP";
    case RTCQualityGPS:
        return "GPS";
    default:
        return "Unknown";
    }
}

/**
 * Sets the RTC time if the provided time is of higher quality than the current RTC time.
 *
 * @param q The quality of the provided time.
 * @param t The time to potentially set the RTC to.
 * @return True if the RTC was set to the provided time, false otherwise.
 */
RTCSetResult perhapsSetRTC(RTCQuality q, const struct tm &t)
{
    /* Convert to unix time
    The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
    (midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
    */
    // horrible hack to make mktime TZ agnostic - best practise according to
    // https://www.gnu.org/software/libc/manual/html_node/Broken_002ddown-Time.html
    time_t res = gm_mktime(&t);
    struct timeval tv;
    tv.tv_sec = res;
    tv.tv_usec = 0;                      // time.centisecond() * (10 / 1000);
    uint32_t printableEpoch = tv.tv_sec; // Print lib only supports 32 bit but time_t can be 64 bit on some platforms
#ifdef BUILD_EPOCH
    if (tv.tv_sec < BUILD_EPOCH) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            LOG_WARN("Ignore time (%lu) before build epoch (%lu)!", printableEpoch, BUILD_EPOCH);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    } else if ((uint64_t)tv.tv_sec > ((uint64_t)BUILD_EPOCH + FORTY_YEARS)) {
        if (Throttle::isWithinTimespanMs(lastTimeValidationWarning, TIME_VALIDATION_WARNING_INTERVAL_MS) == false) {
            // Calculate max allowed time safely to avoid overflow in logging
            uint64_t maxAllowedTime = (uint64_t)BUILD_EPOCH + FORTY_YEARS;
            uint32_t maxAllowedPrintable = (maxAllowedTime > UINT32_MAX) ? UINT32_MAX : (uint32_t)maxAllowedTime;
            LOG_WARN("Ignore time (%lu) too far in the future (build epoch: %lu, max allowed: %lu)!", printableEpoch,
                     (uint32_t)BUILD_EPOCH, maxAllowedPrintable);
            lastTimeValidationWarning = millis();
        }
        return RTCSetResultInvalidTime;
    }
#endif

    // LOG_DEBUG("Got time from GPS month=%d, year=%d, unixtime=%ld", t.tm_mon, t.tm_year, tv.tv_sec);
    if (t.tm_year < 0 || t.tm_year >= 300) {
        // LOG_DEBUG("Ignore invalid GPS month=%d, year=%d, unixtime=%ld", t.tm_mon, t.tm_year, tv.tv_sec);
        return RTCSetResultInvalidTime;
    } else {
        return perhapsSetRTC(q, &tv);
    }
}

/**
 * Returns the timezone offset in seconds.
 *
 * @return The timezone offset in seconds.
 */
int32_t getTZOffset()
{
#if MESHTASTIC_EXCLUDE_TZ
    return 0;
#else
    return TimeFormatUtils::timezoneOffset(getTime(false));
#endif
}

/**
 * Returns the current time in seconds since the Unix epoch (January 1, 1970).
 *
 * @return The current time in seconds since the Unix epoch.
 */
uint32_t getTime(bool local)
{
    if (local) {
        return (((uint32_t)millis() - timeStartMsec) / 1000) + zeroOffsetSecs + getTZOffset();
    } else {
        return (((uint32_t)millis() - timeStartMsec) / 1000) + zeroOffsetSecs;
    }
}

/**
 * Returns the current time from the RTC if the quality of the time is at least minQuality.
 *
 * @param minQuality The minimum quality of the RTC time required for it to be considered valid.
 * @return The current time from the RTC if it meets the minimum quality requirement, or 0 if the time is not valid.
 */
uint32_t getValidTime(RTCQuality minQuality, bool local)
{
    return (currentQuality >= minQuality) ? getTime(local) : 0;
}

#ifdef PIO_UNIT_TESTING
void setBootRelativeTimeForUnitTest(uint32_t secondsSinceBoot)
{
    currentQuality = RTCQualityNone;
    zeroOffsetSecs = 0;
    timeStartMsec = millis() - (secondsSinceBoot * 1000);
    lastSetFromPhoneNtpOrGps = 0;
    lastTimeValidationWarning = 0;
}

void clearRTCSystemTimeForTests()
{
    hasMockSystemTime = false;
    mockSystemTime = {};
}

void setRTCSystemTimeForTests(const struct timeval *tv)
{
    if (tv == NULL) {
        clearRTCSystemTimeForTests();
        return;
    }
    mockSystemTime = *tv;
    hasMockSystemTime = true;
}

void setReadFromRTCUseSystemTimeForTests(bool enabled)
{
    forceSystemTimeFallback = enabled;
}

void resetRTCStateForTests()
{
    currentQuality = RTCQualityNone;
    timeStartMsec = 0;
    zeroOffsetSecs = 0;
    lastSetFromPhoneNtpOrGps = 0;
    lastTimeValidationWarning = 0;
#if defined(TTGO_T_ECHO_PLUS)
    trustedLocalTime = TrustedTimeAnchor{};
    needsTrustedLocalTime = false;
#if defined(PCF8563_RTC)
    trustedRtcRecord = TrustedRTCRecord{};
    trustedRtcRecordLoaded = false;
    trustedRtcSlot = -1;
    lastHardwareRtcEpoch = 0;
#endif
#endif
    setReadFromRTCUseSystemTimeForTests(false);
    clearRTCSystemTimeForTests();
}
#endif

time_t gm_mktime(const struct tm *tm)
{
#if !MESHTASTIC_EXCLUDE_TZ
    return TimeFormatUtils::utcFromBrokenDown(*tm);
#else
    struct tm tmCopy = *tm;
    return mktime(&tmCopy);
#endif
}
