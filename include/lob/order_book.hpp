#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "lob/flat_hash.hpp"
#include "lob/object_pool.hpp"
#include "lob/price_level.hpp"
#include "lob/types.hpp"

namespace lob {

struct BookConfig {
  Price min_price{1};             // inclusive lower tick bound
  Price max_price{100'000};       // inclusive upper tick bound
  std::size_t capacity{1u << 16}; // max simultaneously resting orders
};

// Cache-friendly limit order book; see the README for the full rationale.
//
// Levels are a flat array indexed by (price - min_price), so the price band is
// bounded and memory is proportional to its width. Prices outside the band are
// rejected rather than grown into.
//
// Bids and asks share that one array: the engine matches before resting, so the
// book is never left crossed and the occupied bid and ask prices stay disjoint.
class FlatBook {
public:
  FlatBook() : FlatBook(BookConfig{}) {}

  explicit FlatBook(const BookConfig& cfg)
      : min_price_(cfg.min_price),
        pool_(cfg.capacity),
        index_(cfg.capacity) {
    const std::size_t n =
        static_cast<std::size_t>(cfg.max_price - cfg.min_price + 1);
    levels_.assign(n, PriceLevel{});
  }

  [[nodiscard]] bool accepts(Price p) const noexcept {
    return p >= min_price_ &&
           static_cast<std::size_t>(p - min_price_) < levels_.size();
  }

  [[nodiscard]] std::optional<Price> best_bid() const {
    return best_bid_ < 0 ? std::nullopt
                         : std::optional<Price>(to_price(best_bid_));
  }
  [[nodiscard]] std::optional<Price> best_ask() const {
    return best_ask_ < 0 ? std::nullopt
                         : std::optional<Price>(to_price(best_ask_));
  }

  [[nodiscard]] bool side_empty(Side s) const noexcept {
    return s == Side::Buy ? best_bid_ < 0 : best_ask_ < 0;
  }

  [[nodiscard]] Price best_price(Side s) const {
    return to_price(s == Side::Buy ? best_bid_ : best_ask_);
  }

  [[nodiscard]] Order& front(Side s) {
    const std::int64_t idx = (s == Side::Buy) ? best_bid_ : best_ask_;
    return pool_[levels_[static_cast<std::size_t>(idx)].head];
  }

  void reduce_front(Side s, Quantity exec) {
    const std::int64_t idx = (s == Side::Buy) ? best_bid_ : best_ask_;
    PriceLevel& level = levels_[static_cast<std::size_t>(idx)];
    const std::uint32_t node = level.head;
    Order& o = pool_[node];
    if (exec >= o.qty) {
      index_.erase(o.id);
      level.unlink(pool_, node);
      pool_.deallocate(node);
      if (level.empty()) advance_best(s, idx);
    } else {
      o.qty -= exec;
      level.total_qty -= exec;
    }
  }

  void post(Side s, Price price, OrderId id, Quantity qty) {
    const std::int64_t idx = to_index(price);
    const std::uint32_t node = pool_.allocate();
    Order& o = pool_[node];
    o.id = id;
    o.qty = qty;
    o.price = price;
    o.side = s;
    levels_[static_cast<std::size_t>(idx)].push_back(pool_, node);
    index_.insert(id, node);
    if (s == Side::Buy) {
      if (best_bid_ < 0 || idx > best_bid_) best_bid_ = idx;
    } else {
      if (best_ask_ < 0 || idx < best_ask_) best_ask_ = idx;
    }
  }

  std::optional<CancelInfo> cancel(OrderId id) {
    const std::uint32_t* node_ptr = index_.find(id);
    if (!node_ptr) return std::nullopt;
    const std::uint32_t node = *node_ptr;
    const Order& o = pool_[node];
    const CancelInfo info{o.side, o.price, o.qty};
    const std::int64_t idx = to_index(o.price);
    PriceLevel& level = levels_[static_cast<std::size_t>(idx)];
    const Side side = o.side;
    index_.erase(id);
    level.unlink(pool_, node);
    pool_.deallocate(node);
    if (level.empty() &&
        ((side == Side::Buy && idx == best_bid_) ||
         (side == Side::Sell && idx == best_ask_))) {
      advance_best(side, idx);
    }
    return info;
  }

