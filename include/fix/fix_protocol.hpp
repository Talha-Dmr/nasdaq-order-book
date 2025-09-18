#ifndef FIX_PROTOCOL_HPP
#define FIX_PROTOCOL_HPP

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>
#include <sstream>
#include <iomanip>

namespace fix {

// FIX Protocol Constants
constexpr char SOH = '\001'; // Start of Header separator
constexpr const char* VERSION_4_2 = "FIX.4.2";
constexpr const char* VERSION_4_4 = "FIX.4.4";

// Standard FIX Tags
enum class FixTag : int {
    // Header tags
    BeginString = 8,
    BodyLength = 9,
    MsgType = 35,
    SenderCompID = 49,
    TargetCompID = 56,
    MsgSeqNum = 34,
    SendingTime = 52,
    CheckSum = 10,
    
    // Session tags
    HeartBtInt = 108,
    TestReqID = 112,
    
    // Order tags
    ClOrdID = 11,
    OrderID = 37,
    ExecID = 17,
    ExecType = 150,
    OrdStatus = 39,
    Symbol = 55,
    Side = 54,
    OrderQty = 38,
    OrdType = 40,
    Price = 44,
    TimeInForce = 59,
    TransactTime = 60,
    
    // Execution tags
    LastShares = 32,
    LastPx = 31,
    CumQty = 14,
    AvgPx = 6,
    LeavesQty = 151,
    
    // Market Data tags
    MDReqID = 262,
    SubscriptionRequestType = 263,
    MarketDepth = 264,
    MDUpdateType = 265,
    NoMDEntries = 268,
    MDEntryType = 269,
    MDEntryPx = 270,
    MDEntrySize = 271,
    
    // Common tags
    Text = 58,
    EncryptMethod = 98,
    Username = 553,
    Password = 554
};

// FIX Message Types
enum class MsgType {
    HEARTBEAT = 'A',
    TEST_REQUEST = '1',
    RESEND_REQUEST = '2',
    REJECT = '3',
    SEQUENCE_RESET = '4',
    LOGOUT = '5',
    LOGON = 'A',
    NEW_ORDER_SINGLE = 'D',
    EXECUTION_REPORT = '8',
    ORDER_CANCEL_REQUEST = 'F',
    ORDER_CANCEL_REPLACE_REQUEST = 'G',
    MARKET_DATA_REQUEST = 'V',
    MARKET_DATA_SNAPSHOT_FULL_REFRESH = 'W',
    MARKET_DATA_INCREMENTAL_REFRESH = 'X'
};

// Order sides
enum class Side : char {
    BUY = '1',
    SELL = '2'
};

// Order types
enum class OrdType : char {
    MARKET = '1',
    LIMIT = '2'
};

// Time in force
enum class TimeInForce : char {
    DAY = '0',
    GOOD_TILL_CANCEL = '1',
    IMMEDIATE_OR_CANCEL = '3',
    FILL_OR_KILL = '4'
};

// Order status
enum class OrdStatus : char {
    NEW = '0',
    PARTIALLY_FILLED = '1',
    FILLED = '2',
    CANCELED = '4',
    REJECTED = '8'
};

// Execution types
enum class ExecType : char {
    NEW = '0',
    PARTIAL_FILL = '1',
    FILL = '2',
    CANCELED = '4',
    REPLACE = '5',
    REJECTED = '8'
};

// Market data entry types
enum class MDEntryType : char {
    BID = '0',
    OFFER = '1',
    TRADE = '2'
};

// FIX Field representation
struct FixField {
    int tag;
    std::string value;
    
    FixField() = default;
    FixField(int t, const std::string& v) : tag(t), value(v) {}
    FixField(FixTag t, const std::string& v) : tag(static_cast<int>(t)), value(v) {}
    
    template<typename T>
    FixField(FixTag t, T val) : tag(static_cast<int>(t)) {
        if constexpr (std::is_arithmetic_v<T>) {
            value = std::to_string(val);
        } else {
            value = std::string(val);
        }
    }
    
    std::string to_string() const {
        return std::to_string(tag) + "=" + value + SOH;
    }
};

// FIX Message class
class FixMessage {
private:
    std::unordered_map<int, std::string> fields_;
    std::vector<int> field_order_; // Preserve field ordering
    
public:
    FixMessage() = default;
    
    // Add field
    void add_field(const FixField& field) {
        if (fields_.find(field.tag) == fields_.end()) {
            field_order_.push_back(field.tag);
        }
        fields_[field.tag] = field.value;
    }
    
    void add_field(FixTag tag, const std::string& value) {
        add_field(FixField(tag, value));
    }
    
    template<typename T>
    void add_field(FixTag tag, T value) {
        add_field(FixField(tag, value));
    }
    
    // Get field
    std::optional<std::string> get_field(FixTag tag) const {
        auto it = fields_.find(static_cast<int>(tag));
        return (it != fields_.end()) ? std::optional<std::string>(it->second) : std::nullopt;
    }
    
