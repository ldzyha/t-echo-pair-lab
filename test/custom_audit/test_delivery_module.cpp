#include "FakeDelivery.h"
#include "input/QuickHeart.h"
#include "mesh/DeliveryQueueCodec.h"
std::vector<std::string> trace;
FakeConfig config;
meshtastic_User owner = meshtastic_User_init_default;
FakeFS fakeFS;
namespace concurrency
{
int spiDepth = 0;
Delay mainDelay;
} // namespace concurrency
concurrency::Lock fakeSpi{true};
concurrency::Lock *spiLock = &fakeSpi;
NodeDB fakeNodes;
NodeDB *nodeDB = &fakeNodes;
Router fakeRouter;
Router *router = &fakeRouter;
MeshService fakeService;
MeshService *service = &fakeService;
RoutingModule fakeRouting;
RoutingModule *routingModule = &fakeRouting;
FakePool<meshtastic_MeshPacket> packetPool;
FakePool<meshtastic_ClientNotification> clientNotificationPool;
std::vector<meshtastic_MeshPacket> transmissions, delivered;
std::vector<uint32_t> confirmed;
std::vector<std::pair<uint32_t, int>> queueStatus;
uint32_t packetCounter = 100000;
uint32_t generatePacketId()
{
    return ++packetCounter;
}
size_t File::read(uint8_t *out, size_t n)
{
    assert(concurrency::spiDepth == 1);
    if (!bytes || n > bytes->size())
        return 0;
    memcpy(out, bytes->data(), n);
    trace.push_back("fs:readback");
    return n;
}
size_t File::write(const uint8_t *in, size_t n)
{
    assert(concurrency::spiDepth == 1);
    if (fakeFS.failWrite)
        return 0;
    bytes->insert(bytes->end(), in, in + n);
    trace.push_back("fs:write");
    return n;
}
void MeshService::sendToMesh(meshtastic_MeshPacket *p, int)
{
    assert(concurrency::spiDepth == 0);
    transmissions.push_back(*p);
    trace.push_back("radio:send");
    fakeRouter.transmitting = p->id;
    DeliveryQueue::sentToRouter(p->id, 0);
    delete p;
}
int MeshService::sendQueueStatusToPhone(const meshtastic_QueueStatus &, int result, uint32_t id)
{
    assert(concurrency::spiDepth == 0);
    queueStatus.emplace_back(id, result);
    trace.push_back("client:accepted");
    return 0;
}
void RoutingModule::sendAckNak(meshtastic_Routing_Error, uint32_t, uint32_t, uint8_t)
{
    trace.push_back("client:failure");
}
namespace DeliveryQueue
{
void deliverToClient(const meshtastic_MeshPacket &p)
{
    assert(concurrency::spiDepth == 0);
    delivered.push_back(p);
    trace.push_back("client:text");
}
void confirmToClient(const meshtastic_MeshPacket &, uint32_t id)
{
    assert(concurrency::spiDepth == 0);
    confirmed.push_back(id);
    trace.push_back("client:confirmed");
}
} // namespace DeliveryQueue
#include "../../src/modules/DeliveryQueueModule.cpp"
struct Harness : DeliveryQueue::Module {
    using Module::handleReceived;
    using Module::runOnce;
};
Harness harness;
using namespace DeliveryQueueCodec;
void restart(bool keepDisk)
{
    using namespace DeliveryQueue;
    state = State{};
    backupState = State{};
    generation = 0;
    activeSlot = -1;
    ready = false;
    healthy = false;
    dispatching = false;
    busyId = 0;
    gate = {};
    memset(presented, 0, sizeof(presented));
    memset(liveInbox, 0, sizeof(liveInbox));
    jobs = {};
    jobHead = jobCount = reservedSubmits = 0;
    forgetCount = 0;
    forgetAll = false;
    instance = &harness;
    fakeRouter.transmitting = 0;
    fakeFS.failWrite = false;
    transmissions.clear();
    delivered.clear();
    confirmed.clear();
    queueStatus.clear();
    trace.clear();
    if (!keepDisk)
        fakeFS.files.clear();
    owner.public_key.size = 32;
    memset(owner.public_key.bytes, 0xa5, 32);
    auto &peer = fakeNodes.nodes[PeerStatus::NODE_A];
    peer.num = PeerStatus::NODE_A;
    peer.bitfield = 0;
    peer.is_ignored = false;
    peer.user.public_key.size = 32;
    memset(peer.user.public_key.bytes, 0x5a, 32);
    harness.runOnce();
    assert(DeliveryQueue::healthy);
}
meshtastic_MeshPacket message(uint32_t id, const char *text)
{
    meshtastic_MeshPacket p = meshtastic_MeshPacket_init_default;
    p.id = id;
    p.to = PeerStatus::NODE_A;
    p.want_ack = true;
    p.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    p.decoded.payload.size = strlen(text);
    memcpy(p.decoded.payload.bytes, text, strlen(text));
    return p;
}
meshtastic_MeshPacket incoming(uint8_t kind, uint64_t epoch, uint32_t seq, uint32_t originalId, const char *text = "hello")
{
    auto p = message(generatePacketId(), text);
    p.from = PeerStatus::NODE_A;
    p.to = PeerStatus::NODE_B;
    p.want_ack = false;
    p.pki_encrypted = true;
    p.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    p.public_key.size = 32;
    memset(p.public_key.bytes, 0x5a, 32);
    uint8_t body[meshtastic_Data_size];
    size_t n = kind == DATA ? pb_encode_to_bytes(body, sizeof(body), meshtastic_Data_fields, &p.decoded) : 0;
    assert(DeliveryQueue::makeData(p.decoded, Frame{kind, epoch, seq, originalId, body, n}));
    return p;
}
Frame frameOf(const meshtastic_MeshPacket &p)
{
    Frame f;
    assert(decode(p.decoded.payload.bytes, p.decoded.payload.size, f));
    return f;
}
void completeRadio()
{
    uint32_t id = fakeRouter.transmitting;
    fakeRouter.transmitting = 0;
    DeliveryQueue::radioComplete(id);
    harness.runOnce();
}
void receive(const meshtastic_MeshPacket &p)
{
    const auto writes = std::count(trace.begin(), trace.end(), "fs:write");
    assert(harness.handleReceived(p) == ProcessMessage::STOP);
    assert(std::count(trace.begin(), trace.end(), "fs:write") == writes); // no flash in RX callback
    harness.runOnce();
}
bool before(const char *a, const char *b)
{
    auto x = std::find(trace.begin(), trace.end(), a), y = std::find(trace.begin(), trace.end(), b);
    return x != trace.end() && y != trace.end() && x < y;
}
meshtastic_SharedContact verifiedContact(uint8_t key = 0x6b)
{
    meshtastic_SharedContact contact = meshtastic_SharedContact_init_default;
    contact.node_num = PeerStatus::NODE_A;
    contact.has_user = contact.manually_verified = true;
    contact.user.public_key.size = 32;
    memset(contact.user.public_key.bytes, key, 32);
    auto &peer = fakeNodes.nodes[contact.node_num];
    peer.bitfield = NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
    peer.user = contact.user;
    return contact;
}
meshtastic_MeshPacket localAdmin()
{
    auto source = message(900, "admin");
    source.to = PeerStatus::NODE_B;
    source.decoded.portnum = meshtastic_PortNum_ADMIN_APP;
    return source;
}
void testVerifiedContactRepair()
{
    using namespace DeliveryQueue;
    restart(false);
    assert(submit(message(101, "Ї Є Ґ repair")));
    harness.runOnce();
    const auto firstAttempt = transmissions.back();
    completeRadio();
    assert(submit(message(102, "second pending")));
    harness.runOnce();
    const auto originalState = state;
    auto contact = verifiedContact();
    const auto source = localAdmin();
    const auto filesBefore = fakeFS.files;
    auto rejected = source;
    rejected.from = PeerStatus::NODE_A;
    assert(!verifiedContactImported(rejected, contact)); // even authorized remote admin cannot rebind
    rejected.from = PeerStatus::NODE_B;
    assert(!verifiedContactImported(rejected, contact)); // self nodenum is not a phone-origin packet
    rejected = source;
    rejected.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    assert(!verifiedContactImported(rejected, contact));
    rejected = source;
    rejected.via_mqtt = true;
    assert(!verifiedContactImported(rejected, contact));
    rejected = source;
    rejected.decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    assert(!verifiedContactImported(rejected, contact));
    auto badContact = contact;
    badContact.manually_verified = false;
    assert(!verifiedContactImported(source, badContact));
    badContact = contact;
    badContact.should_ignore = true;
    assert(!verifiedContactImported(source, badContact));
    badContact = contact;
    badContact.has_user = false;
    assert(!verifiedContactImported(source, badContact));
    badContact = contact;
    badContact.node_num = PeerStatus::NODE_B;
    assert(!verifiedContactImported(source, badContact));
    badContact = contact;
    badContact.user.public_key.size = 31;
    assert(!verifiedContactImported(source, badContact));
    badContact = contact;
    badContact.user.public_key.bytes[0] ^= 1;
    assert(!verifiedContactImported(source, badContact)); // rejected import/current-key mismatch
    fakeNodes.nodes[PeerStatus::NODE_A].bitfield = 0;
    assert(!verifiedContactImported(source, contact));
    fakeNodes.nodes[PeerStatus::NODE_A].bitfield = NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;
    fakeNodes.nodes[PeerStatus::NODE_A].is_ignored = true;
    assert(!verifiedContactImported(source, contact));
    fakeNodes.nodes[PeerStatus::NODE_A].is_ignored = false;
    assert(jobCount == 0 && fakeFS.files == filesBefore);

    jobCount = JOB_CAP;
    trace.clear();
    assert(!verifiedContactImported(source, contact));
    assert(std::find(trace.begin(), trace.end(), "Pending message: contact repair queue full; repeat verified import") !=
           trace.end());
    jobCount = 0;

    trace.clear();
    assert(verifiedContactImported(source, contact));
    assert(fakeFS.files == filesBefore && transmissions.size() == 1); // callback only queues work
    harness.runOnce();
    assert(state.epoch == originalState.epoch && state.nextSequence == originalState.nextSequence && state.outCount == 2);
    for (size_t i = 0; i < state.outCount; ++i) {
        auto expected = originalState.out[i];
        memcpy(expected.packet.public_key.bytes, contact.user.public_key.bytes, 32);
        assert(!memcmp(&state.out[i], &expected, sizeof(expected)));
    }
    assert(transmissions.size() == 1 && confirmed.empty()); // import does not invent a reception/delivery
    assert(std::find(trace.begin(), trace.end(), "fs:readback") != trace.end());
    auto logical = frameOf(firstAttempt);
    auto oldReceipt = incoming(RECEIPT, logical.epoch, logical.sequence, logical.originalId);
    receive(oldReceipt);
    assert(confirmed.empty() && state.outCount == 2);
    assert(transmissions.size() == 2 && transmissions.back().id != firstAttempt.id);
    assert(!memcmp(transmissions.back().public_key.bytes, contact.user.public_key.bytes, 32));
    assert(before("fs:readback", "radio:send"));
    completeRadio();
    auto newReceipt = oldReceipt;
    newReceipt.id = generatePacketId();
    memcpy(newReceipt.public_key.bytes, contact.user.public_key.bytes, 32);
    receive(newReceipt);
    assert(confirmed.size() == 1 && confirmed[0] == 101 && state.outCount == 1 && state.out[0].packet.id == 102);

    restart(true);
    assert(state.outCount == 1 && state.out[0].packet.id == 102 && transmissions.empty());
    assert(!memcmp(state.out[0].packet.public_key.bytes, contact.user.public_key.bytes, 32));
    verifiedContact(); // restore the independently persisted NodeDB key in the fake platform
    trace.clear();
    assert(verifiedContactImported(source, contact));
    harness.runOnce();
    assert(std::find(trace.begin(), trace.end(), "fs:write") == trace.end() && transmissions.empty());

    restart(false);
    assert(submit(message(103, "preserve across repair failure")));
    harness.runOnce();
    completeRadio();
    const auto beforeFailure = state;
    contact = verifiedContact();
    fakeFS.failWrite = true;
    trace.clear();
    assert(verifiedContactImported(source, contact));
    harness.runOnce();
    assert(!memcmp(&state, &beforeFailure, sizeof(state)) && transmissions.size() == 1);
    assert(std::find(trace.begin(), trace.end(), "Pending message: contact repair write failed; repeat verified import") !=
           trace.end());
    restart(true);
    assert(state.outCount == 1 && state.out[0].packet.id == 103 && state.out[0].packet.public_key.bytes[0] == 0x5a);
    contact = verifiedContact();
    assert(verifiedContactImported(source, contact)); // identical verified contact retries after failed persistence
    harness.runOnce();
    assert(state.out[0].packet.public_key.bytes[0] == 0x6b && transmissions.empty());

    restart(false);
    assert(submit(message(104, "contact changed while deferred")));
    harness.runOnce();
    completeRadio();
    contact = verifiedContact();
    assert(verifiedContactImported(source, contact));
    verifiedContact(0x7c);
    harness.runOnce();
    assert(state.out[0].packet.public_key.bytes[0] == 0x5a && transmissions.size() == 1);
}
void testQuickHeartQueue()
{
    restart(false);
    auto packet = message(990, "unused");
    auto &peer = fakeNodes.nodes[PeerStatus::NODE_A];
    assert(QuickHeart::prepare(packet, PeerStatus::NODE_B, PeerStatus::NODE_A, peer.user.public_key.bytes, 32));
    assert(!packet.want_ack);
    assert(DeliveryQueue::submit(packet));
    harness.runOnce();
    assert(DeliveryQueue::pendingCount() == 1 && transmissions.size() == 1);
    const auto frame = frameOf(transmissions.back());
    meshtastic_Data decoded = meshtastic_Data_init_default;
    assert(pb_decode_from_bytes(frame.body, frame.size, meshtastic_Data_fields, &decoded));
    assert(decoded.payload.size == sizeof(QuickHeart::TEXT) - 1);
    assert(!memcmp(decoded.payload.bytes, QuickHeart::TEXT, decoded.payload.size));
    completeRadio();
    restart(true);
    assert(DeliveryQueue::pendingCount() == 1 && transmissions.empty());
    auto event = message(generatePacketId(), "peer event");
    event.from = PeerStatus::NODE_A;
    event.to = PeerStatus::NODE_B;
    event.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    DeliveryQueue::observeReceived(event);
    harness.runOnce();
    assert(transmissions.size() == 1 && frameOf(transmissions.back()).originalId == 990);
    completeRadio();
    receive(incoming(RECEIPT, frame.epoch, frame.sequence, 990));
    assert(DeliveryQueue::pendingCount() == 0 && confirmed.size() == 1 && confirmed[0] == 990);
}

