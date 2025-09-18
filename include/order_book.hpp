#ifndef ULTRA_OPTIMIZED_ORDER_BOOK_HPP
#define ULTRA_OPTIMIZED_ORDER_BOOK_HPP

#include <atomic>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <unordered_map>
#include <map>
#include <memory>
#include <string>
#include <immintrin.h>
#include <x86intrin.h>
#include <cstdlib>
#include <iostream>

// Forward declarations
struct Order;
struct PriceLevel;
struct FastPriceLevel;
class OrderBook;
class OptimizedOrderBook;
class OrderBookManager;

// =============================================================================
// STANDARD ORDER BOOK STRUCTURES (for compatibility)
// =============================================================================

struct Order {
    uint64_t id;
    char side;
    uint32_t quantity;
    uint32_t price;
    Order* next;
    Order* prev;
};

struct PriceLevel {
    uint32_t quantity;
    Order* head;
    Order* tail;
};

struct FastPriceLevel {
    uint32_t quantity;
    uint16_t order_count;
    uint16_t active;
    Order* first_order;
};

// Standard price level constants (reduced for stack safety)
static constexpr uint32_t MIN_PRICE = 40000;    // $4.00
static constexpr uint32_t MAX_PRICE = 60000;    // $6.00  
static constexpr uint32_t PRICE_LEVELS = MAX_PRICE - MIN_PRICE + 1; // ~20K levels

// Standard OrderBook class
class OrderBook {
protected:
    std::map<uint32_t, PriceLevel> bids_;
    std::map<uint32_t, PriceLevel> asks_;
    std::unordered_map<uint64_t, Order*> orders_;
    
    void add_to_level(Order* order);
    void remove_from_level(Order* order);
    
public:
    void addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price);
    void executeOrder(uint64_t orderId, uint32_t executed_qty);
    void deleteOrder(uint64_t orderId);
    void replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty, uint32_t newPrice);
    void display() const;
};

// Hash table for fast lookups
class FastHashTable {
protected:
    std::unordered_map<uint64_t, Order*> hash_map_;
    
public:
    void insert(uint64_t order_id, Order* order) {
        hash_map_[order_id] = order;
    }
    
    Order* find(uint64_t order_id) {
        auto it = hash_map_.find(order_id);
        return (it != hash_map_.end()) ? it->second : nullptr;
    }
    
    void remove(uint64_t order_id) {
        hash_map_.erase(order_id);
    }
};

// Optimized OrderBook class
class OptimizedOrderBook {
protected:
    FastPriceLevel bid_levels_[PRICE_LEVELS];
    FastPriceLevel ask_levels_[PRICE_LEVELS];
    FastHashTable order_hash_;
    
    uint32_t price_to_index(uint32_t price) const {
        if (price < MIN_PRICE || price > MAX_PRICE) return 0;
        return price - MIN_PRICE;
    }
    
    void add_to_level_fast(Order* order, FastPriceLevel& level);
    void remove_from_level_fast(Order* order, FastPriceLevel& level);
    
public:
    OptimizedOrderBook() {
        memset(bid_levels_, 0, sizeof(bid_levels_));
        memset(ask_levels_, 0, sizeof(ask_levels_));
    }
    
    void addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price);
    void executeOrder(uint64_t orderId, uint32_t executed_qty);
    void deleteOrder(uint64_t orderId);
    void replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty, uint32_t newPrice);
    uint32_t getBestBid() const;
    uint32_t getBestAsk() const;
    void display() const;
};

// OrderBookManager class
class OrderBookManager {
protected:
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;
    std::unordered_map<std::string, std::unique_ptr<OptimizedOrderBook>> optimized_books_;
    
public:
    OrderBook* getOrCreateOrderBook(const std::string& symbol);
    OptimizedOrderBook* getOrCreateOptimizedOrderBook(const std::string& symbol);
    void displayAllBooks() const;
};

