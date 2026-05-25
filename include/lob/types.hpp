#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

// Core value types and the event model shared by every order book backend.
//
// Prices are represented as signed integer *ticks* (fixed-point). Working in
// integer ticks avoids floating-point comparisons on the hot path and lets the
// optimized backend index price levels directly into a flat array.
namespace lob {

using OrderId = std::uint64_t;
using Price = std::int64_t;     // price in ticks
using Quantity = std::uint64_t; // shares / lots

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

// A request submitted to the matching engine.
struct OrderRequest {
  OrderId id{kInvalidOrderId};
  Side side{Side::Buy};
  OrderType type{OrderType::Limit};
  Price price{0};       // ignored for Market orders
  Quantity quantity{0};
};

// ---- Event model -------------------------------------------------------------
//
// The engine emits exactly these five event kinds:
//   * Accepted        - (part of) the order now rests on the book.
//   * Rejected        - the order was refused (see Reason); the book is unchanged.
//   * PartiallyFilled - an execution left the order with remaining quantity.
//   * Filled          - an execution fully consumed the order.
//   * Cancelled       - the order (or an unfilled remainder) was removed.
//
// A single trade emits two execution events: one for the resting (maker) order
// and one for the incoming (taker) order.
enum class EventType : std::uint8_t {
  Accepted,
  Rejected,
  PartiallyFilled,
  Filled,
  Cancelled,
};

enum class Reason : std::uint8_t {
  None,
  UserRequest,           // explicit cancel
  IocRemainder,          // IOC leftover after matching
  MarketRemainder,       // market order ran out of liquidity
  FokUnsatisfiable,      // FOK could not be fully filled
  NoLiquidity,           // marketable order found nothing to match
  DuplicateOrderId,      // id already active
  UnknownOrder,          // cancel/modify of a non-existent order
  InvalidQuantity,       // quantity == 0
  InvalidPrice,          // non-positive limit price
  BookFull,              // price outside the configured band (flat backend)
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

// A resting order as stored inside a book. Side/price are known from context
// (the level it lives on), so only identity and remaining quantity are kept.
struct RestingOrder {
  OrderId id{kInvalidOrderId};
  Quantity qty{0};
};

// Returned by Book::cancel so the engine can emit a Cancelled event.
struct CancelInfo {
  Side side{Side::Buy};
  Price price{0};
  Quantity qty{0};
};

// True if an aggressor on `aggressor` side priced at `limit` crosses a resting
// order priced at `resting`.
[[nodiscard]] inline constexpr bool crosses(Side aggressor, Price limit,
                                            Price resting) noexcept {
  return aggressor == Side::Buy ? limit >= resting : limit <= resting;
}

} // namespace lob
