/*  test_hl_mm.cpp — Hyperliquid parsing, queue-aware market-making simulator, and the zstd
    archive round trip (src/dex/hl_mm.hpp, src/zstd_daily.hpp). No network, no DB.          */
#include <fstream>
#include "dex/hl_events.hpp"
#include "zstd_daily.hpp"

#include <unistd.h>

#include <cmath>
#include <filesystem>
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

using namespace hl;

static Book book(int64_t t, double bid, double bsz, double ask, double asz) {
    Book b; b.coin = "X"; b.t_ms = t;
    b.bids = {{bid, bsz}, {bid - 1, 50}};
    b.asks = {{ask, asz}, {ask + 1, 50}};
    return b;
}
static Trade sell(int64_t t, double px, double sz) { return {"X", t, px, sz, false}; }
static Trade buy(int64_t t, double px, double sz)  { return {"X", t, px, sz, true}; }
static Params params(int64_t lat = 0, CancelModel cm = CancelModel::Pessimistic) {
    Params p; p.order_usd = 100; p.max_inv_units = 3; p.maker_fee_bps = 1.5; p.taker_fee_bps = 4.5;
    p.latency_ms = lat; p.cancels = cm; return p;
}

TEST(parse_book_and_trades) {
    auto m = json::parse(R"({"channel":"l2Book","data":{"coin":"AAVE","time":1792915291557,
        "levels":[[{"px":"136.68","sz":"0.8","n":2},{"px":"136.67","sz":"2.93","n":1}],
                  [{"px":"136.70","sz":"1.5","n":1}]]}})");
    auto b = parse_book(m);
    ASSERT(b.has_value());
    ASSERT_EQ(b->coin, std::string("AAVE"));
    ASSERT_EQ(b->t_ms, int64_t{1792915291557});
    ASSERT_EQ(b->bids.size(), size_t{2});
    ASSERT_NEAR(b->bids[0].px, 136.68, 1e-9);
    ASSERT_NEAR(b->asks[0].sz, 1.5, 1e-9);
    ASSERT(b->valid());
    ASSERT_NEAR(b->size_at(true, 136.67), 2.93, 1e-9);
    ASSERT_NEAR(b->size_at(false, 136.69), 0.0, 1e-12);
    ASSERT_EQ(coin_of(m), std::string("AAVE"));

    auto t = json::parse(R"({"channel":"trades","data":[{"coin":"AAVE","side":"A","px":"136.68","sz":"0.08",
        "time":1792915297838},{"coin":"AAVE","side":"B","px":"136.70","sz":"0.5","time":1792915297839}]})");
    auto tr = parse_trades(t);
    ASSERT_EQ(tr.size(), size_t{2});
    ASSERT(!tr[0].buy_aggr);
    ASSERT(tr[1].buy_aggr);
    ASSERT_NEAR(tr[1].sz, 0.5, 1e-12);
    ASSERT_EQ(coin_of(t), std::string("AAVE"));
    ASSERT(!parse_book(t).has_value());
    ASSERT(parse_trades(m).empty());
}

TEST(back_of_queue_fills_only_after_queue_ahead) {
    Simulator s(params());
    s.on_book(book(1000, 100, 5, 101, 5));   // quotes at 100 / 101, live at t=1000 (0 latency)
    s.on_trade(sell(1001, 100, 3));          // queue ahead 5 → 2, no fill
    ASSERT_EQ(s.running().fills, int64_t{0});
    s.on_trade(sell(1002, 100, 3));          // 2 eat the queue, 1 left ≥ our ~0.995
    ASSERT_EQ(s.running().fills, int64_t{1});
    ASSERT_NEAR(s.running().inv, 100.0 / 100.5, 1e-9);
}

TEST(trade_through_fills_regardless_of_queue) {   // Legacy behaviour (every backtest before 2026-10-26)
    Params lp = params(); lp.fills = FillModel::Legacy;
    Simulator s(lp);
    s.on_book(book(1000, 100, 500, 101, 500));
    s.on_trade(sell(1001, 99, 0.01));        // printed below our bid: we were swept
    ASSERT_EQ(s.running().fills, int64_t{1});
    ASSERT_NEAR(s.running().inv, 100.0 / 100.5, 1e-9);
    s.on_trade(buy(1002, 102, 0.01));        // above our ask: ask filled too
    ASSERT_EQ(s.running().fills, int64_t{2});
    ASSERT_NEAR(s.running().inv, 0.0, 1e-9);
}

TEST(latency_order_not_live_until_it_arrives) {
    Simulator s(params(500));
    s.on_book(book(1000, 100, 5, 101, 5));
    s.on_trade(sell(1100, 99, 10));          // before t=1500: order still in flight
    ASSERT_EQ(s.running().fills, int64_t{0});
    s.on_trade(sell(1600, 99, 10));
    ASSERT_EQ(s.running().fills, int64_t{1});
}

