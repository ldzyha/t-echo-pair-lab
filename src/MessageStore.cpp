#include "configuration.h"
#if HAS_SCREEN
#include "FSCommon.h"
#include "MessageStore.h"
#include "MessageTextUtils.h"
#include "NodeDB.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "gps/RTC.h"
#include "graphics/draw/MessageRenderer.h"
#if defined(TTGO_T_ECHO_PLUS)
#include "mesh/DeliveryQueue.h"
#endif
#include <algorithm>
#include <cstring> // memcpy

#ifndef MESSAGE_TEXT_POOL_SIZE
#define MESSAGE_TEXT_POOL_SIZE (MAX_MESSAGES_SAVED * MAX_MESSAGE_SIZE)
#endif

// Default autosave interval 2 hours, override per device later with -DMESSAGE_AUTOSAVE_INTERVAL_SEC=300 (etc)
#ifndef MESSAGE_AUTOSAVE_INTERVAL_SEC
#define MESSAGE_AUTOSAVE_INTERVAL_SEC (2 * 60 * 60)
#endif

// Global message text pool and state
static char *g_messagePool = nullptr;
static size_t g_poolWritePos = 0;

// Reset pool (called on boot or clear)
static inline void resetMessagePool()
{
    if (!g_messagePool) {
        g_messagePool = static_cast<char *>(malloc(MESSAGE_TEXT_POOL_SIZE));
        if (!g_messagePool) {
            LOG_ERROR("MessageStore: Failed to allocate %d bytes for message pool", MESSAGE_TEXT_POOL_SIZE);
            return;
        }
    }
    g_poolWritePos = 0;
    memset(g_messagePool, 0, MESSAGE_TEXT_POOL_SIZE);
}

// Allocate text in pool and return offset
// If not enough space remains, wrap around (ring buffer style)
static inline uint16_t storeTextInPool(const char *src, size_t len)
{
    if (len >= MAX_MESSAGE_SIZE)
        len = MAX_MESSAGE_SIZE - 1;
    len = MessageTextUtils::completeUtf8Prefix(src, len);

#if defined(TTGO_T_ECHO_PLUS)
    static_assert(MESSAGE_TEXT_POOL_SIZE >= MAX_MESSAGES_SAVED * MAX_MESSAGE_SIZE, "Message slots must fit retained history");
    g_poolWritePos = MessageTextUtils::availableTextSlot<MAX_MESSAGES_SAVED>(messageStore.getLiveMessages(), MAX_MESSAGE_SIZE);
#else
    // Wrap pool if out of space
    if (g_poolWritePos + len + 1 >= MESSAGE_TEXT_POOL_SIZE) {
        g_poolWritePos = 0;
    }
#endif

    uint16_t offset = g_poolWritePos;
    memcpy(&g_messagePool[g_poolWritePos], src, len);
    g_messagePool[g_poolWritePos + len] = '\0';
#if !defined(TTGO_T_ECHO_PLUS)
    g_poolWritePos += (len + 1);
#endif
    return offset;
}

// Retrieve a const pointer to message text by offset
static inline const char *getTextFromPool(uint16_t offset)
{
    if (!g_messagePool || offset >= MESSAGE_TEXT_POOL_SIZE)
        return "";
    return &g_messagePool[offset];
}

// Helper: assign a timestamp (RTC if available, else boot-relative)
static inline void assignTimestamp(StoredMessage &sm)
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs) {
        sm.timestamp = nowSecs;
        sm.isBootRelative = false;
    } else {
        sm.timestamp = millis() / 1000;
        sm.isBootRelative = true;
    }
}

// Generic push with cap (used by live + persisted queues)
template <typename T> static inline void pushWithLimit(std::deque<T> &queue, const T &msg)
{
    if (queue.size() >= MAX_MESSAGES_SAVED)
        queue.pop_front();
    queue.push_back(msg);
}

template <typename T> static inline void pushWithLimit(std::deque<T> &queue, T &&msg)
{
    if (queue.size() >= MAX_MESSAGES_SAVED)
        queue.pop_front();
    queue.emplace_back(std::move(msg));
}

