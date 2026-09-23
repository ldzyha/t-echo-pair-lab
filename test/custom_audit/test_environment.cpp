#include "modules/Telemetry/LocalEnvironmentCache.h"
#include "modules/Telemetry/Sensor/BME280Reading.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <limits>

static uint32_t nowMs;
uint32_t millis()
{
    return nowMs;
}

struct Metrics {
    float temperature = 0;
    float relative_humidity = 0;
    float barometric_pressure = 0;
    bool has_temperature = false;
    bool has_relative_humidity = false;
    bool has_barometric_pressure = false;
};

struct Measurement {
    uint32_t time = 0;
    Metrics metrics;
};

struct Sensor {
    bool conversionSucceeded = true;
    float temperature = 25.2f;
    float humidity = 55.0f;
    float pressurePa = 98300.0f;
    unsigned conversionCount = 0;
    unsigned readCount = 0;
    bool takeForcedMeasurement()
    {
        ++conversionCount;
        return conversionSucceeded;
    }
    float readTemperature()
    {
        ++readCount;
        return temperature;
    }
    float readHumidity()
    {
        ++readCount;
        return humidity;
    }
    float readPressure()
    {
        ++readCount;
        return pressurePa;
    }
};

static void testCacheWithoutTransport()
{
    LocalEnvironmentCache<Measurement> cache;
    Measurement result;
    unsigned samples = 0;
    auto reader = [&samples](Measurement *m) {
        m->time = 1000 + samples;
        m->metrics.temperature = 25.0f + samples;
        ++samples;
        return true;
    };
    nowMs = 0;
    assert(cache.state(nowMs) == LocalEnvironmentState::WAITING);
    assert(!cache.copyFresh(&result, nowMs));
    assert(cache.ageSeconds(nowMs) == UINT32_MAX);

    // No transport or phone is present: sampling is independent of either.
    assert(cache.sampleIfDue(nowMs, reader));
    assert(cache.copyFresh(&result, nowMs));
    assert(result.time == 1000 && samples == 1);
    assert(cache.ageSeconds(nowMs) == 0);
    nowMs = 10000;
    assert(!cache.sampleIfDue(nowMs, reader));
    assert(cache.copyFresh(&result, nowMs));
    assert(result.time == 1000);
    assert(cache.ageSeconds(nowMs) == 10);
    nowMs = 14999;
    assert(!cache.sampleIfDue(nowMs, reader));
    assert(cache.timeUntilAttemptMs(nowMs) == 1);
    nowMs = 15000;
    assert(cache.sampleIfDue(nowMs, reader));
    assert(cache.copyFresh(&result, nowMs));
    assert(result.time == 1001 && result.metrics.temperature == 26 && samples == 2);

    nowMs = 60000;
    assert(cache.state(nowMs) == LocalEnvironmentState::STALE);
    assert(!cache.copyFresh(&result, nowMs));
    assert(cache.ageSeconds(nowMs) == 45);
}

static void testFailureAndRecovery()
{
    LocalEnvironmentCache<Measurement> cache;
    Measurement result;
    bool succeeds = true;
    unsigned attempts = 0;
    auto reader = [&succeeds, &attempts](Measurement *m) {
        ++attempts;
        m->time = 8765;
        return succeeds;
    };
    nowMs = 10;
    assert(cache.sampleIfDue(nowMs, reader));
    nowMs = 15010;
    succeeds = false;
    assert(cache.sampleIfDue(nowMs, reader));
    assert(cache.state(nowMs) == LocalEnvironmentState::READ_ERROR);
    assert(cache.ageSeconds(nowMs) == 15);
    assert(!cache.copyFresh(&result, nowMs));
    nowMs = 30009;
    assert(!cache.sampleIfDue(nowMs, reader));
    assert(attempts == 2);
    assert(cache.timeUntilAttemptMs(nowMs) == 1);
    nowMs = 30010;
    succeeds = true;
    assert(cache.sampleIfDue(nowMs, reader));
    assert(cache.state(nowMs) == LocalEnvironmentState::FRESH);
    assert(cache.ageSeconds(nowMs) == 0 && attempts == 3);

    LocalEnvironmentCache<Measurement> neverSucceeded;
    succeeds = false;
    assert(neverSucceeded.sampleIfDue(nowMs, reader));
    assert(neverSucceeded.state(nowMs) == LocalEnvironmentState::READ_ERROR);
    assert(neverSucceeded.ageSeconds(nowMs) == UINT32_MAX);
    assert(!neverSucceeded.copyFresh(&result, nowMs));
}

