#include "fix/fix_session.hpp"
#include <iostream>
#include <thread>
#include <string>
#include <sstream>
#include <random>
#include <iomanip>

using namespace fix;

class TradingClient {
private:
    FixSession fix_session_;
    std::string client_id_;
    std::atomic<bool> running_{false};
    std::atomic<uint32_t> next_order_id_{1};
    std::thread input_thread_;
    
    // Order tracking
    struct PendingOrder {
        std::string cl_ord_id;
        std::string symbol;
        char side;
        double quantity;
        double price;
        std::chrono::high_resolution_clock::time_point submit_time;
    };
    std::unordered_map<std::string, PendingOrder> pending_orders_;
    std::mutex orders_mutex_;
    
    // Statistics
    struct Stats {
        uint32_t orders_sent{0};
        uint32_t executions_received{0};
        uint32_t fills_received{0};
        uint64_t total_volume{0};
    } stats_;
    std::mutex stats_mutex_;
    
public:
    TradingClient(const std::string& client_id) 
        : fix_session_(client_id, "GATEWAY"), client_id_(client_id) {
        
        // Set message callback
        fix_session_.set_message_callback([this](FixSession* session, const FixMessage& message) {
            handle_fix_message(message);
        });
        
        // Set state callback
        fix_session_.set_state_callback([this](FixSession* session, bool connected) {
            if (connected) {
                std::cout << "[CLIENT] Connected to FIX gateway" << std::endl;
            } else {
                std::cout << "[CLIENT] Disconnected from FIX gateway" << std::endl;
            }
        });
    }
    
    ~TradingClient() {
        stop();
    }
    
