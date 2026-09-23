#pragma once

#include <cstdint>

namespace PeerStatus
{
enum class DeliveryState : uint8_t { NONE, WAITING, CONFIRMED, NO_CONFIRMATION, NOT_REQUESTED };

struct ReceivedEvent {
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t requestId = 0;
    uint32_t error = 0;
    bool decoded = false;
    bool lora = false;
    bool viaMqtt = false;
    bool local = false;
    bool routing = false;
    bool position = false;
};

inline uint32_t reportedAgeSeconds(uint32_t now, uint32_t timestamp)
{
    return !now || !timestamp || timestamp > now ? UINT32_MAX : now - timestamp;
}

class SessionState
{
  public:
    void selectPeer(uint32_t id)
    {
        if (id != peerId) {
            *this = SessionState{};
            peerId = id;
        }
    }

    void sent(uint32_t id, bool wantAck, uint32_t now)
    {
        if (!peerId || !id)
            return;
        txId = id;
        txMs = now;
        error = 0;
        delivery = wantAck ? DeliveryState::WAITING : DeliveryState::NOT_REQUESTED;
    }

    void received(const ReceivedEvent &event, uint32_t self, uint32_t now)
    {
        if (!peerId || !event.decoded || event.viaMqtt)
            return;
        const bool fromPeer = event.from == peerId && event.lora;
        if (fromPeer) {
            hasRx = true;
            rxMs = now;
            if (event.position) {
                hasPositionRx = true;
                positionRxMs = now;
            }
        }
        if (!txId || !event.routing || event.requestId != txId || event.to != self)
            return;
        if (fromPeer && event.error == 0) {
            delivery = DeliveryState::CONFIRMED;
            error = 0;
        } else if (delivery != DeliveryState::CONFIRMED && event.error != 0 &&
                   (fromPeer || (event.from == self && event.local && !event.lora))) {
            delivery = DeliveryState::NO_CONFIRMATION;
            error = event.error;
        }
    }

    uint32_t rxAgeSeconds(uint32_t now) const { return hasRx ? (uint32_t)(now - rxMs) / 1000 : UINT32_MAX; }
    uint32_t positionRxAgeSeconds(uint32_t now) const
    {
        return hasPositionRx ? (uint32_t)(now - positionRxMs) / 1000 : UINT32_MAX;
    }
    uint32_t txAgeSeconds(uint32_t now) const { return txId ? (uint32_t)(now - txMs) / 1000 : UINT32_MAX; }
    DeliveryState deliveryState(uint32_t now) const
    {
        return delivery == DeliveryState::WAITING && txAgeSeconds(now) >= 180 ? DeliveryState::NO_CONFIRMATION : delivery;
    }
    uint32_t deliveryError() const { return error; }

  private:
    uint32_t peerId = 0;
    uint32_t rxMs = 0;
    uint32_t txMs = 0;
    uint32_t positionRxMs = 0;
    uint32_t txId = 0;
    uint32_t error = 0;
    bool hasRx = false;
    bool hasPositionRx = false;
    DeliveryState delivery = DeliveryState::NONE;
};
} // namespace PeerStatus
