#include "order_book.hpp"

#include <cstring>

OrderBook::OrderBook()
    : bid_levels_(std::make_unique<Limit*[]>(MAX_PRICE)),
      ask_levels_(std::make_unique<Limit*[]>(MAX_PRICE))
{
    std::memset(bid_bits_, 0, sizeof(bid_bits_));
    std::memset(ask_bits_, 0, sizeof(ask_bits_));
}

void OrderBook::addOrder(Order order) {
    const uint32_t price = order.price;
    const Side     side  = order.side;
    const uint64_t id    = order.id;

    // Skip orders outside our price array range.
    // For ITCH data in cents, this filters stocks above $655.35.
    if (price >= MAX_PRICE) return;

    // & to avoid copying the pointer, so we can modify the original slot in the array.
    Limit*& slot = (side == Side::Buy)
        ? bid_levels_[price]
        : ask_levels_[price];

    if (slot == nullptr) {
        slot = limit_pool_.construct(Limit{price});
        if (!slot) return; // pool exhausted — skip silently

        if (side == Side::Buy) setBit(bid_bits_, price);
        else                   setBit(ask_bits_, price);
    }

    OrderNode* node = order_pool_.construct(OrderNode{order});
    if (!node) {
        // Pool exhausted. Undo the level creation if it was just made empty.
        if (slot->empty()) {
            limit_pool_.destroy(slot);
            slot = nullptr;
            if (side == Side::Buy) clearBit(bid_bits_, price);
            else                   clearBit(ask_bits_, price);
        }
        return;
    }

    slot->pushBack(node);
    index_[id] = OrderLocation{price, side, node};
}

void OrderBook::cancelOrder(uint64_t id) {
    auto it = index_.find(id);
    if (it == index_.end()) return;

    const auto [price, side, node] = it->second;

    Limit*& slot = (side == Side::Buy)
        ? bid_levels_[price]
        : ask_levels_[price];

    if (slot != nullptr) {
        slot->remove(node);

        if (slot->empty()) {
            limit_pool_.destroy(slot);
            slot = nullptr;
            if (side == Side::Buy) clearBit(bid_bits_, price);
            else                   clearBit(ask_bits_, price);
        }
    }

    order_pool_.destroy(node);
    index_.erase(it);
}

std::optional<uint32_t> OrderBook::bestBid() const {
    for (size_t w = WORDS; w > 0u; --w) {
        const uint64_t word = bid_bits_[w - 1u];
        if (word != 0u) {
            const size_t   bit   = 63u - static_cast<size_t>(__builtin_clzll(word));
            const uint32_t price = static_cast<uint32_t>((w - 1u) * 64u + bit);
            return price;
        }
    }
    return std::nullopt;
}

std::optional<uint32_t> OrderBook::bestAsk() const {
    for (size_t w = 0u; w < WORDS; ++w) {
        const uint64_t word = ask_bits_[w];
        if (word != 0u) {
            const size_t   bit   = static_cast<size_t>(__builtin_ctzll(word));
            const uint32_t price = static_cast<uint32_t>(w * 64u + bit);
            return price;
        }
    }
    return std::nullopt;
}

Order* OrderBook::bestBidFront() {
    for (size_t w = WORDS; w > 0u; --w) {
        const uint64_t word = bid_bits_[w - 1u];
        if (word != 0u) {
            const size_t bit   = 63u - static_cast<size_t>(__builtin_clzll(word));
            const size_t price = (w - 1u) * 64u + bit;
            return &bid_levels_[price]->head->order;
        }
    }
    return nullptr;
}

Order* OrderBook::bestAskFront() {
    for (size_t w = 0u; w < WORDS; ++w) {
        const uint64_t word = ask_bits_[w];
        if (word != 0u) {
            const size_t bit   = static_cast<size_t>(__builtin_ctzll(word));
            const size_t price = w * 64u + bit;
            return &ask_levels_[price]->head->order;
        }
    }
    return nullptr;
}