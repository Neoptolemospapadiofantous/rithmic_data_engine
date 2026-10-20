/*  ═══════════════════════════════════════════════════════════════════════════
    test_paper_broker.cpp — unit tests for the paper fleet fill simulator

    Deterministic in-memory tick feed (no DB) for fill-model tests; one
    restart-resume integration test against the real local PostgreSQL, fully
    cleaned up after itself (strategy_id = 'test_paper_broker').

    Run: ./test_paper_broker        (from repo root so .env is found)
    ═══════════════════════════════════════════════════════════════════════════ */
#include <cassert>
#include <initializer_list>
#include <utility>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "../src/config.hpp"
#include "../src/paper/paper_broker.hpp"
#include "../src/paper/paper_config.hpp"
#include "../src/paper/signal_publish.hpp"
#include "../src/paper/paper_db.hpp"
#include "../src/paper/paper_engine.hpp"
#include "orb_strategy.hpp"

static int g_passed  = 0;
static int g_failed  = 0;
static int g_skipped = 0;

struct SkipTest : std::exception {
    explicit SkipTest(const char* m) : msg_(m) {}
    const char* what() const noexcept override { return msg_; }
    const char* msg_;
};

#define TEST(name) \
    static void test_##name(); \
    struct _reg_##name { _reg_##name() { \
        std::printf("  %-44s ", #name); \
        try { test_##name(); std::printf("PASS\n"); ++g_passed; } \
        catch (SkipTest& e) { std::printf("SKIP: %s\n", e.what()); ++g_skipped; } \
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

// Tradeify-base config, safe risk ceilings for isolated broker tests.
static OrbConfig make_cfg() {
    OrbConfig c;
    c.orb_minutes = 5; c.sl_points = 15.0; c.trail_step = 10.0;
    c.trail_be_trigger = 3.0; c.trail_delay_secs = 300; c.trail_be_offset = 1.0;
    c.max_daily_trades = 3; c.last_entry_hour = 23;
    c.eod_flatten_hour = 15; c.eod_flatten_min = 55;
    c.session_open_hour = 9; c.session_open_min = 30;
    c.qty = 1; c.commission_rt = 1.0; c.point_value = 2.0;
    c.starting_balance = 25000.0; c.daily_loss_limit = -250.0;
    c.trailing_drawdown_cap = 1e9; c.consistency_cap_pct = 1.0;
    c.stop_cooldown_secs = 5;
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
    return OrbTick{ts_us, price, 1, true};
}

static paper::PaperBroker make_broker(FakeStore& store, OrbConfig cfg = make_cfg()) {
    return paper::PaperBroker("t_broker", "testacct", "MNQ", cfg,
                              /*tick_size=*/0.25, /*slippage_ticks=*/1, &store);
}

// Prime the broker's clock (entry-window gate) then send a BUY signal.
static void signal_buy(paper::PaperBroker& b, int64_t ts_us, double px) {
    b.on_tick(tick(ts_us, px));
    b.on_signal(OrbSignal::BUY, px, "test_signal");
}

// ── fill-model tests ─────────────────────────────────────────────────────────

TEST(market_entry_slippage_and_commission) {
    FakeStore store;
    auto b = make_broker(store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    ASSERT(!b.in_position());                       // fills on NEXT tick
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry tick
    ASSERT(b.in_position());
    ASSERT_NEAR(b.entry_price(), 20000.25, 1e-9);   // +1 tick adverse
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);    // entry − sl_points
    ASSERT_EQ(store.saves > 0, true);

    b.flatten("signal_flatten", et_ts(10, 1), 20010.0);
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
    const auto& tr = store.trades[0];
    ASSERT_STREQ(tr.direction, "LONG");
    ASSERT_NEAR(tr.entry_price, 20000.25, 1e-9);
    ASSERT_NEAR(tr.exit_price, 20009.75, 1e-9);     // market exit, adverse tick
    ASSERT_NEAR(tr.pnl_pts, 9.5, 1e-9);
    ASSERT_NEAR(tr.commission, 1.0, 1e-9);          // commission_rt * qty
    ASSERT_NEAR(tr.pnl_usd, 9.5 * 2.0 - 1.0, 1e-9); // pts*point_value − comm
}

// Relative costs (crypto history, 2026-10-10): commission_pct of notional per round trip (half per fill at its
// own price) and slippage in bps of the fill price replace commission_rt / slippage_ticks when set.
TEST(relative_costs_pct_and_bps) {
    FakeStore store;
    auto b = make_broker(store);
    b.set_relative_costs(0.001, 2.0);                // 0.10 % RT, 2 bps slip
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    ASSERT(b.in_position());
    ASSERT_NEAR(b.entry_price(), 20004.0, 1e-9);    // 20000 + 2 bps
    b.flatten("signal_flatten", et_ts(10, 1), 21000.0);
    ASSERT_EQ(store.trades.size(), (size_t)1);
    const auto& tr = store.trades[0];
    ASSERT_NEAR(tr.exit_price, 21000.0 - 4.2, 1e-9);                 // 21000 − 2 bps
    const double comm = 0.001 * 0.5 * (20004.0 + 20995.8) * 2.0;     // pct × mean notional × point_value × qty
    ASSERT_NEAR(tr.commission, comm, 1e-9);
    ASSERT_NEAR(tr.pnl_usd, (20995.8 - 20004.0) * 2.0 - comm, 1e-6);
}

TEST(stop_fill_adverse_slippage) {
    FakeStore store;
    auto b = make_broker(store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25
    ASSERT(b.in_position());

    b.on_tick(tick(et_ts(10, 0, 30), 19985.25));    // trades AT the stop
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
    const auto& tr = store.trades[0];
    ASSERT_STREQ(tr.exit_reason, "stop");
    ASSERT_NEAR(tr.exit_price, 19985.00, 1e-9);     // stop − 1 tick adverse
    ASSERT_NEAR(tr.pnl_pts, -15.25, 1e-9);
    ASSERT_NEAR(tr.pnl_usd, -15.25 * 2.0 - 1.0, 1e-9);
}

TEST(stop_gap_through_fills_at_the_print) {
    FakeStore store;
    auto b = make_broker(store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25, stop 19985.25
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(10, 0, 30), 19983.00));    // gaps 2.25 pts THROUGH the stop
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
    ASSERT_STREQ(store.trades[0].exit_reason, "stop");
    ASSERT_NEAR(store.trades[0].exit_price, 19983.00, 1e-9);   // the print, not stop − 1 tick
}

TEST(short_stop_gap_through_fills_at_the_print) {
    FakeStore store;
    auto b = make_broker(store);
    b.on_signal(OrbSignal::SELL, 20000.0, "t");
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 19999.75, stop 20014.75
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(10, 0, 30), 20017.50));    // gaps through
    ASSERT(!b.in_position());
    ASSERT_NEAR(store.trades[0].exit_price, 20017.50, 1e-9);
}

TEST(warmup_drops_entries_and_counts_them) {
    FakeStore store;
    auto b = make_broker(store);
    b.set_warmup(true);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    ASSERT(!b.in_position());                        // no pending entry, no fill
    ASSERT_EQ(b.warmup_dropped(), 1);
    ASSERT_EQ(store.trades.size(), (size_t)0);
    b.set_warmup(false);
    signal_buy(b, et_ts(10, 1), 20000.0);
    b.on_tick(tick(et_ts(10, 1, 1), 20000.0));
    ASSERT(b.in_position());                         // live again
}

TEST(breakeven_stop_placed_even_below_trail_step) {
    // 2026-09-28 hand-off shape: sl 8 / be_offset 1 / trail_step 15 — the 9-pt BE move is
    // smaller than trail_step; live now forces it to the exchange, paper must place it too.
    FakeStore store;
    OrbConfig c = make_cfg();
    c.sl_points = 8.0; c.trail_step = 15.0; c.trail_be_trigger = 3.0; c.trail_be_offset = 1.0;
    c.trail_delay_secs = 300;
    paper::PaperBroker b("t", "test", "MNQ", c, 0.25, 1, &store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry 20000.25, stop 19992.25
    b.on_tick(tick(et_ts(10, 0, 5), 20003.50));     // mfe 3.25 ≥ trigger → BE stop 20001.25
    ASSERT_NEAR(b.stop_price(),   20001.25, 1e-9);
    ASSERT_NEAR(b.placed_stop(),  20001.25, 1e-9);  // placed despite 9 pts < trail_step 15
    b.on_tick(tick(et_ts(10, 0, 6), 20001.00));     // crosses the BE stop
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "breakeven");
}

