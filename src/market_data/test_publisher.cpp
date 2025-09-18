#include <iostream>
#include <chrono>
#include <thread>
#include <random>
#include <iomanip>

#include "market_data/publisher.hpp"
#include "matching/matching_engine.hpp"
#include "matching/symbol_manager.hpp"

using namespace std;
using namespace market_data;
using namespace matching;

class TestStrategy : public MarketDataSubscriber {
private:
    std::string id_;
    SymbolManager& symbol_manager_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> price_dist_;
    std::uniform_int_distribution<uint32_t> qty_dist_;
    
public:
    TestStrategy(const std::string& id, SymbolManager& sym_mgr) 
        : id_(id), symbol_manager_(sym_mgr), rng_(std::random_device{}()),
          price_dist_(4.90, 5.10), qty_dist_(100, 1000) {}
    
    std::string get_subscriber_id() const override {
        return id_;
    }
    
    void on_market_data(const MarketDataMessage& message) override {
        switch (message.type) {
            case MessageType::LEVEL1_UPDATE: {
                const auto& l1 = message.data.level1;
                auto symbol_name = symbol_manager_.get_symbol_name(l1.symbol).value_or("UNKNOWN");
                
                cout << "[STRATEGY-" << id_ << "] Level 1 for " << symbol_name 
                     << " - Bid: $" << fixed << setprecision(4) << (l1.best_bid_price / 10000.0)
                     << " x " << l1.best_bid_quantity
                     << ", Ask: $" << (l1.best_ask_price / 10000.0) 
                     << " x " << l1.best_ask_quantity;
                if (l1.has_bid() && l1.has_ask()) {
                    cout << ", Spread: $" << (l1.spread() / 10000.0);
                }
                cout << endl;
                break;
            }
            
            case MessageType::TRADE_REPORT: {
                const auto& trade = message.data.trade;
                auto symbol_name = symbol_manager_.get_symbol_name(trade.symbol).value_or("UNKNOWN");
                cout << "[STRATEGY-" << id_ << "] Trade in " << symbol_name 
                     << " - Price: $" << fixed << setprecision(4) << (trade.execution_price / 10000.0)
                     << ", Qty: " << trade.execution_quantity
                     << ", ID: " << trade.trade_id << endl;
                break;
            }
            
            default:
                break;
        }
    }
    
    void on_subscription_status(SymbolId symbol, MessageType type, bool active) override {
        auto symbol_name = symbol_manager_.get_symbol_name(symbol).value_or("ALL");
        cout << "[STRATEGY-" << id_ << "] Subscription " << (active ? "ACTIVE" : "INACTIVE")
             << " for " << symbol_name << ", type: " << static_cast<int>(type) << endl;
    }
    
    // Generate random orders for testing
    matching::Order generate_buy_order(SymbolId symbol, OrderId order_id) {
        matching::Order order;
        order.id = order_id;
        order.symbol = symbol;
        order.side = Side::BUY;
        order.type = OrderType::LIMIT;
        order.tif = TimeInForce::DAY;
        order.price = static_cast<uint32_t>(price_dist_(rng_) * 10000); // Convert to fixed-point
        order.quantity = qty_dist_(rng_);
        order.timestamp = std::chrono::high_resolution_clock::now();
        return order;
    }
    
    matching::Order generate_sell_order(SymbolId symbol, OrderId order_id) {
        matching::Order order;
        order.id = order_id;
        order.symbol = symbol;
        order.side = Side::SELL;
        order.type = OrderType::LIMIT;
        order.tif = TimeInForce::DAY;
        order.price = static_cast<uint32_t>(price_dist_(rng_) * 10000); // Convert to fixed-point
        order.quantity = qty_dist_(rng_);
        order.timestamp = std::chrono::high_resolution_clock::now();
        return order;
    }
};

