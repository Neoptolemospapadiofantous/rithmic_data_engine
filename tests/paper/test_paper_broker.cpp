/*  ═══════════════════════════════════════════════════════════════════════════
    test_paper_broker.cpp — unit tests for the paper fleet fill simulator

    Deterministic in-memory tick feed (no DB) for fill-model tests; one
    restart-resume integration test against the real local PostgreSQL, fully
    cleaned up after itself (strategy_id = 'test_paper_broker').

    Run: ./test_paper_broker        (from repo root so .env is found)
    ═══════════════════════════════════════════════════════════════════════════ */
#include <cassert>
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

TEST(breakeven_move_requires_trigger_and_delay) {
    FakeStore store;
    OrbConfig cfg = make_cfg();                     // delay 300s, trigger 3
    auto b = make_broker(store, cfg);
    signal_buy(b, et_ts(10, 0), 20000.0);
    b.on_tick(tick(et_ts(10, 0, 1), 20000.0));      // entry @ 20000.25

    b.on_tick(tick(et_ts(10, 1, 40), 20006.0));     // mfe 5.75 but only 99s
    ASSERT_NEAR(b.stop_price(), 19985.25, 1e-9);    // not armed yet

    b.on_tick(tick(et_ts(10, 5, 2), 20006.0));      // elapsed 301s, mfe ok
    ASSERT_NEAR(b.stop_price(), 20001.25, 1e-9);    // entry + be_offset

    b.on_tick(tick(et_ts(10, 5, 30), 20001.25));    // stop at BE fires
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
    b.on_tick(tick(et_ts(10, 1, 1), 20020.0));      // arm + trail to 20010
    ASSERT_NEAR(b.stop_price(), 20010.00, 1e-9);
    b.on_tick(tick(et_ts(10, 2), 20009.0));         // through trailed stop
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

int main() {
    std::printf("paper_broker tests:\n");
    // tests self-register via static initializers
    std::printf("\n%d passed, %d failed, %d skipped\n", g_passed, g_failed, g_skipped);
    return g_failed > 0 ? 1 : 0;
}