TEST(cancel_model_bounds_the_queue) {
    for (int opt = 0; opt < 2; ++opt) {
        Simulator s(params(0, opt ? CancelModel::Optimistic : CancelModel::Pessimistic));
        s.on_book(book(1000, 100, 10, 101, 10));
        s.on_trade(sell(1001, 100, 0.0001));      // activates the order: queue ahead 10 (minus a crumb)
        s.on_book(book(1002, 100, 20, 101, 10));  // 10 joined behind us
        s.on_book(book(1003, 100, 10, 101, 10));  // 10 cancelled — from behind (pess) or pro rata (opt)
        s.on_trade(sell(1004, 100, 6));
        if (opt) ASSERT_EQ(s.running().fills, int64_t{1});     // queue ≈5 → 1 left over fills us
        else     ASSERT_EQ(s.running().fills, int64_t{0});     // queue ≈10 → still waiting
    }
}

TEST(inventory_cap_stops_the_bid) {
    Params p = params(); p.max_inv_units = 1;
    Simulator s(p);
    s.on_book(book(1000, 100, 1, 101, 1));
    s.on_trade(sell(1001, 99, 1));            // bid filled: inventory at the cap
    ASSERT_EQ(s.running().fills, int64_t{1});
    s.on_book(book(1002, 99, 1, 100, 1));     // no new bid allowed; ask 101 → cancel requested
    s.on_book(book(1003, 99, 1, 100, 1));     // cancel landed, ask requoted at 100
    s.on_trade(sell(1004, 98, 5));            // would hit a bid if one existed
    ASSERT_EQ(s.running().fills, int64_t{1});
    s.on_trade(buy(1005, 101, 5));            // ask fills → back near flat ($100 sized at each mid)
    ASSERT_EQ(s.running().fills, int64_t{2});
    ASSERT_NEAR(s.running().inv, 100.0 / 100.5 - 100.0 / 99.5, 1e-9);
}

TEST(post_only_rejects_an_order_that_would_cross_on_arrival) {
    Simulator s(params(500));
    s.on_book(book(1000, 100, 5, 101, 5));    // bid 100 in flight until 1500
    s.on_book(book(1400, 99, 5, 100, 5));     // market dropped BEFORE it arrived: our 100 bid crosses the ask
    s.on_book(book(1600, 99, 5, 100, 5));
    ASSERT_EQ(s.running().rejected_cross, int64_t{1});
    ASSERT_EQ(s.running().fills, int64_t{0});
}

TEST(requote_follows_the_best_price) {
    Simulator s(params());
    s.on_book(book(1000, 100, 5, 101, 5));
    s.on_book(book(1001, 100.5, 5, 101, 5));  // best bid moved up: cancel requested
    s.on_book(book(1002, 100.5, 5, 101, 5));  // cancel landed, new bid placed at 100.5
    s.on_trade(sell(1003, 100.4, 1));         // through the NEW bid
    ASSERT_EQ(s.running().fills, int64_t{1});
    ASSERT_EQ(s.running().quotes_placed, int64_t{3});   // bid, ask, new bid
}

