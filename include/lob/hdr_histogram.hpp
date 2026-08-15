#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace lob {

// From-scratch implementation of the HdrHistogram algorithm (Gil Tene). Values
// are bucketed by power-of-two magnitude with a fixed number of linear
// sub-buckets inside each magnitude, which records any value in O(1) at bounded
// relative error across a huge dynamic range. That range is the point for
// latency work: microsecond outliers must not cost nanosecond resolution near
// the median.
class HdrHistogram {
public:
  explicit HdrHistogram(std::int64_t highest_trackable = 100'000'000,
                        int significant_figures = 3) {
    const std::int64_t largest_with_unit_resolution =
        2 * static_cast<std::int64_t>(std::pow(10, significant_figures));
    const int sub_bucket_count_magnitude = static_cast<int>(
        std::ceil(std::log2(static_cast<double>(largest_with_unit_resolution))));
    sub_bucket_half_count_magnitude_ =
        sub_bucket_count_magnitude > 0 ? sub_bucket_count_magnitude - 1 : 0;
    sub_bucket_count_ =
        static_cast<std::int32_t>(1) << (sub_bucket_half_count_magnitude_ + 1);
    sub_bucket_half_count_ = sub_bucket_count_ / 2;
    sub_bucket_mask_ = static_cast<std::int64_t>(sub_bucket_count_ - 1);

    std::int32_t bucket_count = 1;
    std::int64_t smallest_untrackable = sub_bucket_count_;
    while (smallest_untrackable < highest_trackable) {
      smallest_untrackable <<= 1;
      ++bucket_count;
    }
    bucket_count_ = bucket_count;
    counts_len_ = (bucket_count_ + 1) * sub_bucket_half_count_;
    counts_.assign(static_cast<std::size_t>(counts_len_), 0);
  }

  void record(std::int64_t value) {
    if (value < 0) value = 0;
    ++counts_[static_cast<std::size_t>(counts_index_for(value))];
    ++total_;
    sum_ += value;
    if (value > max_) max_ = value;
    if (value < min_) min_ = value;
  }

  [[nodiscard]] std::int64_t value_at_percentile(double percentile) const {
    if (total_ == 0) return 0;
    const double requested = (percentile / 100.0) * static_cast<double>(total_);
    const std::int64_t count_to =
        static_cast<std::int64_t>(std::ceil(requested));
    std::int64_t cumulative = 0;
    for (std::int32_t i = 0; i < counts_len_; ++i) {
      cumulative += counts_[static_cast<std::size_t>(i)];
      if (cumulative >= count_to) {
        return highest_equivalent_value(value_from_index(i));
      }
    }
    return max_;
  }

  [[nodiscard]] std::int64_t total_count() const noexcept { return total_; }
  [[nodiscard]] std::int64_t min() const noexcept { return total_ ? min_ : 0; }
  [[nodiscard]] std::int64_t max() const noexcept { return max_; }
  [[nodiscard]] double mean() const noexcept {
    return total_ ? static_cast<double>(sum_) / static_cast<double>(total_) : 0.0;
  }

  // Calls f(value, count) for every non-empty entry, in ascending value order.
  template <class F>
  void for_each(F&& f) const {
    for (std::int32_t i = 0; i < counts_len_; ++i) {
      const std::int64_t c = counts_[static_cast<std::size_t>(i)];
      if (c) f(value_from_index(i), c);
    }
  }

private:
  [[nodiscard]] std::int32_t counts_index_for(std::int64_t value) const {
    const std::int32_t bucket_index = get_bucket_index(value);
    const std::int32_t sub_bucket_index =
        get_sub_bucket_index(value, bucket_index);
    return counts_index(bucket_index, sub_bucket_index);
  }
  [[nodiscard]] std::int32_t get_bucket_index(std::int64_t value) const {
    const int pow2ceiling = 64 - __builtin_clzll(static_cast<unsigned long long>(
                                     value | sub_bucket_mask_));
    return pow2ceiling - (sub_bucket_half_count_magnitude_ + 1);
  }
  [[nodiscard]] std::int32_t get_sub_bucket_index(std::int64_t value,
                                                  std::int32_t bucket_index) const {
    return static_cast<std::int32_t>(value >> bucket_index);
  }
  [[nodiscard]] std::int32_t counts_index(std::int32_t bucket_index,
                                          std::int32_t sub_bucket_index) const {
    const std::int32_t bucket_base =
        (bucket_index + 1) << sub_bucket_half_count_magnitude_;
    const std::int32_t offset = sub_bucket_index - sub_bucket_half_count_;
    return bucket_base + offset;
  }
  [[nodiscard]] std::int64_t value_from_index(std::int32_t index) const {
    std::int32_t bucket_index =
        (index >> sub_bucket_half_count_magnitude_) - 1;
    std::int32_t sub_bucket_index =
        (index & (sub_bucket_half_count_ - 1)) + sub_bucket_half_count_;
    if (bucket_index < 0) {
      sub_bucket_index -= sub_bucket_half_count_;
      bucket_index = 0;
    }
    return static_cast<std::int64_t>(sub_bucket_index) << bucket_index;
  }
  [[nodiscard]] std::int64_t size_of_equivalent_range(std::int64_t value) const {
    return static_cast<std::int64_t>(1) << get_bucket_index(value);
  }
  [[nodiscard]] std::int64_t lowest_equivalent_value(std::int64_t value) const {
    const std::int32_t bi = get_bucket_index(value);
    const std::int32_t si = get_sub_bucket_index(value, bi);
    return static_cast<std::int64_t>(si) << bi;
  }
  [[nodiscard]] std::int64_t highest_equivalent_value(std::int64_t value) const {
    return lowest_equivalent_value(value) + size_of_equivalent_range(value) - 1;
  }

  std::int32_t sub_bucket_half_count_magnitude_{0};
  std::int32_t sub_bucket_count_{0};
  std::int32_t sub_bucket_half_count_{0};
  std::int64_t sub_bucket_mask_{0};
  std::int32_t bucket_count_{0};
  std::int32_t counts_len_{0};
  std::vector<std::int64_t> counts_;
  std::int64_t total_{0};
  std::int64_t sum_{0};
  std::int64_t min_{INT64_MAX};
  std::int64_t max_{0};
};

} // namespace lob
