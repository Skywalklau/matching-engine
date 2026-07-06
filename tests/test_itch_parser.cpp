#include "itch/itch_parser.hpp"
#include "order.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// Helper: build a framed ITCH message (2-byte length prefix + message bytes).
//
// ITCH frame layout:
//   [0-1]  message length (big-endian, does NOT include these 2 bytes)
//   [2]    message type
//   [3+]   fields
//
// All multi-byte fields within the message are big-endian.
// ---------------------------------------------------------------------------

// Build a hand-crafted Add Order ('A') frame.
// order_ref=66, timestamp=1ns, side=Buy, shares=10, stock="AAPL    ", price=100000
static const uint8_t kAddOrderBuy[] = {
    0x00, 0x24,              // length = 36
    'A',                     // type
    0x00, 0x01,              // stock locate   = 1  [body 0-1]
    0x00, 0x02,              // tracking num   = 2  [body 2-3]
    0x00, 0x00, 0x00, 0x00, 0x00, 0x01,  // timestamp = 1 ns  [body 4-9]
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42,  // order_ref = 66  [body 10-17]
    'B',                     // side = Buy  [body 18]
    0x00, 0x00, 0x00, 0x0A,  // shares = 10  [body 19-22]
    'A', 'A', 'P', 'L', ' ', ' ', ' ', ' ',  // stock  [body 23-30]
    0x00, 0x01, 0x86, 0xA0   // price = 100000  [body 31-34]
};

// Add Order Sell side, different order_ref.
// order_ref=67, timestamp=2ns, side=Sell, shares=20, stock="MSFT    ", price=200000
static const uint8_t kAddOrderSell[] = {
    0x00, 0x24,
    'A',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x02,              // timestamp = 2
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43,  // order_ref = 67
    'S',                                              // side = Sell
    0x00, 0x00, 0x00, 0x14,                           // shares = 20
    'M', 'S', 'F', 'T', ' ', ' ', ' ', ' ',
    0x00, 0x03, 0x0D, 0x40   // price = 200000
};

// Add Order with MPID ('F'): length=40, same fields + 4-byte MPID at body[35-38].
static const uint8_t kAddOrderMPID[] = {
    0x00, 0x28,              // length = 40
    'F',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x03,              // timestamp = 3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64,  // order_ref = 100
    'B',
    0x00, 0x00, 0x00, 0x05,                           // shares = 5
    'G', 'O', 'O', 'G', 'L', ' ', ' ', ' ',
    0x00, 0x0F, 0x42, 0x40,  // price = 1000000
    'I', 'M', 'M', 'S'       // MPID (should be ignored)
};

// Delete Order ('D'): length=19, body=18 bytes.
static const uint8_t kDeleteOrder[] = {
    0x00, 0x13,              // length = 19
    'D',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x04,              // timestamp = 4
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42   // order_ref = 66
};

// Order Cancel ('X'): length=23, body=22 bytes.
static const uint8_t kCancelOrder[] = {
    0x00, 0x17,              // length = 23
    'X',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x05,              // timestamp = 5
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x43,  // order_ref = 67
    0x00, 0x00, 0x00, 0x08   // cancelled_shares = 8
};

// Order Executed ('E'): length=31, body=30 bytes.
static const uint8_t kExecutedOrder[] = {
    0x00, 0x1F,              // length = 31
    'E',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x06,              // timestamp = 6
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42,  // order_ref = 66
    0x00, 0x00, 0x00, 0x03,                           // executed_shares = 3
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01   // match_number = 1
};

// Order Replace ('U'): length=35, body=34 bytes.
static const uint8_t kReplaceOrder[] = {
    0x00, 0x23,              // length = 35
    'U',
    0x00, 0x01,
    0x00, 0x02,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x07,              // timestamp = 7
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x42,  // original_order_ref = 66
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64,  // new_order_ref = 100
    0x00, 0x00, 0x00, 0x0F,                           // shares = 15
    0x00, 0x02, 0x1E, 0xA4   // price = 138916? No: 0x00021EA4 = 138916?
    // 0x00021EA4 = 2*65536 + 30*256 + 164 = 131072 + 7680 + 164 = 138916. Fine.
};