static void testRolloverAndClockCorrection()
{
    LocalEnvironmentCache<Measurement> cache;
    Measurement result;
    uint32_t wallClock = 2000000000;
    auto reader = [&wallClock](Measurement *m) {
        m->time = wallClock;
        return true;
    };
    nowMs = UINT32_MAX - 9999;
    assert(cache.sampleIfDue(nowMs, reader));
    nowMs = 0;
    wallClock = 1000;
    assert(!cache.sampleIfDue(nowMs, reader));
    assert(cache.ageSeconds(nowMs) == 10);
    assert(cache.copyFresh(&result, nowMs) && result.time == 2000000000);
    assert(cache.timeUntilAttemptMs(nowMs) == 5000);
    nowMs = 5000;
    assert(cache.sampleIfDue(nowMs, reader));
    assert(cache.copyFresh(&result, nowMs) && result.time == 1000);
    assert(cache.ageSeconds(nowMs) == 0);
}

static void assertRejected(Sensor sensor, float heatC = 0)
{
    Metrics original;
    original.temperature = 1.0f;
    original.relative_humidity = 2.0f;
    original.barometric_pressure = 3.0f;
    original.has_temperature = true;
    assert(!readBme280Metrics(sensor, original, heatC));
    assert(original.temperature == 1.0f && original.relative_humidity == 2.0f && original.barometric_pressure == 3.0f);
    assert(original.has_temperature && !original.has_relative_humidity && !original.has_barometric_pressure);
}

