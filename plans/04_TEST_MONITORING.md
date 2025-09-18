# 🧪 TEST VE MONİTORİNG STRATEJİSİ

**Hedef:** Sürekli performans ölçümü ve regression prevention  
**Süre:** Sürekli (DevOps/CI-CD)  
**Öncelik:** HIGH (Quality Assurance)

## 📋 Test Stratejisi

### 🎯 Fase 1: Automated Performance Testing (1 gün)
**Statü:** 🟡 High Priority

#### 1.1 Continuous Benchmarking Pipeline
```bash
#!/bin/bash
# scripts/performance_ci.sh

set -e

echo "🚀 Starting Performance CI Pipeline..."

# Build optimized version
mkdir -p build_release
cd build_release
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-O3 -march=native -mavx2" ..
make -j$(nproc)

# Run full benchmark suite
echo "📊 Running benchmark suite..."
./bin/order_book_benchmark --benchmark_format=json --benchmark_out=results.json

# Performance regression detection
python3 ../scripts/check_regression.py results.json ../baseline/baseline_results.json

# Generate performance report
python3 ../scripts/generate_report.py results.json > performance_report.html

echo "✅ Performance CI completed successfully"
```

#### 1.2 Regression Detection System
```python
# scripts/check_regression.py
import json
import sys
import statistics

class PerformanceRegression:
    def __init__(self, baseline_file, current_file):
        self.baseline = self.load_results(baseline_file)
        self.current = self.load_results(current_file)
        
    def check_all_benchmarks(self):
        regressions = []
        
        for benchmark_name in self.baseline:
            if benchmark_name not in self.current:
                continue
                
            baseline_time = self.baseline[benchmark_name]['cpu_time']
            current_time = self.current[benchmark_name]['cpu_time']
            
            # Allow 5% variance, flag >10% regression
            regression_pct = ((current_time - baseline_time) / baseline_time) * 100
            
            if regression_pct > 10.0:  # 10% slower = regression
                regressions.append({
                    'benchmark': benchmark_name,
                    'regression_pct': regression_pct,
                    'baseline_ns': baseline_time,
                    'current_ns': current_time
                })
                
        return regressions
    
    def load_results(self, filename):
        with open(filename, 'r') as f:
            data = json.load(f)
            
        results = {}
        for benchmark in data['benchmarks']:
            results[benchmark['name']] = {
                'cpu_time': benchmark['cpu_time'],
                'real_time': benchmark['real_time'],
                'iterations': benchmark['iterations']
            }
        return results

if __name__ == "__main__":
    detector = PerformanceRegression(sys.argv[1], sys.argv[2])
    regressions = detector.check_all_benchmarks()
    
    if regressions:
        print("🚨 PERFORMANCE REGRESSIONS DETECTED!")
        for reg in regressions:
            print(f"  ❌ {reg['benchmark']}: {reg['regression_pct']:.1f}% slower")
            print(f"     {reg['baseline_ns']:.0f}ns → {reg['current_ns']:.0f}ns")
        sys.exit(1)
    else:
        print("✅ No performance regressions detected")
        sys.exit(0)
```

#### 1.3 Memory Leak Detection
```bash
# scripts/memory_check.sh
#!/bin/bash

echo "🧠 Memory leak detection..."

# Valgrind memory check
valgrind --tool=memcheck \
         --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
         --xml=yes \
         --xml-file=valgrind_results.xml \
         ./bin/order_book_benchmark --benchmark_filter="BM_Ultra_OrderBook_Add" --benchmark_min_time=0.1

# Parse valgrind results
python3 scripts/parse_valgrind.py valgrind_results.xml

# AddressSanitizer build
mkdir -p build_asan
cd build_asan
cmake -DCMAKE_CXX_FLAGS="-fsanitize=address -g" ..
make -j$(nproc)
./bin/order_book_benchmark --benchmark_filter="BM_Ultra_OrderBook_Add" --benchmark_min_time=0.1
```

---

### 🎯 Fase 2: Micro-benchmarking Framework (2 gün)
**Statü:** 🟡 High Priority