TEST(short_entry_and_stop) {
    FakeStore store;
    auto b = make_broker(store);
    b.on_tick(tick(et_ts(10, 0), 20000.0));
    b.on_signal(OrbSignal::SELL, 20000.0, "test");
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 19999.75
    ASSERT(b.in_position());
    ASSERT_NEAR(b.entry_price(), 19999.75, 1e-9);   // −1 tick adverse
    ASSERT_NEAR(b.stop_price(), 20014.75, 1e-9);    // entry + sl_points

    b.on_tick(tick(et_ts(10, 0, 30), 20014.75));    // at stop → fill stop + slip
    ASSERT(!b.in_position());
    ASSERT_NEAR(store.trades[0].exit_price, 20015.00, 1e-9);
    ASSERT_STREQ(store.trades[0].exit_reason, "stop");
}

TEST(trail_ratchet_never_retreats) {
    FakeStore store;
    OrbConfig cfg = make_cfg();
    cfg.trail_delay_secs = 60;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25

    b.on_tick(tick(et_ts(10, 1, 1), 20010.0));      // elapsed 60s, mfe 9.75 ≥ 3
    // armed: BE move to 20001.25, then trail cand = 20010−10 = 20000 → no gain
    ASSERT_NEAR(b.stop_price(), 20001.25, 1e-9);

    b.on_tick(tick(et_ts(10, 2), 20020.0));         // trail → 20010.00
    ASSERT_NEAR(b.stop_price(), 20010.00, 1e-9);
    b.on_tick(tick(et_ts(10, 3), 20015.0));         // pullback: no retreat
    ASSERT_NEAR(b.stop_price(), 20010.00, 1e-9);
    b.on_tick(tick(et_ts(10, 4), 20022.0));         // advance again
    ASSERT_NEAR(b.stop_price(), 20012.00, 1e-9);
}

// Break-even is IMMEDIATE (same rule as the live executor, order_manager.hpp): the
// first tick at which MFE ≥ trail_be_trigger moves the stop to entry + be_offset,
// no matter how long the trade has been open. The TRAIL still waits for
// trail_delay_secs (and the same MFE trigger).
TEST(breakeven_immediate_trail_after_delay) {
    FakeStore store;
    OrbConfig cfg = make_cfg();                     // delay 300s, trigger 3
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25

    b.on_tick(tick(et_ts(10, 0, 30), 20002.0));     // mfe 1.75 < trigger → stop untouched
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);

    b.on_tick(tick(et_ts(10, 1, 40), 20006.0));     // mfe 5.75 at 99s → BE NOW (no delay)
    ASSERT_NEAR(b.stop_price(), 20001.25, 1e-9);    // entry + be_offset

    b.on_tick(tick(et_ts(10, 2, 0), 20020.0));      // still inside the delay → no trail yet
    ASSERT_NEAR(b.stop_price(), 20001.25, 1e-9);

    b.on_tick(tick(et_ts(10, 5, 2), 20020.0));      // 301s → trail arms: in-memory stop = price − 10
    ASSERT_NEAR(b.stop_price(), 20010.0, 1e-9);
    ASSERT_NEAR(b.placed_stop(), 20001.25, 1e-9);   // …but 20010 is only 8.75 beyond the placed BE stop → not re-placed (live suppression)

    b.on_tick(tick(et_ts(10, 5, 10), 20009.75));    // below the in-memory stop, above the WORKING stop → still in
    ASSERT(b.in_position());

    b.on_tick(tick(et_ts(10, 5, 20), 20025.0));     // in-memory 20015, ≥10 beyond 20001.25 → placed
    ASSERT_NEAR(b.placed_stop(), 20015.0, 1e-9);

    b.on_tick(tick(et_ts(10, 5, 30), 20014.75));    // through the working stop
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "trail");
}

// Book overlays: break-even on book flip moves the stop to entry+offset only while
// price is already beyond that level; a flip while under water leaves the stop alone.
TEST(book_be_on_flip_never_on_wrong_side_of_market) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.book_be_on_flip = true;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25, stop 19985.25
    b.on_quote(paper::Quote{et_ts(10, 0, 10), 19997.75, 19998.00, 2, 9});   // book flips against (0.18 bid share)
    b.on_tick(tick(et_ts(10, 0, 11), 19998.0));     // under water → stop must NOT move
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);
    b.on_quote(paper::Quote{et_ts(10, 0, 40), 20002.75, 20003.00, 2, 9});   // still against, but price +2.5
    b.on_tick(tick(et_ts(10, 0, 41), 20003.0));     // beyond entry+1 → BE on flip fires
    ASSERT_NEAR(b.stop_price(), 20001.25, 1e-9);
    ASSERT(b.in_position());
}

TEST(breakeven_exit_reason_when_stopped_at_be) {
    FakeStore store;
    OrbConfig cfg = make_cfg();
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25
    b.on_tick(tick(et_ts(10, 0, 20), 20004.0));     // mfe 3.75 → BE at 20001.25 immediately
    b.on_tick(tick(et_ts(10, 0, 40), 20001.25));    // stop at BE fires
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "breakeven");
}

TEST(trail_exit_reason) {
    FakeStore store;
    OrbConfig cfg = make_cfg();
    cfg.trail_delay_secs = 60;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    b.on_tick(tick(et_ts(10, 1, 1), 20025.0));      // BE (20001.25) then trail to 20015 — ≥ trail_step beyond the placed BE stop, so it is re-placed
    ASSERT_NEAR(b.stop_price(), 20015.00, 1e-9);
    ASSERT_NEAR(b.placed_stop(), 20015.00, 1e-9);
    b.on_tick(tick(et_ts(10, 2), 20014.0));         // through the working trailed stop (20015)
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "trail");
    ASSERT(store.trades[0].pnl_usd > 0.0);
}

TEST(eod_flatten_by_clock) {
    FakeStore store;
    auto b = make_broker(store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(15, 55), 20050.0));        // 15:55 ET = eod_flatten
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "eod");
    ASSERT_NEAR(store.trades[0].exit_price, 20049.75, 1e-9);
}

TEST(signal_flatten_exits_next_tick) {
    FakeStore store;
    auto b = make_broker(store);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    b.on_signal(OrbSignal::FLATTEN_EOD, 20000.0, "eod_flatten");
    ASSERT(b.in_position());                        // waits for next tick
    b.on_tick(tick(et_ts(10, 1), 20002.0));
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "signal_flatten");
    ASSERT_NEAR(store.trades[0].exit_price, 20001.75, 1e-9);
}

TEST(max_daily_trades_gate) {
    FakeStore store;
    OrbConfig cfg = make_cfg();
    cfg.max_daily_trades = 1;
    cfg.stop_cooldown_secs = 0;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    ASSERT(b.in_position());
    ASSERT_EQ(b.entries_today(), 1);
    b.flatten("signal_flatten", et_ts(10, 1), 20010.0);

    b.on_signal(OrbSignal::BUY, 20010.0, "second"); // blocked: 1/1 trades
    b.on_tick(tick(et_ts(10, 2), 20010.0));
    ASSERT(!b.in_position());
    ASSERT_EQ(b.entries_today(), 1);
}

TEST(entry_window_gate) {
    FakeStore store;
    auto b = make_broker(store);
    b.on_tick(tick(et_ts(8, 0), 20000.0));          // before 9:30 session open
    b.on_signal(OrbSignal::BUY, 20000.0, "early");
    b.on_tick(tick(et_ts(8, 0, 1), 20000.0));
    ASSERT(!b.in_position());
}

// last_entry_min (2026-10-08): the paper broker cuts entries to the minute like OrbStrategy — the
// sig_orb copy of the live ORB stops at 10:30 ET, not 10:00 or 11:00.
TEST(entry_window_minute_cutoff) {
    FakeStore store; OrbConfig cfg = make_cfg(); cfg.last_entry_hour = 10; cfg.last_entry_min = 30;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 29), 20000.0);
    b.on_tick(tick(et_ts(10, 29, 1), 20000.0));
    ASSERT(b.in_position());
    b.flatten("manual", et_ts(10, 29, 30), 20000.0);
    signal_buy(b, et_ts(10, 31), 20000.0);
    b.on_tick(tick(et_ts(10, 31, 1), 20000.0));
    ASSERT(!b.in_position());
}

