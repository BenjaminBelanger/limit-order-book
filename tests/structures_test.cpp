#include <gtest/gtest.h>

#include <unordered_set>
#include <vector>

#include "lob/flat_hash.hpp"
#include "lob/object_pool.hpp"
#include "lob/price_level.hpp"

using namespace lob;

// ---- ObjectPool --------------------------------------------------------------
TEST(ObjectPool, AllocateDeallocateReuse) {
  ObjectPool<Order> pool(4);
  EXPECT_EQ(pool.capacity(), 4u);
  EXPECT_EQ(pool.in_use(), 0u);

  const std::uint32_t a = pool.allocate();
  const std::uint32_t b = pool.allocate();
  EXPECT_NE(a, b);
  EXPECT_EQ(pool.in_use(), 2u);

  pool.deallocate(a);
  EXPECT_EQ(pool.in_use(), 1u);
  const std::uint32_t c = pool.allocate(); // should reuse the freed slot
  EXPECT_EQ(c, a);
}

TEST(ObjectPool, FullWhenExhausted) {
  ObjectPool<Order> pool(2);
  (void)pool.allocate();
  (void)pool.allocate();
  EXPECT_TRUE(pool.full());
}

// ---- FlatHashIndex -----------------------------------------------------------
TEST(FlatHashIndex, InsertFindErase) {
  FlatHashIndex idx(8);
  idx.insert(1, 100);
  idx.insert(2, 200);
  idx.insert(3, 300);

  ASSERT_NE(idx.find(2), nullptr);
  EXPECT_EQ(*idx.find(2), 200u);
  EXPECT_EQ(idx.find(99), nullptr);
  EXPECT_EQ(idx.size(), 3u);

  EXPECT_TRUE(idx.erase(2));
  EXPECT_EQ(idx.find(2), nullptr);
  EXPECT_FALSE(idx.erase(2));
  // Remaining keys still resolve after backward-shift deletion.
  ASSERT_NE(idx.find(1), nullptr);
  ASSERT_NE(idx.find(3), nullptr);
  EXPECT_EQ(*idx.find(3), 300u);
}

TEST(FlatHashIndex, OverwriteUpdatesValue) {
  FlatHashIndex idx(8);
  idx.insert(7, 1);
  idx.insert(7, 2);
  EXPECT_EQ(idx.size(), 1u);
  EXPECT_EQ(*idx.find(7), 2u);
}

TEST(FlatHashIndex, StressManyKeysWithCollisions) {
  FlatHashIndex idx(1024);
  std::unordered_set<OrderId> live;
  for (OrderId k = 1; k <= 500; ++k) {
    idx.insert(k, static_cast<std::uint32_t>(k));
    live.insert(k);
  }
  // Erase evens, ensure odds remain findable with correct values.
  for (OrderId k = 2; k <= 500; k += 2) {
    EXPECT_TRUE(idx.erase(k));
    live.erase(k);
  }
  for (OrderId k = 1; k <= 500; ++k) {
    const auto* v = idx.find(k);
    if (live.count(k)) {
      ASSERT_NE(v, nullptr) << "missing key " << k;
      EXPECT_EQ(*v, static_cast<std::uint32_t>(k));
    } else {
      EXPECT_EQ(v, nullptr) << "stale key " << k;
    }
  }
  EXPECT_EQ(idx.size(), live.size());
}

// ---- PriceLevel (intrusive FIFO) --------------------------------------------
TEST(PriceLevel, FifoOrderAndQuantity) {
  ObjectPool<Order> pool(8);
  PriceLevel level;
  EXPECT_TRUE(level.empty());

  std::vector<std::uint32_t> nodes;
  for (Quantity q : {Quantity{10}, Quantity{20}, Quantity{30}}) {
    const std::uint32_t n = pool.allocate();
    pool[n].id = static_cast<OrderId>(q);
    pool[n].qty = q;
    level.push_back(pool, n);
    nodes.push_back(n);
  }
  EXPECT_EQ(level.total_qty, 60u);
  EXPECT_FALSE(level.empty());
  // Head is the first inserted (FIFO).
  EXPECT_EQ(pool[level.head].qty, 10u);
  EXPECT_EQ(pool[level.tail].qty, 30u);
}

TEST(PriceLevel, UnlinkMiddleKeepsList) {
  ObjectPool<Order> pool(8);
  PriceLevel level;
  std::uint32_t n[3];
  for (int i = 0; i < 3; ++i) {
    n[i] = pool.allocate();
    pool[n[i]].id = static_cast<OrderId>(i + 1);
    pool[n[i]].qty = 10;
    level.push_back(pool, n[i]);
  }
  level.unlink(pool, n[1]); // remove the middle node
  EXPECT_EQ(level.total_qty, 20u);
  EXPECT_EQ(level.head, n[0]);
  EXPECT_EQ(pool[level.head].next, n[2]);
  EXPECT_EQ(pool[n[2]].prev, n[0]);
  EXPECT_EQ(level.tail, n[2]);
}

TEST(PriceLevel, UnlinkHeadAndTail) {
  ObjectPool<Order> pool(8);
  PriceLevel level;
  std::uint32_t n[2];
  for (int i = 0; i < 2; ++i) {
    n[i] = pool.allocate();
    pool[n[i]].qty = 5;
    level.push_back(pool, n[i]);
  }
  level.unlink(pool, n[0]); // head
  EXPECT_EQ(level.head, n[1]);
  EXPECT_EQ(pool[n[1]].prev, kNil);
  level.unlink(pool, n[1]); // tail (now only element)
  EXPECT_TRUE(level.empty());
  EXPECT_EQ(level.total_qty, 0u);
}