#### 2.1 Component-Level Performance Tests
```cpp
// tests/micro_benchmarks.cpp
#include <benchmark/benchmark.h>
#include "order_book.hpp"

// Hash table performance isolation
static void BM_HashTable_Insert(benchmark::State& state) {
    UltraHashTable hash_table;
    uint64_t order_id = 1;
    UltraOrder dummy_order;
    
    for (auto _ : state) {
        hash_table.ultra_insert(order_id++, &dummy_order);
        benchmark::DoNotOptimize(hash_table);
    }
    
    state.SetItemsProcessed(state.iterations());
    state.SetBytesProcessed(state.iterations() * sizeof(UltraOrder));
}

static void BM_HashTable_Find(benchmark::State& state) {
    UltraHashTable hash_table;
    UltraOrder dummy_order;
    
    // Pre-populate with test data
    for (uint64_t i = 1; i <= 100000; ++i) {
        hash_table.ultra_insert(i, &dummy_order);
    }
    
    uint64_t search_id = 1;
    for (auto _ : state) {
        UltraOrder* found = hash_table.ultra_find(search_id);
        benchmark::DoNotOptimize(found);
        
        search_id = (search_id % 100000) + 1;  // Cycle through IDs
    }
    
    state.SetItemsProcessed(state.iterations());
}

// Memory pool performance isolation
static void BM_MemoryPool_Acquire(benchmark::State& state) {
    UltraFastOrderPool pool;
    
    for (auto _ : state) {
        UltraOrder* order = pool.ultra_fast_acquire();
        benchmark::DoNotOptimize(order);
        
        // Reset pool periodically to avoid exhaustion
        if ((state.iterations() % 50000) == 0) {
            pool.~UltraFastOrderPool();
            new(&pool) UltraFastOrderPool();
        }
    }
    
    state.SetItemsProcessed(state.iterations());
}

// SIMD vs Scalar comparison
static void BM_SIMD_GetBestBid(benchmark::State& state) {
    UltraOrderBook book;
    
    // Pre-populate with test data
    for (uint32_t price = 50000; price <= 52000; ++price) {
        book.ultra_addOrder(price, 'B', 100, price);
    }
    
    for (auto _ : state) {
        uint32_t best_bid = book.ultra_getBestBid();
        benchmark::DoNotOptimize(best_bid);
    }
    
    state.SetItemsProcessed(state.iterations());
}

static void BM_Scalar_GetBestBid(benchmark::State& state) {
    OptimizedOrderBook book;
    
    // Pre-populate with test data  
    for (uint32_t price = 50000; price <= 52000; ++price) {
        book.addOrder(price, 'B', 100, price);
    }
    
    for (auto _ : state) {
        uint32_t best_bid = book.getBestBid();
        benchmark::DoNotOptimize(best_bid);
    }
    
    state.SetItemsProcessed(state.iterations());
}

// Register micro-benchmarks
BENCHMARK(BM_HashTable_Insert)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_HashTable_Find)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_MemoryPool_Acquire)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_SIMD_GetBestBid)->Unit(benchmark::kNanosecond);
BENCHMARK(BM_Scalar_GetBestBid)->Unit(benchmark::kNanosecond);

BENCHMARK_MAIN();
```

#### 2.2 Stress Testing Framework
```cpp
// tests/stress_test.cpp
#include <thread>
#include <vector>
#include <random>
#include <chrono>

class StressTestRunner {
private:
    static constexpr size_t NUM_THREADS = 4;
    static constexpr size_t ORDERS_PER_THREAD = 1000000;
    
public:
    void run_concurrent_add_test() {
        std::vector<std::thread> threads;
        std::vector<UltraOrderBook> books(NUM_THREADS);
        std::vector<uint64_t> completion_times(NUM_THREADS);
        
        auto start_time = std::chrono::high_resolution_clock::now();
        
        for (size_t i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&, i]() {
                auto thread_start = std::chrono::high_resolution_clock::now();
                
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<uint32_t> price_dist(50000, 52000);
                std::uniform_int_distribution<uint32_t> qty_dist(100, 1000);
                
                for (size_t order = 0; order < ORDERS_PER_THREAD; ++order) {
                    uint64_t order_id = i * ORDERS_PER_THREAD + order;
                    char side = (order % 2 == 0) ? 'B' : 'S';
                    uint32_t price = price_dist(gen);
                    uint32_t qty = qty_dist(gen);
                    
                    books[i].ultra_addOrder(order_id, side, qty, price);
                    
                    // Periodic cleanup to avoid pool exhaustion
                    if (order % 10000 == 0) {
                        books[i].reset_pool();
                    }
                }
                
                auto thread_end = std::chrono::high_resolution_clock::now();
                completion_times[i] = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    thread_end - thread_start).count();
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        auto end_time = std::chrono::high_resolution_clock::now();
        auto total_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            end_time - start_time).count();
            
        // Calculate statistics
        uint64_t total_orders = NUM_THREADS * ORDERS_PER_THREAD;
        double orders_per_second = (total_orders * 1000.0) / total_time;
        
        uint64_t avg_thread_time = 0;
        for (auto time : completion_times) {
            avg_thread_time += time;
        }
        avg_thread_time /= NUM_THREADS;
        
        double avg_latency_ns = static_cast<double>(avg_thread_time) / ORDERS_PER_THREAD;
        
        std::cout << "\n🚀 STRESS TEST RESULTS:" << std::endl;
        std::cout << "  Total Orders: " << total_orders << std::endl;
        std::cout << "  Total Time: " << total_time << "ms" << std::endl;
        std::cout << "  Throughput: " << orders_per_second << " orders/sec" << std::endl;
        std::cout << "  Avg Latency: " << avg_latency_ns << "ns per order" << std::endl;
    }
};
```

