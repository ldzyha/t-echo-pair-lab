#include "OneButton.h"
#include "input/QuickHeart.h"
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <string>

static unsigned long clockMs = 0;
unsigned long millis()
{
    return clockMs;
}
static unsigned singles = 0, doubles = 0, holds = 0;
static unsigned multis = 0, lastClicks = 0;
static OneButton *activeButton = nullptr;

static OneButton buttonAt(unsigned long start)
{
    clockMs = start;
    singles = doubles = holds = 0;
    multis = lastClicks = 0;
    OneButton button;
    button.setDebounceMs(QuickHeart::DEBOUNCE_MS);
    button.setClickMs(QuickHeart::CLICK_WINDOW_MS);
    button.setPressMs(500);
    button.attachClick([]() { ++singles; });
    button.attachDoubleClick([]() { ++doubles; });
    button.attachMultiClick([]() {
        ++multis;
        lastClicks = activeButton->getNumberClicks();
    });
    button.attachLongPressStart([]() { ++holds; });
    button.tick(false);
    return button;
}

static void advance(OneButton &button, bool down, unsigned duration)
{
    activeButton = &button;
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
    size_t previousWidth = 0;
    for (unsigned clicks = 2; clicks <= 5; ++clicks) {
        assert(QuickHeart::prepare(packet, a, b, key.data(), key.size(), clicks));
        const std::string body(reinterpret_cast<const char *>(packet.decoded.payload.bytes), packet.decoded.payload.size);
        assert(body == QuickHeart::textFor(clicks));
        assert(packet.decoded.payload.size <= 192);
        assert(static_cast<unsigned>(std::count(body.begin(), body.end(), '\n')) == 2 * (clicks - 2));
        size_t width = 0, widest = 0;
        for (unsigned char c : body) {
            if (c == '\n') {
                widest = std::max(widest, width);
                width = 0;
            } else if ((c & 0xc0) != 0x80) {
                ++width;
            }
        }
        widest = std::max(widest, width);
        assert(widest > previousWidth);
        previousWidth = widest;
    }
    assert(QuickHeart::textFor(6) == QuickHeart::textFor(5));
    assert(QuickHeart::textFor(255) == QuickHeart::textFor(5));
    assert(!QuickHeart::textFor(1));
    packet = valid;
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

    for (unsigned clicks = 3; clicks <= 6; ++clicks) {
        button = buttonAt(10000 * clicks);
        for (unsigned click = 0; click < clicks; ++click) {
            advance(button, true, 120);
            advance(button, false, 150);
        }
        advance(button, false, 550);
        assert(singles == 0 && doubles == 0 && holds == 0);
        assert(multis == 1 && lastClicks == clicks);
        assert(QuickHeart::textFor(lastClicks));
    }

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
    puts("Quick heart: growing outlines, bounded UTF-8 payload; actual OneButton single/2-6 clicks/hold/bounce/rollover passed");
}
