#include "matching_engine.hpp"
#include <gtest/gtest.h>

static Order makeLimit(uint64_t id, Side side, uint32_t price, uint32_t qty, uint64_t ts = 0) {
    return Order{id, side, OrderType::Limit, price, qty, ts};
}
static Order makeMarket(uint64_t id, Side side, uint32_t qty) {
    return Order{id, side, OrderType::Market, 0, qty, 0};
}

TEST(Matching, LimitRestsWhenNoLiquidity) {
    MatchingEngine me;
    auto t = me.process(makeLimit(1,Side::Buy,100,10));
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(me.book().bestBid().value(), 100u);
}

TEST(Matching, LimitBuyBelowAskDoesNotCross) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,105,10));
    auto t = me.process(makeLimit(2,Side::Buy,100,10));
    EXPECT_TRUE(t.empty());
    EXPECT_EQ(me.book().bestBid().value(), 100u);
    EXPECT_EQ(me.book().bestAsk().value(), 105u);
}

TEST(Matching, ExactFill) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,10));
    auto t = me.process(makeLimit(2,Side::Buy,100,10));
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0].price,         100u);
    EXPECT_EQ(t[0].quantity,       10u);
    EXPECT_EQ(t[0].buy_order_id,    2u);
    EXPECT_EQ(t[0].sell_order_id,   1u);
    EXPECT_FALSE(me.book().bestBid().has_value());
    EXPECT_FALSE(me.book().bestAsk().has_value());
}

TEST(Matching, PartialFillRemainderRests) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,6));
    auto t = me.process(makeLimit(2,Side::Buy,100,10));
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0].quantity, 6u);
    EXPECT_EQ(me.book().bestBid().value(), 100u);
    EXPECT_FALSE(me.book().bestAsk().has_value());
}

TEST(Matching, PartialFillRestingLarger) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,20));
    auto t = me.process(makeLimit(2,Side::Buy,100,5));
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0].quantity, 5u);
    EXPECT_EQ(me.book().bestAsk().value(), 100u);
    EXPECT_FALSE(me.book().bestBid().has_value());
}

TEST(Matching, SweepMultipleAskLevels) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,5));
    me.process(makeLimit(2,Side::Sell,101,5));
    me.process(makeLimit(3,Side::Sell,102,5));
    auto t = me.process(makeLimit(4,Side::Buy,105,15));
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].price, 100u);
    EXPECT_EQ(t[1].price, 101u);
    EXPECT_EQ(t[2].price, 102u);
    EXPECT_FALSE(me.book().bestAsk().has_value());
}

TEST(Matching, TimePriorityFillsEarliestFirst) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,5,1));
    me.process(makeLimit(2,Side::Sell,100,5,2));
    auto t = me.process(makeLimit(3,Side::Buy,100,5));
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0].sell_order_id, 1u);
}

TEST(Matching, MarketOrderFills) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,10));
    auto t = me.process(makeMarket(2,Side::Buy,10));
    ASSERT_EQ(t.size(), 1u);
    EXPECT_EQ(t[0].price,    100u);
    EXPECT_EQ(t[0].quantity,  10u);
}

TEST(Matching, MarketOrderEmptyBookNoCrash) {
    MatchingEngine me;
    auto t = me.process(makeMarket(1,Side::Buy,10));
    EXPECT_TRUE(t.empty());
}

TEST(Matching, MarketOrderSweepsMultipleLevels) {
    MatchingEngine me;
    me.process(makeLimit(1,Side::Sell,100,3));
    me.process(makeLimit(2,Side::Sell,101,3));
    me.process(makeLimit(3,Side::Sell,102,3));
    auto t = me.process(makeMarket(4,Side::Buy,9));
    ASSERT_EQ(t.size(), 3u);
    EXPECT_EQ(t[0].price, 100u);
    EXPECT_EQ(t[1].price, 101u);
    EXPECT_EQ(t[2].price, 102u);
}
