#include "fix/fix_gateway.hpp"
#include <iostream>
#include <sstream>

namespace fix {

FixGateway::FixGateway(matching::SymbolManager& symbol_manager, 
                       matching::MatchingEngine& matching_engine,
                       market_data::MarketDataPublisher& market_data_publisher,
                       int port)
    : symbol_manager_(symbol_manager), 
      matching_engine_(matching_engine),
      market_data_publisher_(market_data_publisher),
      port_(port) {
    
    // Set up fill callback from matching engine
    matching_engine_.set_fill_callback([this](const matching::Fill& fill) {
        on_fill(fill);
    });
}

FixGateway::~FixGateway() {
    stop();
}

bool FixGateway::start() {
    std::cout << "[GATEWAY] Starting FIX Gateway on port " << port_ << std::endl;
    
    // Create and start FIX server
    fix_server_ = std::make_unique<FixServer>(port_);
    
    // Set callbacks
    fix_server_->set_new_session_callback([this](std::shared_ptr<FixSession> session) {
        handle_new_session(session);
    });
    
    fix_server_->set_message_callback([this](FixSession* session, const FixMessage& message) {
        handle_fix_message(session, message);
    });
    
    if (!fix_server_->start()) {
        std::cerr << "[GATEWAY] Failed to start FIX server" << std::endl;
        return false;
    }
    
    std::cout << "[GATEWAY] FIX Gateway started successfully" << std::endl;
    return true;
}

void FixGateway::stop() {
    if (fix_server_) {
        fix_server_->stop();
        fix_server_.reset();
    }
    
    // Clear order tracking
    std::lock_guard<std::mutex> lock(orders_mutex_);
    client_orders_.clear();
    engine_to_client_orders_.clear();
    
    std::cout << "[GATEWAY] FIX Gateway stopped" << std::endl;
}

bool FixGateway::is_running() const {
    return fix_server_ != nullptr;
}

FixGateway::Stats FixGateway::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void FixGateway::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = {};
}

std::vector<std::string> FixGateway::get_active_session_ids() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::string> session_ids;
    session_ids.reserve(active_sessions_.size());
    
    for (const auto& [id, session] : active_sessions_) {
        if (session && session->is_connected()) {
            session_ids.push_back(id);
        }
    }
    
    return session_ids;
}

// Private methods

void FixGateway::handle_new_session(std::shared_ptr<FixSession> session) {
    std::cout << "[GATEWAY] New FIX session connected" << std::endl;
    
    std::string session_id = session_key(session.get());
    
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        active_sessions_[session_id] = session;
    }
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.active_sessions = active_sessions_.size();
    }
}

void FixGateway::handle_fix_message(FixSession* session, const FixMessage& message) {
    auto msg_type = message.get_msg_type();
    if (!msg_type) {
        std::cerr << "[GATEWAY] Received message without message type" << std::endl;
        return;
    }
    
    std::cout << "[GATEWAY] Received FIX message type: " << *msg_type << std::endl;
    
    switch (static_cast<MsgType>(*msg_type)) {
        case MsgType::LOGON:
            handle_logon(session, message);
            break;
        case MsgType::NEW_ORDER_SINGLE:
            handle_new_order_single(session, message);
            break;
        case MsgType::ORDER_CANCEL_REQUEST:
            handle_order_cancel_request(session, message);
            break;
        case MsgType::MARKET_DATA_REQUEST:
            handle_market_data_request(session, message);
            break;
        default:
            std::cout << "[GATEWAY] Unhandled message type: " << *msg_type << std::endl;
            break;
    }
}

void FixGateway::handle_logon(FixSession* session, const FixMessage& message) {
    std::cout << "[GATEWAY] Processing logon for session" << std::endl;
    
    // Send logon response
    auto logon_response = FixMessageBuilder::create_logon(
        "GATEWAY", session->get_sender_comp_id(), session->get_next_outgoing_seq_num());
    
    session->send_message(logon_response);
    
    std::cout << "[GATEWAY] Logon successful for session" << std::endl;
}

void FixGateway::handle_new_order_single(FixSession* session, const FixMessage& message) {
    std::cout << "[GATEWAY] Processing new order single" << std::endl;
    
    update_order_stats(false); // Will be updated to true if accepted
    
    auto cl_ord_id = message.get_field(FixTag::ClOrdID);
    if (!cl_ord_id) {
        send_order_reject(session, "UNKNOWN", "Missing ClOrdID");
        return;
    }
    
    std::string error_msg = process_new_order(session, message);
    
    if (!error_msg.empty()) {
        send_order_reject(session, *cl_ord_id, error_msg);
    } else {
        std::cout << "[GATEWAY] Order " << *cl_ord_id << " accepted and forwarded to matching engine" << std::endl;
        update_order_stats(true);
    }
}

void FixGateway::handle_order_cancel_request(FixSession* session, const FixMessage& message) {
    std::cout << "[GATEWAY] Order cancel request - not implemented yet" << std::endl;
    // TODO: Implement order cancellation
}