TEST(stop_cooldown_blocks_reentry) {
    FakeStore store;
    OrbConfig cfg = make_cfg();                     // cooldown 5s
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    b.on_tick(tick(et_ts(10, 0, 2), 19985.0));      // stopped out at t+2
    ASSERT(!b.in_position());

    b.on_signal(OrbSignal::BUY, 19990.0, "reentry");
    b.on_tick(tick(et_ts(10, 0, 4), 19990.0));      // inside cooldown (t+2+5)
    ASSERT(!b.in_position());
}

// ── risk tests ───────────────────────────────────────────────────────────────

TEST(strategy_daily_loss_halt) {
    FakeStore store;
    OrbConfig cfg = make_cfg();
    cfg.daily_loss_limit = -20.0;                   // one stop-out breaches it
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    b.on_tick(tick(et_ts(10, 0, 30), 19985.0));     // stop: −$31.50
    ASSERT(b.halted());
    ASSERT(b.risk().halt_reason().find("daily_loss_limit") != std::string::npos);

    b.on_tick(tick(et_ts(10, 1), 20000.0));
    b.on_signal(OrbSignal::BUY, 20000.0, "blocked");
    b.on_tick(tick(et_ts(10, 1, 1), 20000.0));
    ASSERT(!b.in_position());                       // halted: no new entries
}

TEST(account_daily_loss_halt) {
    paper::AccountEnvelope acct(25000.0, -500.0, 1000.0);
    ASSERT(acct.on_trade_close(-200.0).empty());
    std::string r = acct.on_trade_close(-350.0);    // day_pnl −550 ≤ −500
    ASSERT_STREQ(r, "account_daily_loss");
    ASSERT_NEAR(acct.equity(), 24450.0, 1e-9);
}

TEST(account_trailing_drawdown_halt) {
    paper::AccountEnvelope acct(25000.0, -5000.0, 1000.0);
    ASSERT(acct.on_trade_close(1500.0).empty());    // peak 26500
    ASSERT_NEAR(acct.peak_equity(), 26500.0, 1e-9);
    ASSERT(acct.on_trade_close(-900.0).empty());    // equity 25600, dd 900
    std::string r = acct.on_trade_close(-200.0);    // equity 25400 < 25500
    ASSERT_STREQ(r, "account_trailing_dd");
}

TEST(account_rollover_clears_daily_loss_only) {
    paper::AccountEnvelope acct(25000.0, -500.0, 1000.0);
    acct.set_halt("account_daily_loss");
    acct.reset_day();
    ASSERT(!acct.halted());                         // daily loss clears
    acct.set_halt("account_trailing_dd");
    acct.reset_day();
    ASSERT(acct.halted());                          // trailing DD persists
}

// ── DB integration: restart-resume ───────────────────────────────────────────

TEST(restart_resume_from_db) {
    Config env = Config::from_env(".env");
    if (env.pg_password.empty()) throw SkipTest("PG_PASSWORD not set");
    std::unique_ptr<paper::PaperDb> db;
    try {
        db = std::make_unique<paper::PaperDb>(env.pg_connstr());
    } catch (const std::exception& e) {
        throw SkipTest(e.what());
    }
    const std::string sid = "test_paper_broker";
    // Clean slate (FK order), register, and plant an open LONG position.
    db->exec_silent("DELETE FROM paper_trades    WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_positions WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_daily     WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_strategies WHERE strategy_id='" + sid + "'");
    db->upsert_strategy(sid, "testacct", "orb", "{}", true);

    paper::PaperPositionRow p;
    p.strategy_id = sid; p.direction = 1; p.qty = 2;
    p.entry_price = 20000.25; p.entry_time_us = et_ts(10, 0);
    p.stop_price = 19985.25;
    db->save_position(p);

    auto loaded = db->load_position(sid);
    ASSERT(loaded.has_value());
    ASSERT_EQ(loaded->direction, 1);
    ASSERT_EQ(loaded->qty, 2);
    ASSERT_NEAR(loaded->entry_price, 20000.25, 1e-9);

    // Resume into a fresh broker and drive it into the stop.
    OrbConfig cfg = make_cfg();
    cfg.qty = 2;
    FakeStore store;
    paper::PaperBroker b(sid, "testacct", "MNQ", cfg, 0.25, 1, &store);
    b.resume_position(loaded->direction, loaded->qty, loaded->entry_price,
                      loaded->stop_price, loaded->entry_time_us);
    ASSERT(b.in_position());
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);

    b.on_tick(tick(et_ts(10, 5), 19985.25));        // trades AT the stop (a gap-through fills at the print — see stop_gap_through_fills_at_the_print)
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
    ASSERT_NEAR(store.trades[0].pnl_pts, -15.25, 1e-9);   // stop − 1 tick adverse
    ASSERT_NEAR(store.trades[0].pnl_usd,
                -15.25 * 2.0 * 2 - cfg.commission_rt * 2, 1e-6);

    // record_trade round-trip through the real DB (insert + NOTIFY).
    paper::PaperTradeRow tr = store.trades[0];
    db->record_trade(tr);
    double sum = db->sum_pnl(sid);
    ASSERT_NEAR(sum, tr.pnl_usd, 1e-6);

    // Flat position row: load_position must return nothing after close.
    db->save_position(b.position_row(19980.0));
    ASSERT(!db->load_position(sid).has_value());

    // Cleanup.
    db->exec_silent("DELETE FROM paper_trades    WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_positions WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_daily     WHERE strategy_id='" + sid + "'");
    db->exec_silent("DELETE FROM paper_strategies WHERE strategy_id='" + sid + "'");
}


// ── relative-volume gate + volatility-targeted size (2026-09-29) ──────────────
TEST(rvol_gate_reads_the_tape) {
    paper::RegimeState r; r.set_atr("2026-09-18", 400.0);
    // 5 closed minutes of 100 contracts, then a minute of 300 (rvol 3.0 once it closes)
    for (int m = 0; m < 5; ++m) for (int k = 0; k < 4; ++k) r.on_tick("2026-09-18", 10, m, et_ts(10, m, k * 15), 20000.0, 25.0);
    for (int k = 0; k < 4; ++k) r.on_tick("2026-09-18", 10, 5, et_ts(10, 5, k * 15), 20000.0, 75.0);
    r.on_tick("2026-09-18", 10, 6, et_ts(10, 6), 20000.0, 1.0);          // closes minute 10:05
    OrbConfig c = make_cfg(); c.rvol_bars = 5;
    ASSERT_NEAR(r.rvol(5), 3.0, 1e-9);
    c.rvol_min = 1.5; ASSERT_STREQ(r.gate(c, +1), "");                  // participation present
    c.rvol_min = 4.0; ASSERT_STREQ(r.gate(c, +1), "rvol_low");
    c.rvol_min = 0.0; c.rvol_max = 2.0; ASSERT_STREQ(r.gate(c, +1), "rvol_high");
    c.rvol_bars = 20; ASSERT_STREQ(r.gate(c, +1), "rvol_warmup");      // not enough closed minutes yet
    OrbConfig off = make_cfg(); ASSERT_STREQ(r.gate(off, +1), "");     // no knob → no gate
}

// ── liquidity gate (2026-10-04, Asia/London): volume per minute + points per 1k contracts ──
TEST(liquidity_gate_reads_volume_and_impact) {
    paper::RegimeState r;
    // 4 closed overnight minutes, each 200 contracts over a 10-point range (50 pts per 1k)
    for (int m = 0; m < 4; ++m) {
        r.on_tick("2026-09-18", 2, m, et_ts(2, m, 0),  20000.0, 100.0);
        r.on_tick("2026-09-18", 2, m, et_ts(2, m, 30), 20010.0, 100.0);
    }
    r.on_tick("2026-09-18", 2, 4, et_ts(2, 4), 20005.0, 1.0);          // closes minute 02:03
    double vpm = 0, imp = 0;
    ASSERT(r.liquidity(4, vpm, imp));
    ASSERT_NEAR(vpm, 200.0, 1e-9);
    ASSERT_NEAR(imp, 50.0, 1e-9);
    OrbConfig c = make_cfg(); c.liq_bars = 4;
    c.liq_vol_min = 150.0; ASSERT_STREQ(r.gate(c, +1), "");             // enough volume
    c.liq_vol_min = 250.0; ASSERT_STREQ(r.gate(c, +1), "liq_vol_low");
    c.liq_vol_min = 0.0; c.liq_impact_max = 40.0; ASSERT_STREQ(r.gate(c, +1), "liq_thin_book");
    c.liq_impact_max = 60.0; ASSERT_STREQ(r.gate(c, +1), "");           // deep enough
    c.liq_bars = 10; ASSERT_STREQ(r.gate(c, +1), "liq_warmup");        // fail-closed on thin history
    OrbConfig off = make_cfg(); ASSERT_STREQ(r.gate(off, +1), "");     // no knob → no gate
}

