#pragma once

#include <chrono>
#include <cstdint>

// Cycle timer built on the x86 time-stamp counter. steady_clock here has
// ~100 ns granularity, which quantizes away per-operation latencies measured in
// tens of nanoseconds; rdtsc resolves single cycles, calibrated to nanoseconds
// against steady_clock once at startup. Falls back to steady_clock off x86.

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#define LOB_HAVE_TSC 1
#include <x86intrin.h>
#else
#define LOB_HAVE_TSC 0
#endif

namespace bench {

[[nodiscard]] inline std::uint64_t rdtsc_now() {
#if LOB_HAVE_TSC
  _mm_lfence(); // serialize, so the timed region cannot drift across the read
  const std::uint64_t t = __rdtsc();
  _mm_lfence();
  return t;
#else
  return static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

class TscClock {
public:
  void calibrate() {
    using namespace std::chrono;
    const auto t0 = steady_clock::now();
    const std::uint64_t c0 = rdtsc_now();
    while (duration_cast<milliseconds>(steady_clock::now() - t0).count() < 200) {
    }
    const std::uint64_t c1 = rdtsc_now();
    const auto t1 = steady_clock::now();
    const double ns = duration<double, std::nano>(t1 - t0).count();
    ns_per_cycle_ = ns / static_cast<double>(c1 - c0);
  }

  [[nodiscard]] std::uint64_t now() const { return rdtsc_now(); }
  [[nodiscard]] double to_ns(std::uint64_t cycles) const {
    return static_cast<double>(cycles) * ns_per_cycle_;
  }
  [[nodiscard]] double ghz() const { return 1.0 / ns_per_cycle_; }

private:
  double ns_per_cycle_{1.0};
};

} // namespace bench