// Unknown message type — should be silently skipped.
static const uint8_t kUnknownMsg[] = {
    0x00, 0x05,              // length = 5
    'Z',                     // unknown type
    0x00, 0x00, 0x00, 0x00  // 4 bytes of filler
};

// Truncated message — length says 36 but only 10 bytes follow.
static const uint8_t kTruncated[] = {
    0x00, 0x24,              // length = 36 (but only 10 real bytes follow)
    'A',
    0x00, 0x01, 0x00, 0x02,
    0x00, 0x00, 0x00, 0x00  // only 10 bytes after length prefix
};

// ---------------------------------------------------------------------------
// Tests: Add Order ('A')
// ---------------------------------------------------------------------------

TEST(ITCHParser, AddOrderBuyAllFields) {
    ITCHParser p;
    auto msgs = p.parse(kAddOrderBuy, sizeof(kAddOrderBuy));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<AddOrderMessage>(msgs[0]));

    const auto& m = std::get<AddOrderMessage>(msgs[0]);
    EXPECT_EQ(m.order_ref,    66u);
    EXPECT_EQ(m.timestamp_ns,  1u);
    EXPECT_EQ(m.side,         Side::Buy);
    EXPECT_EQ(m.shares,       10u);
    EXPECT_EQ(m.price,        100000u);
    EXPECT_EQ(std::string(m.stock), "AAPL    ");
}

TEST(ITCHParser, AddOrderSellSide) {
    ITCHParser p;
    auto msgs = p.parse(kAddOrderSell, sizeof(kAddOrderSell));

    ASSERT_EQ(msgs.size(), 1u);
    const auto& m = std::get<AddOrderMessage>(msgs[0]);
    EXPECT_EQ(m.side,    Side::Sell);
    EXPECT_EQ(m.shares,  20u);
    EXPECT_EQ(m.price,   200000u);
    EXPECT_EQ(m.order_ref, 67u);
    EXPECT_EQ(m.timestamp_ns, 2u);
}

TEST(ITCHParser, AddOrderMPIDSameFieldsAsAddOrder) {
    ITCHParser p;
    auto msgs = p.parse(kAddOrderMPID, sizeof(kAddOrderMPID));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<AddOrderMessage>(msgs[0]));

    const auto& m = std::get<AddOrderMessage>(msgs[0]);
    EXPECT_EQ(m.order_ref,    100u);
    EXPECT_EQ(m.timestamp_ns,   3u);
    EXPECT_EQ(m.side,          Side::Buy);
    EXPECT_EQ(m.shares,          5u);
    EXPECT_EQ(m.price,       1000000u);
    EXPECT_EQ(std::string(m.stock), "GOOGL   ");
}

// ---------------------------------------------------------------------------
// Tests: Delete Order ('D')
// ---------------------------------------------------------------------------

TEST(ITCHParser, DeleteOrder) {
    ITCHParser p;
    auto msgs = p.parse(kDeleteOrder, sizeof(kDeleteOrder));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<DeleteOrderMessage>(msgs[0]));

    const auto& m = std::get<DeleteOrderMessage>(msgs[0]);
    EXPECT_EQ(m.order_ref,    66u);
    EXPECT_EQ(m.timestamp_ns,  4u);
}

// ---------------------------------------------------------------------------
// Tests: Order Cancel ('X')
// ---------------------------------------------------------------------------

TEST(ITCHParser, CancelOrder) {
    ITCHParser p;
    auto msgs = p.parse(kCancelOrder, sizeof(kCancelOrder));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<CancelOrderMessage>(msgs[0]));

    const auto& m = std::get<CancelOrderMessage>(msgs[0]);
    EXPECT_EQ(m.order_ref,         67u);
    EXPECT_EQ(m.timestamp_ns,       5u);
    EXPECT_EQ(m.cancelled_shares,   8u);
}

