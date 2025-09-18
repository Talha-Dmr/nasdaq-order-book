#include "market_data/publisher.hpp"
#include <iostream>
#include <iomanip>
#include <fstream>

namespace market_data {

// MarketDataPublisher Implementation

MarketDataPublisher::MarketDataPublisher(SymbolManager& sym_mgr, MatchingEngine& engine)
    : symbol_manager_(sym_mgr), matching_engine_(engine) {
}

MarketDataPublisher::~MarketDataPublisher() {
    stop();
}

bool MarketDataPublisher::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }
    
    publisher_thread_ = std::thread(&MarketDataPublisher::publisher_loop, this);
    return true;
}

void MarketDataPublisher::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }
    
    // Wake up publisher thread
    queue_cv_.notify_all();
    
    if (publisher_thread_.joinable()) {
        publisher_thread_.join();
    }
}

bool MarketDataPublisher::add_subscriber(std::shared_ptr<MarketDataSubscriber> subscriber) {
    if (!subscriber) return false;
    
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    std::string id = subscriber->get_subscriber_id();
    if (subscribers_.find(id) != subscribers_.end()) {
        return false; // Subscriber already exists
    }
    
    SubscriberInfo info;
    info.subscriber = subscriber;
    info.active = true;
    
    subscribers_[id] = std::move(info);
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.subscribers = subscribers_.size();
    
    return true;
}

bool MarketDataPublisher::remove_subscriber(const std::string& subscriber_id) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    auto it = subscribers_.find(subscriber_id);
    if (it == subscribers_.end()) {
        return false;
    }
    
    subscribers_.erase(it);
    
    std::lock_guard<std::mutex> stats_lock(stats_mutex_);
    stats_.subscribers = subscribers_.size();
    
    return true;
}

bool MarketDataPublisher::subscribe(const std::string& subscriber_id, SymbolId symbol, 
                                   MessageType type, uint32_t depth, 
                                   std::chrono::milliseconds throttle) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    auto it = subscribers_.find(subscriber_id);
    if (it == subscribers_.end()) {
        return false;
    }
    
    // Check if subscription already exists
    for (auto& sub : it->second.subscriptions) {
        if (sub.symbol == symbol && sub.message_type == type) {
            // Update existing subscription
            sub.max_depth = depth;
            sub.throttle_ms = throttle;
            sub.enabled = true;
            return true;
        }
    }
    
    // Create new subscription
    Subscription subscription;
    subscription.symbol = symbol;
    subscription.message_type = type;
    subscription.max_depth = depth;
    subscription.throttle_ms = throttle;
    subscription.enabled = true;
    
    it->second.subscriptions.push_back(subscription);
    
    // Notify subscriber
    it->second.subscriber->on_subscription_status(symbol, type, true);
    
    return true;
}

bool MarketDataPublisher::unsubscribe(const std::string& subscriber_id, SymbolId symbol, MessageType type) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    auto it = subscribers_.find(subscriber_id);
    if (it == subscribers_.end()) {
        return false;
    }
    
    auto& subscriptions = it->second.subscriptions;
    for (auto sub_it = subscriptions.begin(); sub_it != subscriptions.end(); ++sub_it) {
        if (sub_it->symbol == symbol && sub_it->message_type == type) {
            subscriptions.erase(sub_it);
            
            // Notify subscriber
            it->second.subscriber->on_subscription_status(symbol, type, false);
            return true;
        }
    }
    
    return false;
}

bool MarketDataPublisher::subscribe_all_symbols(const std::string& subscriber_id, MessageType type) {
    return subscribe(subscriber_id, 0, type); // Symbol 0 = all symbols
}

bool MarketDataPublisher::subscribe_symbol_list(const std::string& subscriber_id, 
                                               const std::vector<SymbolId>& symbols, 
                                               MessageType type) {
    bool success = true;
    for (SymbolId symbol : symbols) {
        if (!subscribe(subscriber_id, symbol, type)) {
            success = false;
        }
    }
    return success;
}

void MarketDataPublisher::publish_level1_update(SymbolId symbol) {
    if (!config_.enable_level1) return;
    
    MarketDataMessage message;
    message.type = MessageType::LEVEL1_UPDATE;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.level1 = build_level1_update(symbol);
    
    enqueue_message(std::move(message));
}

void MarketDataPublisher::publish_level2_update(SymbolId symbol) {
    if (!config_.enable_level2) return;
    
    MarketDataMessage message;
    message.type = MessageType::LEVEL2_UPDATE;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.level2 = build_level2_update(symbol, false);
    
    enqueue_message(std::move(message));
}

