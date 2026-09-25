#include "modules/Telemetry/Sensor/BME280ThermalProfile.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static void near(float actual, float expected, float tolerance = .002f)
{
    assert(std::isfinite(actual));
    assert(std::abs(actual - expected) < tolerance);
}

static constexpr Bme280ThermalPoint points[] = {{30, 6, 1.3f, 2}, {35, 10, 1.1f, 1}, {40, 14, 1, 0}};

static void testInterpolationAndRestart()
{
    Bme280ThermalModel a(points, 3);
    near(a.update(0, 37.5f), 12);
    near(a.getHumidityGain(), 1.05f);
    near(a.getHumidityCorrection(), .5f);
    // A warm restart must not restart a zero-to-hot ramp.
    Bme280ThermalModel restarted(points, 3);
    near(restarted.update(15000, 37.5f), 12);
    near(a.update(300000, 37.5f), 12);
    near(restarted.getHumidityGain(), a.getHumidityGain());
}

static void testCoolingAndRollover()
{
    Bme280ThermalModel a(points, 3);
    const uint32_t start = UINT32_MAX - 10000;
    near(a.update(start, 40), 14);
    const float first = a.update(start + uint32_t(15000), 30);
    assert(first < 14 && first > 6);
    const float second = a.update(start + uint32_t(30000), 30);
    assert(second < first && second > 6);
    assert(a.getHumidityGain() > 1);
    near(a.update(start + uint32_t(900000), 30), 6);
    near(a.getHumidityGain(), 1.3f);
    near(a.update(start + uint32_t(1800000), 40), 14);
}

static void testRangeAndInvalidRecovery()
{
    Bme280ThermalModel low(points, 3), high(points, 3);
    near(low.update(0, -20), 6);
    assert(low.getRange() == -1);
    near(high.update(0, 70), 14);
    assert(high.getRange() == 1);
    assert(std::isnan(high.update(1000, NAN)) && !high.isActive());
    near(high.update(2000, 30), 6);
    assert(high.getRange() == 0);
    assert(std::isnan(high.update(3000, 100)));
    const Bme280ThermalPoint duplicate[] = {{30, 6, 1, 0}, {30, 7, 1, 0}};
    Bme280ThermalModel bad(duplicate, 2), empty(nullptr, 0);
    assert(std::isnan(bad.update(0, 30)));
    assert(std::isnan(empty.update(0, 30)));
    const Bme280ThermalPoint invalid[] = {{30, 6, 1, 0}, {40, 14, NAN, 0}};
    Bme280ThermalModel badGain(invalid, 2);
    assert(std::isnan(badGain.update(0, 35)));
}

static void testCalibrationReplay()
{
    struct Metrics {
        float temperature, relative_humidity, barometric_pressure;
        bool has_temperature, has_relative_humidity, has_barometric_pressure;
    } m{};
    Bme280ThermalModel a(T_ECHO_BME280_THERMAL_POINTS, T_ECHO_BME280_THERMAL_POINT_COUNT);
    float heat = a.update(0, 38.5f);
    auto calibration = T_ECHO_BME280_B.calibration;
    calibration.humidityGain *= a.getHumidityGain();
    assert(correctBme280Sample({37.42f, 32.05f, 985.93f}, m, heat, calibration));
    near(m.temperature, 24.8f);
    near(m.relative_humidity, 67, .02f);
    Bme280ThermalModel hot(T_ECHO_BME280_THERMAL_POINTS, T_ECHO_BME280_THERMAL_POINT_COUNT);
    heat = hot.update(0, 40.333333f);
    calibration.humidityGain = hot.getHumidityGain();
    assert(correctBme280Sample({39.046667f, 29.81f, 985.8f}, m, heat, calibration));
    near(m.temperature, 25.1f);
    near(m.relative_humidity, 65, .05f);
    assert(m.barometric_pressure == 985.8f);
    // Outside the fitted range, new raw changes still reach the output; ambient is never replaced by a target.
    Bme280ThermalModel cold(T_ECHO_BME280_THERMAL_POINTS, T_ECHO_BME280_THERMAL_POINT_COUNT);
    heat = cold.update(0, 20);
    assert(correctBme280Sample({19.1f, 50, 990}, m, heat));
    near(m.temperature, 13.09f);
    assert(correctBme280Sample({18.1f, 50, 990}, m, heat));
    near(m.temperature, 12.09f);
}