void FixGateway::handle_market_data_request(FixSession* session, const FixMessage& message) {
    std::cout << "[GATEWAY] Market data request - not implemented yet" << std::endl;
    // TODO: Implement market data subscription
}

std::string FixGateway::process_new_order(FixSession* session, const FixMessage& message) {
    // Extract required fields
    auto cl_ord_id = message.get_field(FixTag::ClOrdID);
    auto symbol_name = message.get_field(FixTag::Symbol);
    auto side_char = message.get_field_as<char>(FixTag::Side);
    auto quantity = message.get_field_as<double>(FixTag::OrderQty);
    auto ord_type_char = message.get_field_as<char>(FixTag::OrdType);
    
    if (!cl_ord_id || !symbol_name || !side_char || !quantity || !ord_type_char) {
        return "Missing required order fields";
    }
    
    if (*quantity <= 0) {
        return "Invalid quantity";
    }
    
    // Convert FIX types to engine types
    fix::Side fix_side = static_cast<fix::Side>(*side_char);
    fix::OrdType fix_ord_type = static_cast<fix::OrdType>(*ord_type_char);
    
    auto engine_side = FixConverter::convert_side(fix_side);
    auto engine_ord_type = FixConverter::convert_order_type(fix_ord_type);
    
    // Resolve symbol
    matching::SymbolId symbol_id = resolve_symbol(*symbol_name);
    if (symbol_id == 0) {
        return "Unknown symbol: " + *symbol_name;
    }
    
    // Get price for limit orders
    double price = 0.0;
    if (fix_ord_type == fix::OrdType::LIMIT) {
        auto price_field = message.get_field_as<double>(FixTag::Price);
        if (!price_field || *price_field <= 0) {
            return "Invalid or missing price for limit order";
        }
        price = *price_field;
    }
    
    // Get time in force
    auto tif_char = message.get_field_as<char>(FixTag::TimeInForce);
    fix::TimeInForce fix_tif = tif_char ? static_cast<fix::TimeInForce>(*tif_char) : fix::TimeInForce::DAY;
    auto engine_tif = FixConverter::convert_tif(fix_tif);
    
    // Create matching engine order
    matching::Order engine_order;
    engine_order.id = next_exec_id_++; // Use exec_id counter for order IDs
    engine_order.symbol = symbol_id;
    engine_order.side = engine_side;
    engine_order.type = engine_ord_type;
    engine_order.tif = engine_tif;
    engine_order.quantity = static_cast<uint32_t>(*quantity);
    engine_order.price = static_cast<uint32_t>(price * 10000); // Convert to fixed-point
    engine_order.timestamp = std::chrono::high_resolution_clock::now();
    engine_order.status = matching::OrderStatus::NEW;
    
    // Submit to matching engine
    auto result = matching_engine_.process_order(engine_order);
    
    // Track the order
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    ClientOrder client_order;
    client_order.cl_ord_id = *cl_ord_id;
    client_order.session_id = session_key(session);
    client_order.engine_order_id = engine_order.id;
    client_order.symbol = symbol_id;
    client_order.quantity = *quantity;
    client_order.filled_quantity = result.total_filled;
    client_order.is_active = !result.is_fully_filled();
    client_order.creation_time = std::chrono::high_resolution_clock::now();
    
    client_orders_[*cl_ord_id] = client_order;
    engine_to_client_orders_[engine_order.id] = *cl_ord_id;
    
    // Send execution report for new order
    auto exec_type = FixConverter::convert_exec_type(result.final_status);
    auto ord_status = FixConverter::convert_order_status(result.final_status);
    
    send_execution_report(client_order.session_id, client_order, exec_type, ord_status);
    
    // Send fill reports for any immediate executions
    for (const auto& fill : result.fills) {
        send_execution_report(client_order.session_id, client_order, 
                            fix::ExecType::PARTIAL_FILL, 
                            result.is_fully_filled() ? fix::OrdStatus::FILLED : fix::OrdStatus::PARTIALLY_FILLED,
                            fill.execution_quantity, fill.execution_price / 10000.0);
    }
    
    // Trigger market data update
    market_data_publisher_.publish_level1_update(symbol_id);
    
    return ""; // Success
}

