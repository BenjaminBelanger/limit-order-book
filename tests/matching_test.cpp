#include <gtest/gtest.h>

#include "lob/matching_engine.hpp"
#include "lob/naive_book.hpp"
#include "test_support.hpp"

using namespace lob;
using namespace lob::test;

// Typed test fixture: every case below runs against each book backend in the
// type list. Phase 2 adds the optimized FlatBook here for free.
template <class Book>
class MatchingTest : public ::testing::Test {
protected:
  Book book_{};
  CollectingSink sink_{};
  MatchingEngine<Book, CollectingSink> engine_{book_, sink_};
};

using BookTypes = ::testing::Types<NaiveBook>;
TYPED_TEST_SUITE(MatchingTest, BookTypes);

// ---- Resting / BBO -----------------------------------------------------------
TYPED_TEST(MatchingTest, LimitOrderRestsAndUpdatesBbo) {
  EXPECT_FALSE(this->engine_.best_bid().has_value());

  this->engine_.submit(limit(1, Side::Buy, 100, 10));

  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Accepted);
  EXPECT_EQ(this->sink_.last().order_id, 1u);
  EXPECT_EQ(this->sink_.last().remaining, 10u);
  ASSERT_TRUE(this->engine_.best_bid().has_value());
  EXPECT_EQ(*this->engine_.best_bid(), 100);
  EXPECT_FALSE(this->engine_.best_ask().has_value());
}

TYPED_TEST(MatchingTest, SpreadComputedFromBbo) {
  this->engine_.submit(limit(1, Side::Buy, 99, 5));
  this->engine_.submit(limit(2, Side::Sell, 101, 5));
  ASSERT_TRUE(this->engine_.spread().has_value());
  EXPECT_EQ(*this->engine_.spread(), 2);
}

// ---- Basic matching ----------------------------------------------------------
TYPED_TEST(MatchingTest, FullMatchEmptiesBook) {
  this->engine_.submit(limit(1, Side::Buy, 100, 10));
  this->sink_.clear();

  this->engine_.submit(limit(2, Side::Sell, 100, 10));

  ASSERT_EQ(this->sink_.events.size(), 2u);
  EXPECT_EQ(this->sink_.events[0].type, EventType::Filled); // maker
  EXPECT_EQ(this->sink_.events[0].order_id, 1u);
  EXPECT_EQ(this->sink_.events[0].counter_id, 2u);
  EXPECT_EQ(this->sink_.events[0].price, 100);
  EXPECT_EQ(this->sink_.events[0].quantity, 10u);
  EXPECT_EQ(this->sink_.events[1].type, EventType::Filled); // taker
  EXPECT_EQ(this->sink_.events[1].order_id, 2u);
  EXPECT_FALSE(this->engine_.best_bid().has_value());
  EXPECT_FALSE(this->engine_.best_ask().has_value());
}

TYPED_TEST(MatchingTest, RestingOrderPartiallyFilled) {
  this->engine_.submit(limit(1, Side::Buy, 100, 10));
  this->sink_.clear();

  this->engine_.submit(limit(2, Side::Sell, 100, 4));

  ASSERT_EQ(this->sink_.events.size(), 2u);
  EXPECT_EQ(this->sink_.events[0].type, EventType::PartiallyFilled);
  EXPECT_EQ(this->sink_.events[0].order_id, 1u);
  EXPECT_EQ(this->sink_.events[0].remaining, 6u);
  EXPECT_EQ(this->sink_.events[1].type, EventType::Filled);
  EXPECT_EQ(this->sink_.events[1].order_id, 2u);
  EXPECT_EQ(*this->engine_.best_bid(), 100);
}

TYPED_TEST(MatchingTest, IncomingRemainderPostsAfterPartialMatch) {
  this->engine_.submit(limit(1, Side::Sell, 100, 4));
  this->sink_.clear();

  this->engine_.submit(limit(2, Side::Buy, 100, 10));

  ASSERT_EQ(this->sink_.events.size(), 3u);
  EXPECT_EQ(this->sink_.events[0].type, EventType::Filled);          // maker
  EXPECT_EQ(this->sink_.events[1].type, EventType::PartiallyFilled); // taker
  EXPECT_EQ(this->sink_.events[1].remaining, 6u);
  EXPECT_EQ(this->sink_.events[2].type, EventType::Accepted);        // remainder rests
  EXPECT_EQ(this->sink_.events[2].order_id, 2u);
  EXPECT_EQ(this->sink_.events[2].remaining, 6u);
  EXPECT_EQ(*this->engine_.best_bid(), 100);
  EXPECT_FALSE(this->engine_.best_ask().has_value());
}

