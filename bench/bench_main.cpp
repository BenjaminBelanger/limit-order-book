#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <numeric>
#include <random>
#include <thread>
#include <vector>

#include "bench_util.hpp"
#include "tsc_clock.hpp"
#include "lob/hdr_histogram.hpp"
#include "lob/matching_engine.hpp"
#include "lob/naive_book.hpp"
#include "lob/order_book.hpp"
#include "lob/spsc_ring.hpp"
#include "lob/types.hpp"

using namespace lob;
using bench::Clock;
using bench::NullSink;
using bench::Stats;

namespace {

double seconds(Clock::duration d) {
  return std::chrono::duration<double>(d).count();
}

constexpr Price kLo = 1;
constexpr Price kHi = 1'000'000;
constexpr std::size_t kPriceSpan = 1u << 16; // distinct prices touched

double g_overhead = 0.0; // rdtsc read overhead, in nanoseconds
bench::TscClock g_tsc;

std::uint64_t sample_ns(std::uint64_t c0, std::uint64_t c1) {
  const double ns = g_tsc.to_ns(c1 - c0) - g_overhead;
  return ns > 0 ? static_cast<std::uint64_t>(ns) : 0;
}

// ---- add: insert resting orders, no matching ---------------------------------
template <class Book, class Factory>
void bench_add(Factory make, std::size_t n, Stats& lat, double& tput,
               HdrHistogram* hist = nullptr) {
  { // throughput pass (no per-op clock)
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
      const Price p = kLo + static_cast<Price>(i & (kPriceSpan - 1));
      eng.submit(OrderRequest{i + 1, Side::Buy, OrderType::Limit, p, 1});
    }
    tput = static_cast<double>(n) / seconds(Clock::now() - t0);
  }
  { // latency pass (per-op clock)
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    std::vector<std::uint64_t> s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      const Price p = kLo + static_cast<Price>(i & (kPriceSpan - 1));
      const auto a = g_tsc.now();
      eng.submit(OrderRequest{i + 1, Side::Buy, OrderType::Limit, p, 1});
      const std::uint64_t ns = sample_ns(a, g_tsc.now());
      s.push_back(ns);
      if (hist) hist->record(static_cast<std::int64_t>(ns));
    }
    lat = bench::summarize(s);
  }
}

// ---- cancel: remove resting orders in randomized order -----------------------
template <class Book, class Factory>
void bench_cancel(Factory make, std::size_t n, Stats& lat, double& tput,
                  HdrHistogram* hist = nullptr) {
  std::vector<OrderId> ids(n);
  std::iota(ids.begin(), ids.end(), OrderId{1});
  std::shuffle(ids.begin(), ids.end(), std::mt19937(12345));

  const auto prefill = [&](MatchingEngine<Book, NullSink>& eng) {
    for (std::size_t i = 0; i < n; ++i) {
      const Price p = kLo + static_cast<Price>(i & (kPriceSpan - 1));
      eng.submit(OrderRequest{i + 1, Side::Buy, OrderType::Limit, p, 1});
    }
  };
  { // throughput
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    prefill(eng);
    const auto t0 = Clock::now();
    for (OrderId id : ids) eng.cancel(id);
    tput = static_cast<double>(n) / seconds(Clock::now() - t0);
  }
  { // latency
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    prefill(eng);
    std::vector<std::uint64_t> s;
    s.reserve(n);
    for (OrderId id : ids) {
      const auto a = g_tsc.now();
      eng.cancel(id);
      const std::uint64_t ns = sample_ns(a, g_tsc.now());
      s.push_back(ns);
      if (hist) hist->record(static_cast<std::int64_t>(ns));
    }
    lat = bench::summarize(s);
  }
}

