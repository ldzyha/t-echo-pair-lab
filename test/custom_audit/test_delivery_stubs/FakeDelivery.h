#pragma once
#include "mesh/DeliveryQueue.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <pb_decode.h>
#include <pb_encode.h>
#include <string>
#include <vector>
#define TTGO_T_ECHO_PLUS 1
#define ERRNO_OK 0
#define ERRNO_UNKNOWN 1
#define MAX_LORA_PAYLOAD_LEN 255
#define MESHTASTIC_HEADER_LENGTH 16
#define MESHTASTIC_PKC_OVERHEAD 12
#define RX_SRC_LOCAL 1
#define NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK 1
extern std::vector<std::string> trace;
template <typename... T> void fakeLog(const char *format, T... args)
{
    char text[256];
    snprintf(text, sizeof(text), format, args...);
    trace.emplace_back(text);
}
#define LOG_INFO(...) fakeLog(__VA_ARGS__)
#define LOG_WARN(...) fakeLog(__VA_ARGS__)
struct FakeConfig {
    struct {
        uint8_t hop_limit = 3;
        bool config_ok_to_mqtt = false;
    } lora;
};
extern FakeConfig config;
extern meshtastic_User owner;
inline size_t pb_encode_to_bytes(uint8_t *out, size_t n, const pb_msgdesc_t *desc, const void *value)
{
    auto stream = pb_ostream_from_buffer(out, n);
    return pb_encode(&stream, desc, value) ? stream.bytes_written : 0;
}
inline bool pb_decode_from_bytes(const uint8_t *in, size_t n, const pb_msgdesc_t *desc, void *value)
{
    auto stream = pb_istream_from_buffer(in, n);
    return pb_decode(&stream, desc, value);
}
namespace concurrency
{
struct Lock {
    bool spi = false;
};
extern int spiDepth;
struct LockGuard {
    Lock *p;
    explicit LockGuard(Lock *p) : p(p)
    {
        if (p->spi)
            ++spiDepth;
    }
    ~LockGuard()
    {
        if (p->spi)
            --spiDepth;
    }
};
struct OSThread {
    bool enabled = true;
    explicit OSThread(const char *) {}
    virtual ~OSThread() = default;
    virtual int32_t runOnce() = 0;
    void setIntervalFromNow(uint32_t) {}
};
struct Delay {
    void interrupt() {}
};
extern Delay mainDelay;
} // namespace concurrency
extern concurrency::Lock *spiLock;
class File
{
    std::vector<uint8_t> *bytes = nullptr;
    bool writing = false;

  public:
    File() = default;
    File(std::vector<uint8_t> *b, bool w) : bytes(b), writing(w) {}
    explicit operator bool() const { return bytes != nullptr; }
    size_t size() const { return bytes ? bytes->size() : 0; }
    size_t read(uint8_t *out, size_t n);
    size_t write(const uint8_t *in, size_t n);
    void close()
    {
        if (writing)
            trace.push_back("fs:closed-write");
    }
};
class FakeFS
{
  public:
    std::map<std::string, std::vector<uint8_t>> files;
    bool failWrite = false;
    bool exists(const char *path) const { return files.count(path); }
    bool remove(const char *path)
    {
        files.erase(path);
        return true;
    }
    File open(const char *path, const char *mode)
    {
        bool w = *mode == 'w';
        if (!w && !exists(path))
            return {};
        return File(&files[path], w);
    }
};
extern FakeFS fakeFS;
#define FSCom fakeFS
#define FILE_O_READ "r"
#define FILE_O_WRITE "w"
struct meshtastic_NodeInfoLite {
    uint32_t num = 0;
    uint32_t bitfield = 0;
    bool is_ignored = false;
    meshtastic_User user = meshtastic_User_init_default;
};
class NodeDB
{
  public:
    uint32_t self = 0x55667788;
    std::map<uint32_t, meshtastic_NodeInfoLite> nodes;
    uint32_t getNodeNum() const { return self; }
    meshtastic_NodeInfoLite *getMeshNode(uint32_t id)
    {
        auto it = nodes.find(id);
        return it == nodes.end() ? nullptr : &it->second;
    }
};
extern NodeDB *nodeDB;
namespace PeerStatus
{
constexpr uint32_t NODE_A = 0x11223344, NODE_B = 0x55667788;
inline void recordReceived(const meshtastic_MeshPacket &) {}
} // namespace PeerStatus
class Router
{
  public:
    uint32_t transmitting = 0;
    meshtastic_QueueStatus getQueueStatus()
    {
        meshtastic_QueueStatus q{};
        q.free = 8;
        q.maxlen = 8;
        return q;
    }
    bool isTransmittingOrQueued(uint32_t, uint32_t id) { return id == transmitting; }
    bool cancelSending(uint32_t, uint32_t id)
    {
        if (id == transmitting) {
            transmitting = 0;
            return true;
        }
        return false;
    }
};
extern Router *router;
uint32_t generatePacketId();
template <class T> struct FakePool {
    T *allocZeroed() { return new T{}; }
};
extern FakePool<meshtastic_MeshPacket> packetPool;
extern FakePool<meshtastic_ClientNotification> clientNotificationPool;
class MeshService
{
  public:
    void sendToMesh(meshtastic_MeshPacket *, int);
    int sendQueueStatusToPhone(const meshtastic_QueueStatus &, int, uint32_t);
    void sendClientNotification(meshtastic_ClientNotification *p)
    {
        trace.emplace_back(p->message);
        delete p;
    }
};
extern MeshService *service;
class RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error, uint32_t, uint32_t, uint8_t);
};
extern RoutingModule *routingModule;
enum class ProcessMessage { CONTINUE, STOP };
class SinglePortModule
{
  public:
    SinglePortModule(const char *, meshtastic_PortNum) {}
    virtual ~SinglePortModule() = default;

  protected:
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &) = 0;
};
