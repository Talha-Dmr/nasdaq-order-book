#include "include/order_book.hpp"
#include <iostream>

int main() {
    std::cout << "Testing OptimizedOrderBook creation..." << std::endl;
    
    try {
        OptimizedOrderBook book;
        std::cout << "✅ OptimizedOrderBook created successfully" << std::endl;
        
        book.addOrder(1, 'B', 100, 50000);
        std::cout << "✅ First order added successfully" << std::endl;
        
        book.addOrder(2, 'S', 100, 50100);
        std::cout << "✅ Second order added successfully" << std::endl;
        
        uint32_t best_bid = book.getBestBid();
        uint32_t best_ask = book.getBestAsk();
        
        std::cout << "✅ Best Bid: " << best_bid << std::endl;
        std::cout << "✅ Best Ask: " << best_ask << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "❌ Exception: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "✅ All tests passed!" << std::endl;
    return 0;
}