// ---- match: aggressive orders each consume one resting order -----------------
template <class Book, class Factory>
void bench_match(Factory make, std::size_t n, Stats& lat, double& tput,
                 HdrHistogram* hist = nullptr) {
  const auto prefill = [&](MatchingEngine<Book, NullSink>& eng) {
    for (std::size_t i = 0; i < n; ++i) {
      const Price p = kLo + static_cast<Price>(i & (kPriceSpan - 1));
      eng.submit(OrderRequest{i + 1, Side::Sell, OrderType::Limit, p, 1});
    }
  };
  { // throughput
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    prefill(eng);
    const auto t0 = Clock::now();
    for (std::size_t i = 0; i < n; ++i) {
      eng.submit(OrderRequest{n + i + 1, Side::Buy, OrderType::Ioc, kHi, 1});
    }
    tput = static_cast<double>(n) / seconds(Clock::now() - t0);
  }
  { // latency
    Book book = make();
    NullSink sink;
    MatchingEngine<Book, NullSink> eng(book, sink);
    prefill(eng);
    std::vector<std::uint64_t> s;
    s.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
      const auto a = g_tsc.now();
      eng.submit(OrderRequest{n + i + 1, Side::Buy, OrderType::Ioc, kHi, 1});
      const std::uint64_t ns = sample_ns(a, g_tsc.now());
      s.push_back(ns);
      if (hist) hist->record(static_cast<std::int64_t>(ns));
    }
    lat = bench::summarize(s);
  }
}

struct SuiteResult {
  Stats add, cancel, match;
  double add_t{0}, cancel_t{0}, match_t{0};
};

template <class Book, class Factory>
SuiteResult run_suite(const char* label, Factory make, std::size_t n) {
  // Warm up caches / branch predictors before measuring.
  Stats warm_l;
  double warm_t = 0;
  bench_add<Book>(make, n / 10, warm_l, warm_t);

  SuiteResult r;
  bench_add<Book>(make, n, r.add, r.add_t);
  bench_cancel<Book>(make, n, r.cancel, r.cancel_t);
  bench_match<Book>(make, n, r.match, r.match_t);

  bench::print_header(label);
  bench::print_row("add", r.add, r.add_t);
  bench::print_row("cancel", r.cancel, r.cancel_t);
  bench::print_row("match", r.match, r.match_t);
  return r;
}

// Export a histogram as a latency-by-percentile spectrum CSV (HdrHistogram
// style): each row is a percentile and the latency at or below which that
// fraction of operations completed. A plotting script turns these into a chart.
void export_hdr(const std::string& op, const HdrHistogram& h) {
  namespace fs = std::filesystem;
  fs::create_directories("bench/results");
  std::ofstream out("bench/results/hdr_" + op + ".csv");
  out << "percentile,latency_ns\n";
  static const double kPercentiles[] = {
      0,    10,   20,   30,   40,   50,    60,    70,    80,     90,
      95,   97.5, 99,   99.5, 99.9, 99.95, 99.99, 99.999, 100.0};
  for (double p : kPercentiles) {
    out << p << "," << h.value_at_percentile(p) << "\n";
  }
}

// ---- SPSC ingestion pipeline -------------------------------------------------
// Producer thread enqueues orders; consumer thread pops and feeds the engine.
// Orders alternate buy/sell at one price so they match immediately and the book
// stays tiny -- this measures the end-to-end ingestion + match pipeline rate.
double bench_spsc(std::size_t n) {
  SpscRing<OrderRequest> ring(1u << 16);
  BookConfig cfg{90, 110, 1024};
  FlatBook book(cfg);
  NullSink sink;
  MatchingEngine<FlatBook, NullSink> eng(book, sink);

  std::atomic<bool> start{false};
  std::thread producer([&] {
    while (!start.load(std::memory_order_acquire)) {}
    for (std::size_t i = 0; i < n; ++i) {
      OrderRequest r{i + 1, (i & 1) ? Side::Sell : Side::Buy, OrderType::Ioc,
                     100, 1};
      while (!ring.try_push(r)) {}
    }
  });

  start.store(true, std::memory_order_release);
  const auto t0 = Clock::now();
  std::size_t processed = 0;
  OrderRequest r{};
  while (processed < n) {
    if (ring.try_pop(r)) {
      eng.submit(r);
      ++processed;
    }
  }
  const double elapsed = seconds(Clock::now() - t0);
  producer.join();
  return static_cast<double>(n) / elapsed;
}

} // namespace

