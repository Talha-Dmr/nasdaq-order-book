#ifndef FIX_GATEWAY_HPP
#define FIX_GATEWAY_HPP

#include "fix_session.hpp"
#include "matching/matching_engine.hpp"
#include "matching/symbol_manager.hpp"
#include "market_data/publisher.hpp"
#include <unordered_map>
#include <atomic>

namespace fix {

// Order tracking for FIX client orders
struct ClientOrder {
    std::string cl_ord_id;           // Client order ID
    std::string session_id;          // Which FIX session
    matching::OrderId engine_order_id; // Order ID in matching engine
    matching::SymbolId symbol;
    double quantity;
    double filled_quantity{0};
    bool is_active{true};
    std::chrono::high_resolution_clock::time_point creation_time;
};

// Convert between FIX and matching engine types
class FixConverter {
public:
    static matching::Side convert_side(fix::Side fix_side) {
        return (fix_side == fix::Side::BUY) ? matching::Side::BUY : matching::Side::SELL;
    }
    
    static fix::Side convert_side(matching::Side engine_side) {
        return (engine_side == matching::Side::BUY) ? fix::Side::BUY : fix::Side::SELL;
    }
    
    static matching::OrderType convert_order_type(fix::OrdType fix_type) {
        return (fix_type == fix::OrdType::MARKET) ? matching::OrderType::MARKET : matching::OrderType::LIMIT;
    }
    
    static matching::TimeInForce convert_tif(fix::TimeInForce fix_tif) {
        switch (fix_tif) {
            case fix::TimeInForce::IMMEDIATE_OR_CANCEL: return matching::TimeInForce::IOC;
            case fix::TimeInForce::FILL_OR_KILL: return matching::TimeInForce::FOK;
            case fix::TimeInForce::GOOD_TILL_CANCEL: return matching::TimeInForce::GTC;
            default: return matching::TimeInForce::DAY;
        }
    }
    
    static fix::ExecType convert_exec_type(matching::OrderStatus engine_status) {
        switch (engine_status) {
            case matching::OrderStatus::NEW: return fix::ExecType::NEW;
            case matching::OrderStatus::FILLED: return fix::ExecType::FILL;
            case matching::OrderStatus::PARTIALLY_FILLED: return fix::ExecType::PARTIAL_FILL;
            case matching::OrderStatus::CANCELLED: return fix::ExecType::CANCELED;
            case matching::OrderStatus::REJECTED: return fix::ExecType::REJECTED;
            default: return fix::ExecType::NEW;
        }
    }
    
    static fix::OrdStatus convert_order_status(matching::OrderStatus engine_status) {
        switch (engine_status) {
            case matching::OrderStatus::NEW: return fix::OrdStatus::NEW;
            case matching::OrderStatus::FILLED: return fix::OrdStatus::FILLED;
            case matching::OrderStatus::PARTIALLY_FILLED: return fix::OrdStatus::PARTIALLY_FILLED;
            case matching::OrderStatus::CANCELLED: return fix::OrdStatus::CANCELED;
            case matching::OrderStatus::REJECTED: return fix::OrdStatus::REJECTED;
            default: return fix::OrdStatus::NEW;
        }
    }
};

// Main FIX Gateway class
class FixGateway {
private:
    // Core components
    matching::SymbolManager& symbol_manager_;
    matching::MatchingEngine& matching_engine_;
    market_data::MarketDataPublisher& market_data_publisher_;
    
    // FIX server
    std::unique_ptr<FixServer> fix_server_;
    int port_;
    
    // Order tracking
    std::unordered_map<std::string, ClientOrder> client_orders_; // cl_ord_id -> order
    std::unordered_map<matching::OrderId, std::string> engine_to_client_orders_; // engine_id -> cl_ord_id
    std::mutex orders_mutex_;
    
    // Execution ID generation
    std::atomic<uint64_t> next_exec_id_{1};
    
    // Session tracking
    std::unordered_map<std::string, std::shared_ptr<FixSession>> active_sessions_;
    std::mutex sessions_mutex_;
    
    // Statistics
    struct Stats {
        uint64_t orders_received{0};
        uint64_t orders_accepted{0};
        uint64_t orders_rejected{0};
        uint64_t executions_sent{0};
        uint64_t active_sessions{0};
        uint64_t total_volume{0};
    } stats_;
    std::mutex stats_mutex_;
    
public:
    FixGateway(matching::SymbolManager& symbol_manager, 
               matching::MatchingEngine& matching_engine,
               market_data::MarketDataPublisher& market_data_publisher,
               int port = 9878);
    
    ~FixGateway();
    
    // Gateway lifecycle
    bool start();
    void stop();
    bool is_running() const;
    
    // Statistics
    Stats get_stats() const;
    void reset_stats();
    
    // Session management
    std::vector<std::string> get_active_session_ids() const;
    
private:
    // FIX message handlers
    void handle_new_session(std::shared_ptr<FixSession> session);
    void handle_fix_message(FixSession* session, const FixMessage& message);
    void handle_logon(FixSession* session, const FixMessage& message);
    void handle_new_order_single(FixSession* session, const FixMessage& message);
    void handle_order_cancel_request(FixSession* session, const FixMessage& message);
    void handle_market_data_request(FixSession* session, const FixMessage& message);
    
    // Order management
    std::string process_new_order(FixSession* session, const FixMessage& message);
    void send_execution_report(const std::string& session_id, const ClientOrder& client_order,
                              fix::ExecType exec_type, fix::OrdStatus ord_status,
                              double last_shares = 0, double last_px = 0);
    void send_order_reject(FixSession* session, const std::string& cl_ord_id, const std::string& reason);
    
    // Fill callback from matching engine
    void on_fill(const matching::Fill& fill);
    
    // Utility methods
    std::string generate_exec_id();
    std::string session_key(FixSession* session);
    matching::SymbolId resolve_symbol(const std::string& symbol_name);
    void update_order_stats(bool accepted);
    
    // Market data forwarding
    void setup_market_data_subscriptions();
    void forward_market_data_to_fix_clients();
};

} // namespace fix

#endif // FIX_GATEWAY_HPP
