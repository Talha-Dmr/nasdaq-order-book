#include "order_book.hpp"
#include "object_pool.hpp"
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <iomanip>
#include <iostream>

// Simple order pool replacement (basic implementation)
static Order g_order_pool_storage[1000000];
static size_t g_order_pool_index = 0;

Order* acquire_order() {
    if (g_order_pool_index >= 1000000) {
        g_order_pool_index = 0; // Reset pool
    }
    return &g_order_pool_storage[g_order_pool_index++];
}

void release_order(Order* /*order*/) {
    // Simple implementation - do nothing
    // In production: add back to free list
}

// Global OrderBookManager instance implementation
OrderBookManager g_orderBookManager;

// Helper methods for OptimizedOrderBook
void OptimizedOrderBook::add_to_level_fast(Order* order, FastPriceLevel& level) {
    level.quantity += order->quantity;
    level.order_count++;
    level.active = 1;
    
    // Simple LIFO insertion
    order->next = level.first_order;
    if (level.first_order) {
        level.first_order->prev = order;
    }
    level.first_order = order;
    order->prev = nullptr;
}

void OptimizedOrderBook::remove_from_level_fast(Order* order, FastPriceLevel& level) {
    level.quantity -= order->quantity;
    level.order_count--;
    
    if (order->prev) {
        order->prev->next = order->next;
    } else {
        level.first_order = order->next;
    }
    
    if (order->next) {
        order->next->prev = order->prev;
    }
    
    if (level.quantity == 0) {
        level.active = 0;
        level.first_order = nullptr;
    }
}

// =============================================================================
// ORIGINAL ORDER BOOK IMPLEMENTATION - Keep unchanged for compatibility
// =============================================================================

OrderBook *OrderBookManager::getOrCreateOrderBook(const std::string &symbol) {
  if (books_.find(symbol) == books_.end()) {
    books_[symbol] = std::make_unique<OrderBook>();
  }
  return books_[symbol].get();
}

OptimizedOrderBook *
OrderBookManager::getOrCreateOptimizedOrderBook(const std::string &symbol) {
  if (optimized_books_.find(symbol) == optimized_books_.end()) {
    optimized_books_[symbol] = std::make_unique<OptimizedOrderBook>();
  }
  return optimized_books_[symbol].get();
}

void OrderBookManager::displayAllBooks() const {
  for (const auto &pair : books_) {
    std::cout << "\n===== Order Book for: " << pair.first
              << " =====" << std::endl;
    pair.second->display();
  }
}

void OrderBook::add_to_level(Order *order) {
  if (order->side == 'B') {
    PriceLevel &level = bids_[order->price];
    level.quantity += order->quantity;
    if (!level.head) {
      level.head = order;
      level.tail = order;
    } else {
      level.tail->next = order;
      order->prev = level.tail;
      level.tail = order;
    }
  } else {
    PriceLevel &level = asks_[order->price];
    level.quantity += order->quantity;
    if (!level.head) {
      level.head = order;
      level.tail = order;
    } else {
      level.tail->next = order;
      order->prev = level.tail;
      level.tail = order;
    }
  }
}

void OrderBook::remove_from_level(Order *order) {
  if (order->side == 'B') {
    auto it = bids_.find(order->price);
    if (it != bids_.end()) {
      PriceLevel &level = it->second;
      level.quantity -= order->quantity;

      if (order->prev)
        order->prev->next = order->next;
      if (order->next)
        order->next->prev = order->prev;
      if (level.head == order)
        level.head = order->next;
      if (level.tail == order)
        level.tail = order->prev;

      if (level.quantity == 0) {
        bids_.erase(it);
      }
    }
  } else {
    auto it = asks_.find(order->price);
    if (it != asks_.end()) {
      PriceLevel &level = it->second;
      level.quantity -= order->quantity;

      if (order->prev)
        order->prev->next = order->next;
      if (order->next)
        order->next->prev = order->prev;
      if (level.head == order)
        level.head = order->next;
      if (level.tail == order)
        level.tail = order->prev;

      if (level.quantity == 0) {
        asks_.erase(it);
      }
    }
  }
}

void OrderBook::addOrder(uint64_t orderId, char side, uint32_t quantity,
                         uint32_t price) {
  if (orders_.count(orderId))
    return;

  Order *order = acquire_order();
  order->id = orderId;
  order->side = side;
  order->quantity = quantity;
  order->price = price;
  order->next = nullptr;
  order->prev = nullptr;

  orders_[orderId] = order;
  add_to_level(order);
}

