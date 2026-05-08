/*  ═══════════════════════════════════════════════════════════════════════════
    test_order_manager.cpp — Unit tests for OrderManager

    Standalone: no external test framework, no DB, no Rithmic connection.
    All network calls are replaced by mock lambdas.

    Build (from repo root):
        g++ -std=c++20 -I src/execution -I src/ \
            tests/execution/test_order_manager.cpp \
            src/execution/order_manager.cpp \
            src/execution/risk_manager.cpp \
            -o build/test_order_manager && ./build/test_order_manager

    CMake target suggestion (add to CMakeLists.txt):
        add_executable(test_order_manager
            tests/execution/test_order_manager.cpp
            src/execution/order_manager.cpp
            src/execution/risk_manager.cpp)
        target_include_directories(test_order_manager PRIVATE src/execution src/)
        target_compile_features(test_order_manager PRIVATE cxx_std_20)
        add_test(NAME order_manager_tests COMMAND test_order_manager)

    State machine under test:
        FLAT → PENDING_ENTRY → LONG/SHORT → PENDING_EXIT → FLAT
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include "../../src/execution/order_manager.hpp"

// ─── Minimal test harness ─────────────────────────────────────────────────────
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

// ─── Helper: build a minimal OrbConfig for testing ───────────────────────────
// dry_run=false so callbacks are exercised; override in individual tests as needed.
static OrbConfig make_cfg(bool dry_run = false) {
    OrbConfig c;
    c.symbol               = "MNQ";
    c.exchange             = "CME";
    c.qty                  = 1;
    c.sl_points            = 10.0;   // tight SL for easy arithmetic
    c.trail_be_trigger     = 5.0;    // activate trailing at +5 pts MFE
    c.trail_step           = 3.0;    // trail stop 3 pts below current price
    c.trail_be_offset      = 1.0;    // move SL to entry+1 at BE trigger
    c.trail_delay_secs     = 0;      // no delay — activate trailing immediately in tests
    c.point_value          = 2.0;    // MNQ: $2/point
    c.daily_loss_limit     = -5000.0;
    c.trailing_drawdown_cap = 99999.0;
    c.consistency_cap_pct  = 0.99;   // effectively disabled
    c.dry_run              = dry_run;
    return c;
}

// ─── OrderManager fixture ─────────────────────────────────────────────────────
// Bundles OrderManager with its dependencies and mock callbacks.
struct Fixture {
    OrbConfig     cfg;
    RiskManager   risk;
    LatencyLogger lat;
    OrderManager  om;

    // Captured order sends: each element is the basket_id
    std::vector<std::string> sent_baskets;
    // Captured cancel calls
    std::vector<std::string> cancelled_baskets;
    // Return value for the send callback (simulate send success/failure)
    bool send_ok = true;

    explicit Fixture(bool dry_run = false)
        : Fixture(make_cfg(dry_run)) {}

    explicit Fixture(OrbConfig custom_cfg)
        : cfg(std::move(custom_cfg))
        , risk(cfg, 50000.0)
        , lat()
        , om(cfg, risk, lat)
    {
        om.set_order_callback([this](
                const std::string& basket_id,
                const std::string& /*symbol*/,
                const std::string& /*exchange*/,
                int  /*qty*/,
                int  /*order_type*/,
                bool /*is_buy*/,
                double /*price*/,
                const std::string& /*user_tag*/) -> bool {
            sent_baskets.push_back(basket_id);
            return send_ok;
        });

        om.set_cancel_callback([this](const std::string& basket_id) {
            cancelled_baskets.push_back(basket_id);
        });

        om.set_modify_callback([](const std::string& /*basket_id*/,
                                   double /*new_trigger*/) -> bool {
            return true;
        });
    }
};

// ─── Helper: complete a full entry fill for a live (non-dry-run) fixture ──────
// After on_signal(), the entry order is in PENDING_ENTRY with a known basket_id.
// We simulate the exchange sending a fill back.
static void sim_entry_fill(Fixture& f, double fill_price) {
    auto snap = f.om.position_snapshot();
    f.om.on_fill_notification(snap.basket_id_entry, fill_price, snap.qty,
                               /*is_entry_fill=*/true);
}

// ─── Helper: complete an exit fill for a live fixture ─────────────────────────
static void sim_exit_fill(Fixture& f, double fill_price) {
    auto snap = f.om.position_snapshot();
    // Use basket_id_exit; if empty, the stop may have been the exit
    std::string basket = snap.basket_id_exit.empty()
                        ? snap.basket_id_stop
                        : snap.basket_id_exit;
    f.om.on_fill_notification(basket, fill_price, snap.qty,
                               /*is_entry_fill=*/false);
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════════════════════

// 1. Initial state is FLAT; is_flat() returns true
TEST(initial_state_is_flat) {
    Fixture f;
    ASSERT(f.om.is_flat());
    ASSERT_EQ(f.om.state(), PosState::FLAT);
}

// 2. on_signal(BUY) when FLAT queues an entry (triggers send callback in live mode)
TEST(buy_signal_when_flat_triggers_send) {
    Fixture f;   // dry_run=false

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");

    // The send callback must have been called exactly once for the entry order
    // (plus possibly a stop order — but stop is only submitted after the fill,
    //  so at PENDING_ENTRY stage only one send should have happened)
    ASSERT(f.sent_baskets.size() >= 1);
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
    ASSERT(!f.om.is_flat());
}

// 3. on_fill_notification() when entry pending transitions to LONG
TEST(entry_fill_transitions_to_long) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);

    sim_entry_fill(f, 19001.0);

    ASSERT_EQ(f.om.state(), PosState::LONG);
}