void MarketDataPublisher::publish_trade(const Fill& fill) {
    if (!config_.enable_trades) return;
    
    MarketDataMessage message;
    message.type = MessageType::TRADE_REPORT;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.trade = build_trade_report(fill);
    
    enqueue_message(std::move(message));
}

void MarketDataPublisher::publish_symbol_status(SymbolId symbol, SymbolState old_state, 
                                               SymbolState new_state, const std::string& reason) {
    if (!config_.enable_status) return;
    
    MarketDataMessage message;
    message.type = MessageType::SYMBOL_STATUS;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.status = build_symbol_status(symbol, old_state, new_state, reason);
    
    enqueue_message(std::move(message));
}

void MarketDataPublisher::send_level1_snapshot(const std::string& subscriber_id, SymbolId symbol) {
    MarketDataMessage message;
    message.type = MessageType::SNAPSHOT_L1;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.level1 = build_level1_update(symbol);
    
    // Send directly to specific subscriber
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    auto it = subscribers_.find(subscriber_id);
    if (it != subscribers_.end() && it->second.active) {
        it->second.subscriber->on_market_data(message);
    }
}

void MarketDataPublisher::send_level2_snapshot(const std::string& subscriber_id, SymbolId symbol, uint32_t depth) {
    MarketDataMessage message;
    message.type = MessageType::SNAPSHOT_L2;
    message.sequence_number = next_sequence();
    message.timestamp = std::chrono::high_resolution_clock::now();
    message.data.level2 = build_level2_update(symbol, true);
    
    // Limit depth
    if (message.data.level2.price_levels.size() > depth * 2) { // *2 for both sides
        message.data.level2.price_levels.resize(depth * 2);
    }
    
    // Send directly to specific subscriber
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    auto it = subscribers_.find(subscriber_id);
    if (it != subscribers_.end() && it->second.active) {
        it->second.subscriber->on_market_data(message);
    }
}

MarketDataPublisher::Stats MarketDataPublisher::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void MarketDataPublisher::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = {};
    stats_.subscribers = subscribers_.size();
}

std::vector<std::string> MarketDataPublisher::get_subscriber_ids() const {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    std::vector<std::string> ids;
    ids.reserve(subscribers_.size());
    
    for (const auto& [id, info] : subscribers_) {
        ids.push_back(id);
    }
    
    return ids;
}

std::vector<Subscription> MarketDataPublisher::get_subscriptions(const std::string& subscriber_id) const {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    auto it = subscribers_.find(subscriber_id);
    if (it != subscribers_.end()) {
        return it->second.subscriptions;
    }
    
    return {};
}

// Private methods

void MarketDataPublisher::enqueue_message(MarketDataMessage&& message) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (message_queue_.size() >= config_.max_queue_size) {
        // Drop oldest message
        message_queue_.pop();
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.dropped_messages++;
    }
    
    message_queue_.push(std::move(message));
    queue_cv_.notify_one();
}

void MarketDataPublisher::publisher_loop() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        queue_cv_.wait(lock, [this] {
            return !message_queue_.empty() || !running_.load();
        });
        
        while (!message_queue_.empty() && running_.load()) {
            MarketDataMessage message = std::move(message_queue_.front());
            message_queue_.pop();
            lock.unlock();
            
            deliver_message(message);
            update_stats(message.type);
            
            lock.lock();
        }
    }
}

void MarketDataPublisher::deliver_message(const MarketDataMessage& message) {
    std::lock_guard<std::mutex> lock(subscribers_mutex_);
    
    for (auto& [id, sub_info] : subscribers_) {
        if (!sub_info.active || !should_deliver(sub_info, message)) {
            continue;
        }
        
        try {
            sub_info.subscriber->on_market_data(message);
        }
        catch (const std::exception& e) {
            // Log error but continue with other subscribers
            std::cerr << "Error delivering message to subscriber " << id << ": " << e.what() << std::endl;
        }
    }
}

bool MarketDataPublisher::should_deliver(const SubscriberInfo& sub_info, const MarketDataMessage& message) {
    for (auto& subscription : sub_info.subscriptions) {
        if (!subscription.enabled) continue;
        
        // Check message type
        if (subscription.message_type != message.type) continue;
        
        // Check symbol filter
        SymbolId msg_symbol = 0;
        switch (message.type) {
            case MessageType::LEVEL1_UPDATE:
            case MessageType::SNAPSHOT_L1:
                msg_symbol = message.data.level1.symbol;
                break;
            case MessageType::LEVEL2_UPDATE:
            case MessageType::SNAPSHOT_L2:
                msg_symbol = message.data.level2.symbol;
                break;
            case MessageType::TRADE_REPORT:
                msg_symbol = message.data.trade.symbol;
                break;
            case MessageType::SYMBOL_STATUS:
                msg_symbol = message.data.status.symbol;
                break;
        }
        
        if (subscription.symbol != 0 && subscription.symbol != msg_symbol) {
            continue; // Symbol doesn't match
        }
        
        // Check throttling
        auto now = std::chrono::high_resolution_clock::now();
        if (subscription.throttle_ms.count() > 0) {
            if (now - subscription.last_sent < subscription.throttle_ms) {
                continue; // Too soon since last message
            }
            const_cast<Subscription&>(subscription).last_sent = now;
        }
        
        return true; // Found matching subscription
    }
    
    return false; // No matching subscription
}

