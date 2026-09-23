#pragma once
#include "PairSettings.h"

#include "NodeDB.h"
#include "PeerStatusState.h"

namespace PeerStatus
{
static constexpr uint32_t NODE_A = PAIR_NODE_A;
static constexpr uint32_t NODE_B = PAIR_NODE_B;
static constexpr uint32_t SILENT_AFTER_SECONDS = 5 * 60;

meshtastic_NodeInfoLite *getPeer();
uint32_t ageSeconds(uint32_t timestamp);
uint32_t rxAgeSeconds();
uint32_t positionRxAgeSeconds();
bool recentlyHeard(const meshtastic_NodeInfoLite *peer);
DeliveryState deliveryState();
const char *deliveryLabel();
uint32_t deliveryError();
uint32_t txAgeSeconds();
void recordSent(const meshtastic_MeshPacket &packet);
void recordReceived(const meshtastic_MeshPacket &packet);
} // namespace PeerStatus
