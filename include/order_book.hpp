#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <map>
#include <string>
#include <cstdint>
#include <unordered_map>
#include <memory>

struct Order {
    uint64_t id;
    char side;
    uint32_t quantity;
    uint32_t price;
};

class OrderBook {
public:
    void addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price);
    void executeOrder(uint64_t orderId, uint32_t executed_qty);
    void deleteOrder(uint64_t orderId);
    void replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty, uint32_t newPrice);
    void display() const;

private:
    void add_to_level(uint32_t price, uint32_t qty, char side);
    void remove_from_level(uint32_t price, uint32_t qty, char side);

    std::unordered_map<uint64_t, Order> orders_;
    std::map<uint32_t, uint64_t, std::greater<uint32_t>> bids_;
    std::map<uint32_t, uint64_t> asks_;
};

class OrderBookManager {
public:
    OrderBook* getOrCreateOrderBook(const std::string& symbol);
    void displayAllBooks() const;

private:
    std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;
};

extern OrderBookManager g_orderBookManager;
#endif // ORDER_BOOK_HPP