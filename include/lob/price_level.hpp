#pragma once

#include <cstdint>

#include "lob/object_pool.hpp"
#include "lob/types.hpp"

namespace lob {

// Mirrors RestingOrder's `id` and `qty` names so the engine can read the front
// of a level the same way on either backend. `prev`/`next` are list links held
// as pool indices rather than pointers.
struct Order {
  OrderId id{kInvalidOrderId};
  Quantity qty{0};
  Price price{0}; // kept per order so cancel/modify can find the level
  Side side{Side::Buy};
  std::uint32_t prev{kNil};
  std::uint32_t next{kNil};
};

// Intrusive FIFO list of the orders resting at one price. Appending at the tail
// and matching from the head is what gives time priority. `total_qty` is kept
// current so fill-or-kill and depth queries never walk the list.
struct PriceLevel {
  std::uint32_t head{kNil};
  std::uint32_t tail{kNil};
  Quantity total_qty{0};

  [[nodiscard]] bool empty() const noexcept { return head == kNil; }

  void push_back(ObjectPool<Order>& pool, std::uint32_t node) {
    Order& o = pool[node];
    o.prev = tail;
    o.next = kNil;
    if (tail == kNil) {
      head = node;
    } else {
      pool[tail].next = node;
    }
    tail = node;
    total_qty += o.qty;
  }

  void unlink(ObjectPool<Order>& pool, std::uint32_t node) {
    const Order& o = pool[node];
    if (o.prev == kNil) {
      head = o.next;
    } else {
      pool[o.prev].next = o.next;
    }
    if (o.next == kNil) {
      tail = o.prev;
    } else {
      pool[o.next].prev = o.prev;
    }
    total_qty -= o.qty;
  }
};

} // namespace lob