// Overnight trend windows reach the broker (2026-10-04): a trend variant's win_start..win_end
// outside 09:30-15:55 becomes the broker's entry window; RTH windows keep the defaults.
TEST(trend_window_sets_broker_window_overnight_only) {
    paper::FleetConfig fc;
    paper::FleetStrategy ldn{}; ldn.id = "t_ldn"; ldn.engine = "trend";
    ldn.params_json = R"({"mode":"donchian","win_start":200,"win_end":500})";
    OrbConfig c = fc.orb_config_for(ldn);
    ASSERT_EQ(c.session_open_hour, 2); ASSERT_EQ(c.session_open_min, 0);
    ASSERT_EQ(c.eod_flatten_hour, 5);  ASSERT_EQ(c.last_entry_hour, 24);

    paper::FleetStrategy wrap = ldn; wrap.params_json = R"({"mode":"donchian","win_start":2000,"win_end":230})";
    c = fc.orb_config_for(wrap);
    ASSERT_EQ(c.session_open_hour, 20); ASSERT_EQ(c.eod_flatten_hour, 2); ASSERT_EQ(c.eod_flatten_min, 30);

    paper::FleetStrategy rth = ldn; rth.params_json = R"({"mode":"donchian","win_start":1030,"win_end":1200})";
    c = fc.orb_config_for(rth);
    ASSERT_EQ(c.session_open_hour, 9); ASSERT_EQ(c.eod_flatten_hour, 15);   // untouched

    paper::FleetStrategy expl = ldn;
    expl.params_json = R"({"mode":"donchian","win_start":200,"win_end":500,"session_open_hour":1,"eod_flatten_hour":6})";
    c = fc.orb_config_for(expl);
    ASSERT_EQ(c.session_open_hour, 1); ASSERT_EQ(c.eod_flatten_hour, 6);    // explicit keys win

    paper::FleetStrategy orb{}; orb.id = "o"; orb.engine = "orb";
    orb.params_json = R"({"win_start":200,"win_end":500})";
    c = fc.orb_config_for(orb);
    ASSERT_EQ(c.session_open_hour, 9);                                      // trend engine only
}

// Manual-signal dashboard (2026-10-07): a live_signals row exists ONLY when the broker actually opens
// the position — a gate that blocks the entry leaves no row; a passing entry leaves one, closed on exit.
struct FakeSignalSink : paper::SignalSink {
    int states = 0, inserts = 0, closes = 0, stop_updates = 0; double last_stop = 0;
    std::string last_state, last_close_reason; std::vector<std::string> notes, events;
    void add_event(int64_t, const std::string& j) override { events.push_back(j); }
    void update_stop(int64_t, double st) override { ++stop_updates; last_stop = st; }
    void notify(const std::string& t) override { notes.push_back(t); }
    void upsert_state(const std::string&, int64_t, const std::string& j) override { ++states; last_state = j; }
    int64_t insert_signal(const std::string&, int64_t, int, double, double, double, double, double, const std::string&) override { return ++inserts; }
    void close_signal(int64_t, int64_t, double, const std::string& why) override { ++closes; last_close_reason = why; }
};
TEST(signal_publisher_only_on_actual_entry) {
    TrendConfig tc; tc.mode = "trend_confirm";
    {   // book gate blocks the BUY (bid share 0.10 < imbalance_min 0.7) → no signal row
        FakeStore store; OrbConfig cfg = make_cfg(); cfg.imbalance_min = 0.7;
        auto b = make_broker(store, cfg); TrendStrategy s(tc, cfg);
        FakeSignalSink sink; paper::SignalPublisher pub("t_pub", &sink);
        b.on_quote(paper::Quote{et_ts(10, 0), 19999.75, 20000.00, 1, 9});
        signal_buy(b, et_ts(10, 0, 1), 20000.0);
        b.on_tick(tick(et_ts(10, 0, 2), 20000.0));
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 2));
        ASSERT(!b.in_position());
        ASSERT_EQ(sink.inserts, 0);
        ASSERT(sink.states >= 1);                                       // state is still published
        ASSERT(sink.last_state.find("\"book_ok\":false") != std::string::npos);
        ASSERT(sink.last_state.find("\"position\":null") != std::string::npos);
    }
    {   // no gate → the entry fills → exactly one signal row; flatten → the row is closed
        FakeStore store; OrbConfig cfg = make_cfg();
        auto b = make_broker(store, cfg); TrendStrategy s(tc, cfg);
        FakeSignalSink sink; paper::SignalPublisher pub("t_pub", &sink);
        signal_buy(b, et_ts(10, 0, 1), 20000.0);
        b.on_tick(tick(et_ts(10, 0, 2), 20000.0));
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 2));
        ASSERT(b.in_position());
        ASSERT_EQ(sink.inserts, 1);
        ASSERT(sink.last_state.find("\"stage\":\"in_trade\"") != std::string::npos);
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 3));
        ASSERT_EQ(sink.inserts, 1);                                     // no duplicate while open
        b.flatten("manual", et_ts(10, 1), 20004.0);
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 1, 1));
        ASSERT_EQ(sink.closes, 1);
        ASSERT_STREQ(sink.last_close_reason, "manual");
    }
}

// 2026-10-08: any trend mode can publish; the manual trader gets the entry, each material stop move
// (break-even / trail) and the exit on Telegram, and the live_signals stop follows the trail.
TEST(signal_publisher_trail_and_exit_alerts) {
    TrendConfig tc; tc.mode = "vwap_fade";
    {   // trail → MOVE STOP alert + DB stop update; stopped on the trail → "STOPPED OUT … trail"
        FakeStore store; OrbConfig cfg = make_cfg(); cfg.trail_delay_secs = 60;
        auto b = make_broker(store, cfg); TrendStrategy s(tc, cfg);
        FakeSignalSink sink; paper::SignalPublisher pub("t_mr", &sink, 5'000'000LL, /*trend_confirm=*/false);
        signal_buy(b, et_ts(10, 0), 20000.0);
        b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 1));
        ASSERT_EQ(sink.inserts, 1);
        ASSERT_EQ((int)sink.notes.size(), 1);
        ASSERT(sink.notes[0].find("LONG / BUY") != std::string::npos);
        ASSERT(sink.notes[0].find("1 contract") != std::string::npos);
        ASSERT(sink.notes[0].find("LONG / BUY MNQ") != std::string::npos);                 // instrument from the config
        ASSERT(sink.notes[0].find("= $30 on 1 MNQ") != std::string::npos);                 // 15-pt stop × $2
        ASSERT(sink.notes[0].find("BRACKET: stop 15.00 pts · auto-breakeven at +3.00 → entry +1.00 · trail 10.00 pts (trail starts after 1 min)") != std::string::npos);
        ASSERT(sink.last_state.find("\"mode\":\"other\"") != std::string::npos);
        b.on_tick(tick(et_ts(10, 1, 1), 20025.0));                     // BE then trail → working stop 20015
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 1, 1));
        ASSERT_EQ(sink.stop_updates, 1);
        ASSERT_NEAR(sink.last_stop, 20015.0, 1e-9);
        ASSERT((int)sink.notes.size() == 2 && sink.notes[1].find("MOVE STOP — LONG MNQ") != std::string::npos);
        ASSERT(sink.notes[1].find("20015.00") != std::string::npos);
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 1, 2));
        ASSERT_EQ(sink.stop_updates, 1);                                // no repeat while the stop is unchanged
        b.on_tick(tick(et_ts(10, 2), 20014.0));                        // through the trailed stop
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 2));
        ASSERT_EQ(sink.closes, 1);
        ASSERT((int)sink.notes.size() == 3 && sink.notes[2].find("STOPPED OUT") != std::string::npos);
        ASSERT(sink.notes[2].find("trail") != std::string::npos);
        // the timeline: entry → first lock above entry (break-even, here already the trailed 20015) → exit
        ASSERT_EQ((int)sink.events.size(), 3);
        ASSERT(sink.events[0].find("\"k\":\"entry\"") != std::string::npos);
        ASSERT(sink.events[1].find("\"k\":\"breakeven\"") != std::string::npos && sink.events[1].find("\"stop\":20015.00") != std::string::npos);
        ASSERT(sink.events[2].find("\"k\":\"exit\"") != std::string::npos && sink.events[2].find("\"note\":\"trail\"") != std::string::npos);
    }
    {   // a strategy exit (not a resting stop) → "EXIT NOW"
        FakeStore store; OrbConfig cfg = make_cfg();
        auto b = make_broker(store, cfg); TrendStrategy s(tc, cfg);
        FakeSignalSink sink; paper::SignalPublisher pub("t_mr", &sink, 5'000'000LL, false);
        signal_buy(b, et_ts(10, 0), 20000.0);
        b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 1));
        b.flatten("signal_flatten", et_ts(10, 0, 30), 20002.0);
        pub.on_tick(b, s, cfg, nullptr, et_ts(10, 0, 31));
        ASSERT((int)sink.notes.size() == 2 && sink.notes[1].find("EXIT NOW") != std::string::npos);
        ASSERT(sink.notes[1].find("SELL to close") != std::string::npos);
        ASSERT(sink.notes[1].find("on 1 MNQ") != std::string::npos);
    }
}