MessageStore::MessageStore(const std::string &label)
{
    filename = "/Messages_" + label + ".msgs";
#if defined(TTGO_T_ECHO_PLUS)
    filename += "2";
#endif
    resetMessagePool(); // initialize text pool on boot
}

// Live message handling (RAM only)
void MessageStore::addLiveMessage(StoredMessage &&msg)
{
    pushWithLimit(liveMessages, std::move(msg));
}
void MessageStore::addLiveMessage(const StoredMessage &msg)
{
    pushWithLimit(liveMessages, msg);
}

#if ENABLE_MESSAGE_PERSISTENCE
static bool g_messageStoreHasUnsavedChanges = false;
static uint32_t g_lastAutoSaveMs = 0; // last time we actually saved

static inline uint32_t autosaveIntervalMs()
{
    uint32_t sec = (uint32_t)MESSAGE_AUTOSAVE_INTERVAL_SEC;
    if (sec < 60)
        sec = 60;
    return sec * 1000UL;
}

static inline bool reachedMs(uint32_t now, uint32_t target)
{
    return (int32_t)(now - target) >= 0;
}

// Mark new messages in RAM that need to be saved later
static inline void markMessageStoreUnsaved()
{
    g_messageStoreHasUnsavedChanges = true;

    if (g_lastAutoSaveMs == 0) {
        g_lastAutoSaveMs = millis();
    }
}

// Called periodically from the main loop in main.cpp
static inline void autosaveTick(MessageStore *store)
{
    if (!store)
        return;

    uint32_t now = millis();

    if (g_lastAutoSaveMs == 0) {
        g_lastAutoSaveMs = now;
        return;
    }

    if (!reachedMs(now, g_lastAutoSaveMs + autosaveIntervalMs()))
        return;

    // Autosave interval reached, only save if there are unsaved messages.
    if (g_messageStoreHasUnsavedChanges) {
        LOG_INFO("Autosaving MessageStore to flash");
        store->saveToFlash();
    } else {
        LOG_INFO("Autosave skipped, no changes to save");
        g_lastAutoSaveMs = now;
    }
}
#endif

// Add from incoming/outgoing packet
const StoredMessage &MessageStore::addFromPacket(const meshtastic_MeshPacket &packet)
{
#if defined(TTGO_T_ECHO_PLUS)
    const uint32_t sender = packet.from ? packet.from : nodeDB->getNodeNum();
    if (packet.id != 0) {
        for (const auto &existing : liveMessages)
            if (existing.sender == sender && existing.packetId == packet.id)
                return existing;
    }
#endif
    StoredMessage sm;
    assignTimestamp(sm);
    sm.channelIndex = packet.channel;

    const char *payload = reinterpret_cast<const char *>(packet.decoded.payload.bytes);
    size_t len = strnlen(payload, std::min(size_t(packet.decoded.payload.size), size_t(MAX_MESSAGE_SIZE - 1)));
    len = MessageTextUtils::completeUtf8Prefix(payload, len);
    sm.textOffset = storeTextInPool(payload, len);
    sm.textLength = len;

    // Determine sender
    uint32_t localNode = nodeDB->getNodeNum();
    sm.sender = (packet.from == 0) ? localNode : packet.from;
#if defined(TTGO_T_ECHO_PLUS)
    sm.unread = packet.from != 0 && packet.from != localNode;
    sm.packetId = packet.id;
#endif

    sm.dest = packet.to;

    bool isDM = (sm.dest != 0 && sm.dest != NODENUM_BROADCAST);

    if (packet.from == 0 || packet.from == localNode) {
        sm.type = isDM ? MessageType::DM_TO_US : MessageType::BROADCAST;
        sm.ackStatus = AckStatus::NONE;
    } else {
        sm.type = isDM ? MessageType::DM_TO_US : MessageType::BROADCAST;
        sm.ackStatus = AckStatus::ACKED;
    }

    addLiveMessage(sm);

#if ENABLE_MESSAGE_PERSISTENCE
    markMessageStoreUnsaved();
#endif

    return liveMessages.back();
}

