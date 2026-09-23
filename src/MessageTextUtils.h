#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace MessageTextUtils
{
template <typename Messages, typename Predicate, typename BeforeErase>
void eraseOldestMatching(Messages &messages, Predicate predicate, BeforeErase beforeErase)
{
    const auto found = std::find_if(messages.begin(), messages.end(), predicate);
    if (found != messages.end()) {
        beforeErase(*found);
        messages.erase(found);
    }
}

template <typename Messages, typename Predicate> void eraseOldestMatching(Messages &messages, Predicate predicate)
{
    eraseOldestMatching(messages, predicate, [](const auto &) {});
}

template <size_t Slots, typename Messages> uint16_t availableTextSlot(const Messages &messages, size_t slotSize)
{
    bool occupied[Slots] = {};
    for (const auto &message : messages) {
        const size_t slot = message.textOffset / slotSize;
        if (slot < Slots)
            occupied[slot] = true;
    }
    for (size_t slot = 0; slot < Slots; ++slot)
        if (!occupied[slot])
            return slot * slotSize;
    return messages.front().textOffset; // The next insertion evicts this oldest record.
}

inline size_t completeUtf8Prefix(const char *text, size_t length)
{
    if (!length)
        return 0;
    size_t start = length - 1;
    while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80)
        --start;
    const unsigned char lead = text[start];
    const size_t width = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 1;
    return length - start < width ? start : length;
}

inline bool matchesThread(uint32_t sender, uint32_t destination, uint8_t channel, int selectedChannel, uint32_t peer)
{
    const bool direct = destination != 0 && destination != UINT32_MAX;
    if (peer)
        return direct && (sender == peer || destination == peer);
    if (selectedChannel >= 0)
        return !direct && channel == selectedChannel;
    return true;
}
} // namespace MessageTextUtils