    bool connect(const std::string& host, int port) {
        if (!fix_session_.connect(host, port)) {
            return false;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Wait for connection
        
        // Send logon
        if (!fix_session_.logon()) {
            std::cerr << "[CLIENT] Failed to logon" << std::endl;
            return false;
        }
        
        running_ = true;
        
        // Start input thread
        input_thread_ = std::thread(&TradingClient::input_loop, this);
        
        std::cout << "[CLIENT] Successfully connected and logged in" << std::endl;
        return true;
    }
    
    void stop() {
        if (!running_.exchange(false)) {
            return;
        }
        
        fix_session_.logout("Client shutdown");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        fix_session_.disconnect();
        
        if (input_thread_.joinable()) {
            input_thread_.join();
        }
    }
    
    void run() {
        print_help();
        
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
private:
    void handle_fix_message(const FixMessage& message) {
        auto msg_type = message.get_msg_type();
        if (!msg_type) return;
        
        switch (static_cast<MsgType>(*msg_type)) {
            case MsgType::EXECUTION_REPORT:
                handle_execution_report(message);
                break;
            case MsgType::LOGOUT:
                std::cout << "[CLIENT] Server initiated logout" << std::endl;
                running_ = false;
                break;
            default:
                std::cout << "[CLIENT] Received message type: " << *msg_type << std::endl;
                break;
        }
    }
    
    void handle_execution_report(const FixMessage& message) {
        auto cl_ord_id = message.get_field(FixTag::ClOrdID);
        auto exec_type = message.get_field_as<char>(FixTag::ExecType);
        auto ord_status = message.get_field_as<char>(FixTag::OrdStatus);
        auto symbol = message.get_field(FixTag::Symbol);
        auto side = message.get_field_as<char>(FixTag::Side);
        auto leaves_qty = message.get_field_as<double>(FixTag::LeavesQty);
        auto cum_qty = message.get_field_as<double>(FixTag::CumQty);
        auto avg_px = message.get_field_as<double>(FixTag::AvgPx);
        auto last_shares = message.get_field_as<double>(FixTag::LastShares);
        auto last_px = message.get_field_as<double>(FixTag::LastPx);
        auto text = message.get_field(FixTag::Text);
        
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.executions_received++;
        
        std::cout << std::endl << "=== EXECUTION REPORT ===" << std::endl;
        if (cl_ord_id) std::cout << "Order ID: " << *cl_ord_id << std::endl;
        if (symbol) std::cout << "Symbol: " << *symbol << std::endl;
        if (side) std::cout << "Side: " << (*side == '1' ? "BUY" : "SELL") << std::endl;
        
        if (exec_type) {
            std::cout << "Exec Type: ";
            switch (static_cast<ExecType>(*exec_type)) {
                case ExecType::NEW: std::cout << "NEW"; break;
                case ExecType::PARTIAL_FILL: std::cout << "PARTIAL FILL"; break;
                case ExecType::FILL: std::cout << "FILL"; break;
                case ExecType::CANCELED: std::cout << "CANCELED"; break;
                case ExecType::REJECTED: std::cout << "REJECTED"; break;
                default: std::cout << *exec_type; break;
            }
            std::cout << std::endl;
        }
        
        if (ord_status) {
            std::cout << "Order Status: ";
            switch (static_cast<OrdStatus>(*ord_status)) {
                case OrdStatus::NEW: std::cout << "NEW"; break;
                case OrdStatus::PARTIALLY_FILLED: std::cout << "PARTIALLY FILLED"; break;
                case OrdStatus::FILLED: std::cout << "FILLED"; break;
                case OrdStatus::CANCELED: std::cout << "CANCELED"; break;
                case OrdStatus::REJECTED: std::cout << "REJECTED"; break;
                default: std::cout << *ord_status; break;
            }
            std::cout << std::endl;
        }
        
        if (leaves_qty) std::cout << "Leaves Qty: " << std::fixed << std::setprecision(0) << *leaves_qty << std::endl;
        if (cum_qty) std::cout << "Cum Qty: " << std::fixed << std::setprecision(0) << *cum_qty << std::endl;
        if (avg_px) std::cout << "Avg Price: $" << std::fixed << std::setprecision(4) << *avg_px << std::endl;
        
        if (last_shares && *last_shares > 0) {
            std::cout << "Last Fill: " << std::fixed << std::setprecision(0) << *last_shares << " shares";
            if (last_px) std::cout << " @ $" << std::fixed << std::setprecision(4) << *last_px;
            std::cout << std::endl;
            
            stats_.fills_received++;
            stats_.total_volume += static_cast<uint64_t>(*last_shares);
        }
        
        if (text && !text->empty()) {
            std::cout << "Text: " << *text << std::endl;
        }
        
        std::cout << "=========================" << std::endl;
        std::cout << "client> " << std::flush;
    }
    
    void input_loop() {
        std::string line;
        while (running_ && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            
            std::istringstream iss(line);
            std::string command;
            iss >> command;
            
            if (command == "buy" || command == "sell") {
                handle_order_command(command, iss);
            } else if (command == "market") {
                std::string side;
                iss >> side;
                handle_market_order_command(side, iss);
            } else if (command == "status") {
                print_status();
            } else if (command == "help") {
                print_help();
            } else if (command == "quit" || command == "exit") {
                running_ = false;
                break;
            } else {
                std::cout << "Unknown command: " << command << ". Type 'help' for available commands." << std::endl;
            }
            
            if (running_) {
                std::cout << "client> " << std::flush;
            }
        }
    }
    
    void handle_order_command(const std::string& side, std::istringstream& iss) {
        std::string symbol;
        double quantity, price;
        
        if (!(iss >> symbol >> quantity >> price)) {
            std::cout << "Usage: " << side << " <symbol> <quantity> <price>" << std::endl;
            return;
        }
        
        if (quantity <= 0 || price <= 0) {
            std::cout << "Quantity and price must be positive" << std::endl;
            return;
        }
        
        send_limit_order(symbol, side == "buy" ? Side::BUY : Side::SELL, quantity, price);
    }
    
    void handle_market_order_command(const std::string& side, std::istringstream& iss) {
        std::string symbol;
        double quantity;
        
        if (!(iss >> symbol >> quantity)) {
            std::cout << "Usage: market <buy|sell> <symbol> <quantity>" << std::endl;
            return;
        }
        
        if (quantity <= 0) {
            std::cout << "Quantity must be positive" << std::endl;
            return;
        }
        
        if (side != "buy" && side != "sell") {
            std::cout << "Side must be 'buy' or 'sell'" << std::endl;
            return;
        }
        
        send_market_order(symbol, side == "buy" ? Side::BUY : Side::SELL, quantity);
    }
    
    void send_limit_order(const std::string& symbol, Side side, double quantity, double price) {
        std::string cl_ord_id = generate_order_id();
        
        auto order = FixMessageBuilder::create_new_order_single(
            client_id_, "GATEWAY", fix_session_.get_next_outgoing_seq_num(),
            cl_ord_id, symbol, side, quantity, OrdType::LIMIT, price
        );
        
        // Track the order
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            PendingOrder pending_order;
            pending_order.cl_ord_id = cl_ord_id;
            pending_order.symbol = symbol;
            pending_order.side = static_cast<char>(side);
            pending_order.quantity = quantity;
            pending_order.price = price;
            pending_order.submit_time = std::chrono::high_resolution_clock::now();
            pending_orders_[cl_ord_id] = pending_order;
        }
        
        fix_session_.send_message(order);
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.orders_sent++;
        }
        
        std::cout << "Sent LIMIT " << (side == Side::BUY ? "BUY" : "SELL") << " order: " 
                  << quantity << " " << symbol << " @ $" << std::fixed << std::setprecision(4) << price
                  << " (ID: " << cl_ord_id << ")" << std::endl;
    }
    
