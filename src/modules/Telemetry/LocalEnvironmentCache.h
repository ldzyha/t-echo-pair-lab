#pragma once

#include "Throttle.h"
#include <cstdint>

enum class LocalEnvironmentState { WAITING, FRESH, STALE, READ_ERROR };

template <typename Measurement> class LocalEnvironmentCache
{
  public:
    static constexpr uint32_t SAMPLE_INTERVAL_MS = 15000;
    static constexpr uint32_t FRESH_FOR_MS = 45000;

    template <typename Reader> bool sampleIfDue(uint32_t nowMs, Reader read)
    {
        if (hasAttempt && Throttle::isWithinTimespanMs(lastAttemptMs, SAMPLE_INTERVAL_MS))
            return false;

        Measurement next{};
        lastAttemptSucceeded = read(&next);
        hasAttempt = true;
        lastAttemptMs = nowMs;
        if (lastAttemptSucceeded) {
            measurement = next;
            sampledAtMs = nowMs;
            hasSample = true;
            hadSuccessfulSample = true;
        } else {
            measurement = Measurement{};
            hasSample = false;
        }
        return true;
    }

    LocalEnvironmentState state(uint32_t nowMs) const
    {
        if (hasAttempt && !lastAttemptSucceeded)
            return LocalEnvironmentState::READ_ERROR;
        if (!hasSample)
            return LocalEnvironmentState::WAITING;
        return nowMs - sampledAtMs < FRESH_FOR_MS ? LocalEnvironmentState::FRESH : LocalEnvironmentState::STALE;
    }

    bool copyFresh(Measurement *out, uint32_t nowMs) const
    {
        if (state(nowMs) != LocalEnvironmentState::FRESH || !hasSample || !out)
            return false;
        *out = measurement;
        return true;
    }

    uint32_t ageSeconds(uint32_t nowMs) const { return hadSuccessfulSample ? (nowMs - sampledAtMs) / 1000 : UINT32_MAX; }

    uint32_t timeUntilAttemptMs(uint32_t nowMs) const
    {
        if (!hasAttempt)
            return 0;
        const uint32_t elapsed = nowMs - lastAttemptMs;
        return elapsed < SAMPLE_INTERVAL_MS ? SAMPLE_INTERVAL_MS - elapsed : 0;
    }

  private:
    Measurement measurement{};
    uint32_t sampledAtMs = 0;
    uint32_t lastAttemptMs = 0;
    bool hasSample = false;
    bool hadSuccessfulSample = false;
    bool hasAttempt = false;
    bool lastAttemptSucceeded = false;
};
