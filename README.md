# C++ NASDAQ ITCH Order Book Builder

This project is a high-performance C++ application that parses a NASDAQ ITCH 5.0 data feed from a binary file and constructs an in-memory limit order book.

The project is built with a clean, modular architecture using modern C++ and CMake.

---

## Features

-   **ITCH 5.0 Parser:** Parses the core messages required to track an order's lifecycle (Add, Execute, Cancel, Delete, Replace).
-   **Order Book Logic:** Accurately builds and maintains a limit order book for each symbol found in the data feed.
-   **Data Structures:** Utilizes efficient C++ STL containers like `std::unordered_map` for fast order lookups and `std::map` for sorted price levels.
-   **Clean Architecture:** The code is separated into a reusable parsing library (`itch_parser`) and an application (`order_book`) that consumes it.

---

## How to Build

The project uses CMake. Ensure you have a C++17 compliant compiler.

1.  **Clone and navigate to the project directory.**
2.  **Create and enter the build directory:**
    ```bash
    mkdir build && cd build
    ```
3.  **Run CMake and Make:**
    ```bash
    cmake ..
    make
    ```
    The executable `nasdaq_order_book` will be created in the `build/bin` directory.

---

## How to Run

The application takes the path to a binary ITCH data file as a command-line argument. A sample `test_data.bin` can be created to test functionality.

From the **project's root directory**, run:
```bash
./build/bin/nasdaq_order_book test_data.bin
