#pragma once

#include "order_book_types.hpp"
#include "pool_allocator.hpp"

#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>

// ---------------------------------------------------------------------------
// OrderBookV2: BST of Limit nodes, each with a doubly linked list.
//
// Kept as a reference implementation for benchmarking against V1 and V3.
//
// Key improvement over V1: cancel is O(1) via direct OrderNode* in the index.
// Remaining bottleneck: addOrder and cancelOrder still pay O(log n) for the
// std::map lookup. V3 eliminates this with a flat price array.
// ---------------------------------------------------------------------------

class OrderBookV2 {
public:
    static constexpr size_t MAX_ORDERS = 4096;
    static constexpr size_t MAX_LEVELS = 1024;

    void addOrder(Order order);
    void cancelOrder(uint64_t id);

    std::optional<uint32_t> bestBid() const;
    std::optional<uint32_t> bestAsk() const;

    Order* bestBidFront();
    Order* bestAskFront();

private:
    std::map<uint32_t, Limit*, std::greater<uint32_t>> bids_;
    std::map<uint32_t, Limit*>                          asks_;

    struct OrderLocation {
        uint32_t   price;
        Side       side;
        OrderNode* node;
    };
    std::unordered_map<uint64_t, OrderLocation> index_;

    PoolAllocator<OrderNode, MAX_ORDERS> order_pool_;
    PoolAllocator<Limit,     MAX_LEVELS> limit_pool_;
};
