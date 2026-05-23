#include <chrono>
#include <cstdio>

// Phase 0 smoke benchmark: confirms the benchmark target compiles, links the
// lob interface library, and that a steady high-resolution clock is available.
int main() {
  const auto t0 = std::chrono::steady_clock::now();
  volatile long long acc = 0;
  for (long long i = 0; i < 1'000'000; ++i) acc += i;
  const auto t1 = std::chrono::steady_clock::now();
  const auto ns =
      std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
  std::printf("smoke_bench ok: acc=%lld elapsed=%lldns\n",
              static_cast<long long>(acc), static_cast<long long>(ns));
  return 0;
}
