#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// Prices are signed integer *ticks*, never floats: this keeps matching free of
// floating-point comparison hazards and lets the optimized backend turn a price
// into an array index with one subtraction.
namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;
using Quantity = std::uint64_t;

inline constexpr OrderId kInvalidOrderId = 0;

enum class Side : std::uint8_t { Buy, Sell };

enum class OrderType : std::uint8_t {
  Limit,  // rests on the book; remainder posts after matching
  Market, // matches available liquidity at any price; never rests
  Ioc,    // immediate-or-cancel: match what is possible, cancel the rest
  Fok,    // fill-or-kill: fully fill immediately or reject entirely
};

[[nodiscard]] inline constexpr Side opposite(Side s) noexcept {
  return s == Side::Buy ? Side::Sell : Side::Buy;
}

struct OrderRequest {
  OrderId id{kInvalidOrderId};
  Side side{Side::Buy};
  OrderType type{OrderType::Limit};
  Price price{0};       // ignored for Market orders
  Quantity quantity{0};
};

// A single trade emits two execution events: one for the resting (maker) order
// and one for the incoming (taker) order, in that order.
enum class EventType : std::uint8_t {
  Accepted,
  Rejected,
  PartiallyFilled,
  Filled,
  Cancelled,
};

enum class Reason : std::uint8_t {
  None,
  UserRequest,      // explicit cancel
  IocRemainder,     // IOC leftover after matching
  MarketRemainder,  // market order ran out of liquidity
  FokUnsatisfiable, // FOK could not be fully filled
  NoLiquidity,      // marketable order found nothing to match
  DuplicateOrderId, // id already active
  UnknownOrder,     // cancel/modify of a non-existent order
  InvalidQuantity,  // quantity == 0
  InvalidPrice,     // non-positive limit price
  BookFull,         // price outside the configured band (flat backend)
};

struct Event {
  EventType type{EventType::Accepted};
  OrderId order_id{kInvalidOrderId};
  OrderId counter_id{kInvalidOrderId}; // counterparty for fills; 0 otherwise
  Side side{Side::Buy};                // side of order_id
  Price price{0};                      // execution price (fills) / resting price
  Quantity quantity{0};                // executed qty (fills) / resting qty
  Quantity remaining{0};               // remaining qty on order_id after event
  Reason reason{Reason::None};
};

[[nodiscard]] inline constexpr std::string_view to_string(EventType t) noexcept {
  switch (t) {
    case EventType::Accepted: return "Accepted";
    case EventType::Rejected: return "Rejected";
    case EventType::PartiallyFilled: return "PartiallyFilled";
    case EventType::Filled: return "Filled";
    case EventType::Cancelled: return "Cancelled";
  }
  return "?";
}

// Side and price are implied by the level an order rests on, so a stored order
// only carries identity and remaining quantity.
struct RestingOrder {
  OrderId id{kInvalidOrderId};
  Quantity qty{0};
};

struct CancelInfo {
  Side side{Side::Buy};
  Price price{0};
  Quantity qty{0};
};

[[nodiscard]] inline constexpr bool crosses(Side aggressor, Price limit,
                                            Price resting) noexcept {
  return aggressor == Side::Buy ? limit >= resting : limit <= resting;
}

} // namespace lob
