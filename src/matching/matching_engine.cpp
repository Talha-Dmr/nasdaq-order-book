#include "matching/matching_engine.hpp"
#include <algorithm>
#include <iostream>

namespace matching {

MatchingEngine::MatchingEngine(FillCallback callback) 
    : fill_callback_(std::move(callback)) {
}

MatchResult MatchingEngine::process_order(Order order) {
    // Set timestamp and validate
    order.timestamp = std::chrono::high_resolution_clock::now();
    
    if (order.quantity == 0) {
        order.status = OrderStatus::REJECTED;
        return {OrderStatus::REJECTED, {}, 0};
    }
    
    // Store in active orders
    active_orders_[order.id] = order;
    
    // Route to appropriate processing logic
    MatchResult result;
    if (order.type == OrderType::MARKET) {
        result = process_market_order(active_orders_[order.id]);
    } else {
        result = process_limit_order(active_orders_[order.id]);
    }
    
    // Update order status
    update_order_status(active_orders_[order.id]);
    
    // Remove if fully filled or canceled
    if (active_orders_[order.id].is_fully_filled() || 
        active_orders_[order.id].status == OrderStatus::CANCELED) {
        active_orders_.erase(order.id);
    }
    
    return result;
}

MatchResult MatchingEngine::process_market_order(Order& order) {
    UltraOrderBook* book = get_or_create_book(order.symbol);
    if (!book) {
        order.status = OrderStatus::REJECTED;
        return {OrderStatus::REJECTED, {}, 0};
    }
    
    // Market orders execute immediately against best available prices
    return attempt_cross(order, *book);
}

MatchResult MatchingEngine::process_limit_order(Order& order) {
    UltraOrderBook* book = get_or_create_book(order.symbol);
    if (!book) {
        order.status = OrderStatus::REJECTED;
        return {OrderStatus::REJECTED, {}, 0};
    }
    
    // First try to cross with existing orders
    MatchResult result = attempt_cross(order, *book);
    
    // If not fully filled and order allows resting, add to book
    if (!order.is_fully_filled() && order.tif != TimeInForce::IOC && order.tif != TimeInForce::FOK) {
        // Add remaining quantity to book
        if (order.is_buy()) {
            book->ultra_addOrder(order.id, 'B', order.remaining_quantity(), order.price);
        } else {
            book->ultra_addOrder(order.id, 'S', order.remaining_quantity(), order.price);
        }
        
        if (result.total_filled == 0) {
            result.final_status = OrderStatus::NEW;
        } else {
            result.final_status = OrderStatus::PARTIALLY_FILLED;
        }
    }
    
    // Handle FOK (Fill or Kill) - cancel if not fully filled
    if (order.tif == TimeInForce::FOK && !order.is_fully_filled()) {
        order.status = OrderStatus::CANCELED;
        result.final_status = OrderStatus::CANCELED;
        // Note: In real system, would need to reverse any partial fills
    }
    
    return result;
}

MatchResult MatchingEngine::attempt_cross(Order& aggressive_order, UltraOrderBook& book) {
    MatchResult result;
    
    while (!aggressive_order.is_fully_filled()) {
        // Find best counter-side price
        Price best_price = 0;
        if (aggressive_order.is_buy()) {
            best_price = book.ultra_getBestAsk();
        } else {
            best_price = book.ultra_getBestBid();
        }
        
        if (best_price == 0) {
            // No counter-side liquidity
            break;
        }
        
        // Check if prices cross
        bool can_execute = false;
        if (aggressive_order.type == OrderType::MARKET) {
            can_execute = true;  // Market orders execute at any price
        } else if (aggressive_order.is_buy() && aggressive_order.price >= best_price) {
            can_execute = true;  // Buy limit crosses with lower ask
        } else if (aggressive_order.is_sell() && aggressive_order.price <= best_price) {
            can_execute = true;  // Sell limit crosses with higher bid
        }
        
        if (!can_execute) {
            break;
        }
        
        // For this MVP, we'll simulate finding and executing against the best order
        // In a real implementation, we'd need to access individual orders at the best price
        
        // Create a simulated passive order (this would come from the order book)
        OrderId passive_order_id = next_trade_id_.fetch_add(1) + 1000000; // Simulate passive order ID
        Quantity available_quantity = std::min(aggressive_order.remaining_quantity(), 
                                             static_cast<Quantity>(100)); // Simulate available qty
        
        // Execute the trade
        Price execution_price = best_price;
        Quantity execution_quantity = std::min(aggressive_order.remaining_quantity(), available_quantity);
        
        // Create fill record
        Fill fill = create_fill(aggressive_order, 
                              Order{passive_order_id, aggressive_order.symbol}, // Simulated passive order
                              execution_price, 
                              execution_quantity);
        
        // Update aggressive order
        aggressive_order.filled_quantity += execution_quantity;
        
        // Update book (simulate removing liquidity)
        if (aggressive_order.is_buy()) {
            // Would remove from ask side
            book.ultra_executeOrder(passive_order_id, execution_quantity);
        } else {
            // Would remove from bid side  
            book.ultra_executeOrder(passive_order_id, execution_quantity);
        }
        
        // Record the fill
        result.fills.push_back(fill);
        result.total_filled += execution_quantity;
        
        // Notify via callback
        if (fill_callback_) {
            fill_callback_(fill);
        }
        
        // For IOC orders, only one attempt
        if (aggressive_order.tif == TimeInForce::IOC) {
            break;
        }
    }
    
    // Set final status
    if (aggressive_order.is_fully_filled()) {
        result.final_status = OrderStatus::FILLED;
    } else if (result.total_filled > 0) {
        result.final_status = OrderStatus::PARTIALLY_FILLED;
    } else {
        result.final_status = OrderStatus::NEW;
    }
    
    return result;
}

Fill MatchingEngine::create_fill(const Order& aggressive, const Order& passive, Price price, Quantity qty) {
    Fill fill;
    fill.aggressive_order_id = aggressive.id;
    fill.passive_order_id = passive.id;
    fill.symbol = aggressive.symbol;
    fill.execution_price = price;
    fill.execution_quantity = qty;
    fill.execution_time = std::chrono::high_resolution_clock::now();
    fill.trade_id = next_trade_id_.fetch_add(1);
    
    return fill;
}

void MatchingEngine::update_order_status(Order& order) {
    if (order.is_fully_filled()) {
        order.status = OrderStatus::FILLED;
    } else if (order.filled_quantity > 0) {
        order.status = OrderStatus::PARTIALLY_FILLED;
    }
    // Status already set for other cases (NEW, CANCELED, REJECTED)
}

UltraOrderBook* MatchingEngine::get_or_create_book(SymbolId symbol) {
    auto it = order_books_.find(symbol);
    if (it != order_books_.end()) {
        return it->second.get();
    }
    
    // Create new order book for symbol
    auto book = std::make_unique<UltraOrderBook>();
    UltraOrderBook* book_ptr = book.get();
    order_books_[symbol] = std::move(book);
    
    return book_ptr;
}

bool MatchingEngine::cancel_order(OrderId order_id) {
    auto it = active_orders_.find(order_id);
    if (it == active_orders_.end()) {
        return false; // Order not found
    }
    
    Order& order = it->second;
    
    // Remove from order book if it's resting there
    UltraOrderBook* book = get_or_create_book(order.symbol);
    if (book) {
        book->ultra_deleteOrder(order_id);
    }
    
    // Update status and remove from active orders
    order.status = OrderStatus::CANCELED;
    active_orders_.erase(it);
    
    return true;
}

bool MatchingEngine::replace_order(OrderId old_id, Order new_order) {
    // Cancel old order
    if (!cancel_order(old_id)) {
        return false;
    }
    
    // Process new order
    MatchResult result = process_order(std::move(new_order));
    
    return result.final_status != OrderStatus::REJECTED;
}

void MatchingEngine::add_symbol(SymbolId symbol) {
    if (order_books_.find(symbol) == order_books_.end()) {
        order_books_[symbol] = std::make_unique<UltraOrderBook>();
    }
}

void MatchingEngine::remove_symbol(SymbolId symbol) {
    // Cancel all orders for this symbol first
    std::vector<OrderId> orders_to_cancel;
    for (const auto& [order_id, order] : active_orders_) {
        if (order.symbol == symbol) {
            orders_to_cancel.push_back(order_id);
        }
    }
    
    for (OrderId order_id : orders_to_cancel) {
        cancel_order(order_id);
    }
    
    // Remove the order book
    order_books_.erase(symbol);
}

std::vector<SymbolId> MatchingEngine::get_active_symbols() const {
    std::vector<SymbolId> symbols;
    symbols.reserve(order_books_.size());
    
    for (const auto& [symbol, book] : order_books_) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

Level1Data MatchingEngine::get_level1_data(SymbolId symbol) const {
    Level1Data data;
    data.symbol = symbol;
    data.update_time = std::chrono::high_resolution_clock::now();
    
    auto it = order_books_.find(symbol);
    if (it != order_books_.end()) {
        const UltraOrderBook* book = it->second.get();
        
        // Get best bid/ask prices (methods would need to be added to UltraOrderBook)
        data.best_bid_price = book->ultra_getBestBid();
        data.best_ask_price = book->ultra_getBestAsk();
        
        // For MVP, simulate quantities (real implementation would get from order book)
        data.best_bid_quantity = (data.best_bid_price > 0) ? 100 : 0;
        data.best_ask_quantity = (data.best_ask_price > 0) ? 100 : 0;
    }
    
    return data;
}

Level2Data MatchingEngine::get_level2_data(SymbolId symbol, uint32_t depth) const {
    Level2Data data;
    data.symbol = symbol;
    data.update_time = std::chrono::high_resolution_clock::now();
    
    // For MVP, return simplified level 2 data
    // Real implementation would iterate through price levels in the order book
    Level1Data l1 = get_level1_data(symbol);
    
    if (l1.best_bid_price > 0) {
        data.bids.push_back({l1.best_bid_price, l1.best_bid_quantity, 1});
        // Simulate additional levels
        for (uint32_t i = 1; i < depth && i < 5; ++i) {
            Price level_price = l1.best_bid_price - i;
            if (level_price > 0) {
                data.bids.push_back({level_price, 50 + i * 10, 1});
            }
        }
    }
    
    if (l1.best_ask_price > 0) {
        data.asks.push_back({l1.best_ask_price, l1.best_ask_quantity, 1});
        // Simulate additional levels
        for (uint32_t i = 1; i < depth && i < 5; ++i) {
            data.asks.push_back({l1.best_ask_price + i, 50 + i * 10, 1});
        }
    }
    
    return data;
}

std::optional<Order> MatchingEngine::get_order(OrderId order_id) const {
    auto it = active_orders_.find(order_id);
    if (it != active_orders_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Order> MatchingEngine::get_orders_for_symbol(SymbolId symbol) const {
    std::vector<Order> orders;
    
    for (const auto& [order_id, order] : active_orders_) {
        if (order.symbol == symbol) {
            orders.push_back(order);
        }
    }
    
    return orders;
}

MatchingEngine::Stats MatchingEngine::get_stats() const {
    Stats stats;
    stats.active_symbols = static_cast<uint32_t>(order_books_.size());
    stats.active_orders = static_cast<uint32_t>(active_orders_.size());
    
    // Other stats would be tracked incrementally in a real implementation
    static uint64_t total_orders = 0;
    static uint64_t total_fills = 0;
    static uint64_t total_volume = 0;
    
    stats.total_orders_processed = ++total_orders;
    stats.total_fills = total_fills;
    stats.total_volume = total_volume;
    
    return stats;
}

} // namespace matching
