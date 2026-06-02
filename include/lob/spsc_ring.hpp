#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace lob {

// Bounded lock-free single-producer / single-consumer ring buffer.
//
// Exactly one thread may call try_push and exactly one (different) thread may
// call try_pop. Correctness relies on acquire/release ordering between the
// producer's tail store and the consumer's head store; no locks or CAS loops
// are used.
//
// Two optimisations matter for throughput:
//   * head_ and tail_ live on separate cache lines (alignas) so the producer
//     and consumer never invalidate each other's line (no false sharing).
//   * each side caches the other index (head_cache_/tail_cache_) so the common
//     case avoids loading the contended atomic.
//
// Capacity is rounded up to a power of two so index wrapping is a cheap mask.
template <class T>
class SpscRing {
public:
  explicit SpscRing(std::size_t min_capacity) {
    std::size_t cap = 2;
    while (cap < min_capacity) cap <<= 1;
    capacity_ = cap;
    mask_ = cap - 1;
    buffer_.resize(cap);
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  // Producer side. Returns false if the ring is full.
  bool try_push(const T& value) {
    const std::size_t t = tail_.load(std::memory_order_relaxed);
    if (t - head_cache_ == capacity_) {
      head_cache_ = head_.load(std::memory_order_acquire);
      if (t - head_cache_ == capacity_) return false; // genuinely full
    }
    buffer_[t & mask_] = value;
    tail_.store(t + 1, std::memory_order_release);
    return true;
  }

  // Consumer side. Returns false if the ring is empty.
  bool try_pop(T& out) {
    const std::size_t h = head_.load(std::memory_order_relaxed);
    if (h == tail_cache_) {
      tail_cache_ = tail_.load(std::memory_order_acquire);
      if (h == tail_cache_) return false; // genuinely empty
    }
    out = buffer_[h & mask_];
    head_.store(h + 1, std::memory_order_release);
    return true;
  }

  [[nodiscard]] bool empty() const {
    return head_.load(std::memory_order_acquire) ==
           tail_.load(std::memory_order_acquire);
  }

private:
  static constexpr std::size_t kCacheLine = 64;

  std::vector<T> buffer_;
  std::size_t capacity_{0};
  std::size_t mask_{0};

  alignas(kCacheLine) std::atomic<std::size_t> head_{0}; // consumer writes
  std::size_t tail_cache_{0};                            // consumer-local
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0}; // producer writes
  std::size_t head_cache_{0};                            // producer-local
};

} // namespace lob