void OrderBook::executeOrder(uint64_t orderId, uint32_t executed_qty) {
  auto it = orders_.find(orderId);
  if (it == orders_.end())
    return;

  Order *order = it->second;
  uint32_t qty_to_remove = std::min(executed_qty, order->quantity);

  if (order->side == 'B') {
    if (bids_.count(order->price)) {
      bids_.at(order->price).quantity -= qty_to_remove;
    }
  } else {
    if (asks_.count(order->price)) {
      asks_.at(order->price).quantity -= qty_to_remove;
    }
  }

  order->quantity -= qty_to_remove;

  if (order->quantity == 0) {
    deleteOrder(orderId);
  }
}

void OrderBook::deleteOrder(uint64_t orderId) {
  auto it = orders_.find(orderId);
  if (it == orders_.end())
    return;

  Order *order = it->second;
  remove_from_level(order);

  release_order(order);
  orders_.erase(it);
}

void OrderBook::replaceOrder(uint64_t oldId, uint64_t newId, uint32_t newQty,
                             uint32_t newPrice) {
  auto it = orders_.find(oldId);
  if (it == orders_.end())
    return;

  Order *oldOrder = it->second;
  char side = oldOrder->side;

  if (oldOrder->price != newPrice) {
    remove_from_level(oldOrder);

    oldOrder->id = newId;
    oldOrder->quantity = newQty;
    oldOrder->price = newPrice;
    oldOrder->next = nullptr;
    oldOrder->prev = nullptr;

    orders_.erase(it);
    orders_[newId] = oldOrder;
    add_to_level(oldOrder);
  } else {
    if (side == 'B') {
      if (bids_.count(newPrice)) {
        bids_.at(newPrice).quantity =
            (bids_.at(newPrice).quantity - oldOrder->quantity) + newQty;
      }
    } else {
      if (asks_.count(newPrice)) {
        asks_.at(newPrice).quantity =
            (asks_.at(newPrice).quantity - oldOrder->quantity) + newQty;
      }
    }
    oldOrder->id = newId;
    oldOrder->quantity = newQty;
    orders_.erase(it);
    orders_[newId] = oldOrder;
  }
}

void OrderBook::display() const {
  std::cout << "--- BIDS ---" << std::setw(12) << "QTY" << " | " << "PRICE"
            << std::endl;
  int count = 0;
  for (const auto &[price, level] : bids_) {
    std::cout << std::setw(12) << level.quantity << " | " << std::fixed
              << std::setprecision(4) << (price / 10000.0) << std::endl;
    if (++count >= 5)
      break;
  }

  std::cout << "\n--- ASKS ---" << std::setw(12) << "QTY" << " | " << "PRICE"
            << std::endl;
  count = 0;
  for (const auto &[price, level] : asks_) {
    std::cout << std::setw(12) << level.quantity << " | " << std::fixed
              << std::setprecision(4) << (price / 10000.0) << std::endl;
    if (++count >= 5)
      break;
  }
  std::cout << "------------------" << std::endl;
}

// =============================================================================
// OPTIMIZED ORDER BOOK IMPLEMENTATION - New Fast Version
// =============================================================================

void OptimizedOrderBook::addOrder(uint64_t orderId, char side,
                                  uint32_t quantity, uint32_t price) {
  // Check if order already exists - early exit
  if (order_hash_.find(orderId)) {
    return;
  }

  // Get order from pool
  Order *order = acquire_order();
  if (!order)
    return;

  order->id = orderId;
  order->side = side;
  order->quantity = quantity;
  order->price = price;
  order->prev = nullptr;
  order->next = nullptr;

  // Branchless side selection
  bool is_buy = (side == 'B');
  FastPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;

  uint32_t index = price_to_index(price);
  FastPriceLevel &level = levels[index];

  add_to_level_fast(order, level);
  order_hash_.insert(orderId, order);
}

void OptimizedOrderBook::executeOrder(uint64_t orderId, uint32_t executed_qty) {
  Order *order = order_hash_.find(orderId);
  if (!order)
    return;

  uint32_t qty_to_remove = std::min(executed_qty, order->quantity);

  // Update level quantity
  bool is_buy = (order->side == 'B');
  FastPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;
  uint32_t index = price_to_index(order->price);
  levels[index].quantity -= qty_to_remove;

  order->quantity -= qty_to_remove;

  if (order->quantity == 0) {
    deleteOrder(orderId);
  }
}

