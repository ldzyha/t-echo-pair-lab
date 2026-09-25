#include "configuration.h"
#if defined(TTGO_T_ECHO_PLUS)
#include "DeliveryQueueModule.h"
#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PeerStatus.h"
#include "RadioInterface.h"
#include "Router.h"
#include "SPILock.h"
#include "SinglePortModule.h"
#include "concurrency/LockGuard.h"
#include "concurrency/OSThread.h"
#include "mesh/DeliveryQueueCodec.h"
#include "modules/RoutingModule.h"
#include <array>
#include <cstring>

namespace DeliveryQueue
{
using namespace DeliveryQueueCodec;
namespace
{
constexpr size_t CAP = 8, INBOX_CAP = 20, SEEN_CAP = 32, STORAGE_CAP = 16384, JOB_CAP = 12;
const char *paths[2] = {"/delivery-a.dat", "/delivery-b.dat"};
struct Outgoing {
    uint32_t sequence = 0;
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
};
struct Seen {
    uint32_t sender = 0;
    uint64_t epoch = 0;
    uint32_t sequence = 0;
};
struct State {
    uint32_t self = 0, nextSequence = 1;
    uint64_t epoch = 0;
    uint8_t key[32] = {};
    uint8_t outCount = 0, inboxCount = 0, seenCount = 0;
    std::array<Outgoing, CAP> out{};
    std::array<meshtastic_MeshPacket, INBOX_CAP> inbox{};
    std::array<Seen, SEEN_CAP> seen{};
};
State state, backupState;
std::array<uint8_t, STORAGE_CAP> storage{};
uint32_t generation = 0;
int activeSlot = -1;
bool ready = false, healthy = false, dispatching = false;
bool presented[INBOX_CAP] = {}, liveInbox[INBOX_CAP] = {};
uint32_t busyId = 0;
EventGate gate;
const char *label = "Starting";
class Module;
Module *instance = nullptr;
concurrency::Lock jobLock;
concurrency::Lock inboxLock;
enum class JobKind : uint8_t { SUBMIT, RECEIVE, CANCEL, REBIND };
struct Job {
    JobKind kind = JobKind::CANCEL;
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_default;
};
std::array<Job, JOB_CAP> jobs;
size_t jobHead = 0, jobCount = 0, reservedSubmits = 0;
uint32_t forgetIds[INBOX_CAP] = {};
size_t forgetCount = 0;
bool forgetAll = false;

struct Writer {
    uint8_t *p;
    size_t cap, used = 0;
    bool ok = true;
    void bytes(const void *v, size_t n)
    {
        if (!ok || n > cap - used) {
            ok = false;
            return;
        }
        memcpy(p + used, v, n);
        used += n;
    }
    void u32(uint32_t v)
    {
        uint8_t b[4];
        put32(b, v);
        bytes(b, 4);
    }
    void u64(uint64_t v)
    {
        uint8_t b[8];
        put64(b, v);
        bytes(b, 8);
    }
    void packet(const meshtastic_MeshPacket &v)
    {
        uint8_t b[meshtastic_MeshPacket_size];
        size_t n = pb_encode_to_bytes(b, sizeof(b), meshtastic_MeshPacket_fields, &v);
        if (!n) {
            ok = false;
            return;
        }
        u32(n);
        bytes(b, n);
    }
};
struct Reader {
    const uint8_t *p;
    size_t cap, used = 0;
    bool ok = true;
    void bytes(void *v, size_t n)
    {
        if (!ok || n > cap - used) {
            ok = false;
            return;
        }
        memcpy(v, p + used, n);
        used += n;
    }
    uint32_t u32()
    {
        uint8_t b[4] = {};
        bytes(b, 4);
        return get32(b);
    }
    uint64_t u64()
    {
        uint8_t b[8] = {};
        bytes(b, 8);
        return get64(b);
    }
    void packet(meshtastic_MeshPacket &v)
    {
        size_t n = u32();
        if (!ok || !n || n > meshtastic_MeshPacket_size || n > cap - used) {
            ok = false;
            return;
        }
        v = meshtastic_MeshPacket_init_default;
        ok = pb_decode_from_bytes(p + used, n, meshtastic_MeshPacket_fields, &v);
        used += n;
    }
};
uint32_t peerId()
{
    if (!nodeDB)
        return 0;
    const uint32_t self = nodeDB->getNodeNum();
    return self == PeerStatus::NODE_A ? PeerStatus::NODE_B : self == PeerStatus::NODE_B ? PeerStatus::NODE_A : 0;
}
bool originalValid(const meshtastic_MeshPacket &p)
{
    return p.id && p.which_payload_variant == meshtastic_MeshPacket_decoded_tag &&
           p.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP && p.decoded.payload.size && p.to == peerId();
}
bool framePacket(const meshtastic_MeshPacket &p)
{
    return p.which_payload_variant == meshtastic_MeshPacket_decoded_tag && p.decoded.portnum == meshtastic_PortNum_PRIVATE_APP &&
           p.pki_encrypted && p.public_key.size == 32 && p.from == peerId() && p.to == nodeDB->getNodeNum() &&
           p.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA && !p.via_mqtt;
}
size_t encodeState()
{
    Writer w{storage.data() + SNAPSHOT_HEADER, storage.size() - SNAPSHOT_HEADER};
    w.u32(state.self);
    w.u64(state.epoch);
    w.u32(state.nextSequence);
    w.bytes(state.key, sizeof(state.key));
    w.u32(state.outCount);
    w.u32(state.inboxCount);
    w.u32(state.seenCount);
    for (size_t i = 0; i < state.outCount; ++i) {
        w.u32(state.out[i].sequence);
        w.packet(state.out[i].packet);
    }
    for (size_t i = 0; i < state.inboxCount; ++i)
        w.packet(state.inbox[i]);
    for (size_t i = 0; i < state.seenCount; ++i) {
        w.u32(state.seen[i].sender);
        w.u64(state.seen[i].epoch);
        w.u32(state.seen[i].sequence);
    }
    if (!w.ok)
        return 0;
    uint32_t next = generation + 1;
    if (!next)
        next = 1;
    snapshotHeader(storage.data(), next, storage.data() + SNAPSHOT_HEADER, w.used);
    return w.used + SNAPSHOT_HEADER;
}
bool decodeState(size_t n)
{
    Reader r{storage.data() + SNAPSHOT_HEADER, n - SNAPSHOT_HEADER};
    backupState = State{};
    backupState.self = r.u32();
    backupState.epoch = r.u64();
    backupState.nextSequence = r.u32();
    r.bytes(backupState.key, sizeof(backupState.key));
    const uint32_t out = r.u32(), inbox = r.u32(), seen = r.u32();
    if (!r.ok || out > CAP || inbox > INBOX_CAP || seen > SEEN_CAP || !backupState.epoch || !backupState.nextSequence ||
        backupState.self != nodeDB->getNodeNum() || owner.public_key.size != 32 ||
        memcmp(backupState.key, owner.public_key.bytes, 32))
        return false;
    backupState.outCount = out;
    backupState.inboxCount = inbox;
    backupState.seenCount = seen;
    uint32_t previous = 0;
    for (size_t i = 0; i < out; ++i) {
        auto &o = backupState.out[i];
        o.sequence = r.u32();
        r.packet(o.packet);
        if (!r.ok || !originalValid(o.packet) || o.packet.public_key.size != 32 || o.sequence <= previous ||
            o.sequence >= backupState.nextSequence)
            return false;
        previous = o.sequence;
    }
    for (size_t i = 0; i < inbox; ++i) {
        r.packet(backupState.inbox[i]);
        Frame f;
        const auto &p = backupState.inbox[i];
        if (!r.ok || !framePacket(p) || !decode(p.decoded.payload.bytes, p.decoded.payload.size, f) || f.kind != DATA)
            return false;
    }
    for (size_t i = 0; i < seen; ++i) {
        auto &s = backupState.seen[i];
        s.sender = r.u32();
        s.epoch = r.u64();
        s.sequence = r.u32();
        if (!r.ok || s.sender != peerId() || !s.epoch || !s.sequence)
            return false;
    }
    return r.ok && r.used == r.cap;
}
size_t readSlot(int slot)
{
    concurrency::LockGuard lock(spiLock);
    auto f = FSCom.open(paths[slot], FILE_O_READ);
    if (!f)
        return 0;
    const size_t n = f.size();
    if (n < SNAPSHOT_HEADER || n > storage.size()) {
        f.close();
        return 0;
    }
    const size_t read = f.read(storage.data(), n);
    f.close();
    return read == n && validSnapshot(storage.data(), n, storage.size()) ? n : 0;
}
bool persist()
{
    const size_t n = encodeState();
    if (!n)
        return false;
    const uint32_t expectedGeneration = get32(storage.data() + 8), expectedCrc = get32(storage.data() + 16);
    const int slot = activeSlot == 0 ? 1 : 0;
    {
        concurrency::LockGuard lock(spiLock);
        FSCom.remove(paths[slot]);
        auto f = FSCom.open(paths[slot], FILE_O_WRITE);
        if (!f)
            return false;
        const size_t written = f.write(storage.data(), n);
        f.close();
        if (written != n)
            return false;
    }
    if (readSlot(slot) != n || get32(storage.data() + 8) != expectedGeneration || get32(storage.data() + 16) != expectedCrc)
        return false;
    generation = expectedGeneration;
    activeSlot = slot;
    return true;
}
bool restore()
{
    concurrency::LockGuard lock(&inboxLock);
    uint32_t gens[2] = {};
    bool exists[2] = {};
    for (int i = 0; i < 2; ++i) {
        {
            concurrency::LockGuard lock(spiLock);
            exists[i] = FSCom.exists(paths[i]);
        }
        if (readSlot(i))
            gens[i] = get32(storage.data() + 8);
    }
    int first = newer(gens[1], gens[0]) ? 1 : 0;
    for (int k = 0; k < 2; ++k) {
        int slot = k ? 1 - first : first;
        if (!gens[slot])
            continue;
        size_t n = readSlot(slot);
        if (n && decodeState(n)) {
            state = backupState;
            generation = gens[slot];
            activeSlot = slot;
            return true;
        }
    }
    if (exists[0] || exists[1] || owner.public_key.size != 32)
        return false;
    state.self = nodeDB->getNodeNum();
    state.epoch = uint64_t(generatePacketId()) << 32 | generatePacketId();
    if (!state.epoch)
        state.epoch = 1;
    memcpy(state.key, owner.public_key.bytes, 32);
    return persist();
}
void accepted(uint32_t id)
{
    service->sendQueueStatusToPhone(router->getQueueStatus(), ERRNO_OK, id);
}
void notifyFailure(uint32_t id, uint8_t channel, meshtastic_Routing_Error error, const char *reason)
{
    LOG_WARN("Delivery queue: id=%08x %s", id, reason);
    if (id) {
        service->sendQueueStatusToPhone(router->getQueueStatus(), ERRNO_UNKNOWN, id);
        if (routingModule)
            routingModule->sendAckNak(error, nodeDB->getNodeNum(), id, channel);
    }
    auto *cn = clientNotificationPool.allocZeroed();
    if (cn) {
        cn->has_reply_id = true;
        cn->reply_id = id;
        cn->level = meshtastic_LogRecord_Level_ERROR;
        snprintf(cn->message, sizeof(cn->message), "Pending message: %s", reason);
        service->sendClientNotification(cn);
    }
}
bool makeData(meshtastic_Data &out, const Frame &frame)
{
    out = meshtastic_Data_init_default;
    out.portnum = meshtastic_PortNum_PRIVATE_APP;
    out.payload.size = encode(out.payload.bytes, sizeof(out.payload.bytes), frame);
    out.has_bitfield = true;
    out.bitfield = config.lora.config_ok_to_mqtt ? 1 : 0;
    if (!out.payload.size)
        return false;
    uint8_t scratch[meshtastic_Data_size];
    const size_t n = pb_encode_to_bytes(scratch, sizeof(scratch), meshtastic_Data_fields, &out);
    return n && n + MESHTASTIC_HEADER_LENGTH + MESHTASTIC_PKC_OVERHEAD <= MAX_LORA_PAYLOAD_LEN;
}
bool encodeOriginal(const meshtastic_MeshPacket &p, uint64_t epoch, uint32_t sequence, meshtastic_Data &out)
{
    uint8_t data[meshtastic_Data_size];
    const size_t n = pb_encode_to_bytes(data, sizeof(data), meshtastic_Data_fields, &p.decoded);
    return n && makeData(out, Frame{DATA, epoch, sequence, p.id, data, n});
}
void sendFrame(const meshtastic_Data &data, uint32_t dest, uint8_t channel, const uint8_t *key, bool outgoing)
{
    auto *p = packetPool.allocZeroed();
    if (!p) {
        label = "No memory";
        return;
    }
    p->from = nodeDB->getNodeNum();
    p->to = dest;
    p->id = generatePacketId();
    p->channel = channel;
    p->hop_limit = config.lora.hop_limit;
    p->want_ack = false;
    p->pki_encrypted = true;
    p->public_key.size = 32;
    memcpy(p->public_key.bytes, key, 32);
    p->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    p->decoded = data;
    p->priority = outgoing ? meshtastic_MeshPacket_Priority_HIGH : meshtastic_MeshPacket_Priority_ACK;
    if (outgoing) {
        busyId = p->id;
        label = "Await receipt";
    }
    Frame frame;
    if (decode(data.payload.bytes, data.payload.size, frame))
        LOG_INFO("Delivery %s attempt orig=%08x transport=%08x", outgoing ? "data" : "receipt", frame.originalId, p->id);
    service->sendToMesh(p, RX_SRC_LOCAL);
}
void receipt(const meshtastic_MeshPacket &p, const Frame &f)
{
    meshtastic_Data data = meshtastic_Data_init_default;
    if (makeData(data, Frame{RECEIPT, f.epoch, f.sequence, f.originalId, nullptr, 0}))
        sendFrame(data, p.from, p.channel, p.public_key.bytes, false);
}
void removeHead()
{
    for (size_t i = 1; i < state.outCount; ++i)
        state.out[i - 1] = state.out[i];
    state.out[--state.outCount] = Outgoing{};
}
void processSubmit(const meshtastic_MeshPacket &p)
{
    if (!healthy) {
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_BAD_REQUEST, "storage unavailable");
        return;
    }
    for (size_t i = 0; i < state.outCount; ++i) {
        const auto &q = state.out[i].packet;
        if (q.id == p.id) {
            uint8_t a[meshtastic_Data_size], b[meshtastic_Data_size];
            const size_t aSize = pb_encode_to_bytes(a, sizeof(a), meshtastic_Data_fields, &q.decoded);
            const size_t bSize = pb_encode_to_bytes(b, sizeof(b), meshtastic_Data_fields, &p.decoded);
            if (q.to != p.to || !aSize || aSize != bSize || memcmp(a, b, aSize))
                notifyFailure(p.id, p.channel, meshtastic_Routing_Error_BAD_REQUEST, "message ID collision");
            else
                accepted(p.id);
            return;
        }
    }
    if (state.outCount == CAP || state.nextSequence == UINT32_MAX) {
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_BAD_REQUEST, "outbox full");
        return;
    }
    auto *peer = nodeDB->getMeshNode(p.to);
    if (!peer || peer->user.public_key.size != 32 ||
        (p.pki_encrypted && p.public_key.size == 32 && memcmp(p.public_key.bytes, peer->user.public_key.bytes, 32))) {
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_PKI_FAILED, "peer key unavailable or changed");
        return;
    }
    meshtastic_Data data = meshtastic_Data_init_default;
    if (!encodeOriginal(p, state.epoch, state.nextSequence, data)) {
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_TOO_LARGE, "text too long for reliable envelope");
        return;
    }
    backupState = state;
    auto &o = state.out[state.outCount++];
    o.sequence = state.nextSequence++;
    o.packet = p;
    o.packet.public_key.size = 32;
    memcpy(o.packet.public_key.bytes, peer->user.public_key.bytes, 32);
    o.packet.pki_encrypted = true;
    if (!persist()) {
        state = backupState;
        label = "Storage error";
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_BAD_REQUEST, "storage write failed");
        return;
    }
    label = "Queued";
    LOG_INFO("Delivery queued orig=%08x pending=%u", p.id, unsigned(state.outCount));
    accepted(p.id);
    if (state.outCount == 1)
        gate.initial();
}
void processReceived(const meshtastic_MeshPacket &p)
{
    concurrency::LockGuard lock(&inboxLock);
    Frame f;
    if (!healthy || !framePacket(p) || !decode(p.decoded.payload.bytes, p.decoded.payload.size, f))
        return;
    if (f.kind == RECEIPT) {
        if (!state.outCount)
            return;
        const auto &head = state.out[0];
        if (!receiptMatches(f, state.epoch, head.sequence, head.packet.id) ||
            memcmp(p.public_key.bytes, head.packet.public_key.bytes, 32))
            return;
        backupState = state;
        removeHead();
        if (!persist()) {
            state = backupState;
            label = "Storage error";
            return;
        }
        busyId = 0;
        LOG_INFO("Delivery receipt confirmed orig=%08x pending=%u", f.originalId, unsigned(state.outCount));
        label = state.outCount ? "Queued" : "Delivered";
        dispatching = true;
        confirmToClient(p, f.originalId);
        dispatching = false;
        return;
    }
    meshtastic_Data original = meshtastic_Data_init_default;
    if (!f.size || !pb_decode_from_bytes(f.body, f.size, meshtastic_Data_fields, &original) ||
        original.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP || !original.payload.size)
        return;
    size_t seenIndex = state.seenCount;
    for (size_t i = 0; i < state.seenCount; ++i)
        if (state.seen[i].sender == p.from && state.seen[i].epoch == f.epoch) {
            seenIndex = i;
            break;
        }
    if (seenIndex < state.seenCount && f.sequence <= state.seen[seenIndex].sequence) {
        LOG_INFO("Delivery duplicate suppressed orig=%08x", f.originalId);
        receipt(p, f);
        return;
    }
    backupState = state;
    if (seenIndex == state.seenCount) {
        if (state.seenCount == SEEN_CAP) {
            state = backupState;
            label = "Dedupe full";
            return;
        }
        ++state.seenCount;
    }
    state.seen[seenIndex] = Seen{p.from, f.epoch, f.sequence};
    bool evicted = state.inboxCount == INBOX_CAP;
    if (evicted) {
        for (size_t i = 1; i < INBOX_CAP; ++i)
            state.inbox[i - 1] = state.inbox[i];
        --state.inboxCount;
    }
    state.inbox[state.inboxCount++] = p;
    if (!persist()) {
        state = backupState;
        label = "Storage error";
        return;
    }
    if (evicted)
        for (size_t i = 1; i < INBOX_CAP; ++i) {
            presented[i - 1] = presented[i];
            liveInbox[i - 1] = liveInbox[i];
        }
    LOG_INFO("Delivery durably received orig=%08x retained=%u", f.originalId, unsigned(state.inboxCount));
    presented[state.inboxCount - 1] = false;
    liveInbox[state.inboxCount - 1] = true;
    receipt(p, f);
}
void processCancel(uint32_t id)
{
    if (!id) {
        backupState = state;
        state.outCount = 0;
        state.out = {};
        if (!persist()) {
            state = backupState;
            label = "Storage error";
            return;
        }
        if (busyId)
            router->cancelSending(nodeDB->getNodeNum(), busyId);
        busyId = 0;
        label = "Cancelled";
        return;
    }
    for (size_t i = 0; i < state.outCount; ++i)
        if (state.out[i].packet.id == id) {
            backupState = state;
            for (size_t j = i + 1; j < state.outCount; ++j)
                state.out[j - 1] = state.out[j];
            state.out[--state.outCount] = Outgoing{};
            if (!persist()) {
                state = backupState;
                label = "Storage error";
                return;
            }
            if (!i && busyId) {
                router->cancelSending(nodeDB->getNodeNum(), busyId);
                busyId = 0;
            }
            label = state.outCount ? "Queued" : "Cancelled";
            return;
        }
}
bool verifiedPeerKey(uint32_t id, const uint8_t *key)
{
    const auto *peer = nodeDB->getMeshNode(id);
    return id && id == peerId() && peer && !peer->is_ignored &&
           (peer->bitfield & NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK) && peer->user.public_key.size == 32 &&
           !memcmp(peer->user.public_key.bytes, key, 32);
}
void processRebind(const meshtastic_MeshPacket &p)
{
    if (!healthy || !verifiedPeerKey(p.to, p.public_key.bytes)) {
        notifyFailure(0, 0, meshtastic_Routing_Error_PKI_FAILED, "verified contact repair unavailable; repeat import");
        return;
    }
    backupState = state;
    size_t changed = 0;
    for (size_t i = 0; i < state.outCount; ++i) {
        auto &pending = state.out[i].packet;
        if (pending.to == p.to && memcmp(pending.public_key.bytes, p.public_key.bytes, 32)) {
            memcpy(pending.public_key.bytes, p.public_key.bytes, 32);
            ++changed;
        }
    }
    if (!changed)
        return;
    if (!persist()) {
        state = backupState;
        label = "Storage error";
        notifyFailure(0, 0, meshtastic_Routing_Error_BAD_REQUEST, "contact repair write failed; repeat verified import");
        return;
    }
    if (busyId)
        router->cancelSending(nodeDB->getNodeNum(), busyId);
    busyId = 0;
    label = "Wait peer RX";
    LOG_INFO("Delivery verified contact repaired peer=%08x pending=%u", p.to, unsigned(changed));
}
void processForget()
{
    concurrency::LockGuard inboxGuard(&inboxLock);
    uint32_t ids[INBOX_CAP] = {};
    size_t count;
    bool all;
    {
        concurrency::LockGuard lock(&jobLock);
        count = forgetCount;
        all = forgetAll;
        memcpy(ids, forgetIds, sizeof(ids));
        forgetCount = 0;
        forgetAll = false;
    }
    if (!count && !all)
        return;
    backupState = state;
    bool oldPresented[INBOX_CAP], oldLive[INBOX_CAP];
    memcpy(oldPresented, presented, sizeof(presented));
    memcpy(oldLive, liveInbox, sizeof(liveInbox));
    size_t kept = 0;
    for (size_t i = 0; i < state.inboxCount; ++i) {
        Frame frame;
        bool remove = all;
        if (decode(state.inbox[i].decoded.payload.bytes, state.inbox[i].decoded.payload.size, frame))
            for (size_t j = 0; j < count; ++j)
                remove |= ids[j] == frame.originalId;
        if (!remove) {
            state.inbox[kept] = state.inbox[i];
            presented[kept] = oldPresented[i];
            liveInbox[kept] = oldLive[i];
            ++kept;
        }
    }
    if (kept == state.inboxCount)
        return;
    for (size_t i = kept; i < INBOX_CAP; ++i)
        state.inbox[i] = meshtastic_MeshPacket_init_default;
    state.inboxCount = kept;
    if (!persist()) {
        state = backupState;
        memcpy(presented, oldPresented, sizeof(presented));
        memcpy(liveInbox, oldLive, sizeof(liveInbox));
        label = "Storage error";
        notifyFailure(0, 0, meshtastic_Routing_Error_BAD_REQUEST, "could not delete saved inbox");
    }
}
void presentInbox()
{
    for (size_t i = 0; i < state.inboxCount; ++i)
        if (!presented[i]) {
            const auto &stored = state.inbox[i];
            Frame f;
            if (!decode(stored.decoded.payload.bytes, stored.decoded.payload.size, f))
                continue;
            meshtastic_MeshPacket ordinary = stored;
            ordinary.decoded = meshtastic_Data_init_default;
            if (!pb_decode_from_bytes(f.body, f.size, meshtastic_Data_fields, &ordinary.decoded))
                continue;
            ordinary.id = f.originalId;
            ordinary.want_ack = false;
            if (!liveInbox[i])
                ordinary.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
            presented[i] = true;
            dispatching = true;
            deliverToClient(ordinary);
            dispatching = false;
        }
}
void tryHead()
{
    if (busyId && !router->isTransmittingOrQueued(nodeDB->getNodeNum(), busyId))
        busyId = 0;
    if (!healthy || !state.outCount || !gate.consume(busyId != 0))
        return;
    const auto &head = state.out[0];
    const auto *peer = nodeDB->getMeshNode(head.packet.to);
    if (!peer || peer->user.public_key.size != 32 || memcmp(peer->user.public_key.bytes, head.packet.public_key.bytes, 32)) {
        label = "Peer key changed";
        notifyFailure(head.packet.id, head.packet.channel, meshtastic_Routing_Error_PKI_FAILED, "queued peer key changed");
        return;
    }
    meshtastic_Data data = meshtastic_Data_init_default;
    if (encodeOriginal(head.packet, state.epoch, head.sequence, data))
        sendFrame(data, head.packet.to, head.packet.channel, head.packet.public_key.bytes, true);
}
class Module : public SinglePortModule, private concurrency::OSThread
{
  public:
    Module() : SinglePortModule("delivery", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("DeliveryQueue") {}
    void wake()
    {
        enabled = true;
        setIntervalFromNow(0);
        concurrency::mainDelay.interrupt();
    }

  protected:
    ProcessMessage handleReceived(const meshtastic_MeshPacket &p) override
    {
        Frame f;
        const bool owned = p.decoded.payload.size >= 4 && !memcmp(p.decoded.payload.bytes, "DZQ2", 4);
        if (!framePacket(p) || !decode(p.decoded.payload.bytes, p.decoded.payload.size, f))
            return owned ? ProcessMessage::STOP : ProcessMessage::CONTINUE;
        observeReceived(p);
        PeerStatus::recordReceived(p);
        bool queued = false;
        {
            concurrency::LockGuard lock(&jobLock);
            if (jobCount < JOB_CAP) {
                auto &j = jobs[(jobHead + jobCount++) % JOB_CAP];
                j.kind = JobKind::RECEIVE;
                j.packet = p;
                queued = true;
            }
        }
        if (queued)
            wake();
        return ProcessMessage::STOP;
    }
    int32_t runOnce() override
    {
        if (!ready) {
            healthy = restore();
            ready = true;
            LOG_INFO("Delivery restore ok=%u pending=%u retained=%u", unsigned(healthy), unsigned(state.outCount),
                     unsigned(state.inboxCount));
            label = healthy ? (state.outCount ? "Wait peer RX" : "Idle") : "Storage error";
        }
        for (size_t n = 0; n < JOB_CAP; ++n) {
            Job j;
            {
                concurrency::LockGuard lock(&jobLock);
                if (!jobCount)
                    break;
                j = jobs[jobHead];
                jobHead = (jobHead + 1) % JOB_CAP;
                --jobCount;
                if (j.kind == JobKind::SUBMIT)
                    --reservedSubmits;
            }
            if (j.kind == JobKind::SUBMIT)
                processSubmit(j.packet);
            else if (j.kind == JobKind::RECEIVE)
                processReceived(j.packet);
            else if (j.kind == JobKind::REBIND)
                processRebind(j.packet);
            else
                processCancel(j.packet.id);
        }
        if (healthy) {
            processForget();
            presentInbox();
        }
        tryHead();
        return INT32_MAX;
    }
};
} // namespace
void setup()
{
    instance = new Module();
}
bool nextForPhone(PhoneReplay &cursor, meshtastic_MeshPacket &packet)
{
    concurrency::LockGuard lock(&inboxLock);
    if (cursor.complete || !ready || !healthy)
        return false;
    for (size_t i = 0; i < state.inboxCount && cursor.count < INBOX_CAP; ++i) {
        const auto &stored = state.inbox[i];
        Frame frame;
        if (!decode(stored.decoded.payload.bytes, stored.decoded.payload.size, frame))
            continue;
        bool seen = false;
        for (size_t j = 0; j < cursor.count; ++j)
            seen |= cursor.ids[j] == frame.originalId;
        if (seen)
            continue;
        packet = stored;
        packet.decoded = meshtastic_Data_init_default;
        if (!pb_decode_from_bytes(frame.body, frame.size, meshtastic_Data_fields, &packet.decoded))
            continue;
        packet.id = frame.originalId;
        packet.want_ack = false;
        packet.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
        cursor.ids[cursor.count++] = frame.originalId;
        return true;
    }
    cursor.complete = true;
    return false;
}
void rememberForPhone(PhoneReplay &cursor, const meshtastic_MeshPacket &packet)
{
    if (!packet.id || packet.from != peerId() || packet.to != nodeDB->getNodeNum() ||
        packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        packet.decoded.portnum != meshtastic_PortNum_TEXT_MESSAGE_APP)
        return;
    for (size_t i = 0; i < cursor.count; ++i)
        if (cursor.ids[i] == packet.id)
            return;
    if (cursor.count < INBOX_CAP)
        cursor.ids[cursor.count++] = packet.id;
}
void logStatus()
{
    LOG_INFO("Delivery state ready=%u healthy=%u pending=%u retained=%u status=%s", unsigned(ready), unsigned(healthy),
             unsigned(pendingCount()), unsigned(state.inboxCount), label);
}
bool submit(const meshtastic_MeshPacket &p)
{
    if (!instance || !originalValid(p) || (p.from && p.from != nodeDB->getNodeNum()))
        return false;
    bool queued = false;
    {
        concurrency::LockGuard lock(&jobLock);
        if (jobCount < JOB_CAP && state.outCount + reservedSubmits < CAP) {
            auto &j = jobs[(jobHead + jobCount++) % JOB_CAP];
            j.kind = JobKind::SUBMIT;
            j.packet = p;
            ++reservedSubmits;
            queued = true;
        }
    }
    if (queued)
        instance->wake();
    else
        notifyFailure(p.id, p.channel, meshtastic_Routing_Error_BAD_REQUEST, "outbox full");
    return true;
}
bool verifiedContactImported(const meshtastic_MeshPacket &source, const meshtastic_SharedContact &contact)
{
    // A contact's verification flag is a trusted local client's assertion, never an RF key-rotation request.
    if (source.from != 0 || source.via_mqtt ||
        source.transport_mechanism != meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL ||
        source.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        source.decoded.portnum != meshtastic_PortNum_ADMIN_APP || !contact.manually_verified || !contact.has_user ||
        contact.should_ignore || contact.user.public_key.size != 32 || !nodeDB ||
        !verifiedPeerKey(contact.node_num, contact.user.public_key.bytes))
        return false;
    bool queued = false;
    if (instance) {
        concurrency::LockGuard lock(&jobLock);
        if (jobCount < JOB_CAP) {
            auto &j = jobs[(jobHead + jobCount++) % JOB_CAP];
            j = Job{};
            j.kind = JobKind::REBIND;
            j.packet.to = contact.node_num;
            j.packet.public_key.size = 32;
            memcpy(j.packet.public_key.bytes, contact.user.public_key.bytes, 32);
            queued = true;
        }
    }
    if (queued)
        instance->wake();
    else
        notifyFailure(0, 0, meshtastic_Routing_Error_BAD_REQUEST, "contact repair queue full; repeat verified import");
    return queued;
}
void observeReceived(const meshtastic_MeshPacket &p)
{
    if (dispatching || !instance)
        return;
    if (p.id && isTargetReception(p.from, peerId(), p.which_payload_variant == meshtastic_MeshPacket_decoded_tag,
                                  p.transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA, p.via_mqtt)) {
        gate.received(p.id);
        instance->wake();
    }
}
void sentToRouter(uint32_t id, int result)
{
    if (id == busyId && result != 0) {
        busyId = 0;
        label = "Wait peer RX";
    }
}
void radioComplete(uint32_t id)
{
    if (id == busyId) {
        busyId = 0;
        if (instance)
            instance->wake();
    }
}
bool cancel(uint32_t id)
{
    if (!instance || !id)
        return false;
    bool found = false;
    for (size_t i = 0; i < state.outCount; ++i)
        found |= state.out[i].packet.id == id;
    {
        concurrency::LockGuard lock(&jobLock);
        for (size_t i = 0; i < jobCount; ++i) {
            const auto &j = jobs[(jobHead + i) % JOB_CAP];
            found |= j.kind == JobKind::SUBMIT && j.packet.id == id;
        }
        if (!found || jobCount == JOB_CAP)
            return false;
        auto &j = jobs[(jobHead + jobCount++) % JOB_CAP];
        j = Job{};
        j.packet.id = id;
    }
    instance->wake();
    return true;
}
bool cancelAll()
{
    if (!instance)
        return false;
    {
        concurrency::LockGuard lock(&jobLock);
        if (jobCount == JOB_CAP)
            return false;
        auto &j = jobs[(jobHead + jobCount++) % JOB_CAP];
        j = Job{};
    }
    instance->wake();
    return true;
}
void forgetInbox(uint32_t id)
{
    {
        concurrency::LockGuard lock(&jobLock);
        if (!id) {
            forgetAll = true;
            forgetCount = 0;
        } else if (!forgetAll) {
            bool exists = false;
            for (size_t i = 0; i < forgetCount; ++i)
                exists |= forgetIds[i] == id;
            if (!exists && forgetCount < INBOX_CAP)
                forgetIds[forgetCount++] = id;
        }
    }
    if (instance)
        instance->wake();
}
size_t pendingCount()
{
    return state.outCount + reservedSubmits;
}
const char *statusLabel()
{
    return label;
}
} // namespace DeliveryQueue
#endif
