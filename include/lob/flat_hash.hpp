#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {

// Open-addressing hash map specialised for OrderId -> 32-bit value (typically a
// pool slot index). Linear probing with backward-shift deletion means there are
// no tombstones and no per-element node allocation. The table is sized once to
// keep the load factor below ~0.5, so lookups stay close to O(1) and the hot
// path never rehashes or allocates.
//
// OrderId 0 (kInvalidOrderId) is reserved as the empty-slot marker.
class FlatHashIndex {
public:
  explicit FlatHashIndex(std::size_t expected) {
    std::size_t cap = 16;
    while (cap < expected * 2) cap <<= 1; // power of two, load factor < 0.5
    slots_.assign(cap, Slot{});
    mask_ = cap - 1;
  }

  void insert(OrderId key, std::uint32_t value) {
    std::size_t i = hash(key) & mask_;
    while (slots_[i].key != kInvalidOrderId) {
      if (slots_[i].key == key) { // overwrite existing
        slots_[i].value = value;
        return;
      }
      i = (i + 1) & mask_;
    }
    slots_[i] = Slot{key, value};
    ++size_;
  }

  [[nodiscard]] const std::uint32_t* find(OrderId key) const {
    std::size_t i = hash(key) & mask_;
    while (slots_[i].key != kInvalidOrderId) {
      if (slots_[i].key == key) return &slots_[i].value;
      i = (i + 1) & mask_;
    }
    return nullptr;
  }

  [[nodiscard]] bool contains(OrderId key) const { return find(key) != nullptr; }

  bool erase(OrderId key) {
    std::size_t i = hash(key) & mask_;
    while (slots_[i].key != kInvalidOrderId && slots_[i].key != key) {
      i = (i + 1) & mask_;
    }
    if (slots_[i].key == kInvalidOrderId) return false;

    // Backward-shift deletion (Knuth 6.4 Algorithm R) to avoid tombstones.
    std::size_t j = i;
    while (true) {
      j = (j + 1) & mask_;
      if (slots_[j].key == kInvalidOrderId) break;
      const std::size_t k = hash(slots_[j].key) & mask_;
      // Skip entries that are still in their probe range relative to the hole.
      const bool in_range = (i <= j) ? (i < k && k <= j) : (i < k || k <= j);
      if (in_range) continue;
      slots_[i] = slots_[j];
      i = j;
    }
    slots_[i] = Slot{};
    --size_;
    return true;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }

private:
  struct Slot {
    OrderId key{kInvalidOrderId};
    std::uint32_t value{0};
  };

  // Fibonacci hashing: cheap, good avalanche for sequential ids.
  static std::size_t hash(OrderId key) noexcept {
    return static_cast<std::size_t>(key * 0x9E3779B97F4A7C15ull);
  }

  std::vector<Slot> slots_;
  std::size_t mask_{0};
  std::size_t size_{0};
};

} // namespace lob