#if defined(TTGO_T_ECHO_PLUS)
bool MessageStore::containsPacket(uint32_t sender, uint32_t packetId) const
{
    if (!packetId)
        return false;
    for (const auto &message : liveMessages)
        if (message.sender == sender && message.packetId == packetId)
            return true;
    return false;
}

void MessageStore::markAcknowledged(uint32_t packetId)
{
    for (auto &message : liveMessages) {
        if (packetId && message.packetId == packetId && message.sender == nodeDB->getNodeNum()) {
            message.ackStatus = AckStatus::ACKED;
#if ENABLE_MESSAGE_PERSISTENCE
            markMessageStoreUnsaved();
#endif
        }
    }
}

uint8_t MessageStore::unreadCount() const
{
    unsigned count = 0;
    for (const auto &message : liveMessages)
        count += message.unread;
    return std::min(count, 99U);
}

void MessageStore::markMessagesRead(int channel, uint32_t peer)
{
    for (auto &message : liveMessages) {
        if (MessageTextUtils::matchesThread(message.sender, message.dest, message.channelIndex, channel, peer))
            message.unread = false;
    }
}
#endif

// Outgoing/manual message
void MessageStore::addFromString(uint32_t sender, uint8_t channelIndex, const std::string &text)
{
    StoredMessage sm;

    // Always use our local time (helper handles RTC vs boot time)
    assignTimestamp(sm);

    sm.sender = sender;
    sm.channelIndex = channelIndex;
    const size_t length = MessageTextUtils::completeUtf8Prefix(text.c_str(), std::min(text.size(), size_t(MAX_MESSAGE_SIZE - 1)));
    sm.textOffset = storeTextInPool(text.c_str(), length);
    sm.textLength = length;

    // Use the provided destination
    sm.dest = sender;
    sm.type = MessageType::DM_TO_US;

    // Outgoing messages always start with unknown ack status
    sm.ackStatus = AckStatus::NONE;

    addLiveMessage(sm);

#if ENABLE_MESSAGE_PERSISTENCE
    markMessageStoreUnsaved();
#endif
}

#if ENABLE_MESSAGE_PERSISTENCE

// Compact, fixed-size on-flash representation using offset + length
struct __attribute__((packed)) StoredMessageRecord {
    uint32_t timestamp;
    uint32_t sender;
    uint8_t channelIndex;
    uint32_t dest;
    uint8_t isBootRelative;
    uint8_t ackStatus;           // static_cast<uint8_t>(AckStatus)
    uint8_t type;                // static_cast<uint8_t>(MessageType)
    uint16_t textLength;         // message length
    char text[MAX_MESSAGE_SIZE]; // store actual text here
};

// Serialize one StoredMessage to flash
static inline void writeMessageRecord(SafeFile &f, const StoredMessage &m)
{
    StoredMessageRecord rec = {};
    rec.timestamp = m.timestamp;
    rec.sender = m.sender;
    rec.channelIndex = m.channelIndex;
    rec.dest = m.dest;
    rec.isBootRelative = m.isBootRelative;
    rec.ackStatus = static_cast<uint8_t>(m.ackStatus);
    rec.type = static_cast<uint8_t>(m.type);
    rec.textLength = m.textLength;

    // Copy the actual text into the record from RAM pool
    const char *txt = getTextFromPool(m.textOffset);
    strncpy(rec.text, txt, MAX_MESSAGE_SIZE - 1);
    rec.text[MAX_MESSAGE_SIZE - 1] = '\0';

    f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
#if defined(TTGO_T_ECHO_PLUS)
    f.write(reinterpret_cast<const uint8_t *>(&m.packetId), sizeof(m.packetId));
#endif
}

