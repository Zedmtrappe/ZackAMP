// ZackAMP — lock-free primitives for handing objects between non-real-time threads
// and the audio thread.
//
// Ownership model:
//   * Objects are built (allocated) on a non-real-time thread.
//   * They are published into a single-slot mailbox (PendingSlot). The audio thread
//     takes ownership with one atomic exchange.
//   * When the audio thread is finished with an object it never deletes it; it pushes
//     the pointer into a bounded SPSC RetireQueue. A non-real-time thread drains that
//     queue and deletes the objects (GarbageCollector::collect()).
//   * If the retire queue is ever full, the audio thread parks the pointer in a small
//     fixed overflow array and retries next block — it never blocks or frees.
#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>

namespace af::dsp
{
/** Base for anything that can be retired from the audio thread. */
struct Retirable
{
    virtual ~Retirable() = default;
};

/** Bounded single-producer / single-consumer queue of trivially copyable values. */
template <typename T, size_t Capacity>
class SPSCQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

public:
    bool push (const T& v) noexcept
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed);
        const size_t r = readIdx.load (std::memory_order_acquire);
        if (w - r >= Capacity)
            return false;
        buffer[w & (Capacity - 1)] = v;
        writeIdx.store (w + 1, std::memory_order_release);
        return true;
    }

    bool pop (T& out) noexcept
    {
        const size_t r = readIdx.load (std::memory_order_relaxed);
        const size_t w = writeIdx.load (std::memory_order_acquire);
        if (r == w)
            return false;
        out = buffer[r & (Capacity - 1)];
        readIdx.store (r + 1, std::memory_order_release);
        return true;
    }

private:
    std::array<T, Capacity> buffer {};
    alignas (64) std::atomic<size_t> writeIdx { 0 };
    alignas (64) std::atomic<size_t> readIdx { 0 };
};

/** Audio thread -> non-RT deletion queue. */
class GarbageCollector
{
public:
    /** Must only be destroyed once the audio thread has stopped using it. */
    ~GarbageCollector()
    {
        collect();
        for (auto* s : overflow)
            delete s;
    }

    /** Audio thread: never blocks, never frees. */
    void retire (Retirable* obj) noexcept
    {
        if (obj == nullptr)
            return;
        flushOverflow();
        if (queue.push (obj))
            return;
        for (auto& slot : overflow)
            if (slot == nullptr)
            {
                slot = obj;
                return;
            }
        // Queue and overflow both full (needs > 80 un-collected swaps): the object is
        // leaked rather than freed on the audio thread, and counted for diagnostics.
        leaks.fetch_add (1, std::memory_order_relaxed);
    }

    /** Non-RT threads: deletes everything retired so far. Several non-RT threads may call
        this (UI timer, IR loader, device thread), so the consumer side is serialised with a
        mutex; the audio-thread producer side stays lock-free. */
    void collect()
    {
        std::lock_guard<std::mutex> lock (consumerLock);
        Retirable* obj = nullptr;
        while (queue.pop (obj))
            delete obj;
    }

    int getLeakCount() const noexcept { return leaks.load(); }

private:
    void flushOverflow() noexcept
    {
        for (auto& slot : overflow)
            if (slot != nullptr && queue.push (slot))
                slot = nullptr;
    }

    SPSCQueue<Retirable*, 64> queue;
    std::mutex consumerLock; // never taken by the audio thread
    std::array<Retirable*, 16> overflow {}; // audio-thread only
    std::atomic<int> leaks { 0 };
};

/** Single-slot mailbox: non-RT publishes, audio thread takes. */
template <typename T>
class PendingSlot
{
public:
    ~PendingSlot() { delete pending.exchange (nullptr); }

    /** Non-RT. Replaces any unconsumed object (which the audio thread never saw). */
    void publish (std::unique_ptr<T> obj)
    {
        delete pending.exchange (obj.release(), std::memory_order_acq_rel);
    }

    /** Audio thread. Returns nullptr when nothing new is waiting. */
    T* take() noexcept { return pending.exchange (nullptr, std::memory_order_acq_rel); }

    bool hasPending() const noexcept { return pending.load (std::memory_order_acquire) != nullptr; }

private:
    std::atomic<T*> pending { nullptr };
};

} // namespace af::dsp