int main()
{
    restart(false);
    trace.clear();
    assert(DeliveryQueue::submit(message(1, "Ї Є Ґ")));
    assert(transmissions.empty());
    harness.runOnce();
    assert(DeliveryQueue::pendingCount() == 1 && transmissions.size() == 1);
    assert(before("fs:readback", "radio:send") && before("fs:readback", "client:accepted"));
    const auto first = transmissions.back();
    Frame logical = frameOf(first);
    completeRadio();
    assert(transmissions.size() == 1);
    assert(DeliveryQueue::submit(message(2, "second")));
    harness.runOnce();
    assert(DeliveryQueue::pendingCount() == 2 && transmissions.size() == 1);
    for (int i = 0; i < 8; ++i)
        harness.runOnce();
    assert(transmissions.size() == 1);
    auto bad = incoming(RECEIPT, logical.epoch, logical.sequence, 999);
    receive(bad);
    assert(confirmed.empty() && DeliveryQueue::pendingCount() == 2);
    completeRadio();
    auto good = incoming(RECEIPT, logical.epoch, logical.sequence, logical.originalId);
    receive(good);
    assert(confirmed.size() == 1 && confirmed[0] == 1 && DeliveryQueue::pendingCount() == 1);
    assert(frameOf(transmissions.back()).originalId == 2);
    completeRadio();
    const size_t previousAttempts = transmissions.size();
    auto peerEvent = message(generatePacketId(), "event");
    peerEvent.from = PeerStatus::NODE_A;
    peerEvent.to = PeerStatus::NODE_B;
    peerEvent.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA;
    DeliveryQueue::observeReceived(peerEvent);
    harness.runOnce();
    assert(transmissions.size() == previousAttempts + 1);
    assert(transmissions.back().id != first.id && frameOf(transmissions.back()).originalId == 2);
    completeRadio();
    DeliveryQueue::observeReceived(peerEvent);
    harness.runOnce();
    assert(transmissions.size() == previousAttempts + 1);
    restart(true);
    assert(DeliveryQueue::pendingCount() == 1 && transmissions.empty());
    DeliveryQueue::observeReceived(peerEvent);
    harness.runOnce();
    assert(transmissions.size() == 1 && frameOf(transmissions.back()).originalId == 2);

    restart(false);
    trace.clear();
    auto data = incoming(DATA, 999, 1, 71, "persistent");
    receive(data);
    assert(delivered.size() == 1 && delivered[0].id == 71 && transmissions.size() == 1);
    assert(frameOf(transmissions.back()).kind == RECEIPT && before("fs:readback", "radio:send"));
    auto writes = std::count(trace.begin(), trace.end(), "fs:write");
    data.id = generatePacketId();
    receive(data);
    assert(delivered.size() == 1 && transmissions.size() == 2 && std::count(trace.begin(), trace.end(), "fs:write") == writes);
    restart(true);
    assert(delivered.size() == 1 && delivered[0].id == 71 && transmissions.empty());
    assert(delivered[0].transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL);
    DeliveryQueue::forgetInbox(71);
    harness.runOnce();
    restart(true);
    assert(delivered.empty());
    data.id = generatePacketId();
    receive(data);
    assert(delivered.empty() && transmissions.size() == 1); // deletion retains watermark

    restart(false);
    fakeFS.failWrite = true;
    trace.clear();
    receive(incoming(DATA, 777, 1, 81));
    assert(delivered.empty() && transmissions.empty() && DeliveryQueue::state.inboxCount == 0);
    assert(DeliveryQueue::submit(message(91, "write failure")));
    harness.runOnce();
    assert(DeliveryQueue::pendingCount() == 0 && transmissions.empty() && !queueStatus.empty() && queueStatus.back().second != 0);
    testVerifiedContactRepair();
    testQuickHeartQueue();
    puts("actual DeliveryQueueModule: FIFO, event retry, receipt, durable dedupe/reboot/delete, storage failure, verified "
         "repair: OK");
}
