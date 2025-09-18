#include "itch_parser.hpp"
#include "object_pool.hpp"
#include "order_book.hpp"
#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>

// OrderBookManager is defined in order_book.cpp
extern OrderBookManager g_orderBookManager;
static std::unordered_map<uint64_t, std::string> g_order_to_symbol_map;

std::string extractSymbol(const char *field, size_t len) {
  std::string symbol(field, len);
  size_t end = symbol.find_last_not_of(' ');
  return (end == std::string::npos) ? "" : symbol.substr(0, end + 1);
}

void handleAddOrder(const AddOrderMessage *msg) {
  std::string symbol = extractSymbol(msg->stockSymbol, 8);
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(symbol);
  if (!book)
    return;
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  book->addOrder(orderId, msg->buySellIndicator, ntohl(msg->shares),
                 ntohl(msg->price));
  g_order_to_symbol_map[orderId] = symbol;
}

void handleAddOrderWithMPID(const AddOrderWithMPIDMessage *msg) {
  std::string symbol = extractSymbol(msg->stockSymbol, 8);
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(symbol);
  if (!book)
    return;
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  book->addOrder(orderId, msg->buySellIndicator, ntohl(msg->shares),
                 ntohl(msg->price));
  g_order_to_symbol_map[orderId] = symbol;
}

void handleOrderExecuted(const OrderExecutedMessage *msg) {
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  auto it = g_order_to_symbol_map.find(orderId);
  if (it == g_order_to_symbol_map.end())
    return;
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(it->second);
  if (book)
    book->executeOrder(orderId, ntohl(msg->executedShares));
}

void handleOrderExecutedWithPrice(const OrderExecutedWithPriceMessage *msg) {
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  auto it = g_order_to_symbol_map.find(orderId);
  if (it == g_order_to_symbol_map.end())
    return;
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(it->second);
  if (book)
    book->executeOrder(orderId, ntohl(msg->executedShares));
}

void handleOrderCancel(const OrderCancelMessage *msg) {
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  auto it = g_order_to_symbol_map.find(orderId);
  if (it == g_order_to_symbol_map.end())
    return;
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(it->second);
  if (book)
    book->executeOrder(orderId, ntohl(msg->canceledShares));
}

void handleOrderDelete(const OrderDeleteMessage *msg) {
  uint64_t orderId = __builtin_bswap64(msg->orderReferenceNumber);
  auto it = g_order_to_symbol_map.find(orderId);
  if (it == g_order_to_symbol_map.end())
    return;
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(it->second);
  if (book)
    book->deleteOrder(orderId);
  g_order_to_symbol_map.erase(orderId);
}

void handleOrderReplace(const OrderReplaceMessage *msg) {
  uint64_t oldOrderId = __builtin_bswap64(msg->originalOrderReferenceNumber);
  auto it = g_order_to_symbol_map.find(oldOrderId);
  if (it == g_order_to_symbol_map.end())
    return;
  std::string symbol = it->second;
  OrderBook *book = g_orderBookManager.getOrCreateOrderBook(symbol);
  if (book) {
    uint64_t newOrderId = __builtin_bswap64(msg->newOrderReferenceNumber);
    book->replaceOrder(oldOrderId, newOrderId, ntohl(msg->shares),
                       ntohl(msg->price));
    g_order_to_symbol_map.erase(oldOrderId);
    g_order_to_symbol_map[newOrderId] = symbol;
  }
}

void parse_packet(const char *buffer, size_t size) {
  const char *current_pos = buffer;
  const char *end_pos = buffer + size;

  while (current_pos < end_pos) {
    char messageType = *current_pos;
    unsigned int messageLength = 0;
    switch (messageType) {
    case 'S':
      messageLength = sizeof(SystemEventMessage);
      break;
    case 'R':
      messageLength = sizeof(StockDirectoryMessage);
      g_orderBookManager.getOrCreateOrderBook(extractSymbol(
          reinterpret_cast<const StockDirectoryMessage *>(current_pos)
              ->stockSymbol,
          8));
      break;
    case 'A':
      messageLength = sizeof(AddOrderMessage);
      handleAddOrder(reinterpret_cast<const AddOrderMessage *>(current_pos));
      break;
    case 'F':
      messageLength = sizeof(AddOrderWithMPIDMessage);
      handleAddOrderWithMPID(
          reinterpret_cast<const AddOrderWithMPIDMessage *>(current_pos));
      break;
    case 'E':
      messageLength = sizeof(OrderExecutedMessage);
      handleOrderExecuted(
          reinterpret_cast<const OrderExecutedMessage *>(current_pos));
      break;
    case 'C':
      messageLength = sizeof(OrderExecutedWithPriceMessage);
      handleOrderExecutedWithPrice(
          reinterpret_cast<const OrderExecutedWithPriceMessage *>(current_pos));
      break;
    case 'X':
      messageLength = sizeof(OrderCancelMessage);
      handleOrderCancel(
          reinterpret_cast<const OrderCancelMessage *>(current_pos));
      break;
    case 'D':
      messageLength = sizeof(OrderDeleteMessage);
      handleOrderDelete(
          reinterpret_cast<const OrderDeleteMessage *>(current_pos));
      break;
    case 'U':
      messageLength = sizeof(OrderReplaceMessage);
      handleOrderReplace(
          reinterpret_cast<const OrderReplaceMessage *>(current_pos));
      break;
    default:
      current_pos = end_pos;
      continue;
    }
    if (messageLength == 0 || (current_pos + messageLength > end_pos))
      break;
    current_pos += messageLength;
  }
}
