#ifndef CORE_PACKET_HPP
#define CORE_PACKET_HPP

#include <cstdint>
#include <cstddef>

namespace core {

// Lightweight view into a packet buffer
struct PacketView {
  const char* data{nullptr};
  uint32_t len{0};
};

// Small inline message storage (enough for largest ITCH message here)
struct SmallMsg {
  uint32_t len{0};
  char bytes[64];
};

} // namespace core

#endif // CORE_PACKET_HPP