// ORB engine publishes too (2026-10-08): window = session open → last entry time, same alerts.
TEST(signal_publisher_orb_engine) {
    FakeStore store; OrbConfig cfg = make_cfg(); cfg.last_entry_hour = 10; cfg.last_entry_min = 30;
    auto b = make_broker(store, cfg);
    FakeSignalSink sink; paper::SignalPublisher pub("t_orb", &sink, 5'000'000LL, false);
    pub.on_tick_orb(b, cfg, nullptr, et_ts(9, 0), 20000.0);
    ASSERT(sink.last_state.find("\"in_window\":false") != std::string::npos);   // before 09:30
    pub.on_tick_orb(b, cfg, nullptr, et_ts(9, 40), 20000.0);
    ASSERT(sink.last_state.find("\"in_window\":true") != std::string::npos);
    ASSERT(sink.last_state.find("\"window\":\"0930-1030\"") != std::string::npos);
    signal_buy(b, et_ts(9, 41), 20000.0);
    b.on_tick(tick(et_ts(9, 41, 1), 20000.0));
    pub.on_tick_orb(b, cfg, nullptr, et_ts(9, 41, 1), 20000.0);
    ASSERT_EQ(sink.inserts, 1);
    ASSERT(sink.notes.size() == 1 && sink.notes[0].find("t_orb · 1 contract") != std::string::npos);
    b.flatten("eod", et_ts(9, 50), 20003.0);
    pub.on_tick_orb(b, cfg, nullptr, et_ts(9, 50, 1), 20003.0);
    ASSERT_EQ(sink.closes, 1);
    ASSERT(sink.notes.size() == 2 && sink.notes[1].find("EXIT NOW") != std::string::npos);
}

TEST(volatility_targeted_size) {
    OrbConfig c = make_cfg(); c.qty = 1; c.point_value = 2.0;
    paper::RegimeState r; r.set_atr("2026-09-18", 400.0);              // $800 per contract per daily ATR
    ASSERT_EQ(paper::RegimeState::qty_for(c, &r), 1);                   // off → plain qty
    c.vt_risk_usd = 1600.0; ASSERT_EQ(paper::RegimeState::qty_for(c, &r), 2);
    c.vt_risk_usd = 8000.0; c.vt_qty_max = 3; ASSERT_EQ(paper::RegimeState::qty_for(c, &r), 3);   // clamped
    c.vt_risk_usd = 100.0;  ASSERT_EQ(paper::RegimeState::qty_for(c, &r), 1);                     // never below 1
    paper::RegimeState none; ASSERT_EQ(paper::RegimeState::qty_for(c, &none), 1);                // no ATR → plain qty
    // the broker sizes the entry with it
    FakeStore store; OrbConfig cfg = make_cfg(); cfg.vt_risk_usd = 1600.0; cfg.vt_qty_max = 5;
    auto b = make_broker(store, cfg); b.set_regime(&r);
    signal_buy(b, et_ts(10, 0), 20000.0); b.on_tick(tick(et_ts(10, 0, 1), 20000.0));
    ASSERT_EQ(b.qty(), 2);
}

// ── restart catch-up must not manage a resumed position with ticks older than its entry ──
TEST(resume_ignores_ticks_before_entry) {
    FakeStore store;
    auto b = make_broker(store);
    const int64_t entered = et_ts(9, 43, 0);
    b.resume_position(-1, 1, 30910.25, 30920.25, entered);          // the 09-23 keltner short
    b.on_tick(tick(et_ts(9, 38, 13), 30920.50));                    // catch-up tick from BEFORE the entry, above the stop
    ASSERT(b.in_position());                                        // was: stopped out with exit < entry
    ASSERT(store.trades.empty());
    b.on_tick(tick(et_ts(9, 43, 5), 30920.50));                     // a tick after the entry: the stop is real
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "stop");
    ASSERT(store.trades[0].exit_time_us >= entered);
}

// ── windows that wrap midnight (overnight holds, 2026-09-29) ─────────────────
TEST(overnight_window_wraps_midnight) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.session_open_hour = 15; cfg.session_open_min = 55; cfg.eod_flatten_hour = 9; cfg.eod_flatten_min = 30;
    cfg.sl_points = 150; cfg.trail_be_trigger = 500; cfg.trail_step = 500;
    auto b = make_broker(store, cfg);
    b.on_tick(tick(et_ts(20, 0), 20000.0));                     // 20:00 ET: inside the wrapped window
    b.on_signal(OrbSignal::BUY, 20000.0, "hold_long");
    b.on_tick(tick(et_ts(20, 0, 1), 20000.0));
    ASSERT(b.in_position());                                    // entered — the old check would have said "outside window"
    b.on_tick(tick(et_ts(23, 30), 20010.0));                    // late evening: no EOD flatten
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(9, 30) + 86400LL * 1'000'000LL, 20020.0));   // 09:30 next day: EOD
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "eod");
    // a normal window (09:30 → 15:55) is unchanged: 20:00 is outside it
    FakeStore st2; auto b2 = make_broker(st2);
    b2.on_tick(tick(et_ts(20, 0), 20000.0)); b2.on_signal(OrbSignal::BUY, 20000.0, "x"); b2.on_tick(tick(et_ts(20, 0, 1), 20000.0));
    ASSERT(!b2.in_position());
}

// ── fixed take-profit (tp_points / tp_r) ─────────────────────────────────────
TEST(take_profit_fills_at_target) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.tp_points = 10.0;
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25
    b.on_tick(tick(et_ts(10, 0, 5), 20009.75));     // just short of the target — still in
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(10, 0, 6), 20010.50));     // through the target → limit fills AT 20010.25
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
    ASSERT_STREQ(store.trades[0].exit_reason, "take_profit");
    ASSERT_NEAR(store.trades[0].exit_price, 20010.25, 1e-9);
    ASSERT_NEAR(store.trades[0].pnl_pts, 10.0, 1e-9);
}

