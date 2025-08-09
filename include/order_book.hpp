#ifndef ORDER_BOOK_HPP
#define ORDER_BOOK_HPP

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Forward declare Order for use in PriceLevel
struct Order;

struct PriceLevel {
  uint64_t quantity;
  Order *head = nullptr;
  Order *tail = nullptr;
};

struct Order {
  uint64_t id;
  char side;
  uint32_t quantity;
  uint32_t price;
  Order *prev = nullptr;
  Order *next = nullptr;
};

class OrderBook {
public:
  void addOrder(uint64_t orderId, char side, uint32_t quantity, uint32_t price);
  void executeOrder(uint64_t orderId, uint32_t executed_qty);
  void deleteOrder(uint64_t orderId);
  void replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty,
                    uint32_t newPrice);
  void display() const;

private:
  void add_to_level(Order *order);
  void remove_from_level(Order *order);

  std::unordered_map<uint64_t, Order *> orders_;
  std::map<uint32_t, PriceLevel, std::greater<uint32_t>> bids_;
  std::map<uint32_t, PriceLevel> asks_;
};

class OrderBookManager {
public:
  OrderBook *getOrCreateOrderBook(const std::string &symbol);
  void displayAllBooks() const;

private:
  std::unordered_map<std::string, std::unique_ptr<OrderBook>> books_;
};

extern OrderBookManager g_orderBookManager;
#endif // ORDER_BOOK_HPP
