#pragma once
#include <atomic>
#include <cstdint>

namespace canto
{
// One service-thread writer; observers may read the maximum concurrently.
// Ticks are supplied by the caller. This measures servicing, not device latency.
class ServiceIntervals
{
    uint64_t previous = 0;
    bool seen = false;
    std::atomic<uint64_t> maximum{0};
  public:
    void reset() noexcept // Only after the writer has stopped.
    {
        previous = 0;
        seen = false;
        maximum = 0;
    }
    void observe(uint64_t tick) noexcept
    {
        if (seen && tick >= previous)
        {
            const auto gap = tick - previous;
            if (gap > maximum.load(std::memory_order_relaxed))
                maximum.store(gap, std::memory_order_relaxed);
        }
        previous = tick;
        seen = true;
    }
    uint64_t maxTicks() const noexcept { return maximum.load(std::memory_order_relaxed); }
};
}
