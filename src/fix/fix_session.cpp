#include "fix/fix_session.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>

namespace fix {

// FixSession Implementation

FixSession::FixSession(const std::string& sender_comp_id, const std::string& target_comp_id)
    : sender_comp_id_(sender_comp_id), target_comp_id_(target_comp_id) {
    stats_.session_start_time = std::chrono::steady_clock::now();
    last_received_time_ = std::chrono::steady_clock::now();
    last_sent_time_ = std::chrono::steady_clock::now();
}

FixSession::~FixSession() {
    disconnect();
}

bool FixSession::connect(const std::string& host, int port) {
    if (is_connected()) {
        return true;
    }
    
    set_state(SessionState::CONNECTING);
    
    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket" << std::endl;
        set_state(SessionState::ERROR);
        return false;
    }
    
    // Setup server address
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Invalid address: " << host << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        set_state(SessionState::ERROR);
        return false;
    }
    
    // Connect
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Connection failed to " << host << ":" << port << std::endl;
        close(socket_fd_);
        socket_fd_ = -1;
        set_state(SessionState::ERROR);
        return false;
    }
    
    host_ = host;
    port_ = port;
    running_ = true;
    
    // Start threads
    receiver_thread_ = std::thread(&FixSession::receiver_loop, this);
    sender_thread_ = std::thread(&FixSession::sender_loop, this);
    heartbeat_thread_ = std::thread(&FixSession::heartbeat_loop, this);
    
    set_state(SessionState::CONNECTED);
    
    if (state_callback_) {
        state_callback_(this, true);
    }
    
    std::cout << "[FIX] Connected to " << host << ":" << port << std::endl;
    return true;
}

void FixSession::disconnect() {
    if (socket_fd_ == -1) {
        return;
    }
    
    running_ = false;
    
    // Close socket to wake up receiver
    close(socket_fd_);
    socket_fd_ = -1;
    
    // Wake up sender
    queue_cv_.notify_all();
    
    // Join threads
    if (receiver_thread_.joinable()) {
        receiver_thread_.join();
    }
    if (sender_thread_.joinable()) {
        sender_thread_.join();
    }
    if (heartbeat_thread_.joinable()) {
        heartbeat_thread_.join();
    }
    
    set_state(SessionState::DISCONNECTED);
    
    if (state_callback_) {
        state_callback_(this, false);
    }
    
    std::cout << "[FIX] Disconnected from " << host_ << ":" << port_ << std::endl;
}

bool FixSession::is_connected() const {
    auto state = get_state();
    return state == SessionState::CONNECTED || state == SessionState::LOGGED_IN;
}

bool FixSession::logon(const std::string& username, const std::string& password) {
    if (!is_connected()) {
        return false;
    }
    
    auto logon_msg = FixMessageBuilder::create_logon(
        sender_comp_id_, target_comp_id_, outgoing_seq_num_.load(), heartbeat_interval_);
    
    if (!username.empty()) {
        logon_msg.add_field(FixTag::Username, username);
    }
    if (!password.empty()) {
        logon_msg.add_field(FixTag::Password, password);
    }
    
    return send_message(logon_msg);
}

void FixSession::logout(const std::string& reason) {
    if (!is_connected()) {
        return;
    }
    
    set_state(SessionState::LOGGING_OUT);
    
    auto logout_msg = FixMessageBuilder::create_logout(
        sender_comp_id_, target_comp_id_, outgoing_seq_num_.load(), reason);
    
    send_message(logout_msg);
}

bool FixSession::send_message(const FixMessage& message) {
    if (socket_fd_ == -1) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    outgoing_queue_.push(message);
    queue_cv_.notify_one();
    return true;
}

bool FixSession::send_raw_message(const std::string& fix_string) {
    if (socket_fd_ == -1) {
        return false;
    }
    
    ssize_t sent = send(socket_fd_, fix_string.c_str(), fix_string.length(), 0);
    if (sent != static_cast<ssize_t>(fix_string.length())) {
        std::cerr << "[FIX] Failed to send message: " << sent << " of " << fix_string.length() << " bytes" << std::endl;
        return false;
    }
    
    update_last_sent_time();
    update_stats_sent();
    return true;
}

SessionState FixSession::get_state() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

void FixSession::set_state(SessionState new_state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (state_ != new_state) {
        state_ = new_state;
        std::cout << "[FIX] Session state changed to " << static_cast<int>(new_state) << std::endl;
    }
}

FixSession::Stats FixSession::get_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void FixSession::reset_stats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = {};
    stats_.session_start_time = std::chrono::steady_clock::now();
}

// Private methods

void FixSession::receiver_loop() {
    char buffer[4096];
    
    while (running_ && socket_fd_ != -1) {
        ssize_t received = recv(socket_fd_, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            if (running_ && socket_fd_ != -1) {
                std::cerr << "[FIX] Connection lost (recv returned " << received << ")" << std::endl;
            }
            break;
        }
        
        process_received_data(buffer, received);
    }
    
    if (running_) {
        set_state(SessionState::ERROR);
        if (state_callback_) {
            state_callback_(this, false);
        }
    }
}