// ---------------------------------------------------------------------------
// Tests: Order Executed ('E')
// ---------------------------------------------------------------------------

TEST(ITCHParser, ExecutedOrder) {
    ITCHParser p;
    auto msgs = p.parse(kExecutedOrder, sizeof(kExecutedOrder));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ExecutedOrderMessage>(msgs[0]));

    const auto& m = std::get<ExecutedOrderMessage>(msgs[0]);
    EXPECT_EQ(m.order_ref,          66u);
    EXPECT_EQ(m.timestamp_ns,        6u);
    EXPECT_EQ(m.executed_shares,     3u);
    EXPECT_EQ(m.match_number,        1u);
}

// ---------------------------------------------------------------------------
// Tests: Order Replace ('U')
// ---------------------------------------------------------------------------

TEST(ITCHParser, ReplaceOrder) {
    ITCHParser p;
    auto msgs = p.parse(kReplaceOrder, sizeof(kReplaceOrder));

    ASSERT_EQ(msgs.size(), 1u);
    ASSERT_TRUE(std::holds_alternative<ReplaceOrderMessage>(msgs[0]));

    const auto& m = std::get<ReplaceOrderMessage>(msgs[0]);
    EXPECT_EQ(m.original_order_ref,  66u);
    EXPECT_EQ(m.new_order_ref,      100u);
    EXPECT_EQ(m.timestamp_ns,         7u);
    EXPECT_EQ(m.shares,              15u);
    EXPECT_EQ(m.price,           138916u);
}

// ---------------------------------------------------------------------------
// Tests: Edge cases
// ---------------------------------------------------------------------------

TEST(ITCHParser, UnknownMessageTypeSilentlySkipped) {
    ITCHParser p;
    auto msgs = p.parse(kUnknownMsg, sizeof(kUnknownMsg));
    EXPECT_TRUE(msgs.empty());
}

TEST(ITCHParser, TruncatedMessageHandledGracefully) {
    ITCHParser p;
    // kTruncated claims length=36 but only 10 bytes follow.
    // parse() should stop without crashing and return nothing.
    EXPECT_NO_THROW({
        auto msgs = p.parse(kTruncated, sizeof(kTruncated));
        EXPECT_TRUE(msgs.empty());
    });
}

TEST(ITCHParser, EmptyBufferReturnsEmptyVector) {
    ITCHParser p;
    auto msgs = p.parse(nullptr, 0u);
    EXPECT_TRUE(msgs.empty());
}

TEST(ITCHParser, TwoMessagesInSequence) {
    // Concatenate Add Order + Delete Order into one buffer.
    uint8_t buf[sizeof(kAddOrderBuy) + sizeof(kDeleteOrder)];
    std::memcpy(buf,                       kAddOrderBuy,  sizeof(kAddOrderBuy));
    std::memcpy(buf + sizeof(kAddOrderBuy), kDeleteOrder, sizeof(kDeleteOrder));

    ITCHParser p;
    auto msgs = p.parse(buf, sizeof(buf));

    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<AddOrderMessage>(msgs[0]));
    EXPECT_TRUE(std::holds_alternative<DeleteOrderMessage>(msgs[1]));
}

TEST(ITCHParser, UnknownInterleavedWithKnownMessages) {
    // Unknown message between two known ones — known ones must both appear.
    uint8_t buf[sizeof(kAddOrderBuy) + sizeof(kUnknownMsg) + sizeof(kDeleteOrder)];
    size_t off = 0u;
    std::memcpy(buf + off, kAddOrderBuy,  sizeof(kAddOrderBuy));  off += sizeof(kAddOrderBuy);
    std::memcpy(buf + off, kUnknownMsg,   sizeof(kUnknownMsg));   off += sizeof(kUnknownMsg);
    std::memcpy(buf + off, kDeleteOrder,  sizeof(kDeleteOrder));

    ITCHParser p;
    auto msgs = p.parse(buf, sizeof(buf));

    ASSERT_EQ(msgs.size(), 2u);
    EXPECT_TRUE(std::holds_alternative<AddOrderMessage>(msgs[0]));
    EXPECT_TRUE(std::holds_alternative<DeleteOrderMessage>(msgs[1]));
}

