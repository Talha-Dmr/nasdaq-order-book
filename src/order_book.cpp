#include "order_book.hpp"
#include "object_pool.hpp"
#include <algorithm>
#include <iomanip>
#include <iostream>

// This is defined in itch_parser.cpp
extern ObjectPool<Order> g_order_pool;

// --- OrderBookManager Methods ---

OrderBook *OrderBookManager::getOrCreateOrderBook(const std::string &symbol) {
  if (books_.find(symbol) == books_.end()) {
    books_[symbol] = std::make_unique<OrderBook>();
  }
  return books_[symbol].get();
}
void OrderBookManager::displayAllBooks() const {
  for (const auto &pair : books_) {
    std::cout << "\n===== Order Book for: " << pair.first
              << " =====" << std::endl;
    pair.second->display();
  }
}

// --- OrderBook Methods (with Intrusive Linked List Logic) ---

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

  Order *order = g_order_pool.acquire();
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

  // Update total quantity at the price level
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

  g_order_pool.release(order);
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