static void testDeviceSelectionAndIntermediateSample()
{
    const auto *radioB = bme280ProfileForNode(0x55667788);
    const auto *radioA = bme280ProfileForNode(0x11223344);
    assert(radioB == &T_ECHO_BME280_B && radioA == &T_ECHO_BME280_A);
    assert(!bme280ProfileForNode(0) && !bme280ProfileForNode(0x12345678));
    struct Metrics {
        float temperature, relative_humidity, barometric_pressure;
        bool has_temperature, has_relative_humidity, has_barometric_pressure;
    } m{}, n{};
    Bme280ThermalModel a(T_ECHO_BME280_THERMAL_POINTS, T_ECHO_BME280_THERMAL_POINT_COUNT);
    const float heat = a.update(0, 32.545f);
    auto calibration = radioB->calibration;
    calibration.humidityGain *= a.getHumidityGain();
    // A measurement held out of the fit catches regressions in the intermediate-temperature region.
    assert(correctBme280Sample({32.54f, 34.23f, 985.46f}, m, heat, calibration));
    near(m.temperature, 25.5f, .3f);
    near(m.relative_humidity, 61, 1);
    calibration = radioA->calibration;
    calibration.humidityGain *= a.getHumidityGain();
    assert(correctBme280Sample({32.54f, 34.23f, 985.46f}, n, heat, calibration));
    near(n.temperature - m.temperature, radioA->calibration.temperatureOffsetC);
    assert(n.relative_humidity != m.relative_humidity);
    near(n.barometric_pressure, m.barometric_pressure);
}

static void testIndependentWarmExtension()
{
    const auto *radioB = bme280ProfileForNode(0x55667788);
    const auto *radioA = bme280ProfileForNode(0x11223344);
    assert(radioB && radioA);
    // The new observation must not recalibrate the other physical sensor.
    for (float die = 20; die <= 55; die += .25f) {
        Bme280ThermalModel old(T_ECHO_BME280_THERMAL_POINTS, T_ECHO_BME280_THERMAL_POINT_COUNT);
        Bme280ThermalModel a(radioA->points, radioA->count);
        near(a.update(0, die), old.update(0, die));
        near(a.getHumidityGain(), old.getHumidityGain());
        if (die <= 40.333333f) {
            Bme280ThermalModel b(radioB->points, radioB->count);
            near(b.update(0, die), old.update(0, die));
            near(b.getHumidityGain(), old.getHumidityGain());
        }
    }

    struct Metrics {
        float temperature, relative_humidity, barometric_pressure;
        bool has_temperature, has_relative_humidity, has_barometric_pressure;
    } m{};
    Bme280ThermalModel b(radioB->points, radioB->count);
    const float heat = b.update(0, 45.315f);
    auto calibration = radioB->calibration;
    calibration.humidityGain *= b.getHumidityGain();
    assert(correctBme280Sample({43.27f, 19.84f, 990.13f}, m, heat, calibration));
    near(m.temperature, 26.4f);
    near(m.relative_humidity, 38, .01f);
    near(m.barometric_pressure, 990.13f);
    // A changed raw conversion must reach the output, even at a fitted knot.
    assert(correctBme280Sample({44.27f, 20.84f, 991.13f}, m, heat, calibration));
    near(m.temperature, 27.4f);
    assert(m.relative_humidity > 38);
    near(m.barometric_pressure, 991.13f);

    Bme280ThermalModel middle(radioB->points, radioB->count);
    near(middle.update(0, (40.333333f + 45.315f) / 2), (13.946667f + 16.87f) / 2);
    near(middle.getHumidityGain(), (.989799f + .750614f) / 2);
    Bme280ThermalModel high(radioB->points, radioB->count);
    near(high.update(0, 60), 16.87f);
    near(high.getHumidityGain(), .750614f);
    assert(high.getRange() == 1);
}

int main()
{
    testInterpolationAndRestart();
    testCoolingAndRollover();
    testRangeAndInvalidRecovery();
    testCalibrationReplay();
    testDeviceSelectionAndIntermediateSample();
    testIndependentWarmExtension();
    std::puts("PASS: board intervals, independent RH, warm restart, cooling, rollover, invalid recovery, profile identity, "
              "held-out replay, separate warm extension");
}
