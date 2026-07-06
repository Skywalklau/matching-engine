#include "itch_parser.hpp"

#include <cstring>

// ---------------------------------------------------------------------------
// Body byte offsets (relative to first byte AFTER the type byte)
// ---------------------------------------------------------------------------

static constexpr size_t OFF_TIMESTAMP  =  4u;
static constexpr size_t OFF_ORDER_REF  = 10u;
static constexpr size_t OFF_SIDE       = 18u;
static constexpr size_t OFF_SHARES_A   = 19u;
static constexpr size_t OFF_STOCK      = 23u;
static constexpr size_t OFF_PRICE_A    = 31u;

static constexpr size_t OFF_CANCEL_SH  = 18u;

static constexpr size_t OFF_EXEC_SH    = 18u;
static constexpr size_t OFF_MATCH_NUM  = 22u;

static constexpr size_t OFF_NEW_REF    = 18u;
static constexpr size_t OFF_SHARES_U   = 26u;
static constexpr size_t OFF_PRICE_U    = 30u;

static constexpr size_t BODY_ADD       = 35u;
static constexpr size_t BODY_ADD_MPID  = 35u;
static constexpr size_t BODY_DELETE    = 18u;
static constexpr size_t BODY_CANCEL    = 22u;
static constexpr size_t BODY_EXECUTED  = 30u;
static constexpr size_t BODY_REPLACE   = 34u;

// ---------------------------------------------------------------------------
// parseOne
// ---------------------------------------------------------------------------

std::optional<ITCHMessage> ITCHParser::parseOne(uint8_t        type,
                                                const uint8_t* body,
                                                size_t         body_len) {
    switch (type) {

    case 'A':
    case 'F':
    {
        const size_t min_len = (type == 'A') ? BODY_ADD : BODY_ADD_MPID;
        if (body_len < min_len) return std::nullopt;

        AddOrderMessage msg{};
        msg.timestamp_ns = readU48(body + OFF_TIMESTAMP);
        msg.order_ref    = readU64(body + OFF_ORDER_REF);
        msg.side         = (body[OFF_SIDE] == 'B') ? Side::Buy : Side::Sell;
        msg.shares       = readU32(body + OFF_SHARES_A);
        std::memcpy(msg.stock, body + OFF_STOCK, 8u);
        msg.stock[8]     = '\0';
        msg.price        = readU32(body + OFF_PRICE_A);
        return msg;
    }

    case 'D':
    {
        if (body_len < BODY_DELETE) return std::nullopt;
        DeleteOrderMessage msg{};
        msg.timestamp_ns = readU48(body + OFF_TIMESTAMP);
        msg.order_ref    = readU64(body + OFF_ORDER_REF);
        return msg;
    }

    case 'X':
    {
        if (body_len < BODY_CANCEL) return std::nullopt;
        CancelOrderMessage msg{};
        msg.timestamp_ns     = readU48(body + OFF_TIMESTAMP);
        msg.order_ref        = readU64(body + OFF_ORDER_REF);
        msg.cancelled_shares = readU32(body + OFF_CANCEL_SH);
        return msg;
    }

    case 'E':
    {
        if (body_len < BODY_EXECUTED) return std::nullopt;
        ExecutedOrderMessage msg{};
        msg.timestamp_ns    = readU48(body + OFF_TIMESTAMP);
        msg.order_ref       = readU64(body + OFF_ORDER_REF);
        msg.executed_shares = readU32(body + OFF_EXEC_SH);
        msg.match_number    = readU64(body + OFF_MATCH_NUM);
        return msg;
    }

    case 'U':
    {
        if (body_len < BODY_REPLACE) return std::nullopt;
        ReplaceOrderMessage msg{};
        msg.timestamp_ns       = readU48(body + OFF_TIMESTAMP);
        msg.original_order_ref = readU64(body + OFF_ORDER_REF);
        msg.new_order_ref      = readU64(body + OFF_NEW_REF);
        msg.shares             = readU32(body + OFF_SHARES_U);
        msg.price              = readU32(body + OFF_PRICE_U);
        return msg;
    }

    default:
        return std::nullopt;
    }
}

// ---------------------------------------------------------------------------
// parse — in-memory
// ---------------------------------------------------------------------------

std::vector<ITCHMessage> ITCHParser::parse(const uint8_t* data,
                                            size_t         len) const {
    std::vector<ITCHMessage> out;
    size_t pos = 0u;

    while (pos + 2u <= len) {
        const uint16_t msg_len = readU16(data + pos);
        pos += 2u;

        if (msg_len == 0u || pos + static_cast<size_t>(msg_len) > len) break;

        const uint8_t  type     = data[pos];
        const uint8_t* body     = data + pos + 1u;
        const size_t   body_len = static_cast<size_t>(msg_len) - 1u;

        auto result = parseOne(type, body, body_len);
        if (result.has_value()) out.push_back(std::move(*result));

        pos += static_cast<size_t>(msg_len);
    }

    return out;
}

// ---------------------------------------------------------------------------
// toOrder
//
// ITCH 5.0 prices are in units of $0.0001 (1/10000 of a dollar).
// Our engine uses prices in cents ($0.01). Dividing by 100 converts the
// unit and keeps prices within OrderBook::MAX_PRICE (65536) for stocks
// trading below $655.35 — which covers virtually all Nasdaq equities.
// ---------------------------------------------------------------------------

Order ITCHParser::toOrder(const AddOrderMessage& msg) noexcept {
    return Order{
        msg.order_ref,
        msg.side,
        OrderType::Limit,
        msg.price / 100u,   // $0.0001 → $0.01 (cents)
        msg.shares,
        msg.timestamp_ns
    };
}