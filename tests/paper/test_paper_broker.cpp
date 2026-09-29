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
#include "../src/paper/paper_db.hpp"
#include "../src/paper/paper_engine.hpp"

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

    b.on_tick(tick(et_ts(10, 5), 19980.0));         // through the stop
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

int main() {
    std::printf("paper_broker tests:\n");
    // tests self-register via static initializers
    std::printf("\n%d passed, %d failed, %d skipped\n", g_passed, g_failed, g_skipped);
    return g_failed > 0 ? 1 : 0;
}
