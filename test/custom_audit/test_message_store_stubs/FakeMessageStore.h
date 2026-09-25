#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#define HAS_SCREEN 1
#define TTGO_T_ECHO_PLUS 1
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
constexpr uint32_t NODENUM_BROADCAST = UINT32_MAX;
extern uint32_t fakeMillis;
inline uint32_t millis()
{
    return fakeMillis;
}
namespace concurrency
{
struct Lock {
    int depth = 0;
    void lock() { assert(depth++ == 0); }
    void unlock() { assert(--depth == 0); }
};
struct LockGuard {
    Lock *lock;
    explicit LockGuard(Lock *value) : lock(value) { lock->lock(); }
    ~LockGuard() { lock->unlock(); }
};
} // namespace concurrency
extern concurrency::Lock *spiLock;
struct File {
    std::vector<uint8_t> *data = nullptr;
    size_t position = 0;
    explicit operator bool() const { return data; }
    size_t readBytes(char *out, size_t count)
    {
        assert(spiLock->depth == 1);
        if (!data)
            return 0;
        count = std::min(count, data->size() - position);
        memcpy(out, data->data() + position, count);
        position += count;
        return count;
    }
    void close() {}
};
struct FakeFS {
    std::map<std::string, std::vector<uint8_t>> files;
    bool exists(const char *path) const
    {
        assert(spiLock->depth == 1);
        return files.count(path);
    }
    void mkdir(const char *) { assert(spiLock->depth == 1); }
    File open(const char *path, int)
    {
        assert(spiLock->depth == 1);
        return files.count(path) ? File{&files[path]} : File{};
    }
};
extern FakeFS fakeFS;
#define FSCom fakeFS
#define FILE_O_READ 0
class SafeFile
{
    std::string path;
    std::vector<uint8_t> data;

  public:
    SafeFile(const char *name, bool) : path(name) { assert(spiLock->depth == 0); }
    size_t write(const uint8_t *p, size_t count)
    {
        assert(spiLock->depth == 1);
        data.insert(data.end(), p, p + count);
        return count;
    }
    bool close()
    {
        assert(spiLock->depth == 0);
        fakeFS.files[path] = data;
        return true;
    }
};
struct NodeDB {
    uint32_t getNodeNum() const { return 0x55667788; }
};
extern NodeDB *nodeDB;
enum class RTCQuality { RTCQualityDevice };
inline uint32_t getValidTime(RTCQuality, bool = false)
{
    return 1800000000;
}