// E1 (drive_stop_atr, 2026-10-14): set_stop_dist gives this trade its own stop distance and the tp_r target
// scales with it; the next entry is back on sl_points.
TEST(stop_dist_sets_stop_and_scales_tp_r) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.tp_r = 3.0;                 // sl_points 15 → old target would be 45
    auto b = make_broker(store, cfg);
    b.on_tick(tick(et_ts(10, 0), 20000.0));
    b.set_stop_dist(20.0);
    b.on_signal(OrbSignal::BUY, 20000.0, "drive_market_long");
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));                  // entry @ 20000.25
    ASSERT_NEAR(b.stop_price(), 19980.25, 1e-9);               // fill − 20
    b.on_tick(tick(et_ts(10, 0, 5), 20050.0));                  // +49.75: past 45, short of 60 → still in
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(10, 0, 9), 20060.50));                 // through entry + 60 → fills AT 20060.25
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "take_profit");
    ASSERT_NEAR(store.trades[0].exit_price, 20060.25, 1e-9);
    b.on_tick(tick(et_ts(10, 1, 0), 20000.0));
    b.on_signal(OrbSignal::BUY, 20000.0, "next");               // no distance given → sl_points again
    b.on_tick(tick(et_ts(10, 1, 1), 20000.0));
    ASSERT_NEAR(b.stop_price(), 20000.25 - 15.0, 1e-9);
}

// set_target_px (2026-10-20, flag_take): an absolute strategy target — a resting limit filled AT the price, exit reason "target";
// the structural stop (set_stop_hint) is the flag low; the next entry has no target unless given one again.
TEST(strategy_target_px_fills_at_target) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.sl_points = 1000; cfg.trail_be_trigger = 50000; cfg.trail_step = 50000;
    auto b = make_broker(store, cfg);
    b.on_tick(tick(et_ts(10, 0), 20090.0));
    b.set_stop_dist(14.5); b.set_stop_hint(20075.75); b.set_target_px(20151.0);
    b.on_signal(OrbSignal::BUY, 20090.25, "flag_take_pole_top");
    b.on_tick(tick(et_ts(10, 0, 1), 20090.25));                 // entry @ 20090.50 (1 tick slippage)
    ASSERT_NEAR(b.stop_price(), 20075.75, 1e-9);               // the flag low − 1 tick, not fill − 14.5
    ASSERT_NEAR(b.strategy_target(), 20151.0, 1e-9);
    b.on_tick(tick(et_ts(10, 5, 0), 20150.75));                 // just short
    ASSERT(b.in_position());
    b.on_tick(tick(et_ts(10, 5, 1), 20151.50));                 // through → fills AT 20151
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "target");
    ASSERT_NEAR(store.trades[0].exit_price, 20151.0, 1e-9);
    b.on_tick(tick(et_ts(10, 6, 0), 20150.0));
    b.on_signal(OrbSignal::BUY, 20150.0, "next");               // no target given → none
    b.on_tick(tick(et_ts(10, 6, 1), 20150.0));
    ASSERT_NEAR(b.strategy_target(), 0.0, 1e-9);
}

// be_r (2026-10-17, mom_dir): break-even when MFE ≥ be_r × this trade's stop distance, not the flat trail_be_trigger
TEST(be_r_moves_stop_at_one_r) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.be_r = 1.0; cfg.trail_be_trigger = 50000; cfg.trail_step = 50000; cfg.trail_be_offset = 1.5;
    auto b = make_broker(store, cfg);
    b.on_tick(tick(et_ts(10, 0), 20000.0));
    b.set_stop_dist(20.0);
    b.on_signal(OrbSignal::BUY, 20000.0, "mom_dir_long");
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));                  // entry @ 20000.25, stop 19980.25
    b.on_tick(tick(et_ts(10, 0, 5), 20015.0));                  // +14.75 < 1R: stop unchanged
    ASSERT_NEAR(b.stop_price(), 19980.25, 1e-9);
    b.on_tick(tick(et_ts(10, 0, 9), 20020.50));                 // +20.25 ≥ 1R → stop to entry + 1.5
    ASSERT_NEAR(b.stop_price(), 20001.75, 1e-9);
    b.on_tick(tick(et_ts(10, 1, 0), 20001.50));                 // back through it → out at the break-even stop
    ASSERT(!b.in_position());
    ASSERT(store.trades[0].pnl_pts > 0.0);
}

TEST(take_profit_r_multiple_and_off_by_default) {
    OrbConfig c = make_cfg();
    ASSERT_NEAR(c.take_profit_pts(), 0.0, 1e-9);            // off: nothing changes for the fleet
    c.tp_r = 2.0;  ASSERT_NEAR(c.take_profit_pts(), 30.0, 1e-9);   // 2 × sl_points(15)
    c.tp_points = 12.0; ASSERT_NEAR(c.take_profit_pts(), 12.0, 1e-9); // explicit points win
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.tp_r = 1.0;              // target = entry + 15
    auto b = make_broker(store, cfg);
    b.on_tick(tick(et_ts(10, 0), 20000.0));
    b.on_signal(OrbSignal::SELL, 20000.0, "test_signal");
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 19999.75
    b.on_tick(tick(et_ts(10, 0, 9), 19984.75));     // 15 pts in favour → fills at 19984.75
    ASSERT(!b.in_position());
    ASSERT_STREQ(store.trades[0].exit_reason, "take_profit");
    ASSERT_NEAR(store.trades[0].pnl_pts, 15.0, 1e-9);
}

// ── regime gate (paper_quote.hpp RegimeState) ────────────────────────────────
// Session shape since the 09:30 open vs the prior day's ATR: the gate every strategy,
// live and paper, can switch on without touching strategy logic (2026-09-28).

static paper::RegimeState regime_day(double atr, std::initializer_list<std::pair<int, double>> path) {
    // path = {(minutes after 09:30, price)…}; open is the first point
    paper::RegimeState r; r.set_atr("2026-09-18", atr);
    for (const auto& [min, px] : path) {
        int h = 9 + (30 + min) / 60, m = (30 + min) % 60;
        r.on_tick("2026-09-18", h, m, et_ts(h, m), px);
    }
    return r;
}

TEST(regime_gate_off_by_default) {
    OrbConfig c = make_cfg();
    ASSERT(!paper::RegimeState::any_gate(c));
    paper::RegimeState r;                             // no ticks, no ATR — still allowed
    ASSERT_STREQ(r.gate(c, +1), "");
    ASSERT_STREQ(r.gate(c, -1), "");
}

TEST(regime_readings_and_warmup) {
    auto r = regime_day(100.0, {{0, 20000.0}, {5, 20030.0}, {10, 19990.0}, {20, 20020.0}});
    ASSERT_NEAR(r.range_pts(), 40.0, 1e-9);           // 20030 − 19990
    ASSERT_NEAR(r.range_atr(), 0.4, 1e-9);
    ASSERT_NEAR(r.eff(), 0.5, 1e-9);                  // |20020 − 20000| / 40
    ASSERT_NEAR(r.move_atr(), 0.2, 1e-9);
    ASSERT_EQ(r.minutes, 20);
    OrbConfig c = make_cfg(); c.regime_min_eff = 0.4; c.regime_min_minutes = 30;
    ASSERT_STREQ(r.gate(c, +1), "regime_warmup");    // 20 min < 30
    c.regime_min_minutes = 15;
    ASSERT_STREQ(r.gate(c, +1), "");
    c.regime_min_eff = 0.6;
    ASSERT_STREQ(r.gate(c, +1), "regime_eff_low");
    c.regime_min_eff = 0.0; c.regime_max_eff = 0.4;
    ASSERT_STREQ(r.gate(c, +1), "regime_eff_high");
}

TEST(regime_pre_rth_ticks_are_ignored_and_date_resets) {
    paper::RegimeState r; r.set_atr("2026-09-18", 100.0);
    r.on_tick("2026-09-18", 9, 15, et_ts(9, 15), 19900.0);   // pre-market: not the open
    ASSERT(!r.has_open());
    r.on_tick("2026-09-18", 9, 30, et_ts(9, 30), 20000.0);
    ASSERT(r.has_open()); ASSERT_NEAR(r.open, 20000.0, 1e-9);
    r.on_tick("2026-09-19", 9, 31, et_ts(9, 31), 21000.0);   // new trade date → fresh session, ATR unknown again
    ASSERT_STREQ(r.date, "2026-09-19");
    ASSERT_NEAR(r.open, 21000.0, 1e-9);
    ASSERT_NEAR(r.atr_pts, 0.0, 1e-9);
}

