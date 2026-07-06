# High-Performance C++ Matching Engine

A limit order book and matching engine built for latency, implementing the canonical data structures used in production electronic trading systems. Written in C++17 with lock-free concurrency primitives, custom memory allocation, and three progressively-optimised order book implementations benchmarked against each other and against real Nasdaq ITCH 5.0 market data.

---

## Benchmark Results

All benchmarks run on AMD Ryzen 5 7500F (6-core, 12-thread, 2.10 GHz) with `-O3`.

### Order Book Latency — 599,000 operations per version

Window of 1,000 simultaneously resting orders across 21 price levels.
The p999 column is the most meaningful for HFT — it shows worst-case tail latency.

| Version | Structure | addOrder p50 | addOrder p999 | cancelOrder p50 | cancelOrder p999 |
|---|---|---|---|---|---|
| V1 | `std::map` + `std::deque` | 40 ns | 280 ns | 40 ns | 810 ns |
| V2 | `std::map` + doubly linked list | 40 ns | 80 ns | 40 ns | 80 ns |
| **V3** | **flat array + bitset** | **20 ns** | **50 ns** | **20 ns** | **50 ns** |

**Key improvements:**
- V1 → V2: cancelOrder tail latency 810 ns → 80 ns (**10x**) — O(k) deque scan replaced by O(1) pointer rewire
- V2 → V3: both operations 2x faster — O(log n) BST lookup replaced by O(1) array index

### Matching Engine Throughput

| Metric | Value |
|---|---|
| Orders processed | 1,000,000 |
| Trades generated | 500,000 |
| Throughput | **3.67 M orders/sec** |
| Mean latency | 272 ns/order |

### SPSC Queue — 1,000,000 messages

| Variant | Throughput |
|---|---|
| Mutex queue (baseline) | 19 M msgs/sec |
| Lock-free, `seq_cst` | 25 M msgs/sec |
| Lock-free, `acq/rel` | **189 M msgs/sec** |
| Lock-free, `acq/rel` + `alignas(64)` | 175 M msgs/sec |

Lock-free eliminates mutex contention: **~10x throughput gain**. Relaxing memory ordering from `seq_cst` to `acq/rel` replaces `LOCK XCHG` with plain `MOV` on x86: **~8x additional gain**.

### ITCH 5.0 Full-Day Replay — July 30, 2019

| Metric | Value |
|---|---|
| Add orders processed | 125,460,750 |
| Trades generated | 52,529,355 |
| Deletes parsed | 119,999,061 |
| Cancels parsed | 2,358,032 |
| Executions parsed | 7,582,422 |
| Replaces parsed | 21,253,951 |

Streamed through the 9 GB binary file with O(1) memory usage — no full file load.

---

## Architecture

### Layer 1 — Core Engine (`src/`)

**`order.hpp`** — Plain data. `uint32_t price` in integer ticks (never float — rounding errors corrupt price-time priority). `uint64_t timestamp` for nanosecond time priority. `uint32_t quantity` tracks remaining unfilled size.

**`trade.hpp`** — Separated from `order.hpp` by design. An `Order` is input; a `Trade` is output. Mixing them couples two independent concerns.

**`order_book_types.hpp`** — `OrderNode` (doubly linked list node) and `Limit` (price level) shared between V2 and V3 to avoid duplicate definitions when both are included in benchmarks.

#### OrderBook V1 — `std::map<price, std::deque<Order>>`

Correctness-first reference implementation. `std::map` with `std::greater` gives O(1) best bid via `begin()`. Cancel requires an O(k) linear scan of the deque to find the order by ID — this is the bottleneck being fixed.

#### OrderBook V2 — BST of Limit nodes, doubly linked list per level

The cancel index stores a direct `OrderNode*` pointer. Removal from a doubly linked list given a pointer is three pointer rewires — O(1) with no scan. `std::map` remains as the BST (O(log n) for add), but the O(k) tail is gone. Pool allocator eliminates `new`/`delete` on the hot path.

#### OrderBook V3 — Flat array + bitset + `__builtin_ctzll`

Eliminates the BST entirely. `bid_levels_[price]` and `ask_levels_[price]` are direct O(1) array accesses — one multiply-add and one memory read. Finding the best bid/ask uses a bitset: one bit per price level, set when that level is active.

`__builtin_ctzll` (Count Trailing Zeros) finds the lowest set bit — the best ask — in a single `BSF` CPU instruction. `63 - __builtin_clzll` finds the highest set bit — the best bid. In a realistic book with activity clustered near the spread, this resolves in the first or second 64-bit word.

Price arrays are heap-allocated (`unique_ptr<Limit*[]>`) since 65,536 pointers × 8 bytes = 512 KB per side would overflow the stack.

**`matching_engine.hpp/.cpp`** — Price-time priority matching. `process(Order)` checks if the incoming order crosses the spread, calls `fillAgainst()` to walk the opposite side greedily, then rests any remainder. Market orders bypass the spread check. The passive (resting) order always sets the execution price.

**`pool_allocator.hpp`** — Fixed-size memory pool. Heap-allocated storage (`unique_ptr`) supports large capacities (512K orders) without stack overflow. Free list implemented as a LIFO stack: `allocate()` pops in O(1), `deallocate()` pushes in O(1). Zero heap involvement after construction.

