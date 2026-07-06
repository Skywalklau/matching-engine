#include "matching_engine.hpp"

#include <algorithm>

std::vector<Trade> MatchingEngine::process(Order order) {
    std::vector<Trade> trades;

    if (order.type == OrderType::Market) {
        // Market orders fill greedily at any available price. Never rest.
        fillAgainst(order, trades);

    } else {
        // Limit order: only fill if the price crosses the spread.
        bool crosses = false;

        if (order.side == Side::Buy) {
            auto ask = book_.bestAsk();
            crosses  = ask.has_value() && order.price >= ask.value();
        } else {
            auto bid = book_.bestBid();
            crosses  = bid.has_value() && order.price <= bid.value();
        }

        if (crosses) fillAgainst(order, trades);

        // If quantity remains, rest the remainder on the book.
        if (order.quantity > 0) book_.addOrder(order);
    }

    return trades;
}

void MatchingEngine::fillAgainst(Order& aggressor, std::vector<Trade>& trades) {
    // Walk the opposite side of the book in price-time priority:
    //   1. bestXFront() always returns the best price level's oldest order.
    //   2. We fill as much as possible, then advance to the next order.
    //   3. For limit orders, stop when the spread no longer crosses.

    while (aggressor.quantity > 0) {
        // Get the front order at the best opposite-side level.
        Order* resting = (aggressor.side == Side::Buy)
            ? book_.bestAskFront()
            : book_.bestBidFront();

        if (resting == nullptr) break; // no liquidity

        // For limit orders, stop if the spread no longer crosses.
        if (aggressor.type == OrderType::Limit) {
            if (aggressor.side == Side::Buy  && aggressor.price < resting->price) break;
            if (aggressor.side == Side::Sell && aggressor.price > resting->price) break;
        }

        // Copy the resting order's id and price before any mutation.
        // cancelOrder() destroys the OrderNode — resting becomes dangling after.
        const uint64_t resting_id  = resting->id;
        const uint32_t exec_price  = resting->price;
        const uint32_t filled      = std::min(aggressor.quantity, resting->quantity);

        Trade trade{};
        trade.price    = exec_price;
        trade.quantity = filled;

        if (aggressor.side == Side::Buy) {
            trade.buy_order_id  = aggressor.id;
            trade.sell_order_id = resting_id;
        } else {
            trade.buy_order_id  = resting_id;
            trade.sell_order_id = aggressor.id;
        }

        trades.push_back(trade);

        aggressor.quantity -= filled;
        resting->quantity  -= filled; // modify in place inside the OrderNode

        // If the resting order is fully filled, remove it from the book.
        // After this call, resting is dangling — do not access it again.
        if (resting->quantity == 0) {
            book_.cancelOrder(resting_id);
        }
    }
}