void OptimizedOrderBook::deleteOrder(uint64_t orderId) {
  Order *order = order_hash_.find(orderId);
  if (!order)
    return;

  bool is_buy = (order->side == 'B');
  FastPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;
  uint32_t index = price_to_index(order->price);
  FastPriceLevel &level = levels[index];

  remove_from_level_fast(order, level);
  order_hash_.remove(orderId);
  release_order(order);
}

void OptimizedOrderBook::replaceOrder(uint64_t oldId, uint64_t newId,
                                      uint32_t newQty, uint32_t newPrice) {
  Order *oldOrder = order_hash_.find(oldId);
  if (!oldOrder)
    return;

  char side = oldOrder->side;
  bool is_buy = (side == 'B');
  FastPriceLevel *levels = is_buy ? bid_levels_ : ask_levels_;

  if (oldOrder->price != newPrice) {
    // Price changed - remove from old level and add to new
    uint32_t old_index = price_to_index(oldOrder->price);
    FastPriceLevel &old_level = levels[old_index];
    remove_from_level_fast(oldOrder, old_level);

    oldOrder->id = newId;
    oldOrder->quantity = newQty;
    oldOrder->price = newPrice;
    oldOrder->prev = nullptr;
    oldOrder->next = nullptr;

    uint32_t new_index = price_to_index(newPrice);
    FastPriceLevel &new_level = levels[new_index];
    add_to_level_fast(oldOrder, new_level);

    order_hash_.remove(oldId);
    order_hash_.insert(newId, oldOrder);
  } else {
    // Same price - just update quantity
    uint32_t index = price_to_index(newPrice);
    FastPriceLevel &level = levels[index];
    level.quantity = level.quantity - oldOrder->quantity + newQty;

    oldOrder->id = newId;
    oldOrder->quantity = newQty;

    order_hash_.remove(oldId);
    order_hash_.insert(newId, oldOrder);
  }
}

uint32_t OptimizedOrderBook::getBestBid() const {
  // Iterate from highest price downward
  for (uint32_t i = PRICE_LEVELS - 1; i > 0; --i) {
    if (bid_levels_[i].active && bid_levels_[i].quantity > 0) {
      return i + MIN_PRICE;
    }
  }
  return 0;
}

uint32_t OptimizedOrderBook::getBestAsk() const {
  // Iterate from lowest price upward
  for (uint32_t i = 0; i < PRICE_LEVELS; ++i) {
    if (ask_levels_[i].active && ask_levels_[i].quantity > 0) {
      return i + MIN_PRICE;
    }
  }
  return 0;
}

void OptimizedOrderBook::display() const {
  std::cout << "--- OPTIMIZED ORDER BOOK ---" << std::endl;
  std::cout << "Best Bid: " << std::fixed << std::setprecision(4)
            << (getBestBid() / 10000.0) << std::endl;
  std::cout << "Best Ask: " << std::fixed << std::setprecision(4)
            << (getBestAsk() / 10000.0) << std::endl;

  // Show top 5 bid levels
  std::cout << "\n--- BIDS ---" << std::setw(12) << "QTY" << " | " << "PRICE"
            << std::endl;
  int count = 0;
  for (uint32_t i = PRICE_LEVELS - 1; i > 0 && count < 5; --i) {
    if (bid_levels_[i].active && bid_levels_[i].quantity > 0) {
      std::cout << std::setw(12) << bid_levels_[i].quantity << " | "
                << std::fixed << std::setprecision(4)
                << ((i + MIN_PRICE) / 10000.0) << std::endl;
      count++;
    }
  }

  // Show top 5 ask levels
  std::cout << "\n--- ASKS ---" << std::setw(12) << "QTY" << " | " << "PRICE"
            << std::endl;
  count = 0;
  for (uint32_t i = 0; i < PRICE_LEVELS && count < 5; ++i) {
    if (ask_levels_[i].active && ask_levels_[i].quantity > 0) {
      std::cout << std::setw(12) << ask_levels_[i].quantity << " | "
                << std::fixed << std::setprecision(4)
                << ((i + MIN_PRICE) / 10000.0) << std::endl;
      count++;
    }
  }
  std::cout << "------------------" << std::endl;
}
