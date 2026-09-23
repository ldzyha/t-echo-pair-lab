#pragma once
#include "mesh/generated/meshtastic/admin.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <cstddef>
namespace DeliveryQueue
{
void setup();
bool submit(const meshtastic_MeshPacket &packet);
bool verifiedContactImported(const meshtastic_MeshPacket &source, const meshtastic_SharedContact &contact);
void observeReceived(const meshtastic_MeshPacket &packet);
void sentToRouter(uint32_t id, int result);
void radioComplete(uint32_t id);
bool cancel(uint32_t originalPhoneId);
bool cancelAll();
void forgetInbox(uint32_t originalId);
size_t pendingCount();
const char *statusLabel();
void deliverToClient(const meshtastic_MeshPacket &ordinaryText);
void confirmToClient(const meshtastic_MeshPacket &provenReceipt, uint32_t originalPhoneId);
} // namespace DeliveryQueue