// 4. on_fill_notification() records entry price correctly
TEST(entry_fill_records_price) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19002.5);

    auto snap = f.om.position_snapshot();
    ASSERT_NEAR(snap.entry_price,       19002.5, 0.001);
    ASSERT_NEAR(snap.fill_price_actual, 19002.5, 0.001);
}

// 4b. SELL entry fill transitions to SHORT and records entry price
TEST(sell_entry_fill_transitions_to_short) {
    Fixture f;

    f.om.on_signal(OrbSignal::SELL, 18900.0, "orb_breakdown");
    sim_entry_fill(f, 18899.0);

    ASSERT_EQ(f.om.state(), PosState::SHORT);
    auto snap = f.om.position_snapshot();
    ASSERT_NEAR(snap.entry_price, 18899.0, 0.001);
}

// 5. check_trail_and_stop() moves stop when MFE exceeds trail_be_trigger
//    cfg: trail_be_trigger=5.0, trail_be_offset=1.0, trail_delay_secs=0
//    Entry at 19000. After MFE >= 5 pts, SL should move from 18990 to 19001.
TEST(trail_activates_and_moves_stop) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    auto snap_before = f.om.position_snapshot();
    ASSERT_NEAR(snap_before.sl_price, 18990.0, 0.001);   // sl_points=10

    // Price rises 5+ pts above entry (trail_be_trigger=5.0)
    f.om.check_trail_and_stop(19005.5);

    auto snap_after = f.om.position_snapshot();
    ASSERT(snap_after.trailing_active);
    // SL should have moved to at least entry+trail_be_offset (19001)
    ASSERT(snap_after.sl_price > snap_before.sl_price);
    ASSERT(snap_after.sl_price >= 19001.0 - 0.001);
}

// 6. flatten_now() from IN_TRADE (LONG) sends an exit order and moves to PENDING_EXIT
TEST(flatten_now_from_long_sends_exit) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    std::size_t sends_before = f.sent_baskets.size();
    f.om.flatten_now("kill_switch", 19010.0);

    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    // A new exit order should have been sent
    ASSERT(f.sent_baskets.size() > sends_before);
}

// 7. on_signal() when RiskManager is halted does nothing
TEST(signal_rejected_when_risk_halted) {
    Fixture f;

    // Force the risk manager to halt
    f.risk.on_trade_pnl(f.cfg.daily_loss_limit - 1.0);  // breach daily limit
    ASSERT(f.risk.halted());

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");

    // Still flat — no order sent
    ASSERT(f.om.is_flat());
    ASSERT(f.sent_baskets.empty());
}

// 7b. on_signal() is ignored when already in a position (not FLAT)
TEST(signal_ignored_when_not_flat) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    std::size_t sends_before = f.sent_baskets.size();
    f.om.on_signal(OrbSignal::SELL, 19000.0, "orb_signal_2");

    // No additional order should have been sent; state unchanged
    ASSERT_EQ(f.om.state(), PosState::LONG);
    ASSERT_EQ(f.sent_baskets.size(), sends_before);
}

// 8. on_order_rejected() — entry rejection reverts to FLAT
TEST(entry_rejection_reverts_to_flat) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);

    auto snap = f.om.position_snapshot();
    f.om.on_order_rejected(snap.basket_id_entry, "margin_insufficient");

    ASSERT(f.om.is_flat());
    ASSERT_EQ(f.om.state(), PosState::FLAT);
}

// 8b. on_order_rejected() — exit rejection retries up to 3 times (rejected_exit_count_ <= 3),
//     then on the 4th rejection entry_halted_ is set.
//     After halt, on_signal() is blocked even when position is not flat.
//     The entry_halted_ guard fires BEFORE the FLAT check in on_signal().
TEST(exit_rejection_halts_after_three_retries) {
    Fixture f;

    // Get into a LONG position
    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // Trigger the first exit attempt
    f.om.flatten_now("test_exit", 19005.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);

    // Reject 3 times → rejected_exit_count_ reaches 3; each time retries (count <= 3)
    // and lands back in PENDING_EXIT via initiate_exit_locked.
    for (int i = 0; i < 3; ++i) {
        auto snap = f.om.position_snapshot();
        f.om.on_order_rejected(snap.basket_id_exit, "exchange_reject");
        // Retried → back to PENDING_EXIT each time (since count 1,2,3 all <= 3)
        ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    }

    // 4th rejection: rejected_exit_count_ becomes 4 (> 3) → entry_halted_ = true.
    // State reverts to LONG (initiate_exit_locked is NOT called this time).
    {
        auto snap = f.om.position_snapshot();
        f.om.on_order_rejected(snap.basket_id_exit, "exchange_reject");
    }
    ASSERT_EQ(f.om.state(), PosState::LONG);  // NOT PENDING_EXIT — halt path skips retry

    // entry_halted_ is checked BEFORE the FLAT check in on_signal().
    // So even if we tried a signal now (still LONG), it would be blocked by entry_halted_,
    // not by the FLAT guard. Confirm by recording sends count before and after.
    std::size_t sends_before = f.sent_baskets.size();
    f.om.on_signal(OrbSignal::BUY, 19010.0, "new_signal_post_halt");
    // entry_halted_ fires first → no new order dispatched
    ASSERT_EQ(f.sent_baskets.size(), sends_before);
}

