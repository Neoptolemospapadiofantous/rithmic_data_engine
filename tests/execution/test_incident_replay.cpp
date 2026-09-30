/*  ═══════════════════════════════════════════════════════════════════════════
    test_incident_replay.cpp — 2026-09-23 tradeify incident, replayed.

    What happened (executor log 1790170502 → 1790172427, RTrader "Completed" list):
      16:35:02.904  SHORT 2 MNQ filled @30933.25 (limit 30925, 29 ticks slippage);
                    stop BUY 2 @30948.25 submitted (client id, server id not mapped yet)
      16:35:02.961  BE fired on a lagging price → cancel that stop BY CLIENT ID,
                    replacement stop @30932.25 submitted
      16:35:03.122  tid=351 "Cancel received from client" then "Cancellation Failed"
                    (notify_type=17) — UNHANDLED: the 30948.25 stop kept working
      16:35:03.133  replacement stop REJECTED ("buy order stop price must be above
                    trade price") → software SL fallback
      16:35:04.479  software SL: BUY 2 limit → filled @30932.25 → executor FLAT, -$4
      16:35:04.645  FLAT purge dropped the guard for the client-id-cancelled stop
      16:35:16.812  the 30948.25 stop FILLED: BUY 2 @30948.75 → logged
                    "fill for unknown user_tag … ignoring (not our order)"
                    → account LONG 2, executor believed FLAT
      trades 2–5    each SHORT 2 / BUY 2, net unchanged; five restarts never asked
                    the exchange (no PNL-plant session existed)
      17:07:08      Tradeify auto-liquidation SELL 2 @30799.25 — account loss 603.52

    These tests drive the REAL OrderManager and the executor's notification
    routing (notification_router.hpp) with those exact values and assert the
    outcome is now a closed position within seconds, never a hidden one.

    No DB, no Rithmic, no network. Build:
        cmake --build build --target test_incident_replay && ./build/test_incident_replay
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <string>
#include <vector>

#include "../../src/execution/order_manager.hpp"
#include "../../src/execution/notification_router.hpp"

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("Assert failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a)-(b)) > (eps)) throw std::runtime_error("ASSERT_NEAR failed: " #a " vs " #b); } while(0)

// ─── The live tradeify config as of 2026-09-23 (config/tradeify_config.json) ──
static const std::string ACCT = "RTG25785042011";

static OrbConfig tradeify_cfg() {
    OrbConfig c;
    c.symbol             = "MNQ";
    c.exchange           = "CME";
    c.qty                = 2;
    c.sl_points          = 15.0;
    c.trail_step         = 10.0;
    c.trail_be_trigger   = 3.0;
    c.trail_be_offset    = 1.0;
    c.trail_delay_secs   = 300;
    c.point_value        = 2.0;
    c.daily_loss_limit   = -500.0;
    c.trailing_drawdown_cap = 1000.0;
    c.consistency_cap_pct = 0.99;
    c.sl_fire_timeout_ms = 3000;
    c.dry_run            = false;
    return c;
}

struct SentOrder { std::string basket; int qty; int type; bool is_buy; double price; std::string tag; };

struct Fixture {
    OrbConfig     cfg;
    RiskManager   risk;
    LatencyLogger lat;
    OrderManager  om;
    std::vector<SentOrder>   sent;
    std::vector<std::string> cancelled;
    std::vector<std::string> halts;

    Fixture() : cfg(tradeify_cfg()), risk(cfg, 25000.0), lat(), om(cfg, risk, lat) {
        om.set_order_callback([this](const std::string& basket, const std::string&,
                                     const std::string&, int qty, int type, bool is_buy,
                                     double price, const std::string& tag) -> bool {
            sent.push_back({basket, qty, type, is_buy, price, tag});
            return true;
        });
        om.set_cancel_callback([this](const std::string& b) { cancelled.push_back(b); });
    }
    std::function<void(const std::string&)> halt_cb() {
        return [this](const std::string& why) { halts.push_back(why); };
    }
};

// Steps 16:35:02.744 → 16:35:02.961: entry, fill, stop, breakeven cancel by client id.
// Returns the original stop's client id (the one whose cancel failed at Rithmic).
static std::string replay_entry_and_be_cancel(Fixture& f) {
    f.om.on_signal(OrbSignal::SELL, 30926.00, "orb_breakout_short");
    std::string entry = f.om.position_snapshot().basket_id_entry;
    f.om.on_fill_notification(entry, 30933.25, 2, /*is_entry_fill=*/true);
    ASSERT_EQ(f.om.state(), PosState::SHORT);
    std::string old_stop = f.om.position_snapshot().basket_id_stop;
    ASSERT(!old_stop.empty());
    ASSERT_NEAR(f.om.exchange_stop(), 30948.25, 0.001);
    ASSERT_EQ(f.sent.size(), (std::size_t)2);              // entry + stop
    ASSERT(f.sent.back().is_buy && f.sent.back().qty == 2); // BUY 2 stop

    f.om.check_trail_and_stop(30925.75);                     // MFE 7.5 ≥ 3 → BE
    ASSERT_EQ(f.cancelled.back(), old_stop);                 // cancelled by CLIENT id
    std::string new_stop = f.om.position_snapshot().basket_id_stop;
    ASSERT(new_stop != old_stop && !new_stop.empty());       // replacement in flight
    ASSERT_NEAR(f.om.exchange_stop(), 30932.25, 0.001);
    return old_stop;
}