void simulate_trading(MatchingEngine& engine, MarketDataPublisher& publisher, SymbolId symbol) {
    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> action_dist(0, 3); // 0=buy, 1=sell, 2=market_buy, 3=market_sell
    std::uniform_real_distribution<double> price_dist(4.90, 5.10);
    std::uniform_int_distribution<uint32_t> qty_dist(100, 1000);
    
    OrderId order_id = 10000;
    
    for (int i = 0; i < 20; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        int action = action_dist(rng);
        matching::Order order;
        order.id = ++order_id;
        order.symbol = symbol;
        order.quantity = qty_dist(rng);
        order.timestamp = std::chrono::high_resolution_clock::now();
        order.tif = TimeInForce::DAY;
        
        switch (action) {
            case 0: // Limit Buy
                order.side = Side::BUY;
                order.type = OrderType::LIMIT;
                order.price = static_cast<uint32_t>(price_dist(rng) * 10000);
                break;
            case 1: // Limit Sell
                order.side = Side::SELL;
                order.type = OrderType::LIMIT;
                order.price = static_cast<uint32_t>(price_dist(rng) * 10000);
                break;
            case 2: // Market Buy
                order.side = Side::BUY;
                order.type = OrderType::MARKET;
                order.price = 0;
                break;
            case 3: // Market Sell
                order.side = Side::SELL;
                order.type = OrderType::MARKET;
                order.price = 0;
                break;
        }
        
        cout << "\\n[SIMULATOR] Submitting " 
             << (order.type == OrderType::MARKET ? "MARKET" : "LIMIT") << " "
             << (order.side == Side::BUY ? "BUY" : "SELL")
             << " order: " << order.quantity << " shares";
        if (order.type == OrderType::LIMIT) {
            cout << " at $" << fixed << setprecision(4) << (order.price / 10000.0);
        }
        cout << " (ID: " << order.id << ")" << endl;
        
        // Submit order to matching engine
        auto result = engine.process_order(order);
        cout << "[SIMULATOR] Order " << order.id << " result: " 
             << static_cast<int>(result.final_status) << ", fills: " << result.fills.size() << endl;
        
        // Trigger market data updates
        publisher.publish_level1_update(symbol);
        
        // Publish trade reports for any fills
        for (const auto& fill : result.fills) {
            publisher.publish_trade(fill);
        }
        
        if (i % 5 == 0) {
            // Occasional Level 2 updates
            publisher.publish_level2_update(symbol);
        }
    }
}