TEST(regime_atr_gates_fail_closed_without_atr) {
    auto r = regime_day(0.0, {{0, 20000.0}, {20, 20040.0}});   // no session_stats row
    OrbConfig c = make_cfg(); c.regime_min_range_atr = 0.35;
    ASSERT_STREQ(r.gate(c, +1), "regime_no_atr");
    auto r2 = regime_day(100.0, {{0, 20000.0}, {20, 20020.0}});  // range 20 = 0.2 ATR
    ASSERT_STREQ(r2.gate(c, +1), "regime_range_low");
    auto r3 = regime_day(100.0, {{0, 20000.0}, {20, 20040.0}});  // 0.4 ATR
    ASSERT_STREQ(r3.gate(c, +1), "");
    c.regime_min_range_atr = 0.0; c.regime_max_range_atr = 0.3;
    ASSERT_STREQ(r3.gate(c, +1), "regime_range_high");
    c.regime_max_range_atr = 0.0; c.regime_max_move_atr = 0.3;     // extended session: don't chase
    ASSERT_STREQ(r3.gate(c, +1), "regime_move_extended");
    c.regime_max_move_atr = 0.0; c.regime_min_move_atr = 0.5;
    ASSERT_STREQ(r3.gate(c, +1), "regime_move_small");
}

TEST(regime_with_and_against_the_session_move) {
    auto up = regime_day(100.0, {{0, 20000.0}, {20, 20040.0}});
    OrbConfig c = make_cfg(); c.regime_with_move = 1;             // trend-follow: only with the move
    ASSERT_STREQ(up.gate(c, +1), "");
    ASSERT_STREQ(up.gate(c, -1), "regime_against_move");
    c.regime_with_move = -1;                                       // fade: only against it
    ASSERT_STREQ(up.gate(c, +1), "regime_with_move");
    ASSERT_STREQ(up.gate(c, -1), "");
    auto flat = regime_day(100.0, {{0, 20000.0}, {20, 20000.0}});
    ASSERT_STREQ(flat.gate(c, -1), "regime_no_move");
}

TEST(regime_gate_blocks_paper_entry_like_the_book_gate) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.regime_with_move = 1; cfg.regime_min_minutes = 0;
    auto b = make_broker(store, cfg);
    auto r = regime_day(100.0, {{0, 20000.0}, {20, 20040.0}});   // session is UP
    b.set_regime(&r);
    signal_buy(b, et_ts(9, 50), 20040.0);                         // with the move → pending
    b.on_tick(tick(et_ts(9, 50, 1), 20040.0));
    ASSERT(b.in_position());
    b.flatten("test", et_ts(9, 51), 20045.0);
    ASSERT(!b.in_position());
    b.on_tick(tick(et_ts(9, 52), 20045.0));                       // past the 5 s cooldown
    b.on_signal(OrbSignal::SELL, 20045.0, "test_signal");        // against the move → blocked
    b.on_tick(tick(et_ts(9, 52, 1), 20045.0));
    ASSERT(!b.in_position());
    ASSERT_EQ(store.trades.size(), (size_t)1);
}


// 2026-10-10: a paper ORB entry the broker refuses (here the regime gate) must not lock the
// variant out for the day — the release gives it back its position AND its trade count, so with
// max_daily_trades 1 the next breakout still trades. Before, only trend runners were released.
TEST(orb_refused_entry_released_and_not_counted) {
    FakeStore store;
    OrbConfig cfg = make_cfg(); cfg.max_daily_trades = 1;
    cfg.regime_with_move = 1; cfg.regime_min_minutes = 0;
    auto b = make_broker(store, cfg);
    auto r = regime_day(100.0, {{0, 20000.0}, {20, 20040.0}});   // session is UP → shorts refused
    b.set_regime(&r);
    OrbStrategy s(cfg);
    s.set_signal_callback([&](OrbSignal sig, double px, const std::string& why) { b.on_signal(sig, px, why); });
    int released = 0;
    auto step = [&](int64_t ts, double px) {
        b.on_tick(tick(ts, px)); s.on_tick(tick(ts, px));
        if (paper::release_refused_entry(s, b, true)) ++released;
    };
    step(et_ts(9, 30), 20000.0); step(et_ts(9, 32), 20040.0); step(et_ts(9, 34), 19980.0);
    step(et_ts(9, 36), 20010.0);                                  // range 19980–20040 set, inside
    step(et_ts(9, 40), 19970.0);                                  // SHORT breakout → refused
    ASSERT_EQ(released, 1);
    ASSERT(!s.session().in_position);
    ASSERT_EQ(s.session().trades_today, 0);
    ASSERT(!b.in_position());
    step(et_ts(9, 41), 20010.0);                                  // back inside, past the cooldown
    step(et_ts(9, 42), 20050.0);                                  // LONG breakout → with the move
    ASSERT(b.has_pending_entry());
    ASSERT_EQ(s.session().trades_today, 1);
    step(et_ts(9, 42, 1), 20051.0);                               // fills
    ASSERT(b.in_position());
    ASSERT_EQ(released, 1);                                       // a held trade is never released
    ASSERT_EQ(s.session().trades_today, 1);
}

// ── price levels (paper_levels.hpp, 2026-10-11) ──────────────────────────────
// UTC µs of an ET wall time on a September 2026 day (EDT).
static int64_t et_day(int mday, int h, int m) {
    struct tm t{}; t.tm_year = 2026 - 1900; t.tm_mon = 8; t.tm_mday = mday; t.tm_hour = h; t.tm_min = m;
    return ((int64_t)timegm(&t) + 4LL * 3600) * 1'000'000;
}
// One RTH session (09:30–15:59) whose bars step lo..lo+9 (low), high = low + 1, volume heavy at step 5,
// then a 16:00 bar that closes the session. Returns nothing; pdh = lo + 10, pdl = lo, pdc = lo + 9.5.
static void lv_session(paper::LevelState& L, int mday, double lo, double scale = 1.0) {
    for (int i = 0; i < 390; ++i) {
        const int mins = 9 * 60 + 30 + i;
        const double l = lo + (i % 10) * scale, h = l + scale, c = l + 0.5 * scale;
        L.on_bar(et_day(mday, mins / 60, mins % 60), l, h, l, c, (i % 10 == 5) ? 100.0 : 1.0);
    }
    L.on_bar(et_day(mday, 16, 0), lo + 9.5 * scale, lo + 9.5 * scale, lo + 9.5 * scale, lo + 9.5 * scale, 1.0);
}

TEST(levels_prior_day_profile_and_overnight) {
    paper::LevelState L;
    lv_session(L, 14, 100.0);                                       // Mon 2026-09-14
    ASSERT(L.have_prior_day());
    ASSERT_NEAR(L.pdh(), 110.0, 1e-9); ASSERT_NEAR(L.pdl(), 100.0, 1e-9); ASSERT_NEAR(L.pdc(), 109.5, 1e-9);
    ASSERT(L.poc() >= 105.0 && L.poc() <= 106.0);                    // volume sat on the 105–106 bars
    ASSERT(L.val() <= L.poc() && L.poc() <= L.vah());
    ASSERT_NEAR(L.atr(), 10.0, 1e-9);                                // fallback ATR = the one session's range
    L.on_bar(et_day(14, 18, 0), 108, 112, 107, 111, 5);             // overnight of Tue's globex day
    L.on_bar(et_day(15, 3, 0), 100, 101, 95, 96, 5);
    ASSERT_NEAR(L.onh(), 112.0, 1e-9); ASSERT_NEAR(L.onl(), 95.0, 1e-9);
    L.on_bar(et_day(14, 16, 30), 1, 999, 1, 2, 5);                  // out of order: ignored
    ASSERT_NEAR(L.onh(), 112.0, 1e-9);
}

TEST(levels_opening_range_and_session_roll) {
    paper::LevelState L;
    lv_session(L, 14, 100.0);
    for (int i = 0; i < 5; ++i) L.on_bar(et_day(15, 9, 30 + i), 120 + i, 121 + i, 119 + i, 120 + i, 1);
    ASSERT(L.or_ready(0)); ASSERT(!L.or_ready(1));
    ASSERT_NEAR(L.or_hi(0), 125.0, 1e-9); ASSERT_NEAR(L.or_lo(0), 119.0, 1e-9);
    ASSERT_NEAR(L.rth_open(), 120.0, 1e-9);
    ASSERT_NEAR(L.pdh(), 110.0, 1e-9);                               // Monday stays the prior day during Tuesday
}

