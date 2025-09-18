#include "net/arbiter.hpp"
#include "itch/messages.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace net {

static inline uint16_t tracking_number_from(const core::PacketView& pkt) {
  if (pkt.len < sizeof(CommonHeader)) return 0;
  CommonHeader hdr; std::memcpy(&hdr, pkt.data, sizeof(hdr));
  return ntohs(hdr.trackingNumber);
}

static inline uint32_t message_size_from(const core::PacketView& pkt) {
  if (pkt.len == 0) return 0;
  return itch_message_size(pkt.data[0]);
}

void Arbiter::load_feed_messages(PopFn& pop, std::deque<core::PacketView>& buf) {
  core::PacketView pkt;
  while (pop(pkt)) {
    const char* cur = pkt.data;
    const char* end = cur + pkt.len;
    while (cur < end) {
      uint32_t msz = itch_message_size(*cur);
      if (msz == 0 || cur + msz > end) break;
      core::PacketView msg{cur, msz};
      buf.push_back(msg);
      cur += msz;
    }
  }
}

void Arbiter::prune_expired() {
  const auto now = Clock::now();
  while (!gap_.empty()) {
    auto it = gap_.begin();
    if (now - it->second.ts > ttl_) {
      gap_.erase(it);
      ++metrics_.gap_dropped_ttl;
    } else { break; }
  }
}

/* removed legacy next() */

std::optional<core::PacketView> Arbiter::next_message() {
  prune_expired();

  // If we have ready (gap-drained) messages, serve them first
  if (!ready_.empty()) {
    staging_ = ready_.front();
    ready_.pop_front();
    return core::PacketView{ staging_.bytes, staging_.len };
  }

  // Fill per-feed message buffers from new packets
  load_feed_messages(popA_, bufA_);
  load_feed_messages(popB_, bufB_);

  // Choose next message across feeds by tracking number ordering
  auto pick_next = [&](std::deque<core::PacketView>& a, std::deque<core::PacketView>& b) -> std::optional<core::PacketView> {
    if (a.empty() && b.empty()) return std::nullopt;
    bool chooseA = false;
    if (!a.empty() && !b.empty()) {
      chooseA = tracking_number_from(a.front()) <= tracking_number_from(b.front());
    } else if (!a.empty()) chooseA = true;
    auto& src = chooseA ? a : b;
    if (src.empty()) return std::nullopt;
    core::PacketView msg = src.front(); src.pop_front();

    const uint16_t tn = tracking_number_from(msg);
    if (tn == 0) return msg;
    if (tn < expected_) { ++metrics_.dup_dropped; return std::nullopt; }
    if (tn > expected_) {
      if (gap_.size() >= gap_capacity_) { ++metrics_.gap_dropped_capacity; gap_.erase(gap_.begin()); }
      GapItem gi; gi.pkt.len = msg.len; std::memcpy(gi.pkt.bytes, msg.data, msg.len > sizeof(gi.pkt.bytes) ? sizeof(gi.pkt.bytes) : msg.len);
      gap_.emplace(tn, GapItem{gi.pkt, Clock::now()}); ++metrics_.gap_detected; return std::nullopt;
    }
    // In-order message: advance expected and drain any consecutive gaps
    ++expected_;
    while (true) {
      auto it = gap_.find(expected_);
      if (it == gap_.end()) break;
      ready_.push_back(it->second.pkt);
      gap_.erase(it);
      ++metrics_.gap_filled;
      ++expected_;
    }
    return msg;
  };

  if (auto m = pick_next(bufA_, bufB_)) return m;
  if (auto m = pick_next(bufB_, bufA_)) return m;
  return std::nullopt;
}

} // namespace net