// ─────────────────────────────────────────────────────────────────────────────
// 1. The same messages, in the same order, through the new handlers.
//    "Cancellation Failed" is now honoured: the 30948.25 stop is re-adopted, the
//    software SL does not double-cover, and the stop's own fill closes the trade.
TEST(replay_cancellation_failed_is_honoured_no_double_cover) {
    Fixture f;
    std::string old_stop = replay_entry_and_be_cancel(f);
    std::string new_stop = f.om.position_snapshot().basket_id_stop;

    // 16:35:03.122 tid=351 notify_type=3 "Cancel received from client" — user_tag
    // empty, basket carries our client id (we cancelled by client id).
    auto r1 = notif::handle_cancel_notification(f.om, 3, "Cancel received from client", "", old_stop);
    ASSERT(r1 == notif::CancelNotice::ACKED);
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);      // not really acknowledged

    // 16:35:03.122 tid=351 notify_type=17 "Cancellation Failed"
    auto r2 = notif::handle_cancel_notification(f.om, 17, "Cancellation Failed", "", old_stop);
    ASSERT(r2 == notif::CancelNotice::FAILED);
    ASSERT_EQ(f.om.position_snapshot().basket_id_stop, old_stop);   // re-adopted
    ASSERT_NEAR(f.om.exchange_stop(), 30948.25, 0.001);              // real level back
    ASSERT_EQ(f.cancelled.back(), new_stop);                         // replacement cancelled

    // 16:35:03.133 replacement REJECTED — must not disturb the re-adopted stop.
    f.om.on_order_rejected(new_stop, "buy order stop price must be above trade price");
    ASSERT_EQ(f.om.position_snapshot().basket_id_stop, old_stop);

    // 16:35:04.479 price 30932.50: in the incident the software SL fired here and
    // bought 2 on top of the still-working stop. Now the exchange stop at 30948.25
    // is known to be live and 30932.50 does not breach it → no exit.
    std::size_t sends = f.sent.size();
    f.om.check_trail_and_stop(30932.50);
    ASSERT_EQ(f.om.state(), PosState::SHORT);
    ASSERT_EQ(f.sent.size(), sends);

    // 16:35:16.812 the stop fills BUY 2 @30948.75 — recognised as OUR exit.
    ASSERT(f.om.is_stop_basket(old_stop));
    f.om.on_fill_notification(old_stop, 30948.75, 2, /*is_entry_fill=*/false);
    ASSERT_EQ(f.om.state(), PosState::FLAT);
    Position done;
    ASSERT(f.om.pop_trade_completed(done));
    ASSERT_EQ(done.exit_reason, std::string("exchange_stop"));
    ASSERT_NEAR(done.pnl_points, -15.5, 0.001);             // one honest stop-out
    ASSERT_EQ(f.sent.size(), sends);                         // nothing else was sent
    ASSERT(f.om.net_qty_consistent(0));                      // exchange flat, we flat

    // The replacement's client-id cancel is still unacknowledged → guarded until
    // the ACK arrives (tid=351 type 3 with our user_tag).
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);
    notif::handle_cancel_notification(f.om, 3, "Cancel received from client", new_stop, "SRV-x");
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);
}