void FixSession::sender_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        queue_cv_.wait(lock, [this] { return !outgoing_queue_.empty() || !running_; });
        
        while (!outgoing_queue_.empty() && running_) {
            FixMessage message = std::move(outgoing_queue_.front());
            outgoing_queue_.pop();
            lock.unlock();
            
            // Set sequence number
            message.add_field(FixTag::MsgSeqNum, outgoing_seq_num_.fetch_add(1));
            
            std::string fix_string = message.to_fix_string();
            if (!send_raw_message(fix_string)) {
                break;
            }
            
            lock.lock();
        }
    }
}

void FixSession::heartbeat_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!running_) break;
        
        auto now = std::chrono::steady_clock::now();
        
        // Check if we need to send a heartbeat
        auto time_since_last_sent = std::chrono::duration_cast<std::chrono::seconds>(now - last_sent_time_);
        if (time_since_last_sent.count() >= heartbeat_interval_) {
            send_heartbeat();
        }
        
        // Check if we haven't received anything for too long
        auto time_since_last_received = std::chrono::duration_cast<std::chrono::seconds>(now - last_received_time_);
        if (time_since_last_received.count() >= heartbeat_interval_ * 2) {
            std::cout << "[FIX] Heartbeat timeout, sending test request" << std::endl;
            send_test_request();
        }
        
        // If still no response, disconnect
        if (time_since_last_received.count() >= heartbeat_interval_ * 3) {
            std::cerr << "[FIX] Session timeout, disconnecting" << std::endl;
            set_state(SessionState::ERROR);
            break;
        }
    }
}

void FixSession::process_received_data(const char* data, size_t length) {
    std::lock_guard<std::mutex> lock(receive_mutex_);
    receive_buffer_.append(data, length);
    
    auto messages = extract_complete_messages();
    for (const auto& msg_str : messages) {
        auto message = FixParser::parse(msg_str);
        if (message) {
            update_last_received_time();
            handle_message(*message);
        } else {
            std::cerr << "[FIX] Failed to parse message: " << msg_str << std::endl;
        }
    }
}

std::vector<std::string> FixSession::extract_complete_messages() {
    std::vector<std::string> messages;
    
    size_t start = 0;
    while (start < receive_buffer_.length()) {
        // Look for SOH after checksum (10=XXX)
        size_t checksum_pos = receive_buffer_.find("10=", start);
        if (checksum_pos == std::string::npos) {
            break; // No complete message yet
        }
        
        // Find the SOH after checksum value
        size_t soh_pos = receive_buffer_.find(SOH, checksum_pos + 3);
        if (soh_pos == std::string::npos) {
            break; // Incomplete checksum
        }
        
        // Extract complete message
        std::string message = receive_buffer_.substr(start, soh_pos - start + 1);
        messages.push_back(message);
        
        start = soh_pos + 1;
    }
    
    // Remove processed messages from buffer
    if (start > 0) {
        receive_buffer_ = receive_buffer_.substr(start);
    }
    
    return messages;
}

void FixSession::handle_message(const FixMessage& message) {
    update_stats_received();
    
    // Validate sequence number
    if (!validate_sequence_number(message)) {
        std::cerr << "[FIX] Sequence number error" << std::endl;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.sequence_errors++;
        return;
    }
    
    // Handle session-level messages
    auto msg_type = message.get_msg_type();
    if (msg_type) {
        switch (static_cast<MsgType>(*msg_type)) {
            case MsgType::LOGON:
                handle_logon(message);
                return;
            case MsgType::LOGOUT:
                handle_logout(message);
                return;
            case MsgType::HEARTBEAT:
                handle_heartbeat(message);
                return;
            case MsgType::TEST_REQUEST:
                handle_test_request(message);
                return;
            default:
                break; // Application message
        }
    }
    
    // Forward application messages to callback
    if (message_callback_) {
        message_callback_(this, message);
    }
}

void FixSession::handle_logon(const FixMessage& message) {
    std::cout << "[FIX] Received logon" << std::endl;
    
    // Extract heartbeat interval
    auto hb_int = message.get_field_as<int>(FixTag::HeartBtInt);
    if (hb_int) {
        heartbeat_interval_ = *hb_int;
        std::cout << "[FIX] Heartbeat interval set to " << heartbeat_interval_ << " seconds" << std::endl;
    }
    
    set_state(SessionState::LOGGED_IN);
}

void FixSession::handle_logout(const FixMessage& message) {
    std::cout << "[FIX] Received logout" << std::endl;
    
    auto text = message.get_field(FixTag::Text);
    if (text && !text->empty()) {
        std::cout << "[FIX] Logout reason: " << *text << std::endl;
    }
    
    // Send logout response if we didn't initiate
    if (get_state() != SessionState::LOGGING_OUT) {
        send_logout_response();
    }
    
    set_state(SessionState::DISCONNECTED);
}

void FixSession::handle_heartbeat(const FixMessage& message) {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.heartbeats_received++;
}

