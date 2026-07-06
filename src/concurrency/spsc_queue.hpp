#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// SPSCQueue<T, N>
//
// Lock-free single-producer single-consumer ring buffer.
//
// Design:
//   - Power-of-two N: bitmask indexing replaces modulo (1 cycle vs ~40).
//   - write_ and read_ on separate cache lines (alignas(64)) — false sharing
//     between them is the most common SPSC performance bug.
//   - Minimum correct memory ordering:
//       Producer loads read_  with acquire  (sees consumer's progress)
//       Producer stores write_ with release (publishes buffer write)
//       Consumer loads write_ with acquire  (sees producer's buffer write)
//       Consumer stores read_  with release (publishes freed slot)
//   - T must be trivially copyable. Enforced at compile time.
//     Trivially copyable means: no user destructor, no virtual functions,
//     all members trivially copyable. Order satisfies this.
//
// Thread safety:
//   push() — PRODUCER thread only.
//   pop()  — CONSUMER thread only.
//   empty(), size() — approximate; safe to call from either thread.
//
// Capacity:
//   One slot is sacrificed to distinguish full from empty.
//   Usable capacity = N - 1.
//   Choose N as the next power of two above your required capacity.
//   e.g. to hold 1000 orders: N = 1024, capacity() = 1023.
// ---------------------------------------------------------------------------

template <typename T, size_t N>
class SPSCQueue {
    static_assert(N >= 2 && (N & (N - 1)) == 0,
                  "SPSCQueue: N must be a power of two >= 2");
    static_assert(std::is_trivially_copyable_v<T>,
                  "SPSCQueue: T must be trivially copyable for safe lock-free copy");

    static constexpr size_t MASK = N - 1;

public:
    SPSCQueue()  = default;
    ~SPSCQueue() = default;

    SPSCQueue(const SPSCQueue&)            = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;
    SPSCQueue(SPSCQueue&&)                 = delete;
    SPSCQueue& operator=(SPSCQueue&&)      = delete;

    // ---------------------------------------------------------------------------
    // push — PRODUCER thread only.
    // Copies or moves val into the next free slot.
    // Returns false if the queue is full (caller should retry or back off).
    // ---------------------------------------------------------------------------
    template <typename U>
    bool push(U&& val) noexcept {
        const size_t w    = write_.load(std::memory_order_relaxed); // producer's own var
        const size_t next = (w + 1) & MASK;

        // Acquire: synchronise with consumer's release store on read_.
        // Ensures we see freed slots before deciding the queue is full.
        if (next == read_.load(std::memory_order_acquire)) {
            return false; // full
        }

        buffer_[w] = std::forward<U>(val);

        // Release: all writes to buffer_[w] must be visible before write_ advances.
        // The consumer's acquire load of write_ synchronises with this.
        write_.store(next, std::memory_order_release);
        return true;
    }

    // ---------------------------------------------------------------------------
    // pop — CONSUMER thread only.
    // Moves the front item into val.
    // Returns false if the queue is empty (caller should retry or back off).
    // ---------------------------------------------------------------------------
    bool pop(T& val) noexcept {
        const size_t r = read_.load(std::memory_order_relaxed); // consumer's own var

        // Acquire: synchronise with producer's release store on write_.
        // Guarantees we see the buffer_[r] write that preceded write_'s store.
        if (r == write_.load(std::memory_order_acquire)) {
            return false; // empty
        }

        val = buffer_[r];

        // Release: buffer_[r] has been consumed; advance read_ to free the slot.
        // The producer's acquire load of read_ synchronises with this.
        read_.store((r + 1) & MASK, std::memory_order_release);
        return true;
    }

    // ---------------------------------------------------------------------------
    // Diagnostics — approximate under concurrent access.
    // ---------------------------------------------------------------------------
    bool empty() const noexcept {
        return write_.load(std::memory_order_acquire) ==
               read_.load(std::memory_order_acquire);
    }

    // Unsigned subtraction wraps correctly for power-of-two N.
    // (w - r) & MASK == (w - r) mod N, which is the item count.
    size_t size() const noexcept {
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t r = read_.load(std::memory_order_acquire);
        return (w - r) & MASK;
    }

    static constexpr size_t capacity() noexcept { return N - 1; }

private:
    // Each field on its own 64-byte cache line.
    // write_ is written only by the producer.
    // read_  is written only by the consumer.
    // If they shared a cache line the hardware would bounce it between
    // cores on every access — false sharing — adding ~100ns per operation.
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};

    // Buffer on its own cache line, separate from the control variables.
    alignas(64) T buffer_[N];
};
