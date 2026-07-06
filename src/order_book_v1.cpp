#include "order_book_v1.hpp"

void OrderBookV1::addOrder(Order order) {
    const uint32_t price = order.price;
    const Side     side  = order.side;
    const uint64_t id    = order.id;

    if (side == Side::Buy) bids_[price].push_back(order);
    else                   asks_[price].push_back(order);

    index_[id] = OrderLocation{price, side};
}

void OrderBookV1::cancelOrder(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;

    const auto [price, side] = it->second;

    auto eraseFromLevel = [&](auto& levels) {
        auto level = levels.find(price);
        if (level == levels.end()) return;
        auto& dq = level->second;
        for (auto oi = dq.begin(); oi != dq.end(); ++oi) {
            if (oi->id == id) { dq.erase(oi); break; }
        }
        if (dq.empty()) levels.erase(level);
    };

    if (side == Side::Buy) eraseFromLevel(bids_);
    else                   eraseFromLevel(asks_);

    index_.erase(it);
}

std::optional<uint32_t> OrderBookV1::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<uint32_t> OrderBookV1::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}

std::deque<Order>* OrderBookV1::bestBidLevel() {
    if (bids_.empty()) return nullptr;
    return &bids_.begin()->second;
}

std::deque<Order>* OrderBookV1::bestAskLevel() {
    if (asks_.empty()) return nullptr;
    return &asks_.begin()->second;
}

void OrderBookV1::removeBidLevel(uint32_t price) { bids_.erase(price); }
void OrderBookV1::removeAskLevel(uint32_t price) { asks_.erase(price); }
