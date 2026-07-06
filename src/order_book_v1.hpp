#pragma once

#include "order.hpp"

#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <unordered_map>

// ---------------------------------------------------------------------------
// OrderBookV1: std::map<price, std::deque<Order>>
//
// Kept as a reference implementation for benchmarking against V2 and V3.
// cancel is O(k) linear scan — this is the bottleneck being fixed.
// ---------------------------------------------------------------------------

class OrderBookV1 {
public:
    void addOrder(Order order);
    void cancelOrder(uint64_t id);

    std::optional<uint32_t> bestBid() const;
    std::optional<uint32_t> bestAsk() const;

    std::deque<Order>* bestBidLevel();
    std::deque<Order>* bestAskLevel();

    void removeBidLevel(uint32_t price);
    void removeAskLevel(uint32_t price);

private:
    std::map<uint32_t, std::deque<Order>, std::greater<uint32_t>> bids_; // highest price first (best bid = highest)
    std::map<uint32_t, std::deque<Order>>                          asks_; // lowest price first (best ask = lowest)

    struct OrderLocation {
        uint32_t price;
        Side     side;
    };
    std::unordered_map<uint64_t, OrderLocation> index_;
};