TEST(capture_markout_fees_and_pnl) {
    Params p = params(); p.max_inv_units = 1; p.fills = FillModel::Full;
    Simulator s(p);
    s.on_book(book(0, 100, 1, 101, 1));       // mid 100.5
    s.on_trade(sell(1, 99.9, 1));             // bid filled at 100
    for (int64_t t = 2; t <= 70'000; t += 1000) s.on_book(book(t, 99, 1, 100, 1));   // mid 99.5
    Result r = s.finish();
    const double qty = 100.0 / 100.5, usd = qty * 100;
    ASSERT_EQ(r.fills, int64_t{1});
    ASSERT_NEAR(r.capture_bps_sum / r.volume_usd, 50.0, 1e-6);          // (100.5−100)/100
    ASSERT_NEAR(r.mk10_bps_sum / r.mk_weight, -50.0, 1e-6);             // (99.5−100)/100
    ASSERT_NEAR(r.fees, usd * 1.5e-4, 1e-9);
    const double mtm = -usd - usd * 1.5e-4 + qty * 99.5;
    ASSERT_NEAR(r.mtm_pnl, mtm, 1e-9);
    // the close sells at the best bid (99), half a point under the mid, then pays the taker fee on the mid value
    ASSERT_NEAR(r.flat_pnl, mtm - qty * 99.5 * 4.5e-4 - qty * 0.5, 1e-9);
    ASSERT(r.max_dd > 0);
}

TEST(markout_curve_and_minute_equity_samples) {
    Params p = params(); p.max_inv_units = 1;
    Simulator s(p);
    s.on_book(book(0, 100, 1, 101, 1));                    // mid 100.5
    s.on_trade(sell(1, 99.9, 1));                          // bought at 100
    for (int64_t t = 1000; t <= 180'000; t += 1000)        // mid steps down 1.0 at t = 20 s
        s.on_book(t < 20'000 ? book(t, 100, 1, 101, 1) : book(t, 99, 1, 100, 1));
    Result r = s.finish();
    const double w = r.mk_weight;
    ASSERT_NEAR(r.mk_curve_sum[0] / w, 50.0, 1e-6);         // 1 s: mid 100.5
    ASSERT_NEAR(r.mk_curve_sum[2] / w, 50.0, 1e-6);         // 10 s: still 100.5
    ASSERT_NEAR(r.mk_curve_sum[3] / w, -50.0, 1e-6);        // 30 s: 99.5
    ASSERT_NEAR(r.mk_curve_sum[4] / w, -50.0, 1e-6);        // 60 s
    // one sample per minute while running (t=0, 60 s, 120 s, 180 s) + the final mark
    ASSERT_EQ(r.equity.size(), size_t{5});
    ASSERT_EQ(r.equity[1].first, int64_t{60'000});
    ASSERT_EQ(r.equity.back().first, int64_t{180'000});
    const double qty = 100.0 / 100.5;
    ASSERT_NEAR(r.equity.back().second, -qty * 100 - qty * 100 * 1.5e-4 + qty * 99.5, 1e-9);
}

static Book wide(int64_t t, double bid, double bsz, double ask, double asz) {   // 4-tick spread, tick 1
    Book b; b.coin = "X"; b.t_ms = t;
    b.bids = {{bid, bsz}, {bid - 1, 50}, {bid - 2, 50}};
    b.asks = {{ask, asz}, {ask + 1, 50}, {ask + 2, 50}};
    return b;
}

TEST(tick_and_microprice) {
    Book b = wide(0, 100, 9, 104, 1);
    ASSERT_NEAR(b.tick(), 1.0, 1e-12);
    ASSERT_NEAR(b.mid(), 102.0, 1e-12);
    ASSERT_NEAR(b.microprice(), (100 * 1 + 104 * 9) / 10.0, 1e-12);   // leans to the thin ask
    Book c = book(0, 100, 5, 100.5, 5);                                 // spread below the level gap
    ASSERT_NEAR(c.tick(), 0.5, 1e-12);
}

TEST(default_width_joins_a_one_tick_market) {
    Simulator s(params());
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 101.0, 1e-9);
}

TEST(small_width_quotes_inside_a_wide_spread_first_in_queue) {
    Simulator s(params());
    s.on_book(wide(1000, 100, 5, 104, 5));                 // mid 102 → bid 101, ask 103
    ASSERT_NEAR(s.quote_px(true), 101.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 103.0, 1e-9);
    s.on_trade(sell(1001, 101, 0.5));                      // nobody ahead of us at 101
    ASSERT_EQ(s.running().fills, int64_t{1});
}

TEST(wider_width_quotes_behind_the_best_price) {
    Params p = params(); p.half_width_ticks = 2.5;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));                 // mid 100.5 → bid 98, ask 103
    ASSERT_NEAR(s.quote_px(true), 98.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 103.0, 1e-9);
    s.on_trade(sell(1001, 99, 100));                       // above our bid: no fill
    ASSERT_EQ(s.running().fills, int64_t{0});
    s.on_trade(sell(1002, 97.5, 1));                       // through it
    ASSERT_EQ(s.running().fills, int64_t{1});
}

TEST(skew_leans_quotes_against_inventory) {
    Params p = params(); p.skew_ticks = 1;
    Simulator s(p);
    s.on_book(wide(1000, 100, 5, 104, 5));                 // bid 101 / ask 103
    s.on_trade(sell(1001, 100.5, 1));                      // bought ~1 order-size at 101
    ASSERT_EQ(s.running().fills, int64_t{1});
    s.on_book(wide(1002, 100, 5, 104, 5));                 // centre 102 − 1 tick → bid 100, ask cancel requested
    s.on_book(wide(1003, 100, 5, 104, 5));                 // ask requoted at 102
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 102.0, 1e-9);
}

TEST(microprice_fair_value_shifts_both_quotes) {
    Params p = params(); p.fair = Fair::Micro;
    Simulator s(p);
    s.on_book(wide(1000, 100, 9, 104, 1));                 // micro 103.6 → bid 103, ask 105
    ASSERT_NEAR(s.quote_px(true), 103.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 105.0, 1e-9);
}

TEST(tardis_rows_parse) {
    Book b;
    ASSERT(!parse_tardis_book("exchange,symbol,timestamp,local_timestamp,asks[0].price,asks[0].amount,bids[0].price,bids[0].amount", b));
    ASSERT(parse_tardis_book("hyperliquid,AAVE,1790812799495000,1790812801040846,159.52,2.51,159.51,17.72,159.53,2.62,159.5,4.6,,,,", b));
    ASSERT_EQ(b.t_ms, int64_t{1790812799495});
    ASSERT_EQ(b.asks.size(), size_t{2});
    ASSERT_EQ(b.bids.size(), size_t{2});
    ASSERT_NEAR(b.bids[1].px, 159.5, 1e-12);
    ASSERT_NEAR(b.asks[0].sz, 2.51, 1e-12);
    Trade t;
    ASSERT(parse_tardis_trade("hyperliquid,AAVE,1790812800597000,1790812802721957,552701971398474,sell,159.51,1.34", t));
    ASSERT(!t.buy_aggr);
    ASSERT_EQ(t.t_ms, int64_t{1790812800597});
    ASSERT(parse_tardis_trade("hyperliquid,AAVE,1,2,3,buy,1,1", t));
    ASSERT(t.buy_aggr);
}

TEST(trades_sort_before_the_book_of_the_same_block) {
    std::vector<Event> ev(3);
    ev[0].t_ms = 10; ev[0].is_book = true; ev[0].seq = 0;    // book of block 10 arrived first
    ev[1].t_ms = 10; ev[1].is_book = false; ev[1].seq = 1;   // its trade
    ev[2].t_ms = 5;  ev[2].is_book = true; ev[2].seq = 2;
    sort_events(ev);
    ASSERT_EQ(ev[0].t_ms, int64_t{5});
    ASSERT(!ev[1].is_book);
    ASSERT(ev[2].is_book);
}

TEST(queue_deeper_than_the_visible_book_is_assumed_not_empty) {
    Params p = params(); p.half_width_ticks = 3.5;         // bid 3 ticks below best: 97, book shows 100 and 99
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_NEAR(s.quote_px(true), 97.0, 1e-9);
    s.on_trade(sell(1001, 97, 30));                        // activates: queue = deepest visible size (50)
    ASSERT_EQ(s.running().fills, int64_t{0});
    s.on_book(book(1002, 100, 5, 101, 5));                 // 97 still not shown: queue must NOT drop to 0
    s.on_trade(sell(1003, 97, 10));
    ASSERT_EQ(s.running().fills, int64_t{0});              // 50 − 30 − 10 = 10 still ahead
    s.on_trade(sell(1004, 97, 11));
    ASSERT_EQ(s.running().fills, int64_t{1});
}

TEST(spread_filter_quotes_only_wide_markets) {
    Params p = params(); p.min_spread_ticks = 2;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));                 // 1 tick: no quotes
    ASSERT_EQ(s.running().quotes_placed, int64_t{0});
    s.on_book(wide(1001, 100, 5, 104, 5));                 // 4 ticks: quote
    ASSERT_EQ(s.running().quotes_placed, int64_t{2});
}

TEST(imbalance_pulls_the_side_about_to_be_run_over) {
    Params p = params(); p.imb_pull = 0.6;
    Simulator s(p);
    s.on_book(book(1000, 100, 9, 101, 1));                 // bids heavy (+0.8): no ask
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 0.0, 1e-12);
    Simulator t(p);
    t.on_book(book(1000, 100, 1, 101, 9));                 // asks heavy: no bid
    ASSERT_NEAR(t.quote_px(true), 0.0, 1e-12);
    ASSERT_NEAR(t.quote_px(false), 101.0, 1e-9);
}

