#include "OneButton.h"
#include "input/QuickHeart.h"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>

static unsigned long clockMs = 0;
unsigned long millis()
{
    return clockMs;
}
static unsigned singles = 0, doubles = 0, holds = 0;

static OneButton buttonAt(unsigned long start)
{
    clockMs = start;
    singles = doubles = holds = 0;
    OneButton button;
    button.setDebounceMs(QuickHeart::DEBOUNCE_MS);
    button.setClickMs(QuickHeart::CLICK_WINDOW_MS);
    button.setPressMs(500);
    button.attachClick([]() { ++singles; });
    button.attachDoubleClick([]() { ++doubles; });
    button.attachLongPressStart([]() { ++holds; });
    button.tick(false);
    return button;
}

static void advance(OneButton &button, bool down, unsigned duration)
{
    for (unsigned i = 0; i < duration; i += 5) {
        clockMs += 5;
        button.tick(down);
    }
}

int main()
{
    constexpr uint32_t a = 0x11223344, b = 0x55667788;
    assert(QuickHeart::targetFor(a, a, b) == b);
    assert(QuickHeart::targetFor(b, a, b) == a);
    for (auto self : {0u, 123u, UINT32_MAX})
        assert(QuickHeart::targetFor(self, a, b) == 0);
    assert(QuickHeart::targetFor(a, a, a) == 0);
    assert(QuickHeart::targetFor(a, a, 0) == 0);
    assert(QuickHeart::targetFor(a, a, UINT32_MAX) == 0);
    std::array<uint8_t, 32> key{};
    key.fill(0x5a);
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
    packet.id = 42;
    assert(QuickHeart::prepare(packet, a, b, key.data(), key.size()));
    assert(packet.id == 42 && packet.from == a && packet.to == b && packet.channel == 0);
    assert(packet.pki_encrypted && packet.public_key.size == 32 && !packet.want_ack);
    assert(packet.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && packet.decoded.dest == b);
    const uint8_t heart[] = {0xe2, 0x9d, 0xa4, 0xef, 0xb8, 0x8f};
    assert(packet.decoded.payload.size == sizeof(heart));
    assert(!memcmp(packet.decoded.payload.bytes, heart, sizeof(heart)));
    assert(!packet.decoded.reply_id && !packet.decoded.want_response);
    const auto valid = packet;
    assert(!QuickHeart::prepare(packet, a, UINT32_MAX, key.data(), key.size()));
    assert(!memcmp(&packet, &valid, sizeof(packet)));
    assert(!QuickHeart::prepare(packet, a, a, key.data(), key.size()));
    assert(!QuickHeart::prepare(packet, a, b, nullptr, 32));
    assert(!QuickHeart::prepare(packet, a, b, key.data(), 31));
    key.fill(0);
    assert(!QuickHeart::prepare(packet, a, b, key.data(), 32));

    auto button = buttonAt(1000);
    advance(button, true, 120);
    advance(button, false, 550);
    assert(singles == 1 && doubles == 0 && holds == 0);

    button = buttonAt(2000);
    advance(button, true, 120);
    advance(button, false, 150);
    advance(button, true, 120);
    advance(button, false, 550);
    assert(singles == 0 && doubles == 1 && holds == 0);

    button = buttonAt(3000);
    advance(button, true, 1000);
    advance(button, false, 550);
    assert(singles == 0 && doubles == 0 && holds == 1);

    button = buttonAt(4000);
    advance(button, true, 120);
    advance(button, false, 150);
    advance(button, true, 1000);
    advance(button, false, 550);
    assert(singles == 0 && doubles == 0 && holds == 1);

    button = buttonAt(5000);
    for (unsigned i = 0; i < 4; ++i) {
        advance(button, true, 5);
        advance(button, false, 5);
    }
    advance(button, true, 120);
    for (unsigned i = 0; i < 4; ++i) {
        advance(button, false, 5);
        advance(button, true, 5);
    }
    advance(button, false, 550);
    assert(singles == 1 && doubles == 0 && holds == 0);

    button = buttonAt(ULONG_MAX - 200);
    advance(button, true, 120);
    advance(button, false, 150);
    advance(button, true, 120);
    advance(button, false, 550);
    assert(singles == 0 && doubles == 1 && holds == 0);
    puts("Quick heart: paired encrypted UTF-8 payload; actual OneButton single/double/hold/bounce/rollover passed");
}
