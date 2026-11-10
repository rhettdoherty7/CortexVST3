#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace cortex
{

/** Single-producer / single-consumer lock-free ring. One thread calls push(), one other thread
    calls pop(). Storage is allocated once in the constructor; push and pop never allocate or lock.
    When full, push() drops the new item and returns false. */
template <typename T>
class SpscFifo
{
public:
    /** capacity must be a power of two; one slot is kept empty. */
    explicit SpscFifo (size_t capacity) : buffer (capacity), mask (capacity - 1) {}

    bool push (const T& item) noexcept
    {
        const auto w    = writeIndex.load (std::memory_order_relaxed);
        const auto next = (w + 1) & mask;
        if (next == readIndex.load (std::memory_order_acquire))
            return false;

        buffer[w] = item;
        writeIndex.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const auto r = readIndex.load (std::memory_order_relaxed);
        if (r == writeIndex.load (std::memory_order_acquire))
            return false;

        out = buffer[r];
        readIndex.store ((r + 1) & mask, std::memory_order_release);
        return true;
    }

private:
    std::vector<T> buffer;
    size_t mask;
    std::atomic<size_t> writeIndex { 0 };
    std::atomic<size_t> readIndex { 0 };
};

} // namespace cortex
