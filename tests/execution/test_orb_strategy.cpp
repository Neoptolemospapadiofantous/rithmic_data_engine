/*  ═══════════════════════════════════════════════════════════════════════════
    test_orb_strategy.cpp — Unit tests for OrbStrategy (C3 audit finding)

    OrbStrategy is a pure signal generator (no DB, no sockets, no I/O beyond
    log.hpp which only writes to stdout/file). All tests run entirely in-process.

    Standalone build (from repo root):
        g++ -std=c++20 -Isrc/execution -Isrc \
            tests/execution/test_orb_strategy.cpp \
            -o build/test_orb_strategy && ./build/test_orb_strategy

    CMake target suggestion (add to CMakeLists.txt):
        add_executable(test_orb_strategy tests/execution/test_orb_strategy.cpp)
        target_include_directories(test_orb_strategy PRIVATE src/execution src/)
        target_compile_features(test_orb_strategy PRIVATE cxx_std_20)
        add_test(NAME orb_strategy_tests COMMAND test_orb_strategy)

    Time reference (EDT, UTC-4):
        09:30 ET = 13:30 UTC
        09:45 ET = 13:45 UTC
        08:30 ET = 12:30 UTC
        10:00 ET = 14:00 UTC
    All timestamps below are for 2025-04-30 (Wednesday, EDT=-4h).
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include "../../src/execution/orb_strategy.hpp"
#include "../../src/execution/orb_config.hpp"

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

// ─── Helpers ──────────────────────────────────────────────────────────────────

// Build a default OrbConfig suitable for unit tests.
// breakout_buffer=0 so price just needs to clear orb_high/low strictly.
static OrbConfig make_cfg() {
    OrbConfig c;
    c.orb_minutes       = 15;       // 9:30–9:45 ET
    c.session_open_hour = 9;
    c.session_open_min  = 30;
    c.breakout_buffer   = 0.0;      // no extra buffer — cleaner assertions
    c.max_daily_trades  = 3;
    c.last_entry_hour   = 13;
    c.news_blackout_min = 5;        // ±5 min around 8:30, 10:00, 14:00, 14:30
    c.eod_flatten_hour  = 15;
    c.eod_flatten_min   = 55;
    c.daily_loss_limit  = -999999.0;
    c.trailing_drawdown_cap = 999999.0;
    c.stop_cooldown_secs = 0;  // disable wall-clock cooldown by default; tests that need it set it explicitly
    return c;
}

// Convert (hour, minute, second) in ET (EDT=UTC-4) on 2025-04-30 to UTC microseconds.
// 2025-04-30 00:00:00 UTC = Unix epoch 1746057600
static constexpr int64_t kDate20250430_UTC = 1746057600LL; // 2025-04-30 00:00:00 UTC

static int64_t et_to_utc_us(int et_hour, int et_min, int et_sec = 0) {
    // 2025-04-30 is in EDT (UTC-4) — confirmed: DST began March 9, ends November 2
    int64_t et_seconds = kDate20250430_UTC + et_hour * 3600LL + et_min * 60LL + et_sec;
    int64_t utc_seconds = et_seconds + 4 * 3600LL;  // EDT = UTC - 4
    return utc_seconds * 1'000'000LL;
}

// Build a simple OrbTick.
static OrbTick make_tick(int et_hour, int et_min, int et_sec, double price, int64_t size = 1) {
    OrbTick t;
    t.ts_micros = et_to_utc_us(et_hour, et_min, et_sec);
    t.price     = price;
    t.size      = size;
    t.is_buy    = true;
    return t;
}

// Captured signals during a test.
struct CapturedSignal {
    OrbSignal  signal;
    double     price;
    std::string reason;
};

// Send one tick inside the range to consume the seeded_ guard after seed_orb_range.
// seed_orb_range sets seeded_=true so the first real tick only anchors last_price_
// without trading.  Call this helper right after seed_orb_range in every test.
static void anchor_after_seed(OrbStrategy& s,
                               double inside_price = 19025.0,
                               int et_hour = 10, int et_min = 6) {
    s.on_tick(make_tick(et_hour, et_min, 0, inside_price));
}

// Wire a signal collector and return a freshly reset OrbStrategy.
static OrbStrategy make_strategy(const OrbConfig& cfg,
                                 std::vector<CapturedSignal>& out) {
    OrbStrategy s(cfg);
    s.set_signal_callback([&out](OrbSignal sig, double price, const std::string& reason) {
        out.push_back({sig, price, reason});
    });
    s.reset_session();
    return s;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════════════════════

// 1. ORB range building: ticks during 9:30–9:44 ET update high/low
TEST(orb_range_accumulates_during_window) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    // Feed ticks at 9:30, 9:35, 9:40 with varying prices
    s.on_tick(make_tick(9, 30, 0,  19000.0));
    s.on_tick(make_tick(9, 35, 0,  19050.0));
    s.on_tick(make_tick(9, 40, 0,  18980.0));

    // ORB is not yet set (window still open)
    ASSERT(!s.orb_set());

    // High/low should reflect what we fed (bar accumulation, not session)
    // Note: session_.orb_high/low accumulate raw tick prices while in window
    ASSERT_NEAR(s.orb_high(), 19050.0, 0.001);
    ASSERT_NEAR(s.orb_low(),  18980.0, 0.001);

    // No signals should have been emitted during the window
    ASSERT(signals.empty());
}

// 2. No signal before the ORB window closes (orb_set == false)
TEST(no_signal_before_orb_set) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    // Feed ticks only inside the 9:30–9:44 ET window
    for (int m = 30; m < 45; ++m) {
        s.on_tick(make_tick(9, m, 30, 19100.0 + m));
    }

    ASSERT(!s.orb_set());
    ASSERT(signals.empty());
}

// 3. seed_orb_range bypasses window; subsequent tick above orb_high fires BUY
//    Use 10:10 ET — safely outside all news blackout windows (10:00 ± 5 min ends at 10:05)
TEST(buy_signal_on_breakout_above_orb_high) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    // Seed the ORB range directly (avoids bar-boundary timing complexity)
    s.seed_orb_range(19100.0, 18950.0);
    ASSERT(s.orb_set());
    anchor_after_seed(s);  // consume seeded_ guard; last_price_ = 19025

    // Tick at 10:10 ET — after ORB window, past news blackout, before last_entry_hour=13
    // price > orb_high (19100) → BUY
    s.on_tick(make_tick(10, 10, 0, 19101.0));

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
    ASSERT_NEAR(signals[0].price, 19101.0, 0.001);
}

// 4. Tick below orb_low fires SELL
TEST(sell_signal_on_breakout_below_orb_low) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    // price < orb_low (18950) → SELL  (10:10 ET — outside news blackout)
    s.on_tick(make_tick(10, 10, 0, 18949.0));

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::SELL);
    ASSERT_NEAR(signals[0].price, 18949.0, 0.001);
}

// 5. Tick at orb_high (equal, not strictly above) does NOT fire
//    check_breakout uses ">" not ">=" for buy side
TEST(no_signal_at_orb_high_exact) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);
    s.on_tick(make_tick(10, 10, 0, 19100.0));  // exactly at high, not above

    ASSERT(signals.empty());
}

// 6. No double entry — after BUY signal, second tick above orb_high is ignored
TEST(no_double_entry_long) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    s.on_tick(make_tick(10, 10, 0, 19105.0));  // BUY signal
    s.on_tick(make_tick(10, 11, 0, 19110.0));  // above high again — ignored

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
}

// 7. No double entry — after SELL signal, second tick below orb_low is ignored
TEST(no_double_entry_short) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    s.on_tick(make_tick(10, 10, 0, 18940.0));  // SELL signal
    s.on_tick(make_tick(10, 11, 0, 18930.0));  // below low again — ignored

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::SELL);
}

// 8. BUY then SELL: flatten between to allow second entry
//    in_position blocks re-entry; notify_trade_filled() clears it.
TEST(buy_then_sell_both_fire_independently) {
    OrbConfig cfg = make_cfg();
    cfg.max_daily_trades = 3;
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    // First: breakout above — BUY, in_position = true
    s.on_tick(make_tick(10, 10, 0, 19105.0));
    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);

    // Simulate fill → flat again so the short side can fire
    s.notify_trade_filled(OrbSignal::BUY);

    // Second: breakout below — SELL (now allowed since flat)
    s.on_tick(make_tick(10, 11, 0, 18940.0));
    ASSERT_EQ(signals.size(), (size_t)2);
    ASSERT(signals[1].signal == OrbSignal::SELL);
}

// 9. News blackout: 8:30 ET ± 5 min (8:25–8:35) blocks entries
//    Even if orb_set is true (via seed), ticks in that window are silenced
TEST(news_blackout_blocks_signal_at_830) {
    OrbConfig cfg = make_cfg();
    cfg.last_entry_hour = 20;  // push last entry late so only blackout matters
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s, 19025.0, 8, 20);  // 8:20 ET — before blackout starts at 8:25

    // 8:30 ET is right on the news event (8:30 ± 5 min → 8:25–8:35)
    s.on_tick(make_tick(8, 30, 0, 19105.0));  // would be BUY, blocked by blackout
    s.on_tick(make_tick(8, 32, 0, 18940.0));  // would be SELL, blocked by blackout

    ASSERT(signals.empty());
}

// 10. After news blackout window, signals resume
TEST(signals_resume_after_news_blackout) {
    OrbConfig cfg = make_cfg();
    cfg.last_entry_hour = 20;
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s, 19025.0, 8, 20);  // 8:20 ET anchor — before blackout

    // 8:36 ET is 6 minutes after 8:30 — outside ±5 window
    s.on_tick(make_tick(8, 36, 0, 19105.0));

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
}

// 11. No entries at or after last_entry_hour (default 13 ET)
TEST(no_signal_at_or_after_last_entry_hour) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    s.on_tick(make_tick(13, 0, 0, 19105.0));  // 13:00 ET — at boundary, blocked
    s.on_tick(make_tick(13, 30, 0, 19110.0)); // 13:30 ET — blocked

    ASSERT(signals.empty());
}

// 12. max_daily_trades cap: after N trades, no further signals
TEST(max_daily_trades_cap) {
    OrbConfig cfg = make_cfg();
    cfg.max_daily_trades = 1;   // only one trade allowed
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    s.on_tick(make_tick(10, 10, 0, 19105.0));  // trade 1 (BUY)
    s.on_tick(make_tick(10, 11, 0, 18940.0));  // would be SELL — blocked (trades_today >= 1)

    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
}

// 13. risk_halted flag prevents all tick processing
TEST(risk_halt_blocks_all_signals) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    s.halt_trading("test_halt");

    s.on_tick(make_tick(10, 0, 0, 19105.0));
    s.on_tick(make_tick(10, 0, 0, 18940.0));

    ASSERT(signals.empty());
    ASSERT(s.session().risk_halted);
}

// 14. reset_session clears all state including halt, in_position, and orb_set
TEST(reset_session_clears_all_state) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    s.halt_trading("test");
    s.on_tick(make_tick(10, 0, 0, 19105.0));  // blocked by halt

    s.reset_session();

    ASSERT(!s.orb_set());
    ASSERT(!s.session().risk_halted);
    ASSERT(!s.session().in_position);
    ASSERT_EQ(s.session().trades_today, 0);

    // After reset and re-seeding, signals fire again
    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);
    s.on_tick(make_tick(10, 10, 0, 19105.0));
    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
}

// 15. EOD flatten signal emitted by check_eod at or after 15:55 ET
TEST(eod_flatten_emitted_at_1555) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);

    // check_eod at 15:55 ET should emit FLATTEN_EOD
    s.check_eod(15, 55);
    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::FLATTEN_EOD);
}

// 16. EOD flatten not emitted before 15:55 ET
TEST(no_eod_flatten_before_1555) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);

    s.check_eod(15, 54);
    ASSERT(signals.empty());
}

// 17. EOD flatten emits only once even when check_eod called repeatedly
TEST(eod_flatten_emits_only_once) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);

    s.check_eod(15, 55);
    s.check_eod(15, 56);
    s.check_eod(16, 0);

    ASSERT_EQ(signals.size(), (size_t)1);
}

// 18. notify_trade_filled clears in_position, allowing next entry
TEST(notify_trade_filled_clears_in_position) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s);

    // Enter long — in_position becomes true
    s.on_tick(make_tick(10, 10, 0, 19105.0));
    ASSERT(s.session().in_position);

    // Fill notification — in_position cleared
    s.notify_trade_filled(OrbSignal::BUY);
    ASSERT(!s.session().in_position);

    // Now a short entry is possible
    s.on_tick(make_tick(10, 11, 0, 18940.0));
    ASSERT(s.session().in_position);
    s.notify_trade_filled(OrbSignal::SELL);
    ASSERT(!s.session().in_position);
}

// 19. Seeded restart guard: when seed_orb_range() is called (executor restart recovery),
//     the very first real tick anchors last_price_ but MUST NOT fire a breakout signal,
//     even if the price is already outside the ORB range.
//     Without the guard, prev_price=0 (no_prev=true) would allow any price above orb_high
//     to trigger a BUY on the first tick — a spurious entry at an unknown position.
TEST(seeded_restart_first_tick_no_signal) {
    OrbConfig cfg = make_cfg();
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    // Simulate executor restart: ORB range already known from DB
    s.seed_orb_range(19100.0, 18950.0);
    ASSERT(s.orb_set());

    // First real tick after restart: price is ABOVE orb_high — would be a BUY without guard
    s.on_tick(make_tick(10, 10, 0, 19105.0));
    ASSERT(signals.empty());  // guard: seeded_ consumed, no signal

    // Second tick at the same price: prev_price=19105 is already OUTSIDE range
    // so the cross condition (prev <= orb_high) fails → still no signal
    s.on_tick(make_tick(10, 10, 1, 19105.0));
    ASSERT(signals.empty());
}

// 20. notify_trade_filled applies cooldown on ALL exit types, not just stops.
//     With stop_cooldown_secs=2, an EOD flatten must block re-entry for 2s.
TEST(notify_trade_filled_cooldown_applies_to_all_exits) {
    OrbConfig cfg = make_cfg();
    cfg.stop_cooldown_secs = 2;
    std::vector<CapturedSignal> signals;
    OrbStrategy s = make_strategy(cfg, signals);

    // Seed ORB range and consume the seeded_ guard with an inside-range price
    s.seed_orb_range(19100.0, 18950.0);
    anchor_after_seed(s, 19025.0);  // last_price_ = 19025 (inside range), seeded_ consumed

    // Fire a BUY breakout to put strategy in_position
    s.on_tick(make_tick(10, 10, 0, 19105.0));  // price > orb_high (19100) — BUY
    ASSERT_EQ(signals.size(), (size_t)1);
    ASSERT(signals[0].signal == OrbSignal::BUY);
    signals.clear();

    // Close via a non-stop exit (EOD flatten) — must arm cooldown just like a stop
    s.notify_trade_filled(OrbSignal::BUY, "eod_flatten");
    ASSERT(!s.session().in_position);

    // Cooldown is now active (2s). Try to re-enter: prev_price=19105 (outside range),
    // so we need a cross from inside. Feed an inside tick first to reset prev_price,
    // then a breakout tick — cooldown must block the breakout signal.
    s.on_tick(make_tick(10, 10, 1, 19099.0));  // pull back inside range — no signal expected
    ASSERT(signals.empty());
    s.on_tick(make_tick(10, 10, 2, 19105.0));  // cross above orb_high — blocked by cooldown
    ASSERT(signals.empty());  // cooldown blocking re-entry
}

// ═══════════════════════════════════════════════════════════════════════════════
int main() {
    RUN(orb_range_accumulates_during_window);
    RUN(no_signal_before_orb_set);
    RUN(buy_signal_on_breakout_above_orb_high);
    RUN(sell_signal_on_breakout_below_orb_low);
    RUN(no_signal_at_orb_high_exact);
    RUN(no_double_entry_long);
    RUN(no_double_entry_short);
    RUN(buy_then_sell_both_fire_independently);
    RUN(news_blackout_blocks_signal_at_830);
    RUN(signals_resume_after_news_blackout);
    RUN(no_signal_at_or_after_last_entry_hour);
    RUN(max_daily_trades_cap);
    RUN(risk_halt_blocks_all_signals);
    RUN(reset_session_clears_all_state);
    RUN(eod_flatten_emitted_at_1555);
    RUN(no_eod_flatten_before_1555);
    RUN(eod_flatten_emits_only_once);
    RUN(notify_trade_filled_clears_in_position);
    RUN(seeded_restart_first_tick_no_signal);
    RUN(notify_trade_filled_cooldown_applies_to_all_exits);

    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
