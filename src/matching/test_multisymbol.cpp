#include "matching/symbol_manager.hpp"
#include "matching/matching_engine.hpp"
#include <iostream>
#include <iomanip>

using namespace matching;

void print_symbol_info(const SymbolInfo& info) {
    std::cout << "Symbol: " << info.symbol_name 
              << " (ID:" << info.id << ")"
              << " | State: " << static_cast<int>(info.state)
              << " | Tick: $" << std::fixed << std::setprecision(4) << (info.tick_size / 10000.0)
              << " | Range: $" << (info.min_price / 10000.0) 
              << "-$" << (info.max_price / 10000.0)
              << " | Volume: " << info.total_volume
              << " | Trades: " << info.total_trades << std::endl;
}

void print_routing_result(const SymbolRouter::RoutedOrder& result) {
    std::cout << "Order ID " << result.order.id << " | Result: ";
    
    switch (result.result) {
        case SymbolRouter::RouteResult::SUCCESS:
            std::cout << "SUCCESS";
            break;
        case SymbolRouter::RouteResult::INVALID_SYMBOL:
            std::cout << "INVALID_SYMBOL";
            break;
        case SymbolRouter::RouteResult::MARKET_CLOSED:
            std::cout << "MARKET_CLOSED";
            break;
        case SymbolRouter::RouteResult::INVALID_PRICE:
            std::cout << "INVALID_PRICE";
            break;
        case SymbolRouter::RouteResult::INVALID_QUANTITY:
            std::cout << "INVALID_QUANTITY";
            break;
        case SymbolRouter::RouteResult::REJECTED:
            std::cout << "REJECTED";
            break;
    }
    
    if (!result.error_message.empty()) {
        std::cout << " | Error: " << result.error_message;
    }
    
    std::cout << std::endl;
}

