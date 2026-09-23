#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace DeliveryQueueCodec
{
constexpr size_t HEADER_SIZE = 22;
constexpr uint8_t DATA = 1, RECEIPT = 2;
struct Frame {
    uint8_t kind = 0;
    uint64_t epoch = 0;
    uint32_t sequence = 0, originalId = 0;
    const uint8_t *body = nullptr;
    size_t size = 0;
};
inline uint32_t crc32Update(uint32_t c, const uint8_t *p, size_t n)
{
    while (n--) {
        c ^= *p++;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c;
}
inline uint32_t crc32(const uint8_t *p, size_t n)
{
    return ~crc32Update(UINT32_MAX, p, n);
}
inline void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        p[i] = v >> (8 * i);
}
inline uint32_t get32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void put64(uint8_t *p, uint64_t v)
{
    put32(p, uint32_t(v));
    put32(p + 4, uint32_t(v >> 32));
}
inline uint64_t get64(const uint8_t *p)
{
    return get32(p) | uint64_t(get32(p + 4)) << 32;
}
inline size_t encode(uint8_t *out, size_t cap, const Frame &f)
{
    if (cap < HEADER_SIZE || f.size > cap - HEADER_SIZE || !f.epoch || !f.sequence || !f.originalId ||
        (f.kind != DATA && f.kind != RECEIPT) || (f.kind == RECEIPT && f.size))
        return 0;
    memcpy(out, "DZQ2", 4);
    out[4] = 1;
    out[5] = f.kind;
    put64(out + 6, f.epoch);
    put32(out + 14, f.sequence);
    put32(out + 18, f.originalId);
    if (f.size)
        memcpy(out + HEADER_SIZE, f.body, f.size);
    return HEADER_SIZE + f.size;
}
inline bool decode(const uint8_t *in, size_t n, Frame &f)
{
    if (n < HEADER_SIZE || memcmp(in, "DZQ2", 4) || in[4] != 1)
        return false;
    f = {in[5], get64(in + 6), get32(in + 14), get32(in + 18), in + HEADER_SIZE, n - HEADER_SIZE};
    return f.epoch && f.sequence && f.originalId && (f.kind == DATA || (f.kind == RECEIPT && f.size == 0));
}
inline bool receiptMatches(const Frame &f, uint64_t epoch, uint32_t sequence, uint32_t originalId)
{
    return f.kind == RECEIPT && f.epoch == epoch && f.sequence == sequence && f.originalId == originalId && f.size == 0;
}
constexpr size_t SNAPSHOT_HEADER = 20;
inline void snapshotHeader(uint8_t *out, uint32_t generation, const uint8_t *body, size_t n)
{
    memcpy(out, "DQS2", 4);
    put32(out + 4, 1);
    put32(out + 8, generation);
    put32(out + 12, uint32_t(n));
    put32(out + 16, ~crc32Update(crc32Update(UINT32_MAX, out, 16), body, n));
}
inline bool validSnapshot(const uint8_t *p, size_t n, size_t cap)
{
    return n >= SNAPSHOT_HEADER && n <= cap && !memcmp(p, "DQS2", 4) && get32(p + 4) == 1 && get32(p + 8) &&
           get32(p + 12) == n - SNAPSHOT_HEADER &&
           get32(p + 16) == ~crc32Update(crc32Update(UINT32_MAX, p, 16), p + SNAPSHOT_HEADER, n - SNAPSHOT_HEADER);
}
inline bool newer(uint32_t a, uint32_t b)
{
    return int32_t(a - b) > 0;
}
inline bool isTargetReception(uint32_t from, uint32_t target, bool decoded, bool lora, bool mqtt)
{
    return target != 0 && from == target && decoded && lora && !mqtt;
}
struct EventGate {
    uint32_t token = 0, consumed = 0, recentIds[16] = {};
    size_t next = 0;
    void received(uint32_t id)
    {
        if (!id)
            return;
        for (uint32_t old : recentIds)
            if (old == id)
                return;
        recentIds[next] = id;
        next = (next + 1) % 16;
        ++token;
    }
    bool consume(bool busy)
    {
        if (busy || token == consumed)
            return false;
        consumed = token;
        return true;
    }
    void initial() { ++token; }
};
} // namespace DeliveryQueueCodec
