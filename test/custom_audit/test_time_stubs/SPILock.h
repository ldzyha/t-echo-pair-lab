#pragma once
namespace concurrency
{
struct LockGuard {
    explicit LockGuard(void *) {}
};
} // namespace concurrency
inline void *spiLock = nullptr;