int main() {
    std::cout << "=== MULTI-SYMBOL TRADING SYSTEM TEST ===" << std::endl;
    
    // Create matching engine with fill callback
    MatchingEngine engine([](const Fill& fill) {
        std::cout << "FILL: " << fill.symbol << " | Trade ID=" << fill.trade_id 
                  << " | Price=$" << std::fixed << std::setprecision(4) << (fill.execution_price / 10000.0)
                  << " | Qty=" << fill.execution_quantity << std::endl;
    });
    
    // Create symbol manager
    SymbolManager symbol_manager;
    
    // Create order router
    SymbolRouter router(symbol_manager, engine);
    
    std::cout << "\n1. Loading major tech symbols..." << std::endl;
    
    // Load some real symbols
    std::vector<std::string> tech_symbols = {"AAPL", "MSFT", "GOOGL", "TSLA", "NVDA"};
    
    for (const auto& symbol_name : tech_symbols) {
        SymbolId id = symbol_manager.add_symbol(symbol_name, 1, 1000, 5000000); // $0.10 - $500.00
        std::cout << "Added symbol " << symbol_name << " with ID " << id << std::endl;
        
        // Add to matching engine
        engine.add_symbol(id);
        
        // Set to PRE_OPEN state initially
        symbol_manager.set_symbol_state(id, SymbolState::PRE_OPEN);
    }
    
    // Show all symbols
    std::cout << "\n2. Symbol Registry:" << std::endl;
    auto all_symbols = symbol_manager.get_all_symbols();
    for (const auto& info : all_symbols) {
        print_symbol_info(info);
    }
    
    std::cout << "\n3. Opening market for all symbols..." << std::endl;
    symbol_manager.open_market();
    
    // Show trading symbols
    auto trading_symbols = symbol_manager.get_trading_symbols();
    std::cout << "Trading symbols: " << trading_symbols.size() << std::endl;
    
    std::cout << "\n4. Testing order routing..." << std::endl;
    
    // Test successful orders
    auto result1 = router.route_order("AAPL", Side::BUY, OrderType::LIMIT, 100, 1500000); // $150.00
    print_routing_result(result1);
    
    auto result2 = router.route_order("MSFT", Side::SELL, OrderType::LIMIT, 200, 3000000); // $300.00
    print_routing_result(result2);
    
    auto result3 = router.route_order("GOOGL", Side::BUY, OrderType::MARKET, 50, 0);
    print_routing_result(result3);
    
    // Test error cases
    std::cout << "\n5. Testing error conditions..." << std::endl;
    
    // Invalid symbol
    auto result4 = router.route_order("INVALID", Side::BUY, OrderType::LIMIT, 100, 50000);
    print_routing_result(result4);
    
    // Close a symbol and try to trade
    symbol_manager.close_trading(*symbol_manager.get_symbol_id("TSLA"));
    auto result5 = router.route_order("TSLA", Side::BUY, OrderType::LIMIT, 100, 2000000);
    print_routing_result(result5);
    
    // Invalid price (too high)
    auto result6 = router.route_order("AAPL", Side::BUY, OrderType::LIMIT, 100, 10000000); // $1000.00 > max $500.00
    print_routing_result(result6);
    
    // Invalid quantity (too large)
    auto result7 = router.route_order("NVDA", Side::BUY, OrderType::LIMIT, 2000000, 5000000);
    print_routing_result(result7);
    
    std::cout << "\n6. Market data across symbols..." << std::endl;
    
    for (const auto& symbol_name : tech_symbols) {
        auto symbol_id = symbol_manager.get_symbol_id(symbol_name);
        if (symbol_id && symbol_manager.can_trade_symbol(*symbol_id)) {
            auto l1_data = engine.get_level1_data(*symbol_id);
            std::cout << symbol_name << " | Best Bid: $" 
                      << std::fixed << std::setprecision(4) << (l1_data.best_bid_price / 10000.0)
                      << " x " << l1_data.best_bid_quantity
                      << " | Best Ask: $" << (l1_data.best_ask_price / 10000.0)
                      << " x " << l1_data.best_ask_quantity << std::endl;
        }
    }
    
    std::cout << "\n7. Cross-symbol trading..." << std::endl;
    
    // Add liquidity to multiple symbols
    router.route_order("AAPL", Side::BUY, OrderType::LIMIT, 100, 1490000);   // $149 bid
    router.route_order("AAPL", Side::SELL, OrderType::LIMIT, 150, 1510000);  // $151 ask
    
    router.route_order("MSFT", Side::BUY, OrderType::LIMIT, 200, 2990000);   // $299 bid
    router.route_order("MSFT", Side::SELL, OrderType::LIMIT, 250, 3010000);  // $301 ask
    
    // Market orders to create trades
    router.route_order("AAPL", Side::BUY, OrderType::MARKET, 75, 0);  // Should hit ask at $151
    router.route_order("MSFT", Side::SELL, OrderType::MARKET, 100, 0); // Should hit bid at $299
    
    std::cout << "\n8. Symbol statistics after trading..." << std::endl;
    
    auto updated_symbols = symbol_manager.get_all_symbols();
    for (const auto& info : updated_symbols) {
        if (info.total_trades > 0) {
            print_symbol_info(info);
        }
    }
    
    std::cout << "\n9. Overall system statistics..." << std::endl;
    
    auto sym_stats = symbol_manager.get_stats();
    std::cout << "Symbol Manager Stats:" << std::endl;
    std::cout << "  Total Symbols: " << sym_stats.total_symbols << std::endl;
    std::cout << "  Active Symbols: " << sym_stats.active_symbols << std::endl;
    std::cout << "  Trading Symbols: " << sym_stats.trading_symbols << std::endl;
    std::cout << "  Total Volume: " << sym_stats.total_volume << std::endl;
    std::cout << "  Total Trades: " << sym_stats.total_trades << std::endl;
    
    auto routing_stats = router.get_routing_stats();
    std::cout << "\\nRouting Stats:" << std::endl;
    std::cout << "  Total Orders: " << routing_stats.total_orders << std::endl;
    std::cout << "  Routed Orders: " << routing_stats.routed_orders << std::endl;
    std::cout << "  Rejected Orders: " << routing_stats.rejected_orders << std::endl;
    std::cout << "  Invalid Symbol Orders: " << routing_stats.invalid_symbol_orders << std::endl;
    std::cout << "  Market Closed Orders: " << routing_stats.market_closed_orders << std::endl;
    
    auto engine_stats = engine.get_stats();
    std::cout << "\\nMatching Engine Stats:" << std::endl;
    std::cout << "  Orders Processed: " << engine_stats.total_orders_processed << std::endl;
    std::cout << "  Total Fills: " << engine_stats.total_fills << std::endl;
    std::cout << "  Active Orders: " << engine_stats.active_orders << std::endl;
    
    std::cout << "\n10. Testing bulk symbol loading..." << std::endl;
    
    // Load test symbols in bulk
    auto test_symbols = symbol_utils::create_test_symbols(50);
    size_t loaded = symbol_manager.load_symbols(test_symbols);
    std::cout << "Loaded " << loaded << " test symbols" << std::endl;
    
    // Open trading for test symbols
    for (size_t i = 1; i <= 10; ++i) {
        std::ostringstream oss;
        oss << "TEST" << std::setfill('0') << std::setw(3) << i;
        std::string symbol_name = oss.str();
        
        auto symbol_id = symbol_manager.get_symbol_id(symbol_name);
        if (symbol_id) {
            engine.add_symbol(*symbol_id);
            symbol_manager.set_symbol_state(*symbol_id, SymbolState::OPEN);
        }
    }
    
    std::cout << "\\nFinal system state:" << std::endl;
    auto final_stats = symbol_manager.get_stats();
    std::cout << "Total symbols in system: " << final_stats.total_symbols << std::endl;
    std::cout << "Currently trading: " << final_stats.trading_symbols << std::endl;
    
    std::cout << "\\n=== MULTI-SYMBOL TEST COMPLETED ===" << std::endl;
    
    return 0;
}
