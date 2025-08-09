#include "itch_parser.hpp"
#include "order_book.hpp" // For g_orderBookManager
#include <iostream>
#include <fstream>
#include <vector>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_data.bin>" << std::endl;
        return 1;
    }

    std::ifstream file(argv[1], std::ios::binary);
    if (!file) {
        std::cerr << "Error: Could not open file " << argv[1] << std::endl;
        return 1;
    }

    std::vector<char> buffer(std::istreambuf_iterator<char>(file), {});
    std::cout << "Read " << buffer.size() << " bytes from file." << std::endl;
    
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!buffer.empty()) {
        parse_packet(buffer.data(), buffer.size());
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    std::cout << "\n--- FINAL ORDER BOOK STATE ---" << std::endl;
    g_orderBookManager.displayAllBooks();
    
    std::cout << "\nTotal processing time: " << duration.count() << "ms" << std::endl;

    return 0;
}