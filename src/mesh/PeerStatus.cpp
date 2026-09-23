#include "configuration.h"
#if defined(TTGO_T_ECHO_PLUS)
#include "PeerStatus.h"
#include "gps/RTC.h"
#include "mesh-pb-constants.h"
#include "meshUtils.h"

namespace PeerStatus
{
static SessionState session;

meshtastic_NodeInfoLite *getPeer()
{
    if (!nodeDB)
        return nullptr;
    const uint32_t self = nodeDB->getNodeNum();
    const uint32_t desired = self == NODE_A ? NODE_B : self == NODE_B ? NODE_A : 0;
    if (desired) {
        auto *peer = nodeDB->getMeshNode(desired);
        if (peer && !peer->is_ignored)
            return peer;
    }
    for (size_t i = 0; i < nodeDB->getNumMeshNodes(); ++i) {
        auto *peer = nodeDB->getMeshNodeByIndex(i);
        if (peer && peer->num != self && peer->is_favorite && !peer->is_ignored)
            return peer;
    }
    return nullptr;
}

static void selectPeer()
{
    const auto *peer = getPeer();
    session.selectPeer(peer ? peer->num : 0);
}

uint32_t ageSeconds(uint32_t timestamp)
{
    return reportedAgeSeconds(getTime(), timestamp);
}

uint32_t rxAgeSeconds()
{
    selectPeer();
    return session.rxAgeSeconds(millis());
}

uint32_t positionRxAgeSeconds()
{
    selectPeer();
    return session.positionRxAgeSeconds(millis());
}

bool recentlyHeard(const meshtastic_NodeInfoLite *peer)
{
    const auto *selected = getPeer();
    return peer && selected && selected->num == peer->num && rxAgeSeconds() < SILENT_AFTER_SECONDS;
}

DeliveryState deliveryState()
{
    selectPeer();
    return session.deliveryState(millis());
}

const char *deliveryLabel()
{
    switch (deliveryState()) {
    case DeliveryState::WAITING:
        return "DM: wait ACK";
    case DeliveryState::CONFIRMED:
        return "DM: radio ACK";
    case DeliveryState::NO_CONFIRMATION:
        return "DM: no ACK";
    case DeliveryState::NOT_REQUESTED:
        return "DM: ACK off";
    default:
        return "DM: no send";
    }
}

uint32_t deliveryError()
{
    selectPeer();
    return session.deliveryError();
}

uint32_t txAgeSeconds()
{
    selectPeer();
    return session.txAgeSeconds(millis());
}

void recordSent(const meshtastic_MeshPacket &packet)
{
    selectPeer();
    const auto *peer = getPeer();
    if (!peer || (packet.from != 0 && packet.from != nodeDB->getNodeNum()) || packet.to != peer->num ||
        packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag || packet.decoded.payload.size == 0 ||
        (packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP &&
         packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_COMPRESSED_APP))
        return;
    session.sent(packet.id, packet.want_ack, millis());
}

void recordReceived(const meshtastic_MeshPacket &packet)
{
    selectPeer();
    if (!nodeDB)
        return;
    ReceivedEvent event;
    event.from = packet.from;
    event.to = packet.to;
    event.decoded = packet.which_payload_variant == meshtastic_MeshPacket_decoded_tag;
    event.lora = packet.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    event.viaMqtt = packet.via_mqtt || packet.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_MQTT;
    event.local = packet.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
    if (event.decoded && packet.decoded.portnum == meshtastic_PortNum_POSITION_APP) {
        meshtastic_Position position = meshtastic_Position_init_default;
        event.position = pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size,
                                              meshtastic_Position_fields, &position) &&
                         position.has_latitude_i && position.has_longitude_i;
    }
    if (event.decoded && packet.decoded.portnum == meshtastic_PortNum_ROUTING_APP) {
        meshtastic_Routing routing = meshtastic_Routing_init_default;
        if (pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, meshtastic_Routing_fields,
                                 &routing) &&
            routing.which_variant == meshtastic_Routing_error_reason_tag) {
            event.routing = true;
            event.requestId = packet.decoded.request_id;
            event.error = routing.error_reason;
        }
    }
    session.received(event, nodeDB->getNodeNum(), millis());
}
} // namespace PeerStatus
#endif
