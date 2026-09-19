/*  ═══════════════════════════════════════════════════════════════════════════
    test_bracket_broker.cpp — unit tests for the bracket-driven paper broker

    The strategy owns the bracket (cur_stop/cur_tp); the broker simulates the
    exchange. Deterministic in-memory tick feed — no DB, no network.

    Run: ./test_bracket_broker      (from repo root)
    ═══════════════════════════════════════════════════════════════════════════ */
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/config.hpp"
#include "../src/paper/paper_bracket_broker.hpp"

static int g_passed = 0;
static int g_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct _reg_##name { _reg_##name() { \
        std::printf("  %-44s ", #name); \
        try { test_##name(); std::printf("PASS\n"); ++g_passed; } \
        catch (std::exception& e) { std::printf("FAIL: %s\n", e.what()); ++g_failed; } \
    }} _inst_##name; \
    static void test_##name()

#define ASSERT(expr) \
    if (!(expr)) throw std::runtime_error("Assertion failed: " #expr)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Expected ") + std::to_string(b) + " got " + std::to_string(a))

#define ASSERT_STREQ(a, b) \
    if ((a) != (b)) throw std::runtime_error( \
        std::string("Expected '") + (b) + "' got '" + (a) + "'")

#define ASSERT_NEAR(a, b, tol) \
    if (std::fabs((a) - (b)) > (tol)) throw std::runtime_error( \
        std::string("Expected ~") + std::to_string(b) + " got " + std::to_string(a))

// ── fake sink ────────────────────────────────────────────────────────────────
struct FakeStore : paper::PaperStore {
    std::vector<paper::PaperTradeRow> trades;
    paper::PaperPositionRow last_pos;
    int saves = 0;
    void save_position(const paper::PaperPositionRow& p) override { last_pos = p; ++saves; }
    void record_trade(const paper::PaperTradeRow& t) override { trades.push_back(t); }
};

// ── helpers ──────────────────────────────────────────────────────────────────

static OrbConfig make_cfg() {
    OrbConfig c;
    c.orb_minutes = 5; c.sl_points = 15.0; c.trail_step = 10.0;
    c.trail_be_trigger = 3.0; c.trail_delay_secs = 300; c.trail_be_offset = 1.0;
    c.max_daily_trades = 20; c.last_entry_hour = 23;
    c.eod_flatten_hour = 15; c.eod_flatten_min = 55;
    c.session_open_hour = 9; c.session_open_min = 30;
    c.qty = 1; c.commission_rt = 1.0; c.point_value = 2.0;
    c.starting_balance = 25000.0; c.daily_loss_limit = -250.0;
    c.trailing_drawdown_cap = 1e9; c.consistency_cap_pct = 1.0;
    c.stop_cooldown_secs = 0;
    return c;
}

// Micros since epoch for a given ET wall time on 2026-09-18 (EDT, offset 4h).
static int64_t et_ts(int h, int m, int s = 0) {
    struct tm t{};
    t.tm_year = 2026 - 1900; t.tm_mon = 8; t.tm_mday = 18;
    t.tm_hour = h; t.tm_min = m; t.tm_sec = s;
    int64_t et_epoch = (int64_t)timegm(&t);
    int64_t utc = et_epoch + 5LL * 3600;  // initial guess (EST)
    for (int i = 0; i < 3; ++i) {
        time_t tt = (time_t)utc;
        struct tm g;
        gmtime_r(&tt, &g);
        utc = et_epoch + (int64_t)us_et_offset(g) * 3600;
    }
    return utc * 1'000'000;
}

static OrbTick tick(int64_t ts_us, double price) {
    OrbTick t;
    t.ts_micros = ts_us;
    t.price     = price;
    t.size      = 1;
    t.is_buy    = true;
    return t;
}

static const double NaN_ = std::numeric_limits<double>::quiet_NaN();

using broker_ptr = std::unique_ptr<paper::PaperBracketBroker>;
static broker_ptr make_broker(FakeStore& fs, OrbConfig cfg = make_cfg()) {
    return std::make_unique<paper::PaperBracketBroker>(
        "test_bracket", "test_acct", "MNQ", cfg, 0.25, 1, &fs);
}

// ── tests ────────────────────────────────────────────────────────────────────

TEST(signal_does_not_fill_until_next_tick) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "flag break");
    ASSERT(!b->in_position());   // no tick yet — no fill
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, 110.0, 2);
    ASSERT(b->in_position());
    ASSERT_EQ(b->direction(), 1);
    ASSERT_EQ(b->qty(), 2);
    ASSERT_NEAR(b->entry_price(), 100.25, 1e-9);   // 1 tick adverse slip
    ASSERT_NEAR(b->stop_price(), 95.0, 1e-9);
    ASSERT(fs.last_pos.qty == 2);
}