// Deserialize one StoredMessage from flash; returns false on short read
static inline bool readMessageRecord(File &f, StoredMessage &m, bool legacy = false)
{
    StoredMessageRecord rec = {};
    if (f.readBytes(reinterpret_cast<char *>(&rec), sizeof(rec)) != sizeof(rec))
        return false;
#if defined(TTGO_T_ECHO_PLUS)
    if (!legacy && f.readBytes(reinterpret_cast<char *>(&m.packetId), sizeof(m.packetId)) != sizeof(m.packetId))
        return false;
#else
    (void)legacy;
#endif

    m.timestamp = rec.timestamp;
    m.sender = rec.sender;
    m.channelIndex = rec.channelIndex;
    m.dest = rec.dest;
    m.isBootRelative = rec.isBootRelative;
    m.ackStatus = static_cast<AckStatus>(rec.ackStatus);
    m.type = static_cast<MessageType>(rec.type);
    m.textLength = rec.textLength;

    // 💡 Re-store text into pool and update offset
    m.textLength = strnlen(rec.text, MAX_MESSAGE_SIZE - 1);
    m.textOffset = storeTextInPool(rec.text, m.textLength);

    return true;
}

void MessageStore::saveToFlash()
{
#ifdef FSCom
    // Ensure root exists
    spiLock->lock();
    FSCom.mkdir("/");
    spiLock->unlock();

    SafeFile f(filename.c_str(), false);

    spiLock->lock();
    uint8_t count = static_cast<uint8_t>(liveMessages.size());
    if (count > MAX_MESSAGES_SAVED)
        count = MAX_MESSAGES_SAVED;
    f.write(&count, 1);

    for (uint8_t i = 0; i < count; ++i) {
        writeMessageRecord(f, liveMessages[i]);
    }
    spiLock->unlock();

    f.close();
#endif

    // Reset autosave state after any save
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = millis();
}

void MessageStore::loadFromFlash()
{
    std::deque<StoredMessage>().swap(liveMessages);
    resetMessagePool(); // reset pool when loading

#ifdef FSCom
    concurrency::LockGuard guard(spiLock);

    std::string source = filename;
    bool legacy = false;
#if defined(TTGO_T_ECHO_PLUS)
    if (!FSCom.exists(source.c_str())) {
        source.pop_back(); // Import the original .msgs format once; new saves use .msgs2.
        legacy = true;
    }
#endif
    if (!FSCom.exists(source.c_str()))
        return;

    auto f = FSCom.open(source.c_str(), FILE_O_READ);
    if (!f)
        return;

    uint8_t count = 0;
    f.readBytes(reinterpret_cast<char *>(&count), 1);
    if (count > MAX_MESSAGES_SAVED)
        count = MAX_MESSAGES_SAVED;

    for (uint8_t i = 0; i < count; ++i) {
        StoredMessage m;
        if (!readMessageRecord(f, m, legacy))
            break;
        liveMessages.push_back(m);
    }

    f.close();
#endif
    // Loading messages does not trigger an autosave
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = millis();
}

#else
// If persistence is disabled, these functions become no-ops
void MessageStore::saveToFlash() {}
void MessageStore::loadFromFlash() {}
#endif

// Clear all messages (RAM + persisted queue)
void MessageStore::clearAllMessages()
{
#if defined(TTGO_T_ECHO_PLUS)
    DeliveryQueue::forgetInbox(0);
#endif
    std::deque<StoredMessage>().swap(liveMessages);
    resetMessagePool();

#ifdef FSCom
    SafeFile f(filename.c_str(), false);
    uint8_t count = 0;
    {
        concurrency::LockGuard guard(spiLock);
        f.write(&count, 1); // SafeFile open/close acquire their own lock.
    }
    f.close();
#endif

#if ENABLE_MESSAGE_PERSISTENCE
    g_messageStoreHasUnsavedChanges = false;
    g_lastAutoSaveMs = millis();
#endif
}

static void forgetStoredMessage(const StoredMessage &message)
{
#if defined(TTGO_T_ECHO_PLUS)
    if (message.packetId && message.sender != nodeDB->getNodeNum())
        DeliveryQueue::forgetInbox(message.packetId);
#else
    (void)message;
#endif
}