TEST(levels_week_roll_and_naked_poc) {
    paper::LevelState L;
    for (int d = 14; d <= 18; ++d) lv_session(L, d, 100.0 + d);     // Mon–Fri, lows 114..118
    ASSERT_NEAR(L.pwh(), 0.0, 1e-9);                                 // no completed week yet
    L.on_bar(et_day(21, 9, 30), 200, 201, 199, 200, 1);             // next Monday opens
    ASSERT_NEAR(L.pwh(), 128.0, 1e-9); ASSERT_NEAR(L.pwl(), 114.0, 1e-9);
    // Friday's POC (≈123.5) is naked until a bar trades through it; Monday's 199–201 bar did not
    bool has = false; for (double p : L.npocs()) has = has || (p > 123.0 && p < 124.0);
    ASSERT(has);
    L.on_bar(et_day(21, 9, 31), 200, 200, 120, 121, 1);
    has = false; for (double p : L.npocs()) has = has || (p > 123.0 && p < 124.0);
    ASSERT(!has);
}

TEST(levels_round_step_and_tick_folding) {
    ASSERT_NEAR(paper::LevelState::round_step(20000.0), 50.0, 1e-9);
    ASSERT_NEAR(paper::LevelState::round_step(100000.0), 500.0, 1e-9);
    ASSERT_NEAR(paper::LevelState::round_step(2500.0), 5.0, 1e-9);
    ASSERT_EQ(paper::parse_level_set("day,round"), paper::LV_DAY | paper::LV_ROUND);
    ASSERT_EQ(paper::parse_level_set("all"), (int)paper::LV_ALL);
    paper::LevelState L;                                             // ticks: a minute folds when the next starts
    lv_session(L, 14, 100.0);
    L.on_tick(et_day(15, 9, 30) + 1'000'000, 150.0, 1); L.on_tick(et_day(15, 9, 30) + 2'000'000, 152.0, 1);
    L.on_tick(et_day(15, 9, 31) + 1'000'000, 151.0, 1);
    ASSERT_NEAR(L.rth_open(), 150.0, 1e-9);
}

TEST(levels_entry_gates) {
    paper::LevelState L;
    lv_session(L, 14, 20000.0, 3.0);                                 // pdh 20030, pdl 20000
    OrbConfig c = make_cfg(); c.lv_mask = paper::LV_DAY;
    c.lv_room_atr = 0.1;                                             // ATR 100 → need 10 pts of room
    ASSERT_STREQ(L.gate(c, +1, 20025.0, 100.0), std::string("lv_room"));   // pdh 5 above a long
    ASSERT_STREQ(L.gate(c, -1, 20025.0, 100.0), std::string(""));          // short: pdc 20028.5 is behind
    ASSERT_STREQ(L.gate(c, +1, 20025.0, 0.0), std::string(""));            // no ATR → pass (variant = base)
    c.lv_room_atr = 0.0; c.lv_near_atr = 0.02;                       // within 2 pts of a level
    ASSERT_STREQ(L.gate(c, +1, 20015.0, 100.0), std::string("lv_far"));
    ASSERT_STREQ(L.gate(c, +1, 20001.0, 100.0), std::string(""));
}

TEST(levels_broker_stop_and_target) {
    paper::LevelState L;
    lv_session(L, 17, 19990.0, 4.0);                                 // Thu: pdl 19990, pdh 20030, pdc 20028
    L.on_bar(et_day(18, 9, 30), 20000, 20001, 19999, 20000, 1);      // Fri: today's open 20000
    paper::RegimeState reg; reg.atr_pts = 100.0;
    FakeStore store; OrbConfig cfg = make_cfg();
    cfg.lv_mask = paper::LV_DAY; cfg.lv_stop = true; cfg.lv_target = true;
    paper::PaperBroker b("t_lv", "testacct", "MNQ", cfg, 0.25, 1, &store);
    b.set_regime(&reg); b.set_levels(&L);
    const int64_t t0 = et_day(18, 9, 40);
    b.on_tick(tick(t0, 20000.0)); b.on_signal(OrbSignal::BUY, 20000.0, "test");
    b.on_tick(tick(t0 + 1'000'000, 20000.0));                        // fills 20000.25
    ASSERT(b.in_position());
    ASSERT_NEAR(b.stop_price(), 19988.0, 1e-9);                      // pdl 19990 − 2 pts buffer (tighter than −15)
    ASSERT_NEAR(b.level_target(), 20028.0, 1e-9);                    // pdc 20028 is the first level ≥ 10 pts ahead
    b.on_tick(tick(t0 + 2'000'000, 20028.0));
    ASSERT(!b.in_position());
    ASSERT_EQ((int)store.trades.size(), 1);
    ASSERT_STREQ(store.trades[0].exit_reason, std::string("level_target"));
    ASSERT_NEAR(store.trades[0].exit_price, 20028.0, 1e-9);
}

TEST(levels_stop_never_widens) {
    paper::LevelState L;
    lv_session(L, 17, 19970.0, 6.0);                                 // pdl 19970: 30 pts behind a 20000 long
    paper::RegimeState reg; reg.atr_pts = 100.0;
    FakeStore store; OrbConfig cfg = make_cfg(); cfg.lv_mask = paper::LV_DAY; cfg.lv_stop = true;
    paper::PaperBroker b("t_lv2", "testacct", "MNQ", cfg, 0.25, 1, &store);
    b.set_regime(&reg); b.set_levels(&L);
    const int64_t t0 = et_day(18, 9, 40);
    b.on_tick(tick(t0, 20000.0)); b.on_signal(OrbSignal::BUY, 20000.0, "test");
    b.on_tick(tick(t0 + 1'000'000, 20000.0));
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);                     // sl_points 15 kept
}

// ── limit entries (entry_limit_ticks, 2026-10-16, paper only) ───────────────
TEST(limit_entry_fills_only_when_traded_through) {
    FakeStore store; OrbConfig c = make_cfg(); c.entry_limit_ticks = 1; c.entry_limit_secs = 60;
    auto b = make_broker(store, c);
    signal_buy(b, et_ts(10, 0), 20000.0);           // limit 19999.75
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // market tick: a market order would fill here
    ASSERT(!b.in_position());
    b.on_tick(tick(et_ts(10, 0, 5), 19999.75));     // touches the limit only — queue unknown, no fill
    ASSERT(!b.in_position());
    b.on_tick(tick(et_ts(10, 0, 9), 19999.50));     // trades through by a tick → filled AT the limit, no slippage
    ASSERT(b.in_position());
    ASSERT_NEAR(b.entry_price(), 19999.75, 1e-9);
    ASSERT_NEAR(b.stop_price(), 19984.75, 1e-9);
    ASSERT_EQ(b.limit_fills(), 1);
}
TEST(limit_entry_cancelled_after_timeout) {
    FakeStore store; OrbConfig c = make_cfg(); c.entry_limit_ticks = 1; c.entry_limit_secs = 60;
    auto b = make_broker(store, c);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 30), 20002.0));     // runs away
    ASSERT(!b.in_position() && b.has_pending_entry());
    b.on_tick(tick(et_ts(10, 1, 1), 20003.0));      // 61 s → cancelled
    ASSERT(!b.has_pending_entry());
    b.on_tick(tick(et_ts(10, 1, 5), 19990.0));      // a later dip does not fill a cancelled order
    ASSERT(!b.in_position());
    ASSERT_EQ(b.limit_misses(), 1);
    ASSERT_EQ(b.entries_today(), 0);                // a miss is not a trade
}
TEST(limit_entry_sell_side) {
    FakeStore store; OrbConfig c = make_cfg(); c.entry_limit_ticks = 2;
    auto b = make_broker(store, c);
    b.on_tick(tick(et_ts(10, 0), 20000.0)); b.on_signal(OrbSignal::SELL, 20000.0, "t");   // limit 20000.50
    b.on_tick(tick(et_ts(10, 0, 2), 20000.50));
    ASSERT(!b.in_position());
    b.on_tick(tick(et_ts(10, 0, 4), 20000.75));
    ASSERT(b.in_position());
    ASSERT_NEAR(b.entry_price(), 20000.50, 1e-9);
}

int main() {
    std::printf("paper_broker tests:\n");
    // tests self-register via static initializers
    std::printf("\n%d passed, %d failed, %d skipped\n", g_passed, g_failed, g_skipped);
    return g_failed > 0 ? 1 : 0;
}