    void send_market_order(const std::string& symbol, Side side, double quantity) {
        std::string cl_ord_id = generate_order_id();
        
        auto order = FixMessageBuilder::create_new_order_single(
            client_id_, "GATEWAY", fix_session_.get_next_outgoing_seq_num(),
            cl_ord_id, symbol, side, quantity, OrdType::MARKET
        );
        
        // Track the order
        {
            std::lock_guard<std::mutex> lock(orders_mutex_);
            PendingOrder pending_order;
            pending_order.cl_ord_id = cl_ord_id;
            pending_order.symbol = symbol;
            pending_order.side = static_cast<char>(side);
            pending_order.quantity = quantity;
            pending_order.price = 0; // Market order
            pending_order.submit_time = std::chrono::high_resolution_clock::now();
            pending_orders_[cl_ord_id] = pending_order;
        }
        
        fix_session_.send_message(order);
        
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.orders_sent++;
        }
        
        std::cout << "Sent MARKET " << (side == Side::BUY ? "BUY" : "SELL") << " order: "
                  << quantity << " " << symbol << " (ID: " << cl_ord_id << ")" << std::endl;
    }
    
    void print_status() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        
        std::cout << std::endl << "=== CLIENT STATUS ===" << std::endl;
        std::cout << "Session State: " << (fix_session_.is_connected() ? "Connected" : "Disconnected") << std::endl;
        std::cout << "Orders Sent: " << stats_.orders_sent << std::endl;
        std::cout << "Executions Received: " << stats_.executions_received << std::endl;
        std::cout << "Fills Received: " << stats_.fills_received << std::endl;
        std::cout << "Total Volume: " << stats_.total_volume << " shares" << std::endl;
        
        auto session_stats = fix_session_.get_stats();
        std::cout << "Messages Sent: " << session_stats.messages_sent << std::endl;
        std::cout << "Messages Received: " << session_stats.messages_received << std::endl;
        std::cout << "Heartbeats Sent: " << session_stats.heartbeats_sent << std::endl;
        std::cout << "Heartbeats Received: " << session_stats.heartbeats_received << std::endl;
        
        std::cout << "Active Orders: ";
        {
            std::lock_guard<std::mutex> orders_lock(orders_mutex_);
            std::cout << pending_orders_.size() << std::endl;
        }
        
        std::cout << "=====================" << std::endl;
    }
    
    void print_help() {
        std::cout << std::endl << "=== TRADING CLIENT HELP ===" << std::endl;
        std::cout << "Available commands:" << std::endl;
        std::cout << "  buy <symbol> <quantity> <price>  - Send limit buy order" << std::endl;
        std::cout << "  sell <symbol> <quantity> <price> - Send limit sell order" << std::endl;
        std::cout << "  market buy <symbol> <quantity>   - Send market buy order" << std::endl;
        std::cout << "  market sell <symbol> <quantity>  - Send market sell order" << std::endl;
        std::cout << "  status                           - Show client status" << std::endl;
        std::cout << "  help                             - Show this help" << std::endl;
        std::cout << "  quit/exit                        - Exit client" << std::endl;
        std::cout << std::endl;
        std::cout << "Examples:" << std::endl;
        std::cout << "  buy AAPL 100 150.25             - Buy 100 AAPL at $150.25" << std::endl;
        std::cout << "  sell MSFT 50 300.00             - Sell 50 MSFT at $300.00" << std::endl;
        std::cout << "  market buy AAPL 25              - Market buy 25 AAPL" << std::endl;
        std::cout << "============================" << std::endl;
        std::cout << "client> " << std::flush;
    }
    
    std::string generate_order_id() {
        return client_id_ + "_" + std::to_string(next_order_id_++);
    }
};

int main(int argc, char* argv[]) {
    std::string host = "127.0.0.1";
    int port = 9878;
    std::string client_id = "CLIENT1";
    
    if (argc > 1) {
        client_id = argv[1];
    }
    if (argc > 2) {
        host = argv[2];
    }
    if (argc > 3) {
        port = std::stoi(argv[3]);
    }
    
    std::cout << "=== FIX Trading Client ===" << std::endl;
    std::cout << "Client ID: " << client_id << std::endl;
    std::cout << "Connecting to " << host << ":" << port << std::endl;
    std::cout << "==========================" << std::endl;
    
    TradingClient client(client_id);
    
    if (!client.connect(host, port)) {
        std::cerr << "Failed to connect to FIX gateway" << std::endl;
        return 1;
    }
    
    try {
        client.run();
    } catch (const std::exception& e) {
        std::cerr << "Client error: " << e.what() << std::endl;
    }
    
    std::cout << "Client shutting down..." << std::endl;
    return 0;
}
