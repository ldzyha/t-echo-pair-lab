#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

struct Bme280ThermalPoint {
    float dieC;
    float heatC;
    float humidityGain;
    float humidityOffsetPercent;
};

// An empirical board-temperature profile, valid only under its calibration conditions.
class Bme280ThermalModel
{
  public:
    static constexpr float DIE_FILTER_MS = 30000.0f;

    Bme280ThermalModel(const Bme280ThermalPoint *points, size_t count) : points(points), count(count) {}

    float update(uint32_t nowMs, float dieC)
    {
        active = false;
        if (!validProfile() || !std::isfinite(dieC) || dieC < -40 || dieC > 85) {
            started = false;
            return NAN;
        }
        if (!started) {
            filteredDieC = dieC;
        } else {
            const uint32_t elapsed = nowMs - lastMs;
            filteredDieC = dieC + (filteredDieC - dieC) * std::exp(-static_cast<float>(elapsed) / DIE_FILTER_MS);
        }
        started = true;
        lastMs = nowMs;

        size_t upper = 1;
        while (upper + 1 < count && filteredDieC > points[upper].dieC)
            ++upper;
        const auto &a = points[upper - 1];
        const auto &b = points[upper];
        // Keep the endpoint correction outside the profile range instead of extrapolating a high-gain curve.
        const float fraction = std::fmin(1.0f, std::fmax(0.0f, (filteredDieC - a.dieC) / (b.dieC - a.dieC)));
        heatC = a.heatC + fraction * (b.heatC - a.heatC);
        humidityGain = a.humidityGain + fraction * (b.humidityGain - a.humidityGain);
        humidityOffset = a.humidityOffsetPercent + fraction * (b.humidityOffsetPercent - a.humidityOffsetPercent);
        range = filteredDieC < points[0].dieC ? -1 : filteredDieC > points[count - 1].dieC ? 1 : 0;
        active = true;
        return heatC;
    }

    float getHumidityGain() const { return humidityGain; }
    float getHumidityCorrection() const { return humidityOffset; }
    float getFilteredDieC() const { return filteredDieC; }
    int getRange() const { return range; }
    bool isActive() const { return active; }

  private:
    bool validProfile() const
    {
        if (!points || count < 2)
            return false;
        for (size_t i = 0; i < count; ++i) {
            const auto &p = points[i];
            if (!std::isfinite(p.dieC) || p.dieC < -40 || p.dieC > 85 || !std::isfinite(p.heatC) || p.heatC < 0 ||
                p.heatC > 125 || !std::isfinite(p.humidityGain) || p.humidityGain <= 0 ||
                !std::isfinite(p.humidityOffsetPercent) || (i && p.dieC <= points[i - 1].dieC))
                return false;
        }
        return true;
    }

    const Bme280ThermalPoint *points;
    size_t count;
    float filteredDieC = NAN;
    float heatC = 0;
    float humidityGain = 1;
    float humidityOffset = 0;
    uint32_t lastMs = 0;
    int range = 0;
    bool started = false;
    bool active = false;
};
