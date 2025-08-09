#include "order_book.hpp"
#include <iostream>
#include <iomanip>
#include <algorithm> // for std::min

// OrderBookManager Methods
OrderBook* OrderBookManager::getOrCreateOrderBook(const std::string& symbol) {
    if (books_.find(symbol) == books_.end()) {
        books_[symbol] = std::make_unique<OrderBook>();
    }
    return books_[symbol].get();
}

void OrderBookManager::displayAllBooks() const {
    for (const auto& pair : books_) {
        std::cout << "\n===== Order Book for: " << pair.first << " =====" << std::endl;
        pair.second->display();
    }
}

// OrderBook Methods
void OrderBook::add_to_level(uint32_t price, uint32_t qty, char side) {
    if (side == 'B') {
        bids_[price] += qty;
    } else {
        asks_[price] += qty;
    }
}

void OrderBook::remove_from_level(uint32_t price, uint32_t qty, char side) {
    if (side == 'B') {
        if (bids_.count(price)) {
            bids_[price] -= qty;
            if (bids_[price] == 0) {
                bids_.erase(price);
            }
        }
    } else {
        if (asks_.count(price)) {
            asks_[price] -= qty;
            if (asks_[price] == 0) {
                asks_.erase(price);
            }
        }
    }
}

void OrderBook::addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price) {
    if (orders_.count(orderId)) return; // Duplicate order check
    orders_[orderId] = {orderId, side, quantity, price};
    add_to_level(price, quantity, side);
}

void OrderBook::executeOrder(uint64_t orderId, uint32_t executed_qty) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) return; // Order not found

    Order& order = it->second;
    uint32_t qty_to_remove = std::min(executed_qty, order.quantity);
    
    remove_from_level(order.price, qty_to_remove, order.side);
    order.quantity -= qty_to_remove;

    if (order.quantity == 0) {
        orders_.erase(it);
    }
}

void OrderBook::deleteOrder(uint64_t orderId) {
    auto it = orders_.find(orderId);
    if (it == orders_.end()) return;
    
    Order& order = it->second;
    remove_from_level(order.price, order.quantity, order.side);
    orders_.erase(it);
}

void OrderBook::replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty, uint32_t newPrice) {
    auto it = orders_.find(oldId);
    if (it == orders_.end()) return;

    // Get side from the original order before deleting it
    char side = it->second.side;
    
    // First, remove the old order completely
    deleteOrder(oldId);
    
    // Then, add the new order with the correct side
    addOrder(newId, side, newQty, newPrice);
}

void OrderBook::display() const {
    std::cout << "--- BIDS ---" << std::setw(12) << "QTY" << " | " << "PRICE" << std::endl;
    int count = 0;
    for (const auto& [price, quantity] : bids_) {
        std::cout << std::setw(12) << quantity << " | " << std::fixed << std::setprecision(4) << (price / 10000.0) << std::endl;
        if (++count >= 5) break;
    }

    std::cout << "\n--- ASKS ---" << std::setw(12) << "QTY" << " | " << "PRICE" << std::endl;
    count = 0;
    for (const auto& [price, quantity] : asks_) {
        std::cout << std::setw(12) << quantity << " | " << std::fixed << std::setprecision(4) << (price / 10000.0) << std::endl;
        if (++count >= 5) break;
    }
    std::cout << "------------------" << std::endl;
}