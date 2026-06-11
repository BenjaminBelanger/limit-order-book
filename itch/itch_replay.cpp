#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

#include "lob/itch.hpp"
#include "lob/itch_replayer.hpp"
#include "lob/order_book.hpp"

// Reconstruct the order book from an ITCH 5.0 file and report throughput.
//
//   itch_replay [in_path]
//
// Works on the synthetic feed from itch_gen and on a real NASDAQ ITCH 5.0
// sample (decompress the .gz first). Default path: data/synthetic.itch.
int main(int argc, char** argv) {
  const char* in_path = argc > 1 ? argv[1] : "data/synthetic.itch";

  std::ifstream in(in_path, std::ios::binary | std::ios::ate);
  if (!in) {
    std::fprintf(stderr,
                 "failed to open %s\n"
                 "  generate one with:  itch_gen %s 5000000\n",
                 in_path, in_path);
    return 1;
  }
  const std::streamsize size = in.tellg();
  in.seekg(0);
  std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
  in.read(reinterpret_cast<char*>(data.data()), size);

  // Band covers $0.01..$1000 in cents; capacity sized for the synthetic peak.
  lob::BookConfig cfg{1, 100'000, 1u << 21};
  lob::FlatBook book(cfg);
  lob::itch::ItchReplayer replayer(book);

  const auto t0 = std::chrono::steady_clock::now();
  const std::size_t msgs =
      lob::itch::parse_stream(data.data(), data.size(), replayer);
  const auto t1 = std::chrono::steady_clock::now();

  const double secs = std::chrono::duration<double>(t1 - t0).count();
  const auto& s = replayer.stats();

  std::printf("ITCH 5.0 replay: %s\n", in_path);
  std::printf("  file size        : %.1f MB\n",
              static_cast<double>(size) / 1e6);
  std::printf("  messages         : %zu\n", msgs);
  std::printf("  adds=%llu exec=%llu cancel=%llu del=%llu replace=%llu skip=%llu\n",
              (unsigned long long)s.adds, (unsigned long long)s.executes,
              (unsigned long long)s.cancels, (unsigned long long)s.deletes,
              (unsigned long long)s.replaces,
              (unsigned long long)s.skipped_out_of_band);
  std::printf("  peak resting     : %zu orders\n", s.peak_resting_orders);
  std::printf("  final resting    : %zu orders\n", book.size());
  if (book.best_bid())
    std::printf("  final best bid   : %lld\n", (long long)*book.best_bid());
  if (book.best_ask())
    std::printf("  final best ask   : %lld\n", (long long)*book.best_ask());
  std::printf("  elapsed          : %.3f s\n", secs);
  std::printf("  reconstruction   : %.2f M msgs/s  (%.1f MB/s)\n",
              static_cast<double>(msgs) / secs / 1e6,
              static_cast<double>(size) / secs / 1e6);
  return 0;
}
