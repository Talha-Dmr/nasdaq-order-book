cmake_minimum_required(VERSION 3.10)
project(nasdaq_order_book LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Derleyiciye header dosyaları için 'include' klasörüne bakmasını söyler
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Kaynak dosyalarımızı tanımlar
set(SOURCES
    src/main.cpp
    src/itch_parser.cpp
    src/order_book.cpp
)

# Kaynak dosyalarından çalıştırılabilir bir program oluşturur
add_executable(${PROJECT_NAME} ${SOURCES})

# Programın çıktısının build/bin klasörüne yazılmasını sağlar
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)