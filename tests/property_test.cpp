#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

#include "lob/matching_engine.hpp"
#include "lob/naive_book.hpp"
#include "lob/order_book.hpp"
#include "test_support.hpp"

using namespace lob;
using namespace lob::test;

namespace {

// One generated operation in a random scenario.
struct Op {
  enum class Kind { Submit, Cancel, Modify } kind;
  OrderRequest req;       // for Submit
  OrderId target{0};      // for Cancel / Modify
  Price new_price{0};     // for Modify
  Quantity new_qty{0};    // for Modify
};

constexpr Price kMinPrice = 1;
constexpr Price kMaxPrice = 200;

// Deterministically generate a mixed sequence of operations for a given seed.
std::vector<Op> generate(std::uint32_t seed, std::size_t count) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> op_pick(0, 9);
  std::uniform_int_distribution<int> type_pick(0, 3);
  std::uniform_int_distribution<int> side_pick(0, 1);
  std::uniform_int_distribution<Price> price_pick(kMinPrice, kMaxPrice);
  std::uniform_int_distribution<int> qty_pick(1, 10);

  std::vector<Op> ops;
  ops.reserve(count);
  std::vector<OrderId> seen;
  OrderId next_id = 1;

  for (std::size_t i = 0; i < count; ++i) {
    const int pick = op_pick(rng);
    if (pick < 7 || seen.empty()) { // mostly submits
      Op op;
      op.kind = Op::Kind::Submit;
      op.req.id = next_id++;
      op.req.side = side_pick(rng) ? Side::Buy : Side::Sell;
      op.req.type = static_cast<OrderType>(type_pick(rng));
      op.req.price = price_pick(rng);
      op.req.quantity = static_cast<Quantity>(qty_pick(rng));
      seen.push_back(op.req.id);
      ops.push_back(op);
    } else if (pick < 9) { // cancel a previously-seen id
      Op op;
      op.kind = Op::Kind::Cancel;
      op.target = seen[std::uniform_int_distribution<std::size_t>(
          0, seen.size() - 1)(rng)];
      ops.push_back(op);
    } else { // modify a previously-seen id
      Op op;
      op.kind = Op::Kind::Modify;
      op.target = seen[std::uniform_int_distribution<std::size_t>(
          0, seen.size() - 1)(rng)];
      op.new_price = price_pick(rng);
      op.new_qty = static_cast<Quantity>(qty_pick(rng));
      ops.push_back(op);
    }
  }
  return ops;
}

bool same_event(const Event& a, const Event& b) {
  return a.type == b.type && a.order_id == b.order_id &&
         a.counter_id == b.counter_id && a.side == b.side &&
         a.price == b.price && a.quantity == b.quantity &&
         a.remaining == b.remaining && a.reason == b.reason;
}

// Run a scenario through `Book`, checking the no-crossed-book invariant after
// every operation. Returns the full event stream.
template <class Book>
std::vector<Event> run(const std::vector<Op>& ops, Book& book) {
  CollectingSink sink;
  MatchingEngine<Book, CollectingSink> engine(book, sink);

  for (const Op& op : ops) {
    switch (op.kind) {
      case Op::Kind::Submit: engine.submit(op.req); break;
      case Op::Kind::Cancel: engine.cancel(op.target); break;
      case Op::Kind::Modify:
        engine.modify(op.target, op.new_price, op.new_qty);
        break;
    }
    const auto bb = engine.best_bid();
    const auto ba = engine.best_ask();
    if (bb && ba) {
      EXPECT_LT(*bb, *ba) << "book is crossed/locked";
    }
  }
  return sink.events;
}

// Reconstruct resting state purely from the event stream and verify it matches
// the book's own accounting (conservation of quantity).
template <class Book>
void check_conservation(const std::vector<Event>& events, const Book& book) {
  std::unordered_map<OrderId, Quantity> resting; // id -> resting qty
  for (const Event& e : events) {
    switch (e.type) {
      case EventType::Accepted:
        resting[e.order_id] = e.remaining;
        break;
      case EventType::PartiallyFilled:
        // Only the maker (already resting) updates resting state; the taker leg
        // refers to the incoming order, which is not yet resting.
        if (auto it = resting.find(e.order_id); it != resting.end())
          it->second = e.remaining;
        break;
      case EventType::Filled:
      case EventType::Cancelled:
        resting.erase(e.order_id);
        break;
      case EventType::Rejected:
        break;
    }
  }
  Quantity expected = 0;
  for (const auto& [id, q] : resting) expected += q;

  EXPECT_EQ(book.size(), resting.size());
  EXPECT_EQ(book.total_quantity(), expected);
}

} // namespace

// Headline property: two completely different book implementations must produce
// byte-for-byte identical event streams on the same random scenario.
TEST(Property, DifferentialNaiveVsFlatEventStreams) {
  for (std::uint32_t seed = 1; seed <= 25; ++seed) {
    const auto ops = generate(seed, 4000);

    NaiveBook naive;
    BookConfig cfg{kMinPrice, kMaxPrice, 1u << 16};
    FlatBook flat(cfg);

    const auto naive_events = run(ops, naive);
    const auto flat_events = run(ops, flat);

    ASSERT_EQ(naive_events.size(), flat_events.size())
        << "event count diverged at seed " << seed;
    for (std::size_t i = 0; i < naive_events.size(); ++i) {
      ASSERT_TRUE(same_event(naive_events[i], flat_events[i]))
          << "event " << i << " diverged at seed " << seed
          << " (naive type=" << to_string(naive_events[i].type)
          << " flat type=" << to_string(flat_events[i].type) << ")";
    }
  }
}

// Invariants: no crossed book (checked inside run) + quantity conservation.
TEST(Property, InvariantsHoldOnRandomScenarios) {
  for (std::uint32_t seed = 100; seed <= 130; ++seed) {
    const auto ops = generate(seed, 4000);

    NaiveBook naive;
    const auto naive_events = run(ops, naive);
    check_conservation(naive_events, naive);

    BookConfig cfg{kMinPrice, kMaxPrice, 1u << 16};
    FlatBook flat(cfg);
    const auto flat_events = run(ops, flat);
    check_conservation(flat_events, flat);
  }
}
