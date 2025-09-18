#include "fix/fix_gateway.hpp"
#include "matching/matching_engine.hpp"
#include "matching/symbol_manager.hpp"
#include "market_data/publisher.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace fix;
using namespace matching;
using namespace market_data;

int main() {
    std::cout << "=== MVP Trading System Integration Test ===" << std::endl;
    std::cout << "Initializing components..." << std::endl;
    
    // Initialize core components
    SymbolManager symbol_manager;
    MatchingEngine matching_engine;
    MarketDataPublisher market_data_publisher(symbol_manager, matching_engine);
    FixGateway fix_gateway(symbol_manager, matching_engine, market_data_publisher);
    
    // Add test symbols
    auto aapl_id = symbol_manager.add_symbol("AAPL");
    auto msft_id = symbol_manager.add_symbol("MSFT");
    auto googl_id = symbol_manager.add_symbol("GOOGL");
    
    std::cout << "Added symbols: AAPL=" << aapl_id << ", MSFT=" << msft_id << ", GOOGL=" << googl_id << std::endl;
    
    // Start market data publisher
    if (!market_data_publisher.start()) {
        std::cerr << "Failed to start market data publisher" << std::endl;
        return 1;
    }
    std::cout << "Market data publisher started" << std::endl;
    
    // Create console subscriber for market data
    auto console_subscriber = std::make_shared<ConsoleSubscriber>("console", symbol_manager, false);
    market_data_publisher.add_subscriber(console_subscriber);
    market_data_publisher.subscribe_all_symbols("console", MessageType::LEVEL1_UPDATE);
    market_data_publisher.subscribe_all_symbols("console", MessageType::TRADE_REPORT);
    
    std::cout << "Market data subscriptions set up" << std::endl;
    
    // Start FIX gateway
    if (!fix_gateway.start()) {
        std::cerr << "Failed to start FIX gateway" << std::endl;
        return 1;
    }
    std::cout << "FIX gateway started on port 9878" << std::endl;
    
    std::cout << std::endl << "=== SYSTEM READY ===" << std::endl;
    std::cout << "MVP Trading System is running with:" << std::endl;
    std::cout << "- Ultra-low latency order book (100-200ns operations)" << std::endl;
    std::cout << "- Multi-symbol support (AAPL, MSFT, GOOGL)" << std::endl;
    std::cout << "- Real-time market data publishing" << std::endl;
    std::cout << "- FIX Protocol gateway on port 9878" << std::endl;
    std::cout << "- Order matching with price-time priority" << std::endl;
    std::cout << std::endl;
    
    std::cout << "To test the system:" << std::endl;
    std::cout << "1. Compile and run the trading client:" << std::endl;
    std::cout << "   ./bin/trading_client CLIENT1" << std::endl;
    std::cout << "2. Or run multiple clients simultaneously:" << std::endl;
    std::cout << "   ./bin/trading_client CLIENT1 &" << std::endl;
    std::cout << "   ./bin/trading_client CLIENT2 &" << std::endl;
    std::cout << "3. In each client, try commands like:" << std::endl;
    std::cout << "   buy AAPL 100 150.25" << std::endl;
    std::cout << "   sell AAPL 50 150.50" << std::endl;
    std::cout << "   market buy MSFT 25" << std::endl;
    std::cout << "   status" << std::endl;
    std::cout << std::endl;
    
    // Add some initial market structure
    std::cout << "Adding initial market structure..." << std::endl;
    
    // Add some resting orders to create a market
    std::vector<Order> initial_orders = {
        // AAPL bids
        {1001, aapl_id, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 100, 0, 1500000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $150.00
        {1002, aapl_id, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 200, 0, 1499000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $149.90
        {1003, aapl_id, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 150, 0, 1498000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $149.80
        
        // AAPL asks
        {2001, aapl_id, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 100, 0, 1502000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $150.20
        {2002, aapl_id, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 200, 0, 1503000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $150.30
        {2003, aapl_id, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 150, 0, 1504000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $150.40
        
        // MSFT bids and asks
        {3001, msft_id, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 50, 0, 3000000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $300.00
        {3002, msft_id, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 50, 0, 3010000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $301.00
        
        // GOOGL bids and asks
        {4001, googl_id, Side::BUY, OrderType::LIMIT, TimeInForce::DAY, 25, 0, 2500000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $2500.00
        {4002, googl_id, Side::SELL, OrderType::LIMIT, TimeInForce::DAY, 25, 0, 2520000, std::chrono::high_resolution_clock::now(), OrderStatus::NEW}, // $2520.00
    };
    
    for (auto& order : initial_orders) {
        matching_engine.process_order(order);
    }
    
    // Publish initial market data
    market_data_publisher.publish_level1_update(aapl_id);
    market_data_publisher.publish_level1_update(msft_id);
    market_data_publisher.publish_level1_update(googl_id);
    
    std::cout << "Initial market structure created" << std::endl;
    std::cout << std::endl;
    
    // Main loop - keep the system running
    std::cout << "System running... Press Ctrl+C to stop" << std::endl;
    
    // Print periodic statistics
    auto start_time = std::chrono::high_resolution_clock::now();
    int stats_interval = 30; // Print stats every 30 seconds
    
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(stats_interval));
        
        auto now = std::chrono::high_resolution_clock::now();
        auto uptime = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
        
        std::cout << std::endl << "=== SYSTEM STATISTICS (Uptime: " << uptime << "s) ===" << std::endl;
        
        // Matching engine stats
        auto engine_stats = matching_engine.get_stats();
        std::cout << "Matching Engine:" << std::endl;
        std::cout << "  Orders Processed: " << engine_stats.total_orders_processed << std::endl;
        std::cout << "  Total Fills: " << engine_stats.total_fills << std::endl;
        std::cout << "  Total Volume: " << engine_stats.total_volume << std::endl;
        std::cout << "  Active Symbols: " << engine_stats.active_symbols << std::endl;
        std::cout << "  Active Orders: " << engine_stats.active_orders << std::endl;
        
        // Market data stats
        auto md_stats = market_data_publisher.get_stats();
        std::cout << "Market Data Publisher:" << std::endl;
        std::cout << "  Total Messages: " << md_stats.total_messages << std::endl;
        std::cout << "  Level 1 Messages: " << md_stats.level1_messages << std::endl;
        std::cout << "  Trade Messages: " << md_stats.trade_messages << std::endl;
        std::cout << "  Subscribers: " << md_stats.subscribers << std::endl;
        std::cout << "  Dropped Messages: " << md_stats.dropped_messages << std::endl;
        
        // FIX gateway stats
        auto gateway_stats = fix_gateway.get_stats();
        std::cout << "FIX Gateway:" << std::endl;
        std::cout << "  Orders Received: " << gateway_stats.orders_received << std::endl;
        std::cout << "  Orders Accepted: " << gateway_stats.orders_accepted << std::endl;
        std::cout << "  Orders Rejected: " << gateway_stats.orders_rejected << std::endl;
        std::cout << "  Executions Sent: " << gateway_stats.executions_sent << std::endl;
        std::cout << "  Active Sessions: " << gateway_stats.active_sessions << std::endl;
        std::cout << "  Total Volume: " << gateway_stats.total_volume << std::endl;
        
        std::cout << "=====================================" << std::endl;
        
        // Show current market levels
        std::cout << "Current Market:" << std::endl;
        auto aapl_l1 = matching_engine.get_level1_data(aapl_id);
        if (aapl_l1.best_bid_price > 0 || aapl_l1.best_ask_price > 0) {
            std::cout << "  AAPL: $" << std::fixed << std::setprecision(2) 
                      << (aapl_l1.best_bid_price / 10000.0) << " x " << aapl_l1.best_bid_quantity
                      << " / $" << (aapl_l1.best_ask_price / 10000.0) << " x " << aapl_l1.best_ask_quantity << std::endl;
        }
        
        auto msft_l1 = matching_engine.get_level1_data(msft_id);
        if (msft_l1.best_bid_price > 0 || msft_l1.best_ask_price > 0) {
            std::cout << "  MSFT: $" << std::fixed << std::setprecision(2)
                      << (msft_l1.best_bid_price / 10000.0) << " x " << msft_l1.best_bid_quantity
                      << " / $" << (msft_l1.best_ask_price / 10000.0) << " x " << msft_l1.best_ask_quantity << std::endl;
        }
        
        auto googl_l1 = matching_engine.get_level1_data(googl_id);
        if (googl_l1.best_bid_price > 0 || googl_l1.best_ask_price > 0) {
            std::cout << "  GOOGL: $" << std::fixed << std::setprecision(2)
                      << (googl_l1.best_bid_price / 10000.0) << " x " << googl_l1.best_bid_quantity
                      << " / $" << (googl_l1.best_ask_price / 10000.0) << " x " << googl_l1.best_ask_quantity << std::endl;
        }
        std::cout << std::endl;
    }
    
    // Cleanup (this code won't be reached due to infinite loop above)
    std::cout << "Shutting down system..." << std::endl;
    
    fix_gateway.stop();
    market_data_publisher.stop();
    
    std::cout << "System shutdown complete" << std::endl;
    return 0;
}
