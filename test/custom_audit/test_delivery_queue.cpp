#include "mesh/DeliveryQueueCodec.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace DeliveryQueueCodec;
int main()
{
    const uint8_t input[] = {'T', 0xd0, 0x87, 0xd0, 0x84, 0xd2, 0x90};
    std::array<uint8_t, 256> b{};
    Frame sent{DATA, 0x123456789abcdefULL, 22, 12345, input, sizeof(input)}, received;
    size_t n = encode(b.data(), b.size(), sent);
    assert(n == HEADER_SIZE + sizeof(input));
    assert(decode(b.data(), n, received));
    assert(received.epoch == sent.epoch && received.sequence == sent.sequence && received.originalId == sent.originalId);
    assert(received.size == sizeof(input) && !memcmp(received.body, input, sizeof(input)));
    assert(!encode(b.data(), n - 1, sent));
    for (size_t len = 0; len < HEADER_SIZE; ++len)
        assert(!decode(b.data(), len, received));
    b[4] = 99;
    assert(!decode(b.data(), n, received));
    b[4] = 1;
    b[5] = 99;
    assert(!decode(b.data(), n, received));
    b[5] = RECEIPT;
    assert(!decode(b.data(), n, received));
    Frame ack{RECEIPT, sent.epoch, sent.sequence, sent.originalId, nullptr, 0};
    n = encode(b.data(), b.size(), ack);
    assert(n == HEADER_SIZE && decode(b.data(), n, received));
    assert(receiptMatches(received, sent.epoch, sent.sequence, sent.originalId));
    assert(!receiptMatches(received, sent.epoch + 1, sent.sequence, sent.originalId));
    assert(!receiptMatches(received, sent.epoch, sent.sequence + 1, sent.originalId));
    assert(!receiptMatches(received, sent.epoch, sent.sequence, sent.originalId + 1));
    ack.sequence = 0;
    assert(!encode(b.data(), b.size(), ack));

    assert(isTargetReception(9, 9, true, true, false));
    assert(!isTargetReception(8, 9, true, true, false));
    assert(!isTargetReception(9, 9, true, true, true));
    assert(!isTargetReception(9, 9, true, false, false));
    assert(!isTargetReception(9, 9, false, true, false));
    assert(!isTargetReception(0, 0, true, true, false));
    EventGate events;
    assert(!events.consume(false));
    for (int ticks = 0; ticks < 100000; ++ticks)
        assert(!events.consume(false)); // elapsed time cannot grant a token
    events.initial();
    assert(events.consume(false));
    assert(!events.consume(false));
    events.received(101);
    assert(!events.consume(true));
    assert(events.consume(false));
    events.received(101);
    assert(!events.consume(false));
    events.received(102);
    events.received(101);
    assert(events.consume(false));
    assert(!events.consume(false));
    events.received(0);
    assert(!events.consume(false));
    events.received(103);
    events.received(104);
    assert(events.consume(false));
    assert(!events.consume(false));
    events = {};
    assert(!events.consume(false)); // reboot waits for fresh reception

    assert(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xcbf43926);
    std::array<uint8_t, 96> old{}, next{};
    memcpy(old.data() + SNAPSHOT_HEADER, input, sizeof(input));
    snapshotHeader(old.data(), 7, old.data() + SNAPSHOT_HEADER, sizeof(input));
    size_t total = SNAPSHOT_HEADER + sizeof(input);
    assert(validSnapshot(old.data(), total, old.size()));
    memcpy(next.data() + SNAPSHOT_HEADER, input, sizeof(input));
    snapshotHeader(next.data(), 8, next.data() + SNAPSHOT_HEADER, sizeof(input));
    // Every interrupted write to the alternate slot leaves the previous slot
    // usable.
    for (size_t cut = 0; cut < total; ++cut) {
        assert(!validSnapshot(next.data(), cut, next.size()));
        assert(validSnapshot(old.data(), total, old.size()));
    }
    assert(validSnapshot(next.data(), total, next.size()) && newer(get32(next.data() + 8), get32(old.data() + 8)));
    for (size_t i = 0; i < total; ++i) {
        next[i] ^= 1;
        assert(!validSnapshot(next.data(), total, next.size()));
        next[i] ^= 1;
    }
    next[12] ^= 1;
    assert(!validSnapshot(next.data(), total, next.size()));
    next[12] ^= 1;
    next[4] ^= 1;
    assert(!validSnapshot(next.data(), total, next.size()));
    next[4] ^= 1;
    assert(!validSnapshot(next.data(), total, total - 1));
    assert(newer(1, UINT32_MAX));
    puts("delivery codec, receipt correlation, event gate, snapshot recovery: OK");
}
