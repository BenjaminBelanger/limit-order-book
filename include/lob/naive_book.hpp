#pragma once

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>

#include "lob/types.hpp"

// Reference order book. Deliberately unoptimized: node-based containers and
// pointer chasing on the hot path are the point, since this is both the
// obviously-correct baseline the flat book is differentially tested against and
// the comparison target the benchmark measures against.
namespace lob {

class NaiveBook {
public:
  using Level = std::list<RestingOrder>;

  [[nodiscard]] std::optional<Price> best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
  }
  [[nodiscard]] std::optional<Price> best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
  }

  [[nodiscard]] bool side_empty(Side s) const {
    return s == Side::Buy ? bids_.empty() : asks_.empty();
  }

  [[nodiscard]] Price best_price(Side s) const {
    return s == Side::Buy ? bids_.begin()->first : asks_.begin()->first;
  }

  [[nodiscard]] RestingOrder& front(Side s) {
    return s == Side::Buy ? bids_.begin()->second.front()
                          : asks_.begin()->second.front();
  }

  void reduce_front(Side s, Quantity exec) {
    if (s == Side::Buy) {
      reduce_front_impl(bids_, exec);
    } else {
      reduce_front_impl(asks_, exec);
    }
  }

  void post(Side s, Price price, OrderId id, Quantity qty) {
    if (s == Side::Buy) {
      post_impl(bids_, s, price, id, qty);
    } else {
      post_impl(asks_, s, price, id, qty);
    }
  }

  std::optional<CancelInfo> cancel(OrderId id) {
    const auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    const Locator loc = it->second;
    const CancelInfo info{loc.side, loc.price, loc.it->qty};
    if (loc.side == Side::Buy) {
      erase_at(bids_, loc);
    } else {
      erase_at(asks_, loc);
    }
    index_.erase(it);
    return info;
  }

  [[nodiscard]] std::optional<CancelInfo> find(OrderId id) const {
    const auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    const Locator& loc = it->second;
    return CancelInfo{loc.side, loc.price, loc.it->qty};
  }

  bool reduce_order(OrderId id, Quantity new_qty) {
    const auto it = index_.find(id);
    if (it == index_.end()) return false;
    it->second.it->qty = new_qty;
    return true;
  }

  [[nodiscard]] bool contains(OrderId id) const {
    return index_.find(id) != index_.end();
  }

  // No price band here, unlike FlatBook, so nothing is ever out of range.
  [[nodiscard]] static constexpr bool accepts(Price) noexcept { return true; }

  [[nodiscard]] Quantity available_against(Side aggressor,
                                           std::optional<Price> limit) const {
    Quantity total = 0;
    if (aggressor == Side::Buy) { // asks, cheapest first
      for (const auto& [price, level] : asks_) {
        if (limit && price > *limit) break;
        total += level_qty(level);
      }
    } else { // bids, richest first
      for (const auto& [price, level] : bids_) {
        if (limit && price < *limit) break;
        total += level_qty(level);
      }
    }
    return total;
  }

  [[nodiscard]] std::size_t size() const { return index_.size(); }

  // Walks every level, so this is for tests and debugging, not the hot path.
  [[nodiscard]] Quantity total_quantity() const {
    Quantity t = 0;
    for (const auto& [price, level] : bids_) t += level_qty(level);
    for (const auto& [price, level] : asks_) t += level_qty(level);
    return t;
  }

private:
  struct Locator {
    Side side;
    Price price;
    Level::iterator it;
  };

  static Quantity level_qty(const Level& level) {
    Quantity q = 0;
    for (const auto& o : level) q += o.qty;
    return q;
  }

  template <class Map>
  void reduce_front_impl(Map& book, Quantity exec) {
    auto level_it = book.begin();
    Level& level = level_it->second;
    RestingOrder& o = level.front();
    if (exec >= o.qty) {
      index_.erase(o.id);
      level.pop_front();
      if (level.empty()) book.erase(level_it);
    } else {
      o.qty -= exec;
    }
  }

  template <class Map>
  void post_impl(Map& book, Side s, Price price, OrderId id, Quantity qty) {
    Level& level = book[price];
    level.push_back(RestingOrder{id, qty});
    index_.emplace(id, Locator{s, price, std::prev(level.end())});
  }

  template <class Map>
  void erase_at(Map& book, const Locator& loc) {
    const auto level_it = book.find(loc.price);
    Level& level = level_it->second;
    level.erase(loc.it);
    if (level.empty()) book.erase(level_it);
  }

  // Comparators put the best price of each side at begin().
  std::map<Price, Level, std::greater<Price>> bids_;
  std::map<Price, Level, std::less<Price>> asks_;
  std::unordered_map<OrderId, Locator> index_;
};

} // namespace lob
