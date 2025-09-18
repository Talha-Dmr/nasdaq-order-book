#ifndef MARKET_DATA_PUBLISHER_HPP
#define MARKET_DATA_PUBLISHER_HPP

#include "matching/matching_engine.hpp"
#include "matching/symbol_manager.hpp"
#include <functional>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>
#include <fstream>
#include <iomanip>

namespace market_data {

using namespace matching;

// Market data message types
enum class MessageType : uint8_t {
    LEVEL1_UPDATE = 1,    // Best Bid/Offer update
    LEVEL2_UPDATE = 2,    // Market depth update
    TRADE_REPORT = 3,     // Trade execution report
    SYMBOL_STATUS = 4,    // Symbol status change
    SNAPSHOT_L1 = 5,      // Level 1 snapshot
    SNAPSHOT_L2 = 6       // Level 2 snapshot
};

// Level 1 market data update
struct Level1Update {
    SymbolId symbol{0};
    Price best_bid_price{0};
    Quantity best_bid_quantity{0};
    Price best_ask_price{0};
    Quantity best_ask_quantity{0};
    uint64_t sequence_number{0};
    std::chrono::high_resolution_clock::time_point timestamp{};
    
    bool has_bid() const { return best_bid_price > 0; }
    bool has_ask() const { return best_ask_price > 0; }
    Price spread() const {
        return (has_bid() && has_ask()) ? (best_ask_price - best_bid_price) : 0;
    }
};

// Level 2 price level update
struct Level2PriceLevel {
    Price price{0};
    Quantity quantity{0};
    uint32_t order_count{0};
    char side{'B'}; // 'B' = bid, 'A' = ask
    char action{'A'}; // 'A' = add/update, 'D' = delete, 'M' = modify
};

// Level 2 market data update
struct Level2Update {
    SymbolId symbol{0};
    std::vector<Level2PriceLevel> price_levels;
    uint64_t sequence_number{0};
    std::chrono::high_resolution_clock::time_point timestamp{};
    bool is_snapshot{false}; // true for full snapshot, false for incremental
};

// Trade report
struct TradeReport {
    SymbolId symbol{0};
    uint64_t trade_id{0};
    Price execution_price{0};
    Quantity execution_quantity{0};
    OrderId aggressive_order_id{0};
    OrderId passive_order_id{0};
    char aggressive_side{'B'}; // 'B' or 'S'
    std::chrono::high_resolution_clock::time_point execution_time{};
    uint64_t sequence_number{0};
};

// Symbol status update
struct SymbolStatus {
    SymbolId symbol{0};
    SymbolState old_state{SymbolState::INACTIVE};
    SymbolState new_state{SymbolState::INACTIVE};
    std::string reason;
    std::chrono::high_resolution_clock::time_point timestamp{};
    uint64_t sequence_number{0};
};

// Generic market data message (simplified - no union)
struct MarketDataMessage {
    MessageType type{MessageType::LEVEL1_UPDATE};
    uint64_t sequence_number{0};
    std::chrono::high_resolution_clock::time_point timestamp{};
    
    // Separate storage for different message types
    struct {
        Level1Update level1;
        Level2Update level2;
        TradeReport trade;
        SymbolStatus status;
    } data;
    
    MarketDataMessage() = default;
    ~MarketDataMessage() = default;
    MarketDataMessage(const MarketDataMessage&) = default;
    MarketDataMessage& operator=(const MarketDataMessage&) = default;
    MarketDataMessage(MarketDataMessage&&) = default;
    MarketDataMessage& operator=(MarketDataMessage&&) = default;
};

// Market data subscription
struct Subscription {
    SymbolId symbol{0};           // 0 = all symbols
    MessageType message_type{MessageType::LEVEL1_UPDATE};
    bool enabled{true};
    uint32_t max_depth{10};       // For Level 2 data
    std::chrono::milliseconds throttle_ms{0}; // Minimum time between updates
    std::chrono::high_resolution_clock::time_point last_sent{};
};

// Subscriber interface
class MarketDataSubscriber {
public:
    virtual ~MarketDataSubscriber() = default;
    
    // Called when market data is available
    virtual void on_market_data(const MarketDataMessage& message) = 0;
    
    // Called when subscription status changes
    virtual void on_subscription_status(SymbolId symbol, MessageType type, bool active) {}
    
    // Get subscriber ID for management
    virtual std::string get_subscriber_id() const = 0;
};

// Market data publisher
class MarketDataPublisher {
private:
    SymbolManager& symbol_manager_;
    MatchingEngine& matching_engine_;
    
    // Sequence number generation
    std::atomic<uint64_t> sequence_number_{1};
    
    // Subscribers
    struct SubscriberInfo {
        std::shared_ptr<MarketDataSubscriber> subscriber;
        std::vector<Subscription> subscriptions;
        bool active{true};
    };
    std::unordered_map<std::string, SubscriberInfo> subscribers_;
    mutable std::mutex subscribers_mutex_;
    
