/*  test_lighter.cpp — Lighter book/trade parsing (src/dex/lighter_feed.hpp), Lighter replay through the
    shared loader (src/dex/hl_events.hpp), the simulator's halt/flatten controls and the paper portfolio's
    risk controls (src/dex/mm_paper.hpp), the live re-order buffer, the exchange-clock halt, the post-halt re-flatten
    and the DB write path's per-statement retry + persisted drop ledger (src/dex/mm_paper_db.hpp). No network, no DB.                                         */
#include "dex/hl_events.hpp"
#include "dex/mm_paper.hpp"
#include "dex/mm_paper_db.hpp"

#include <unistd.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <iostream>
#include <stdexcept>

static int tests_run = 0, tests_failed = 0;
#define TEST(name) void test_##name()
#define RUN(name) do { ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while (0)
#define ASSERT(c) do { if (!(c)) throw std::runtime_error("Assert failed: " #c); } while (0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while (0)
#define ASSERT_NEAR(a, b, e) do { if (std::abs((a) - (b)) > (e)) throw std::runtime_error("ASSERT_NEAR failed: " #a " vs " #b); } while (0)

using json = nlohmann::json;

static json snap(int64_t ts, int64_t nonce) {
    return json::parse(R"({"channel":"order_book:27","type":"subscribed/order_book","timestamp":)" + std::to_string(ts) +
        R"(,"order_book":{"code":0,"nonce":)" + std::to_string(nonce) +
        R"(,"asks":[{"price":"101.0","size":"2.0"},{"price":"102.0","size":"5.0"}],"bids":[{"price":"100.0","size":"3.0"},{"price":"99.0","size":"4.0"}]}})");
}
static json delta(int64_t ts, int64_t begin, int64_t nonce, const std::string& asks, const std::string& bids) {
    return json::parse(R"({"channel":"order_book:27","type":"update/order_book","timestamp":)" + std::to_string(ts) +
        R"(,"order_book":{"code":0,"begin_nonce":)" + std::to_string(begin) + R"(,"nonce":)" + std::to_string(nonce) +
        R"(,"asks":)" + asks + R"(,"bids":)" + bids + "}}");
}

TEST(snapshot_then_deltas_rebuild_the_book) {
    lighter::Book b;
    ASSERT(b.apply(snap(1000, 10)) == lighter::Book::Apply::Snapshot);
    ASSERT(b.valid());
    ASSERT_EQ(lighter::market_of(snap(1, 1), "order_book"), 27);
    ASSERT_EQ(lighter::market_of(snap(1, 1), "trade"), -1);
    // remove the 101 ask, add 100.5 bid, change 99 bid
    ASSERT(b.apply(delta(1001, 10, 11, R"([{"price":"101.0","size":"0.000"}])", R"([{"price":"100.5","size":"1.0"},{"price":"99.0","size":"9.0"}])")) == lighter::Book::Apply::Delta);
    hl::Book t = b.top(5);
    ASSERT_EQ(t.t_ms, int64_t{1001});
    ASSERT_NEAR(t.asks[0].px, 102.0, 1e-12);
    ASSERT_NEAR(t.bids[0].px, 100.5, 1e-12);
    ASSERT_NEAR(t.bids[2].sz, 9.0, 1e-12);
    ASSERT_EQ(t.bids.size(), size_t{3});
    ASSERT_EQ(b.nonce(), int64_t{11});
}

TEST(a_nonce_gap_invalidates_until_the_next_snapshot) {
    lighter::Book b;
    b.apply(snap(1000, 10));
    ASSERT(b.apply(delta(1001, 12, 13, "[]", "[]")) == lighter::Book::Apply::Gap);   // begin 12 ≠ nonce 10
    ASSERT(!b.valid());
    ASSERT(b.apply(delta(1002, 13, 14, "[]", "[]")) == lighter::Book::Apply::Gap);   // stays untrusted
    ASSERT(b.apply(snap(1003, 20)) == lighter::Book::Apply::Snapshot);
    ASSERT(b.valid());
}

TEST(synthetic_snapshot_round_trips) {
    lighter::Book a;
    a.apply(snap(1000, 10));
    a.apply(delta(1001, 10, 11, "[]", R"([{"price":"100.5","size":"1.25"}])"));
    lighter::Book b;
    ASSERT(b.apply(a.snapshot_message(27)) == lighter::Book::Apply::Snapshot);
    ASSERT_EQ(b.nonce(), a.nonce());
    hl::Book x = a.top(10), y = b.top(10);
    ASSERT_EQ(x.bids.size(), y.bids.size());
    for (size_t i = 0; i < x.bids.size(); ++i) { ASSERT_NEAR(x.bids[i].px, y.bids[i].px, 1e-9); ASSERT_NEAR(x.bids[i].sz, y.bids[i].sz, 1e-9); }
    ASSERT(b.apply(delta(1002, 11, 12, "[]", "[]")) == lighter::Book::Apply::Delta);   // the live stream continues from it
}

