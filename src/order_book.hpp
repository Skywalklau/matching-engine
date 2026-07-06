#pragma once

#include "order_book_types.hpp"
#include "pool_allocator.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

// ---------------------------------------------------------------------------
// OrderBook — Version 3: flat price array + bitset + __builtin_ctzll.
//
// Complexity vs V2:
//   addOrder:    O(log n) → O(1)
//   cancelOrder: O(log n) → O(1)
//   bestBid/Ask:           O(P/64) ≈ O(1) for realistic price distributions
//
// Price range: 0 to MAX_PRICE-1 ticks.
// For ITCH data converted to cent resolution ($0.01/tick): $0 to $655.35.
// Orders outside this range are silently skipped in addOrder().
//
// Pool sizes are generous to handle full Nasdaq ITCH replays
// (~100K-500K simultaneously resting orders across all listed stocks).
// Pool storage is heap-allocated so large N does not overflow the stack.
// If pools are exhausted, addOrder() returns silently rather than crashing.
// ---------------------------------------------------------------------------

class OrderBook {
public:
    static constexpr size_t MAX_PRICE  = 65536;
    static constexpr size_t WORDS      = MAX_PRICE / 64;

    // Pool sizes for full ITCH replay.
    // 524288 OrderNodes × 48 bytes = 24 MB (heap).
    //  65536 Limits     × 32 bytes =  2 MB (heap).
    static constexpr size_t MAX_ORDERS = 524288;
    static constexpr size_t MAX_LEVELS =  65536;

    OrderBook();

    OrderBook(const OrderBook&)            = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&)                 = delete;
    OrderBook& operator=(OrderBook&&)      = delete;

    void addOrder(Order order);
    void cancelOrder(uint64_t id);

    std::optional<uint32_t> bestBid() const;
    std::optional<uint32_t> bestAsk() const;

    Order* bestBidFront();
    Order* bestAskFront();

private:
    std::unique_ptr<Limit*[]> bid_levels_;
    std::unique_ptr<Limit*[]> ask_levels_;

    uint64_t bid_bits_[WORDS];
    uint64_t ask_bits_[WORDS];

    struct OrderLocation {
        uint32_t   price;
        Side       side;
        OrderNode* node;
    };
    std::unordered_map<uint64_t, OrderLocation> index_;

    PoolAllocator<OrderNode, MAX_ORDERS> order_pool_;
    PoolAllocator<Limit,     MAX_LEVELS> limit_pool_;

    static void setBit(uint64_t* bits, uint32_t price) noexcept {
        bits[price / 64] |= uint64_t{1} << (price % 64);
    }

    static void clearBit(uint64_t* bits, uint32_t price) noexcept {
        bits[price / 64] &= ~(uint64_t{1} << (price % 64));
    }
};