TEST(entry_without_strategy_stop_uses_fallback) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), NaN_, NaN_, 1);
    ASSERT(b->in_position());
    ASSERT_NEAR(b->stop_price(), 100.25 - 15.0, 1e-9);   // cfg sl_points fallback
}

TEST(strategy_stop_ratchet_never_loosens_long) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->on_tick(tick(et_ts(10, 6), 101.0), 96.5, NaN_, 1);   // strategy raises stop
    ASSERT_NEAR(b->stop_price(), 96.5, 1e-9);
    b->on_tick(tick(et_ts(10, 7), 102.0), 94.0, NaN_, 1);   // strategy lowers it
    ASSERT_NEAR(b->stop_price(), 96.5, 1e-9);               // broker refuses
}

TEST(strategy_stop_ratchet_never_loosens_short) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::SELL, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 105.0, NaN_, 1);
    ASSERT_EQ(b->direction(), -1);
    ASSERT_NEAR(b->entry_price(), 99.75, 1e-9);
    b->on_tick(tick(et_ts(10, 6), 99.0), 103.5, NaN_, 1);   // tighten down
    ASSERT_NEAR(b->stop_price(), 103.5, 1e-9);
    b->on_tick(tick(et_ts(10, 7), 98.0), 106.0, NaN_, 1);   // loosen up — refused
    ASSERT_NEAR(b->stop_price(), 103.5, 1e-9);
}

TEST(stop_fills_at_stop_minus_slippage) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->on_tick(tick(et_ts(10, 9), 94.75), 95.0, NaN_, 1);   // trade through 95
    ASSERT(!b->in_position());
    ASSERT_EQ(fs.trades.size(), 1u);
    ASSERT_NEAR(fs.trades[0].exit_price, 94.75, 1e-9);      // 95.00 - 0.25
    ASSERT_STREQ(fs.trades[0].exit_reason, "stop");
    // entry 100.25, exit 94.75 → -5.50 pts; -5.50*2*1 - $1 comm = -$12.00
    ASSERT_NEAR(fs.trades[0].pnl_usd, -12.0, 1e-9);
    ASSERT_NEAR(fs.trades[0].commission, 1.0, 1e-9);
}

TEST(target_fills_at_limit_price) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, 110.0, 1);
    b->on_tick(tick(et_ts(10, 9), 111.0), 95.0, 110.0, 1);  // trades past limit
    ASSERT(!b->in_position());
    ASSERT_NEAR(fs.trades[0].exit_price, 110.0, 1e-9);      // limit, no slip
    ASSERT_STREQ(fs.trades[0].exit_reason, "target");
    // entry 100.25, exit 110.00 → +9.75 pts → $19.50 - $1 = $18.50
    ASSERT_NEAR(fs.trades[0].pnl_usd, 18.5, 1e-9);
}

TEST(nan_tp_means_no_limit_leg) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->on_tick(tick(et_ts(10, 6), 150.0), 95.0, NaN_, 1);   // huge spike, no exit
    ASSERT(b->in_position());
}

TEST(tp_update_replaces_limit_leg) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, 110.0, 1);
    b->on_tick(tick(et_ts(10, 6), 104.0), 95.0, 105.0, 1);  // strategy tightens tp
    b->on_tick(tick(et_ts(10, 7), 106.0), 95.0, 105.0, 1);  // trades through 105
    ASSERT(!b->in_position());
    ASSERT_STREQ(fs.trades[0].exit_reason, "target");
    ASSERT_NEAR(fs.trades[0].exit_price, 105.0, 1e-9);
}

TEST(flatten_signal_exits_next_tick_with_reason) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->on_signal(OrbSignal::FLATTEN_EOD, 101.0, "Time stop");
    ASSERT(b->in_position());                               // fills on next tick
    b->on_tick(tick(et_ts(10, 30), 101.0), 95.0, NaN_, 1);
    ASSERT(!b->in_position());
    ASSERT_STREQ(fs.trades[0].exit_reason, "Time stop");    // reason preserved
    ASSERT_NEAR(fs.trades[0].exit_price, 100.75, 1e-9);     // market + slip
}

TEST(daily_loss_halt_blocks_entry) {
    FakeStore fs;
    auto b = make_broker(fs);
    // Cross the -250 daily limit with one real loser: qty 10, -15.5 pts.
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 85.0, NaN_, 10);  // stop 15 pts below
    b->on_tick(tick(et_ts(10, 9), 84.75), 85.0, NaN_, 10);  // stop-out
    ASSERT(b->halted());                                    // limit latched on close
    ASSERT_EQ(fs.trades.size(), 1u);
    ASSERT_NEAR(fs.trades[0].pnl_usd, -15.5 * 2.0 * 10 - 10.0, 1e-9);  // -$320
    b->on_signal(OrbSignal::BUY, 100.0, "y");
    b->on_tick(tick(et_ts(10, 20), 100.0), 95.0, NaN_, 1);
    ASSERT(!b->in_position());                              // halted — no entry
    ASSERT_EQ(fs.trades.size(), 1u);
}

