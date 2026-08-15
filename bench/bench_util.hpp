#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "lob/types.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace bench {

using Clock = std::chrono::steady_clock;

// Discards every event, so the benchmark measures the engine and not the cost
// of observing it.
struct NullSink {
  void operator()(const lob::Event&) noexcept {}
};

struct Stats {
  std::uint64_t count{0};
  double min{0}, p50{0}, p99{0}, p999{0}, max{0}, mean{0};
};

inline Stats summarize(std::vector<std::uint64_t>& samples) {
  Stats s;
  if (samples.empty()) return s;
  std::sort(samples.begin(), samples.end());
  const auto pct = [&](double p) -> double {
    const std::size_t idx = static_cast<std::size_t>(
        p * static_cast<double>(samples.size() - 1));
    return static_cast<double>(samples[idx]);
  };
  s.count = samples.size();
  s.min = static_cast<double>(samples.front());
  s.max = static_cast<double>(samples.back());
  s.p50 = pct(0.50);
  s.p99 = pct(0.99);
  s.p999 = pct(0.999);
  const long double sum =
      std::accumulate(samples.begin(), samples.end(), (long double)0);
  s.mean = static_cast<double>(sum / static_cast<long double>(samples.size()));
  return s;
}

// Per-call clock read overhead, to subtract from single-operation samples: at
// these latencies the measurement itself is a meaningful share of the number.
inline double clock_overhead_ns() {
  constexpr int kIters = 200'000;
  std::uint64_t acc = 0;
  for (int i = 0; i < kIters; ++i) {
    const auto a = Clock::now();
    const auto b = Clock::now();
    acc += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
  }
  return static_cast<double>(acc) / kIters;
}

// Best effort only: pinning and priority reduce scheduling jitter but cannot
// remove it, since there is no real-time scheduling here.
inline void pin_and_boost() {
#if defined(_WIN32)
  SetThreadAffinityMask(GetCurrentThread(), 1ull);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

inline void print_row(const char* name, const Stats& s, double ops_per_sec) {
  std::printf("%-14s %10llu %9.1f %9.1f %9.1f %9.1f %12.2f\n", name,
              static_cast<unsigned long long>(s.count), s.p50, s.p99, s.p999,
              s.max, ops_per_sec / 1e6);
}

inline void print_header(const char* title) {
  std::printf("\n=== %s ===\n", title);
  std::printf("%-14s %10s %9s %9s %9s %9s %12s\n", "operation", "samples",
              "p50(ns)", "p99(ns)", "p99.9(ns)", "max(ns)", "M ops/s");
}

} // namespace bench