### Layer 3 — Concurrency (`src/concurrency/`)

**`spsc_queue.hpp`** — Lock-free single-producer single-consumer ring buffer. Power-of-two capacity for bitmask indexing (avoids modulo). `alignas(64)` on `write_` and `read_` places them on separate cache lines to prevent false sharing. `memory_order_release` on stores, `memory_order_acquire` on cross-thread loads — minimum correct ordering, no unnecessary fences.

**`feed_thread.hpp`** — Producer thread wrapper. Accepts a generator function `bool(Order&)`, pushes into the SPSC queue, spins on full. Stop signal is checked in the inner spin loop so `stop()` never deadlocks on a full queue.

### Layer 4 — Market Data (`src/itch/`)

**`itch_parser.hpp/.cpp`** — Nasdaq ITCH 5.0 binary protocol parser. All multi-byte fields are big-endian; explicit byte-shift readers handle conversion portably without `ntohs`/`ntohl`. Supports Add Order, Add Order with MPID, Delete Order, Order Cancel, Order Executed, and Order Replace. Unknown message types are silently skipped. `parseFile()` streams one message at a time using a 512-byte stack buffer — O(1) memory regardless of file size.

ITCH prices are in units of $0.0001. `toOrder()` converts to cents ($0.01) to keep prices within the order book's 65,536-tick array range, covering stocks up to $655.35.

---

## Data Structure Decision Summary

| Decision | Reason |
|---|---|
| Integer tick prices | Float equality is undefined. Integer ticks are exact. Every real exchange uses this. |
| `std::map` with `std::greater` for bids | `begin()` is always the best bid in O(1) — no search required. |
| Doubly linked list per price level | O(1) cancel via direct pointer vs O(k) deque scan. The key V1→V2 improvement. |
| Flat array indexed by price | O(1) level lookup vs O(log n) BST traversal. The key V2→V3 improvement. |
| Bitset for active levels | Single `BSF` instruction finds best bid/ask. Hardware-accelerated. |
| Pool allocator | Eliminates heap allocation on the hot path. Removes the primary source of latency variance. |
| SPSC queue between threads | Zero-contention data transfer. Producer and consumer never touch the same cache line (when on separate physical cores). |
| `acq/rel` not `seq_cst` | `seq_cst` store = `LOCK XCHG` (~100 cycles). `acq/rel` store = `MOV` (~1 cycle) on x86. |

---

## Build

**Dependencies:** GCC/Clang (C++17), CMake ≥ 3.20, Google Test.

```bash
# Ubuntu / WSL2
sudo apt install g++ cmake libgtest-dev

# Build
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

---

## Run

```bash
# Run all 55 tests
./tests

# Benchmarks
./bench_order_book      # p50/p99/p999 latency — V1 vs V2 vs V3
./bench_matching        # end-to-end throughput
./bench_spsc            # queue throughput, four variants

# Matching engine — synthetic mode (no data file needed)
./matching_engine

# Matching engine — ITCH 5.0 replay
# Download: https://emi.nasdaq.com/ITCH/Nasdaq%20ITCH/
./matching_engine ../data/sample.bin
```

---

## Flamegraph

```bash
# Record with perf (Linux)
sudo perf record -g ./bench_order_book
sudo perf script | stackcollapse-perf.pl | flamegraph.pl > flamegraph.svg

# Or with Valgrind (WSL2)
valgrind --tool=callgrind ./bench_order_book
kcachegrind callgrind.out.*
```

---

## Project Structure

```
matching-engine/
├── src/
│   ├── order.hpp                   Order struct — id, side, type, price, qty, timestamp
│   ├── trade.hpp                   Trade struct — emitted on every execution
│   ├── order_book_types.hpp        OrderNode + Limit — shared between V2 and V3
│   ├── order_book_v1.hpp/.cpp      V1: std::map + std::deque
│   ├── order_book_v2.hpp/.cpp      V2: std::map + doubly linked list
│   ├── order_book.hpp/.cpp         V3: flat array + bitset  (active version)
│   ├── matching_engine.hpp/.cpp    Price-time priority matching logic
│   ├── pool_allocator.hpp          Fixed-size memory pool, heap-allocated storage
│   ├── main.cpp                    Pipeline: FeedThread → SPSCQueue → MatchingEngine
│   └── concurrency/
│       ├── spsc_queue.hpp          Lock-free ring buffer
│       └── feed_thread.hpp         Producer thread wrapper
│   └── itch/
│       ├── itch_parser.hpp         ITCH 5.0 message structs + streaming parser
│       └── itch_parser.cpp
├── tests/
│   ├── test_order_book.cpp         18 tests: insert, cancel, best price, time priority
│   ├── test_matching.cpp           10 tests: fills, partial fills, market orders, sweeps
│   ├── test_spsc.cpp               10 tests: single-threaded + 1M-message concurrent test
│   └── test_itch_parser.cpp        17 tests: hand-crafted byte arrays from ITCH 5.0 spec
├── benchmarks/
│   ├── bench_order_book.cpp        p50/p99/p999 latency — all three versions
│   ├── bench_matching.cpp          Orders/sec throughput
│   └── bench_spsc.cpp              Queue throughput — four variants
├── data/
│   └── .gitkeep                    Download ITCH data here (not committed)
└── CMakeLists.txt
```