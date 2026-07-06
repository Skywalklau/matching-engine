#include "concurrency/spsc_queue.hpp"
#include "concurrency/feed_thread.hpp"
#include "order.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// Single-threaded correctness
// ---------------------------------------------------------------------------

TEST(SPSC, EmptyOnConstruction) {
    SPSCQueue<int, 8> q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0u);
}

TEST(SPSC, CapacityIsNMinusOne) {
    SPSCQueue<int, 8> q;
    EXPECT_EQ(q.capacity(), 7u);
}

TEST(SPSC, PushAndPop) {
    SPSCQueue<int, 8> q;
    EXPECT_TRUE(q.push(42));
    EXPECT_FALSE(q.empty());
    int val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42);
    EXPECT_TRUE(q.empty());
}

TEST(SPSC, PopFromEmptyReturnsFalse) {
    SPSCQueue<int, 8> q;
    int val = 0;
    EXPECT_FALSE(q.pop(val));
}

TEST(SPSC, PushBeyondCapacityReturnsFalse) {
    SPSCQueue<int, 4> q; // capacity = 3
    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4)); // full
}

TEST(SPSC, FillAndDrainVerifiesOrder) {
    SPSCQueue<int, 8> q; // capacity = 7
    for (int i = 0; i < 7; ++i) ASSERT_TRUE(q.push(i));
    EXPECT_FALSE(q.push(99)); // full

    for (int i = 0; i < 7; ++i) {
        int val = -1;
        ASSERT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
    EXPECT_TRUE(q.empty());
}

TEST(SPSC, WrapAroundPreservesOrder) {
    // Push and pop in batches to force the write/read indices to wrap around.
    SPSCQueue<int, 4> q; // capacity = 3, indices wrap at 4

    for (int batch = 0; batch < 4; ++batch) {
        for (int i = 0; i < 3; ++i) ASSERT_TRUE(q.push(batch * 3 + i));
        for (int i = 0; i < 3; ++i) {
            int val = -1;
            ASSERT_TRUE(q.pop(val));
            EXPECT_EQ(val, batch * 3 + i);
        }
    }
    EXPECT_TRUE(q.empty());
}

TEST(SPSC, WorksWithOrderStruct) {
    SPSCQueue<Order, 16> q;
    Order o{1, Side::Buy, OrderType::Limit, 100, 10, 999};
    ASSERT_TRUE(q.push(o));
    Order out{};
    ASSERT_TRUE(q.pop(out));
    EXPECT_EQ(out.id,       1u);
    EXPECT_EQ(out.side,     Side::Buy);
    EXPECT_EQ(out.price,    100u);
    EXPECT_EQ(out.quantity, 10u);
}

// ---------------------------------------------------------------------------
// Multi-threaded correctness
// Verifies no items lost, no corruption, correct order under real concurrency.
// ---------------------------------------------------------------------------

TEST(SPSC, MultiThreaded1MillionInts) {
    static constexpr size_t QSIZE = 1024;
    static constexpr int    COUNT = 1'000'000;

    SPSCQueue<int, QSIZE> q;
    std::vector<int> received;
    received.reserve(COUNT);

    std::thread producer([&]() {
        for (int i = 0; i < COUNT; ++i) {
            while (!q.push(i)) {} // spin on full — wait for consumer
        }
    });

    std::thread consumer([&]() {
        int val = 0;
        int n   = 0;
        while (n < COUNT) {
            if (q.pop(val)) {
                received.push_back(val);
                ++n;
            }
        }
    });

    producer.join();
    consumer.join();

    ASSERT_EQ(static_cast<int>(received.size()), COUNT);
    for (int i = 0; i < COUNT; ++i) {
        EXPECT_EQ(received[static_cast<size_t>(i)], i) << "Mismatch at index " << i;
    }
}

// ---------------------------------------------------------------------------
// FeedThread
// ---------------------------------------------------------------------------

TEST(FeedThread, DeliversSyntheticOrders) {
    static constexpr size_t QSIZE    = 256;
    static constexpr int    N_ORDERS = 1000;

    SPSCQueue<Order, QSIZE> queue;

    // Generator: produces N_ORDERS sequential orders then signals done.
    int counter = 0;
    auto gen = [&](Order& o) -> bool {
        if (counter >= N_ORDERS) return false;
        o = Order{static_cast<uint64_t>(counter), Side::Buy,
                  OrderType::Limit, 100, 10, 0};
        ++counter;
        return true;
    };

    FeedThread<QSIZE> feed(queue, gen);
    feed.start();

    // Consume everything the feed produces.
    std::vector<Order> received;
    received.reserve(N_ORDERS);
    Order o{};
    while (static_cast<int>(received.size()) < N_ORDERS) {
        if (queue.pop(o)) received.push_back(o);
    }

    feed.stop();

    ASSERT_EQ(static_cast<int>(received.size()), N_ORDERS);
    for (int i = 0; i < N_ORDERS; ++i) {
        EXPECT_EQ(received[static_cast<size_t>(i)].id, static_cast<uint64_t>(i));
    }
}