// 9. pop_trade_completed() returns true exactly once after a trade closes,
//    then returns false on the next call
TEST(pop_trade_completed_returns_true_once) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    f.om.flatten_now("test", 19010.0);
    sim_exit_fill(f, 19010.0);

    Position out;
    ASSERT(f.om.pop_trade_completed(out));   // first call: true
    ASSERT(!f.om.pop_trade_completed(out));  // second call: false (flag cleared)
}

// 10. Correct PnL calculation on exit fill (LONG)
//     entry=19000, exit=19010, point_value=2.0, MNQ commission=$0.50/side
//     pnl_points = 10.0
//     pnl_usd    = 10.0 * 2.0 - 2 * 0.50 = 20.0 - 1.0 = 19.0
TEST(pnl_calculation_long_correct) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    f.om.flatten_now("test", 19010.0);
    sim_exit_fill(f, 19010.0);

    Position out;
    ASSERT(f.om.pop_trade_completed(out));

    ASSERT_NEAR(out.pnl_points, 10.0, 0.001);
    ASSERT_NEAR(out.pnl_usd,    19.0, 0.001);   // 10 pts * $2 - $1 round-turn comm
}

// 10b. Correct PnL calculation on exit fill (SHORT — losing trade)
//      entry=19000, exit=19010 (SHORT at 19000, closed higher → loss)
//      pnl_points = 19000 - 19010 = -10.0
//      pnl_usd    = -10.0 * 2.0 - 1.0 (round-turn) = -21.0
TEST(pnl_calculation_short_loss_correct) {
    Fixture f;

    f.om.on_signal(OrbSignal::SELL, 19000.0, "orb_breakdown");
    sim_entry_fill(f, 19000.0);
    f.om.flatten_now("test", 19010.0);
    sim_exit_fill(f, 19010.0);

    Position out;
    ASSERT(f.om.pop_trade_completed(out));

    ASSERT_NEAR(out.pnl_points, -10.0, 0.001);
    ASSERT_NEAR(out.pnl_usd,    -21.0, 0.001);
}

// 11. dry_run mode: on_signal processes entry and immediately self-fills via
//     on_fill_notification_locked (no send callback needed), landing in LONG
TEST(dry_run_auto_fills_on_signal) {
    Fixture f(/*dry_run=*/true);   // no order_cb needed

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");

    // In dry_run the fill is immediate inside send_market_order
    ASSERT_EQ(f.om.state(), PosState::LONG);
    auto snap = f.om.position_snapshot();
    ASSERT_NEAR(snap.entry_price, 19000.0, 0.001);
    // No real network send in dry_run
    ASSERT(f.sent_baskets.empty());
}

// 12. flatten_now() when already FLAT is a no-op (no crash, state stays FLAT)
TEST(flatten_now_when_already_flat_is_noop) {
    Fixture f;
    ASSERT(f.om.is_flat());

    f.om.flatten_now("spurious_eod", 19000.0);

    ASSERT(f.om.is_flat());
    ASSERT(f.sent_baskets.empty());
}

// 13. flatten_now() when PENDING_ENTRY cancels the entry and reverts to FLAT
TEST(flatten_now_cancels_pending_entry) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);

    f.om.flatten_now("eod_flatten", 0.0);

    ASSERT(f.om.is_flat());
    // The cancel callback should have been called
    ASSERT(!f.cancelled_baskets.empty());
}

// 14. Software SL fallback: when no exchange stop is active (basket_id_stop empty),
//     check_trail_and_stop() exits immediately if price crosses the SL
TEST(software_sl_fires_when_no_exchange_stop) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    // Manually verify exchange stop was set (send count increased after fill)
    auto snap = f.om.position_snapshot();
    ASSERT(!snap.basket_id_stop.empty());  // exchange stop was submitted

    // Simulate stop rejection — clears basket_id_stop so software SL activates
    f.om.on_order_rejected(snap.basket_id_stop, "stop_order_rejected");
    auto snap2 = f.om.position_snapshot();
    ASSERT(snap2.basket_id_stop.empty());  // software SL now active

    // Price drops below SL (entry=19000, sl_points=10 → sl=18990)
    std::size_t sends_before = f.sent_baskets.size();
    f.om.check_trail_and_stop(18989.0);  // below SL

    // Should have transitioned to PENDING_EXIT and sent an exit order
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    ASSERT(f.sent_baskets.size() > sends_before);
}

// 15. MFE and MAE are tracked correctly by check_trail_and_stop
TEST(mfe_and_mae_tracked_correctly) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    // Price moves in favour then against
    f.om.check_trail_and_stop(19004.0);   // +4 pts above entry → mfe=4
    f.om.check_trail_and_stop(18997.0);   // -3 pts below entry → mae=3

    auto snap = f.om.position_snapshot();
    ASSERT_NEAR(snap.mfe, 4.0, 0.001);
    ASSERT_NEAR(snap.mae, 3.0, 0.001);
}