// 2. Exactly as it happened — the executor had already closed via the software SL
//    and the orphaned stop fills 12 s later. The fill used to be ignored. Now the
//    guard survived FLAT and the fill is unwound at once: SELL 2 near 30948.75.
TEST(replay_as_it_happened_orphan_fill_is_unwound_not_ignored) {
    Fixture f;
    std::string old_stop = replay_entry_and_be_cancel(f);
    std::string new_stop = f.om.position_snapshot().basket_id_stop;

    // Replacement rejected → software SL mode (no live stop in memory).
    f.om.on_order_rejected(new_stop, "buy order stop price must be above trade price");
    ASSERT(f.om.position_snapshot().basket_id_stop.empty());

    // 16:35:04.479 software SL: BUY 2 limit, filled @30932.25 → FLAT, -$4 gross.
    f.om.check_trail_and_stop(30932.50);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    std::string exit_b = f.om.position_snapshot().basket_id_exit;
    f.om.on_fill_notification(exit_b, 30932.25, 2, false);
    ASSERT_EQ(f.om.state(), PosState::FLAT);
    Position t1; ASSERT(f.om.pop_trade_completed(t1)); ASSERT_NEAR(t1.pnl_points, 1.0, 0.001);

    // 16:35:04.645 FLAT purge — the client-id-cancelled stop's guard must survive.
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);
    // 16:35:09.9 the 5 s post-close window expires (executor calls this).
    f.om.clear_post_close_recancels();
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);

    // 16:35:16.812 tid=352 FILL user_tag=<old stop> basket=37816417 px=30948.75 qty=2
    std::size_t sends = f.sent.size();
    auto r = notif::handle_unowned_fill(f.om, old_stop, "37816417", ACCT, ACCT,
                                        30948.75, 2, f.halt_cb());
    ASSERT(r == notif::UnownedFill::GUARDS_RUN);
    ASSERT_EQ(f.sent.size(), sends + 1);                    // the unwind
    const SentOrder& unwind = f.sent.back();
    ASSERT(!unwind.is_buy);                                  // SELL …
    ASSERT_EQ(unwind.qty, 2);                                // … 2 …
    ASSERT_NEAR(unwind.price, 30948.75 - 1.0, 0.001);        // … crossing the spread
    ASSERT(f.halts.empty());                                 // no manual intervention needed
    ASSERT(!f.om.is_entry_halted());                         // handled, not ghost-halted
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);

    // The unwind fills → still FLAT, exchange flat.
    f.om.on_fill_notification(unwind.basket, 30947.50, 2, false);
    ASSERT_EQ(f.om.state(), PosState::FLAT);
    ASSERT(!f.om.is_entry_halted());
    ASSERT(f.om.net_qty_consistent(0));
    // Cost of the drill vs the incident: (30948.75-30947.50)*2*$2 = $5, not $598.
}

// 3. The fill notification never arrives at all (dropped frame, reconnect gap):
//    the PNL-plant stream still shows net=+2 while we are FLAT. The reconciler
//    waits one grace window, then plans SELL 2, and only once.
TEST(replay_reconciler_closes_a_hidden_long_after_grace) {
    Fixture f;
    NetReconciler rec;
    using V = NetReconciler::Verdict;
    ASSERT(rec.observe(f.om.net_qty_consistent(0), 0, 5000) == V::OK);       // 16:35:04 flat
    ASSERT(rec.observe(f.om.net_qty_consistent(+2), 16812, 5000) == V::MISMATCH_WAIT);
    ASSERT(rec.observe(f.om.net_qty_consistent(+2), 19000, 5000) == V::MISMATCH_WAIT);
    ASSERT(rec.observe(f.om.net_qty_consistent(+2), 21813, 5000) == V::MISMATCH_ACT);
    auto plan = notif::plan_unwind(+2, f.om.position_snapshot(), f.cfg.qty);
    ASSERT_EQ(plan.expected, 0);
    ASSERT_EQ(plan.qty, 2);
    ASSERT(!plan.is_buy);                                                     // SELL 2
    ASSERT(rec.observe(f.om.net_qty_consistent(+2), 23000, 5000) == V::MISMATCH_WAIT); // acted once
    ASSERT(rec.observe(f.om.net_qty_consistent(0),  24000, 5000) == V::OK);   // unwind filled
    // Had this existed: closed at ~16:35:22 near 30930 → ≈ $75, not $598 at 17:07.
}