// ---- Price-time priority -----------------------------------------------------
TYPED_TEST(MatchingTest, TimePriorityFifoAtSamePrice) {
  this->engine_.submit(limit(1, Side::Buy, 100, 5));
  this->engine_.submit(limit(2, Side::Buy, 100, 5)); // later -> behind id 1
  this->sink_.clear();

  this->engine_.submit(limit(3, Side::Sell, 100, 5));

  // First resting order (id 1) must trade first.
  ASSERT_GE(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.events[0].order_id, 1u);
  EXPECT_EQ(this->sink_.events[0].counter_id, 3u);
}

TYPED_TEST(MatchingTest, PricePriorityBestLevelFirst) {
  this->engine_.submit(limit(1, Side::Sell, 101, 5));
  this->engine_.submit(limit(2, Side::Sell, 100, 5)); // better ask
  this->sink_.clear();

  this->engine_.submit(limit(3, Side::Buy, 101, 5));

  ASSERT_GE(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.events[0].order_id, 2u); // best (lowest) ask matched
  EXPECT_EQ(this->sink_.events[0].price, 100);
  EXPECT_EQ(*this->engine_.best_ask(), 101); // id 1 remains
}

// ---- Market orders -----------------------------------------------------------
TYPED_TEST(MatchingTest, MarketOrderSweepsLevels) {
  this->engine_.submit(limit(1, Side::Sell, 100, 5));
  this->engine_.submit(limit(2, Side::Sell, 101, 5));
  this->sink_.clear();

  this->engine_.submit(market(3, Side::Buy, 8));

  EXPECT_EQ(this->sink_.count(EventType::Filled), 2u);          // maker1 + taker
  EXPECT_EQ(this->sink_.count(EventType::PartiallyFilled), 2u); // taker leg + maker2
  EXPECT_EQ(*this->engine_.best_ask(), 101);
}

TYPED_TEST(MatchingTest, MarketOrderEmptyBookRejectedRemainder) {
  this->engine_.submit(market(1, Side::Buy, 5));
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Cancelled);
  EXPECT_EQ(this->sink_.last().reason, Reason::NoLiquidity);
}

TYPED_TEST(MatchingTest, MarketOrderCancelsLeftoverRemainder) {
  this->engine_.submit(limit(1, Side::Sell, 100, 3));
  this->sink_.clear();

  this->engine_.submit(market(2, Side::Buy, 5));

  EXPECT_EQ(this->sink_.count(EventType::Cancelled), 1u);
  EXPECT_EQ(this->sink_.of_type(EventType::Cancelled)[0].reason,
            Reason::MarketRemainder);
  EXPECT_EQ(this->sink_.of_type(EventType::Cancelled)[0].quantity, 2u);
}

// ---- IOC ---------------------------------------------------------------------
TYPED_TEST(MatchingTest, IocFillsAvailableAndCancelsRest) {
  this->engine_.submit(limit(1, Side::Sell, 100, 3));
  this->sink_.clear();

  this->engine_.submit(ioc(2, Side::Buy, 100, 5));

  EXPECT_EQ(this->sink_.count(EventType::Filled), 1u);    // maker
  EXPECT_EQ(this->sink_.count(EventType::PartiallyFilled), 1u); // taker leg
  ASSERT_EQ(this->sink_.count(EventType::Cancelled), 1u);
  EXPECT_EQ(this->sink_.of_type(EventType::Cancelled)[0].reason,
            Reason::IocRemainder);
  EXPECT_FALSE(this->engine_.best_bid().has_value()); // never rests
}

TYPED_TEST(MatchingTest, IocNoMatchCancelsFully) {
  this->engine_.submit(limit(1, Side::Sell, 101, 5));
  this->sink_.clear();

  this->engine_.submit(ioc(2, Side::Buy, 100, 5)); // does not cross

  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Cancelled);
  EXPECT_EQ(this->sink_.last().reason, Reason::IocRemainder);
  EXPECT_EQ(*this->engine_.best_ask(), 101); // book unchanged
}

// ---- FOK ---------------------------------------------------------------------
TYPED_TEST(MatchingTest, FokFullyFillable) {
  this->engine_.submit(limit(1, Side::Sell, 100, 5));
  this->sink_.clear();

  this->engine_.submit(fok(2, Side::Buy, 100, 5));

  EXPECT_EQ(this->sink_.count(EventType::Filled), 2u);
  EXPECT_EQ(this->sink_.count(EventType::Rejected), 0u);
}