static void testBmeValidationAndAtomicWrite()
{
    Sensor sensor;
    Metrics metrics;
    sensor.conversionSucceeded = false;
    assert(!readBme280Metrics(sensor, metrics));
    assert(sensor.readCount == 0 && sensor.conversionCount == 1);
    assert(!metrics.has_temperature && !metrics.has_relative_humidity && !metrics.has_barometric_pressure);
    sensor.conversionSucceeded = true;
    assert(readBme280Metrics(sensor, metrics));
    assert(metrics.temperature == 25.2f && metrics.relative_humidity == 55.0f && metrics.barometric_pressure == 983.0f);
    assert(metrics.has_temperature && metrics.has_relative_humidity && metrics.has_barometric_pressure);

    for (float value : {-40.1f, 85.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        Sensor bad;
        bad.temperature = value;
        assertRejected(bad);
    }
    for (float value : {-0.1f, 100.1f, std::numeric_limits<float>::quiet_NaN()}) {
        Sensor bad;
        bad.humidity = value;
        assertRejected(bad);
    }
    for (float value : {0.0f, 29999.0f, 110001.0f, std::numeric_limits<float>::infinity()}) {
        Sensor bad;
        bad.pressurePa = value;
        assertRejected(bad);
    }
    assertRejected(Sensor{}, std::numeric_limits<float>::quiet_NaN());
    assertRejected(Sensor{}, 100.0f);

    sensor.temperature = 0.0f;
    sensor.humidity = 100.0f;
    sensor.pressurePa = 30000.0f;
    assert(readBme280Metrics(sensor, metrics));
    assert(metrics.temperature == 0 && metrics.relative_humidity == 100 && metrics.barometric_pressure == 300);
    sensor.temperature = -40;
    assert(readBme280Metrics(sensor, metrics));
    sensor.temperature = 85;
    sensor.pressurePa = 110000;
    assert(readBme280Metrics(sensor, metrics));
}

static void testOptionalCorrection()
{
    Sensor sensor;
    Metrics metrics;
    sensor.temperature = 31.0f;
    sensor.humidity = 35.0f;
    assert(readBme280Metrics(sensor, metrics));
    assert(metrics.temperature == 31.0f && metrics.relative_humidity == 35.0f);
    assert(readBme280Metrics(sensor, metrics, 5.8f));
    assert(std::abs(metrics.temperature - 25.2f) < 0.01f);
    assert(std::abs(metrics.relative_humidity - 49.07f) < 0.02f);
    assert(readBme280Metrics(sensor, metrics, 6.0f));
    assert(std::abs(metrics.temperature - 25.0f) < 0.01f);
    assert(std::abs(metrics.relative_humidity - 49.6624f) < 0.02f);
    assert(metrics.barometric_pressure == 983.0f);
    sensor.humidity = 100;
    assert(readBme280Metrics(sensor, metrics, 5.8f));
    assert(metrics.relative_humidity == 100);
}

static void testRawAndCorrectedSameSample()
{
    Sensor sensor;
    Metrics metrics;
    Bme280RawReading raw{};
    sensor.temperature = 31.0f;
    sensor.humidity = 33.0f;
    assert(readBme280Metrics(sensor, metrics, 6.0f, &raw));
    assert(sensor.conversionCount == 1 && sensor.readCount == 3);
    assert(raw.temperature == 31 && raw.relativeHumidity == 33 && raw.pressureHpa == 983);
    assert(metrics.temperature == 25 && metrics.barometric_pressure == raw.pressureHpa);
    assert(std::abs(metrics.relative_humidity - 46.8245f) < 0.02f);
    const auto previous = metrics;
    sensor.conversionSucceeded = false;
    assert(!readBme280Metrics(sensor, metrics, 6.0f, &raw));
    assert(raw.temperature == 31 && metrics.temperature == previous.temperature);
    sensor.conversionSucceeded = true;
    sensor.temperature = -39;
    assert(!readBme280Metrics(sensor, metrics, 6.0f, &raw));
    assert(raw.temperature == 31 && metrics.temperature == previous.temperature);
}

static void testIndependentCalibration()
{
    const Bme280RawReading raw{25.2f, 55.0f, 983.0f};
    Metrics metrics;
    assert(correctBme280Sample(raw, metrics, 0, {0, 1.1f, 4}));
    assert(metrics.temperature == raw.temperature);
    assert(std::abs(metrics.relative_humidity - 64.5f) < .001f);
    assert(correctBme280Sample(raw, metrics, 0, {0, 1.1f, 4}, 2));
    assert(std::abs(metrics.relative_humidity - 66.5f) < .001f);
    assert(correctBme280Sample(raw, metrics, 0, {0, 1, -70}));
    assert(metrics.relative_humidity == 0);
    assert(correctBme280Sample(raw, metrics, 0, {0, 2, 0}));
    assert(metrics.relative_humidity == 100);
    assert(correctBme280Sample(raw, metrics, 6, {-3, 1, 0}));
    assert(std::abs(metrics.temperature - (raw.temperature - 9)) < .001f);
    assert(correctBme280Sample(raw, metrics, 0, {2, 1, 0}));
    assert(std::abs(metrics.temperature - 27.2f) < .001f);
    assert(correctBme280Sample({39.02f, 29.55f, 985.8f}, metrics, 13.92f, {0, 0.999910f, 0}));
    assert(std::abs(metrics.temperature - 25.1f) < .001f);
    assert(std::abs(metrics.relative_humidity - 65.0f) < .01f);
    const auto previous = metrics;
    assert(!correctBme280Sample(raw, metrics, 0, {0, NAN, 0}));
    assert(!correctBme280Sample(raw, metrics, 0, {0, 0, 0}));
    assert(!correctBme280Sample(raw, metrics, 0, {0, 1, 0}, NAN));
    assert(metrics.temperature == previous.temperature && metrics.relative_humidity == previous.relative_humidity);
    assert(metrics.barometric_pressure == previous.barometric_pressure);
}

int main()
{
    testCacheWithoutTransport();
    testFailureAndRecovery();
    testRolloverAndClockCorrection();
    testBmeValidationAndAtomicWrite();
    testOptionalCorrection();
    testRawAndCorrectedSameSample();
    testIndependentCalibration();
    std::puts("PASS: 7 environment suites (cache, failures, rollover, atomic validation, correction, raw sample, independent "
              "calibration)");
}