  [[nodiscard]] std::optional<CancelInfo> find(OrderId id) const {
    const std::uint32_t* node_ptr = index_.find(id);
    if (!node_ptr) return std::nullopt;
    const Order& o = pool_[*node_ptr];
    return CancelInfo{o.side, o.price, o.qty};
  }

  bool reduce_order(OrderId id, Quantity new_qty) {
    const std::uint32_t* node_ptr = index_.find(id);
    if (!node_ptr) return false;
    Order& o = pool_[*node_ptr];
    levels_[static_cast<std::size_t>(to_index(o.price))].total_qty -=
        (o.qty - new_qty);
    o.qty = new_qty;
    return true;
  }

  [[nodiscard]] bool contains(OrderId id) const { return index_.contains(id); }

  [[nodiscard]] Quantity available_against(Side aggressor,
                                           std::optional<Price> limit) const {
    Quantity total = 0;
    if (aggressor == Side::Buy) { // asks, cheapest first
      if (best_ask_ < 0) return 0;
      const std::int64_t hi = limit ? to_index_clamped_high(*limit)
                                    : static_cast<std::int64_t>(levels_.size()) - 1;
      for (std::int64_t i = best_ask_; i <= hi; ++i) total += levels_[static_cast<std::size_t>(i)].total_qty;
    } else { // bids, richest first
      if (best_bid_ < 0) return 0;
      const std::int64_t lo = limit ? to_index_clamped_low(*limit) : 0;
      for (std::int64_t i = best_bid_; i >= lo; --i) total += levels_[static_cast<std::size_t>(i)].total_qty;
    }
    return total;
  }

  [[nodiscard]] std::size_t size() const noexcept { return index_.size(); }

  // Walks every level, so this is for tests and debugging, not the hot path.
  [[nodiscard]] Quantity total_quantity() const {
    Quantity t = 0;
    for (const auto& level : levels_) t += level.total_qty;
    return t;
  }

private:
  [[nodiscard]] Price to_price(std::int64_t idx) const noexcept {
    return min_price_ + idx;
  }
  [[nodiscard]] std::int64_t to_index(Price p) const noexcept {
    return static_cast<std::int64_t>(p - min_price_);
  }
  // A limit price may sit outside the band, so clamp before using it as a bound.
  [[nodiscard]] std::int64_t to_index_clamped_high(Price p) const noexcept {
    const std::int64_t hi = static_cast<std::int64_t>(levels_.size()) - 1;
    const std::int64_t i = to_index(p);
    return i > hi ? hi : i;
  }
  [[nodiscard]] std::int64_t to_index_clamped_low(Price p) const noexcept {
    const std::int64_t i = to_index(p);
    return i < 0 ? 0 : i;
  }

  // Linear scan for the next occupied level once the best one empties. Real flow
  // clusters at the touch, so this is O(1) amortised; a lone order far from the
  // rest is the worst case the flat layout pays for.
  void advance_best(Side s, std::int64_t from) {
    if (s == Side::Buy) {
      for (std::int64_t i = from - 1; i >= 0; --i) {
        if (!levels_[static_cast<std::size_t>(i)].empty()) {
          best_bid_ = i;
          return;
        }
      }
      best_bid_ = -1;
    } else {
      const std::int64_t n = static_cast<std::int64_t>(levels_.size());
      for (std::int64_t i = from + 1; i < n; ++i) {
        if (!levels_[static_cast<std::size_t>(i)].empty()) {
          best_ask_ = i;
          return;
        }
      }
      best_ask_ = -1;
    }
  }

  Price min_price_;
  std::vector<PriceLevel> levels_;
  ObjectPool<Order> pool_;
  FlatHashIndex index_;
  std::int64_t best_bid_{-1};
  std::int64_t best_ask_{-1};
};

} // namespace lob