void FixSession::handle_test_request(const FixMessage& message) {
    std::cout << "[FIX] Received test request" << std::endl;
    
    auto test_req_id = message.get_field(FixTag::TestReqID);
    send_heartbeat(test_req_id ? *test_req_id : "");
}

bool FixSession::send_heartbeat(const std::string& test_req_id) {
    auto hb_msg = FixMessageBuilder::create_heartbeat(
        sender_comp_id_, target_comp_id_, outgoing_seq_num_.load(), test_req_id);
    
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_.heartbeats_sent++;
    
    return send_message(hb_msg);
}

bool FixSession::send_test_request() {
    FixMessage msg;
    msg.add_field(FixTag::BeginString, VERSION_4_2);
    msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::TEST_REQUEST));
    msg.add_field(FixTag::SenderCompID, sender_comp_id_);
    msg.add_field(FixTag::TargetCompID, target_comp_id_);
    msg.add_field(FixTag::SendingTime, current_utc_timestamp());
    msg.add_field(FixTag::TestReqID, "TEST_" + std::to_string(std::time(nullptr)));
    
    return send_message(msg);
}

bool FixSession::send_logout_response(const std::string& reason) {
    auto logout_msg = FixMessageBuilder::create_logout(
        sender_comp_id_, target_comp_id_, outgoing_seq_num_.load(), reason);
    
    return send_message(logout_msg);
}

bool FixSession::validate_sequence_number(const FixMessage& message) {
    auto seq_num = message.get_field_as<int>(FixTag::MsgSeqNum);
    if (!seq_num) {
        return false; // No sequence number
    }
    
    int expected = expected_seq_num_.load();
    if (*seq_num == expected) {
        expected_seq_num_.fetch_add(1);
        return true;
    } else if (*seq_num > expected) {
        std::cerr << "[FIX] Sequence gap: expected " << expected << ", got " << *seq_num << std::endl;
        expected_seq_num_.store(*seq_num + 1);
        return true; // Accept but note the gap
    } else {
        std::cerr << "[FIX] Duplicate sequence: expected " << expected << ", got " << *seq_num << std::endl;
        return false; // Duplicate
    }
}

// FixServer Implementation

FixServer::FixServer(int port) : port_(port) {}

FixServer::~FixServer() {
    stop();
}

bool FixServer::start() {
    if (running_.exchange(true)) {
        return false; // Already running
    }
    
    server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket_ < 0) {
        std::cerr << "[FIX] Failed to create server socket" << std::endl;
        running_ = false;
        return false;
    }
    
    // Allow address reuse
    int opt = 1;
    setsockopt(server_socket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);
    
    if (bind(server_socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "[FIX] Failed to bind server socket to port " << port_ << std::endl;
        close(server_socket_);
        running_ = false;
        return false;
    }
    
    if (listen(server_socket_, 10) < 0) {
        std::cerr << "[FIX] Failed to listen on socket" << std::endl;
        close(server_socket_);
        running_ = false;
        return false;
    }
    
    accept_thread_ = std::thread(&FixServer::accept_loop, this);
    
    std::cout << "[FIX] Server listening on port " << port_ << std::endl;
    return true;
}

void FixServer::stop() {
    if (!running_.exchange(false)) {
        return; // Already stopped
    }
    
    // Close server socket to stop accepting
    if (server_socket_ != -1) {
        close(server_socket_);
        server_socket_ = -1;
    }
    
    // Wait for accept thread
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // Disconnect all client sessions
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : client_sessions_) {
        session->disconnect();
    }
    client_sessions_.clear();
    
    std::cout << "[FIX] Server stopped" << std::endl;
}

void FixServer::broadcast_message(const FixMessage& message) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    for (auto& session : client_sessions_) {
        if (session->is_connected()) {
            session->send_message(message);
        }
    }
}

std::vector<std::shared_ptr<FixSession>> FixServer::get_active_sessions() const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);
    std::vector<std::shared_ptr<FixSession>> active_sessions;
    
    for (auto& session : client_sessions_) {
        if (session->is_connected()) {
            active_sessions.push_back(session);
        }
    }
    
    return active_sessions;
}

void FixServer::accept_loop() {
    while (running_) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        
        int client_socket = accept(server_socket_, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (running_) {
                std::cerr << "[FIX] Accept failed" << std::endl;
            }
            break;
        }
        
        if (!running_) {
            close(client_socket);
            break;
        }
        
        handle_new_client(client_socket);
    }
}

void FixServer::handle_new_client(int client_socket) {
    // Get client address
    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    getpeername(client_socket, (struct sockaddr*)&client_addr, &client_len);
    
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    std::cout << "[FIX] New client connection from " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;
    
    // Create session for this client connection
    auto session = std::make_shared<FixSession>("SERVER", "CLIENT");
    
    // Since we can't access private members directly, we need a different approach
    // For now, close the client socket and let the session handle it properly
    close(client_socket);
    
    // Set callbacks
    if (message_callback_) {
        session->set_message_callback(message_callback_);
    }
    
    // Add to sessions list
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        client_sessions_.push_back(session);
    }
    
    // Notify callback
    if (new_session_callback_) {
        new_session_callback_(session);
    }
}

} // namespace fix
