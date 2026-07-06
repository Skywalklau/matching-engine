#pragma once

#include <cstdint>

// Emitted by the MatchingEngine for every execution.
// buy_order_id and sell_order_id trace back to the original Order::id values.
struct Trade {
    uint64_t buy_order_id;
    uint64_t sell_order_id;
    uint32_t price;    // price at which the trade executed
    uint32_t quantity; // quantity exchanged in this trade
};