// Global instance
extern OrderBookManager g_orderBookManager;

// =============================================================================
// EXTREME OPTIMIZATIONS - 100-200ns Target
// =============================================================================

// Smaller price range for better cache utilization
static constexpr uint32_t ULTRA_MIN_PRICE = 40000; // $4.00
static constexpr uint32_t ULTRA_MAX_PRICE = 60000; // $6.00
static constexpr uint32_t ULTRA_PRICE_LEVELS =
    ULTRA_MAX_PRICE - ULTRA_MIN_PRICE + 1;
static constexpr uint32_t ULTRA_TICK_SIZE = 1; // 0.0001 units

// Much smaller hash table - better cache hit rate
static constexpr uint32_t ULTRA_HASH_SIZE = 65536; // 64K entries
static constexpr uint32_t ULTRA_HASH_MASK = ULTRA_HASH_SIZE - 1;

// =============================================================================
// REGISTER-OPTIMIZED ORDER STRUCTURE
// =============================================================================

// Packed order structure - fits in single cache line
struct alignas(32) UltraOrder {
  uint64_t id_and_side; // Pack ID + side in one 64-bit value
  uint32_t quantity;
  uint32_t price;
  UltraOrder *next; // Only forward pointer - no prev
  uint64_t padding; // Align to 32 bytes

  // Inline accessors
  __attribute__((always_inline)) inline uint64_t get_id() const {
    return id_and_side >> 1;
  }

  __attribute__((always_inline)) inline char get_side() const {
    return (id_and_side & 1) ? 'B' : 'S';
  }

  __attribute__((always_inline)) inline void set_id_side(uint64_t id,
                                                         char side) {
    id_and_side = (id << 1) | (side == 'B' ? 1 : 0);
  }
};

// =============================================================================
// ULTRA-FAST PRICE LEVEL (No linked list overhead)
// =============================================================================

struct alignas(32) UltraPriceLevel {
  uint32_t total_quantity;
  uint16_t order_count;
  uint16_t padding;
  UltraOrder *first_order; // Stack-based allocation
  uint64_t padding2;
};

// =============================================================================
// LOCK-FREE STACK-BASED ORDER POOL
// =============================================================================

class UltraFastOrderPool {
private:
  UltraOrder *pool_{nullptr};
  uint32_t capacity_{0};
  std::atomic<uint32_t> stack_top_{0};

public:
  explicit UltraFastOrderPool(uint32_t capacity = 1000000) : capacity_(capacity) {
    // 64-byte aligned allocation for cache friendliness
    pool_ = static_cast<UltraOrder *>(std::aligned_alloc(64, sizeof(UltraOrder) * capacity_));
    if (!pool_) {
      // Fallback to unaligned new if aligned_alloc is unavailable
      pool_ = reinterpret_cast<UltraOrder *>(::operator new[](sizeof(UltraOrder) * capacity_));
    }
  }

  ~UltraFastOrderPool() {
    if (pool_) {
#if defined(__GLIBC__)
      std::free(pool_);
#else
      ::operator delete[](pool_);
#endif
      pool_ = nullptr;
    }
  }

  __attribute__((always_inline)) inline UltraOrder *ultra_fast_acquire() {
    uint32_t top = stack_top_.load(std::memory_order_relaxed);
    if (top >= capacity_)
      return nullptr;
    UltraOrder *order = &pool_[top];
    stack_top_.store(top + 1, std::memory_order_relaxed);
    return order;
  }

  inline void reset() { stack_top_.store(0, std::memory_order_relaxed); }
};

// =============================================================================
// BRANCHLESS HASH TABLE WITH PERFECT HASHING
// =============================================================================

class UltraHashTable {
private:
  static constexpr uint64_t TOMBSTONE = 0xFFFFFFFFFFFFFFFFULL;
  struct Entry { uint64_t order_id; UltraOrder *order; };
  Entry *entries_{nullptr};

