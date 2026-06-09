#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "lob/hdr_histogram.hpp"

using namespace lob;

namespace {
std::int64_t ref_percentile(std::vector<std::int64_t> v, double p) {
  std::sort(v.begin(), v.end());
  const std::size_t idx = static_cast<std::size_t>(
      std::ceil(p / 100.0 * static_cast<double>(v.size()))) - 1;
  return v[std::min(idx, v.size() - 1)];
}
} // namespace

TEST(HdrHistogram, BasicCounts) {
  HdrHistogram h;
  for (int i = 0; i < 100; ++i) h.record(50);
  EXPECT_EQ(h.total_count(), 100);
  EXPECT_EQ(h.min(), 50);
  EXPECT_EQ(h.max(), 50);
  EXPECT_NEAR(h.mean(), 50.0, 1e-9);
  // Single value: every percentile resolves to ~50 (within HDR resolution).
  EXPECT_NEAR(static_cast<double>(h.value_at_percentile(50)), 50.0, 1.0);
  EXPECT_NEAR(static_cast<double>(h.value_at_percentile(99.9)), 50.0, 1.0);
}

TEST(HdrHistogram, PercentilesMatchReferenceWithinPrecision) {
  HdrHistogram h(100'000'000, 3); // 3 significant figures
  std::mt19937 rng(7);
  // Mixed distribution: a bulk near 100ns plus heavy-tailed outliers.
  std::vector<std::int64_t> samples;
  std::normal_distribution<double> bulk(120.0, 25.0);
  std::lognormal_distribution<double> tail(6.0, 1.0);
  for (int i = 0; i < 200000; ++i) {
    double v = (i % 50 == 0) ? tail(rng) : bulk(rng);
    if (v < 1) v = 1;
    const std::int64_t s = static_cast<std::int64_t>(v);
    samples.push_back(s);
    h.record(s);
  }

  for (double p : {50.0, 90.0, 99.0, 99.9}) {
    const double got = static_cast<double>(h.value_at_percentile(p));
    const double ref = static_cast<double>(ref_percentile(samples, p));
    // HDR guarantees the reported value is within 0.1% of the true value.
    EXPECT_LE(std::abs(got - ref), 0.01 * ref + 2.0)
        << "p=" << p << " got=" << got << " ref=" << ref;
  }
}

TEST(HdrHistogram, ForEachVisitsAllSamples) {
  HdrHistogram h;
  for (std::int64_t v : {10, 10, 200, 5000, 5000, 5000}) h.record(v);
  std::int64_t total = 0;
  h.for_each([&](std::int64_t, std::int64_t count) { total += count; });
  EXPECT_EQ(total, h.total_count());
  EXPECT_EQ(total, 6);
}
