#pragma once

#include <algorithm>
#include <optional>

#include "lob/types.hpp"

// The engine is templated on a `Book` backend and an `EventHandler` sink so the
// exact same matching logic runs on both backends, which is what makes the
// differential test meaningful. Neither requirement is a C++ concept, so the
// duck-typed Book interface is spelled out here:
//
//   std::optional<Price> best_bid() const;
//   std::optional<Price> best_ask() const;
//   bool                 side_empty(Side) const;
//   Price                best_price(Side) const;        // precondition: !side_empty
//   RestingOrder&        front(Side);                   // front FIFO at best level
//   void                 reduce_front(Side, Quantity);  // removes it if it hits zero
//   void                 post(Side, Price, OrderId, Quantity);
//   std::optional<CancelInfo> cancel(OrderId);
//   std::optional<CancelInfo> find(OrderId) const;
//   bool                 reduce_order(OrderId, Quantity);// keeps time priority
//   bool                 contains(OrderId) const;
//   bool                 accepts(Price) const;
//   Quantity             available_against(Side aggressor, std::optional<Price>) const;
//
// EventHandler is any callable invocable as `handler(const Event&)`.
namespace lob {

template <class Book, class EventHandler>
class MatchingEngine {
public:
  MatchingEngine(Book& book, EventHandler& handler)
      : book_(book), handler_(handler) {}

  void submit(const OrderRequest& req) {
    if (req.id == kInvalidOrderId || book_.contains(req.id)) {
      reject(req.id, req.side, req.price, req.quantity, Reason::DuplicateOrderId);
      return;
    }
    if (req.quantity == 0) {
      reject(req.id, req.side, req.price, req.quantity, Reason::InvalidQuantity);
      return;
    }
    if (req.type == OrderType::Limit && req.price <= 0) {
      reject(req.id, req.side, req.price, req.quantity, Reason::InvalidPrice);
      return;
    }
    if (req.type == OrderType::Limit && !book_.accepts(req.price)) {
      reject(req.id, req.side, req.price, req.quantity, Reason::BookFull);
      return;
    }

    const bool is_market = (req.type == OrderType::Market);
    const std::optional<Price> limit =
        is_market ? std::nullopt : std::optional<Price>(req.price);

    if (req.type == OrderType::Fok &&
        book_.available_against(req.side, limit) < req.quantity) {
      reject(req.id, req.side, req.price, req.quantity, Reason::FokUnsatisfiable);
      return;
    }

    const Quantity remaining = match(req.id, req.side, limit, req.quantity);
    if (remaining == 0) return; // fully filled; taker Filled already emitted

    switch (req.type) {
      case OrderType::Limit:
        book_.post(req.side, req.price, req.id, remaining);
        emit(EventType::Accepted, req.id, kInvalidOrderId, req.side, req.price,
             remaining, remaining, Reason::None);
        break;
      case OrderType::Ioc:
        emit(EventType::Cancelled, req.id, kInvalidOrderId, req.side, req.price,
             remaining, 0, Reason::IocRemainder);
        break;
      case OrderType::Market: {
        const Reason r = (remaining == req.quantity) ? Reason::NoLiquidity
                                                     : Reason::MarketRemainder;
        emit(EventType::Cancelled, req.id, kInvalidOrderId, req.side, req.price,
             remaining, 0, r);
        break;
      }
      case OrderType::Fok:
        break; // unreachable: pre-checked to fully fill
    }
  }

  void cancel(OrderId id) {
    const std::optional<CancelInfo> info = book_.cancel(id);
    if (!info) {
      reject(id, Side::Buy, 0, 0, Reason::UnknownOrder);
      return;
    }
    emit(EventType::Cancelled, id, kInvalidOrderId, info->side, info->price,
         info->qty, 0, Reason::UserRequest);
  }

  // Only a same-price reduction keeps time priority; anything else is a cancel
  // plus a re-submit, which sends the order to the back of the new level.
  void modify(OrderId id, Price new_price, Quantity new_qty) {
    const std::optional<CancelInfo> info = book_.find(id);
    if (!info) {
      reject(id, Side::Buy, 0, 0, Reason::UnknownOrder);
      return;
    }
    if (new_qty == 0) {
      cancel(id);
      return;
    }
    if (new_price == info->price && new_qty <= info->qty) {
      book_.reduce_order(id, new_qty);
      emit(EventType::Accepted, id, kInvalidOrderId, info->side, new_price,
           new_qty, new_qty, Reason::None);
      return;
    }
    const Side side = info->side;
    book_.cancel(id);
    emit(EventType::Cancelled, id, kInvalidOrderId, side, info->price, info->qty,
         0, Reason::UserRequest);
    submit(OrderRequest{id, side, OrderType::Limit, new_price, new_qty});
  }

  [[nodiscard]] std::optional<Price> best_bid() const { return book_.best_bid(); }
  [[nodiscard]] std::optional<Price> best_ask() const { return book_.best_ask(); }

  [[nodiscard]] std::optional<Price> spread() const {
    const auto b = book_.best_bid();
    const auto a = book_.best_ask();
    if (b && a) return *a - *b;
    return std::nullopt;
  }

private:
  // Returns the unfilled remainder of `qty`.
  Quantity match(OrderId taker_id, Side taker_side, std::optional<Price> limit,
                 Quantity qty) {
    const Side opp = opposite(taker_side);
    while (qty > 0 && !book_.side_empty(opp)) {
      const Price best = book_.best_price(opp);
      if (limit && !crosses(taker_side, *limit, best)) break;

      auto& maker = book_.front(opp);
      const OrderId maker_id = maker.id;
      const Quantity exec = std::min(qty, maker.qty);
      const Quantity maker_remaining = maker.qty - exec;

      qty -= exec;

      emit(maker_remaining == 0 ? EventType::Filled : EventType::PartiallyFilled,
           maker_id, taker_id, opp, best, exec, maker_remaining, Reason::None);
      emit(qty == 0 ? EventType::Filled : EventType::PartiallyFilled, taker_id,
           maker_id, taker_side, best, exec, qty, Reason::None);

      book_.reduce_front(opp, exec);
    }
    return qty;
  }

  void reject(OrderId id, Side side, Price price, Quantity qty, Reason r) {
    emit(EventType::Rejected, id, kInvalidOrderId, side, price, qty, 0, r);
  }

  void emit(EventType type, OrderId id, OrderId counter, Side side, Price price,
            Quantity qty, Quantity remaining, Reason r) {
    Event e;
    e.type = type;
    e.order_id = id;
    e.counter_id = counter;
    e.side = side;
    e.price = price;
    e.quantity = qty;
    e.remaining = remaining;
    e.reason = r;
    handler_(e);
  }

  Book& book_;
  EventHandler& handler_;
};

} // namespace lob
