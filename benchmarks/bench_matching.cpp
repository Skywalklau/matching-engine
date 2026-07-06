#include "matching_engine.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>

// ---------------------------------------------------------------------------
// MatchingEngine Throughput Benchmark
//
// Measures end-to-end orders/second through the full matching pipeline:
// process() → fillAgainst() → cancelOrder() → Trade emission.
//
// Workload: alternating passive ask + aggressive buy.
//   - Passive ask rests on the book at a price in [101, 110].
//   - Aggressive buy at price 115 always crosses → exact fill → 1 Trade.
// This keeps the book bounded (never more than 1 resting order at a time)
// while exercising the full matching hot path on every other order.
// ---------------------------------------------------------------------------

using Clock = std::chrono::high_resolution_clock;

// Xorshift64 — fast, deterministic, no stdlib overhead.
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

int main() {
    static constexpr size_t N = 1'000'000u; // total orders (500K pairs)

    // Pre-generate orders so RNG cost is excluded from the measured hot path.
    RNG rng;
    std::vector<Order> orders;
    orders.reserve(N);

    uint64_t id = 1u;
    for (size_t i = 0; i < N; i += 2u) {
        // Passive sell: rests on the ask side.
        orders.push_back(Order{
            id++, Side::Sell, OrderType::Limit,
            rng.range(101u, 110u), 10u, id
        });
        // Aggressive buy: price 115 crosses all asks in [101,110].
        // Generates exactly 1 trade per pair — exercises the full fill path.
        orders.push_back(Order{
            id++, Side::Buy, OrderType::Limit,
            115u, 10u, id
        });
    }

    // -----------------------------------------------------------------------
    // Warm-up pass (not measured): brings instruction/data caches to hot state.
    // -----------------------------------------------------------------------
    {
        MatchingEngine warmup;
        size_t dummy = 0u;
        const size_t warmup_n = std::min(N, size_t{20'000u});
        for (size_t i = 0u; i < warmup_n; ++i) {
            dummy += warmup.process(orders[i]).size();
        }
        // Read dummy to prevent the compiler eliding the loop.
        if (dummy == 0u) std::cout << ""; // never prints; just prevents elision
    }

    // -----------------------------------------------------------------------
    // Measured pass.
    // -----------------------------------------------------------------------
    MatchingEngine engine;
    size_t total_trades = 0u;

    const auto t0 = Clock::now();

    for (const auto& order : orders) {
        total_trades += engine.process(order).size();
    }

    const auto t1 = Clock::now();

    // -----------------------------------------------------------------------
    // Results.
    // -----------------------------------------------------------------------
    const int64_t elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    const double elapsed_sec  = static_cast<double>(elapsed_ns) / 1e9;
    const double orders_per_s = static_cast<double>(N) / elapsed_sec;
    const double ns_per_order = static_cast<double>(elapsed_ns) / static_cast<double>(N);
    const double trades_per_s = static_cast<double>(total_trades) / elapsed_sec;

    std::cout
        << "============================================================\n"
        << "  MatchingEngine Throughput Benchmark\n"
        << "============================================================\n\n"
        << "  Orders processed : " << N << "\n"
        << "  Trades generated : " << total_trades << "\n"
        << "  Elapsed          : " << std::fixed << std::setprecision(2)
                                   << elapsed_sec * 1000.0 << " ms\n\n"
        << "  Throughput       : " << std::setprecision(2)
                                   << orders_per_s / 1e6 << " M orders/sec\n"
        << "  Trade rate       : " << std::setprecision(2)
                                   << trades_per_s / 1e6 << " M trades/sec\n"
        << "  Latency (mean)   : " << std::setprecision(1)
                                   << ns_per_order << " ns/order\n";

    return 0;
}