int main() {
  bench::pin_and_boost();
  g_tsc.calibrate();

  // Measure rdtsc read overhead (subtracted from latency samples).
  {
    constexpr int kIters = 200'000;
    std::uint64_t acc = 0;
    for (int i = 0; i < kIters; ++i) {
      const auto a = g_tsc.now();
      acc += g_tsc.now() - a;
    }
    g_overhead = g_tsc.to_ns(acc / kIters);
  }

  constexpr std::size_t kN = 1'000'000;
  std::printf("Limit Order Book microbenchmarks\n");
  std::printf("TSC ~ %.2f GHz | rdtsc read overhead ~ %.1f ns (subtracted)\n",
              g_tsc.ghz(), g_overhead);

  const auto make_flat = [] {
    return FlatBook(BookConfig{kLo, kHi, kN + 16});
  };
  const auto make_naive = [] { return NaiveBook(); };

  const SuiteResult flat = run_suite<FlatBook>("FlatBook (optimized)", make_flat, kN);
  const SuiteResult naive =
      run_suite<NaiveBook>("NaiveBook (std::map baseline)", make_naive, kN);

  std::printf("\n=== Speedup (optimized vs naive, throughput) ===\n");
  std::printf("%-14s %12s %12s %10s\n", "operation", "flat M/s", "naive M/s",
              "speedup");
  std::printf("%-14s %12.2f %12.2f %9.2fx\n", "add", flat.add_t / 1e6,
              naive.add_t / 1e6, flat.add_t / naive.add_t);
  std::printf("%-14s %12.2f %12.2f %9.2fx\n", "cancel", flat.cancel_t / 1e6,
              naive.cancel_t / 1e6, flat.cancel_t / naive.cancel_t);
  std::printf("%-14s %12.2f %12.2f %9.2fx\n", "match", flat.match_t / 1e6,
              naive.match_t / 1e6, flat.match_t / naive.match_t);

  // ---- HDR latency capture + CSV export (for plotting) -----------------------
  {
    HdrHistogram h_add, h_cancel, h_match;
    Stats tmp;
    double tmp_t = 0;
    bench_add<FlatBook>(make_flat, kN, tmp, tmp_t, &h_add);
    bench_cancel<FlatBook>(make_flat, kN, tmp, tmp_t, &h_cancel);
    bench_match<FlatBook>(make_flat, kN, tmp, tmp_t, &h_match);
    export_hdr("add", h_add);
    export_hdr("cancel", h_cancel);
    export_hdr("match", h_match);
    std::printf("\n=== HDR latency (FlatBook) -> bench/results/hdr_*.csv ===\n");
    std::printf("%-8s %8s %8s %8s %10s\n", "op", "p50", "p99", "p99.9",
                "p99.99");
    const auto row = [](const char* op, const HdrHistogram& h) {
      std::printf("%-8s %8lld %8lld %8lld %10lld\n", op,
                  static_cast<long long>(h.value_at_percentile(50)),
                  static_cast<long long>(h.value_at_percentile(99)),
                  static_cast<long long>(h.value_at_percentile(99.9)),
                  static_cast<long long>(h.value_at_percentile(99.99)));
    };
    row("add", h_add);
    row("cancel", h_cancel);
    row("match", h_match);
  }

  const double spsc = bench_spsc(10'000'000);
  std::printf("\n=== SPSC ingestion pipeline (1 producer -> 1 consumer) ===\n");
  std::printf("alternating IOC orders, end-to-end: %.2f M orders/s\n",
              spsc / 1e6);
  return 0;
}