// 4. Broker day P&L guard on the numbers the PNL plant actually reported.
TEST(broker_day_pnl_guard_on_reported_values) {
    ASSERT(notif::broker_loss_breached(notif::parse_decimal("-605.34"), -500.0));
    ASSERT(!notif::broker_loss_breached(notif::parse_decimal("-25.00"),  -500.0));
    ASSERT(!notif::broker_loss_breached(notif::parse_decimal(""),        -500.0)); // absent → no action
    ASSERT(!notif::broker_loss_breached(notif::parse_decimal("-605.34"), 0.0));    // limit disabled
    ASSERT(std::isnan(notif::parse_decimal("")));
    ASSERT_NEAR(notif::parse_decimal("24584.52"), 24584.52, 0.001);
}

// 4b. Broker trailing drawdown on the balances the PNL plant reported (2026-09-29:
//     balance 24321.76, never above the 25000 start → $321.76 of room, not $1000).
TEST(broker_trailing_drawdown_on_reported_balances) {
    double hwm = notif::broker_hwm(std::nan(""), 25000.0, std::nan(""));   // nothing persisted
    ASSERT_NEAR(hwm, 25000.0, 0.001);
    hwm = notif::broker_hwm(hwm, 25000.0, notif::parse_decimal("24794.24"));
    ASSERT_NEAR(hwm, 25000.0, 0.001);                                       // below start: no new mark
    ASSERT_NEAR(notif::broker_drawdown_room(24321.76, hwm, 1000.0), 321.76, 0.001);
    ASSERT(!notif::broker_drawdown_breached(24321.76, hwm, 1000.0));
    ASSERT(notif::broker_drawdown_breached(24000.00, hwm, 1000.0));         // at the cap
    ASSERT(notif::broker_drawdown_breached(23990.00, hwm, 1000.0));
    ASSERT(!notif::broker_drawdown_breached(std::nan(""), hwm, 1000.0));    // absent → no action
    ASSERT(!notif::broker_drawdown_breached(23990.00, hwm, 0.0));           // cap disabled
    hwm = notif::broker_hwm(hwm, 25000.0, 25410.50);                         // a new high
    ASSERT_NEAR(hwm, 25410.50, 0.001);
    ASSERT(notif::broker_drawdown_breached(24410.50, hwm, 1000.0));         // trails the mark
    ASSERT_NEAR(notif::broker_hwm(25410.50, 25000.0, 25100.0), 25410.50, 0.001); // persisted mark wins
}

// 5. A fill for ANOTHER account on the same session is still ignored, and a
//    duplicate delivery (tid=351 then tid=352) is not unwound twice.
TEST(other_account_ignored_and_duplicate_not_unwound_twice) {
    Fixture f;
    std::string old_stop = replay_entry_and_be_cancel(f);
    f.om.on_order_rejected(f.om.position_snapshot().basket_id_stop, "rejected");
    f.om.check_trail_and_stop(30932.50);
    f.om.on_fill_notification(f.om.position_snapshot().basket_id_exit, 30932.25, 2, false);
    ASSERT_EQ(f.om.state(), PosState::FLAT);

    auto other = notif::handle_unowned_fill(f.om, old_stop, "37816417", "RTG-OTHER", ACCT,
                                            30948.75, 2, f.halt_cb());
    ASSERT(other == notif::UnownedFill::OTHER_ACCOUNT);
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);      // untouched

    std::size_t sends = f.sent.size();
    ASSERT(notif::handle_unowned_fill(f.om, old_stop, "37816417", ACCT, ACCT, 30948.75, 2,
                                      f.halt_cb()) == notif::UnownedFill::GUARDS_RUN);
    ASSERT_EQ(f.sent.size(), sends + 1);
    ASSERT(notif::handle_unowned_fill(f.om, old_stop, "37816417", ACCT, ACCT, 30948.75, 2,
                                      f.halt_cb()) == notif::UnownedFill::DUPLICATE);
    ASSERT_EQ(f.sent.size(), sends + 1);                     // one unwind only
}