TEST(eod_clock_backstop_flattens) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(15, 50), 100.0), 95.0, NaN_, 1);
    ASSERT(b->in_position());
    b->on_tick(tick(et_ts(15, 55), 100.5), 95.0, NaN_, 1);  // 15:55 ET = eod
    ASSERT(!b->in_position());
    ASSERT_STREQ(fs.trades[0].exit_reason, "eod");
}

TEST(restart_resume_keeps_persisted_stop) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->resume_position(1, 2, 100.0, 95.0, et_ts(9, 45));
    ASSERT(b->in_position());
    // Strategy restarts flat-minded → reports NaN stop; broker must keep 95.0.
    b->on_tick(tick(et_ts(10, 0), 99.0), NaN_, NaN_, 1);
    ASSERT_NEAR(b->stop_price(), 95.0, 1e-9);
    b->on_tick(tick(et_ts(10, 1), 94.9), NaN_, NaN_, 1);
    ASSERT(!b->in_position());
    ASSERT_STREQ(fs.trades[0].exit_reason, "stop");
    ASSERT_EQ(fs.trades[0].qty, 2);
}

TEST(account_halt_flatten) {
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->flatten("account_halt", et_ts(10, 10), 101.0);
    ASSERT(!b->in_position());
    ASSERT_STREQ(fs.trades[0].exit_reason, "account_halt");
    ASSERT_NEAR(fs.trades[0].exit_price, 100.75, 1e-9);
}

TEST(same_direction_signal_while_in_position_ignored) {
    // No pyramiding: a second SAME-direction signal while positioned is dropped.
    // (Opposite-direction is a reversal flip — covered above.)
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);
    b->on_signal(OrbSignal::BUY, 101.0, "dup");
    b->on_tick(tick(et_ts(10, 6), 101.0), 95.0, NaN_, 1);
    ASSERT(b->in_position());
    ASSERT_EQ(b->direction(), 1);
    ASSERT_EQ(b->qty(), 1);
    ASSERT_NEAR(b->entry_price(), 100.25, 1e-9);            // unchanged
}

TEST(stale_flatten_reason_does_not_kill_next_trade) {
    // Regression: a FLATTEN_EOD queued behind a stop-out must not leak into
    // the NEXT trade (audit B1 — proven phantom exit pre-fix).
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "x");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);      // long @100.25
    b->on_signal(OrbSignal::FLATTEN_EOD, 99.0, "Osc reversal");
    b->on_tick(tick(et_ts(10, 6), 94.75), 95.0, NaN_, 1);      // stop wins
    ASSERT_EQ(fs.trades.size(), 1u);
    ASSERT_STREQ(fs.trades[0].exit_reason, "stop");
    b->on_signal(OrbSignal::BUY, 100.0, "y");
    b->on_tick(tick(et_ts(10, 7), 100.0), 95.0, NaN_, 1);      // re-enter
    ASSERT(b->in_position());
    b->on_tick(tick(et_ts(10, 8), 100.5), 95.0, NaN_, 1);      // no phantom exit
    ASSERT(b->in_position());
    ASSERT_EQ(fs.trades.size(), 1u);
}

TEST(reversal_flip_closes_then_reenters) {
    // Regression: opposite signal while positioned must close the old leg and
    // enter the new one with the NEW bracket, not be dropped (audit B2).
    FakeStore fs;
    auto b = make_broker(fs);
    b->on_signal(OrbSignal::BUY, 100.0, "long");
    b->on_tick(tick(et_ts(10, 5), 100.0), 95.0, NaN_, 1);      // long @100.25
    b->on_signal(OrbSignal::SELL, 99.5, "flip");
    b->on_tick(tick(et_ts(10, 6), 100.0), 105.0, NaN_, 1);     // exits long
    ASSERT_EQ(fs.trades.size(), 1u);
    ASSERT_STREQ(fs.trades[0].exit_reason, "reversal");
    ASSERT(!b->in_position());                                 // one-tick gap
    b->on_tick(tick(et_ts(10, 7), 100.0), 105.0, 90.0, 1);     // enters short
    ASSERT(b->in_position());
    ASSERT_EQ(b->direction(), -1);
    ASSERT_NEAR(b->entry_price(), 99.75, 1e-9);
    ASSERT_NEAR(b->stop_price(), 105.0, 1e-9);                 // new leg's stop
}

int main() {
    std::printf("\n=== Results: %d passed, %d failed ===\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
