#ifndef NET_ARBITER_HPP
#define NET_ARBITER_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <functional>
#include <chrono>
#include <deque>
#include "core/packet.hpp"

namespace net {

struct ArbiterMetrics {
  uint64_t gap_detected{0};
  uint64_t gap_filled{0};
  uint64_t dup_dropped{0};
  uint64_t gap_dropped_ttl{0};
  uint64_t gap_dropped_capacity{0};
};

// Arbiter: merges two feeds by tracking number with bounded TTL gap buffering
class Arbiter {
public:
  // Feeds are polled via provided callbacks; return false if empty
  using PopFn = std::function<bool(core::PacketView&)>;

  Arbiter(PopFn popA, PopFn popB, size_t gap_capacity = 65536,
          std::chrono::milliseconds ttl = std::chrono::milliseconds(50))
      : popA_(std::move(popA)), popB_(std::move(popB)),
        gap_capacity_(gap_capacity), ttl_(ttl) {}

  // Message-level arbitration: returns next in-order ITCH message as PacketView
  std::optional<core::PacketView> next_message();


  const ArbiterMetrics& metrics() const { return metrics_; }

private:
  using Clock = std::chrono::steady_clock;
  struct GapItem { core::SmallMsg pkt; Clock::time_point ts; };

  void prune_expired();
  void load_feed_messages(PopFn& pop, std::deque<core::PacketView>& buf);

  uint64_t expected_{1};
  std::map<uint64_t, GapItem> gap_; // ordered for draining
  size_t gap_capacity_;
  std::chrono::milliseconds ttl_;
  ArbiterMetrics metrics_{};
  PopFn popA_;
  PopFn popB_;
  std::deque<core::PacketView> bufA_;
  std::deque<core::PacketView> bufB_;
  std::deque<core::SmallMsg> ready_; // in-order messages drained from gap (owned)
  core::SmallMsg staging_; // staging buffer to expose ready_ as PacketView
};

} // namespace net

#endif // NET_ARBITER_HPP