// Day roll: the recorder applies the update that crossed midnight, writes the snapshot (which already contains it),
// then archives the update again. Replaying the new day file must skip that repeat, not call it a gap.
TEST(day_roll_repeat_of_the_last_update_is_not_a_gap) {
    lighter::Book live;
    live.apply(snap(1000, 10));
    live.apply(delta(1001, 10, 11, "[]", R"([{"price":"100.5","size":"1.0"}])"));
    lighter::Book replay;
    ASSERT(replay.apply(live.snapshot_message(27)) == lighter::Book::Apply::Snapshot);
    ASSERT(replay.apply(delta(1001, 10, 11, "[]", R"([{"price":"100.5","size":"1.0"}])")) == lighter::Book::Apply::Ignored);
    ASSERT(replay.valid());
    ASSERT(replay.apply(delta(1002, 11, 12, "[]", "[]")) == lighter::Book::Apply::Delta);
    ASSERT(replay.apply(delta(1003, 14, 15, "[]", "[]")) == lighter::Book::Apply::Gap);   // a real gap still is one
}

TEST(trades_skip_the_replay_and_map_the_aggressor) {
    auto replay = json::parse(R"({"channel":"trade:27","type":"subscribed/trade","trades":[{"price":"1","size":"1","timestamp":5,"is_maker_ask":true,"type":"trade"}]})");
    ASSERT(lighter::parse_trades(replay).empty());
    auto live = json::parse(R"({"channel":"trade:27","type":"update/trade",
        "trades":[{"price":"101.0","size":"0.5","timestamp":1700,"is_maker_ask":true,"type":"trade"},
                  {"price":"100.0","size":"0.2","timestamp":1701,"is_maker_ask":false,"type":"trade"},
                  {"price":"100.0","size":"0.2","timestamp":1702,"is_maker_ask":false,"type":"deleverage"}],
        "liquidation_trades":[{"price":"99.0","size":"1.0","timestamp":1703,"is_maker_ask":false,"type":"liquidation"}]})");
    auto t = lighter::parse_trades(live);
    ASSERT_EQ(t.size(), size_t{3});
    ASSERT(t[0].buy_aggr);                 // maker was the ask → taker bought
    ASSERT(!t[1].buy_aggr);
    ASSERT_NEAR(t[2].px, 99.0, 1e-12);     // liquidations trade against resting orders too
    ASSERT_EQ(t[0].t_ms, int64_t{1700});
}

TEST(lighter_archive_replays_through_the_shared_loader) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("test_lighter_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const int64_t at = 1792915291557000LL;
    {
        ZstdDaily z(dir / "AAVE");
        auto line = [](const json& m) { return std::string("{\"r\":1,\"m\":") + m.dump() + "}"; };
        z.write_line(line(snap(1000, 10)), at);
        z.write_line(line(delta(1001, 10, 11, "[]", R"([{"price":"100.5","size":"1.0"}])")), at);
        z.write_line(line(delta(1002, 99, 100, "[]", "[]")), at);                 // gap: no event
        z.write_line(line(json::parse(R"({"channel":"trade:27","type":"update/trade","trades":[{"price":"101","size":"1","timestamp":1001,"is_maker_ask":true,"type":"trade"}]})")), at);
    }
    auto days = hl::list_days(dir);
    ASSERT_EQ(days.size(), size_t{1});
    auto ev = hl::load_day(days[0]);
    int books = 0, trades = 0;
    for (const auto& e : ev) (e.is_book ? books : trades)++;
    ASSERT_EQ(books, 2);
    ASSERT_EQ(trades, 1);
    ASSERT(!ev[1].is_book);                // the 1001 trade sorts before the 1001 book
    fs::remove_all(dir);
}

static hl::Book bk(int64_t t, double bid, double ask) {
    hl::Book b; b.t_ms = t;
    b.bids = {{bid, 5}, {bid - 1, 50}, {bid - 2, 50}};
    b.asks = {{ask, 5}, {ask + 1, 50}, {ask + 2, 50}};
    return b;
}

TEST(halt_cancels_quotes_and_flatten_crosses_the_spread) {
    hl::Params p; p.latency_ms = 0; p.maker_fee_bps = 0; p.taker_fee_bps = 10; p.max_inv_units = 3;
    hl::Simulator s(p);
    s.on_book(bk(0, 100, 104));                 // wide market: bid 101 / ask 103
    s.on_trade({"", 1, 100.5, 1, false});       // bought ~1 order at 101
    ASSERT_EQ(s.running().fills, int64_t{1});
    s.set_halted(true, 2);
    s.on_book(bk(3, 100, 104));                  // cancels land
    ASSERT_NEAR(s.quote_px(true), 0.0, 1e-12);
    ASSERT_NEAR(s.quote_px(false), 0.0, 1e-12);
    const double qty = s.running().inv;
    const double cost = s.flatten_now(4);        // sell at the 100 bid, mid 102
    ASSERT_NEAR(s.running().inv, 0.0, 1e-12);
    ASSERT_NEAR(cost, qty * 2 + qty * 100 * 10e-4, 1e-9);
    ASSERT_EQ(s.running().flattens, int64_t{1});
    ASSERT_EQ(s.running().fills, int64_t{1});    // the exit is not a resting fill
    auto r = s.finish();
    ASSERT_NEAR(r.flat_pnl, -qty * 101 + qty * 100 - qty * 100 * 10e-4, 1e-9);
}

static mmpaper::Config cfg() {
    mmpaper::Config c; c.p = mmpaper::frozen_params(); c.p.latency_ms = 0; c.daily_loss_usd = 5; c.stale_ms = 10'000;
    c.p.min_spread_ticks = 0; c.p.imb_pull = 0; c.p.pause_move_ticks = 0; c.p.requote_ticks = 0; c.p.half_width_ticks = 0.5;
    return c;
}
static const int64_t D1 = 1792886400000LL;   // 2026-10-25 00:00:00 UTC

