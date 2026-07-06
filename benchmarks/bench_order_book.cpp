#include "order.hpp"
#include "order_book.hpp"
#include "order_book_v1.hpp"
#include "order_book_v2.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------
using Clock = std::chrono::high_resolution_clock;

static int64_t ns(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count();
}

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift64) — fast, no stdlib dependency on hot path.
// ---------------------------------------------------------------------------
struct RNG {
    uint64_t s;
    explicit RNG(uint64_t seed = 0xdeadbeef42ULL) noexcept : s(seed) {}
    uint64_t next() noexcept {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        return s;
    }
    uint32_t range(uint32_t lo, uint32_t hi) noexcept {
        return lo + static_cast<uint32_t>(next() % static_cast<uint64_t>(hi - lo + 1u));
    }
};

// ---------------------------------------------------------------------------
// Pre-generate all orders upfront so RNG cost is excluded from timings.
// ---------------------------------------------------------------------------
static std::vector<Order> generateOrders(size_t n, uint32_t p_lo, uint32_t p_hi) {
    RNG rng;
    std::vector<Order> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        v.push_back(Order{
            static_cast<uint64_t>(i + 1u),
            (i % 2u == 0u) ? Side::Buy : Side::Sell,
            OrderType::Limit,
            rng.range(p_lo, p_hi),
            10u,
            static_cast<uint64_t>(i)
        });
    }
    return v;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------
struct Stats { int64_t p50; int64_t p99; int64_t p999; };

static Stats computeStats(std::vector<int64_t> v) { // by value — sorts in place
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return { v[n * 50u / 100u], v[n * 99u / 100u], v[n * 999u / 1000u] };
}

// ---------------------------------------------------------------------------
// Core benchmark — templated so V1, V2, V3 share identical logic.
//
// Strategy: sliding window of WINDOW live orders.
// Each iteration: cancel the oldest resting order, add a new one.
// Pool never exceeds WINDOW objects → safe for V2/V3 (MAX_ORDERS=4096).
// ---------------------------------------------------------------------------
struct Result { Stats add; Stats cancel; };

template <typename Book>
static Result benchBook(const std::vector<Order>& orders, size_t window) {
    Book book;

    // Seed the book to steady state before measuring.
    for (size_t i = 0; i < window; ++i) {
        book.addOrder(orders[i]);
    }

    const size_t total = orders.size();
    std::vector<int64_t> add_lat;
    std::vector<int64_t> cancel_lat;
    add_lat.reserve(total - window);
    cancel_lat.reserve(total - window);

    for (size_t i = window; i < total; ++i) {
        // cancelOrder — the order added `window` steps ago is still in the book.
        {
            const auto t0 = Clock::now();
            book.cancelOrder(orders[i - window].id);
            const auto t1 = Clock::now();
            cancel_lat.push_back(ns(t0, t1));
        }
        // addOrder
        {
            const auto t0 = Clock::now();
            book.addOrder(orders[i]);
            const auto t1 = Clock::now();
            add_lat.push_back(ns(t0, t1));
        }
    }

    return { computeStats(add_lat), computeStats(cancel_lat) };
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------
static void printResult(const char* label, const Result& r) {
    std::cout << label << "\n"
              << "  addOrder    "
              << "p50=" << std::setw(6) << r.add.p50    << " ns  "
              << "p99=" << std::setw(6) << r.add.p99    << " ns  "
              << "p999=" << std::setw(8) << r.add.p999  << " ns\n"
              << "  cancelOrder "
              << "p50=" << std::setw(6) << r.cancel.p50    << " ns  "
              << "p99=" << std::setw(6) << r.cancel.p99    << " ns  "
              << "p999=" << std::setw(8) << r.cancel.p999  << " ns\n\n";
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    // 21 price levels, window=1000 → avg depth ~47 orders/level.
    // V1 cancelOrder scans ~23 nodes on average (O(k)).
    // V2 cancelOrder is O(1) pointer rewire — expect clear improvement.
    // V3 addOrder replaces O(log n) BST lookup with O(1) array access.
    static constexpr size_t   N      = 600'000u;
    static constexpr size_t   WINDOW = 1'000u;
    static constexpr uint32_t P_LO   = 100u;
    static constexpr uint32_t P_HI   = 120u;

    // V2 and V3 pool constraints: MAX_ORDERS=4096, MAX_LEVELS=1024.
    static_assert(WINDOW <= 4096u, "WINDOW exceeds pool capacity");

    const auto orders = generateOrders(N, P_LO, P_HI);
    const size_t iters = N - WINDOW;

    std::cout
        << "============================================================\n"
        << "  OrderBook Latency Benchmark\n"
        << "  Iterations  : " << iters << " per version\n"
        << "  Window      : " << WINDOW << " simultaneous resting orders\n"
        << "  Price range : [" << P_LO << ", " << P_HI << "]"
        << " (" << (P_HI - P_LO + 1u) << " levels,"
        << " ~" << WINDOW / (P_HI - P_LO + 1u) << " orders/level)\n"
        << "  Note: clock overhead ~20-30 ns is included in each sample.\n"
        << "============================================================\n\n";

    printResult(
        "V1  std::map + std::deque   [O(log n) add | O(log n + k) cancel]",
        benchBook<OrderBookV1>(orders, WINDOW));

    printResult(
        "V2  std::map + linked list  [O(log n) add | O(1) cancel        ]",
        benchBook<OrderBookV2>(orders, WINDOW));

    printResult(
        "V3  flat array + bitset     [O(1) add     | O(1) cancel        ]",
        benchBook<OrderBook>(orders, WINDOW));

    return 0;
}
