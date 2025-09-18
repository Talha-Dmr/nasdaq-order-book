#include "matching/matching_engine.hpp"
#include <iostream>
#include <iomanip>

using namespace matching;

void print_fill(const Fill& fill) {
    std::cout << "FILL: Trade ID=" << fill.trade_id 
              << " | Aggressive=" << fill.aggressive_order_id
              << " | Passive=" << fill.passive_order_id
              << " | Symbol=" << fill.symbol
              << " | Price=$" << std::fixed << std::setprecision(4) << (fill.execution_price / 10000.0)
              << " | Quantity=" << fill.execution_quantity << std::endl;
}

void print_order_result(const matching::Order& order, const MatchResult& result) {
    std::cout << "\nORDER RESULT: ID=" << order.id << " | Symbol=" << order.symbol 
              << " | Side=" << (char)order.side << " | Type=" << (char)order.type
              << " | Quantity=" << order.quantity 
              << " | Price=$" << std::fixed << std::setprecision(4) << (order.price / 10000.0) << std::endl;
    
    std::cout << "  Status: " << (char)result.final_status << " | Filled: " << result.total_filled 
              << " | Fills: " << result.fills.size() << std::endl;
    
    for (const auto& fill : result.fills) {
        std::cout << "  ";
        print_fill(fill);
    }
}

void print_market_data(const MatchingEngine& engine, SymbolId symbol) {
    Level1Data l1 = engine.get_level1_data(symbol);
    std::cout << "\nMARKET DATA (Symbol " << symbol << "):" << std::endl;
    std::cout << "  Best Bid: $" << std::fixed << std::setprecision(4) << (l1.best_bid_price / 10000.0)
              << " x " << l1.best_bid_quantity << std::endl;
    std::cout << "  Best Ask: $" << std::fixed << std::setprecision(4) << (l1.best_ask_price / 10000.0) 
              << " x " << l1.best_ask_quantity << std::endl;
}

int main() {
    std::cout << "=== MATCHING ENGINE MVP TEST ===" << std::endl;
    
    // Create matching engine with fill callback
    MatchingEngine engine([](const Fill& fill) {
        std::cout << "CALLBACK: ";
        print_fill(fill);
    });
    
    // Add a test symbol
    SymbolId test_symbol = 1;
    engine.add_symbol(test_symbol);
    
    std::cout << "\n1. Adding resting BID orders..." << std::endl;
    
    // Add some resting bid orders (limit orders below market)
    matching::Order bid1{1001, test_symbol, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 100, 0, 49900}; // $4.99
    auto result1 = engine.process_order(bid1);
    print_order_result(bid1, result1);
    
    matching::Order bid2{1002, test_symbol, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 200, 0, 49800}; // $4.98
    auto result2 = engine.process_order(bid2);
    print_order_result(bid2, result2);
    
    // Show market data
    print_market_data(engine, test_symbol);
    
    std::cout << "\n2. Adding resting ASK orders..." << std::endl;
    
    // Add some resting ask orders
    matching::Order ask1{2001, test_symbol, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 150, 0, 50100}; // $5.01
    auto result3 = engine.process_order(ask1);
    print_order_result(ask1, result3);
    
    matching::Order ask2{2002, test_symbol, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 300, 0, 50200}; // $5.02
    auto result4 = engine.process_order(ask2);
    print_order_result(ask2, result4);
    
    // Show updated market data
    print_market_data(engine, test_symbol);
    
    std::cout << "\n3. Testing MARKET BUY order (should cross with asks)..." << std::endl;
    
    // Send aggressive market buy - should cross with best ask
    matching::Order market_buy{3001, test_symbol, Side::BUY, OrderType::MARKET, TimeInForce::DAY, 80, 0, 0};
    auto result5 = engine.process_order(market_buy);
    print_order_result(market_buy, result5);
    
    // Show market data after trade
    print_market_data(engine, test_symbol);
    
    std::cout << "\n4. Testing LIMIT SELL order that crosses..." << std::endl;
    
    // Send aggressive limit sell that crosses with best bid
    matching::Order limit_sell{3002, test_symbol, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 50, 0, 49900}; // At bid price
    auto result6 = engine.process_order(limit_sell);
    print_order_result(limit_sell, result6);
    
    // Show final market data
    print_market_data(engine, test_symbol);
    
    std::cout << "\n5. Testing IOC order (Immediate or Cancel)..." << std::endl;
    
    // IOC order that partially crosses
    matching::Order ioc_buy{4001, test_symbol, Side::BUY, OrderType::LIMIT, TimeInForce::IOC, 500, 0, 50200}; // High price
    auto result7 = engine.process_order(ioc_buy);
    print_order_result(ioc_buy, result7);
    
    // Show engine statistics
    std::cout << "\n=== ENGINE STATISTICS ===" << std::endl;
    auto stats = engine.get_stats();
    std::cout << "Total Orders Processed: " << stats.total_orders_processed << std::endl;
    std::cout << "Total Fills: " << stats.total_fills << std::endl;
    std::cout << "Active Symbols: " << stats.active_symbols << std::endl;
    std::cout << "Active Orders: " << stats.active_orders << std::endl;
    
    // List active orders
    std::cout << "\n=== ACTIVE ORDERS ===" << std::endl;
    auto active_orders = engine.get_orders_for_symbol(test_symbol);
    for (const auto& order : active_orders) {
        std::cout << "Order ID=" << order.id << " | " << (char)order.side 
                  << " | Qty=" << order.remaining_quantity() 
                  << " | Price=$" << std::fixed << std::setprecision(4) << (order.price / 10000.0)
                  << " | Status=" << (char)order.status << std::endl;
    }
    
    std::cout << "\n=== MATCHING ENGINE TEST COMPLETED ===" << std::endl;
    
    return 0;
}
