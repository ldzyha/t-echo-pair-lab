#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern bool failRtcMarkerWrite;
class File
{
  public:
    explicit File(std::vector<uint8_t> *data = nullptr) : data(data) {}
    explicit operator bool() const { return data != nullptr; }
    size_t size() const { return data->size(); }
    size_t read(uint8_t *out, size_t length)
    {
        if (!data || data->size() < length)
            return 0;
        std::memcpy(out, data->data(), length);
        return length;
    }
    size_t write(const uint8_t *value, size_t length)
    {
        if (failRtcMarkerWrite)
            return 0;
        data->insert(data->end(), value, value + length);
        return length;
    }
    void flush() {}
    void close() {}

  private:
    std::vector<uint8_t> *data;
};
class FakeRtcFilesystem
{
  public:
    std::unordered_map<std::string, std::vector<uint8_t>> files;
    bool exists(const char *path) const { return files.count(path); }
    void remove(const char *path) { files.erase(path); }
    File open(const char *path, const char *mode)
    {
        if (*mode == 'r' && !exists(path))
            return File();
        return File(&files[path]);
    }
};
extern FakeRtcFilesystem fakeRtcFilesystem;
#define FSCom fakeRtcFilesystem
#define FILE_O_READ "r"
#define FILE_O_WRITE "w"