TEST(ITCHParser, OnlyOneByteSingleByteBuffer) {
    // 1-byte buffer — cannot even hold the 2-byte length prefix. Must not crash.
    const uint8_t buf[] = {0x00};
    ITCHParser p;
    EXPECT_NO_THROW({
        auto msgs = p.parse(buf, 1u);
        EXPECT_TRUE(msgs.empty());
    });
}

// ---------------------------------------------------------------------------
// Tests: toOrder conversion
// ---------------------------------------------------------------------------

TEST(ITCHParser, ToOrderConversion) {
    ITCHParser p;
    auto msgs = p.parse(kAddOrderBuy, sizeof(kAddOrderBuy));
    ASSERT_EQ(msgs.size(), 1u);

    const auto& add = std::get<AddOrderMessage>(msgs[0]);
    const Order  o  = ITCHParser::toOrder(add);

    EXPECT_EQ(o.id,        add.order_ref);
    EXPECT_EQ(o.side,      Side::Buy);
    EXPECT_EQ(o.type,      OrderType::Limit);
    EXPECT_EQ(o.price, add.price / 100u); // toOrder converts from $0.0001 to cents
    EXPECT_EQ(o.quantity,  add.shares);
    EXPECT_EQ(o.timestamp, add.timestamp_ns);
}

TEST(ITCHParser, ToOrderSellConversion) {
    ITCHParser p;
    auto msgs = p.parse(kAddOrderSell, sizeof(kAddOrderSell));
    ASSERT_EQ(msgs.size(), 1u);

    const Order o = ITCHParser::toOrder(std::get<AddOrderMessage>(msgs[0]));
    EXPECT_EQ(o.side,  Side::Sell);
    EXPECT_EQ(o.price, 2000u); // 200000 / 100 = 2000 cents = $20.00
}

// ---------------------------------------------------------------------------
// Tests: Big-endian field correctness
// Verify multi-byte fields are decoded correctly across byte boundaries.
// ---------------------------------------------------------------------------

TEST(ITCHParser, TimestampDecodedCorrectly) {
    // Build a message with a known large timestamp: 0x0001020304050607 truncated
    // to 6 bytes: 0x010203040506 = 1108152157446 ns
    uint8_t buf[sizeof(kAddOrderBuy)];
    std::memcpy(buf, kAddOrderBuy, sizeof(kAddOrderBuy));
    // Timestamp is at buf[2+1+2+2] = buf[7..12]
    buf[7]  = 0x01; buf[8]  = 0x02; buf[9]  = 0x03;
    buf[10] = 0x04; buf[11] = 0x05; buf[12] = 0x06;

    ITCHParser p;
    auto msgs = p.parse(buf, sizeof(buf));
    ASSERT_EQ(msgs.size(), 1u);
    const auto& m = std::get<AddOrderMessage>(msgs[0]);
    EXPECT_EQ(m.timestamp_ns, 0x010203040506ULL);
}

TEST(ITCHParser, PriceDecodedCorrectly) {
    // Build a message with price = 0x12345678
    uint8_t buf[sizeof(kAddOrderBuy)];
    std::memcpy(buf, kAddOrderBuy, sizeof(kAddOrderBuy));
    // Price is at buf[2+35-4..2+35-1] = buf[35..38]
    // buf[2] = 'A', body starts at buf[3]
    // body[31..34] = buf[3+31..3+34] = buf[34..37]
    buf[34] = 0x12; buf[35] = 0x34; buf[36] = 0x56; buf[37] = 0x78;

    ITCHParser p;
    auto msgs = p.parse(buf, sizeof(buf));
    ASSERT_EQ(msgs.size(), 1u);
    EXPECT_EQ(std::get<AddOrderMessage>(msgs[0]).price, 0x12345678u);
}