---

### 🎯 Fase 3: Real-time Monitoring (3 gün)
**Statü:** 🟠 Medium Priority

#### 3.1 Performance Metrics Collection
```cpp
// include/performance_monitor.hpp
#include <atomic>
#include <chrono>
#include <unordered_map>

class PerformanceMonitor {
private:
    struct MetricData {
        std::atomic<uint64_t> count{0};
        std::atomic<uint64_t> total_time_ns{0};
        std::atomic<uint64_t> min_time_ns{UINT64_MAX};
        std::atomic<uint64_t> max_time_ns{0};
    };
    
    std::unordered_map<std::string, MetricData> metrics_;
    
    class Timer {
    private:
        std::chrono::high_resolution_clock::time_point start_;
        MetricData* metric_;
        
    public:
        Timer(MetricData* metric) : metric_(metric) {
            start_ = std::chrono::high_resolution_clock::now();
        }
        
        ~Timer() {
            auto end = std::chrono::high_resolution_clock::now();
            uint64_t duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                end - start_).count();
                
            metric_->count.fetch_add(1, std::memory_order_relaxed);
            metric_->total_time_ns.fetch_add(duration_ns, std::memory_order_relaxed);
            
            // Update min/max (approximate due to race conditions, but good enough for monitoring)
            uint64_t current_min = metric_->min_time_ns.load(std::memory_order_relaxed);
            while (duration_ns < current_min && 
                   !metric_->min_time_ns.compare_exchange_weak(current_min, duration_ns));
                   
            uint64_t current_max = metric_->max_time_ns.load(std::memory_order_relaxed);
            while (duration_ns > current_max && 
                   !metric_->max_time_ns.compare_exchange_weak(current_max, duration_ns));
        }
    };
    
public:
    Timer time_operation(const std::string& operation_name) {
        return Timer(&metrics_[operation_name]);
    }
    
    void print_statistics() const {
        std::cout << "\n📊 PERFORMANCE STATISTICS:" << std::endl;
        std::cout << std::setw(20) << "Operation" 
                  << std::setw(12) << "Count"
                  << std::setw(12) << "Avg(ns)"
                  << std::setw(12) << "Min(ns)"
                  << std::setw(12) << "Max(ns)" << std::endl;
        std::cout << std::string(68, '-') << std::endl;
        
        for (const auto& [name, metric] : metrics_) {
            uint64_t count = metric.count.load();
            if (count == 0) continue;
            
            uint64_t avg_ns = metric.total_time_ns.load() / count;
            uint64_t min_ns = metric.min_time_ns.load();
            uint64_t max_ns = metric.max_time_ns.load();
            
            std::cout << std::setw(20) << name
                      << std::setw(12) << count  
                      << std::setw(12) << avg_ns
                      << std::setw(12) << min_ns
                      << std::setw(12) << max_ns << std::endl;
        }
    }
    
    void reset_statistics() {
        for (auto& [name, metric] : metrics_) {
            metric.count.store(0);
            metric.total_time_ns.store(0);
            metric.min_time_ns.store(UINT64_MAX);
            metric.max_time_ns.store(0);
        }
    }
};

// Global monitor instance
extern PerformanceMonitor g_perf_monitor;

// Macro for easy timing
#define TIME_OPERATION(name) \
    auto timer = g_perf_monitor.time_operation(name)
```

