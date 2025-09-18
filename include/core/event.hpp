#ifndef CORE_EVENT_HPP
#define CORE_EVENT_HPP

#include <cstdint>
#include <variant>

namespace core {

struct AddEvt   { uint64_t id; char side; uint32_t qty; uint32_t px; uint16_t sym_id; };
struct ExecEvt  { uint64_t id; uint32_t exec_qty; };
struct CancelEvt{ uint64_t id; uint32_t qty; };
struct DeleteEvt{ uint64_t id; };
struct ReplaceEvt{ uint64_t old_id; uint64_t new_id; uint32_t qty; uint32_t px; uint16_t sym_id; };

using ItchEvent = std::variant<AddEvt, ExecEvt, CancelEvt, DeleteEvt, ReplaceEvt>;

} // namespace core

#endif // CORE_EVENT_HPP

