#ifndef FIX_SESSION_HPP
#define FIX_SESSION_HPP

#include "fix_protocol.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <queue>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

namespace fix {

// Forward declarations
class FixSession;

// Callback types
using MessageCallback = std::function<void(FixSession*, const FixMessage&)>;
using StateChangeCallback = std::function<void(FixSession*, bool connected)>;

// Session state
enum class SessionState {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    LOGGED_IN,
    LOGGING_OUT,
    ERROR
};

// FIX Session class - handles connection and protocol state
class FixSession {
private:
    // Session identifiers
    std::string sender_comp_id_;
    std::string target_comp_id_;
    
    // Network connection
    int socket_fd_{-1};
    std::string host_;
    int port_;
    
    // Session state
    SessionState state_{SessionState::DISCONNECTED};
    std::mutex state_mutex_;
    
    // Sequence numbers
    std::atomic<int> outgoing_seq_num_{1};
    std::atomic<int> incoming_seq_num_{1};
    std::atomic<int> expected_seq_num_{1};
    
    // Heartbeat management
    int heartbeat_interval_{30}; // seconds
    std::chrono::steady_clock::time_point last_received_time_;
    std::chrono::steady_clock::time_point last_sent_time_;
    
    // Threading
    std::thread receiver_thread_;
    std::thread heartbeat_thread_;
    std::atomic<bool> running_{false};
    
    // Message queue for sending
    std::queue<FixMessage> outgoing_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread sender_thread_;
    
    // Callbacks
    MessageCallback message_callback_;
    StateChangeCallback state_callback_;
    
    // Buffer for receiving partial messages
    std::string receive_buffer_;
    std::mutex receive_mutex_;
    
    // Statistics
    struct Stats {
        uint64_t messages_sent{0};
        uint64_t messages_received{0};
        uint64_t heartbeats_sent{0};
        uint64_t heartbeats_received{0};
        uint64_t sequence_errors{0};
        std::chrono::steady_clock::time_point session_start_time;
    } stats_;
    std::mutex stats_mutex_;
    
public:
    FixSession(const std::string& sender_comp_id, const std::string& target_comp_id);
    ~FixSession();
    
    // Connection management
    bool connect(const std::string& host, int port);
    void disconnect();
    bool is_connected() const;
    
    // Session management
    bool logon(const std::string& username = "", const std::string& password = "");
    void logout(const std::string& reason = "");
    
    // Message sending
    bool send_message(const FixMessage& message);
    bool send_raw_message(const std::string& fix_string);
    
    // Callbacks
    void set_message_callback(MessageCallback callback) { message_callback_ = callback; }
    void set_state_callback(StateChangeCallback callback) { state_callback_ = callback; }
    
    // Session info
    SessionState get_state() const;
    std::string get_sender_comp_id() const { return sender_comp_id_; }
    std::string get_target_comp_id() const { return target_comp_id_; }
    int get_next_outgoing_seq_num() const { return outgoing_seq_num_.load(); }
    int get_expected_incoming_seq_num() const { return expected_seq_num_.load(); }
    
    // Statistics
    Stats get_stats() const;
    void reset_stats();
    
private:
    // Internal methods
    void receiver_loop();
    void sender_loop(); 
    void heartbeat_loop();
    
    void set_state(SessionState new_state);
    void handle_message(const FixMessage& message);
    void handle_logon(const FixMessage& message);
    void handle_logout(const FixMessage& message);
    void handle_heartbeat(const FixMessage& message);
    void handle_test_request(const FixMessage& message);
    
    bool send_heartbeat(const std::string& test_req_id = "");
    bool send_test_request();
    bool send_logout_response(const std::string& reason = "");
    
    void process_received_data(const char* data, size_t length);
    std::vector<std::string> extract_complete_messages();
    
    bool validate_sequence_number(const FixMessage& message);
    void update_last_received_time() { last_received_time_ = std::chrono::steady_clock::now(); }
    void update_last_sent_time() { last_sent_time_ = std::chrono::steady_clock::now(); }
    
    void update_stats_sent() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_sent++;
    }
    
    void update_stats_received() {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_received++;
    }
};

// FIX Server - accepts multiple client connections
class FixServer {
private:
    int server_socket_{-1};
    int port_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    
    // Client sessions
    std::vector<std::shared_ptr<FixSession>> client_sessions_;
    std::mutex sessions_mutex_;
    
    // Callbacks
    std::function<void(std::shared_ptr<FixSession>)> new_session_callback_;
    MessageCallback message_callback_;
    
public:
    FixServer(int port);
    ~FixServer();
    
    bool start();
    void stop();
    
    void set_new_session_callback(std::function<void(std::shared_ptr<FixSession>)> callback) {
        new_session_callback_ = callback;
    }
    
    void set_message_callback(MessageCallback callback) {
        message_callback_ = callback;
    }
    
    // Broadcast message to all connected clients
    void broadcast_message(const FixMessage& message);
    
    // Get all active sessions
    std::vector<std::shared_ptr<FixSession>> get_active_sessions() const;
    
private:
    void accept_loop();
    void handle_new_client(int client_socket);
};

} // namespace fix

#endif // FIX_SESSION_HPP
