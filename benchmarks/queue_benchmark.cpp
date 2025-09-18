#include <atomic>
#include <benchmark/benchmark.h>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// --- Rakip 1: Standart Mutex Korumalı Kuyruk ---
std::queue<int> g_mutex_queue;
std::mutex g_mutex;

static void BM_Mutex_Queue(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    // Kuyruğu her testten önce temizle
    std::queue<int> empty;
    std::swap(g_mutex_queue, empty);
    state.ResumeTiming();

    std::thread producer([&] {
      for (int i = 0; i < state.range(0); ++i) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_mutex_queue.push(i);
      }
    });

    std::thread consumer([&] {
      int val;
      for (int i = 0; i < state.range(0); ++i) {
        while (true) {
          std::lock_guard<std::mutex> lock(g_mutex);
          if (!g_mutex_queue.empty()) {
            val = g_mutex_queue.front();
            g_mutex_queue.pop();
            break;
          }
        }
      }
    });
    producer.join();
    consumer.join();
  }
}
BENCHMARK(BM_Mutex_Queue)->Arg(10000); // 10,000 eleman ile test et

// --- Rakip 2: Basit bir Lock-Free Kuyruk (SPSC - Tek Üretici, Tek Tüketici)
// ---
template <typename T> class LockFreeQueue {
public:
  LockFreeQueue(size_t capacity) : buffer_(capacity), head_(0), tail_(0) {}

  bool push(const T &value) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t next_tail = (tail + 1) % buffer_.size();
    if (next_tail == head_.load(std::memory_order_acquire)) {
      return false; // Kuyruk dolu
    }
    buffer_[tail] = value;
    tail_.store(next_tail, std::memory_order_release);
    return true;
  }

  bool pop(T &value) {
    size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return false; // Kuyruk boş
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

static void BM_LockFree_Queue(benchmark::State &state) {
  for (auto _ : state) {
    state.PauseTiming();
    LockFreeQueue<int> queue(state.range(0) + 1);
    state.ResumeTiming();

    std::thread producer([&] {
      for (int i = 0; i < state.range(0); ++i) {
        while (!queue.push(i))
          ;
      }
    });

    std::thread consumer([&] {
      int val;
      for (int i = 0; i < state.range(0); ++i) {
        while (!queue.pop(val))
          ;
      }
    });
    producer.join();
    consumer.join();
  }
}
BENCHMARK(BM_LockFree_Queue)->Arg(10000);

BENCHMARK_MAIN();