// 16. unknown_exit_cancels_stop: a fill arriving with an unrecognised basket_id
//     (neither entry nor stop) while LONG triggers exit_reason="unknown_exit",
//     causes the stop basket to be cancelled, and leaves the position FLAT.
TEST(unknown_exit_cancels_stop) {
    Fixture f;

    // Get into LONG with an active exchange stop
    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    auto snap = f.om.position_snapshot();
    ASSERT(!snap.basket_id_stop.empty());   // exchange stop was submitted after entry fill

    std::string stop_basket = snap.basket_id_stop;

    // Simulate a fill arriving with a completely unknown basket_id (not entry, not stop)
    // This represents an exchange fill for a basket we don't recognise.
    f.om.on_fill_notification("UNKNOWN-BASKET-999", 18995.0, 1, /*is_entry_fill=*/false);

    // exit_reason should be "unknown_exit" (basket didn't match basket_id_stop)
    Position out;
    ASSERT(f.om.pop_trade_completed(out));
    ASSERT_EQ(out.exit_reason, std::string("unknown_exit"));

    // The stop basket should have been cancelled (basket_id_stop != fill basket_id)
    bool stop_cancelled = false;
    for (const auto& b : f.cancelled_baskets) {
        if (b == stop_basket) { stop_cancelled = true; break; }
    }
    ASSERT(stop_cancelled);

    // Position must be FLAT after the unknown fill resolved
    ASSERT(f.om.is_flat());
}

// 17. all_exit_paths_cancel_stop: exchange_stop, stop_loss_timeout, and unknown_exit
//     all result in at least one cancel call for the stop basket.
TEST(all_exit_paths_cancel_stop) {
    // ── Path A: exchange_stop ──────────────────────────────────────────────────
    // The stop basket itself fills → cancel_stop_locked is NOT called (basket matches),
    // but the stop basket is no longer relevant. What matters for exchange_stop is that
    // when the stop fills, any OTHER pending stop is cancelled. Here we verify the
    // basic path: stop basket fills → position goes FLAT without extra cancel needed.
    // We confirm cancelled_baskets is empty (no extra cancel sent for a clean stop fill).
    {
        Fixture f;
        f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
        sim_entry_fill(f, 19000.0);
        ASSERT_EQ(f.om.state(), PosState::LONG);

        auto snap = f.om.position_snapshot();
        std::string stop_basket = snap.basket_id_stop;
        ASSERT(!stop_basket.empty());

        // Fill arrives for the stop basket itself → exchange_stop path
        f.om.on_fill_notification(stop_basket, 18990.0, 1, /*is_entry_fill=*/false);

        Position out;
        ASSERT(f.om.pop_trade_completed(out));
        ASSERT_EQ(out.exit_reason, std::string("exchange_stop"));
        ASSERT(f.om.is_flat());
        // No cancel should have been sent (the stop itself was the filler)
        ASSERT(f.cancelled_baskets.empty());
    }

    // ── Path B: stop_loss_timeout ──────────────────────────────────────────────
    // Exchange stop active but unresponsive → software SL timeout fires initiate_exit_locked,
    // which calls cancel_stop_locked → cancel_cb_ is triggered for the stop basket.
    {
        Fixture f;
        // Override kSLFireTimeoutMs by forcing the timeout path:
        // We can't change the constant, but we can advance time by calling
        // check_trail_and_stop twice. The first call starts the breach timer;
        // since kSLFireTimeoutMs=3000ms we cannot fast-forward real time in a unit test.
        // Instead we simulate the timeout path by rejecting the stop (software SL tier 1)
        // and verifying a cancel was issued for the stop basket before it was cleared.
        f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
        sim_entry_fill(f, 19000.0);
        ASSERT_EQ(f.om.state(), PosState::LONG);

        auto snap = f.om.position_snapshot();
        std::string stop_basket = snap.basket_id_stop;
        ASSERT(!stop_basket.empty());

        // Use flatten_now with reason "stop_loss_timeout" to exercise the cancel path
        // directly: initiate_exit_locked calls cancel_stop_locked before sending exit.
        f.om.flatten_now("stop_loss_timeout", 18989.0);

        // cancel_cb_ should have been called for the stop basket
        bool stop_cancelled = false;
        for (const auto& b : f.cancelled_baskets) {
            if (b == stop_basket) { stop_cancelled = true; break; }
        }
        ASSERT(stop_cancelled);
        ASSERT(!f.cancelled_baskets.empty());
    }

    // ── Path C: unknown_exit ───────────────────────────────────────────────────
    // Fill arrives with unrecognised basket_id → cancel_stop_locked fires for stop basket.
    {
        Fixture f;
        f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
        sim_entry_fill(f, 19000.0);
        ASSERT_EQ(f.om.state(), PosState::LONG);

        auto snap = f.om.position_snapshot();
        std::string stop_basket = snap.basket_id_stop;
        ASSERT(!stop_basket.empty());

        f.om.on_fill_notification("UNKNOWN-BASKET-42", 18995.0, 1, /*is_entry_fill=*/false);

        bool stop_cancelled = false;
        for (const auto& b : f.cancelled_baskets) {
            if (b == stop_basket) { stop_cancelled = true; break; }
        }
        ASSERT(stop_cancelled);
        ASSERT(!f.cancelled_baskets.empty());
    }
}

// 18. seed_cancelled_stops() populates the unconfirmed-cancel guard
TEST(seed_cancelled_stops_populates_guard) {
    Fixture f;

    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);

    f.om.seed_cancelled_stops("BASKET-RESTART-1", /*was_buy=*/true);
    f.om.seed_cancelled_stops("BASKET-RESTART-2", /*was_buy=*/false);

    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 2);
}

