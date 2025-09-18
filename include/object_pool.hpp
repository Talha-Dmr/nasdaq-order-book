#ifndef OPTIMIZED_OBJECT_POOL_HPP
#define OPTIMIZED_OBJECT_POOL_HPP

#include <atomic>
#include <cstddef>
#include <immintrin.h>

template <typename T> class OptimizedObjectPool {
private:
  // Cache-aligned blocks to reduce false sharing
  struct alignas(64) PoolBlock {
    T objects[64]; // 64 objects per cache-aligned block
    std::atomic<uint64_t> free_mask{~0ULL}; // All initially free
  };

  alignas(64) PoolBlock *blocks_;
  size_t num_blocks_;
  std::atomic<size_t> current_block_{0};

public:
  OptimizedObjectPool(size_t total_objects) {
    num_blocks_ = (total_objects + 63) / 64; // Round up to blocks

    // Allocate cache-aligned memory
    blocks_ = static_cast<PoolBlock *>(
        aligned_alloc(64, num_blocks_ * sizeof(PoolBlock)));

    // Initialize all blocks
    for (size_t i = 0; i < num_blocks_; ++i) {
      blocks_[i].free_mask.store(~0ULL, std::memory_order_relaxed);
    }
  }

  ~OptimizedObjectPool() { free(blocks_); }

  __attribute__((always_inline)) inline T *acquire() {
    size_t start_block = current_block_.load(std::memory_order_relaxed);

    for (size_t attempts = 0; attempts < num_blocks_; ++attempts) {
      size_t block_idx = (start_block + attempts) % num_blocks_;
      PoolBlock &block = blocks_[block_idx];

      uint64_t mask = block.free_mask.load(std::memory_order_acquire);

      while (mask != 0) {
        // Find first free slot using builtin
        int slot = __builtin_ctzll(mask); // Count trailing zeros
        uint64_t slot_bit = 1ULL << slot;

        // Try to claim this slot atomically
        uint64_t expected = mask;
        uint64_t desired = mask & ~slot_bit;

        if (block.free_mask.compare_exchange_weak(expected, desired,
                                                  std::memory_order_acq_rel,
                                                  std::memory_order_acquire)) {

          // Successfully claimed slot
          current_block_.store(block_idx, std::memory_order_relaxed);
          return &block.objects[slot];
        }

        // Reload mask and retry
        mask = expected;
      }
    }

    // Pool exhausted - should handle this gracefully
    return nullptr;
  }

  __attribute__((always_inline)) inline void release(T *object) {
    // Find which block this object belongs to
    for (size_t block_idx = 0; block_idx < num_blocks_; ++block_idx) {
      PoolBlock &block = blocks_[block_idx];

      if (object >= &block.objects[0] && object < &block.objects[64]) {
        size_t slot = object - &block.objects[0];
        uint64_t slot_bit = 1ULL << slot;

        // Mark slot as free
        block.free_mask.fetch_or(slot_bit, std::memory_order_acq_rel);
        return;
      }
    }
  }

  // Batch allocation for better cache utilization
  __attribute__((always_inline)) inline size_t batch_acquire(T **objects,
                                                             size_t count) {
    size_t acquired = 0;

    while (acquired < count) {
      T *obj = acquire();
      if (!obj)
        break;
      objects[acquired++] = obj;
    }

    return acquired;
  }
};

#endif // OPTIMIZED_OBJECT_POOL_HPP
