/*  ═══════════════════════════════════════════════════════════════════════════
    test_lifecycle.cpp — Integration: OrbStrategy + OrderManager, multi-cycle

    Simulates the full executor loop N times:
      begin_cycle → ORB seeded → breakout signal → entry fill →
      trail/SL management → exit fill → FLAT → notify strategy → repeat

    No DB, no Rithmic, no network. All exchange I/O is mocked.

    Build (from repo root):
        g++ -std=c++20 -I src/execution -I src/ \
            tests/execution/test_lifecycle.cpp \
            src/execution/order_manager.cpp \
            src/execution/risk_manager.cpp \
            -o build/test_lifecycle && ./build/test_lifecycle
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <cmath>
#include <string>
#include <vector>
#include <functional>

#include "../../src/execution/orb_strategy.hpp"
#include "../../src/execution/order_manager.hpp"

// ─── Minimal test harness ─────────────────────────────────────────────────────
static int tests_run = 0, tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; \
        std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("Assert failed: " #cond); } while(0)
#define ASSERT_EQ(a,b) do { if ((a)!=(b)) throw std::runtime_error("ASSERT_EQ: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a,b,eps) do { if (std::abs((a)-(b))>(eps)) throw std::runtime_error("ASSERT_NEAR: " #a " vs " #b); } while(0)

// ─── Timestamp helper (2025-04-30 EDT = UTC-4) ───────────────────────────────
static constexpr int64_t kDate_UTC = 1746057600LL;
static int64_t et_us(int h, int m, int s = 0) {
    return (kDate_UTC + (int64_t)h*3600 + (int64_t)m*60 + s + 4*3600LL) * 1'000'000LL;
}
static OrbTick tick(int h, int m, int s, double price) {
    OrbTick t; t.ts_micros = et_us(h,m,s); t.price = price; t.size = 1; t.is_buy = true;
    return t;
}

// ─── Shared config ────────────────────────────────────────────────────────────
static OrbConfig make_cfg() {
    OrbConfig c;
    c.symbol             = "MNQ";
    c.exchange           = "CME";
    c.qty                = 1;
    c.point_value        = 2.0;
    c.sl_points          = 10.0;
    c.trail_be_trigger   = 5.0;
    c.trail_step         = 3.0;
    c.trail_be_offset    = 1.0;
    c.trail_delay_secs   = 0;       // immediate trail activation in tests
    c.sl_fire_timeout_ms = 3000;
    c.orb_minutes        = 5;
    c.session_open_hour  = 9;
    c.session_open_min   = 30;
    c.breakout_buffer    = 0.0;
    c.max_daily_trades   = 20;
    c.last_entry_hour    = 23;      // cycle mode: no RTH gate
    c.news_blackout_min  = 0;       // disabled for deterministic tests
    c.eod_flatten_hour   = 23;
    c.eod_flatten_min    = 55;
    c.stop_cooldown_secs = 0;
    c.daily_loss_limit   = -50000.0;
    c.trailing_drawdown_cap = 99999.0;
    c.consistency_cap_pct   = 0.99;
    c.dry_run            = false;
    return c;
}

// ─── Integration fixture — OrbStrategy wired to OrderManager ─────────────────
struct Fixture {
    OrbConfig     cfg;
    RiskManager   risk;
    LatencyLogger lat;
    OrbStrategy   strategy;
    OrderManager  om;

    std::vector<std::string> sent_baskets;
    std::vector<std::string> cancelled_baskets;

    explicit Fixture(OrbConfig c = make_cfg())
        : cfg(c), risk(cfg, 100000.0), lat(), strategy(cfg), om(cfg, risk, lat)
    {
        strategy.set_signal_callback([this](OrbSignal sig, double px,
                                            const std::string& reason) {
            if (sig == OrbSignal::BUY || sig == OrbSignal::SELL)
                om.on_signal(sig, px, reason);
        });
        om.set_order_callback([this](const std::string& bid,
                                     const std::string&, const std::string&,
                                     int, int, bool, double,
                                     const std::string&) -> bool {
            sent_baskets.push_back(bid);
            return true;
        });
        om.set_cancel_callback([this](const std::string& bid) {
            cancelled_baskets.push_back(bid);
        });
    }

    // Reset strategy for the next cycle; OM + RiskManager state persists.
    void begin_cycle() { strategy.reset_session(); }

    // Seed ORB range and consume the seeded_ guard with an inside-range price.
    void seed(double orb_h, double orb_l, double anchor, int eh, int em) {
        strategy.seed_orb_range(orb_h, orb_l);
        strategy.on_tick(tick(eh, em, 0, anchor));
    }

    // Advance price: may emit a signal that flows into OrderManager.on_signal().
    void price(double px, int eh, int em, int es = 1) {
        strategy.on_tick(tick(eh, em, es, px));
    }

    void entry_fill(double px) {
        auto s = om.position_snapshot();
        om.on_fill_notification(s.basket_id_entry, px, s.qty, true);
    }

    void exit_fill(double px) {
        auto s = om.position_snapshot();
        std::string bid = s.basket_id_exit.empty() ? s.basket_id_stop : s.basket_id_exit;
        om.on_fill_notification(bid, px, s.qty, false);
    }

    // Retrieve completed trade and notify strategy, returning the Position record.
    Position close_cycle(OrbSignal dir, const std::string& exit_reason = "") {
        Position out;
        ASSERT(om.pop_trade_completed(out));
        ASSERT(om.is_flat());
        strategy.notify_trade_filled(dir, exit_reason);
        ASSERT(!strategy.session().in_position);
        return out;
    }
};

// ═══════════════════════════════════════════════════════════════════════════════

// 1. Five complete 1-minute cycles, each with a different exit path.
//    Verifies full state machine round-trip and no cross-cycle state leakage.
TEST(five_1min_cycles_different_exit_paths) {
    Fixture f;

    // ─────────────────────────────────────────────────────────────────────────
    // Cycle 1  LONG → BE fires → exchange stop fills at BE level → +1pt
    //   entry=19105, sl=19095. BE at 19110.5: exchange stop→19106, in-mem→19107.5.
    //   Exchange stop at 19106 fills.
    // ─────────────────────────────────────────────────────────────────────────
    f.begin_cycle();
    ASSERT(!f.strategy.orb_set());  ASSERT(f.om.is_flat());

    f.seed(19100.0, 18950.0, 19025.0, 10, 5);
    ASSERT(f.strategy.orb_set());

    f.price(19105.0, 10, 6);                        // BUY signal → PENDING_ENTRY
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
    ASSERT(f.strategy.session().in_position);

    f.entry_fill(19105.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 19095.0, 0.001);

    f.om.check_trail_and_stop(19110.5);             // BE fires; in-mem SL=19107.5
    {
        auto s = f.om.position_snapshot();
        ASSERT(s.be_triggered);
        ASSERT(s.trailing_active);
        ASSERT_NEAR(s.sl_price, 19107.5, 0.001);   // trail ran to 19110.5-3=19107.5
        ASSERT(!s.basket_id_stop.empty());          // exchange stop at 19106 still active
    }

    f.exit_fill(19106.0);                           // exchange stop at BE level fills
    auto p1 = f.close_cycle(OrbSignal::BUY, "exchange_stop");
    ASSERT_EQ(p1.exit_reason, std::string("exchange_stop"));
    ASSERT_NEAR(p1.pnl_points,  1.0, 0.001);       // 19106 - 19105
    ASSERT_NEAR(p1.pnl_usd,     1.0, 0.001);       // 1pt * $2 - $1 rt-comm = $1
    ASSERT_EQ(f.strategy.session().trades_today, 1);

    // ─────────────────────────────────────────────────────────────────────────
    // Cycle 2  SHORT → initial exchange stop fires (SL hit) → -10pt
    //   entry=18940, sl=18950. Price rallies back to 18950 → stop fills.
    // ─────────────────────────────────────────────────────────────────────────
    f.begin_cycle();
    ASSERT(f.om.is_flat());  ASSERT(!f.strategy.orb_set());

    f.seed(19100.0, 18950.0, 19025.0, 10, 10);
    f.price(18940.0, 10, 11);                       // SELL signal → PENDING_ENTRY
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);

    f.entry_fill(18940.0);
    ASSERT_EQ(f.om.state(), PosState::SHORT);
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 18950.0, 0.001);

    f.exit_fill(18950.0);                           // initial stop fires (loss)
    auto p2 = f.close_cycle(OrbSignal::SELL, "exchange_stop");
    ASSERT_EQ(p2.exit_reason, std::string("exchange_stop"));
    ASSERT_NEAR(p2.pnl_points, -10.0, 0.001);      // 18940 - 18950
    ASSERT_NEAR(p2.pnl_usd,   -21.0, 0.001);       // -10pt * $2 - $1 = -$21
    ASSERT_EQ(f.strategy.session().trades_today, 1); // reset each cycle

    // ─────────────────────────────────────────────────────────────────────────
    // Cycle 3  LONG → trail step fires → exchange stop at trailed SL → +7pt
    //   entry=19200. BE at 19205.5 → exchange stop at 19201, in-mem 19202.5.
    //   Trail step at 19210 → |19207-19201|=6 ≥ 3 → exchange stop resubmitted at 19207.
    //   Exchange stop at 19207 fills.
    // ─────────────────────────────────────────────────────────────────────────
    f.begin_cycle();
    ASSERT(f.om.is_flat());

    f.seed(19150.0, 19000.0, 19075.0, 10, 15);
    f.price(19205.0, 10, 16);                       // BUY signal
    f.entry_fill(19200.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    f.om.check_trail_and_stop(19205.5);             // BE: exchange stop at 19201, in-mem 19202.5
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 19202.5, 0.001);

    f.om.check_trail_and_stop(19210.0);             // trail_sl=19207; |6| ≥ 3 → exchange resubmit
    {
        auto s = f.om.position_snapshot();
        ASSERT_NEAR(s.sl_price, 19207.0, 0.001);
        ASSERT(s.trailing_active);
        ASSERT(!s.basket_id_stop.empty());          // new stop at 19207
    }

    f.exit_fill(19207.0);                           // trail stop fills
    auto p3 = f.close_cycle(OrbSignal::BUY, "exchange_stop");
    ASSERT_EQ(p3.exit_reason, std::string("exchange_stop"));
    ASSERT_NEAR(p3.pnl_points,  7.0, 0.001);       // 19207 - 19200
    ASSERT_NEAR(p3.pnl_usd,    13.0, 0.001);       // 7pt * $2 - $1 = $13

    // ─────────────────────────────────────────────────────────────────────────
    // Cycle 4  SHORT → BE fires → flatten_now (EOD) → +3pt
    //   entry=19000. BE at 18994.5 → exchange stop at 18999, in-mem at 18997.5.
    //   flatten_now("eod_flatten") at 18997 → +3pt.
    // ─────────────────────────────────────────────────────────────────────────
    f.begin_cycle();
    ASSERT(f.om.is_flat());

    f.seed(19050.0, 18900.0, 18975.0, 10, 20);
    f.price(18890.0, 10, 21);                       // SELL signal
    f.entry_fill(19000.0);
    ASSERT_EQ(f.om.state(), PosState::SHORT);

    f.om.check_trail_and_stop(18994.5);             // BE fires
    {
        auto s = f.om.position_snapshot();
        ASSERT(s.be_triggered);
        ASSERT_NEAR(s.sl_price, 18997.5, 0.001);   // trail ran: 18994.5+3=18997.5
    }

    f.om.flatten_now("eod_flatten", 18997.0);
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    f.exit_fill(18997.0);
    auto p4 = f.close_cycle(OrbSignal::SELL, "eod_flatten");
    ASSERT_EQ(p4.exit_reason, std::string("eod_flatten"));
    ASSERT_NEAR(p4.pnl_points,  3.0, 0.001);       // 19000 - 18997
    ASSERT_NEAR(p4.pnl_usd,     5.0, 0.001);       // 3pt * $2 - $1 = $5

    // ─────────────────────────────────────────────────────────────────────────
    // Cycle 5  LONG → stop rejected → software SL (tier-1) → -11pt
    //   entry=19300, sl=19290. Stop order rejected → basket_id_stop cleared.
    //   Price 19289 < 19290 → tier-1 software SL fires immediately.
    // ─────────────────────────────────────────────────────────────────────────
    f.begin_cycle();
    ASSERT(f.om.is_flat());

    f.seed(19250.0, 19100.0, 19175.0, 10, 25);
    f.price(19260.0, 10, 26);                       // BUY signal
    f.entry_fill(19300.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    {
        auto s = f.om.position_snapshot();
        ASSERT(!s.basket_id_stop.empty());
        f.om.on_order_rejected(s.basket_id_stop, "sim_reject"); // reject stop
        ASSERT(f.om.position_snapshot().basket_id_stop.empty()); // software SL now active
    }

    f.om.check_trail_and_stop(19289.0);             // below SL=19290; no exchange stop → tier-1
    ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);
    f.exit_fill(19289.0);
    auto p5 = f.close_cycle(OrbSignal::BUY, "stop_loss");
    ASSERT_EQ(p5.exit_reason, std::string("stop_loss"));
    ASSERT_NEAR(p5.pnl_points, -11.0, 0.001);      // 19289 - 19300
    ASSERT_NEAR(p5.pnl_usd,   -23.0, 0.001);       // -11pt * $2 - $1 = -$23

    // ─────────────────────────────────────────────────────────────────────────
    // Final assertions: clean terminal state after all 5 cycles
    // ─────────────────────────────────────────────────────────────────────────
    ASSERT(f.om.is_flat());
    ASSERT(!f.strategy.session().in_position);
    // net daily PnL = $1 - $21 + $13 + $5 - $23 = -$25
    ASSERT_NEAR(f.risk.daily_pnl(), -25.0, 0.5);
}

// 2. Three consecutive cycles using the same ORB range each time.
//    Checks that begin_cycle() fully resets strategy state between cycles
//    (orb_set, in_position, trades_today) without affecting OM or RiskManager.
TEST(three_cycles_state_reset_verification) {
    Fixture f;

    const double ORB_H = 19100.0, ORB_L = 18950.0, ANCHOR = 19025.0;
    const double ENTRY = 19105.0, EXIT = 19115.0;
    const double EXPECTED_PNL_PTS = 10.0;  // 19115 - 19105

    for (int cycle = 0; cycle < 3; ++cycle) {
        f.begin_cycle();

        // Strategy fully reset
        ASSERT(!f.strategy.orb_set());
        ASSERT(!f.strategy.session().in_position);
        ASSERT_EQ(f.strategy.session().trades_today, 0);
        ASSERT(f.om.is_flat());

        f.seed(ORB_H, ORB_L, ANCHOR, 10, 5 + cycle * 5);
        ASSERT(f.strategy.orb_set());

        f.price(ENTRY, 10, 6 + cycle * 5);         // BUY signal
        ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
        ASSERT(f.strategy.session().in_position);

        f.entry_fill(ENTRY);
        ASSERT_EQ(f.om.state(), PosState::LONG);

        f.om.flatten_now("test_eod", EXIT);
        ASSERT_EQ(f.om.state(), PosState::PENDING_EXIT);

        f.exit_fill(EXIT);
        ASSERT(f.om.is_flat());

        Position out = f.close_cycle(OrbSignal::BUY, "test_eod");
        ASSERT_NEAR(out.pnl_points, EXPECTED_PNL_PTS, 0.001);
        ASSERT_EQ(f.strategy.session().trades_today, 1);
    }

    // Risk manager accumulated 3 winning trades
    ASSERT(f.risk.daily_pnl() > 0.0);
}

// 3. Mixed LONG/SHORT within a cycle using notify_trade_filled to re-enable entry.
//    Simulates a rare scenario: LONG stopped out, then re-enters SHORT same session.
TEST(long_then_short_same_cycle) {
    Fixture f;
    f.begin_cycle();

    f.seed(19100.0, 18950.0, 19025.0, 10, 5);

    // First trade: BUY breakout, stopped out at initial SL
    f.price(19105.0, 10, 6);
    f.entry_fill(19105.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    f.exit_fill(19095.0);                           // initial stop fills: -10pt
    Position p1 = f.close_cycle(OrbSignal::BUY, "exchange_stop");
    ASSERT_NEAR(p1.pnl_points, -10.0, 0.001);
    ASSERT_EQ(f.strategy.session().trades_today, 1);

    // Second trade: SELL breakout (same session, same ORB — re-enabled by notify_trade_filled)
    f.price(18940.0, 10, 10);                       // SELL signal
    ASSERT_EQ(f.om.state(), PosState::PENDING_ENTRY);
    ASSERT(f.strategy.session().in_position);

    f.entry_fill(18940.0);
    ASSERT_EQ(f.om.state(), PosState::SHORT);

    f.om.flatten_now("test_exit", 18930.0);
    f.exit_fill(18930.0);
    Position p2 = f.close_cycle(OrbSignal::SELL, "test_exit");
    ASSERT_NEAR(p2.pnl_points, 10.0, 0.001);       // 18940 - 18930

    ASSERT_EQ(f.strategy.session().trades_today, 2);
    ASSERT(f.om.is_flat());
}

// 4. Multiple trail ratchets within one cycle — verifies stop basket updates
//    cleanly across 3 trail steps and the final stop fills correctly.
TEST(three_trail_steps_then_stop_fills) {
    Fixture f;
    f.begin_cycle();

    f.seed(19100.0, 18950.0, 19025.0, 10, 5);
    f.price(19105.0, 10, 6);
    f.entry_fill(19200.0);
    ASSERT_EQ(f.om.state(), PosState::LONG);

    // Step 1: BE fires at 19205.5 — exchange stop at 19201, in-mem at 19202.5
    f.om.check_trail_and_stop(19205.5);
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 19202.5, 0.001);

    // Step 2: trail fires at 19210 — exchange stop resubmitted at 19207
    f.om.check_trail_and_stop(19210.0);
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 19207.0, 0.001);

    // Step 3: trail fires at 19215 — exchange stop resubmitted at 19212
    //   |19212 - 19207| = 5 >= step=3 → fires
    bool sl_moved = f.om.check_trail_and_stop(19215.0);
    ASSERT(sl_moved);
    ASSERT_NEAR(f.om.position_snapshot().sl_price, 19212.0, 0.001);
    ASSERT(!f.om.position_snapshot().basket_id_stop.empty());

    // Final stop fills at 19212
    f.exit_fill(19212.0);
    Position out = f.close_cycle(OrbSignal::BUY, "exchange_stop");
    ASSERT_EQ(out.exit_reason, std::string("exchange_stop"));
    ASSERT_NEAR(out.pnl_points, 12.0, 0.001);      // 19212 - 19200
    ASSERT_NEAR(out.pnl_usd,   23.0, 0.001);       // 12pt * $2 - $1 = $23
}

// ═══════════════════════════════════════════════════════════════════════════════
int main() {
    RUN(five_1min_cycles_different_exit_paths);
    RUN(three_cycles_state_reset_verification);
    RUN(long_then_short_same_cycle);
    RUN(three_trail_steps_then_stop_fills);

    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