// 19. on_cancel_confirmed() removes the basket from the unconfirmed-cancel guard
//     and fires cancel_remove_cb_ exactly once for that basket
TEST(on_cancel_confirmed_removes_from_guard) {
    Fixture f;

    std::vector<std::string> removed;
    f.om.set_cancel_persist_callbacks(
        [](const std::string&, bool) {},          // persist: no-op
        [&removed](const std::string& id) { removed.push_back(id); }  // remove
    );

    f.om.seed_cancelled_stops("BASKET-A", true);
    f.om.seed_cancelled_stops("BASKET-B", false);
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 2);

    f.om.on_cancel_confirmed("BASKET-A");

    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);
    ASSERT_EQ(removed.size(), std::size_t(1));
    ASSERT_EQ(removed[0], std::string("BASKET-A"));

    // BASKET-B still in guard
    f.om.on_cancel_confirmed("BASKET-B");
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);
}

// 20. recancel_pending_stops() re-fires cancel_cb_ for every unconfirmed stop
TEST(recancel_pending_stops_resends_cancels) {
    Fixture f;

    f.om.seed_cancelled_stops("STOP-OLD-1", true);
    f.om.seed_cancelled_stops("STOP-OLD-2", false);

    f.cancelled_baskets.clear();  // reset capture from seed (seed doesn't call cancel_cb_)
    ASSERT(f.cancelled_baskets.empty());

    f.om.recancel_pending_stops();

    // cancel_cb_ must have been called once per pending stop
    ASSERT_EQ(f.cancelled_baskets.size(), std::size_t(2));
    bool found1 = false, found2 = false;
    for (const auto& b : f.cancelled_baskets) {
        if (b == "STOP-OLD-1") found1 = true;
        if (b == "STOP-OLD-2") found2 = true;
    }
    ASSERT(found1);
    ASSERT(found2);

    // Guard still intact after recancel (stops not yet confirmed)
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 2);
}