    std::optional<std::string> get_field(int tag) const {
        auto it = fields_.find(tag);
        return (it != fields_.end()) ? std::optional<std::string>(it->second) : std::nullopt;
    }
    
    // Check if field exists
    bool has_field(FixTag tag) const {
        return fields_.find(static_cast<int>(tag)) != fields_.end();
    }
    
    // Get field as specific type
    template<typename T>
    std::optional<T> get_field_as(FixTag tag) const {
        auto field = get_field(tag);
        if (!field) return std::nullopt;
        
        if constexpr (std::is_same_v<T, std::string>) {
            return *field;
        } else if constexpr (std::is_same_v<T, int>) {
            return std::stoi(*field);
        } else if constexpr (std::is_same_v<T, double>) {
            return std::stod(*field);
        } else if constexpr (std::is_same_v<T, char>) {
            return field->empty() ? '\0' : (*field)[0];
        }
        return std::nullopt;
    }
    
    // Serialize to FIX string
    std::string to_fix_string() const {
        std::ostringstream oss;
        
        // Build message without checksum first
        std::string body;
        for (int tag : field_order_) {
            if (tag == static_cast<int>(FixTag::BeginString) || 
                tag == static_cast<int>(FixTag::BodyLength) ||
                tag == static_cast<int>(FixTag::CheckSum)) {
                continue; // These are handled specially
            }
            auto it = fields_.find(tag);
            if (it != fields_.end()) {
                body += std::to_string(tag) + "=" + it->second + SOH;
            }
        }
        
        // Add BeginString
        auto begin_string = get_field(FixTag::BeginString);
        if (begin_string) {
            oss << static_cast<int>(FixTag::BeginString) << "=" << *begin_string << SOH;
        }
        
        // Add BodyLength
        oss << static_cast<int>(FixTag::BodyLength) << "=" << body.length() << SOH;
        
        // Add body
        oss << body;
        
        // Calculate and add checksum
        std::string msg = oss.str();
        unsigned char checksum = 0;
        for (char c : msg) {
            checksum += c;
        }
        checksum %= 256;
        
        oss << static_cast<int>(FixTag::CheckSum) << "=" << std::setfill('0') << std::setw(3) << static_cast<int>(checksum) << SOH;
        
        return oss.str();
    }
    
    // Get all fields
    const std::unordered_map<int, std::string>& get_fields() const { return fields_; }
    
    // Clear message
    void clear() {
        fields_.clear();
        field_order_.clear();
    }
    
    // Get message type
    std::optional<char> get_msg_type() const {
        auto msg_type = get_field_as<char>(FixTag::MsgType);
        return msg_type;
    }
};

// FIX Message Parser
class FixParser {
public:
    static std::optional<FixMessage> parse(const std::string& fix_string) {
        if (fix_string.empty()) {
            return std::nullopt;
        }
        
        FixMessage message;
        std::istringstream stream(fix_string);
        std::string field;
        
        while (std::getline(stream, field, SOH)) {
            if (field.empty()) continue;
            
            size_t eq_pos = field.find('=');
            if (eq_pos == std::string::npos) {
                continue; // Invalid field format
            }
            
            try {
                int tag = std::stoi(field.substr(0, eq_pos));
                std::string value = field.substr(eq_pos + 1);
                message.add_field(FixField(tag, value));
            } catch (const std::exception&) {
                // Skip invalid fields
                continue;
            }
        }
        
        // Validate required fields
        if (!message.has_field(FixTag::BeginString) ||
            !message.has_field(FixTag::MsgType)) {
            return std::nullopt;
        }
        
        return message;
    }
    