TEST(pause_after_a_jump_pulls_both_quotes) {
    Params p = params(); p.pause_move_ticks = 2; p.pause_ms = 1000;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_EQ(s.running().quotes_placed, int64_t{2});
    s.on_book(book(1100, 103, 5, 104, 5));                 // mid +3 ticks: cancels requested, nothing new
    s.on_book(book(1200, 103, 5, 104, 5));                 // cancels landed, still paused
    ASSERT_NEAR(s.quote_px(true), 0.0, 1e-12);
    ASSERT_NEAR(s.quote_px(false), 0.0, 1e-12);
    s.on_book(book(2200, 103, 5, 104, 5));                 // pause over
    ASSERT_NEAR(s.quote_px(true), 103.0, 1e-9);
}

TEST(requote_hysteresis_keeps_queue_place) {
    Params p = params(); p.requote_ticks = 1;
    p.fills = FillModel::NoCross;   // the 1001 book's bid sits on our ask: Full would fill it (crossed_book_* tests)
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));
    s.on_book(book(1000, 100, 5, 101, 5));                 // both orders land in the book
    s.on_book(book(1001, 101, 5, 102, 5));                 // target moved 1 tick: keep the old orders
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    ASSERT_EQ(s.running().quotes_placed, int64_t{2});
    s.on_book(book(1002, 102, 5, 103, 5));                 // 2 ticks: requote
    s.on_book(book(1003, 102, 5, 103, 5));
    ASSERT_NEAR(s.quote_px(true), 102.0, 1e-9);
}