// 6. An unowned fill while we are IN a trade halts entries and defers to the
//    reconciler instead of corrupting the open position's record.
TEST(unowned_fill_in_trade_halts_and_defers_to_reconciler) {
    Fixture f;
    f.om.on_signal(OrbSignal::SELL, 30926.00, "orb_breakout_short");
    f.om.on_fill_notification(f.om.position_snapshot().basket_id_entry, 30933.25, 2, true);
    ASSERT_EQ(f.om.state(), PosState::SHORT);
    std::size_t sends = f.sent.size();
    auto r = notif::handle_unowned_fill(f.om, "MANUAL-RTRADER", "99999", ACCT, ACCT,
                                        30930.00, 2, f.halt_cb());
    ASSERT(r == notif::UnownedFill::HALTED);
    ASSERT_EQ(f.halts.size(), (std::size_t)1);
    ASSERT_EQ(f.halts.back(), std::string("unowned_fill_in_trade"));
    ASSERT_EQ(f.om.state(), PosState::SHORT);                // record untouched
    ASSERT_EQ(f.sent.size(), sends);
    // Exchange now net 0 (a manual BUY 2 flattened the short) while we hold SHORT 2:
    // never re-enter — adopt flat, cancel the stop, record the close.
    auto plan = notif::plan_unwind(0, f.om.position_snapshot(), f.cfg.qty);
    ASSERT_EQ(plan.expected, -2);
    ASSERT(plan.adopt_flat);
    ASSERT_EQ(plan.qty, 0);
    std::size_t cancels = f.cancelled.size();
    f.om.adopt_external_close(30930.00);
    ASSERT_EQ(f.om.state(), PosState::FLAT);
    ASSERT_EQ(f.cancelled.size(), cancels + 1);              // protective stop cancelled
    Position done; ASSERT(f.om.pop_trade_completed(done));
    ASSERT_EQ(done.exit_reason, std::string("external_close"));
    ASSERT_NEAR(done.pnl_points, 3.25, 0.001);               // 30933.25 → 30930.00 short
    ASSERT_EQ(f.sent.size(), sends);                         // nothing was sent

    // Partial external close (we SHORT 2, exchange -1): close the remaining 1, adopt flat.
    Fixture g;
    g.om.on_signal(OrbSignal::SELL, 30926.00, "orb_breakout_short");
    g.om.on_fill_notification(g.om.position_snapshot().basket_id_entry, 30933.25, 2, true);
    auto p2 = notif::plan_unwind(-1, g.om.position_snapshot(), g.cfg.qty);
    ASSERT(p2.adopt_flat); ASSERT_EQ(p2.qty, 1); ASSERT(p2.is_buy);
    // Extra contracts in our direction (we SHORT 2, exchange -4): sell… no — BUY 2 back.
    auto p3 = notif::plan_unwind(-4, g.om.position_snapshot(), g.cfg.qty);
    ASSERT(!p3.adopt_flat); ASSERT_EQ(p3.qty, 2); ASSERT(p3.is_buy);
    // Consistent: nothing to do.
    auto p4 = notif::plan_unwind(-2, g.om.position_snapshot(), g.cfg.qty);
    ASSERT(!p4.adopt_flat); ASSERT_EQ(p4.qty, 0);
}

int main() {
    RUN(replay_cancellation_failed_is_honoured_no_double_cover);
    RUN(replay_as_it_happened_orphan_fill_is_unwound_not_ignored);
    RUN(replay_reconciler_closes_a_hidden_long_after_grace);
    RUN(broker_day_pnl_guard_on_reported_values);
    RUN(broker_trailing_drawdown_on_reported_balances);
    RUN(other_account_ignored_and_duplicate_not_unwound_twice);
    RUN(unowned_fill_in_trade_halts_and_defers_to_reconciler);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
