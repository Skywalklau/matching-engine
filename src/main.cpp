#include "concurrency/feed_thread.hpp"
#include "concurrency/spsc_queue.hpp"
#include "itch/itch_parser.hpp"
#include "matching_engine.hpp"
#include "order.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

// ---------------------------------------------------------------------------
// SPSCQueue<Order, 65536> is ~2MB — too large for the stack.
// ---------------------------------------------------------------------------
static constexpr size_t QUEUE_CAPACITY = 65536u;
using Queue = SPSCQueue<Order, QUEUE_CAPACITY>;

using Clock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Result
// ---------------------------------------------------------------------------
struct Result {
    size_t  orders_processed;
    size_t  trades_generated;
    int64_t elapsed_ns;
};

static void printResult(const char* mode, const Result& r) {
    const double sec     = static_cast<double>(r.elapsed_ns) / 1e9;
    const double ops_sec = static_cast<double>(r.orders_processed) / sec;
    const double ns_each = static_cast<double>(r.elapsed_ns) /
                           static_cast<double>(r.orders_processed);

    std::cout
        << "============================================================\n"
        << "  Matching Engine — " << mode << "\n"
        << "============================================================\n\n"
        << "  Orders processed : " << r.orders_processed << "\n"
        << "  Trades generated : " << r.trades_generated << "\n"
        << "  Elapsed          : "
            << std::fixed << std::setprecision(2)
            << sec * 1000.0 << " ms\n\n"
        << "  Throughput       : "
            << std::setprecision(2) << ops_sec / 1e6 << " M orders/sec\n"
        << "  Latency (mean)   : "
            << std::setprecision(1) << ns_each << " ns/order\n"
        << "============================================================\n";
}

// ---------------------------------------------------------------------------
// ITCH 5.0 file mode — streaming (O(1) memory regardless of file size).
//
// Reads one message at a time directly from disk.
// No full file load into RAM — a 9 GB file uses only a 512-byte stack buffer.
//
// Single-threaded: file I/O is the bottleneck here, not the matching engine,
// so adding the FeedThread + queue would not improve throughput.
// ---------------------------------------------------------------------------
static Result runITCHMode(const char* path) {
    std::cout << "Streaming " << path << " ...\n";

    ITCHParser     parser;
    MatchingEngine engine;

    size_t n_add  = 0u, n_del = 0u, n_cancel = 0u,
           n_exec = 0u, n_rep = 0u, trades   = 0u;

    const auto t0 = Clock::now();

    parser.parseFile(path, [&](ITCHMessage msg) {
        if (std::holds_alternative<AddOrderMessage>(msg)) {
            trades += engine.process(
                ITCHParser::toOrder(std::get<AddOrderMessage>(msg))
            ).size();
            ++n_add;
        } else if (std::holds_alternative<DeleteOrderMessage>(msg))  { ++n_del;    }
        else if   (std::holds_alternative<CancelOrderMessage>(msg))  { ++n_cancel; }
        else if   (std::holds_alternative<ExecutedOrderMessage>(msg)) { ++n_exec;  }
        else if   (std::holds_alternative<ReplaceOrderMessage>(msg))  { ++n_rep;   }
    });

    const auto t1 = Clock::now();

    std::cout << "  Add orders   : " << n_add    << "\n"
              << "  Deletes      : " << n_del    << "\n"
              << "  Cancels      : " << n_cancel << "\n"
              << "  Executions   : " << n_exec   << "\n"
              << "  Replaces     : " << n_rep    << "\n\n";

    const int64_t ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    return Result{n_add, trades, ns};
}

// ---------------------------------------------------------------------------
// Synthetic mode — demonstrates the full concurrent pipeline:
//   FeedThread (producer) → SPSCQueue → MatchingEngine (consumer/main thread)
//
// Pattern: alternating passive sell + aggressive buy.
// Every pair generates exactly 1 trade. Pool never exceeds 1 resting order.
// ---------------------------------------------------------------------------
static std::vector<Order> makeSyntheticOrders(size_t n) {
    std::vector<Order> orders;
    orders.reserve(n);
    uint64_t id = 1u;
    for (size_t i = 0u; i < n; i += 2u) {
        const uint32_t ask = static_cast<uint32_t>(101u + (i / 2u) % 10u);
        orders.push_back(Order{id++, Side::Sell, OrderType::Limit,
                               ask, 10u, static_cast<uint64_t>(i)});
        if (i + 1u < n) {
            orders.push_back(Order{id++, Side::Buy, OrderType::Limit,
                                   115u, 10u, static_cast<uint64_t>(i + 1u)});
        }
    }
    return orders;
}

static Result runSyntheticMode(size_t n) {
    auto queue = std::make_unique<Queue>(); // heap: ~2 MB
    MatchingEngine engine;

    const auto orders = makeSyntheticOrders(n);
    const size_t total = orders.size();
    size_t idx = 0u;

    auto gen = [&](Order& o) -> bool {
        if (idx >= total) return false;
        o = orders[idx++];
        return true;
    };

    FeedThread<QUEUE_CAPACITY> feed(*queue, gen);

    const auto t0 = Clock::now();
    feed.start();

    Order  o{};
    size_t processed = 0u;
    size_t trades    = 0u;

    while (processed < total) {
        if (queue->pop(o)) {
            trades += engine.process(o).size();
            ++processed;
        }
    }

    feed.stop();
    const auto t1 = Clock::now();

    const int64_t ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    return Result{total, trades, ns};
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[]) {
    static constexpr size_t SYNTHETIC_N = 1'000'000u;

    try {
        if (argc >= 2) {
            const auto result = runITCHMode(argv[1]);
            printResult("ITCH 5.0 Replay", result);
        } else {
            std::cout
                << "Usage: ./matching_engine [path/to/itch/file.bin]\n"
                << "No file provided — running synthetic mode ("
                << SYNTHETIC_N << " orders).\n\n";
            const auto result = runSyntheticMode(SYNTHETIC_N);
            printResult("Synthetic Mode", result);
        }
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }

    return 0;
}