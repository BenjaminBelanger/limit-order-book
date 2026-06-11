#pragma once

#include <cstdint>

#include "lob/itch.hpp"
#include "lob/order_book.hpp"
#include "lob/types.hpp"

namespace lob::itch {

// Reconstructs the displayable order book from an ITCH 5.0 stream by applying
// add/execute/cancel/delete/replace messages directly to a FlatBook. (ITCH is
// post-match exchange output, so executions are *applied*, not re-matched.)
//
// ITCH prices have 4 implied decimals (value/10000 USD). The flat book indexes a
// bounded tick band, so prices are scaled to cents (value/100) which covers
// equities up to ~$10,000 with a 1,000,000-entry level array. This mirrors the
// flat-array tradeoff discussed in the README; orders outside the band are
// skipped and counted.
class ItchReplayer {
public:
  explicit ItchReplayer(FlatBook& book, std::uint32_t price_divisor = 100)
      : book_(book), price_divisor_(price_divisor) {}

  struct Stats {
    std::uint64_t adds{0};
    std::uint64_t executes{0};
    std::uint64_t cancels{0};
    std::uint64_t deletes{0};
    std::uint64_t replaces{0};
    std::uint64_t skipped_out_of_band{0};
    std::size_t peak_resting_orders{0};
  };

  [[nodiscard]] const Stats& stats() const { return stats_; }

  void on_add(std::uint64_t ref, bool is_buy, std::uint32_t shares,
              std::uint32_t price) {
    const Price tick = static_cast<Price>(price / price_divisor_);
    if (!book_.accepts(tick)) {
      ++stats_.skipped_out_of_band;
      return;
    }
    book_.post(is_buy ? Side::Buy : Side::Sell, tick, ref, shares);
    ++stats_.adds;
    track_peak();
  }

  void on_execute(std::uint64_t ref, std::uint32_t shares) {
    reduce(ref, shares);
    ++stats_.executes;
  }
  void on_execute_price(std::uint64_t ref, std::uint32_t shares,
                        std::uint32_t /*price*/) {
    reduce(ref, shares);
    ++stats_.executes;
  }
  void on_cancel(std::uint64_t ref, std::uint32_t shares) {
    reduce(ref, shares);
    ++stats_.cancels;
  }
  void on_delete(std::uint64_t ref) {
    book_.cancel(ref);
    ++stats_.deletes;
  }
  void on_replace(std::uint64_t orig_ref, std::uint64_t new_ref,
                  std::uint32_t shares, std::uint32_t price) {
    const auto info = book_.find(orig_ref);
    if (!info) return;
    const Side side = info->side;
    book_.cancel(orig_ref);
    const Price tick = static_cast<Price>(price / price_divisor_);
    if (!book_.accepts(tick)) {
      ++stats_.skipped_out_of_band;
      return;
    }
    book_.post(side, tick, new_ref, shares);
    ++stats_.replaces;
    track_peak();
  }

private:
  void reduce(std::uint64_t ref, std::uint32_t shares) {
    const auto info = book_.find(ref);
    if (!info) return;
    if (static_cast<Quantity>(shares) >= info->qty) {
      book_.cancel(ref);
    } else {
      book_.reduce_order(ref, info->qty - static_cast<Quantity>(shares));
    }
  }

  void track_peak() {
    if (book_.size() > stats_.peak_resting_orders)
      stats_.peak_resting_orders = book_.size();
  }

  FlatBook& book_;
  std::uint32_t price_divisor_;
  Stats stats_{};
};

} // namespace lob::itch