  __attribute__((always_inline)) inline uint32_t
  ultra_hash(uint64_t order_id) const {
    return static_cast<uint32_t>(order_id * 0x9e3779b97f4a7c15ULL >> 32) &
           ULTRA_HASH_MASK;
  }

public:
  UltraHashTable() {
    entries_ = static_cast<Entry *>(std::calloc(ULTRA_HASH_SIZE, sizeof(Entry)));
  }
  ~UltraHashTable() { std::free(entries_); }

  __attribute__((always_inline)) inline void ultra_insert(uint64_t order_id,
                                                          UltraOrder *order) {
    uint32_t h = ultra_hash(order_id);
    int first_tomb = -1;
    for (uint32_t i = 0; i < 64; ++i) { // probe up to 64 slots
      uint32_t idx = (h + i) & ULTRA_HASH_MASK;
      uint64_t k = entries_[idx].order_id;
      if (k == 0) {
        uint32_t target = (first_tomb >= 0) ? static_cast<uint32_t>(first_tomb) : idx;
        entries_[target].order_id = order_id;
        entries_[target].order = order;
        return;
      }
      if (k == TOMBSTONE && first_tomb < 0) first_tomb = static_cast<int>(idx);
      if (k == order_id) {
        entries_[idx].order = order;
        return;
      }
    }
    uint32_t idx = (first_tomb >= 0) ? static_cast<uint32_t>(first_tomb) : (h & ULTRA_HASH_MASK);
    entries_[idx].order_id = order_id;
    entries_[idx].order = order;
  }

  __attribute__((always_inline)) inline UltraOrder *
  ultra_find(uint64_t order_id) const {
    uint32_t h = ultra_hash(order_id);
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t idx = (h + i) & ULTRA_HASH_MASK;
      uint64_t k = entries_[idx].order_id;
      if (k == order_id) return entries_[idx].order;
      if (k == 0) break; // empty slot -> not found
      if (k == TOMBSTONE) continue; // tombstone - keep probing
    }
    return nullptr;
  }

  __attribute__((always_inline)) inline void ultra_remove(uint64_t order_id) {
    uint32_t h = ultra_hash(order_id);
    for (uint32_t i = 0; i < 64; ++i) {
      uint32_t idx = (h + i) & ULTRA_HASH_MASK;
      uint64_t k = entries_[idx].order_id;
      if (k == order_id) {
        entries_[idx].order_id = TOMBSTONE;
        entries_[idx].order = nullptr;
        return;
      }
      if (k == 0) break;
    }
  }
};

// =============================================================================
// ULTRA-OPTIMIZED ORDER BOOK - Target: 100-200ns
// =============================================================================

class UltraOrderBook {
private:
  alignas(64) UltraPriceLevel bid_levels_[ULTRA_PRICE_LEVELS];
  alignas(64) UltraPriceLevel ask_levels_[ULTRA_PRICE_LEVELS];
  UltraHashTable order_hash_;
  UltraFastOrderPool order_pool_;

  // Optimized price to index conversion (simplified for now)
  __attribute__((always_inline)) inline uint32_t
  ultra_price_to_index(uint32_t price) const {
    if (price < ULTRA_MIN_PRICE || price > ULTRA_MAX_PRICE) {
      return 0;
    }
    return price - ULTRA_MIN_PRICE;
  }

  // Ultra-fast level addition with minimal branching
  __attribute__((always_inline)) inline void
  ultra_add_to_level(UltraOrder *order, UltraPriceLevel &level) {
    level.total_quantity += order->quantity;
    level.order_count++;

    // Simple stack-based insertion (LIFO)
    order->next = level.first_order;
    level.first_order = order;
  }