void FixGateway::send_execution_report(const std::string& session_id, const ClientOrder& client_order,
                                      fix::ExecType exec_type, fix::OrdStatus ord_status,
                                      double last_shares, double last_px) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end() || !it->second->is_connected()) {
        std::cerr << "[GATEWAY] Session not found for execution report: " << session_id << std::endl;
        return;
    }
    
    auto session = it->second;
    auto symbol_name = symbol_manager_.get_symbol_name(client_order.symbol);
    if (!symbol_name) {
        std::cerr << "[GATEWAY] Symbol name not found for ID: " << client_order.symbol << std::endl;
        return;
    }
    
    // Calculate cumulative quantities and average price
    double cum_qty = client_order.filled_quantity + last_shares;
    double leaves_qty = client_order.quantity - cum_qty;
    double avg_px = cum_qty > 0 ? last_px : 0.0; // Simplified avg price calculation
    
    fix::Side side = fix::Side::BUY; // TODO: Track side in ClientOrder
    
    auto exec_report = FixMessageBuilder::create_execution_report(
        "GATEWAY", 
        session->get_sender_comp_id(),
        session->get_next_outgoing_seq_num(),
        std::to_string(client_order.engine_order_id),
        client_order.cl_ord_id,
        generate_exec_id(),
        exec_type,
        ord_status,
        *symbol_name,
        side,
        leaves_qty,
        cum_qty,
        avg_px,
        last_shares,
        last_px
    );
    
    session->send_message(exec_report);
    
    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.executions_sent++;
        if (last_shares > 0) {
            stats_.total_volume += static_cast<uint64_t>(last_shares);
        }
    }
    
    std::cout << "[GATEWAY] Sent execution report for " << client_order.cl_ord_id 
              << ", exec_type=" << static_cast<char>(exec_type)
              << ", status=" << static_cast<char>(ord_status) << std::endl;
}

void FixGateway::send_order_reject(FixSession* session, const std::string& cl_ord_id, const std::string& reason) {
    std::cout << "[GATEWAY] Rejecting order " << cl_ord_id << ": " << reason << std::endl;
    
    // Create rejection execution report
    auto exec_report = FixMessageBuilder::create_execution_report(
        "GATEWAY",
        session->get_sender_comp_id(),
        session->get_next_outgoing_seq_num(),
        "0", // No order ID for rejected orders
        cl_ord_id,
        generate_exec_id(),
        fix::ExecType::REJECTED,
        fix::OrdStatus::REJECTED,
        "UNKNOWN", // Symbol
        fix::Side::BUY, // Default side
        0, 0, 0 // No quantities for rejected order
    );
    
    exec_report.add_field(FixTag::Text, reason);
    
    session->send_message(exec_report);
    
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.orders_rejected++;
    }
}

void FixGateway::on_fill(const matching::Fill& fill) {
    std::cout << "[GATEWAY] Received fill: order_id=" << fill.aggressive_order_id 
              << ", qty=" << fill.execution_quantity 
              << ", price=" << (fill.execution_price / 10000.0) << std::endl;
    
    std::lock_guard<std::mutex> lock(orders_mutex_);
    
    // Find client order
    auto it = engine_to_client_orders_.find(fill.aggressive_order_id);
    if (it == engine_to_client_orders_.end()) {
        std::cerr << "[GATEWAY] Fill for unknown order: " << fill.aggressive_order_id << std::endl;
        return;
    }
    
    std::string cl_ord_id = it->second;
    auto client_it = client_orders_.find(cl_ord_id);
    if (client_it == client_orders_.end()) {
        std::cerr << "[GATEWAY] Client order not found: " << cl_ord_id << std::endl;
        return;
    }
    
    ClientOrder& client_order = client_it->second;
    client_order.filled_quantity += fill.execution_quantity;
    
    // Determine order status
    bool is_fully_filled = (client_order.filled_quantity >= client_order.quantity);
    fix::OrdStatus ord_status = is_fully_filled ? fix::OrdStatus::FILLED : fix::OrdStatus::PARTIALLY_FILLED;
    fix::ExecType exec_type = is_fully_filled ? fix::ExecType::FILL : fix::ExecType::PARTIAL_FILL;
    
    if (is_fully_filled) {
        client_order.is_active = false;
    }
    
    // Send execution report
    send_execution_report(client_order.session_id, client_order, exec_type, ord_status,
                         fill.execution_quantity, fill.execution_price / 10000.0);
    
    // Publish market data update
    market_data_publisher_.publish_level1_update(fill.symbol);
    
    // Publish trade
    market_data_publisher_.publish_trade(fill);
}

std::string FixGateway::generate_exec_id() {
    return "E" + std::to_string(next_exec_id_++);
}

std::string FixGateway::session_key(FixSession* session) {
    std::ostringstream oss;
    oss << session->get_sender_comp_id() << "_" << session->get_target_comp_id() << "_" << session;
    return oss.str();
}

matching::SymbolId FixGateway::resolve_symbol(const std::string& symbol_name) {
    auto symbol_id = symbol_manager_.find_symbol(symbol_name);
    if (!symbol_id) {
        // Try to add new symbol
        symbol_id = symbol_manager_.add_symbol(symbol_name);
        if (symbol_id != 0) {
            std::cout << "[GATEWAY] Added new symbol: " << symbol_name << " (ID=" << symbol_id << ")" << std::endl;
        }
    }
    return symbol_id ? symbol_id : 0;
}

void FixGateway::update_order_stats(bool accepted) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.orders_received++;
    if (accepted) {
        stats_.orders_accepted++;
    } else {
        stats_.orders_rejected++;
    }
}

} // namespace fix
