#include "matching/symbol_manager.hpp"
#include <algorithm>
#include <shared_mutex>
#include <sstream>
#include <cctype>
#include <iomanip>

namespace matching {

// SymbolManager Implementation

SymbolId SymbolManager::add_symbol(const std::string& symbol_name, 
                                  uint32_t tick_size, 
                                  uint32_t min_price,
                                  uint32_t max_price) {
    std::unique_lock lock(mutex_);
    
    // Check if symbol already exists
    auto it = name_to_id_.find(symbol_name);
    if (it != name_to_id_.end()) {
        return it->second; // Return existing ID
    }
    
    // Create new symbol
    SymbolId new_id = next_symbol_id_.fetch_add(1);
    
    SymbolInfo info;
    info.id = new_id;
    info.symbol_name = symbol_name;
    info.state = SymbolState::INACTIVE;
    info.tick_size = tick_size;
    info.min_price = min_price;
    info.max_price = max_price;
    info.created_time = std::chrono::system_clock::now();
    info.last_updated = info.created_time;
    
    // Register symbol
    symbols_[new_id] = info;
    name_to_id_[symbol_name] = new_id;
    
    update_stats();
    
    return new_id;
}

bool SymbolManager::remove_symbol(const std::string& symbol_name) {
    std::unique_lock lock(mutex_);
    
    auto name_it = name_to_id_.find(symbol_name);
    if (name_it == name_to_id_.end()) {
        return false;
    }
    
    SymbolId symbol_id = name_it->second;
    
    // Remove from both maps
    symbols_.erase(symbol_id);
    name_to_id_.erase(name_it);
    
    update_stats();
    return true;
}

bool SymbolManager::remove_symbol(SymbolId symbol_id) {
    std::unique_lock lock(mutex_);
    
    auto symbol_it = symbols_.find(symbol_id);
    if (symbol_it == symbols_.end()) {
        return false;
    }
    
    const std::string& symbol_name = symbol_it->second.symbol_name;
    
    // Remove from both maps
    name_to_id_.erase(symbol_name);
    symbols_.erase(symbol_it);
    
    update_stats();
    return true;
}

bool SymbolManager::set_symbol_state(SymbolId symbol_id, SymbolState state) {
    std::unique_lock lock(mutex_);
    
    auto it = symbols_.find(symbol_id);
    if (it == symbols_.end()) {
        return false;
    }
    
    it->second.state = state;
    it->second.last_updated = std::chrono::system_clock::now();
    
    update_stats();
    return true;
}

bool SymbolManager::set_symbol_state(const std::string& symbol_name, SymbolState state) {
    auto symbol_id = get_symbol_id(symbol_name);
    if (!symbol_id) {
        return false;
    }
    return set_symbol_state(*symbol_id, state);
}

bool SymbolManager::open_trading(SymbolId symbol_id) {
    return set_symbol_state(symbol_id, SymbolState::OPEN);
}

bool SymbolManager::close_trading(SymbolId symbol_id) {
    return set_symbol_state(symbol_id, SymbolState::CLOSED);
}

bool SymbolManager::halt_trading(SymbolId symbol_id, const std::string& reason) {
    // In a real system, would log the halt reason
    return set_symbol_state(symbol_id, SymbolState::HALTED);
}

bool SymbolManager::resume_trading(SymbolId symbol_id) {
    return set_symbol_state(symbol_id, SymbolState::OPEN);
}

std::optional<SymbolId> SymbolManager::get_symbol_id(const std::string& symbol_name) const {
    std::shared_lock lock(mutex_);
    
    auto it = name_to_id_.find(symbol_name);
    if (it != name_to_id_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<std::string> SymbolManager::get_symbol_name(SymbolId symbol_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        return it->second.symbol_name;
    }
    return std::nullopt;
}

std::optional<SymbolInfo> SymbolManager::get_symbol_info(SymbolId symbol_id) const {
    std::shared_lock lock(mutex_);
    
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<SymbolInfo> SymbolManager::get_symbol_info(const std::string& symbol_name) const {
    auto symbol_id = get_symbol_id(symbol_name);
    if (symbol_id) {
        return get_symbol_info(*symbol_id);
    }
    return std::nullopt;
}

bool SymbolManager::is_valid_symbol(SymbolId symbol_id) const {
    std::shared_lock lock(mutex_);
    return symbols_.find(symbol_id) != symbols_.end();
}

bool SymbolManager::is_valid_symbol(const std::string& symbol_name) const {
    std::shared_lock lock(mutex_);
    return name_to_id_.find(symbol_name) != name_to_id_.end();
}

bool SymbolManager::can_trade_symbol(SymbolId symbol_id) const {
    auto info = get_symbol_info(symbol_id);
    return info && info->accepts_orders();
}

bool SymbolManager::validate_price(SymbolId symbol_id, uint32_t price) const {
    auto info = get_symbol_info(symbol_id);
    if (!info) return false;
    
    return price >= info->min_price && 
           price <= info->max_price &&
           (price % info->tick_size) == 0;
}

bool SymbolManager::validate_quantity(SymbolId symbol_id, uint32_t quantity) const {
    auto info = get_symbol_info(symbol_id);
    if (!info) return false;
    
    return quantity >= info->min_quantity && quantity <= info->max_quantity;
}

uint32_t SymbolManager::round_to_tick(SymbolId symbol_id, uint32_t price) const {
    auto info = get_symbol_info(symbol_id);
    if (!info) return price;
    
    return (price / info->tick_size) * info->tick_size;
}

uint32_t SymbolManager::round_to_lot(SymbolId symbol_id, uint32_t quantity) const {
    auto info = get_symbol_info(symbol_id);
    if (!info) return quantity;
    
    return (quantity / info->lot_size) * info->lot_size;
}

std::vector<SymbolInfo> SymbolManager::get_all_symbols() const {
    std::shared_lock lock(mutex_);
    
    std::vector<SymbolInfo> result;
    result.reserve(symbols_.size());
    
    for (const auto& [id, info] : symbols_) {
        result.push_back(info);
    }
    
    return result;
}

std::vector<SymbolInfo> SymbolManager::get_trading_symbols() const {
    return get_symbols_by_state(SymbolState::OPEN);
}

std::vector<SymbolInfo> SymbolManager::get_symbols_by_state(SymbolState state) const {
    std::shared_lock lock(mutex_);
    
    std::vector<SymbolInfo> result;
    
    for (const auto& [id, info] : symbols_) {
        if (info.state == state) {
            result.push_back(info);
        }
    }
    
    return result;
}

void SymbolManager::update_symbol_stats(SymbolId symbol_id, uint32_t volume, bool is_trade) {
    std::unique_lock lock(mutex_);
    
    auto it = symbols_.find(symbol_id);
    if (it != symbols_.end()) {
        it->second.total_volume += volume;
        if (is_trade) {
            it->second.total_trades++;
        }
        it->second.last_updated = std::chrono::system_clock::now();
    }
}

SymbolManager::Stats SymbolManager::get_stats() const {
    std::shared_lock lock(mutex_);
    return stats_;
}

void SymbolManager::open_market() {
    std::unique_lock lock(mutex_);
    
    // Open all symbols that are in PRE_OPEN state
    for (auto& [id, info] : symbols_) {
        if (info.state == SymbolState::PRE_OPEN) {
            info.state = SymbolState::OPEN;
            info.last_updated = std::chrono::system_clock::now();
        }
    }
    
    update_stats();
}

void SymbolManager::close_market() {
    std::unique_lock lock(mutex_);
    
    // Close all trading symbols
    for (auto& [id, info] : symbols_) {
        if (info.state == SymbolState::OPEN || info.state == SymbolState::PRE_OPEN) {
            info.state = SymbolState::CLOSED;
            info.last_updated = std::chrono::system_clock::now();
        }
    }
    
    update_stats();
}

bool SymbolManager::is_market_open() const {
    // Simplified: market is open if any symbol is trading
    auto trading_symbols = get_trading_symbols();
    return !trading_symbols.empty();
}

size_t SymbolManager::load_symbols(const std::vector<SymbolConfig>& symbols) {
    size_t loaded = 0;
    
    for (const auto& config : symbols) {
        SymbolId id = add_symbol(config.name, config.tick_size, config.min_price, config.max_price);
        if (id > 0) {
            set_symbol_state(id, config.initial_state);
            loaded++;
        }
    }
    
    return loaded;
}

std::vector<SymbolManager::SymbolConfig> SymbolManager::export_symbol_configs() const {
    std::vector<SymbolConfig> configs;
    auto symbols = get_all_symbols();
    
    configs.reserve(symbols.size());
    
    for (const auto& info : symbols) {
        SymbolConfig config;
        config.name = info.symbol_name;
        config.tick_size = info.tick_size;
        config.min_price = info.min_price;
        config.max_price = info.max_price;
        config.initial_state = info.state;
        
        configs.push_back(config);
    }
    
    return configs;
}

void SymbolManager::update_stats() {
    // Called while holding unique lock
    stats_.total_symbols = symbols_.size();
    stats_.active_symbols = 0;
    stats_.trading_symbols = 0;
    stats_.total_volume = 0;
    stats_.total_trades = 0;
    
    for (const auto& [id, info] : symbols_) {
        if (info.accepts_orders()) {
            stats_.active_symbols++;
        }
        if (info.is_trading()) {
            stats_.trading_symbols++;
        }
        stats_.total_volume += info.total_volume;
        stats_.total_trades += info.total_trades;
    }
}

// SymbolRouter Implementation

SymbolRouter::RoutedOrder SymbolRouter::route_order(Order order) {
    RoutedOrder result;
    result.order = order;
    result.result = RouteResult::SUCCESS;
    
    stats_.total_orders++;
    
    // Validate symbol
    if (!symbol_manager_.is_valid_symbol(order.symbol)) {
        result.result = RouteResult::INVALID_SYMBOL;
        result.error_message = "Invalid symbol ID: " + std::to_string(order.symbol);
        stats_.invalid_symbol_orders++;
        stats_.rejected_orders++;
        return result;
    }
    
    // Check if symbol accepts orders
    if (!symbol_manager_.can_trade_symbol(order.symbol)) {
        result.result = RouteResult::MARKET_CLOSED;
        result.error_message = "Market closed for symbol";
        stats_.market_closed_orders++;
        stats_.rejected_orders++;
        return result;
    }
    
    // Validate price (for limit orders)
    if (order.type == OrderType::LIMIT && 
        !symbol_manager_.validate_price(order.symbol, order.price)) {
        result.result = RouteResult::INVALID_PRICE;
        result.error_message = "Invalid price for symbol";
        stats_.rejected_orders++;
        return result;
    }
    
    // Validate quantity
    if (!symbol_manager_.validate_quantity(order.symbol, order.quantity)) {
        result.result = RouteResult::INVALID_QUANTITY;
        result.error_message = "Invalid quantity for symbol";
        stats_.rejected_orders++;
        return result;
    }
    
    // Round price and quantity to symbol specifications
    if (order.type == OrderType::LIMIT) {
        result.order.price = symbol_manager_.round_to_tick(order.symbol, order.price);
    }
    result.order.quantity = symbol_manager_.round_to_lot(order.symbol, result.order.quantity);
    
    // Route to matching engine
    try {
        MatchResult match_result = matching_engine_.process_order(result.order);
        
        // Update symbol statistics
        if (match_result.has_fills()) {
            for (const auto& fill : match_result.fills) {
                symbol_manager_.update_symbol_stats(order.symbol, fill.execution_quantity, true);
            }
        }
        
        stats_.routed_orders++;
    }
    catch (const std::exception& e) {
        result.result = RouteResult::REJECTED;
        result.error_message = "Matching engine error: " + std::string(e.what());
        stats_.rejected_orders++;
    }
    
    return result;
}

SymbolRouter::RoutedOrder SymbolRouter::route_order(const std::string& symbol_name, 
                                                   Side side, 
                                                   OrderType type,
                                                   uint32_t quantity,
                                                   uint32_t price,
                                                   TimeInForce tif) {
    // Get symbol ID
    auto symbol_id = symbol_manager_.get_symbol_id(symbol_name);
    if (!symbol_id) {
        RoutedOrder result;
        result.result = RouteResult::INVALID_SYMBOL;
        result.error_message = "Unknown symbol: " + symbol_name;
        stats_.invalid_symbol_orders++;
        stats_.rejected_orders++;
        return result;
    }
    
    // Create order with generated ID
    static std::atomic<OrderId> order_id_counter{10000};
    
    Order order;
    order.id = order_id_counter.fetch_add(1);
    order.symbol = *symbol_id;
    order.side = side;
    order.type = type;
    order.quantity = quantity;
    order.price = price;
    order.tif = tif;
    
    return route_order(order);
}

std::vector<SymbolRouter::RoutedOrder> SymbolRouter::route_orders(const std::vector<Order>& orders) {
    std::vector<RoutedOrder> results;
    results.reserve(orders.size());
    
    for (const auto& order : orders) {
        results.push_back(route_order(order));
    }
    
    return results;
}

bool SymbolRouter::route_cancel(OrderId order_id) {
    // Simple passthrough to matching engine
    return matching_engine_.cancel_order(order_id);
}

SymbolRouter::RoutedOrder SymbolRouter::route_replace(OrderId old_id, const Order& new_order) {
    // Route the replacement order
    RoutedOrder routed = route_order(new_order);
    
    if (routed.result == RouteResult::SUCCESS) {
        // If successful, cancel the old order
        matching_engine_.cancel_order(old_id);
    }
    
    return routed;
}

// Utility Functions Implementation

namespace symbol_utils {

std::string normalize_symbol(const std::string& symbol) {
    std::string result = symbol;
    
    // Convert to uppercase
    std::transform(result.begin(), result.end(), result.begin(), ::toupper);
    
    // Remove whitespace
    result.erase(std::remove_if(result.begin(), result.end(), ::isspace), result.end());
    
    return result;
}

bool is_valid_symbol_format(const std::string& symbol) {
    if (symbol.empty() || symbol.length() > 8) {
        return false;
    }
    
    // Check if all characters are alphanumeric
    return std::all_of(symbol.begin(), symbol.end(), [](char c) {
        return std::isalnum(c) || c == '.';
    });
}

std::vector<SymbolManager::SymbolConfig> create_test_symbols(size_t count) {
    std::vector<SymbolManager::SymbolConfig> symbols;
    symbols.reserve(count);
    
    // Create test symbols TEST001, TEST002, etc.
    for (size_t i = 1; i <= count; ++i) {
        SymbolManager::SymbolConfig config;
        
        std::ostringstream oss;
        oss << "TEST" << std::setfill('0') << std::setw(3) << i;
        config.name = oss.str();
        
        config.tick_size = 1;  // $0.0001
        config.min_price = 1000;  // $0.10
        config.max_price = 1000000;  // $100.00
        config.initial_state = SymbolState::INACTIVE;
        
        symbols.push_back(config);
    }
    
    return symbols;
}

std::vector<std::string> get_sp500_symbols() {
    // Simplified list of major S&P 500 symbols for MVP
    return {
        "AAPL", "MSFT", "AMZN", "GOOGL", "TSLA", "META", "NVDA", "JPM", "JNJ", "V",
        "PG", "UNH", "HD", "MA", "PYPL", "DIS", "ADBE", "NFLX", "CRM", "CMCSA"
    };
}

std::vector<std::string> get_nasdaq_symbols() {
    // Simplified list of major NASDAQ symbols
    return {
        "AAPL", "MSFT", "AMZN", "GOOGL", "GOOG", "TSLA", "META", "NVDA", "NFLX", "ADBE",
        "PYPL", "INTC", "CSCO", "PEP", "COST", "CMCSA", "TMUS", "AVGO", "TXN", "QCOM"
    };
}

} // namespace symbol_utils

} // namespace matching
