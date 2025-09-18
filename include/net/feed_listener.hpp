#ifndef NET_FEED_LISTENER_HPP
#define NET_FEED_LISTENER_HPP

#include <atomic>
#include <thread>
#include <string>
#include <vector>
#include <array>
#include "lock_free_queue.hpp"
#include "core/packet.hpp"

namespace net {

class FeedListener {
public:
  FeedListener(const std::string& mcast_group, int port, size_t q_capacity)
      : mcast_group_(mcast_group), port_(port), queue_(q_capacity), ring_(q_capacity) {}

  bool start();
  void stop();

  // Non-blocking pop; returns false if empty
  bool pop(core::PacketView& pkt) { return queue_.pop(pkt); }

private:
  void run();

  std::string mcast_group_;
  int port_;
  std::atomic<bool> running_{false};
  std::thread th_;
  LockFreeQueue<core::PacketView> queue_;
  std::vector<std::array<char, 4096>> ring_;
  size_t ring_idx_{0};
};

} // namespace net

#endif // NET_FEED_LISTENER_HPP

