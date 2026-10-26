#pragma once
/*  order_state.hpp — order state machine and live-exposure accounting (R-ORD-1..6).

        PendingNew ──ack──► Open ──fill──► PartiallyFilled ──fill──► Filled
            │  │              │                   │
            │  └─post-only/reject──► Rejected     └──cancel req──► CancelPending ──confirm──► Cancelled
            │                 └──cancel req────────────────────────────┘
            └──no ack within timeout──► Unknown   (also CancelPending ──no confirm──► Unknown)

    * Every state except Filled / Cancelled / Rejected can still fill — PendingNew, Unknown and CancelPending
      included (Lighter's 300 ms cancel delay is real). Worst-case exposure per side = position + Σ remaining of the
      fillable orders on that side; the inventory cap is applied to that figure.
    * Unknown is NEVER resolved to "gone" by a timeout. It leaves Unknown only on an exchange event (ack, fill,
      cancel confirm) or on reconcile() with an explicit exchange answer.
    * Fills are applied idempotently by exchange trade id and are never lost: the id is recorded only after the
      fill is applied; a fill for a terminal order (late, after the cancel confirm) still moves the position; a fill
      for an order not known yet is held (or, with market/side, applied to the position) and flags reconciliation.
    * A cancel requested while an order is Unknown is re-issued when the order is acked (on_ack returns true).
    * Illegal transitions throw.
    * Modify is preferred over cancel+create when only the price changes; an order with a modify in flight counts
      its exposure at the worse of the old and new price.                                                    */

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace lighter::live {

enum class OrderState { PendingNew, Open, PartiallyFilled, Filled, CancelPending, Cancelled, Rejected, Unknown };
enum class RejectReason { None, PostOnlyCross, MinSize, ApiReject, DuplicateClientIndex, Other };

inline const char* state_name(OrderState s) {
    switch (s) {
        case OrderState::PendingNew: return "PendingNew";
        case OrderState::Open: return "Open";
        case OrderState::PartiallyFilled: return "PartiallyFilled";
        case OrderState::Filled: return "Filled";
        case OrderState::CancelPending: return "CancelPending";
        case OrderState::Cancelled: return "Cancelled";
        case OrderState::Rejected: return "Rejected";
        case OrderState::Unknown: return "Unknown";
    }
    return "?";
}
inline bool is_terminal(OrderState s) {
    return s == OrderState::Filled || s == OrderState::Cancelled || s == OrderState::Rejected;
}
inline bool is_fillable(OrderState s) { return !is_terminal(s); }

struct OrderStateError : std::logic_error { using std::logic_error::logic_error; };

// Lighter order status codes (lighter_account_spec.md §3).
inline constexpr int kStatusOpen = 2, kStatusFilled = 3, kStatusCanceled = 4, kStatusCanceledPostOnly = 5;

struct Order {
    int64_t client_index = 0;
    int64_t order_index = 0;      // exchange index (≥ 2^48) once known
    int64_t market = -1;
    bool is_ask = false;
    int64_t base = 0;             // total size
    int64_t price = 0;
    int64_t filled = 0;
    OrderState state = OrderState::PendingNew;
    RejectReason reject = RejectReason::None;
    int64_t since_ms = 0;         // time of the last transition (wall, for the ack timeout only)
    bool modify_pending = false;
    int64_t modify_price = 0, modify_base = 0;
    bool cancel_requested_while_unknown = false;
    int64_t remaining() const { return base - filled; }
};

// client_order_index = (run_id << 20) | seq, never repeats across runs (run_id persisted monotonic by the caller).
class ClientIndexAllocator {
public:
    explicit ClientIndexAllocator(int64_t run_id) : run_(run_id) {
        if (run_id < 1 || run_id >= (int64_t{1} << 28)) throw std::invalid_argument("run id must be 1..2^28-1");
    }
    int64_t next() {
        if (seq_ >= (int64_t{1} << 20) - 1) throw std::runtime_error("client index space of this run exhausted");
        return (run_ << 20) | ++seq_;
    }
    bool owns(int64_t coi) const { return (coi >> 20) == run_; }

private:
    int64_t run_, seq_ = 0;
};

struct SideExposure {
    int64_t position = 0;          // signed base, from the EXCHANGE
    int64_t bid_fillable = 0;      // Σ remaining of fillable bids
    int64_t ask_fillable = 0;
    int64_t worst_long() const { return position + bid_fillable; }
    int64_t worst_short() const { return -position + ask_fillable; }
};

enum class RequoteAction { Keep, Modify, CancelThenCreate, Blocked };

class OrderTracker {
public:
    Order& add_pending(int64_t coi, int64_t market, bool is_ask, int64_t base, int64_t price, int64_t now_ms) {
        if (orders_.count(coi)) throw OrderStateError("client index reused: " + std::to_string(coi));
        if (base <= 0 || price <= 0) throw OrderStateError("bad order size/price");
        Order o;
        o.client_index = coi; o.market = market; o.is_ask = is_ask; o.base = base; o.price = price; o.since_ms = now_ms;
        Order& r = orders_[coi] = o;
        // fills that arrived (with market/side) before we knew this order: already in position_, attribute them now
        if (auto it = orphan_qty_.find(coi); it != orphan_qty_.end()) {
            r.filled = std::min(r.base, it->second);
            if (it->second > r.base) reconcile_needed_ = true;
            if (r.filled == r.base) r.state = OrderState::Filled;
            else if (r.filled > 0) r.state = OrderState::PartiallyFilled;
            orphan_qty_.erase(it);
        }
        return r;
    }

    const Order& get(int64_t coi) const {
        auto it = orders_.find(coi);
        if (it == orders_.end()) throw OrderStateError("unknown client index " + std::to_string(coi));
        return it->second;
    }
    bool has(int64_t coi) const { return orders_.count(coi) > 0; }
    const std::map<int64_t, Order>& all() const { return orders_; }

    // Returns true when a cancel that was requested while the order was Unknown must be (re-)sent now: the order is
    // then put in CancelPending and the caller submits the cancel intent.
    bool on_ack(int64_t coi, int64_t order_index, int64_t now_ms) {
        Order& o = ref(coi);
        o.order_index = order_index;
        switch (o.state) {
            case OrderState::PendingNew: case OrderState::Unknown:
                if (o.state == OrderState::Unknown && o.cancel_requested_while_unknown) {
                    o.cancel_requested_while_unknown = false;
                    move(o, OrderState::CancelPending, now_ms);
                    return true;
                }
                move(o, o.filled > 0 ? OrderState::PartiallyFilled : OrderState::Open, now_ms); return false;
            case OrderState::CancelPending: case OrderState::Open: case OrderState::PartiallyFilled:
            case OrderState::Filled: case OrderState::Cancelled:
                return false;   // late / duplicate ack (an ack can trail a fill or a cancel confirm)
            default: illegal(o, "ack");
        }
    }

    // A fill is NEVER discarded. Returns true when applied by this call, false for a duplicate trade id or when the
    // order is not known yet (then the fill is HELD, its trade id is not marked seen, and a redelivery applies it;
    // held fills also make reconcile_needed() true). The trade id is recorded only after the fill is applied.
    // Fills on terminal orders (a fill that arrives after the cancel confirm) are applied too; position_ always
    // moves. An overfill is applied to the position (exchange truth) and flags reconciliation.
    bool on_fill(int64_t trade_id, int64_t coi, int64_t qty, int64_t now_ms) {
        if (qty <= 0) throw OrderStateError("fill qty must be > 0");
        if (trade_ids_.count(trade_id)) { ++duplicate_fills_; return false; }
        auto it = orders_.find(coi);
        if (it == orders_.end()) { held_[trade_id] = {coi, qty}; return false; }
        apply_fill(it->second, trade_id, qty, now_ms);
        return true;
    }
    // Same, for a fill whose market and side are known from the exchange record: an unknown order's fill moves the
    // position immediately and is attributed when the order is added (or by reconciliation).
    bool on_fill(int64_t trade_id, int64_t coi, int64_t qty, int64_t now_ms, int64_t market, bool is_ask) {
        if (qty <= 0) throw OrderStateError("fill qty must be > 0");
        if (trade_ids_.count(trade_id)) { ++duplicate_fills_; return false; }
        auto it = orders_.find(coi);
        if (it != orders_.end()) { apply_fill(it->second, trade_id, qty, now_ms); return true; }
        position_[market] += is_ask ? -qty : qty;
        orphan_qty_[coi] += qty;
        trade_ids_.insert(trade_id);
        held_.erase(trade_id);
        reconcile_needed_ = true;
        return true;
    }
    // Apply fills held for an order that is now known (e.g. right after add_pending).
    int apply_held(int64_t coi, int64_t now_ms) {
        int n = 0;
        for (auto it = held_.begin(); it != held_.end();) {
            if (it->second.first != coi || !orders_.count(coi)) { ++it; continue; }
            auto [tid, f] = *it;
            it = held_.erase(it);
            apply_fill(orders_.at(coi), tid, f.second, now_ms);
            ++n;
        }
        return n;
    }
    size_t held_fills() const { return held_.size(); }
    bool reconcile_needed() const { return reconcile_needed_ || !held_.empty(); }
    void clear_reconcile_flag() { reconcile_needed_ = false; }
    uint64_t late_fills() const { return late_fills_; }

    void request_cancel(int64_t coi, int64_t now_ms) {
        Order& o = ref(coi);
        switch (o.state) {
            case OrderState::PendingNew: case OrderState::Open: case OrderState::PartiallyFilled:
                o.modify_pending = false;
                move(o, OrderState::CancelPending, now_ms); return;
            case OrderState::Unknown: o.cancel_requested_while_unknown = true; return;   // stays Unknown
            case OrderState::CancelPending: return;
            default: illegal(o, "cancel");
        }
    }

    void request_modify(int64_t coi, int64_t price, int64_t base) {
        Order& o = ref(coi);
        if (!(o.state == OrderState::Open || o.state == OrderState::PartiallyFilled) || o.modify_pending) illegal(o, "modify");
        if (base < o.filled + 1 || price <= 0) throw OrderStateError("modify size/price invalid");
        o.modify_pending = true; o.modify_price = price; o.modify_base = base;
    }
    void on_modified(int64_t coi, int64_t now_ms) {
        Order& o = ref(coi);
        if (!o.modify_pending) illegal(o, "modify-confirm");
        o.price = o.modify_price; o.base = o.modify_base; o.modify_pending = false; o.since_ms = now_ms;
    }
    void on_modify_failed(int64_t coi) { ref(coi).modify_pending = false; }

    // Exchange says the order is cancelled. status 5 (post-only would cross) on a new order = Rejected.
    void on_cancelled(int64_t coi, int status, int64_t now_ms) {
        Order& o = ref(coi);
        if (status == kStatusCanceledPostOnly && o.state == OrderState::PendingNew) {
            o.reject = RejectReason::PostOnlyCross;
            ++post_only_rejects_;
            move(o, OrderState::Rejected, now_ms); return;
        }
        if (!is_fillable(o.state)) illegal(o, "cancel-confirm");
        o.modify_pending = false;
        move(o, OrderState::Cancelled, now_ms);
    }

    // API reject (never reached the book) or sequencer reject of a create.
    void on_rejected(int64_t coi, RejectReason r, int64_t now_ms) {
        Order& o = ref(coi);
        if (o.state != OrderState::PendingNew) illegal(o, "reject");
        o.reject = r;
        move(o, OrderState::Rejected, now_ms);
    }

    // PendingNew / CancelPending without an exchange answer for ack_timeout_ms → Unknown (still exposure).
    std::vector<int64_t> on_timeout(int64_t now_ms, int64_t ack_timeout_ms) {
        std::vector<int64_t> out;
        for (auto& [coi, o] : orders_)
            if ((o.state == OrderState::PendingNew || o.state == OrderState::CancelPending) && now_ms - o.since_ms >= ack_timeout_ms) {
                if (o.state == OrderState::CancelPending) o.cancel_requested_while_unknown = true;
                move(o, OrderState::Unknown, now_ms);
                out.push_back(coi);
            }
        return out;
    }

    // The order's tx was rejected by the sequencer before applying, or its outcome is otherwise in doubt: it becomes
    // Unknown (still exposure) until reconciliation answers. A cancel that was pending is remembered.
    void mark_unknown(int64_t coi, int64_t now_ms) {
        Order& o = ref(coi);
        if (o.state == OrderState::PendingNew || o.state == OrderState::CancelPending) {
            if (o.state == OrderState::CancelPending) o.cancel_requested_while_unknown = true;
            move(o, OrderState::Unknown, now_ms);
        }
    }

    // Reconciliation with an explicit exchange answer: status ∈ {open, filled, canceled, canceled-post-only}.
    void reconcile(int64_t coi, int exchange_status, int64_t exchange_filled, int64_t order_index, int64_t now_ms) {
        Order& o = ref(coi);
        if (is_terminal(o.state)) return;
        if (exchange_status == kStatusFilled) exchange_filled = std::max(exchange_filled, o.base);
        if (exchange_filled > o.filled) {   // fills we missed: apply the difference (journal source=reconcile)
            int64_t d = exchange_filled - o.filled;
            o.filled = std::min(exchange_filled, o.base);
            position_[o.market] += o.is_ask ? -d : d;
        }
        if (order_index) o.order_index = order_index;
        switch (exchange_status) {
            case kStatusOpen: move(o, o.filled > 0 ? OrderState::PartiallyFilled : OrderState::Open, now_ms); break;
            case kStatusFilled: move(o, OrderState::Filled, now_ms); break;
            case kStatusCanceledPostOnly:
                o.reject = RejectReason::PostOnlyCross; move(o, OrderState::Rejected, now_ms); break;
            default: move(o, OrderState::Cancelled, now_ms); break;
        }
    }

    // Inventory comes from the exchange (R-START-2/A3). Fills move the local estimate until the next snapshot.
    void set_position(int64_t market, int64_t base) { position_[market] = base; }
    int64_t position(int64_t market) const { auto it = position_.find(market); return it == position_.end() ? 0 : it->second; }

    SideExposure exposure(int64_t market) const {
        SideExposure e;
        e.position = position(market);
        for (auto& [_, o] : orders_) {
            if (o.market != market || !is_fillable(o.state)) continue;
            int64_t rem = o.modify_pending ? std::max(o.remaining(), o.modify_base - o.filled) : o.remaining();
            (o.is_ask ? e.ask_fillable : e.bid_fillable) += rem;
        }
        return e;
    }
    // Σ notional (base × price integer units) of fillable orders, worst of old/new price when a modify is pending.
    __int128 fillable_notional_units(int64_t market) const {
        __int128 n = 0;
        for (auto& [_, o] : orders_) {
            if (o.market != market || !is_fillable(o.state)) continue;
            int64_t px = o.modify_pending ? std::max(o.price, o.modify_price) : o.price;
            int64_t rem = o.modify_pending ? std::max(o.remaining(), o.modify_base - o.filled) : o.remaining();
            n += __int128(rem) * px;
        }
        return n;
    }

    // The one live order on a side (non-terminal), or 0. Two would be a bug.
    int64_t live_on_side(int64_t market, bool is_ask) const {
        int64_t found = 0;
        for (auto& [coi, o] : orders_)
            if (o.market == market && o.is_ask == is_ask && is_fillable(o.state)) {
                if (found) throw OrderStateError("two live orders on one side");
                found = coi;
            }
        return found;
    }
    bool slot_free(int64_t market, bool is_ask) const { return live_on_side(market, is_ask) == 0; }

    // Requote decision: keep within band; modify when only the price moves on an acked order; otherwise
    // cancel first (the create waits until the old order is terminal).
    RequoteAction plan_requote(int64_t coi, int64_t new_price, int64_t new_base, int64_t band) const {
        const Order& o = get(coi);
        if (o.state == OrderState::Unknown || o.state == OrderState::CancelPending || o.state == OrderState::PendingNew ||
            o.modify_pending) return RequoteAction::Blocked;
        if (is_terminal(o.state)) return RequoteAction::CancelThenCreate;   // slot free: plain create
        bool same_size = new_base == o.remaining() && o.filled == 0;
        if (same_size && std::llabs(new_price - o.price) <= band) return RequoteAction::Keep;
        if (same_size) return RequoteAction::Modify;
        return RequoteAction::CancelThenCreate;
    }

    // Forget terminal orders whose last transition is older than keep_ms (a 24/7 engine creates thousands a day).
    // A fill that still arrives for one later takes the market/side path (position moves; reconcile is flagged).
    size_t prune_terminal(int64_t now_ms, int64_t keep_ms) {
        size_t n = 0;
        for (auto it = orders_.begin(); it != orders_.end();) {
            if (is_terminal(it->second.state) && now_ms - it->second.since_ms >= keep_ms) { it = orders_.erase(it); ++n; }
            else ++it;
        }
        return n;
    }

    uint64_t duplicate_fills() const { return duplicate_fills_; }
    uint64_t post_only_rejects() const { return post_only_rejects_; }

private:
    Order& ref(int64_t coi) {
        auto it = orders_.find(coi);
        if (it == orders_.end()) throw OrderStateError("unknown client index " + std::to_string(coi));
        return it->second;
    }
    static void move(Order& o, OrderState s, int64_t now_ms) { o.state = s; o.since_ms = now_ms; }

    void apply_fill(Order& o, int64_t trade_id, int64_t qty, int64_t now_ms) {
        position_[o.market] += o.is_ask ? -qty : qty;   // always: the position is exchange truth
        if (o.filled + qty > o.base) { o.filled = o.base; reconcile_needed_ = true; }   // overfill: flag, never drop
        else o.filled += qty;
        trade_ids_.insert(trade_id);                     // recorded only once applied
        held_.erase(trade_id);
        if (is_terminal(o.state)) {                      // e.g. fill delivered after the cancel confirm
            ++late_fills_;
            if (o.state != OrderState::Cancelled) reconcile_needed_ = true;
            return;
        }
        if (o.filled == o.base) { move(o, OrderState::Filled, now_ms); return; }
        if (o.state == OrderState::PendingNew || o.state == OrderState::Open) move(o, OrderState::PartiallyFilled, now_ms);
        // CancelPending / Unknown keep their state with the fill applied
    }
    [[noreturn]] static void illegal(const Order& o, const char* ev) {
        throw OrderStateError(std::string("illegal ") + ev + " in state " + state_name(o.state) + " (coi " +
                              std::to_string(o.client_index) + ")");
    }

    std::map<int64_t, Order> orders_;
    std::set<int64_t> trade_ids_;
    std::map<int64_t, int64_t> position_;
    std::map<int64_t, std::pair<int64_t, int64_t>> held_;   // trade id → (client index, qty), order not known yet
    std::map<int64_t, int64_t> orphan_qty_;                  // client index → qty already in position_
    bool reconcile_needed_ = false;
    uint64_t duplicate_fills_ = 0, post_only_rejects_ = 0, late_fills_ = 0;
};

}  // namespace lighter::live