static Book fine(int64_t t, double bid, double ask) {        // Lighter-like: 0.001 grid at ~$135, sparse levels
    Book b; b.coin = "X"; b.t_ms = t;
    b.bids = {{bid, 5}, {bid - 0.001, 3}, {bid - 0.004, 9}};
    b.asks = {{ask, 5}, {ask + 0.001, 3}, {ask + 0.007, 9}};
    return b;
}

TEST(hyperliquid_tick_rule_keeps_distances_in_hl_ticks_on_a_finer_grid) {
    Params p = params(); p.strat_tick_rule = TickRule::Hyperliquid; p.hl_sz_decimals = 2; p.min_spread_ticks = 4; p.half_width_ticks = 5;
    Simulator s(p);
    s.on_book(fine(1000, 135.200, 135.244));          // spread 0.044 = 44 venue ticks = 4.4 HL ticks (0.01) → allowed
    // HL tick at $135 is 0.01: half width 5 → 0.05 off the mid 135.222; rounded to the 0.001 grid
    ASSERT_NEAR(s.quote_px(true), 135.172, 1e-9);
    ASSERT_NEAR(s.quote_px(false), 135.272, 1e-9);
    Simulator t(p);
    t.on_book(fine(1000, 135.200, 135.230));          // spread 3.0 HL ticks < 4 → no quotes (30 venue ticks would have passed)
    ASSERT_EQ(t.running().quotes_placed, int64_t{0});
    Params q = params(); q.min_spread_ticks = 4; q.half_width_ticks = 5;   // Book rule on the same book: 44 ticks pass, width 5 × 0.001
    Simulator u(q);
    u.on_book(fine(1000, 135.200, 135.244));
    ASSERT_NEAR(u.quote_px(true), 135.217, 1e-9);
}

TEST(hl_tick_rule_matches_hyperliquid_including_the_decimals_cap) {
    Params p = params(); p.strat_tick_rule = TickRule::Hyperliquid;
    p.hl_sz_decimals = 0;                               // kPEPE-like: cap 1e-6 wins over the 1e-7 of 5 sig figs
    Simulator s(p);
    Book b; b.t_ms = 1; b.bids = {{0.004366, 1}, {0.004365, 1}}; b.asks = {{0.004367, 1}, {0.004368, 1}};
    s.on_book(b);                                       // width 0.5 × 1e-6 → joins best bid/ask (post-only clip)
    ASSERT_NEAR(s.quote_px(true), 0.004366, 1e-12);
    Params q = params(); q.strat_tick_rule = TickRule::Hyperliquid; q.hl_sz_decimals = 2; q.half_width_ticks = 2.5;
    Simulator t(q);                                     // EIGEN-like $0.1978: 5 sig figs give 1e-5, cap gives 1e-4 → 1e-4
    Book c; c.t_ms = 1; c.bids = {{0.1978, 1}, {0.1977, 1}}; c.asks = {{0.1979, 1}, {0.1980, 1}};
    t.on_book(c);                                       // mid 0.19785 − 2.5e-4 = 0.19760
    ASSERT_NEAR(t.quote_px(true), 0.1976, 1e-9);
}

TEST(venue_tick_param_overrides_book_inference) {
    Params p = params(); p.venue_tick = 0.01; p.half_width_ticks = 2.5;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));             // book infers 1.0; venue says 0.01 → 2.5 ticks = 0.025, on the 0.01 grid
    ASSERT_NEAR(s.quote_px(true), 100.47, 1e-9);        // floor(100.475) on the grid
    ASSERT_NEAR(s.quote_px(false), 100.53, 1e-9);       // ceil(100.525)
}

TEST(no_fill_before_first_book) {
    Simulator s(params());
    s.on_trade(sell(1, 1, 100));
    ASSERT_EQ(s.running().fills, int64_t{0});
    ASSERT_EQ(s.running().quotes_placed, int64_t{0});
}

TEST(zstd_archive_round_trip_including_an_unfinished_file) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("test_hl_mm_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const int64_t at = 1792915291557000LL;
    {
        ZstdDaily z(dir);
        z.write_line(R"({"r":1,"m":{"a":1}})", at);
        z.write_line(R"({"r":2,"m":{"a":2}})", at);
        z.flush();                                   // file still open: last frame unfinished
        ZstdLines in(z.current());
        std::string l; int n = 0;
        while (in.next(l)) ++n;
        ASSERT_EQ(n, 2);
        ASSERT(!in.corrupt());
        z.write_line(R"({"r":3,"m":{"a":3}})", at);
    }                                                // closed: frame ended
    {
        ZstdDaily z(dir);                            // reopened after a "restart": a second frame
        z.write_line(R"({"r":4,"m":{"a":4}})", at);
    }
    ZstdLines in(dir / (ZstdDaily::utc_date(at) + ".jsonl.zst"));
    std::string l, last; int n = 0;
    while (in.next(l)) { ++n; last = l; }
    ASSERT_EQ(n, 4);
    ASSERT_EQ(last, std::string(R"({"r":4,"m":{"a":4}})"));
    fs::remove_all(dir);
}

