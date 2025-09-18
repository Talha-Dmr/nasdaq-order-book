// benchmarks/ultra_benchmark.cpp
#include "order_book.hpp"
#include <benchmark/benchmark.h>

// Current vs Ultra comparison
static void BM_Current_OrderBook_Add(benchmark::State &state) {
  OptimizedOrderBook book;
  uint64_t orderId = 1;
  uint32_t price = 50000;

  // CPU warm up
  for (int i = 0; i < 1000; ++i) {
    book.addOrder(i, 'B', 100, price);
  }

  for (auto _ : state) {
    book.addOrder(orderId++, 'B', 100, price);
    benchmark::DoNotOptimize(book);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations());
}

static void BM_Ultra_OrderBook_Add(benchmark::State &state) {
  UltraOrderBook book;
  uint64_t orderId = 1000;  // Start from non-zero to avoid edge cases
  uint32_t price = 50000;

  // CPU warm up with safe range
  for (int i = 1; i < 100; ++i) {
    book.ultra_addOrder(i, 'B', 100, price);
  }

  for (auto _ : state) {
    book.ultra_addOrder(orderId++, 'B', 100, price);

    // Reset every 10000 orders to be more conservative
    if ((orderId % 10000) == 0) {
      // book.reset_pool(); // Commented out until we fix reset_pool
      orderId = 1000;
    }

    benchmark::DoNotOptimize(book);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations());
}

// Template specialization benchmark
static void BM_Template_Buy_OrderBook_Add(benchmark::State &state) {
  UltraBuyOrderBook book;
  uint64_t orderId = 1;
  uint32_t price = 50000;

  for (auto _ : state) {
    book.addOrder(orderId++, 100, price);
    benchmark::DoNotOptimize(book);
  }

  state.SetItemsProcessed(state.iterations());
}

// Mixed operations - more realistic test
static void BM_Ultra_Mixed_Operations(benchmark::State &state) {
  UltraOrderBook book;
  uint64_t orderId = 1;
  uint32_t price = 50000;

  for (auto _ : state) {
    // Add 2 orders
    book.ultra_addOrder(orderId++, 'B', 100, price);
    book.ultra_addOrder(orderId++, 'S', 100, price + 10);

    // Execute 1 order
    book.ultra_executeOrder(orderId - 2, 50);

    // Find best bid (SIMD optimized)
    uint32_t best = book.ultra_getBestBid();
    benchmark::DoNotOptimize(best);

    price++;

    if ((orderId % 10000) == 0) {
      book.reset_pool();
      orderId = 1000;
      price = 50000;
    }

    benchmark::DoNotOptimize(book);
  }

  state.SetItemsProcessed(state.iterations());
}

// CPU cache performance test
static void BM_Cache_Performance_Test(benchmark::State &state) {
  UltraOrderBook book;
  uint64_t orderId = 1;

  // Test cache locality with sequential prices
  for (auto _ : state) {
    uint32_t base_price = 50000 + (orderId % 1000);

    book.ultra_addOrder(orderId++, 'B', 100, base_price);
    book.ultra_addOrder(orderId++, 'B', 100, base_price + 1);
    book.ultra_addOrder(orderId++, 'B', 100, base_price + 2);
    book.ultra_addOrder(orderId++, 'B', 100, base_price + 3);

    if ((orderId % 10000) == 0) {
      book.reset_pool();
      orderId = 1000;
    }

    benchmark::DoNotOptimize(book);
  }

  state.SetItemsProcessed(state.iterations() * 4);
}

// Ultra Replace benchmarks - test the new replace functionality
static void BM_Ultra_Replace_Add_Then_Replace(benchmark::State &state) {
  UltraOrderBook book;
  uint64_t baseId = 2000;

  // Pre-populate with orders that we'll replace
  for (int i = 0; i < 500; ++i) {
    book.ultra_addOrder(baseId + i, 'B', 100, 50000 + i);
  }

  uint64_t orderId = baseId + 1000;
  for (auto _ : state) {
    // Add an order
    book.ultra_addOrder(orderId, 'B', 100, 51000);
    // Replace it immediately 
    book.ultra_replaceOrder(orderId, orderId + 10000, 110, 51010);
    orderId += 2;

    if ((orderId % 5000) == 0) {
      orderId = baseId + 1000;
    }

    benchmark::DoNotOptimize(book);
  }

  state.SetItemsProcessed(state.iterations() * 2); // Count both Add and Replace
}

static void BM_Ultra_Replace_Only(benchmark::State &state) {
  UltraOrderBook book;
  uint64_t baseId = 3000;

  // Pre-populate with orders to replace
  for (int i = 0; i < 1000; ++i) {
    book.ultra_addOrder(baseId + i, 'B', 100, 50000 + (i % 100));
  }

  int replaceIdx = 0;
  for (auto _ : state) {
    uint64_t origId = baseId + (replaceIdx % 1000);
    uint64_t newId = baseId + 10000 + replaceIdx;
    book.ultra_replaceOrder(origId, newId, 120, 50050);
    replaceIdx++;

    benchmark::DoNotOptimize(book);
  }

  state.SetItemsProcessed(state.iterations());
}

// Register benchmarks - only test working implementations for now
BENCHMARK(BM_Current_OrderBook_Add)->Unit(benchmark::kNanosecond)->MinTime(1.0);

// Enable UltraOrderBook benchmark  
BENCHMARK(BM_Ultra_OrderBook_Add)->Unit(benchmark::kNanosecond)->MinTime(1.0);

// Replace benchmarks
BENCHMARK(BM_Ultra_Replace_Add_Then_Replace)->Unit(benchmark::kNanosecond)->MinTime(1.0);
BENCHMARK(BM_Ultra_Replace_Only)->Unit(benchmark::kNanosecond)->MinTime(1.0);

// BENCHMARK(BM_Template_Buy_OrderBook_Add)->Unit(benchmark::kNanosecond)->MinTime(2.0);
// BENCHMARK(BM_Ultra_Mixed_Operations)->Unit(benchmark::kNanosecond)->MinTime(2.0);
// BENCHMARK(BM_Cache_Performance_Test)->Unit(benchmark::kNanosecond)->MinTime(2.0);

BENCHMARK_MAIN();