#### 3.2 Integration with Order Book
```cpp
// Modified UltraOrderBook with monitoring
class MonitoredUltraOrderBook : public UltraOrderBook {
public:
    void ultra_addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price) override {
        TIME_OPERATION("addOrder");
        UltraOrderBook::ultra_addOrder(orderId, side, quantity, price);
    }
    
    void ultra_executeOrder(uint64_t orderId, uint32_t executed_qty) override {
        TIME_OPERATION("executeOrder");
        UltraOrderBook::ultra_executeOrder(orderId, executed_qty);
    }
    
    void ultra_deleteOrder(uint64_t orderId) override {
        TIME_OPERATION("deleteOrder");
        UltraOrderBook::ultra_deleteOrder(orderId);
    }
    
    uint32_t ultra_getBestBid() const override {
        TIME_OPERATION("getBestBid");
        return UltraOrderBook::ultra_getBestBid();
    }
};
```

#### 3.3 Prometheus Integration (Optional)
```cpp
// monitoring/prometheus_exporter.cpp
#include <prometheus/exposer.h>
#include <prometheus/registry.h>
#include <prometheus/counter.h>
#include <prometheus/histogram.h>

class PrometheusMonitor {
private:
    std::unique_ptr<prometheus::Exposer> exposer_;
    std::shared_ptr<prometheus::Registry> registry_;
    
    prometheus::Family<prometheus::Counter>* operation_counter_;
    prometheus::Family<prometheus::Histogram>* latency_histogram_;
    
public:
    PrometheusMonitor(const std::string& bind_address = "127.0.0.1:8080") {
        exposer_ = std::make_unique<prometheus::Exposer>(bind_address);
        registry_ = std::make_shared<prometheus::Registry>();
        
        // Register metrics
        operation_counter_ = &prometheus::BuildCounter()
            .Name("orderbook_operations_total")
            .Help("Total number of order book operations")
            .Register(*registry_);
            
        latency_histogram_ = &prometheus::BuildHistogram()
            .Name("orderbook_operation_duration_ns")
            .Help("Order book operation latency in nanoseconds")
            .Register(*registry_);
            
        exposer_->RegisterCollectable(registry_);
    }
    
    void increment_operation(const std::string& operation) {
        operation_counter_->Add({{"operation", operation}}).Increment();
    }
    
    void observe_latency(const std::string& operation, double latency_ns) {
        latency_histogram_->Add({{"operation", operation}}, 
                               {50, 100, 200, 500, 1000, 2000, 5000}).Observe(latency_ns);
    }
};
```

---

## 📊 Test Metrics and KPIs

### Performance KPIs:
- **Latency P50/P95/P99**: Target <150ns / <300ns / <500ns
- **Throughput**: >1M operations/second
- **Memory Usage**: <500MB steady state
- **CPU Utilization**: <30% on single core
- **Cache Miss Ratio**: <2% L1, <10% L2

### Quality KPIs:
- **Memory Leaks**: 0 bytes leaked over 24h run
- **Crashes**: 0 crashes in stress testing
- **Data Corruption**: 0 order book inconsistencies
- **Performance Regression**: <5% slowdown per change

### CI/CD KPIs:
- **Build Time**: <5 minutes full build
- **Test Execution**: <10 minutes full test suite
- **Coverage**: >90% code coverage
- **Static Analysis**: 0 critical issues

---

## 🔄 Continuous Improvement Process

### Daily:
- [ ] Automated benchmark runs
- [ ] Performance regression checks
- [ ] Memory leak detection

### Weekly:
- [ ] Stress test execution
- [ ] Performance trend analysis
- [ ] Baseline update (if improvements confirmed)

### Monthly:
- [ ] Full architecture performance review
- [ ] Competitor benchmarking
- [ ] Hardware upgrade impact assessment

---

## 📝 Implementation Checklist

- [ ] Set up CI/CD pipeline with performance testing
- [ ] Implement micro-benchmark framework  
- [ ] Add real-time monitoring to order book
- [ ] Create performance regression detection
- [ ] Set up alerting for performance degradation
- [ ] Establish performance baseline and SLAs

**Next Steps**: İlk olarak acil düzeltmeler planını (`01_ACIL_DUZELTMELER.md`) uygulayarak benchmark sorununu çöz!
