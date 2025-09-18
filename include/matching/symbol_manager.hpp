#ifndef SYMBOL_MANAGER_HPP
#define SYMBOL_MANAGER_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>
#include <optional>
#include "matching/matching_engine.hpp"

namespace matching {

// Symbol states for lifecycle management
enum class SymbolState : uint8_t {
    INACTIVE = 0,        // Symbol not trading
    PRE_OPEN = 1,        // Pre-market phase
    OPEN = 2,            // Normal trading
    HALTED = 3,          // Trading halted
    CLOSED = 4,          // Market closed
    SUSPENDED = 5        // Trading suspended
};

// Symbol metadata
struct SymbolInfo {
    SymbolId id{0};
    std::string symbol_name;     // e.g., "AAPL", "GOOGL"
    SymbolState state{SymbolState::INACTIVE};
    
    // Trading parameters
    uint32_t tick_size{1};       // Minimum price increment (in 0.0001 units)
    uint32_t min_quantity{1};    // Minimum order size
    uint32_t max_quantity{1000000}; // Maximum order size
    uint32_t lot_size{100};      // Round lot size
    
    // Price limits (circuit breakers)
    uint32_t min_price{1000};    // $0.10 minimum
    uint32_t max_price{999999};  // $99.99 maximum
    
    // Timestamps
    std::chrono::system_clock::time_point created_time;
    std::chrono::system_clock::time_point last_updated;
    
    // Trading session times (simplified for MVP)
    struct TradingSession {
        std::chrono::system_clock::time_point market_open;
        std::chrono::system_clock::time_point market_close;
    } session;
    
    // Statistics
    uint64_t total_volume{0};
    uint32_t total_trades{0};
    uint32_t active_orders{0};
    
    bool is_trading() const {
        return state == SymbolState::OPEN;
    }
    
    bool accepts_orders() const {
        return state == SymbolState::PRE_OPEN || state == SymbolState::OPEN;
    }
};

// Symbol routing and management
class SymbolManager {
private:
    // Symbol registry
    std::unordered_map<std::string, SymbolId> name_to_id_;
    std::unordered_map<SymbolId, SymbolInfo> symbols_;
    
    // Atomic counter for symbol ID generation
    std::atomic<SymbolId> next_symbol_id_{1};
    
    // Thread safety
    mutable std::shared_mutex mutex_;
    
    // Statistics
    struct Stats {
        uint32_t total_symbols{0};
        uint32_t active_symbols{0};
        uint32_t trading_symbols{0};
        uint64_t total_volume{0};
        uint32_t total_trades{0};
    } stats_;

public:
    SymbolManager() = default;
    ~SymbolManager() = default;
    
    // Symbol registration and lifecycle
    SymbolId add_symbol(const std::string& symbol_name, 
                       uint32_t tick_size = 1, 
                       uint32_t min_price = 1000,
                       uint32_t max_price = 999999);
    
    bool remove_symbol(const std::string& symbol_name);
    bool remove_symbol(SymbolId symbol_id);
    
    // Symbol state management
    bool set_symbol_state(SymbolId symbol_id, SymbolState state);
    bool set_symbol_state(const std::string& symbol_name, SymbolState state);
    
    // Open/close market for symbol
    bool open_trading(SymbolId symbol_id);
    bool close_trading(SymbolId symbol_id);
    bool halt_trading(SymbolId symbol_id, const std::string& reason = "");
    bool resume_trading(SymbolId symbol_id);
    
    // Symbol lookup
    std::optional<SymbolId> get_symbol_id(const std::string& symbol_name) const;
    std::optional<std::string> get_symbol_name(SymbolId symbol_id) const;
    std::optional<SymbolInfo> get_symbol_info(SymbolId symbol_id) const;
    std::optional<SymbolInfo> get_symbol_info(const std::string& symbol_name) const;
    
    // Symbol validation
    bool is_valid_symbol(SymbolId symbol_id) const;
    bool is_valid_symbol(const std::string& symbol_name) const;
    bool can_trade_symbol(SymbolId symbol_id) const;
    
    // Order validation helpers
    bool validate_price(SymbolId symbol_id, uint32_t price) const;
    bool validate_quantity(SymbolId symbol_id, uint32_t quantity) const;
    uint32_t round_to_tick(SymbolId symbol_id, uint32_t price) const;
    uint32_t round_to_lot(SymbolId symbol_id, uint32_t quantity) const;
    
    // Bulk operations
    std::vector<SymbolInfo> get_all_symbols() const;
    std::vector<SymbolInfo> get_trading_symbols() const;
    std::vector<SymbolInfo> get_symbols_by_state(SymbolState state) const;
    
    // Statistics and monitoring
    void update_symbol_stats(SymbolId symbol_id, uint32_t volume, bool is_trade = false);
    Stats get_stats() const;
    
    // Market hours management (simplified)
    void open_market();
    void close_market();
    bool is_market_open() const;
    
    // Batch symbol loading (for startup)
    struct SymbolConfig {
        std::string name;
        uint32_t tick_size{1};
        uint32_t min_price{1000};
        uint32_t max_price{999999};
        SymbolState initial_state{SymbolState::INACTIVE};
    };
    
    size_t load_symbols(const std::vector<SymbolConfig>& symbols);
    
    // Export/import for persistence (basic)
    std::vector<SymbolConfig> export_symbol_configs() const;
    
private:
    void update_stats();
};

// Multi-Symbol Order Router
class SymbolRouter {
private:
    SymbolManager& symbol_manager_;
    MatchingEngine& matching_engine_;
    
    // Routing statistics
    struct RoutingStats {
        uint64_t total_orders{0};
        uint64_t routed_orders{0};
        uint64_t rejected_orders{0};
        uint64_t invalid_symbol_orders{0};
        uint64_t market_closed_orders{0};
    } stats_;
    
public:
    SymbolRouter(SymbolManager& sym_mgr, MatchingEngine& engine)
        : symbol_manager_(sym_mgr), matching_engine_(engine) {}
    
    // Route order to appropriate symbol
    enum class RouteResult {
        SUCCESS,
        INVALID_SYMBOL,
        MARKET_CLOSED,
        INVALID_PRICE,
        INVALID_QUANTITY,
        REJECTED
    };
    
    struct RoutedOrder {
        Order order;
        RouteResult result;
        std::string error_message;
    };
    
    RoutedOrder route_order(Order order);
    RoutedOrder route_order(const std::string& symbol_name, 
                          Side side, 
                          OrderType type,
                          uint32_t quantity,
                          uint32_t price = 0,
                          TimeInForce tif = TimeInForce::DAY);
    
    // Bulk routing for market data feeds
    std::vector<RoutedOrder> route_orders(const std::vector<Order>& orders);
    
    // Cancel/replace routing
    bool route_cancel(OrderId order_id);
    RoutedOrder route_replace(OrderId old_id, const Order& new_order);
    
    // Statistics
    const RoutingStats& get_routing_stats() const { return stats_; }
    void reset_stats() { stats_ = {}; }
};

// Utility functions for symbol management
namespace symbol_utils {
    // Parse symbol from various formats
    std::string normalize_symbol(const std::string& symbol);
    bool is_valid_symbol_format(const std::string& symbol);
    
    // Generate test symbols
    std::vector<SymbolManager::SymbolConfig> create_test_symbols(size_t count = 100);
    
    // Common symbol lists
    std::vector<std::string> get_sp500_symbols();
    std::vector<std::string> get_nasdaq_symbols();
}

} // namespace matching

#endif // SYMBOL_MANAGER_HPP