TEST(portfolio_rolls_the_day_flat_and_reports_it) {
    mmpaper::Portfolio pf(cfg());
    std::vector<mmpaper::DayRow> rows;
    pf.on_day([&](const mmpaper::DayRow& r) { rows.push_back(r); });
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    pf.trade("AAVE", {"", D1 + 1001, 99, 1, false});           // our 100 bid swept
    ASSERT_NEAR(pf.equity(), 100.0 / 100.5 * 0.5, 1e-9);        // bought at 100, mid 100.5
    pf.book("AAVE", bk(D1 + 86'400'000 + 5, 100, 101));         // next UTC day
    ASSERT_EQ(rows.size(), size_t{1});
    ASSERT_EQ(rows[0].day, std::string("2026-10-25"));
    ASSERT_EQ(rows[0].fills, int64_t{1});
    ASSERT_EQ(rows[0].flattens, int64_t{1});
    ASSERT_EQ(pf.day(), std::string("2026-10-26"));
    ASSERT_NEAR(pf.gross_usd(), 0.0, 1e-9);                     // fresh, flat simulators
}

TEST(portfolio_loss_stop_flattens_and_halts_until_tomorrow) {
    mmpaper::Portfolio pf(cfg());
    std::vector<std::string> kinds;
    pf.on_event([&](int64_t, const std::string& k, const std::string&, const std::string&) { kinds.push_back(k); });
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    pf.trade("AAVE", {"", D1 + 1001, 99, 1, false});           // long ~1 coin at 100
    pf.book("AAVE", bk(D1 + 2000, 90, 91));                     // −10% → equity ≈ −9.5
    pf.tick(D1 + 2001, false);
    ASSERT(pf.halted());
    ASSERT(std::find(kinds.begin(), kinds.end(), "loss_stop") != kinds.end());
    ASSERT_NEAR(pf.gross_usd(), 0.0, 1e-9);
    pf.trade("AAVE", {"", D1 + 3000, 80, 10, false});           // halted: no new fills
    ASSERT_EQ(pf.snapshot()[0].fills, int64_t{1});
    pf.book("AAVE", bk(D1 + 86'400'000 + 1, 90, 91));           // new day releases it
    ASSERT(!pf.halted());
}

TEST(portfolio_per_market_params_override) {
    mmpaper::Config c = cfg();
    hl::Params fine = c.p; fine.venue_tick = 0.01; fine.half_width_ticks = 2.5;      // one market on a 0.01 grid
    c.per_market["FINE"] = fine;
    mmpaper::Portfolio pf(c);
    pf.book("FINE", bk(D1 + 1000, 100, 101));
    pf.book("COARSE", bk(D1 + 1000, 100, 101));
    pf.trade("FINE", {"", D1 + 1001, 100.3, 1, false});      // through a 100.47 bid, not through a 100 bid
    pf.trade("COARSE", {"", D1 + 1001, 100.3, 1, false});
    int64_t fine_fills = 0, coarse_fills = 0;
    for (const auto& s : pf.snapshot()) (s.market == "FINE" ? fine_fills : coarse_fills) = s.fills;
    ASSERT_EQ(fine_fills, int64_t{1});
    ASSERT_EQ(coarse_fills, int64_t{0});
}

