#ifndef CORE_APPLY_HPP
#define CORE_APPLY_HPP

#include "core/event.hpp"
#include "order_book.hpp"

namespace core {

// -------- OptimizedOrderBook overloads --------
inline void apply_event(const AddEvt& e, OptimizedOrderBook& ob) {
  ob.addOrder(e.id, e.side, e.qty, e.px);
}
inline void apply_event(const ExecEvt& e, OptimizedOrderBook& ob) {
  ob.executeOrder(e.id, e.exec_qty);
}
inline void apply_event(const CancelEvt& e, OptimizedOrderBook& ob) {
  ob.executeOrder(e.id, e.qty);
}
inline void apply_event(const DeleteEvt& e, OptimizedOrderBook& ob) {
  ob.deleteOrder(e.id);
}
inline void apply_event(const ReplaceEvt& e, OptimizedOrderBook& ob) {
  ob.replaceOrder(e.old_id, e.new_id, e.qty, e.px);
}

inline void apply(const ItchEvent& evt, OptimizedOrderBook& ob) {
  std::visit([&](auto&& ev){ apply_event(ev, ob); }, evt);
}

// -------- UltraOrderBook overloads --------
inline void apply_event(const AddEvt& e, UltraOrderBook& ob) {
  ob.ultra_addOrder(e.id, e.side, e.qty, e.px);
}
inline void apply_event(const ExecEvt& e, UltraOrderBook& ob) {
  ob.ultra_executeOrder(e.id, e.exec_qty);
}
inline void apply_event(const CancelEvt& e, UltraOrderBook& ob) {
  ob.ultra_executeOrder(e.id, e.qty);
}
inline void apply_event(const DeleteEvt& e, UltraOrderBook& ob) {
  ob.ultra_deleteOrder(e.id);
}
inline void apply_event(const ReplaceEvt& e, UltraOrderBook& ob) {
  ob.ultra_replaceOrder(e.old_id, e.new_id, e.qty, e.px);
}

inline void apply(const ItchEvent& evt, UltraOrderBook& ob) {
  std::visit([&](auto&& ev){ apply_event(ev, ob); }, evt);
}

} // namespace core

#endif // CORE_APPLY_HPP

