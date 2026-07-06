#include "order_book_v2.hpp"

#include <cassert>

void OrderBookV2::addOrder(Order order) {
    const uint32_t price = order.price;
    const Side     side  = order.side;
    const uint64_t id    = order.id;

    Limit* limit = nullptr;

    if (side == Side::Buy) {
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            limit        = limit_pool_.construct(Limit{price});
            bids_[price] = limit;
        } else {
            limit = it->second;
        }
    } else {
        auto it = asks_.find(price);
        if (it == asks_.end()) {
            limit        = limit_pool_.construct(Limit{price});
            asks_[price] = limit;
        } else {
            limit = it->second;
        }
    }

    OrderNode* node = order_pool_.construct(OrderNode{order});
    assert(node  != nullptr && "Order pool exhausted");
    assert(limit != nullptr && "Limit pool exhausted");

    limit->pushBack(node);
    index_[id] = OrderLocation{price, side, node};
}

void OrderBookV2::cancelOrder(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;

    const auto [price, side, node] = it->second;

    auto removeFromSide = [&](auto& levels) {
        auto lit = levels.find(price);
        if (lit == levels.end()) return;
        Limit* limit = lit->second;
        limit->remove(node);
        if (limit->empty()) {
            levels.erase(lit);
            limit_pool_.destroy(limit);
        }
    };

    if (side == Side::Buy) removeFromSide(bids_);
    else                   removeFromSide(asks_);

    order_pool_.destroy(node);
    index_.erase(it);
}

std::optional<uint32_t> OrderBookV2::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<uint32_t> OrderBookV2::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

Order* OrderBookV2::bestBidFront() {
    if (bids_.empty()) return nullptr;
    return &bids_.begin()->second->head->order;
}

Order* OrderBookV2::bestAskFront() {
    if (asks_.empty()) return nullptr;
    return &asks_.begin()->second->head->order;
}
