#pragma once
#include "mesh/PairSettings.h"

#include "BME280Reading.h"
#include "BME280ThermalModel.h"

// TH4 room experiment near 25 C; other ambient temperatures and cooling remain unvalidated.
constexpr Bme280ThermalPoint T_ECHO_BME280_THERMAL_POINTS[] = {
    {30.500000f, 6.010000f, 1.234997f, 0.0f},  {32.232000f, 6.845000f, 1.182214f, 0.0f},
    {33.296667f, 7.166667f, 1.175090f, 0.0f},  {34.420500f, 7.935000f, 1.087115f, 0.0f},
    {35.173500f, 8.830000f, 1.111829f, 0.0f},  {36.169000f, 9.535000f, 1.089149f, 0.0f},
    {38.500000f, 12.620000f, 1.017910f, 0.0f}, {40.333333f, 13.946667f, 0.989799f, 0.0f},
};
constexpr size_t T_ECHO_BME280_THERMAL_POINT_COUNT =
    sizeof(T_ECHO_BME280_THERMAL_POINTS) / sizeof(T_ECHO_BME280_THERMAL_POINTS[0]);

struct Bme280ThermalProfile {
    const char *name;
    Bme280SensorCalibration calibration;
};

constexpr Bme280ThermalProfile T_ECHO_BME280_B{"TH4-B", {0.0f, 1.0f, 0.0f}};
// Radio A has one warming run; the shared curve shape is provisional for this device.
constexpr Bme280ThermalProfile T_ECHO_BME280_A{"TH4-A", {0.764661f, 1.094644f, 0.0f}};

inline const Bme280ThermalProfile *bme280ProfileForNode(uint32_t nodeNum)
{
    if (!PAIR_ENABLE_THERMAL_EXPERIMENTAL)
        return nullptr;
    if (PAIR_NODE_A && nodeNum == PAIR_NODE_A)
        return &T_ECHO_BME280_A;
    if (PAIR_NODE_B && nodeNum == PAIR_NODE_B)
        return &T_ECHO_BME280_B;
    return nullptr;
}