int main() {
    cout << "=== Market Data Publisher Test ===" << endl;
    
    // Initialize components
    SymbolManager symbol_manager;
    MatchingEngine matching_engine;
    MarketDataPublisher publisher(symbol_manager, matching_engine);
    
    // Add test symbols
    auto aapl_id = symbol_manager.add_symbol("AAPL");
    auto msft_id = symbol_manager.add_symbol("MSFT");
    
    if (aapl_id == 0 || msft_id == 0) {
        cerr << "Failed to add symbols" << endl;
        return 1;
    }
    
    cout << "Added symbols: AAPL=" << aapl_id << ", MSFT=" << msft_id << endl;
    
    // Create subscribers
    auto console_sub = std::make_shared<ConsoleSubscriber>("console", symbol_manager, false); // verbose=false
    auto strategy1 = std::make_shared<TestStrategy>("strategy1", symbol_manager);
    auto strategy2 = std::make_shared<TestStrategy>("strategy2", symbol_manager);
    auto file_recorder = std::make_shared<FileRecorder>("recorder", "market_data.csv");
    
    // Start publisher
    if (!publisher.start()) {
        cerr << "Failed to start publisher" << endl;
        return 1;
    }
    cout << "Market data publisher started" << endl;
    
    // Add subscribers
    publisher.add_subscriber(console_sub);
    publisher.add_subscriber(strategy1);
    publisher.add_subscriber(strategy2);
    publisher.add_subscriber(file_recorder);
    cout << "Added " << publisher.get_subscriber_ids().size() << " subscribers" << endl;
    
    // Subscribe console to all messages for AAPL
    publisher.subscribe("console", aapl_id, MessageType::LEVEL1_UPDATE);
    publisher.subscribe("console", aapl_id, MessageType::TRADE_REPORT);
    publisher.subscribe("console", aapl_id, MessageType::LEVEL2_UPDATE, 5); // 5 levels
    
    // Subscribe strategies to Level 1 and trades for all symbols
    publisher.subscribe_all_symbols("strategy1", MessageType::LEVEL1_UPDATE);
    publisher.subscribe_all_symbols("strategy1", MessageType::TRADE_REPORT);
    
    publisher.subscribe("strategy2", msft_id, MessageType::LEVEL1_UPDATE);
    publisher.subscribe("strategy2", msft_id, MessageType::TRADE_REPORT);
    
    // Subscribe file recorder to all messages for all symbols
    publisher.subscribe_all_symbols("recorder", MessageType::LEVEL1_UPDATE);
    publisher.subscribe_all_symbols("recorder", MessageType::TRADE_REPORT);
    
    cout << "\\nSet up subscriptions. Starting simulation..." << endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Send initial snapshots
    publisher.send_level1_snapshot("console", aapl_id);
    publisher.send_level2_snapshot("console", aapl_id, 10);
    
    // Add some initial orders to create market structure
    cout << "\\n--- Setting up initial market ---" << endl;
    
    // Add some resting bids and asks for AAPL
    for (int i = 0; i < 5; ++i) {
        matching::Order bid;
        bid.id = 1000 + i;
        bid.symbol = aapl_id;
        bid.side = Side::BUY;
        bid.type = OrderType::LIMIT;
        bid.tif = TimeInForce::DAY;
        bid.price = static_cast<uint32_t>((4.98 - i * 0.01) * 10000);
        bid.quantity = 500 + i * 100;
        bid.timestamp = std::chrono::high_resolution_clock::now();
        
        auto result = matching_engine.process_order(bid);
        cout << "Added resting bid: $" << fixed << setprecision(4) << (bid.price / 10000.0) 
             << " x " << bid.quantity << " (ID: " << bid.id << ")" << endl;
    }
    
    for (int i = 0; i < 5; ++i) {
        matching::Order ask;
        ask.id = 2000 + i;
        ask.symbol = aapl_id;
        ask.side = Side::SELL;
        ask.type = OrderType::LIMIT;
        ask.tif = TimeInForce::DAY;
        ask.price = static_cast<uint32_t>((5.02 + i * 0.01) * 10000);
        ask.quantity = 500 + i * 100;
        ask.timestamp = std::chrono::high_resolution_clock::now();
        
        auto result = matching_engine.process_order(ask);
        cout << "Added resting ask: $" << fixed << setprecision(4) << (ask.price / 10000.0)
             << " x " << ask.quantity << " (ID: " << ask.id << ")" << endl;
    }
    
    // Publish initial market state
    publisher.publish_level1_update(aapl_id);
    publisher.publish_level2_update(aapl_id);
    
    cout << "\\n--- Starting live trading simulation ---" << endl;
    
    // Run simulation in separate thread
    std::thread simulator(simulate_trading, std::ref(matching_engine), std::ref(publisher), aapl_id);
    
    // Let simulation run
    simulator.join();
    
    cout << "\\n--- Simulation completed ---" << endl;
    
    // Show final statistics
    auto stats = publisher.get_stats();
    cout << "\\nMarket Data Publisher Statistics:" << endl;
    cout << "  Total Messages: " << stats.total_messages << endl;
    cout << "  Level 1 Messages: " << stats.level1_messages << endl;
    cout << "  Level 2 Messages: " << stats.level2_messages << endl;
    cout << "  Trade Messages: " << stats.trade_messages << endl;
    cout << "  Status Messages: " << stats.status_messages << endl;
    cout << "  Dropped Messages: " << stats.dropped_messages << endl;
    cout << "  Subscribers: " << stats.subscribers << endl;
    
    cout << "\\nFinal market state:" << endl;
    publisher.send_level1_snapshot("console", aapl_id);
    publisher.send_level2_snapshot("console", aapl_id, 10);
    
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    // Stop publisher
    publisher.stop();
    cout << "\\nMarket data publisher stopped" << endl;
    
    cout << "\\nMarket data recorded to 'market_data.csv'" << endl;
    
    return 0;
}
