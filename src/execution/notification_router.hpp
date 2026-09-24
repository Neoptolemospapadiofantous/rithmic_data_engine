// notification_router.hpp — the executor's policy for exchange notifications
// that do not belong to the order we expected. Kept out of executor_main.cpp so
// it is unit-testable; tests/execution/test_incident_replay.cpp replays the
// 2026-09-23 sequence through these functions with the log's own values.
#pragma once

#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>

#include "order_manager.hpp"

namespace notif {

// A fill on our account whose user_tag is not the current entry/stop/exit.
enum class UnownedFill { OTHER_ACCOUNT, DUPLICATE, GUARDS_RUN, HALTED };

// FLAT → run the order manager's stale-stop guards (cancelled stop → unwind,
// unknown → ghost-halt). In a trade → halt entries; the tid=451 reconciliation
// unwinds any net mismatch. Until 2026-09-23 this fill was ignored outright.
inline UnownedFill handle_unowned_fill(OrderManager& om,
                                       const std::string& client_id,
                                       const std::string& server_basket,
                                       const std::string& notif_account,
                                       const std::string& our_account,
                                       double px, int qty,
                                       const std::function<void(const std::string&)>& halt) {
    if (!notif_account.empty() && notif_account != our_account) return UnownedFill::OTHER_ACCOUNT;
    const std::string key = client_id.empty() ? server_basket : client_id;
    if (om.fill_already_processed(key, qty)) return UnownedFill::DUPLICATE;
    if (om.is_flat()) {
        om.on_fill_notification(key, px, qty, /*is_entry_fill=*/false);
        return UnownedFill::GUARDS_RUN;
    }
    halt("unowned_fill_in_trade");
    return UnownedFill::HALTED;
}

// tid=351 cancel outcomes. notify_type 3 = cancel acknowledged; 17 (or the
// status text) = "Cancellation Failed": the order is STILL WORKING.
enum class CancelNotice { ACKED, FAILED, IGNORED };

inline CancelNotice handle_cancel_notification(OrderManager& om, int notify_type,
                                               const std::string& status,
                                               const std::string& user_tag,
                                               const std::string& basket) {
    if (notify_type == 17 || status == "Cancellation Failed") {
        om.on_cancel_failed(!user_tag.empty() ? user_tag : basket);
        return CancelNotice::FAILED;
    }
    if (notify_type == 3) {
        if (!user_tag.empty()) om.on_cancel_confirmed(user_tag);
        else                   om.on_cancel_confirmed_by_server_basket(basket);
        return CancelNotice::ACKED;
    }
    return CancelNotice::IGNORED;
}

// What closes the gap between what the exchange holds and what we hold.
//   • we are FLAT, exchange holds N          → trade N away (the 2026-09-23 case)
//   • same direction, exchange holds MORE    → trade the extra away, keep our trade
//   • exchange holds LESS / flipped / zero   → our position was closed externally
//     (manual RTrader close, broker liquidation): close whatever is left and
//     ADOPT flat — never re-enter to "match our book".
struct UnwindPlan { int qty = 0; bool is_buy = false; int expected = 0; bool adopt_flat = false; };

inline UnwindPlan plan_unwind(int exchange_net, const Position& snap, int cfg_qty) {
    int held = snap.qty > 0 ? snap.qty : cfg_qty;
    UnwindPlan p;
    p.expected = (snap.state == PosState::LONG)  ? +held
               : (snap.state == PosState::SHORT) ? -held : 0;
    int target;
    if (p.expected == 0) {
        target = 0;
    } else if ((exchange_net > 0) == (p.expected > 0) &&
               std::abs(exchange_net) >= std::abs(p.expected)) {
        target = p.expected;                       // extra contracts in our direction
    } else {
        target = 0;                                // externally closed / flipped
        p.adopt_flat = true;
    }
    int diff = exchange_net - target;
    p.qty    = std::abs(diff);
    p.is_buy = diff < 0;
    return p;
}

// Rithmic sends balances / P&L as decimal strings; empty means absent.
inline double parse_decimal(const std::string& s) {
    return s.empty() ? std::nan("") : std::strtod(s.c_str(), nullptr);
}

inline bool broker_loss_breached(double day_pnl, double daily_loss_limit) {
    return !std::isnan(day_pnl) && daily_loss_limit < 0.0 && day_pnl <= daily_loss_limit;
}

} // namespace notif