// A recorder killed mid-frame leaves a truncated frame, and the next run appends a new frame after it. The reader must
// keep everything before the damage AND everything after it (until 2026-10-26 it stopped at the damage).
TEST(zstd_reader_skips_a_truncated_frame_and_keeps_reading) {
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("test_hl_mm_trunc_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    const int64_t at = 1792915291557000LL;
    const fs::path f = dir / (ZstdDaily::utc_date(at) + ".jsonl.zst");
    std::string big(4000, 'x');
    { ZstdDaily z(dir); z.write_line(R"({"r":1})", at); z.write_line(R"({"r":2})", at); }    // frame 1, complete
    std::string frame2;
    { ZstdDaily z(dir); for (int i = 0; i < 200; ++i) z.write_line("{\"r\":" + std::to_string(10 + i) + ",\"p\":\"" + big + "\"}", at); }
    {   // cut frame 2 in half: what a kill -9 mid-write leaves
        const auto sz = fs::file_size(f);
        std::ifstream in(f, std::ios::binary); std::string all((std::istreambuf_iterator<char>(in)), {});
        const size_t f1 = all.find("\x28\xB5\x2F\xFD", 4);                     // start of frame 2
        ASSERT(f1 != std::string::npos && f1 < sz);
        all.resize(f1 + (sz - f1) / 2);
        std::ofstream out(f, std::ios::binary | std::ios::trunc); out << all;
    }
    { ZstdDaily z(dir); z.write_line(R"({"r":900})", at); z.write_line(R"({"r":901})", at); }  // frame 3 after the damage
    ZstdLines in(f);
    std::string l, last; int n = 0, before = 0;
    while (in.next(l)) {
        ++n; last = l;
        ASSERT(!l.empty() && l.front() == '{' && l.back() == '}');                // never a half line
        if (l.find("\"r\":9") == std::string::npos) ++before;
    }
    ASSERT(in.corrupt());
    ASSERT_EQ(in.skipped_frames(), 1);
    ASSERT_EQ(last, std::string(R"({"r":901})"));
    ASSERT(before >= 2);                                                          // frame 1 + the readable part of 2
    ASSERT_EQ(n - before, 2);                                                     // frame 3 fully read
    fs::remove_all(dir);
}

// ── fill model (audit 2026-10-26) ──────────────────────────────────────────
// Wide book 100 / 104 (tick 1 from the level spacing), half width 0.5 → our bid at 101, ask at 103: alone inside.
static Params inside(FillModel f, int64_t lat = 0) { Params p = params(lat); p.fills = f; return p; }

TEST(through_fill_alone_inside_spread_capped_by_trade_size) {
    for (FillModel f : {FillModel::Legacy, FillModel::NoCross, FillModel::Full}) {
        Simulator s(inside(f));
        s.on_book(book(1000, 100, 5, 104, 5));
        ASSERT_NEAR(s.quote_px(true), 101.0, 1e-9);
        s.on_trade(sell(1001, 100, 0.1));             // a 0.1 seller hit the real best bid below us
        ASSERT_NEAR(s.running().inv, f == FillModel::Legacy ? 100.0 / 102 : 0.1, 1e-9);   // size = $100 at the mid
    }
}

TEST(trade_through_visible_level_also_capped_by_trade_size) {
    for (FillModel f : {FillModel::NoCross, FillModel::Full}) {
        Params p = params(); p.fills = f;
        Simulator s(p);
        s.on_book(book(1000, 100, 500, 101, 500));    // we join the 500 resting at 100
        s.on_trade(sell(1001, 99, 0.01));             // a 0.01 print below us: only 0.01 is known to have reached us
        ASSERT_NEAR(s.running().inv, 0.01, 1e-9);
    }
}

TEST(crossed_book_fills_resting_bid_only_in_full) {
    for (FillModel f : {FillModel::Legacy, FillModel::NoCross, FillModel::Full}) {
        Simulator s(inside(f));
        s.on_book(book(1000, 100, 5, 104, 5));        // bid 101 rests
        s.on_book(book(1001, 100, 5, 101, 0.3));      // a seller now rests AT our bid: price-time priority → us
        ASSERT_NEAR(s.running().inv, f == FillModel::Full ? 0.3 : 0.0, 1e-9);
    }
}

TEST(arrival_checked_against_book_in_force_when_it_arrived) {
    for (FillModel f : {FillModel::Legacy, FillModel::NoCross, FillModel::Full}) {
        Simulator s(inside(f, 500));
        s.on_book(book(1000, 100, 5, 104, 5));        // bid 101 decided, arrives at 1500
        s.on_book(book(1400, 100, 5, 104, 5));
        s.on_book(book(1600, 100, 5, 101, 0.3));      // market ran over our price after we arrived
        if (f == FillModel::Legacy) {                 // judged against the 1600 book: rejected as crossing
            ASSERT_EQ(s.running().rejected_cross, int64_t{1});
            ASSERT_NEAR(s.running().inv, 0.0, 1e-9);
        } else {
            ASSERT_EQ(s.running().rejected_cross, int64_t{0});
            ASSERT_NEAR(s.running().inv, f == FillModel::Full ? 0.3 : 0.0, 1e-9);
        }
    }
}

TEST(final_close_crosses_the_spread) {
    for (FillModel f : {FillModel::Legacy, FillModel::NoCross}) {
        Params p = inside(f); p.taker_fee_bps = 0; p.maker_fee_bps = 0;
        Simulator s(p);
        s.on_book(book(1000, 100, 5, 104, 5));
        s.on_trade(sell(1001, 101, 100));             // at our price, nothing ahead: full fill at 101
        const Result r = s.finish();                  // long ~0.99 at mid 102, best bid 100
        const double inv = r.inv;
        ASSERT(inv > 0.9);
        ASSERT_NEAR(r.mtm_pnl, inv * (102 - 101), 1e-9);
        ASSERT_NEAR(r.flat_pnl, f == FillModel::Legacy ? r.mtm_pnl : inv * (100 - 101), 1e-9);
    }
}

// ── quote reasons (observability, 2026-10-26): each rule reports its own reason, counted per requote decision ──
static int64_t cnt(const Simulator& s, bool bid, QuoteReason r) { return s.reason_counts(bid)[r]; }

TEST(reason_no_book_before_first_book_then_quoting) {
    Simulator s(params());
    ASSERT_EQ(s.reason(true), NoBook);
    ASSERT_EQ(s.reason(false), NoBook);
    ASSERT_EQ(std::string(reason_name(NoBook)), std::string("no_book"));
    ASSERT_EQ(std::string(reason_name(Quoting)), std::string("quoting"));
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_EQ(s.reason(true), Quoting);
    ASSERT_EQ(s.reason(false), Quoting);
    ASSERT_EQ(cnt(s, true, Quoting), int64_t{1});
    ASSERT_EQ(cnt(s, false, Quoting), int64_t{1});
    s.on_book(book(1001, 100, 6, 101, 6));
    ASSERT_EQ(cnt(s, true, Quoting), int64_t{2});
}

TEST(reason_halted) {
    Simulator s(params());
    s.on_book(book(1000, 100, 5, 101, 5));
    s.set_halted(true, 1000);
    ASSERT_EQ(s.reason(true), Halted);
    ASSERT_EQ(s.reason(false), Halted);
    ASSERT_EQ(cnt(s, true, Halted), int64_t{1});
    ASSERT_EQ(std::string(reason_name(Halted)), std::string("halted"));
    s.set_halted(false, 1000);
    ASSERT_EQ(s.reason(true), Quoting);
}

TEST(reason_spread_narrow) {
    Params p = params(); p.min_spread_ticks = 4;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));         // spread 1 tick < 4
    ASSERT_EQ(s.reason(true), SpreadNarrow);
    ASSERT_EQ(s.reason(false), SpreadNarrow);
    ASSERT_NEAR(s.quote_px(true), 0.0, 1e-12);
    ASSERT_EQ(std::string(reason_name(SpreadNarrow)), std::string("spread_narrow"));
}