TYPED_TEST(MatchingTest, FokUnsatisfiableRejectedLeavesBookUntouched) {
  this->engine_.submit(limit(1, Side::Sell, 100, 3));
  this->sink_.clear();

  this->engine_.submit(fok(2, Side::Buy, 100, 5));

  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Rejected);
  EXPECT_EQ(this->sink_.last().reason, Reason::FokUnsatisfiable);
  EXPECT_EQ(*this->engine_.best_ask(), 100); // untouched
}

// ---- Cancel ------------------------------------------------------------------
TYPED_TEST(MatchingTest, CancelExistingOrder) {
  this->engine_.submit(limit(1, Side::Buy, 100, 5));
  this->sink_.clear();

  this->engine_.cancel(1);

  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Cancelled);
  EXPECT_EQ(this->sink_.last().reason, Reason::UserRequest);
  EXPECT_FALSE(this->engine_.best_bid().has_value());
}

TYPED_TEST(MatchingTest, CancelNonExistentRejected) {
  this->engine_.cancel(999);
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Rejected);
  EXPECT_EQ(this->sink_.last().reason, Reason::UnknownOrder);
}

// ---- Modify ------------------------------------------------------------------
TYPED_TEST(MatchingTest, ModifyReduceKeepsTimePriority) {
  this->engine_.submit(limit(1, Side::Buy, 100, 10));
  this->engine_.submit(limit(2, Side::Buy, 100, 10)); // behind id 1
  this->engine_.modify(1, 100, 4);                    // reduce in place
  this->sink_.clear();

  this->engine_.submit(limit(3, Side::Sell, 100, 4));

  // id 1 kept its front position despite the modify.
  ASSERT_GE(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.events[0].order_id, 1u);
  EXPECT_EQ(this->sink_.events[0].type, EventType::Filled);
}

TYPED_TEST(MatchingTest, ModifyPriceChangeLosesPriority) {
  this->engine_.submit(limit(1, Side::Buy, 100, 10));
  this->engine_.submit(limit(2, Side::Buy, 100, 10));
  this->sink_.clear();

  this->engine_.modify(1, 99, 10); // reprice down -> cancel + repost

  // Cancelled then Accepted at the new price.
  EXPECT_EQ(this->sink_.count(EventType::Cancelled), 1u);
  EXPECT_EQ(this->sink_.count(EventType::Accepted), 1u);

  this->sink_.clear();
  this->engine_.submit(limit(3, Side::Sell, 100, 5));
  // id 2 is now the front at 100.
  ASSERT_GE(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.events[0].order_id, 2u);
}

TYPED_TEST(MatchingTest, ModifyUnknownRejected) {
  this->engine_.modify(42, 100, 5);
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().type, EventType::Rejected);
  EXPECT_EQ(this->sink_.last().reason, Reason::UnknownOrder);
}

// ---- Validation --------------------------------------------------------------
TYPED_TEST(MatchingTest, ZeroQuantityRejected) {
  this->engine_.submit(limit(1, Side::Buy, 100, 0));
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().reason, Reason::InvalidQuantity);
}

TYPED_TEST(MatchingTest, NonPositiveLimitPriceRejected) {
  this->engine_.submit(limit(1, Side::Buy, 0, 5));
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().reason, Reason::InvalidPrice);
}

TYPED_TEST(MatchingTest, DuplicateOrderIdRejected) {
  this->engine_.submit(limit(1, Side::Buy, 100, 5));
  this->sink_.clear();
  this->engine_.submit(limit(1, Side::Buy, 100, 5));
  ASSERT_EQ(this->sink_.events.size(), 1u);
  EXPECT_EQ(this->sink_.last().reason, Reason::DuplicateOrderId);
}

// ---- Crossed-book / self-cross ----------------------------------------------
TYPED_TEST(MatchingTest, AggressiveLimitNeverLeavesCrossedBook) {
  this->engine_.submit(limit(1, Side::Sell, 100, 5));
  this->sink_.clear();

  this->engine_.submit(limit(2, Side::Buy, 105, 8)); // priced through the ask

  // Matched at the resting ask price, remainder rests above -> not crossed.
  EXPECT_EQ(this->sink_.of_type(EventType::Filled)[0].price, 100);
  ASSERT_TRUE(this->engine_.best_bid().has_value());
  EXPECT_EQ(*this->engine_.best_bid(), 105);
  EXPECT_FALSE(this->engine_.best_ask().has_value());
}

TYPED_TEST(MatchingTest, EmptyBookQueries) {
  EXPECT_FALSE(this->engine_.best_bid().has_value());
  EXPECT_FALSE(this->engine_.best_ask().has_value());
  EXPECT_FALSE(this->engine_.spread().has_value());
  this->engine_.cancel(1);
  EXPECT_EQ(this->sink_.last().type, EventType::Rejected);
}
