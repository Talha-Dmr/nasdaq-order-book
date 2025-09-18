#ifndef PERF_LATENCY_TRACKER_HPP
#define PERF_LATENCY_TRACKER_HPP

#include <chrono>
#include <vector>
#include <algorithm>
#include <iostream>

namespace perf {

using TimePoint = std::chrono::time_point<std::chrono::high_resolution_clock>;
using Duration = std::chrono::nanoseconds;

class LatencyTracker {
private:
    std::vector<uint64_t> samples_;
    size_t capacity_;
    size_t count_{0};
    
public:
    explicit LatencyTracker(size_t capacity = 10000) 
        : capacity_(capacity) {
        samples_.reserve(capacity_);
    }
    
    void record(uint64_t latency_ns) {
        if (samples_.size() < capacity_) {
            samples_.push_back(latency_ns);
        } else {
            samples_[count_ % capacity_] = latency_ns;
        }
        count_++;
    }
    
    struct Stats {
        uint64_t min_ns{0};
        uint64_t max_ns{0};
        double avg_ns{0.0};
        uint64_t p50_ns{0};
        uint64_t p95_ns{0};
        uint64_t p99_ns{0};
        size_t count{0};
    };
    
    Stats get_stats() const {
        if (samples_.empty()) return {};
        
        std::vector<uint64_t> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        
        Stats stats;
        stats.count = samples_.size();
        stats.min_ns = sorted.front();
        stats.max_ns = sorted.back();
        
        uint64_t total = 0;
        for (auto ns : sorted) total += ns;
        stats.avg_ns = static_cast<double>(total) / sorted.size();
        
        stats.p50_ns = sorted[sorted.size() * 50 / 100];
        stats.p95_ns = sorted[sorted.size() * 95 / 100];  
        stats.p99_ns = sorted[sorted.size() * 99 / 100];
        
        return stats;
    }
    
    void print_stats(const std::string& name) const {
        auto stats = get_stats();
        if (stats.count == 0) {
            std::cout << name << ": No samples" << std::endl;
            return;
        }
        
        std::cout << "=== " << name << " LATENCY STATS ===\n"
                  << "Samples: " << stats.count << "\n"
                  << "Min:     " << stats.min_ns << " ns\n"
                  << "Avg:     " << static_cast<int>(stats.avg_ns) << " ns\n" 
                  << "P50:     " << stats.p50_ns << " ns\n"
                  << "P95:     " << stats.p95_ns << " ns\n"
                  << "P99:     " << stats.p99_ns << " ns\n"
                  << "Max:     " << stats.max_ns << " ns\n" << std::endl;
    }
    
    void reset() {
        samples_.clear();
        count_ = 0;
    }
};

// High-resolution timer utilities
inline TimePoint now() {
    return std::chrono::high_resolution_clock::now();
}

inline uint64_t elapsed_ns(TimePoint start, TimePoint end) {
    return std::chrono::duration_cast<Duration>(end - start).count();
}

inline uint64_t elapsed_ns(TimePoint start) {
    return elapsed_ns(start, now());
}

// RAII latency measurement
class ScopedLatencyMeasurement {
private:
    TimePoint start_;
    LatencyTracker& tracker_;
    
public:
    ScopedLatencyMeasurement(LatencyTracker& tracker) 
        : start_(now()), tracker_(tracker) {}
    
    ~ScopedLatencyMeasurement() {
        tracker_.record(elapsed_ns(start_));
    }
};

#define MEASURE_LATENCY(tracker) perf::ScopedLatencyMeasurement _measure(tracker)

} // namespace perf

#endif // PERF_LATENCY_TRACKER_HPP