TEST(reason_jump_pause) {
    Params p = params(); p.pause_move_ticks = 2; p.pause_ms = 1000;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_EQ(s.reason(true), Quoting);
    s.on_book(book(1100, 105, 5, 106, 5));         // mid jumped 5 ticks
    ASSERT_EQ(s.reason(true), JumpPause);
    ASSERT_EQ(s.reason(false), JumpPause);
    s.on_book(book(2200, 105, 5, 106, 5));         // pause over
    ASSERT_EQ(s.reason(true), Quoting);
    ASSERT_EQ(cnt(s, true, JumpPause), int64_t{1});
    ASSERT_EQ(std::string(reason_name(JumpPause)), std::string("jump_pause"));
}

TEST(reason_imbalance_pull_only_the_side_about_to_be_run_over) {
    Params p = params(); p.imb_pull = 0.6;
    Simulator s(p);
    s.on_book(book(1000, 100, 90, 101, 10));       // heavy bids: imbalance 0.8 → the ask is pulled
    ASSERT_EQ(s.reason(true), Quoting);
    ASSERT_EQ(s.reason(false), ImbalancePull);
    s.on_book(book(1001, 100, 10, 101, 90));       // heavy asks → the bid is pulled
    ASSERT_EQ(s.reason(true), ImbalancePull);
    ASSERT_EQ(s.reason(false), Quoting);
    ASSERT_EQ(std::string(reason_name(ImbalancePull)), std::string("imbalance_pull"));
}

