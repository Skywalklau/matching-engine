#pragma once

#include "spsc_queue.hpp"
#include "../order.hpp"

#include <atomic>
#include <functional>
#include <thread>

// ---------------------------------------------------------------------------
// FeedThread<N>
//
// Wraps a producer thread that generates orders and pushes them into a
// SPSCQueue. The matching engine calls pop() on the other end in a tight loop.
//
// The generator function has signature: bool(Order&)
//   - Fill the Order& with the next order to submit.
//   - Return true  if an order was produced.
//   - Return false when the feed is exhausted (EOF, end of sim, etc.).
//
// Usage:
//   SPSCQueue<Order, 4096> queue;
//   auto gen = [](Order& o) -> bool { /* fill o */ return true; };
//   FeedThread<4096> feed(queue, gen);
//   feed.start();
//   // matching engine loop: while (queue.pop(o)) engine.process(o);
//   feed.stop();
//
// Notes:
//   - The generator must not throw. Exceptions from the thread are not caught.
//   - stop() is idempotent. ~FeedThread() calls stop() automatically.
// ---------------------------------------------------------------------------

template <size_t N>
class FeedThread {
public:
    using Queue     = SPSCQueue<Order, N>;
    using Generator = std::function<bool(Order&)>;

    FeedThread(Queue& queue, Generator gen)
        : queue_(queue), gen_(std::move(gen)) {}

    ~FeedThread() { stop(); }

    FeedThread(const FeedThread&)            = delete;
    FeedThread& operator=(const FeedThread&) = delete;
    FeedThread(FeedThread&&)                 = delete;
    FeedThread& operator=(FeedThread&&)      = delete;

    void start() {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this]() {
            Order order{};
            while (running_.load(std::memory_order_acquire)) {
                if (!gen_(order)) break; // generator exhausted

                // Spin until the queue has space or we are told to stop.
                while (!queue_.push(order)) {
                    if (!running_.load(std::memory_order_acquire)) return;
                }
            }
        });
    }

    // Signal the thread to stop and wait for it to finish.
    void stop() {
        running_.store(false, std::memory_order_release);
        if (thread_.joinable()) thread_.join();
    }

    bool running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

private:
    Queue&            queue_;
    Generator         gen_;
    std::atomic<bool> running_{false};
    std::thread       thread_;
};