Level1Update MarketDataPublisher::build_level1_update(SymbolId symbol) {
    Level1Update update;
    update.symbol = symbol;
    update.sequence_number = sequence_number_.load();
    update.timestamp = std::chrono::high_resolution_clock::now();
    
    // Get current market data from matching engine
    auto level1_data = matching_engine_.get_level1_data(symbol);
    update.best_bid_price = level1_data.best_bid_price;
    update.best_bid_quantity = level1_data.best_bid_quantity;
    update.best_ask_price = level1_data.best_ask_price;
    update.best_ask_quantity = level1_data.best_ask_quantity;
    
    return update;
}

Level2Update MarketDataPublisher::build_level2_update(SymbolId symbol, bool is_snapshot) {
    Level2Update update;
    update.symbol = symbol;
    update.sequence_number = sequence_number_.load();
    update.timestamp = std::chrono::high_resolution_clock::now();
    update.is_snapshot = is_snapshot;
    
    // Get Level 2 data from matching engine
    auto level2_data = matching_engine_.get_level2_data(symbol, config_.default_l2_depth);
    
    // Convert to our format
    // Add bids (sorted by price descending)
    for (const auto& bid_level : level2_data.bids) {
        Level2PriceLevel level;
        level.price = bid_level.price;
        level.quantity = bid_level.quantity;
        level.order_count = bid_level.order_count;
        level.side = 'B';
        level.action = is_snapshot ? 'A' : 'M';
        update.price_levels.push_back(level);
    }
    
    // Add asks (sorted by price ascending)
    for (const auto& ask_level : level2_data.asks) {
        Level2PriceLevel level;
        level.price = ask_level.price;
        level.quantity = ask_level.quantity;
        level.order_count = ask_level.order_count;
        level.side = 'A';
        level.action = is_snapshot ? 'A' : 'M';
        update.price_levels.push_back(level);
    }
    
    return update;
}

TradeReport MarketDataPublisher::build_trade_report(const Fill& fill) {
    TradeReport report;
    report.symbol = fill.symbol;
    report.trade_id = fill.trade_id;
    report.execution_price = fill.execution_price;
    report.execution_quantity = fill.execution_quantity;
    report.aggressive_order_id = fill.aggressive_order_id;
    report.passive_order_id = fill.passive_order_id;
    report.execution_time = fill.execution_time;
    report.sequence_number = sequence_number_.load();
    
    // Determine aggressive side (simplified)
    report.aggressive_side = 'B'; // Would need to track order sides properly
    
    return report;
}

SymbolStatus MarketDataPublisher::build_symbol_status(SymbolId symbol, SymbolState old_state, 
                                                    SymbolState new_state, const std::string& reason) {
    SymbolStatus status;
    status.symbol = symbol;
    status.old_state = old_state;
    status.new_state = new_state;
    status.reason = reason;
    status.timestamp = std::chrono::high_resolution_clock::now();
    status.sequence_number = sequence_number_.load();
    
    return status;
}

void MarketDataPublisher::update_stats(MessageType type) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    stats_.total_messages++;
    
    switch (type) {
        case MessageType::LEVEL1_UPDATE:
        case MessageType::SNAPSHOT_L1:
            stats_.level1_messages++;
            break;
        case MessageType::LEVEL2_UPDATE:
        case MessageType::SNAPSHOT_L2:
            stats_.level2_messages++;
            break;
        case MessageType::TRADE_REPORT:
            stats_.trade_messages++;
            break;
        case MessageType::SYMBOL_STATUS:
            stats_.status_messages++;
            break;
    }
}

// ConsoleSubscriber Implementation

