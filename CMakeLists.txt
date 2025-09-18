cmake_minimum_required(VERSION 3.10)
project(nasdaq_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Performance optimization flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -march=native -mavx2 -DNDEBUG")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffast-math -funroll-loops")

# --- Find Packages ---
find_package(Threads REQUIRED)
find_package(benchmark REQUIRED) # Google Benchmark kütüphanesini bul

# --- Include Directories ---
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# --- Ana Uygulama (Order Book) ---
set(APP_SOURCES
    src/main.cpp
    src/itch_parser.cpp
    src/order_book.cpp
    src/itch/decoder.cpp
    src/net/feed_listener.cpp
    src/net/arbiter.cpp
    src/matching/matching_engine.cpp
)
add_executable(nasdaq_order_book ${APP_SOURCES})
target_link_libraries(nasdaq_order_book PRIVATE Threads::Threads) # Gerekli olmasa da kalabilir
set_target_properties(nasdaq_order_book PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Benchmark Testleri ---
set(BENCHMARK_SOURCES
    benchmarks/benchmark.cpp
    src/order_book.cpp 
)
add_executable(order_book_benchmark ${BENCHMARK_SOURCES})

# Benchmark'ı, benchmark kütüphanesine bağla
target_link_libraries(order_book_benchmark PRIVATE benchmark::benchmark)

set_target_properties(order_book_benchmark PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Matching Engine Test ---
set(MATCHING_TEST_SOURCES
    src/matching/test_matching.cpp
    src/matching/matching_engine.cpp
    src/order_book.cpp
)
add_executable(test_matching ${MATCHING_TEST_SOURCES})
target_link_libraries(test_matching PRIVATE Threads::Threads)
set_target_properties(test_matching PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Multi-Symbol Test ---
set(MULTISYMBOL_TEST_SOURCES
    src/matching/test_multisymbol.cpp
    src/matching/matching_engine.cpp
    src/matching/symbol_manager.cpp
    src/order_book.cpp
)
add_executable(test_multisymbol ${MULTISYMBOL_TEST_SOURCES})
target_link_libraries(test_multisymbol PRIVATE Threads::Threads)
set_target_properties(test_multisymbol PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Market Data Publisher Test ---
set(MARKETDATA_TEST_SOURCES
    src/market_data/test_publisher.cpp
    src/market_data/publisher.cpp
    src/matching/matching_engine.cpp
    src/matching/symbol_manager.cpp
    src/order_book.cpp
)
add_executable(test_market_data ${MARKETDATA_TEST_SOURCES})
target_link_libraries(test_market_data PRIVATE Threads::Threads)
set_target_properties(test_market_data PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Trading Client ---
set(TRADING_CLIENT_SOURCES
    src/fix/trading_client.cpp
    src/fix/fix_session.cpp
)
add_executable(trading_client ${TRADING_CLIENT_SOURCES})
target_link_libraries(trading_client PRIVATE Threads::Threads)
set_target_properties(trading_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- MVP Integration Test ---
set(MVP_INTEGRATION_SOURCES
    src/fix/test_mvp_integration.cpp
    src/fix/fix_gateway.cpp
    src/fix/fix_session.cpp
    src/market_data/publisher.cpp
    src/matching/matching_engine.cpp
    src/matching/symbol_manager.cpp
    src/order_book.cpp
)
add_executable(mvp_trading_system ${MVP_INTEGRATION_SOURCES})
target_link_libraries(mvp_trading_system PRIVATE Threads::Threads)
set_target_properties(mvp_trading_system PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# --- Simple MVP Demo ---
add_executable(mvp_demo src/fix/simple_mvp_test.cpp)
set_target_properties(mvp_demo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
