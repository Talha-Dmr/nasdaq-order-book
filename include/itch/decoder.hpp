#ifndef ITCH_DECODER_HPP
#define ITCH_DECODER_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include "itch/messages.hpp"
#include "core/event.hpp"
#include "core/symbol_table.hpp"

namespace itch {

struct DecodeResult {
  std::optional<core::ItchEvent> event; // empty for non-order events
  uint32_t message_size{0};             // bytes consumed
};

class Decoder {
public:
  explicit Decoder(core::SymbolTable& symtab) : symtab_(symtab) {}

  // Decodes a single message at ptr (size >= minimal). Always returns message_size.
  DecodeResult decode_one(const char* ptr, size_t size) const;

private:
  core::SymbolTable& symtab_;
};

} // namespace itch

#endif // ITCH_DECODER_HPP

