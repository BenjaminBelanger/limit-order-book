#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <unordered_map>
#include <vector>

#include "lob/itch_writer.hpp"

// Generate a synthetic NASDAQ ITCH 5.0 stream.
//
//   itch_gen [out_path] [num_messages]
//
// Message refs and prices are tracked as they are emitted so that executes,
// cancels, deletes and replaces always name a live order: a stream of random
// bytes would parse but would reconstruct nothing.
int main(int argc, char** argv) {
  const char* out_path = argc > 1 ? argv[1] : "data/synthetic.itch";
  const std::size_t target = argc > 2 ? std::strtoull(argv[2], nullptr, 10)
                                      : 5'000'000ull;

  lob::itch::ItchWriter w;
  std::mt19937_64 rng(42);
  std::uniform_int_distribution<int> action(0, 99);
  std::uniform_int_distribution<int> side(0, 1);
  std::uniform_int_distribution<std::uint32_t> shares(1, 1000);
  // Ticks are cents, 1..100,000 ($0.01..$1000); ITCH prices are tick * 100
  // because the wire format carries 4 implied decimals.
  std::uniform_int_distribution<std::uint32_t> tick(1, 100'000);

  std::unordered_map<std::uint64_t, std::uint32_t> live; // ref -> shares
  std::vector<std::uint64_t> refs;
  std::uint64_t next_ref = 1;
  constexpr std::size_t kMaxLive = 300'000; // caps peak resting orders

  w.system_event('O'); // start of messages
  std::size_t produced = 1;

  // Swap with the back: refs is only ever sampled at random, so its order is
  // free to change.
  const auto remove_ref = [&](std::uint64_t ref, std::size_t vec_idx) {
    live.erase(ref);
    refs[vec_idx] = refs.back();
    refs.pop_back();
  };

  while (produced < target) {
    const int a = action(rng);
    const bool want_add = refs.empty() || a < 55;
    if (want_add && refs.size() < kMaxLive) { // ~55% adds (capped)
      const std::uint64_t ref = next_ref++;
      const std::uint32_t sh = shares(rng);
      live[ref] = sh;
      refs.push_back(ref);
      w.add_order(ref, side(rng) == 0, sh, tick(rng) * 100);
    } else {
      const std::size_t idx =
          std::uniform_int_distribution<std::size_t>(0, refs.size() - 1)(rng);
      const std::uint64_t ref = refs[idx];
      std::uint32_t& cur = live[ref];
      const int b = action(rng);
      if (b < 45) { // execute (partial or full)
        const std::uint32_t ex =
            std::uniform_int_distribution<std::uint32_t>(1, cur)(rng);
        w.execute(ref, ex);
        if (ex >= cur) remove_ref(ref, idx);
        else cur -= ex;
      } else if (b < 65) { // cancel (partial or full)
        const std::uint32_t cx =
            std::uniform_int_distribution<std::uint32_t>(1, cur)(rng);
        w.cancel(ref, cx);
        if (cx >= cur) remove_ref(ref, idx);
        else cur -= cx;
      } else if (b < 82) { // delete
        w.delete_order(ref);
        remove_ref(ref, idx);
      } else { // replace
        const std::uint64_t nref = next_ref++;
        const std::uint32_t sh = shares(rng);
        w.replace(ref, nref, sh, tick(rng) * 100);
        live.erase(ref);
        live[nref] = sh;
        refs[idx] = nref;
      }
    }
    ++produced;
  }

  std::ofstream out(out_path, std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "failed to open %s for writing\n", out_path);
    return 1;
  }
  out.write(reinterpret_cast<const char*>(w.bytes().data()),
            static_cast<std::streamsize>(w.bytes().size()));
  std::printf("wrote %zu messages (%.1f MB) to %s\n", produced,
              static_cast<double>(w.bytes().size()) / 1e6, out_path);
  return 0;
}
