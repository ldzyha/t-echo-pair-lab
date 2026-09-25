#pragma once
#include <cstdint>
namespace concurrency
{
class OSThread
{
  public:
    explicit OSThread(const char *) {}
    virtual ~OSThread() = default;
    virtual int32_t runOnce() = 0;
    void setIntervalFromNow(unsigned) {}
};
struct TestDelay {
    unsigned wakes = 0;
    void interruptFromISR(int *) { ++wakes; }
};
extern TestDelay mainDelay;
} // namespace concurrency
