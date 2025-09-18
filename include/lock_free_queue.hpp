#ifndef LOCK_FREE_QUEUE_HPP
#define LOCK_FREE_QUEUE_HPP

#include <atomic>
#include <cstddef>
#include <vector>

template <typename T> class LockFreeQueue {
public:
  LockFreeQueue(size_t capacity) : buffer_(capacity), head_(0), tail_(0) {}

  bool push(const T &value) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = (tail + 1) % buffer_.size();
    if (next_tail == head_.load(std::memory_order_acquire)) {
      return false; // Full
    }
    buffer_[tail] = value;
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  bool pop(T &value) {
    size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return false; // Empty
    }
    value = buffer_[head];
    head_.store((head + 1) % buffer_.size(), std::memory_order_release);
    return true;
  }

private:
  std::vector<T> buffer_;
  std::atomic<size_t> head_;
  std::atomic<size_t> tail_;
};

#endif // LOCK_FREE_QUEUE_HPP
