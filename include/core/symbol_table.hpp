#ifndef CORE_SYMBOL_TABLE_HPP
#define CORE_SYMBOL_TABLE_HPP

#include <array>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace core {

// Simple symbol table: maps 8-char (padded) symbol to uint16_t id.
class SymbolTable {
public:
  using SymbolId = uint16_t;

  SymbolTable() : next_id_(1) {}

  // Accepts pointer to 8-char array (may be space padded). Returns stable id.
  SymbolId get_or_intern(const char* sym8) {
    // Create trimmed view without allocation
    std::string_view sv(sym8, 8);
    while (!sv.empty() && sv.back() == ' ') sv.remove_suffix(1);

    auto it = map_.find(sv);
    if (it != map_.end()) return it->second;

    SymbolId id = next_id_++;
    // Store key in storage_ to own the bytes
    auto& slot = storage_[id];
    for (size_t i = 0; i < 8; ++i) slot[i] = sym8[i];
    slot[8] = '\0';
    map_.emplace(std::string_view(slot.data(), sv.size()), id);
    return id;
  }

  // Read-only view of stored symbol bytes
  std::string_view view(SymbolId id) const {
    if (id == 0) return {};
    const auto& slot = storage_[id];
    std::string_view sv(slot.data());
    return sv;
  }

private:
  // Up to 65535 symbols
  std::array<std::array<char,9>, 65536> storage_{}; // 8 + NUL
  std::unordered_map<std::string_view, SymbolId> map_{}; // views into storage_
  SymbolId next_id_;
};

} // namespace core

#endif // CORE_SYMBOL_TABLE_HPP

