#pragma once

#include <ErriezCRC32.h>
#include <cstddef>
#include <cstdint>

struct TrustedRTCRecord {
    static constexpr uint32_t MAGIC = 0x54494d31;
    uint32_t magic = MAGIC;
    uint32_t generation = 0;
    uint32_t minimumEpoch = 0;
    uint32_t checksum = 0;

    void seal() { checksum = crc32Buffer(this, offsetof(TrustedRTCRecord, checksum)); }
    bool valid() const
    {
        return magic == MAGIC && generation != 0 && minimumEpoch != 0 &&
               checksum == crc32Buffer(this, offsetof(TrustedRTCRecord, checksum));
    }
    bool acceptsRTC(uint32_t epoch, bool integrityGood) const { return valid() && integrityGood && epoch >= minimumEpoch; }
};
static_assert(sizeof(TrustedRTCRecord) == 16, "RTC marker format must remain stable");
