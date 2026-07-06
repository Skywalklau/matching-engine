#pragma once

#include "order.hpp"
#include "order_book.hpp"
#include "trade.hpp"

#include <vector>

class MatchingEngine {
public:
    std::vector<Trade> process(Order order);
    const OrderBook& book() const { return book_; }

private:
    OrderBook book_;
    void fillAgainst(Order& aggressor, std::vector<Trade>& trades);
};