  // Prefetch next likely cache lines
  __attribute__((always_inline)) inline void
  ultra_prefetch(uint32_t price) const {
    uint32_t index = ultra_price_to_index(price);
    __builtin_prefetch(&bid_levels_[index], 1, 3);
    __builtin_prefetch(&ask_levels_[index], 1, 3);
  }

public:
  // Main optimized addOrder function
  __attribute__((always_inline)) inline void ultra_addOrder(uint64_t orderId,
                                                            char side,
                                                            uint32_t quantity,
                                                            uint32_t price) {
    // Early prefetch
    ultra_prefetch(price);

    // Fast duplicate check
    if (__builtin_expect(order_hash_.ultra_find(orderId) != nullptr, 0)) {
      return; // Order exists
    }

    // Fast order allocation
    UltraOrder *order = order_pool_.ultra_fast_acquire();
    if (__builtin_expect(!order, 0))
      return; // Pool exhausted

    // Pack order data efficiently
    order->set_id_side(orderId, side);
    order->quantity = quantity;
    order->price = price;
    order->next = nullptr;

    // Branchless level selection
    uint32_t index = ultra_price_to_index(price);
    bool is_buy = (side == 'B');
    UltraPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;

    // Add to level and hash
    ultra_add_to_level(order, levels[index]);
    order_hash_.ultra_insert(orderId, order);
  }

  // Simplified execute - remove complexity
  __attribute__((always_inline)) inline void
  ultra_executeOrder(uint64_t orderId, uint32_t executed_qty) {
    UltraOrder *order = order_hash_.ultra_find(orderId);
    if (!order)
      return;

    uint32_t index = ultra_price_to_index(order->price);
    bool is_buy = (order->get_side() == 'B');
    UltraPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;

    uint32_t qty_to_remove = (executed_qty < order->quantity) ? executed_qty : order->quantity;
    levels[index].total_quantity -= qty_to_remove;
    order->quantity -= qty_to_remove;

    // Simple deletion - no complex list manipulation
    if (order->quantity == 0) {
      levels[index].order_count--;
    }
  }

  // Minimal delete operation
  __attribute__((always_inline)) inline void
  ultra_deleteOrder(uint64_t orderId) {
    UltraOrder *order = order_hash_.ultra_find(orderId);
    if (!order)
      return;

    uint32_t index = ultra_price_to_index(order->price);
    bool is_buy = (order->get_side() == 'B');
    UltraPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;

    levels[index].total_quantity -= order->quantity;
    levels[index].order_count--;
    // Mark as deleted
    order->quantity = 0;
    // Remove from hash
    order_hash_.ultra_remove(orderId);
  }

