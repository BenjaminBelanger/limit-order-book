#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace lob {

// Sentinel "null" index used by intrusive lists and the order pool.
inline constexpr std::uint32_t kNil = 0xFFFFFFFFu;

// Fixed-capacity object pool with an O(1) free list.
//
// All storage is allocated once at construction; allocate()/deallocate() never
// touch the heap, so the matching hot path performs zero dynamic allocation.
// Handles are 32-bit indices into the backing array rather than pointers, which
// keeps them small and cache-dense and makes them stable across reuse.
template <class T>
class ObjectPool {
public:
  explicit ObjectPool(std::size_t capacity) {
    slots_.resize(capacity);
    free_.reserve(capacity);
    // Push in reverse so the first allocations hand out low indices (nicer for
    // cache locality and easier to reason about in tests).
    for (std::size_t i = capacity; i-- > 0;) {
      free_.push_back(static_cast<std::uint32_t>(i));
    }
  }

  [[nodiscard]] std::uint32_t allocate() {
    const std::uint32_t idx = free_.back();
    free_.pop_back();
    return idx;
  }

  void deallocate(std::uint32_t idx) { free_.push_back(idx); }

  [[nodiscard]] T& operator[](std::uint32_t idx) { return slots_[idx]; }
  [[nodiscard]] const T& operator[](std::uint32_t idx) const {
    return slots_[idx];
  }

  [[nodiscard]] bool full() const noexcept { return free_.empty(); }
  [[nodiscard]] std::size_t capacity() const noexcept { return slots_.size(); }
  [[nodiscard]] std::size_t in_use() const noexcept {
    return slots_.size() - free_.size();
  }

private:
  std::vector<T> slots_;
  std::vector<std::uint32_t> free_;
};

} // namespace lob
