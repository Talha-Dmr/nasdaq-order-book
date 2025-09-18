#include "net/feed_listener.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <vector>
#include <array>
#include <time.h>

namespace net {

bool FeedListener::start() {
  if (running_.exchange(true)) return false;
  th_ = std::thread(&FeedListener::run, this);
  return true;
}

void FeedListener::stop() {
  if (!running_.exchange(false)) return;
  if (th_.joinable()) th_.join();
}

void FeedListener::run() {
  int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0) { perror("socket"); running_.store(false); return; }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = INADDR_ANY;
  local.sin_port = htons(port_);
  if (bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0) {
    perror("bind");
    ::close(sock);
    running_.store(false);
    return;
  }

  ip_mreq mreq{};
  mreq.imr_multiaddr.s_addr = inet_addr(mcast_group_.c_str());
  mreq.imr_interface.s_addr = htonl(INADDR_ANY);
  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
    perror("IP_ADD_MEMBERSHIP");
    ::close(sock);
    running_.store(false);
    return;
  }

  // Set a small recv timeout to ensure thread can stop promptly
  timeval tv{0, 50 * 1000}; // 50ms
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  constexpr size_t BUF_SZ = 4096;
#ifdef __linux__
  // Use recvmmsg for batched receive mapping directly into ring slots
  const int BATCH = 16;
  std::vector<mmsghdr> msgs(BATCH);
  std::vector<sockaddr_in> addrs(BATCH);
  std::vector<iovec> iovs(BATCH);

  timespec timeout{0, 50 * 1000 * 1000}; // 50ms

  while (running_.load()) {
    // Prepare iovecs mapped to ring slots
    for (int i = 0; i < BATCH; ++i) {
      std::memset(&msgs[i], 0, sizeof(mmsghdr));
      std::memset(&addrs[i], 0, sizeof(sockaddr_in));
      iovs[i].iov_base = ring_[(ring_idx_ + i) % ring_.size()].data();
      iovs[i].iov_len = BUF_SZ;
      msgs[i].msg_hdr.msg_name = &addrs[i];
      msgs[i].msg_hdr.msg_namelen = sizeof(sockaddr_in);
      msgs[i].msg_hdr.msg_iov = &iovs[i];
      msgs[i].msg_hdr.msg_iovlen = 1;
    }

    int n = ::recvmmsg(sock, msgs.data(), BATCH, 0, &timeout);
    if (n <= 0) continue; // timeout or error; re-check running_

    for (int i = 0; i < n; ++i) {
      size_t slot = (ring_idx_ + i) % ring_.size();
      core::PacketView view;
      view.data = ring_[slot].data();
      view.len = static_cast<uint32_t>(msgs[i].msg_len);
      (void)queue_.push(view); // drop if full
      msgs[i].msg_len = 0;
    }
    ring_idx_ = (ring_idx_ + n) % ring_.size();
  }
#else
  // Portable path: recvfrom with SO_RCVTIMEO mapped to ring slots
  while (running_.load()) {
    size_t slot = ring_idx_ % ring_.size();
    ssize_t r = ::recvfrom(sock, ring_[slot].data(), BUF_SZ, 0, nullptr, nullptr);
    if (r > 0) {
      core::PacketView view{ ring_[slot].data(), static_cast<uint32_t>(r) };
      (void)queue_.push(view);
      ring_idx_ = (ring_idx_ + 1) % ring_.size();
    }
  }
#endif
  ::close(sock);
}

} // namespace net