template <typename Predicate> static void eraseIf(std::deque<StoredMessage> &deque, Predicate pred)
{
    for (auto it = deque.begin(); it != deque.end();) {
        if (pred(*it)) {
            forgetStoredMessage(*it);
            it = deque.erase(it);
        } else
            ++it;
    }
}

// Delete oldest message (RAM + persisted queue)
void MessageStore::deleteOldestMessage()
{
    MessageTextUtils::eraseOldestMatching(
        liveMessages, [](const StoredMessage &) { return true; }, forgetStoredMessage);
    saveToFlash();
}

// Delete oldest message in a specific channel
void MessageStore::deleteOldestMessageInChannel(uint8_t channel)
{
    auto pred = [channel](const StoredMessage &m) { return m.type == MessageType::BROADCAST && m.channelIndex == channel; };
    MessageTextUtils::eraseOldestMatching(liveMessages, pred, forgetStoredMessage);
    saveToFlash();
}

void MessageStore::deleteAllMessagesInChannel(uint8_t channel)
{
    auto pred = [channel](const StoredMessage &m) { return m.type == MessageType::BROADCAST && m.channelIndex == channel; };
    eraseIf(liveMessages, pred);
    saveToFlash();
}

void MessageStore::deleteAllMessagesWithPeer(uint32_t peer)
{
    uint32_t local = nodeDB->getNodeNum();
    auto pred = [&](const StoredMessage &m) {
        if (m.type != MessageType::DM_TO_US)
            return false;
        uint32_t other = (m.sender == local) ? m.dest : m.sender;
        return other == peer;
    };
    eraseIf(liveMessages, pred);
    saveToFlash();
}

// Delete oldest message in a direct chat with a node
void MessageStore::deleteOldestMessageWithPeer(uint32_t peer)
{
    auto pred = [peer](const StoredMessage &m) {
        if (m.type != MessageType::DM_TO_US)
            return false;
        uint32_t other = (m.sender == nodeDB->getNodeNum()) ? m.dest : m.sender;
        return other == peer;
    };
    MessageTextUtils::eraseOldestMatching(liveMessages, pred, forgetStoredMessage);
    saveToFlash();
}

std::deque<StoredMessage> MessageStore::getChannelMessages(uint8_t channel) const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (m.type == MessageType::BROADCAST && m.channelIndex == channel) {
            result.push_back(m);
        }
    }
    return result;
}

std::deque<StoredMessage> MessageStore::getDirectMessages() const
{
    std::deque<StoredMessage> result;
    for (const auto &m : liveMessages) {
        if (m.type == MessageType::DM_TO_US) {
            result.push_back(m);
        }
    }
    return result;
}

// Upgrade boot-relative timestamps once RTC is valid
// Only same-boot boot-relative messages are healed.
// Persisted boot-relative messages from old boots stay ??? forever.
void MessageStore::upgradeBootRelativeTimestamps()
{
    uint32_t nowSecs = getValidTime(RTCQuality::RTCQualityDevice, true);
    if (nowSecs == 0)
        return; // Still no valid RTC

    uint32_t bootNow = millis() / 1000;

    auto fix = [&](std::deque<StoredMessage> &dq) {
        for (auto &m : dq) {
            if (m.isBootRelative && m.timestamp <= bootNow) {
                uint32_t bootOffset = nowSecs - bootNow;
                m.timestamp += bootOffset;
                m.isBootRelative = false;
            }
        }
    };
    fix(liveMessages);
}

const char *MessageStore::getText(const StoredMessage &msg)
{
    // Wrapper around the internal helper
    return getTextFromPool(msg.textOffset);
}

uint16_t MessageStore::storeText(const char *src, size_t len)
{
    // Wrapper around the internal helper
    return storeTextInPool(src, len);
}

#if ENABLE_MESSAGE_PERSISTENCE
void messageStoreAutosaveTick()
{
    // Called from the main loop to check autosave timing
    autosaveTick(&messageStore);
}
#endif

// Global definition
MessageStore messageStore("default");
#endif