    // Validate message checksum
    static bool validate_checksum(const std::string& fix_string) {
        size_t checksum_pos = fix_string.rfind("10=");
        if (checksum_pos == std::string::npos) {
            return false;
        }
        
        // Extract stated checksum
        std::string checksum_str = fix_string.substr(checksum_pos + 3, 3);
        int stated_checksum;
        try {
            stated_checksum = std::stoi(checksum_str);
        } catch (const std::exception&) {
            return false;
        }
        
        // Calculate actual checksum
        std::string message_part = fix_string.substr(0, checksum_pos + 3);
        unsigned char calculated_checksum = 0;
        for (char c : message_part) {
            calculated_checksum += c;
        }
        calculated_checksum %= 256;
        
        return calculated_checksum == stated_checksum;
    }
};

// Utility functions
inline std::string current_utc_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&time_t), "%Y%m%d-%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// FIX Message builders
class FixMessageBuilder {
public:
    static FixMessage create_logon(const std::string& sender, const std::string& target, 
                                  int seq_num, int heartbeat_int = 30) {
        FixMessage msg;
        msg.add_field(FixTag::BeginString, VERSION_4_2);
        msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::LOGON));
        msg.add_field(FixTag::SenderCompID, sender);
        msg.add_field(FixTag::TargetCompID, target);
        msg.add_field(FixTag::MsgSeqNum, seq_num);
        msg.add_field(FixTag::SendingTime, current_utc_timestamp());
        msg.add_field(FixTag::EncryptMethod, 0);
        msg.add_field(FixTag::HeartBtInt, heartbeat_int);
        return msg;
    }
    
    static FixMessage create_logout(const std::string& sender, const std::string& target, 
                                   int seq_num, const std::string& reason = "") {
        FixMessage msg;
        msg.add_field(FixTag::BeginString, VERSION_4_2);
        msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::LOGOUT));
        msg.add_field(FixTag::SenderCompID, sender);
        msg.add_field(FixTag::TargetCompID, target);
        msg.add_field(FixTag::MsgSeqNum, seq_num);
        msg.add_field(FixTag::SendingTime, current_utc_timestamp());
        if (!reason.empty()) {
            msg.add_field(FixTag::Text, reason);
        }
        return msg;
    }
    
    static FixMessage create_heartbeat(const std::string& sender, const std::string& target, 
                                      int seq_num, const std::string& test_req_id = "") {
        FixMessage msg;
        msg.add_field(FixTag::BeginString, VERSION_4_2);
        msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::HEARTBEAT));
        msg.add_field(FixTag::SenderCompID, sender);
        msg.add_field(FixTag::TargetCompID, target);
        msg.add_field(FixTag::MsgSeqNum, seq_num);
        msg.add_field(FixTag::SendingTime, current_utc_timestamp());
        if (!test_req_id.empty()) {
            msg.add_field(FixTag::TestReqID, test_req_id);
        }
        return msg;
    }
    
    static FixMessage create_new_order_single(const std::string& sender, const std::string& target,
                                             int seq_num, const std::string& cl_ord_id,
                                             const std::string& symbol, Side side, double quantity,
                                             OrdType ord_type, double price = 0.0,
                                             TimeInForce tif = TimeInForce::DAY) {
        FixMessage msg;
        msg.add_field(FixTag::BeginString, VERSION_4_2);
        msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::NEW_ORDER_SINGLE));
        msg.add_field(FixTag::SenderCompID, sender);
        msg.add_field(FixTag::TargetCompID, target);
        msg.add_field(FixTag::MsgSeqNum, seq_num);
        msg.add_field(FixTag::SendingTime, current_utc_timestamp());
        msg.add_field(FixTag::ClOrdID, cl_ord_id);
        msg.add_field(FixTag::Symbol, symbol);
        msg.add_field(FixTag::Side, static_cast<char>(side));
        msg.add_field(FixTag::OrderQty, quantity);
        msg.add_field(FixTag::OrdType, static_cast<char>(ord_type));
        msg.add_field(FixTag::TimeInForce, static_cast<char>(tif));
        msg.add_field(FixTag::TransactTime, current_utc_timestamp());
        
        if (ord_type == OrdType::LIMIT && price > 0.0) {
            msg.add_field(FixTag::Price, price);
        }
        
        return msg;
    }
    
    static FixMessage create_execution_report(const std::string& sender, const std::string& target,
                                             int seq_num, const std::string& order_id,
                                             const std::string& cl_ord_id, const std::string& exec_id,
                                             ExecType exec_type, OrdStatus ord_status,
                                             const std::string& symbol, Side side,
                                             double leaves_qty, double cum_qty, double avg_px,
                                             double last_shares = 0.0, double last_px = 0.0) {
        FixMessage msg;
        msg.add_field(FixTag::BeginString, VERSION_4_2);
        msg.add_field(FixTag::MsgType, static_cast<char>(MsgType::EXECUTION_REPORT));
        msg.add_field(FixTag::SenderCompID, sender);
        msg.add_field(FixTag::TargetCompID, target);
        msg.add_field(FixTag::MsgSeqNum, seq_num);
        msg.add_field(FixTag::SendingTime, current_utc_timestamp());
        msg.add_field(FixTag::OrderID, order_id);
        msg.add_field(FixTag::ClOrdID, cl_ord_id);
        msg.add_field(FixTag::ExecID, exec_id);
        msg.add_field(FixTag::ExecType, static_cast<char>(exec_type));
        msg.add_field(FixTag::OrdStatus, static_cast<char>(ord_status));
        msg.add_field(FixTag::Symbol, symbol);
        msg.add_field(FixTag::Side, static_cast<char>(side));
        msg.add_field(FixTag::LeavesQty, leaves_qty);
        msg.add_field(FixTag::CumQty, cum_qty);
        msg.add_field(FixTag::AvgPx, avg_px);
        
        if (last_shares > 0.0) {
            msg.add_field(FixTag::LastShares, last_shares);
            msg.add_field(FixTag::LastPx, last_px);
        }
        
        return msg;
    }
};

} // namespace fix

#endif // FIX_PROTOCOL_HPP