void ConsoleSubscriber::on_market_data(const MarketDataMessage& message) {
    auto symbol_name = symbol_manager_.get_symbol_name(
        message.type == MessageType::LEVEL1_UPDATE || message.type == MessageType::SNAPSHOT_L1 ? message.data.level1.symbol :
        message.type == MessageType::LEVEL2_UPDATE || message.type == MessageType::SNAPSHOT_L2 ? message.data.level2.symbol :
        message.type == MessageType::TRADE_REPORT ? message.data.trade.symbol :
        message.data.status.symbol
    ).value_or("UNKNOWN");
    
    switch (message.type) {
        case MessageType::LEVEL1_UPDATE:
        case MessageType::SNAPSHOT_L1: {
            const auto& l1 = message.data.level1;
            std::cout << "[L1] " << symbol_name 
                      << " | Bid: $" << std::fixed << std::setprecision(4) << (l1.best_bid_price / 10000.0) << " x " << l1.best_bid_quantity
                      << " | Ask: $" << (l1.best_ask_price / 10000.0) << " x " << l1.best_ask_quantity;
            if (l1.has_bid() && l1.has_ask()) {
                std::cout << " | Spread: $" << (l1.spread() / 10000.0);
            }
            std::cout << " | Seq: " << l1.sequence_number << std::endl;
            break;
        }
        
        case MessageType::LEVEL2_UPDATE:
        case MessageType::SNAPSHOT_L2: {
            const auto& l2 = message.data.level2;
            if (verbose_) {
                std::cout << "[L2] " << symbol_name << " | " << (l2.is_snapshot ? "SNAPSHOT" : "UPDATE") 
                          << " | Levels: " << l2.price_levels.size() << " | Seq: " << l2.sequence_number << std::endl;
                for (const auto& level : l2.price_levels) {
                    std::cout << "  " << level.side << " " << level.action 
                              << " $" << std::fixed << std::setprecision(4) << (level.price / 10000.0)
                              << " x " << level.quantity << " (" << level.order_count << " orders)" << std::endl;
                }
            } else {
                std::cout << "[L2] " << symbol_name << " | " << l2.price_levels.size() << " levels | Seq: " << l2.sequence_number << std::endl;
            }
            break;
        }
        
        case MessageType::TRADE_REPORT: {
            const auto& trade = message.data.trade;
            std::cout << "[TRADE] " << symbol_name 
                      << " | ID: " << trade.trade_id
                      << " | Price: $" << std::fixed << std::setprecision(4) << (trade.execution_price / 10000.0)
                      << " | Qty: " << trade.execution_quantity
                      << " | Side: " << trade.aggressive_side
                      << " | Seq: " << trade.sequence_number << std::endl;
            break;
        }
        
        case MessageType::SYMBOL_STATUS: {
            const auto& status = message.data.status;
            std::cout << "[STATUS] " << symbol_name 
                      << " | " << static_cast<int>(status.old_state) << " -> " << static_cast<int>(status.new_state);
            if (!status.reason.empty()) {
                std::cout << " | Reason: " << status.reason;
            }
            std::cout << " | Seq: " << status.sequence_number << std::endl;
            break;
        }
    }
}

void ConsoleSubscriber::on_subscription_status(SymbolId symbol, MessageType type, bool active) {
    auto symbol_name = symbol_manager_.get_symbol_name(symbol).value_or("ALL");
    std::cout << "[SUB] " << subscriber_id_ << " | " << symbol_name 
              << " | Type: " << static_cast<int>(type) 
              << " | " << (active ? "SUBSCRIBED" : "UNSUBSCRIBED") << std::endl;
}

// FileRecorder Implementation

FileRecorder::FileRecorder(const std::string& id, const std::string& filename)
    : subscriber_id_(id), output_file_(filename) {
    file_.open(output_file_, std::ios::out | std::ios::trunc);
    if (file_.is_open()) {
        // Write CSV header
        file_ << "Timestamp,Sequence,Type,Symbol,Data\n";
    }
}

FileRecorder::~FileRecorder() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FileRecorder::on_market_data(const MarketDataMessage& message) {
    if (!file_.is_open()) return;
    
    std::lock_guard<std::mutex> lock(file_mutex_);
    
    auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        message.timestamp.time_since_epoch()).count();
    
    file_ << timestamp_ns << "," << message.sequence_number << ","
          << static_cast<int>(message.type) << ",";
    
    switch (message.type) {
        case MessageType::LEVEL1_UPDATE:
        case MessageType::SNAPSHOT_L1: {
            const auto& l1 = message.data.level1;
            file_ << l1.symbol << ",\"bid=" << l1.best_bid_price << "x" << l1.best_bid_quantity
                  << ",ask=" << l1.best_ask_price << "x" << l1.best_ask_quantity << "\"";
            break;
        }
        
        case MessageType::TRADE_REPORT: {
            const auto& trade = message.data.trade;
            file_ << trade.symbol << ",\"trade_id=" << trade.trade_id
                  << ",price=" << trade.execution_price 
                  << ",qty=" << trade.execution_quantity << "\"";
            break;
        }
        
        default:
            file_ << "0,\"\"";
            break;
    }
    
    file_ << "\n";
    file_.flush();
}

} // namespace market_data
