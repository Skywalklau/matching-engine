#include "order_book.hpp"

#include <gtest/gtest.h>

static Order makeLimit(uint64_t id, Side side, uint32_t price, uint32_t qty,
                       uint64_t ts = 0) {
    return Order{id, side, OrderType::Limit, price, qty, ts};
}

// ---------------------------------------------------------------------------
// bestBid / bestAsk — empty book
// ---------------------------------------------------------------------------
TEST(OrderBook, BestBidEmptyBook) {
    OrderBook ob;
    EXPECT_FALSE(ob.bestBid().has_value());
}

TEST(OrderBook, BestAskEmptyBook) {
    OrderBook ob;
    EXPECT_FALSE(ob.bestAsk().has_value());
}

TEST(OrderBook, BestBidFrontEmptyBook) {
    OrderBook ob;
    EXPECT_EQ(ob.bestBidFront(), nullptr);
}

TEST(OrderBook, BestAskFrontEmptyBook) {
    OrderBook ob;
    EXPECT_EQ(ob.bestAskFront(), nullptr);
}

// ---------------------------------------------------------------------------
// Insert and query
// ---------------------------------------------------------------------------
TEST(OrderBook, BestBidSingleInsert) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10));
    ASSERT_TRUE(ob.bestBid().has_value());
    EXPECT_EQ(ob.bestBid().value(), 100u);
}

TEST(OrderBook, BestBidPicksHighest) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10));
    ob.addOrder(makeLimit(2, Side::Buy, 105, 10));
    ob.addOrder(makeLimit(3, Side::Buy,  95, 10));
    EXPECT_EQ(ob.bestBid().value(), 105u);
}

TEST(OrderBook, BestAskPicksLowest) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell, 100, 10));
    ob.addOrder(makeLimit(2, Side::Sell,  95, 10));
    ob.addOrder(makeLimit(3, Side::Sell, 110, 10));
    EXPECT_EQ(ob.bestAsk().value(), 95u);
}

TEST(OrderBook, BestBidFrontReturnsOldestOrder) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10, /*ts=*/1));
    ob.addOrder(makeLimit(2, Side::Buy, 100, 20, /*ts=*/2));
    Order* front = ob.bestBidFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 1u); // order 1 arrived first
}

TEST(OrderBook, BestAskFrontReturnsCorrectOrder) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell, 100, 10));
    ob.addOrder(makeLimit(2, Side::Sell,  95, 10));
    Order* front = ob.bestAskFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->price, 95u);  // lowest ask is at front
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------
TEST(OrderBook, CancelRemovesOrder) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10));
    ob.cancelOrder(1);
    EXPECT_FALSE(ob.bestBid().has_value());
    EXPECT_EQ(ob.bestBidFront(), nullptr);
}

TEST(OrderBook, CancelNonExistentNoCrash) {
    OrderBook ob;
    EXPECT_NO_THROW(ob.cancelOrder(999));
}

TEST(OrderBook, CancelCleansEmptyPriceLevel) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10));
    ob.addOrder(makeLimit(2, Side::Buy, 105, 10));
    ob.cancelOrder(2);
    EXPECT_EQ(ob.bestBid().value(), 100u); // 105 level gone
}

TEST(OrderBook, CancelFromFrontOfLevel) {
    // Cancel the head — next order becomes the new head.
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell, 100, 10));
    ob.addOrder(makeLimit(2, Side::Sell, 100, 10));
    ob.cancelOrder(1);
    Order* front = ob.bestAskFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 2u);
}

TEST(OrderBook, CancelFromMiddleOfLevel) {
    // Cancel the middle node — head and tail must be unaffected.
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell, 100, 10));
    ob.addOrder(makeLimit(2, Side::Sell, 100, 10));
    ob.addOrder(makeLimit(3, Side::Sell, 100, 10));
    ob.cancelOrder(2);

    // Front should still be order 1.
    Order* front = ob.bestAskFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 1u);

    // Cancel order 1 — order 3 should become front.
    ob.cancelOrder(1);
    front = ob.bestAskFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 3u);

    // Cancel order 3 — book empty.
    ob.cancelOrder(3);
    EXPECT_FALSE(ob.bestAsk().has_value());
    EXPECT_EQ(ob.bestAskFront(), nullptr);
}

TEST(OrderBook, CancelFromBackOfLevel) {
    // Cancel the tail — head must be unaffected.
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10));
    ob.addOrder(makeLimit(2, Side::Buy, 100, 10));
    ob.cancelOrder(2);
    Order* front = ob.bestBidFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 1u);
}

// ---------------------------------------------------------------------------
// Time priority
// ---------------------------------------------------------------------------
TEST(OrderBook, TimePriorityPreservedAtSamePrice) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Buy, 100, 10, /*ts=*/1));
    ob.addOrder(makeLimit(2, Side::Buy, 100, 20, /*ts=*/2));
    Order* front = ob.bestBidFront();
    ASSERT_NE(front, nullptr);
    EXPECT_EQ(front->id, 1u);
}

// ---------------------------------------------------------------------------
// Price level cleanup
// ---------------------------------------------------------------------------
TEST(OrderBook, BestAskUpdatesWhenLevelEmptied) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell,  95, 10));
    ob.addOrder(makeLimit(2, Side::Sell, 100, 10));
    ob.cancelOrder(1); // empties the 95 level
    EXPECT_EQ(ob.bestAsk().value(), 100u);
}

// ---------------------------------------------------------------------------
// In-place quantity modification (used by matching engine)
// ---------------------------------------------------------------------------
TEST(OrderBook, FrontPointerAllowsInPlaceQuantityUpdate) {
    OrderBook ob;
    ob.addOrder(makeLimit(1, Side::Sell, 100, 10));
    Order* front = ob.bestAskFront();
    ASSERT_NE(front, nullptr);
    front->quantity -= 4; // simulate partial fill
    EXPECT_EQ(ob.bestAskFront()->quantity, 6u);
}
