#pragma once
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace QuickHeart
{
constexpr uint16_t CLICK_WINDOW_MS = 400;
constexpr int16_t DEBOUNCE_MS = 30;
constexpr char TEXT[] = u8"\u2764\uFE0F";
constexpr char SMALL[] = u8"❤❤\u2003 ❤❤\n\u2003 ❤\u2003 ❤\n\u2003\u2003  ❤";
constexpr char MEDIUM[] = u8"\u2003 ❤\u2003\u2003\u2003   ❤\n❤\u2003 ❤\u2003 ❤\u2003 ❤\n❤\u2003\u2003\u2003\u2003\u2003\u2003 "
                          u8"❤\n\u2003 ❤\u2003\u2003\u2003   ❤\n\u2003\u2003\u2003   ❤";
constexpr char LARGE[] =
    u8"\u2003   ❤❤\u2003\u2003  ❤❤\n❤\u2003\u2003 ❤\u2003 ❤\u2003\u2003 ❤\n❤\u2003\u2003\u2003\u2003\u2003\u2003\u2003\u2003  "
    u8"❤\n\u2003 ❤\u2003\u2003\u2003\u2003\u2003\u2003❤\n\u2003\u2003  ❤\u2003\u2003\u2003  ❤\n\u2003\u2003\u2003   ❤\u2003 "
    u8"❤\n\u2003\u2003\u2003\u2003\u2003❤";
static_assert(sizeof(LARGE) - 1 <= 187, "Keep room for the encrypted envelope and decoded destination");

inline const char *textFor(unsigned clicks)
{
    return clicks < 2 ? nullptr : clicks == 2 ? TEXT : clicks == 3 ? SMALL : clicks == 4 ? MEDIUM : LARGE;
}

inline uint32_t targetFor(uint32_t self, uint32_t nodeA, uint32_t nodeB)
{
    if (!nodeA || !nodeB || nodeA == UINT32_MAX || nodeB == UINT32_MAX || nodeA == nodeB)
        return 0;
    return self == nodeA ? nodeB : self == nodeB ? nodeA : 0;
}

inline bool prepare(meshtastic_MeshPacket &packet, uint32_t self, uint32_t target, const uint8_t *key, size_t keySize,
                    unsigned clicks = 2)
{
    const char *text = textFor(clicks);
    if (!text)
        return false;
    if (!packet.id || !self || self == UINT32_MAX || !target || target == UINT32_MAX || target == self || !key || keySize != 32)
        return false;
    bool keyPresent = false;
    for (size_t i = 0; i < keySize; ++i)
        keyPresent |= key[i] != 0;
    if (!keyPresent)
        return false;
    packet.from = self;
    packet.to = target;
    packet.channel = 0;
    packet.want_ack = false;
    packet.pki_encrypted = true;
    packet.public_key.size = keySize;
    memcpy(packet.public_key.bytes, key, keySize);
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded = meshtastic_Data_init_default;
    packet.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet.decoded.dest = target;
    packet.decoded.payload.size = strlen(text);
    memcpy(packet.decoded.payload.bytes, text, packet.decoded.payload.size);
    return true;
}

void send(unsigned clicks = 2);
} // namespace QuickHeart
