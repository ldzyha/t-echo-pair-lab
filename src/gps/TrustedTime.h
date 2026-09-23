#pragma once

#include <cstdint>

inline bool isTrustedLocalTimeSource(uint32_t from, uint32_t ownNode, bool viaMqtt, bool internalTransport)
{
    return internalTransport && !viaMqtt && (from == 0 || from == ownNode);
}

class TrustedTimeAnchor
{
  public:
    static constexpr uint32_t GPS_TOLERANCE_SECONDS = 300;

    void set(uint32_t epoch, uint32_t nowMs)
    {
        valid = true;
        epochSeconds = epoch;
        previousMs = nowMs;
        elapsedMs = 0;
    }

    bool accepts(uint32_t candidateEpoch, uint32_t nowMs)
    {
        if (!valid)
            return true;
        elapsedMs += static_cast<uint32_t>(nowMs - previousMs);
        previousMs = nowMs;
        const int64_t expected = static_cast<int64_t>(epochSeconds) + static_cast<int64_t>(elapsedMs / 1000);
        const int64_t difference = static_cast<int64_t>(candidateEpoch) - expected;
        return difference >= -static_cast<int64_t>(GPS_TOLERANCE_SECONDS) && difference <= GPS_TOLERANCE_SECONDS;
    }

  private:
    uint32_t epochSeconds = 0;
    uint32_t previousMs = 0;
    uint64_t elapsedMs = 0;
    bool valid = false;
};