// 21. on_cancel_confirmed_by_server_basket clears guard for external cancellations
// RTrader cancellations arrive as cancel ACKs with empty user_tag; we resolve
// the client basket via the server_basket → client_basket reverse map.
TEST(on_cancel_confirmed_by_server_basket_resolves_external_cancel) {
    Fixture f;
    std::vector<std::string> removed;
    f.om.set_cancel_persist_callbacks(
        [](const std::string&, bool) {},
        [&removed](const std::string& id) { removed.push_back(id); }
    );

    // Get into LONG so a stop basket exists
    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    auto snap = f.om.position_snapshot();
    const std::string stop_basket = snap.basket_id_stop;
    ASSERT(!stop_basket.empty());

    // Register the exchange-assigned server basket_id (arrives via tid=351 NEW notification)
    f.om.set_stop_server_basket("SERVER-12345");

    // flatten_now triggers cancel_stop_locked which:
    //   - adds stop_basket to cancelled_stops_
    //   - saves server_to_client_cancelled_["SERVER-12345"] = stop_basket
    f.om.flatten_now("stop_loss_timeout", 18989.0);

    // Guard should now have exactly one pending cancelled stop
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 1);
    ASSERT(removed.empty());

    // Simulate external cancel ACK with empty user_tag — resolved via server basket
    f.om.on_cancel_confirmed_by_server_basket("SERVER-12345");

    // Guard should be cleared and cancel_remove_cb_ should have fired
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);
    ASSERT_EQ(removed.size(), std::size_t(1));
    ASSERT_EQ(removed[0], stop_basket);

    // Calling again with the same server basket is a no-op (already resolved)
    f.om.on_cancel_confirmed_by_server_basket("SERVER-12345");
    ASSERT_EQ(f.om.pending_cancelled_stop_count(), 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ghost-fill recovery tests
// Scenario: stale stop from a prior session fires after executor restarts.
// ─────────────────────────────────────────────────────────────────────────────

// 22. Unknown fill while FLAT sets ghost_halted_, blocking new signals.
TEST(ghost_fill_halts_entries) {
    Fixture f;
    ASSERT(f.om.is_flat());
    ASSERT(!f.om.is_entry_halted());

    // Exchange delivers fill for a basket we've never seen (stale stop from prior session)
    f.om.on_fill_notification("STALE-STOP-PRIOR-SESSION", 19000.0, 1, /*is_entry=*/false);

    ASSERT(f.om.is_flat());
    ASSERT(f.om.is_entry_halted());  // ghost_halted_ set

    // Signal must be rejected while halted
    size_t before = f.sent_baskets.size();
    f.om.on_signal(OrbSignal::BUY, 19100.0, "orb_breakout");
    ASSERT_EQ(f.sent_baskets.size(), before);
    ASSERT(f.om.is_flat());
}

// 23. confirm_exchange_flat() clears ghost_halted_ and re-enables entries.
//     Simulates the tid=451 snapshot arriving after operator closes via RTrader.
TEST(confirm_exchange_flat_clears_ghost_halt) {
    Fixture f;

    f.om.on_fill_notification("GHOST-BASKET", 19000.0, 1, /*is_entry=*/false);
    ASSERT(f.om.is_entry_halted());

    // tid=451 fires with net_qty=0 — exchange confirmed flat
    f.om.confirm_exchange_flat();
    ASSERT(!f.om.is_entry_halted());

    // Entries should now be unblocked
    f.om.on_signal(OrbSignal::BUY, 19100.0, "orb_breakout");
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
}

// 24. A second unknown fill while ghost_halted_ is MANUAL-CLOSE-DETECTED —
//     it must NOT escalate, must NOT send anything, and halt must stay active.
TEST(second_fill_while_ghost_halted_is_manual_not_ghost) {
    Fixture f;

    // First ghost fill
    f.om.on_fill_notification("STALE-STOP", 19000.0, 1, /*is_entry=*/false);
    ASSERT(f.om.is_entry_halted());

    size_t cancels_before = f.cancelled_baskets.size();
    size_t sends_before   = f.sent_baskets.size();

    // Second unknown fill (operator closed via RTrader) — should send nothing
    f.om.on_fill_notification("RTRADER-CLOSE", 19010.0, 1, /*is_entry=*/false);

    ASSERT_EQ(f.cancelled_baskets.size(), cancels_before);
    ASSERT_EQ(f.sent_baskets.size(), sends_before);
    ASSERT(f.om.is_entry_halted());  // still halted — tid=451 needed to clear
    ASSERT(f.om.is_flat());
}

// 25. register_unwind_basket(): fill for a registered basket is UNWIND-FILL,
//     not GHOST-FILL, and does not set ghost_halted_.
TEST(register_unwind_basket_fill_not_ghost) {
    Fixture f;
    ASSERT(!f.om.is_entry_halted());

    f.om.register_unwind_basket("STARTUP-RECON-UNWIND-1");

    size_t sends_before = f.sent_baskets.size();
    f.om.on_fill_notification("STARTUP-RECON-UNWIND-1", 19000.0, 1, /*is_entry=*/false);

    ASSERT(!f.om.is_entry_halted());          // no halt — recognised as tracked unwind
    ASSERT_EQ(f.sent_baskets.size(), sends_before);  // no extra order sent
    ASSERT(f.om.is_flat());
}

// 26. register_unwind_basket(): when ghost_halted_ is set, a registered-unwind
//     fill clears ghost_halted_ automatically.
TEST(register_unwind_basket_clears_ghost_halt) {
    Fixture f;

    // Ghost fill from a stale stop
    f.om.on_fill_notification("STALE-STOP-PRIOR", 19000.0, 1, /*is_entry=*/false);
    ASSERT(f.om.is_entry_halted());

    // Startup-recon sends a market unwind and registers it
    f.om.register_unwind_basket("MNQ-startup-recon-1234567890");

    // Unwind fills — halt should clear
    f.om.on_fill_notification("MNQ-startup-recon-1234567890", 19005.0, 1, /*is_entry=*/false);

    ASSERT(!f.om.is_entry_halted());
    ASSERT(f.om.is_flat());
}

// 27. Full stale-stop recovery flow:
//     stale stop fires → halt → register unwind → unwind fills → halt cleared → entries re-enabled.
TEST(stale_stop_full_recovery_flow) {
    Fixture f;

    // Prior-session stop fires while we think we're flat
    f.om.on_fill_notification("OLD-SESSION-STOP", 19000.0, 1, /*is_entry=*/false);
    ASSERT(f.om.is_entry_halted());

    // Signal rejected while halted
    size_t sends = f.sent_baskets.size();
    f.om.on_signal(OrbSignal::BUY, 19100.0, "orb_breakout");
    ASSERT_EQ(f.sent_baskets.size(), sends);

    // Startup-recon registers and dispatches unwind
    f.om.register_unwind_basket("RECON-UNWIND-XYZ");

    // Operator also closes via RTrader — second unknown fill while halted
    f.om.on_fill_notification("RTRADER-MKT-CLOSE", 19002.0, 1, /*is_entry=*/false);
    ASSERT(f.om.is_entry_halted());  // still halted (unwind not yet filled)

    // Unwind fills — halt cleared
    f.om.on_fill_notification("RECON-UNWIND-XYZ", 19003.0, 1, /*is_entry=*/false);
    ASSERT(!f.om.is_entry_halted());

    // New entries are now allowed
    f.om.on_signal(OrbSignal::BUY, 19200.0, "orb_breakout");
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
}

// 28. confirm_exchange_flat() must NOT clear entry_halted_ (exit-rejection halt).
//     The two halts are independent — ghost flat-confirm only clears ghost halt.
TEST(exit_rejection_halt_not_cleared_by_exchange_flat) {
    Fixture f;

    // Get into LONG and trigger entry_halted_ via 4 market-exit rejections
    // (mirrors exit_rejection_halts_after_three_retries pattern)
    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    f.om.flatten_now("test_exit", 19005.0);
    for (int i = 0; i < 3; ++i) {
        auto snap = f.om.position_snapshot();
        f.om.on_order_rejected(snap.basket_id_exit, "test_reject");
    }
    {
        auto snap = f.om.position_snapshot();
        f.om.on_order_rejected(snap.basket_id_exit, "test_reject");
    }
    ASSERT(f.om.is_entry_halted());

    // tid=451 flat confirm must NOT clear the exit-rejection halt
    f.om.confirm_exchange_flat();
    ASSERT(f.om.is_entry_halted());  // still halted — only successful exit fill clears it

    // Signal still blocked
    size_t sends_before = f.sent_baskets.size();
    f.om.on_signal(OrbSignal::BUY, 19100.0, "orb_breakout");
    ASSERT_EQ(f.sent_baskets.size(), sends_before);
}

// 29. Tier-2 software SL fires on the second below-SL tick when sl_fire_timeout_ms=0.
//     First tick starts the breach timer; second tick (breach_ms >= 0) triggers the exit.
TEST(sl_tier2_fires_after_breach_timeout) {
    OrbConfig cfg = make_cfg(false);
    cfg.sl_fire_timeout_ms = 0;  // fires as soon as breach_ms >= 0 (next tick)
    Fixture f(cfg);

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    // Verify exchange stop is active (basket_id_stop non-empty)
    ASSERT(!f.om.position_snapshot().basket_id_stop.empty());

    // First tick below SL — starts breach timer, does NOT fire yet
    f.om.check_trail_and_stop(18989.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // Second tick below SL — breach_ms >= 0 → software SL fires
    std::size_t sends_before = f.sent_baskets.size();
    f.om.check_trail_and_stop(18989.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    ASSERT(f.sent_baskets.size() > sends_before);
}

// 30. Tier-2 breach timer resets when price recovers above SL.
//     After recovery the "first tick below SL" logic restarts from scratch.
TEST(sl_tier2_resets_on_price_recovery) {
    OrbConfig cfg = make_cfg(false);
    cfg.sl_fire_timeout_ms = 0;
    Fixture f(cfg);

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    f.om.check_trail_and_stop(18989.0);  // start timer
    ASSERT_EQ(f.om.state(), PosState::LONG);

    f.om.check_trail_and_stop(18995.0);  // price recovered — timer cleared
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // One more tick below SL — timer set again (first call after reset), no fire
    f.om.check_trail_and_stop(18989.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);  // timer just restarted, not yet fired
}

// 31. BE trigger fires cancel+resubmit of the exchange stop.
//     Entry at 19000; trail_be_trigger=5, trail_be_offset=1, trail_step=3, trail_delay_secs=0.
//     At price=19005.5: MFE=5.5 >= trigger → BE fires, SL moves up, trail also moves SL.
TEST(trail_be_triggers_cancel_resubmit) {
    Fixture f;  // dry_run=false; trail_delay_secs=0

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    // After fill: entry + stop submitted → sent_baskets.size() == 2
    ASSERT_EQ(f.sent_baskets.size(), (std::size_t)2);
    ASSERT(f.cancelled_baskets.empty());

    f.om.check_trail_and_stop(19005.5);  // MFE=5.5 → BE triggers, trail activates
    // Old stop cancelled, new stop submitted
    ASSERT(!f.cancelled_baskets.empty());  // cancel was sent
    ASSERT(f.sent_baskets.size() > 2);    // new stop submitted
}

// 32. Trail suppression: small SL move (< trail_step) does NOT trigger cancel+resubmit.
TEST(trail_suppression_prevents_rapid_resubmit) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    f.om.check_trail_and_stop(19005.5);  // BE triggers; trail: SL → 19002.5
    std::size_t cancels_after_first = f.cancelled_baskets.size();
    std::size_t sends_after_first   = f.sent_baskets.size();

    // Small price move: trail_sl = 19006.0 - 3.0 = 19003.0
    // |19003.0 - last_exchange_sl| < trail_step=3 → suppressed
    f.om.check_trail_and_stop(19006.0);
    ASSERT_EQ(f.cancelled_baskets.size(), cancels_after_first);  // no new cancel
    ASSERT_EQ(f.sent_baskets.size(),      sends_after_first);    // no new send
}

// 33. BE fires with retraced price (trail_be_offset > trail_be_trigger):
//     be_sl=entry+2 is above current_price=entry+0.6 when BE triggers.
//     Correct behaviour: on tick 1, stop is cancel+resubmitted at be_sl (no immediate
//     market exit at current_price). On tick 2, stop_resubmit_pending_ fires the
//     immediate software SL because the new stop is in-flight and price is below it.
TEST(be_retraced_price_uses_breach_not_market_exit) {
    OrbConfig cfg = make_cfg(false);
    cfg.trail_be_trigger   = 0.5;
    cfg.trail_be_offset    = 2.0;  // be_sl = entry+2 > trigger price entry+0.5
    cfg.trail_delay_secs   = 300;
    cfg.sl_fire_timeout_ms = 3000;  // timeout irrelevant — resubmit path fires first
    Fixture f(cfg);

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    // Tick 1: BE fires (mfe=0.6 >= 0.5); be_sl=19002 > current 19000.6 → stop
    // cancel+resubmitted. No immediate market exit on THIS tick — sl_breached was
    // computed against the OLD sl (18990) before BE updated it.
    std::size_t sends_after_entry = f.sent_baskets.size();
    f.om.check_trail_and_stop(19000.6);
    ASSERT_EQ(f.om.state(), PosState::LONG);
    ASSERT(f.sent_baskets.size() > sends_after_entry);  // stop cancel+resubmit to be_sl

    // Tick 2: price still below be_sl=19002; stop_resubmit_pending_=true (stop in-flight)
    // → immediate software SL fires rather than risk a delayed exchange fill.
    std::size_t sends_before = f.sent_baskets.size();
    f.om.check_trail_and_stop(19000.6);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    ASSERT(f.sent_baskets.size() > sends_before);
}

// 34. flatten_now() when PENDING_EXIT logs and returns — no duplicate exit order sent.
TEST(eod_flatten_during_pending_exit_is_noop) {
    Fixture f;

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);
    f.om.flatten_now("eod", 19010.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);

    std::size_t sends_before = f.sent_baskets.size();
    f.om.flatten_now("eod_second_call", 19010.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);  // state unchanged
    ASSERT_EQ(f.sent_baskets.size(), sends_before);    // no second exit order
}

// 35. BE fires at entry+trigger, but trail_be_offset > trail_be_trigger so be_sl is
//     ABOVE current price. On tick 1 the stop is cancel+resubmitted at be_sl; state
//     stays LONG (sl_breached was computed against the OLD sl before BE updated it).
//     On tick 2, stop_resubmit_pending_=true fires the immediate software SL because
//     the new stop is in-flight and price is already below it.
TEST(be_trigger_below_be_sl_no_immediate_exit) {
    OrbConfig cfg = make_cfg(false);
    cfg.trail_be_trigger   = 3.0;
    cfg.trail_be_offset    = 5.0;  // be_sl = entry+5 — above trigger price of entry+3
    cfg.sl_fire_timeout_ms = 3000; // timeout irrelevant — resubmit path fires first
    Fixture f(cfg);

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    // Tick 1: BE fires (mfe=3.0), be_sl=19005 > current_price=19003.
    // sl_breached was evaluated against OLD sl=18990 → false → state stays LONG.
    // Stop cancel+resubmit happens; stop_resubmit_pending_=true.
    f.om.check_trail_and_stop(19003.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // Tick 2: price still below be_sl=19005; stop_resubmit_pending_=true (stop in-flight)
    // → immediate software SL fires — no 3000ms wait needed.
    std::size_t sends_before = f.sent_baskets.size();
    f.om.check_trail_and_stop(19003.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    ASSERT(f.sent_baskets.size() > sends_before);
}

// 36. Stop-resubmit race: BE fires (cancel+resubmit in progress) and price immediately
//     breaches the new stop. Software SL fires at once — no 3000ms wait — preventing
//     a delayed exchange fill at a far worse price.
TEST(stop_resubmit_race_fires_immediate_software_sl) {
    OrbConfig cfg = make_cfg(false);
    cfg.trail_be_trigger   = 3.0;
    cfg.trail_be_offset    = 1.0;
    cfg.sl_fire_timeout_ms = 3000;  // normal timeout — should NOT be needed
    Fixture f(cfg);

    f.om.on_signal(OrbSignal::BUY, 19000.0, "orb_breakout");
    sim_entry_fill(f, 19000.0);

    // BE fires at entry+3: stop cancel+resubmit sent, stop_resubmit_pending_=true
    f.om.check_trail_and_stop(19003.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // Price immediately drops below new be_sl=19001 (resubmit window still open).
    // Must fire software SL at once, NOT wait 3000ms.
    std::size_t sends_before = f.sent_baskets.size();
    f.om.check_trail_and_stop(19000.5);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);   // immediate exit
    ASSERT(f.sent_baskets.size() > sends_before);       // market order sent
}

// ═══════════════════════════════════════════════════════════════════════════════
int main() {
    RUN(initial_state_is_flat);
    RUN(buy_signal_when_flat_triggers_send);
    RUN(entry_fill_transitions_to_long);
    RUN(entry_fill_records_price);
    RUN(sell_entry_fill_transitions_to_short);
    RUN(trail_activates_and_moves_stop);
    RUN(flatten_now_from_long_sends_exit);
    RUN(signal_rejected_when_risk_halted);
    RUN(signal_ignored_when_not_flat);
    RUN(entry_rejection_reverts_to_flat);
    RUN(exit_rejection_halts_after_three_retries);
    RUN(pop_trade_completed_returns_true_once);
    RUN(pnl_calculation_long_correct);
    RUN(pnl_calculation_short_loss_correct);
    RUN(dry_run_auto_fills_on_signal);
    RUN(flatten_now_when_already_flat_is_noop);
    RUN(flatten_now_cancels_pending_entry);
    RUN(software_sl_fires_when_no_exchange_stop);
    RUN(mfe_and_mae_tracked_correctly);
    RUN(unknown_exit_cancels_stop);
    RUN(all_exit_paths_cancel_stop);
    RUN(seed_cancelled_stops_populates_guard);
    RUN(on_cancel_confirmed_removes_from_guard);
    RUN(recancel_pending_stops_resends_cancels);
    RUN(on_cancel_confirmed_by_server_basket_resolves_external_cancel);
    RUN(ghost_fill_halts_entries);
    RUN(confirm_exchange_flat_clears_ghost_halt);
    RUN(second_fill_while_ghost_halted_is_manual_not_ghost);
    RUN(register_unwind_basket_fill_not_ghost);
    RUN(register_unwind_basket_clears_ghost_halt);
    RUN(stale_stop_full_recovery_flow);
    RUN(exit_rejection_halt_not_cleared_by_exchange_flat);
    RUN(sl_tier2_fires_after_breach_timeout);
    RUN(sl_tier2_resets_on_price_recovery);
    RUN(trail_be_triggers_cancel_resubmit);
    RUN(trail_suppression_prevents_rapid_resubmit);
    RUN(be_retraced_price_uses_breach_not_market_exit);
    RUN(eod_flatten_during_pending_exit_is_noop);
    RUN(be_trigger_below_be_sl_no_immediate_exit);
    RUN(stop_resubmit_race_fires_immediate_software_sl);

    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
