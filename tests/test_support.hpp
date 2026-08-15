#pragma once

#include <cstddef>
#include <vector>

#include "lob/types.hpp"

namespace lob::test {

struct CollectingSink {
  std::vector<Event> events;

  void operator()(const Event& e) { events.push_back(e); }

  void clear() { events.clear(); }

  [[nodiscard]] std::size_t count(EventType t) const {
    std::size_t n = 0;
    for (const auto& e : events)
      if (e.type == t) ++n;
    return n;
  }

  [[nodiscard]] std::vector<Event> of_type(EventType t) const {
    std::vector<Event> out;
    for (const auto& e : events)
      if (e.type == t) out.push_back(e);
    return out;
  }

  [[nodiscard]] const Event& last() const { return events.back(); }
};

inline OrderRequest limit(OrderId id, Side side, Price price, Quantity qty) {
  return OrderRequest{id, side, OrderType::Limit, price, qty};
}
inline OrderRequest market(OrderId id, Side side, Quantity qty) {
  return OrderRequest{id, side, OrderType::Market, 0, qty};
}
inline OrderRequest ioc(OrderId id, Side side, Price price, Quantity qty) {
  return OrderRequest{id, side, OrderType::Ioc, price, qty};
}
inline OrderRequest fok(OrderId id, Side side, Price price, Quantity qty) {
  return OrderRequest{id, side, OrderType::Fok, price, qty};
}

} // namespace lob::test