TEST(portfolio_stale_feed_and_kill_switch) {
    mmpaper::Portfolio pf(cfg());
    std::vector<std::string> kinds;
    pf.on_event([&](int64_t, const std::string& k, const std::string&, const std::string&) { kinds.push_back(k); });
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    pf.tick(D1 + 5'000, false, false);                         // connection silent: everything pulled
    ASSERT(pf.snapshot()[0].halted);
    ASSERT(std::find(kinds.begin(), kinds.end(), "feed_down") != kinds.end());
    pf.tick(D1 + 6'000, false, true);
    ASSERT(!pf.snapshot()[0].halted);
    pf.tick(D1 + 20'000, false);                                // 19 s without a book
    ASSERT(pf.snapshot()[0].halted);
    ASSERT(std::find(kinds.begin(), kinds.end(), "stale") != kinds.end());
    pf.book("AAVE", bk(D1 + 20'500, 100, 101));
    pf.tick(D1 + 21'000, false);
    ASSERT(!pf.snapshot()[0].halted);
    pf.tick(D1 + 22'000, true);                                 // kill switch
    ASSERT(pf.halted() && pf.snapshot()[0].halted);
    pf.book("AAVE", bk(D1 + 22'500, 100, 101));
    pf.tick(D1 + 23'000, false);                                // cleared
    ASSERT(!pf.halted() && !pf.snapshot()[0].halted);
    ASSERT(std::find(kinds.begin(), kinds.end(), "resume") != kinds.end());
}

// ── audit 2026-10-26 ─────────────────────────────────────────────────────────────────────────────────

static hl::Book bkq(int64_t t, double bid_sz) {          // bid 100 (size bid_sz) / ask 101
    hl::Book b; b.t_ms = t;
    b.bids = {{100, bid_sz}, {99, 50}, {98, 50}};
    b.asks = {{101, 5}, {102, 50}, {103, 50}};
    return b;
}

// 1. A book stamped at the trade's time that ARRIVES before the trade already shows the traded size gone. Applied
// first, it shrinks the queue ahead to 4 and the 6-lot trade then "fills" us — a phantom the backtest (trades
// before books, hl_events.hpp) never sees. Through the re-order buffer the live path gets the backtest's 0.
TEST(reorder_buffer_matches_backtest_order_no_phantom_fill) {
    const int64_t R = D1 + 50;                                  // receive clock (wall), unrelated to exchange time
    const hl::Book b1 = bkq(D1 + 1000, 10), b2 = bkq(D1 + 2000, 4);
    const hl::Trade tr{"", D1 + 2000, 100, 6, false};           // seller hits our 100 bid level for 6

    mmpaper::Portfolio live(cfg());
    live.push_book("X", b1, R);
    ASSERT_EQ(live.pump(R + 100), size_t{0});                  // held: not 250 ms old yet
    ASSERT_EQ(live.pump(R + 250), size_t{1});
    live.push_book("X", b2, R + 1010);                          // the book arrives first …
    live.pump(R + 1010);
    live.push_trade("X", tr, R + 1050);                         // … the trade 40 ms later
    live.pump(R + 1050);
    ASSERT_EQ(live.sequencer().pending(), size_t{2});
    live.pump(R + 1300);                                        // book's hold expired: trade released first
    ASSERT_EQ(live.sequencer().pending(), size_t{0});
    ASSERT_EQ(live.snapshot()[0].fills, int64_t{0});            // 10 ahead, 6 traded → no fill

    mmpaper::Portfolio arrival(cfg());                          // the old live path: arrival order
    arrival.book("X", b1); arrival.book("X", b2); arrival.trade("X", tr);
    ASSERT(arrival.snapshot()[0].fills == 1);                   // the phantom the audit found

    std::vector<hl::Event> ev(3);                               // the backtest loader's order on the same events
    ev[0].t_ms = b1.t_ms; ev[0].is_book = true; ev[0].book = b1; ev[0].seq = 0;
    ev[1].t_ms = b2.t_ms; ev[1].is_book = true; ev[1].book = b2; ev[1].seq = 1;
    ev[2].t_ms = tr.t_ms; ev[2].trade = tr; ev[2].seq = 2;
    hl::sort_events(ev);
    hl::Simulator s(cfg().p);
    hl::run_events(ev, s);
    ASSERT_EQ(s.running().fills, int64_t{0});

    // A trade older than a book already released (it arrived > 250 ms late) is still applied, and counted.
    live.push_trade("X", {"", D1 + 1500, 100, 1, false}, R + 5000);
    live.pump(R + 5300);
    ASSERT_EQ(live.sequencer().late_trades(), int64_t{1});
}

// 2. Halting must run on the exchange clock. With the wall clock an hour ahead of the feed's timestamps, a halt used
// to schedule the cancel an hour into the exchange's future, so a later trade still filled the "pulled" quote; and the
// stale check compared the wall clock with exchange time, calling a market stale the moment it started.
TEST(halt_uses_exchange_time_and_stale_uses_receive_time) {
    mmpaper::Config c = cfg(); c.p.latency_ms = 300;
    mmpaper::Portfolio pf(c);
    std::vector<std::string> kinds;
    pf.on_event([&](int64_t, const std::string& k, const std::string&, const std::string&) { kinds.push_back(k); });
    const int64_t W = D1 + 3'600'000 + 1000;                    // wall clock: an hour ahead of exchange time
    pf.book("A", bk(D1 + 1000, 100, 104), W);                    // quotes 101 / 103, active at +300 ms
    pf.tick(W + 1000, false, false);                            // feed down → halt; cancels land at exchange +300 ms
    ASSERT(pf.snapshot()[0].halted);
    ASSERT(std::find(kinds.begin(), kinds.end(), "stale") == kinds.end());   // 1 s since RECEIVE: not stale
    pf.trade("A", {"", D1 + 2000, 100, 5, false});              // through 101, but the bid was cancelled at +300 ms
    ASSERT_EQ(pf.snapshot()[0].fills, int64_t{0});
    pf.tick(W + 1000 + c.stale_ms + 1, false, true);            // stale is still caught — on receive time
    ASSERT(std::find(kinds.begin(), kinds.end(), "stale") != kinds.end());
}

// 3. After a kill (or loss-stop) flatten, an order whose cancel was still in flight fills: once the cancel latency has
// passed, the remainder is flattened again.
TEST(halt_flatten_rechecks_inventory_after_cancel_latency) {
    mmpaper::Config c = cfg(); c.p.latency_ms = 300;
    mmpaper::Portfolio pf(c);
    std::vector<std::string> kinds;
    pf.on_event([&](int64_t, const std::string& k, const std::string&, const std::string&) { kinds.push_back(k); });
    const int64_t t0 = D1 + 1000;
    pf.book("A", bk(t0, 100, 101));                              // bid 100, live at t0+300
    pf.book("A", bk(t0 + 400, 100, 101));                        // bid now resting in the book
    pf.tick(t0 + 450, true);                                    // kill: flat (nothing held), cancel lands at t0+700
    ASSERT(pf.halted());
    pf.trade("A", {"", t0 + 500, 99, 1, false});                // the in-flight bid is swept before its cancel lands
    ASSERT(pf.inventory("A") > 0.5);
    ASSERT(std::find(kinds.begin(), kinds.end(), "reflatten") == kinds.end());
    pf.book("A", bk(t0 + 800, 100, 101));                        // cancel latency passed → re-checked and flattened
    ASSERT_NEAR(pf.inventory("A"), 0.0, 1e-12);
    ASSERT(std::find(kinds.begin(), kinds.end(), "reflatten") != kinds.end());
    pf.trade("A", {"", t0 + 900, 99, 1, false});                // nothing left to fill
    ASSERT_NEAR(pf.inventory("A"), 0.0, 1e-12);
}

// 4. DB write path: a failing statement drops only itself (logged + counted), a connection lost mid-batch is retried
// from the statement in flight, and the retry of a statement that had in fact committed does not double count.
TEST(db_batch_per_statement_retry_is_idempotent) {
    std::vector<mmpaper::Stmt> batch;
    for (int i = 0; i < 5; ++i) batch.push_back({"INSERT", {"u" + std::to_string(i)}});
    std::set<std::string> table;                                // ON CONFLICT (uid) DO NOTHING
    bool lost_once = false;
    auto exec = [&](const std::string&, const std::vector<std::string>& p) -> mmpaper::ExecResult {
        if (p[0] == "u1") return {mmpaper::ExecStatus::Error, "bad value"};
        table.insert(p[0]);
        if (p[0] == "u3" && !lost_once) { lost_once = true; return {mmpaper::ExecStatus::ConnLost, "server closed"}; }   // committed, then lost
        return {};
    };
    auto o = mmpaper::run_batch(batch, exec);
    ASSERT(o.conn_lost);
    ASSERT_EQ(o.resume_at, size_t{3});
    ASSERT_EQ(o.failed.size(), size_t{1});
    ASSERT_EQ(o.failed[0].first, size_t{1});
    ASSERT_EQ(o.applied, size_t{2});                            // u0, u2 — u4 not reached before the loss
    std::vector<mmpaper::Stmt> rest(batch.begin() + o.resume_at, batch.end());
    auto o2 = mmpaper::run_batch(rest, exec);                   // reconnect + retry from the statement in flight
    ASSERT(!o2.conn_lost);
    ASSERT_EQ(o2.applied, size_t{2});
    ASSERT_EQ(table.size(), size_t{4});                         // u0 u2 u3 u4 once each; u1 dropped

    namespace fs = std::filesystem;
    const fs::path f = fs::temp_directory_path() / ("test_drops_" + std::to_string(::getpid()) + ".json");
    fs::remove(f);
    mmpaper::DropLedger a;
    ASSERT(!a.load(f));
    a.start("2026-10-26");
    a.add("2026-10-26", o.failed.size());
    a.add("2026-10-27", 3);
    ASSERT(a.save(f));
    mmpaper::DropLedger b;                                      // a restart: the evidence survives
    ASSERT(b.load(f));
    b.start("2026-10-28");                                      // an existing ledger keeps its first day
    ASSERT_EQ(b.since(), std::string("2026-10-26"));
    ASSERT_EQ(b.total(), int64_t{4});
    ASSERT_EQ(b.on("2026-10-26"), int64_t{1});
    ASSERT_EQ(b.on("2026-10-27"), int64_t{3});
    ASSERT_EQ(b.on("2026-10-28"), int64_t{0});
    { std::ofstream(f) << "{not json"; }
    mmpaper::DropLedger c2;
    ASSERT(!c2.load(f));                                        // unreadable → a new ledger (earlier days unknown)
    fs::remove(f);
}

// 5. A fill stamped 23:59:59.9 that is released from the re-order buffer after WALL-clock midnight belongs to day D:
// the wall clock must not roll the day while such an event can still be waiting in the buffer.
TEST(day_roll_waits_for_buffered_events_of_the_old_day) {
    mmpaper::Portfolio pf(cfg());
    std::vector<mmpaper::DayRow> rows;
    pf.on_day([&](const mmpaper::DayRow& r) { rows.push_back(r); });
    const int64_t D2 = D1 + 86'400'000;
    pf.book("AAVE", bk(D2 - 1000, 100, 101), D2 - 1000);           // 23:59:59.0, our bid at 100
    pf.push_trade("AAVE", {"", D2 - 100, 99, 1, false}, D2 + 100);  // 23:59:59.9 sweep, received 00:00:00.1
    pf.tick(D2 + 200, false);                                       // wall clock past midnight, trade still held
    ASSERT(rows.empty());
    ASSERT_EQ(pf.day(), std::string("2026-10-25"));
    pf.pump(D2 + 350);                                              // released: fills the day-D simulator
    ASSERT_EQ(pf.snapshot()[0].fills, int64_t{1});
    pf.tick(D2 + 3'000, false);                                     // now the day rolls
    ASSERT_EQ(rows.size(), size_t{1});
    ASSERT_EQ(rows[0].day, std::string("2026-10-25"));
    ASSERT_EQ(rows[0].fills, int64_t{1});
    ASSERT_EQ(pf.day(), std::string("2026-10-26"));
}

// 6. Drops are counted against the row's own UTC day; the DDL runs one statement at a time.
TEST(db_drop_day_and_ddl_split) {
    mmpaper::Stmt with_day{"INSERT", {}, "2026-10-25"}, without{"INSERT", {}};
    ASSERT_EQ(mmpaper::drop_day(with_day, "2026-10-26"), std::string("2026-10-25"));
    ASSERT_EQ(mmpaper::drop_day(without, "2026-10-26"), std::string("2026-10-26"));
    const auto st = mmpaper::split_sql("\nCREATE TABLE a (x INT);\n  ALTER TABLE a ADD COLUMN y INT;\n\n");
    ASSERT_EQ(st.size(), size_t{2});
    ASSERT_EQ(st[0], std::string("CREATE TABLE a (x INT)"));
    ASSERT_EQ(st[1], std::string("ALTER TABLE a ADD COLUMN y INT"));
}

// ── observability 2026-10-26: quote rows (change detection, 1 s limit, 60 s keepalive) and status.json fields ──
static mmpaper::QuoteState qs(double bid, double ask, hl::QuoteReason br = hl::Quoting, hl::QuoteReason ar = hl::Quoting) {
    mmpaper::QuoteState q; q.mid = 100.5; q.best_bid = 100; q.best_ask = 101; q.bid_px = bid; q.ask_px = ask;
    q.bid_reason = br; q.ask_reason = ar; return q;
}

TEST(quote_rows_on_change_at_most_once_per_second_plus_keepalive) {
    mmpaper::QuoteTracker t;
    ASSERT(t.observe("A", 1000, qs(100, 101)).has_value());                      // first state: written
    ASSERT(!t.observe("A", 1200, qs(100, 101)).has_value());                     // unchanged: nothing
    ASSERT(!t.observe("A", 1500, qs(99, 101)).has_value());                      // changed inside 1 s: deferred
    ASSERT_EQ(t.deferred(), int64_t{1});
    auto r = t.observe("A", 2000, qs(99, 101));                                  // 1 s later (the tick): written
    ASSERT(r.has_value() && !r->keepalive);
    ASSERT_EQ(r->ts_ms, int64_t{2000});
    ASSERT_NEAR(r->q.bid_px, 99.0, 1e-12);
    ASSERT(!t.observe("A", 2500, qs(99, 101)).has_value());
    ASSERT(t.observe("A", 3100, qs(0, 101, hl::InvCap)).has_value());     // a reason change after 1 s: written
    ASSERT(!t.observe("A", 3200, qs(0, 101, hl::Quoting)).has_value());          // flip inside 1 s: deferred …
    ASSERT(!t.observe("A", 3900, qs(0, 101, hl::InvCap)).has_value());           // … and reverted: never written
    ASSERT(!t.observe("A", 4200, qs(0, 101, hl::InvCap)).has_value());           // matches the last row: nothing
    ASSERT(!t.observe("A", 63'000, qs(0, 101, hl::InvCap)).has_value());         // < 60 s since the last row
    r = t.observe("A", 63'100, qs(0, 101, hl::InvCap));                         // 60 s: keepalive
    ASSERT(r.has_value() && r->keepalive);
    ASSERT(!t.observe("A", 63'050, qs(1, 2)).has_value());                       // never back in time (primary key)
    ASSERT(t.observe("B", 63'050, qs(1, 2)).has_value());                        // per market
    ASSERT_EQ(t.rows(), int64_t{5});
    ASSERT_EQ(t.keepalives(), int64_t{1});
}

TEST(quote_rows_follow_the_portfolio_through_on_applied) {
    mmpaper::Portfolio pf(cfg());
    mmpaper::QuoteTracker t;
    std::vector<mmpaper::QuoteTracker::Row> rows;
    pf.on_applied([&](const std::string& m, int64_t ts) { if (auto r = t.observe(m, ts, pf.quote_state(m))) rows.push_back(*r); });
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    ASSERT_EQ(rows.size(), size_t{1});
    ASSERT_NEAR(rows[0].q.bid_px, 100.0, 1e-9);
    ASSERT_NEAR(rows[0].q.ask_px, 101.0, 1e-9);
    ASSERT_EQ(rows[0].q.bid_reason, hl::Quoting);
    ASSERT_NEAR(rows[0].q.best_bid, 100.0, 1e-12);
    ASSERT_NEAR(rows[0].q.mid, 100.5, 1e-12);
    pf.trade("AAVE", {"", D1 + 2500, 99, 1, false});                             // bid filled: bid_px → none
    ASSERT_EQ(rows.size(), size_t{2});
    ASSERT_NEAR(rows[1].q.bid_px, 0.0, 1e-12);
    ASSERT(rows[1].q.inv > 0 && rows[1].q.inv_usd > 0);
    pf.tick(D1 + 3000, true);                                                    // kill: halted
    ASSERT_EQ(pf.quote_state("AAVE").bid_reason, hl::Halted);
    ASSERT_EQ(pf.quote_state("AAVE").ask_reason, hl::Halted);
    ASSERT_EQ(pf.quote_state("NOPE").bid_reason, hl::NoBook);
}

TEST(status_json_markets_reasons_and_components) {
    mmpaper::Portfolio pf(cfg());
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    pf.book("AAVE", bk(D1 + 1001, 100, 101));
    const auto st = pf.market_status();
    ASSERT_EQ(st.size(), size_t{1});
    const auto& m = st[0];
    for (const char* k : {"market", "equity", "inv", "fills", "halted", "bid_px", "ask_px", "bid_reason", "ask_reason", "reasons_bid", "reasons_ask",
                          "mid", "best_bid", "best_ask", "venue_tick", "tick_rule", "hl_sz_decimals"})
        ASSERT(m.contains(k));
    ASSERT_NEAR(m["bid_px"].get<double>(), 100.0, 1e-9);
    ASSERT_EQ(m["bid_reason"].get<std::string>(), std::string("quoting"));
    ASSERT_EQ(m["reasons_bid"].size(), size_t{hl::N_QUOTE_REASON});
    ASSERT_EQ(m["reasons_bid"]["quoting"].get<int64_t>(), int64_t{2});
    ASSERT_EQ(m["reasons_ask"]["kept"].get<int64_t>(), int64_t{0});
    pf.tick(D1 + 2000, true);
    pf.book("AAVE", bk(D1 + 2100, 100, 101));                                   // the cancel lands with the next event
    ASSERT(pf.market_status()[0]["bid_px"].is_null());                          // pulled: null, not 0
    ASSERT_EQ(pf.market_status()[0]["bid_reason"].get<std::string>(), std::string("halted"));

    mmpaper::Health h;
    h.now_ms = 100'000; h.started_ms = 0; h.connected = true; h.last_msg_ms = 99'000; h.markets = 4; h.halted_markets = 1;
    h.db_enabled = true; h.db_last_commit_ms = 95'000; h.daily_loss_limit = 150; h.worst_market_equity = -3;
    auto c = mmpaper::components_json(h);
    for (const char* k : {"feed", "reorder", "sim", "db", "risk"}) { ASSERT(c.contains(k)); ASSERT(c[k]["ok"].get<bool>()); }
    for (const char* k : {"last_ms", "books", "trades", "gaps", "reconnects"}) ASSERT(c["feed"].contains(k));
    for (const char* k : {"pending", "late_trades"}) ASSERT(c["reorder"].contains(k));
    for (const char* k : {"markets", "halted"}) ASSERT(c["sim"].contains(k));
    for (const char* k : {"queue", "retries", "errors", "dropped", "last_commit_ms"}) ASSERT(c["db"].contains(k));
    for (const char* k : {"daily_loss_limit", "worst_market_equity", "kill"}) ASSERT(c["risk"].contains(k));
    h.last_msg_ms = 80'000;                         ASSERT(!mmpaper::components_json(h)["feed"]["ok"].get<bool>());
    h.connected = false; h.last_msg_ms = 99'000;    ASSERT(!mmpaper::components_json(h)["feed"]["ok"].get<bool>());
    h.connected = true; h.halted_markets = 2;       ASSERT(!mmpaper::components_json(h)["sim"]["ok"].get<bool>());   // half halted = outage
    h.halted_markets = 0; h.db_dropped_today = 1;   ASSERT(!mmpaper::components_json(h)["db"]["ok"].get<bool>());
    h.db_dropped_today = 0; h.db_queue = 5; h.db_last_commit_ms = 1'000; h.now_ms = 130'000; h.last_msg_ms = 129'000;
    ASSERT(!mmpaper::components_json(h)["db"]["ok"].get<bool>());                // rows waiting, nothing committed for 129 s
    h.db_enabled = false;                           ASSERT(mmpaper::components_json(h)["db"]["ok"].get<bool>());
    h.kill = true;
    c = mmpaper::components_json(h);
    ASSERT(!c["risk"]["ok"].get<bool>() && !c["sim"]["ok"].get<bool>() && c["risk"]["kill"].get<bool>());
    h.kill = false; h.loss_stop = true;             ASSERT(!mmpaper::components_json(h)["risk"]["ok"].get<bool>());
}

// Review fixes 2026-10-26: a call "before" the last row is not skipped silently; no-book states make no row and are not counted.
TEST(quote_tracker_clock_skew_and_no_book) {
    mmpaper::QuoteTracker t;
    mmpaper::QuoteState nb;                                                      // mid 0: no book
    ASSERT(!t.observe("A", 500, nb).has_value());
    ASSERT_EQ(t.rows(), int64_t{0});
    ASSERT(t.observe("A", 10'000, qs(100, 101)).has_value());                    // event row (exchange time)
    const int64_t d0 = t.deferred();
    ASSERT(!t.observe("A", 9'800, qs(99, 101)).has_value());                     // tick clock behind: moved to 10'001, deferred + counted
    ASSERT_EQ(t.deferred(), d0 + 1);
    auto r = t.observe("A", 11'001, qs(99, 101));
    ASSERT(r.has_value() && r->ts_ms == 11'001);
    r = t.observe("A", 71'000, qs(99, 101));                                     // keepalive from a lagging clock still lands after the last row
    ASSERT(!r.has_value());
    r = t.observe("A", 72'001, qs(99, 101));
    ASSERT(r.has_value() && r->keepalive && r->ts_ms == 72'001);
    ASSERT_EQ(t.rows(), int64_t{3});
}

TEST(quote_rows_own_queue_sheds_oldest_and_counts) {
    mmpaper::DropLedger fills_led, quote_led;
    fills_led.start("2026-10-26"); quote_led.start("2026-10-26");
    mmpaper::ShedQueue q(3);
    size_t shed = 0;
    for (int i = 0; i < 5; ++i) shed += q.push({"Q" + std::to_string(i), {}, i < 2 ? "2026-10-25" : "2026-10-26"}, quote_led, "2026-10-26");
    ASSERT_EQ(shed, size_t{2});
    ASSERT_EQ(q.size(), size_t{3});
    ASSERT_EQ(q.rows().front().sql, std::string("Q2"));                         // the oldest went
    ASSERT_EQ(quote_led.on("2026-10-25"), int64_t{2});                           // counted against the rows' own day
    ASSERT_EQ(fills_led.total(), int64_t{0});                                    // the gate's ledger never sees them
    auto batch = q.take_all();
    ASSERT(q.empty());
    q.push({"Q5", {}, "2026-10-26"}, quote_led, "2026-10-26");
    q.push({"Q6", {}, "2026-10-26"}, quote_led, "2026-10-26");
    shed = q.requeue(batch, 1, quote_led, "2026-10-26");                         // Q3,Q4 back in front of Q5,Q6; cap 3 → Q3 shed
    ASSERT_EQ(shed, size_t{1});
    ASSERT_EQ(q.rows()[0].sql, std::string("Q4"));
    ASSERT_EQ(q.rows()[1].sql, std::string("Q5"));
    ASSERT_EQ(q.rows()[2].sql, std::string("Q6"));
    ASSERT_EQ(quote_led.total(), int64_t{3});
}

TEST(run_batch_reports_on_conflict_no_rows) {
    std::vector<mmpaper::Stmt> b = {{"A", {}, ""}, {"B", {}, ""}, {"C", {}, ""}};
    auto o = mmpaper::run_batch(b, [](const std::string& sql, const std::vector<std::string>&) -> mmpaper::ExecResult {
        if (sql == "B") return {mmpaper::ExecStatus::Ok, "", 0};                 // ON CONFLICT DO NOTHING: nothing inserted
        if (sql == "C") return {mmpaper::ExecStatus::Ok, "", -1};                // unknown count: not a conflict
        return {mmpaper::ExecStatus::Ok, "", 1};
    });
    ASSERT_EQ(o.applied, size_t{3});
    ASSERT_EQ(o.no_rows.size(), size_t{1});
    ASSERT_EQ(o.no_rows[0], size_t{1});
}

TEST(status_publishes_the_engine_params) {
    mmpaper::Portfolio pf(cfg());
    pf.book("AAVE", bk(D1 + 1000, 100, 101));
    const auto p = pf.market_status()[0]["params"];
    for (const char* k : {"order_usd", "max_inv_units", "maker_fee_bps", "taker_fee_bps", "latency_ms", "place_latency_ms", "cancel_latency_ms",
                          "cancels", "half_width_ticks", "skew_ticks", "fair", "min_spread_ticks", "imb_pull", "pause_move_ticks", "pause_ms",
                          "requote_ticks", "venue_tick", "tick_rule", "hl_sz_decimals", "fills"})
        ASSERT(p.contains(k));
    ASSERT_NEAR(p["half_width_ticks"].get<double>(), cfg().p.half_width_ticks, 1e-12);
    ASSERT_EQ(p["cancels"].get<std::string>(), std::string("pessimistic"));
    mmpaper::Health h;
    h.quote_dropped_today = 7; h.db_enabled = true; h.now_ms = 1; h.started_ms = 1;
    const auto c = mmpaper::components_json(h);
    ASSERT(c["db"]["ok"].get<bool>());                                           // quote drops never fail db.ok
    ASSERT_EQ(c["db"]["quote_dropped_today"].get<int64_t>(), int64_t{7});
    for (const char* k : {"quote_queue", "quote_dropped", "quote_written", "quote_conflicts", "quote_errors"}) ASSERT(c["db"].contains(k));
}

int main() {
    RUN(snapshot_then_deltas_rebuild_the_book);
    RUN(a_nonce_gap_invalidates_until_the_next_snapshot);
    RUN(synthetic_snapshot_round_trips);
    RUN(trades_skip_the_replay_and_map_the_aggressor);
    RUN(lighter_archive_replays_through_the_shared_loader);
    RUN(halt_cancels_quotes_and_flatten_crosses_the_spread);
    RUN(portfolio_rolls_the_day_flat_and_reports_it);
    RUN(portfolio_loss_stop_flattens_and_halts_until_tomorrow);
    RUN(portfolio_per_market_params_override);
    RUN(portfolio_stale_feed_and_kill_switch);
    RUN(day_roll_repeat_of_the_last_update_is_not_a_gap);
    RUN(reorder_buffer_matches_backtest_order_no_phantom_fill);
    RUN(halt_uses_exchange_time_and_stale_uses_receive_time);
    RUN(halt_flatten_rechecks_inventory_after_cancel_latency);
    RUN(db_batch_per_statement_retry_is_idempotent);
    RUN(day_roll_waits_for_buffered_events_of_the_old_day);
    RUN(db_drop_day_and_ddl_split);
    RUN(quote_rows_on_change_at_most_once_per_second_plus_keepalive);
    RUN(quote_rows_follow_the_portfolio_through_on_applied);
    RUN(status_json_markets_reasons_and_components);
    RUN(quote_tracker_clock_skew_and_no_book);
    RUN(quote_rows_own_queue_sheds_oldest_and_counts);
    RUN(run_batch_reports_on_conflict_no_rows);
    RUN(status_publishes_the_engine_params);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
