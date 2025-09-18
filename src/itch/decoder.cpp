#include "itch/decoder.hpp"
#include <arpa/inet.h>
#include <cstring>

namespace itch {

static inline uint64_t bswap64(uint64_t v) {
#if defined(__GNUC__)
  return __builtin_bswap64(v);
#else
  return ((v & 0x00000000000000FFull) << 56) |
         ((v & 0x000000000000FF00ull) << 40) |
         ((v & 0x0000000000FF0000ull) << 24) |
         ((v & 0x00000000FF000000ull) << 8)  |
         ((v & 0x000000FF00000000ull) >> 8)  |
         ((v & 0x0000FF0000000000ull) >> 24) |
         ((v & 0x00FF000000000000ull) >> 40) |
         ((v & 0xFF00000000000000ull) >> 56);
#endif
}

DecodeResult Decoder::decode_one(const char* ptr, size_t size) const {
  DecodeResult out;
  if (size < sizeof(CommonHeader)) { return out; }
  const char type = *ptr;
  const uint32_t msize = itch_message_size(type);
  if (msize == 0 || msize > size) { out.message_size = 0; return out; }

  switch (type) {
    case 'A': {
      AddOrderMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      auto sym_id = symtab_.get_or_intern(msg.stockSymbol);
      core::AddEvt e{ bswap64(msg.orderReferenceNumber), msg.buySellIndicator,
                      ntohl(msg.shares), ntohl(msg.price), sym_id };
      out.event = e; out.message_size = msize; return out;
    }
    case 'F': {
      AddOrderWithMPIDMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      auto sym_id = symtab_.get_or_intern(msg.stockSymbol);
      core::AddEvt e{ bswap64(msg.orderReferenceNumber), msg.buySellIndicator,
                      ntohl(msg.shares), ntohl(msg.price), sym_id };
      out.event = e; out.message_size = msize; return out;
    }
    case 'E': {
      OrderExecutedMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      core::ExecEvt e{ bswap64(msg.orderReferenceNumber), ntohl(msg.executedShares) };
      out.event = e; out.message_size = msize; return out;
    }
    case 'C': {
      OrderExecutedWithPriceMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      core::ExecEvt e{ bswap64(msg.orderReferenceNumber), ntohl(msg.executedShares) };
      out.event = e; out.message_size = msize; return out;
    }
    case 'X': {
      OrderCancelMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      core::CancelEvt e{ bswap64(msg.orderReferenceNumber), ntohl(msg.canceledShares) };
      out.event = e; out.message_size = msize; return out;
    }
    case 'D': {
      OrderDeleteMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      core::DeleteEvt e{ bswap64(msg.orderReferenceNumber) };
      out.event = e; out.message_size = msize; return out;
    }
    case 'U': {
      OrderReplaceMessage msg; std::memcpy(&msg, ptr, sizeof(msg));
      // Not: stock symbol yok; symbol değişimi yok varsayıyoruz
      core::ReplaceEvt e{ bswap64(msg.originalOrderReferenceNumber),
                          bswap64(msg.newOrderReferenceNumber),
                          ntohl(msg.shares), ntohl(msg.price), /*sym_id*/0 };
      out.event = e; out.message_size = msize; return out;
    }
    default: {
      // Non-order book affecting messages: S, R ...
      out.event.reset(); out.message_size = msize; return out;
    }
  }
}

} // namespace itch