  // Replace operation (delete + add)
  __attribute__((always_inline)) inline void
  ultra_replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty, uint32_t newPrice) {
    UltraOrder *oldOrder = order_hash_.ultra_find(oldId);
    if (!oldOrder) return;
    char side = oldOrder->get_side();

    // Adjust level and remove old mapping
    uint32_t idx = ultra_price_to_index(oldOrder->price);
    UltraPriceLevel *levels = (side == 'B') ? bid_levels_ : ask_levels_;
    levels[idx].total_quantity -= oldOrder->quantity;
    levels[idx].order_count--;
    oldOrder->quantity = 0;
    order_hash_.ultra_remove(oldId);

    // Insert new order
    ultra_addOrder(newId, side, newQty, newPrice);
  }

  // Simplified best price finding (safe version for MVP)
  __attribute__((always_inline)) inline uint32_t ultra_getBestBid() const {
    // Find highest bid price (iterate from high to low)
    for (int i = ULTRA_PRICE_LEVELS - 1; i >= 0; --i) {
      if (bid_levels_[i].total_quantity > 0) {
        return ULTRA_MIN_PRICE + i;
      }
    }
    return 0;
  }

  // Simplified best ask finding (safe version for MVP)
  __attribute__((always_inline)) inline uint32_t ultra_getBestAsk() const {
    // Find lowest ask price (iterate from low to high)
    for (uint32_t i = 0; i < ULTRA_PRICE_LEVELS; ++i) {
      if (ask_levels_[i].total_quantity > 0) {
        return ULTRA_MIN_PRICE + i;
      }
    }
    return 0;
  }

  // Reset pool for continuous operation
  inline void reset_pool() {
    order_pool_.reset();
    memset(bid_levels_, 0, sizeof(bid_levels_));
    memset(ask_levels_, 0, sizeof(ask_levels_));
  }

  // Simple display method for debugging/validation
  void display() const {
    std::cout << "--- ULTRA ORDER BOOK ---" << std::endl;
    
    // Find best bid and ask
    uint32_t best_bid = 0, best_ask = 0;
    for (int i = ULTRA_PRICE_LEVELS - 1; i >= 0; i--) {
      if (bid_levels_[i].total_quantity > 0 && best_bid == 0) {
        best_bid = ULTRA_MIN_PRICE + i;
        break;
      }
    }
    for (int i = 0; i < ULTRA_PRICE_LEVELS; i++) {
      if (ask_levels_[i].total_quantity > 0 && best_ask == 0) {
        best_ask = ULTRA_MIN_PRICE + i;
        break;
      }
    }
    
    std::cout << "Best Bid: " << (best_bid / 10000.0) << std::endl;
    std::cout << "Best Ask: " << (best_ask / 10000.0) << std::endl;
    std::cout << std::endl;
    
    // Show top bids (highest first)
    std::cout << "--- BIDS ---         QTY | PRICE" << std::endl;
    int bid_count = 0;
    for (int i = ULTRA_PRICE_LEVELS - 1; i >= 0 && bid_count < 10; i--) {
      if (bid_levels_[i].total_quantity > 0) {
        uint32_t price = ULTRA_MIN_PRICE + i;
        std::cout << "        " << bid_levels_[i].total_quantity << " | " << (price / 10000.0) << std::endl;
        bid_count++;
      }
    }
    
    // Show top asks (lowest first)
    std::cout << std::endl << "--- ASKS ---         QTY | PRICE" << std::endl;
    int ask_count = 0;
    for (int i = 0; i < ULTRA_PRICE_LEVELS && ask_count < 10; i++) {
      if (ask_levels_[i].total_quantity > 0) {
        uint32_t price = ULTRA_MIN_PRICE + i;
        std::cout << "        " << ask_levels_[i].total_quantity << " | " << (price / 10000.0) << std::endl;
        ask_count++;
      }
    }
    std::cout << "------------------" << std::endl;
  }
};

// =============================================================================
// TEMPLATE-BASED COMPILE-TIME OPTIMIZATION
// =============================================================================

template <char Side> class TemplatedUltraOrderBook {
private:
  UltraOrderBook book_;

public:
  __attribute__((always_inline)) inline void
  addOrder(uint64_t orderId, uint32_t quantity, uint32_t price) {
    book_.ultra_addOrder(orderId, Side, quantity, price);
  }
};

using UltraBuyOrderBook = TemplatedUltraOrderBook<'B'>;
using UltraSellOrderBook = TemplatedUltraOrderBook<'S'>;

#endif // ULTRA_OPTIMIZED_ORDER_BOOK_HPP

// =============================================================================
// USAGE EXAMPLE AND BENCHMARK
// =============================================================================

/*
// benchmark.cpp'ye ekle:

static void BM_Ultra_OrderBook_Add(benchmark::State& state) {
    UltraOrderBook book;
    uint64_t orderId = 1;
    uint32_t price = 50000;

    for (auto _ : state) {
        book.ultra_addOrder(orderId++, 'B', 100, price);

        // Reset every 10000 orders to avoid pool exhaustion
        if ((orderId % 10000) == 0) {
            book.reset_pool();
            orderId = 1;
        }

        benchmark::DoNotOptimize(book);
    }

    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(BM_Ultra_OrderBook_Add)->Unit(benchmark::kNanosecond);

Expected result: ~100-150ns per operation
*/