TEST(reason_inventory_cap) {
    Params p = params(); p.max_inv_units = 1;
    Simulator s(p);
    s.on_book(book(1000, 100, 0.5, 101, 5));
    s.on_trade(sell(1001, 99, 5));                 // through our bid: filled, long one unit
    s.on_book(book(1002, 100, 5, 101, 5));
    ASSERT(s.running().inv > 0);
    ASSERT_EQ(s.reason(true), InvCap);
    ASSERT_EQ(s.reason(false), Quoting);
    ASSERT_EQ(std::string(reason_name(InvCap)), std::string("inv_cap"));
}

TEST(reason_kept_by_requote_hysteresis) {
    Params p = params(); p.requote_ticks = 1;
    Simulator s(p);
    s.on_book(book(1000, 100, 5, 101, 5));
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    s.on_book(book(1001, 101, 5, 102, 5));         // target moved 1 tick: order kept at its old price
    ASSERT_EQ(s.reason(true), Kept);
    ASSERT_EQ(s.reason(false), Kept);
    ASSERT_NEAR(s.quote_px(true), 100.0, 1e-9);
    s.on_book(book(1002, 103, 5, 104, 5));         // moved 3 ticks: cancel sent, new order follows
    ASSERT_EQ(s.reason(true), Quoting);
    ASSERT_EQ(cnt(s, true, Kept), int64_t{1});
    ASSERT_EQ(std::string(reason_name(Kept)), std::string("kept"));
}

TEST(reason_priority_halted_beats_every_other_rule) {
    Params p = params(); p.min_spread_ticks = 4; p.imb_pull = 0.6;
    Simulator s(p);
    s.set_halted(true, 0);
    s.on_book(book(1000, 100, 90, 101, 10));
    ASSERT_EQ(s.reason(true), Halted);
    ASSERT_EQ(s.reason(false), Halted);
}

int main() {
    RUN(parse_book_and_trades);
    RUN(back_of_queue_fills_only_after_queue_ahead);
    RUN(trade_through_fills_regardless_of_queue);
    RUN(latency_order_not_live_until_it_arrives);
    RUN(cancel_model_bounds_the_queue);
    RUN(inventory_cap_stops_the_bid);
    RUN(post_only_rejects_an_order_that_would_cross_on_arrival);
    RUN(requote_follows_the_best_price);
    RUN(capture_markout_fees_and_pnl);
    RUN(markout_curve_and_minute_equity_samples);
    RUN(tick_and_microprice);
    RUN(default_width_joins_a_one_tick_market);
    RUN(small_width_quotes_inside_a_wide_spread_first_in_queue);
    RUN(wider_width_quotes_behind_the_best_price);
    RUN(skew_leans_quotes_against_inventory);
    RUN(microprice_fair_value_shifts_both_quotes);
    RUN(tardis_rows_parse);
    RUN(trades_sort_before_the_book_of_the_same_block);
    RUN(queue_deeper_than_the_visible_book_is_assumed_not_empty);
    RUN(spread_filter_quotes_only_wide_markets);
    RUN(imbalance_pulls_the_side_about_to_be_run_over);
    RUN(pause_after_a_jump_pulls_both_quotes);
    RUN(requote_hysteresis_keeps_queue_place);
    RUN(hyperliquid_tick_rule_keeps_distances_in_hl_ticks_on_a_finer_grid);
    RUN(hl_tick_rule_matches_hyperliquid_including_the_decimals_cap);
    RUN(venue_tick_param_overrides_book_inference);
    RUN(no_fill_before_first_book);
    RUN(zstd_archive_round_trip_including_an_unfinished_file);
    RUN(zstd_reader_skips_a_truncated_frame_and_keeps_reading);
    RUN(through_fill_alone_inside_spread_capped_by_trade_size);
    RUN(trade_through_visible_level_also_capped_by_trade_size);
    RUN(crossed_book_fills_resting_bid_only_in_full);
    RUN(arrival_checked_against_book_in_force_when_it_arrived);
    RUN(final_close_crosses_the_spread);
    RUN(reason_no_book_before_first_book_then_quoting);
    RUN(reason_halted);
    RUN(reason_spread_narrow);
    RUN(reason_jump_pause);
    RUN(reason_imbalance_pull_only_the_side_about_to_be_run_over);
    RUN(reason_inventory_cap);
    RUN(reason_kept_by_requote_hysteresis);
    RUN(reason_priority_halted_beats_every_other_rule);
    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
