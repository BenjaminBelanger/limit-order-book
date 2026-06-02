#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "lob/spsc_ring.hpp"

using namespace lob;

TEST(SpscRing, CapacityRoundedToPowerOfTwo) {
  SpscRing<int> ring(5);
  EXPECT_EQ(ring.capacity(), 8u);
}

TEST(SpscRing, PushPopSingleThread) {
  SpscRing<int> ring(4); // capacity 4
  EXPECT_TRUE(ring.empty());
  EXPECT_TRUE(ring.try_push(1));
  EXPECT_TRUE(ring.try_push(2));
  EXPECT_TRUE(ring.try_push(3));
  EXPECT_TRUE(ring.try_push(4));
  EXPECT_FALSE(ring.try_push(5)); // full

  int v = 0;
  EXPECT_TRUE(ring.try_pop(v));
  EXPECT_EQ(v, 1);
  EXPECT_TRUE(ring.try_push(5)); // space again
  EXPECT_TRUE(ring.try_pop(v));
  EXPECT_EQ(v, 2);
}

TEST(SpscRing, PopEmptyReturnsFalse) {
  SpscRing<int> ring(2);
  int v = 0;
  EXPECT_FALSE(ring.try_pop(v));
}

// Concurrent producer/consumer: every value is delivered exactly once, in order.
TEST(SpscRing, ConcurrentProducerConsumer) {
  constexpr std::uint64_t kN = 1'000'000;
  SpscRing<std::uint64_t> ring(1024);

  std::thread producer([&] {
    for (std::uint64_t i = 0; i < kN; ++i) {
      while (!ring.try_push(i)) { /* spin until space */ }
    }
  });

  std::uint64_t received = 0;
  std::uint64_t expected = 0;
  bool in_order = true;
  while (received < kN) {
    std::uint64_t v = 0;
    if (ring.try_pop(v)) {
      if (v != expected) in_order = false;
      ++expected;
      ++received;
    }
  }
  producer.join();

  EXPECT_TRUE(in_order);
  EXPECT_EQ(received, kN);
}