    // Message queue for async publishing
    std::queue<MarketDataMessage> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Publishing thread
    std::thread publisher_thread_;
    std::atomic<bool> running_{false};
    
    // Statistics
    struct Stats {
        uint64_t total_messages{0};
        uint64_t level1_messages{0};
        uint64_t level2_messages{0};
        uint64_t trade_messages{0};
        uint64_t status_messages{0};
        uint64_t subscribers{0};
        uint64_t dropped_messages{0}; // Due to throttling or subscriber overload
    } stats_;
    mutable std::mutex stats_mutex_;
    
    // Configuration
    struct Config {
        size_t max_queue_size{10000};
        bool enable_level1{true};
        bool enable_level2{true};
        bool enable_trades{true};
        bool enable_status{true};
        uint32_t default_l2_depth{10};
        std::chrono::milliseconds default_throttle{1}; // 1ms minimum between updates
    } config_;
    
public:
    MarketDataPublisher(SymbolManager& sym_mgr, MatchingEngine& engine);
    ~MarketDataPublisher();
    
    // Lifecycle
    bool start();
    void stop();
    bool is_running() const { return running_.load(); }
    
    // Subscriber management
    bool add_subscriber(std::shared_ptr<MarketDataSubscriber> subscriber);
    bool remove_subscriber(const std::string& subscriber_id);
    
    // Subscription management
    bool subscribe(const std::string& subscriber_id, SymbolId symbol, MessageType type, 
                  uint32_t depth = 10, std::chrono::milliseconds throttle = std::chrono::milliseconds(1));
    bool unsubscribe(const std::string& subscriber_id, SymbolId symbol, MessageType type);
    
    // Bulk subscriptions
    bool subscribe_all_symbols(const std::string& subscriber_id, MessageType type);
    bool subscribe_symbol_list(const std::string& subscriber_id, const std::vector<SymbolId>& symbols, MessageType type);
    
    // Manual data publishing (usually called by matching engine callbacks)
    void publish_level1_update(SymbolId symbol);
    void publish_level2_update(SymbolId symbol);
    void publish_trade(const Fill& fill);
    void publish_symbol_status(SymbolId symbol, SymbolState old_state, SymbolState new_state, const std::string& reason = "");
    
    // Snapshot requests
    void send_level1_snapshot(const std::string& subscriber_id, SymbolId symbol);
    void send_level2_snapshot(const std::string& subscriber_id, SymbolId symbol, uint32_t depth = 10);
    
    // Statistics and monitoring
    Stats get_stats() const;
    void reset_stats();
    
    // Configuration
    void set_config(const Config& config) { config_ = config; }
    const Config& get_config() const { return config_; }
    
    // Get current subscribers
    std::vector<std::string> get_subscriber_ids() const;
    std::vector<Subscription> get_subscriptions(const std::string& subscriber_id) const;

private:
    // Internal publishing methods
    void enqueue_message(MarketDataMessage&& message);
    void publisher_loop();
    void deliver_message(const MarketDataMessage& message);
    bool should_deliver(const SubscriberInfo& sub_info, const MarketDataMessage& message);
    
    // Data builders
    Level1Update build_level1_update(SymbolId symbol);
    Level2Update build_level2_update(SymbolId symbol, bool is_snapshot = false);
    TradeReport build_trade_report(const Fill& fill);
    SymbolStatus build_symbol_status(SymbolId symbol, SymbolState old_state, SymbolState new_state, const std::string& reason);
    
    // Utilities
    uint64_t next_sequence() { return sequence_number_.fetch_add(1); }
    void update_stats(MessageType type);
};

// Simple console-based market data subscriber for testing
class ConsoleSubscriber : public MarketDataSubscriber {
private:
    std::string subscriber_id_;
    SymbolManager& symbol_manager_;
    bool verbose_{false};
    
public:
    ConsoleSubscriber(const std::string& id, SymbolManager& sym_mgr, bool verbose = false)
        : subscriber_id_(id), symbol_manager_(sym_mgr), verbose_(verbose) {}
    
    void on_market_data(const MarketDataMessage& message) override;
    void on_subscription_status(SymbolId symbol, MessageType type, bool active) override;
    std::string get_subscriber_id() const override { return subscriber_id_; }
    
    void set_verbose(bool verbose) { verbose_ = verbose; }
};

// File-based market data recorder
class FileRecorder : public MarketDataSubscriber {
private:
    std::string subscriber_id_;
    std::string output_file_;
    std::ofstream file_;
    std::mutex file_mutex_;
    
public:
    FileRecorder(const std::string& id, const std::string& filename);
    ~FileRecorder();
    
    void on_market_data(const MarketDataMessage& message) override;
    std::string get_subscriber_id() const override { return subscriber_id_; }
    
    bool is_open() const { return file_.is_open(); }
};

} // namespace market_data

#endif // MARKET_DATA_PUBLISHER_HPP
