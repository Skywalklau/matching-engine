#pragma once

#include <cstdint>

enum class Side : uint8_t {
    Buy,
    Sell
};

enum class OrderType : uint8_t {
    Limit,
    Market
};

struct Order {
    uint64_t  id;
    Side      side;
    OrderType type;
    uint32_t  price;     // price in ticks. Convention: 0 for market orders
    uint32_t  quantity;  // remaining unfilled quantity
    uint64_t  timestamp; // nanoseconds since epoch, used for time priority
};
