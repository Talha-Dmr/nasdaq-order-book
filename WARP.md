# WARP.md

This file provides guidance to WARP (warp.dev) when working with code in this repository.

## Project Overview

This is a high-performance C++ NASDAQ ITCH 5.0 order book implementation targeting ultra-low latency (100-200ns operations). The project focuses on extreme optimization using modern C++ techniques, SIMD instructions, cache-friendly data structures, and lock-free programming.

## Build System

The project uses CMake with C++17. Build commands:

```bash
# Create build directory and compile
mkdir build && cd build
cmake ..
make

# Build specific targets
make nasdaq_order_book          # Main application
make order_book_benchmark       # Performance benchmarks
```

Executables are placed in `build/bin/`:
- `nasdaq_order_book` - Main ITCH parser application
- `order_book_benchmark` - Google Benchmark performance tests

## Running the Application

```bash
# Run main application with binary ITCH data
./build/bin/nasdaq_order_book test_data.bin

# Run benchmarks
./build/bin/order_book_benchmark
```

## Core Architecture

### Two-Tier Order Book Design

The project implements two distinct order book architectures:

1. **Standard Order Book** (`order_book.hpp/cpp`) - Traditional implementation using STL containers
2. **Ultra-Optimized Order Book** (`order_book.hpp` UltraOrderBook class) - Extreme performance implementation

### Ultra-Optimization Techniques

The `UltraOrderBook` class employs several advanced optimization strategies:

- **Custom Memory Management**: Lock-free object pool with 1M pre-allocated orders
- **Cache-Friendly Data Layout**: 32-byte aligned structures, single cache line orders
- **SIMD Instructions**: AVX2 for parallel price level scanning
- **Branchless Operations**: Conditional moves and bit manipulation
- **Template Specialization**: Compile-time optimization for buy/sell sides
- **Inline Assembly**: Direct assembly for critical price-to-index conversions
- **Prefetching**: Explicit cache line prefetching for likely-accessed data

### ITCH Parser Architecture

The parser (`itch_parser.hpp/cpp`) handles NASDAQ ITCH 5.0 message types:
- System Event Messages
- Stock Directory Messages  
- Add Order Messages (with/without MPID)
- Order Execution Messages (with/without price)
- Order Cancel/Delete/Replace Messages

All message structures use `#pragma pack(push, 1)` for exact binary layout matching.

### Performance Characteristics

- **Target Latency**: 100-200ns per order operation
- **Price Range**: Optimized for $4.00-$6.00 range (configurable via `ULTRA_MIN_PRICE`/`ULTRA_MAX_PRICE`)
- **Hash Table Size**: 64K entries with custom hash function
- **Order Pool**: 1M pre-allocated orders in lock-free stack

## Key Files

- `include/order_book.hpp` - Contains both standard and ultra-optimized order book implementations
- `include/itch_parser.hpp` - ITCH message structures and parsing interface
- `src/itch_parser.cpp` - ITCH parsing logic and order book integration
- `src/main.cpp` - Application entry point with timing measurements
- `benchmarks/benchmark.cpp` - Comprehensive performance benchmarks
- `test_data.bin` - Sample binary ITCH data file

## Development Guidelines

### Performance Considerations

When modifying the ultra-optimized code:
- Maintain cache alignment (`alignas(32)`, `alignas(64)`)
- Preserve SIMD optimization paths
- Keep critical functions inlined with `__attribute__((always_inline))`
- Use `benchmark::DoNotOptimize()` to prevent compiler over-optimization in tests

### Memory Layout Rules

- Orders are packed in 32-byte structures for single cache line access
- Price levels use 32-byte alignment for SIMD operations
- Hash table entries are 64-byte aligned to avoid false sharing

### Debugging vs Performance

The codebase includes both readable (standard) and ultra-optimized implementations. Use the standard `OptimizedOrderBook` for debugging and development, then switch to `UltraOrderBook` for performance testing.

## Testing and Benchmarking

```bash
# Run all benchmarks
./build/bin/order_book_benchmark

# Run specific benchmark patterns
./build/bin/order_book_benchmark --benchmark_filter="Ultra.*"
./build/bin/order_book_benchmark --benchmark_filter=".*Add.*"
```

Benchmark categories:
- `BM_Current_OrderBook_Add` - Standard implementation baseline
- `BM_Ultra_OrderBook_Add` - Ultra-optimized single operations
- `BM_Ultra_Mixed_Operations` - Realistic workload simulation
- `BM_Cache_Performance_Test` - Cache locality testing

## Dependencies

Required system packages:
- CMake 3.10+
- C++17 compliant compiler (GCC/Clang)
- Google Benchmark library
- AVX2 CPU support (for SIMD optimizations)

The project uses minimal external dependencies, relying primarily on STL and compiler intrinsics for maximum performance.
