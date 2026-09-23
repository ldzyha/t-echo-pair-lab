#include "mesh/PeerStatusState.h"
#include <cassert>
#include <cstdio>

using namespace PeerStatus;

int main()
{
    constexpr uint32_t self = 10, peer = 20;
    SessionState state;
    state.selectPeer(peer);
    assert(state.rxAgeSeconds(1000) == UINT32_MAX);
    assert(state.deliveryState(1000) == DeliveryState::NONE);
    assert(reportedAgeSeconds(100, 101) == UINT32_MAX);
    assert(reportedAgeSeconds(0, 10) == UINT32_MAX);
    assert(reportedAgeSeconds(100, 90) == 10);

    ReceivedEvent event;
    event.from = peer;
    event.to = self;
    event.decoded = true;
    state.received(event, self, 1000);
    assert(state.rxAgeSeconds(1000) == UINT32_MAX);
    event.lora = true;
    event.viaMqtt = true;
    state.received(event, self, 2000);
    assert(state.rxAgeSeconds(2000) == UINT32_MAX);
    event.viaMqtt = false;
    event.decoded = false;
    state.received(event, self, 3000);
    assert(state.rxAgeSeconds(3000) == UINT32_MAX);
    event.decoded = true;
    state.received(event, self, 4000);
    assert(state.rxAgeSeconds(24000) == 20);
    assert(state.deliveryState(24000) == DeliveryState::NONE);
    assert(state.positionRxAgeSeconds(24000) == UINT32_MAX);
    event.position = true;
    state.received(event, self, 4100);
    assert(state.positionRxAgeSeconds(24100) == 20);
    event.position = false;

    state.sent(111, true, 5000);
    event.routing = true;
    event.requestId = 112;
    state.received(event, self, 6000);
    assert(state.deliveryState(6000) == DeliveryState::WAITING);
    event.requestId = 111;
    event.from = self;
    event.lora = false;
    event.local = true;
    state.received(event, self, 7000);
    assert(state.deliveryState(7000) == DeliveryState::WAITING);
    assert(state.rxAgeSeconds(7000) == 1); // Wrong-request peer packet was a real RX.
    event.from = 30;
    event.lora = true;
    event.local = false;
    state.received(event, self, 8000);
    assert(state.deliveryState(8000) == DeliveryState::WAITING);
    event.from = peer;
    event.to = 99;
    state.received(event, self, 9000);
    assert(state.deliveryState(9000) == DeliveryState::WAITING);
    event.to = self;
    state.received(event, self, 10000);
    assert(state.deliveryState(10000) == DeliveryState::CONFIRMED);

    state.sent(222, true, 11000);
    event.requestId = 111;
    state.received(event, self, 12000);
    assert(state.deliveryState(12000) == DeliveryState::WAITING);
    event.requestId = 222;
    event.from = self;
    event.lora = false;
    event.local = true;
    event.error = 5;
    state.received(event, self, 13000);
    assert(state.deliveryState(13000) == DeliveryState::NO_CONFIRMATION);
    assert(state.deliveryError() == 5);
    event.from = peer;
    event.lora = true;
    event.local = false;
    event.error = 0;
    state.received(event, self, 14000);
    assert(state.deliveryState(14000) == DeliveryState::CONFIRMED);
    assert(state.deliveryError() == 0);
    event.from = self;
    event.local = true;
    event.lora = false;
    event.error = 5;
    state.received(event, self, 15000);
    assert(state.deliveryState(15000) == DeliveryState::CONFIRMED);

    state.sent(333, true, 16000);
    assert(state.deliveryState(196000) == DeliveryState::NO_CONFIRMATION);
    event.from = peer;
    event.local = false;
    event.lora = true;
    event.requestId = 333;
    event.error = 0;
    state.received(event, self, 197000);
    assert(state.deliveryState(197000) == DeliveryState::CONFIRMED);
    state.sent(444, false, 200000);
    assert(state.deliveryState(300000) == DeliveryState::NOT_REQUESTED);

    state.selectPeer(30);
    assert(state.rxAgeSeconds(300000) == UINT32_MAX);
    assert(state.deliveryState(300000) == DeliveryState::NONE);
    state.selectPeer(peer);
    state.received(event, self, 0xfffffc18U);
    assert(state.rxAgeSeconds(1000) == 2); // millis wrap, not wall time.
    state.selectPeer(0);
    state.received(event, self, 1000);
    assert(state.rxAgeSeconds(2000) == UINT32_MAX);
    std::puts("peer status: all assertions passed");
}
