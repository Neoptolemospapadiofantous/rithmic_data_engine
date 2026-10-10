// test_dbn.cpp — DBN reader (src/history/dbn.hpp) + MBO book/feed (src/history/mbo_feed.hpp).
// Synthetic DBN buffers (v1 and v2 metadata, plain and zstd) plus, when the owned history is on
// this box, one check against the first records of a real file.
#include "history/dbn.hpp"
#include "history/mbo_feed.hpp"
#include "history/binance_feed.hpp"
#include "history/bar_feed.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <unistd.h>
#include <string>
#include <vector>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond) do { if (cond) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static dbn::MboMsg mbo(uint64_t ts_ns, uint32_t iid, char action, char side, double price, uint32_t size,
                       uint64_t oid, uint8_t flags = dbn::kFlagLast) {
    dbn::MboMsg m{};
    m.hd.length = sizeof(dbn::MboMsg) / 4; m.hd.rtype = dbn::kMbo; m.hd.publisher_id = 1;
    m.hd.instrument_id = iid; m.hd.ts_event = ts_ns;
    m.order_id = oid; m.price = action == 'R' ? dbn::kUndefPrice : (int64_t)std::llround(price * 1e9);
    m.size = size; m.flags = flags; m.action = action; m.side = side; m.ts_recv = ts_ns + 1000;
    return m;
}
static dbn::OhlcvMsg bar(uint64_t ts_ns, double o, double h, double l, double c, uint64_t v) {
    dbn::OhlcvMsg b{};
    b.hd.length = sizeof(dbn::OhlcvMsg) / 4; b.hd.rtype = dbn::kOhlcv1m; b.hd.publisher_id = 1;
    b.hd.instrument_id = 7; b.hd.ts_event = ts_ns;
    b.open = (int64_t)std::llround(o * 1e9); b.high = (int64_t)std::llround(h * 1e9);
    b.low = (int64_t)std::llround(l * 1e9); b.close = (int64_t)std::llround(c * 1e9); b.volume = v;
    return b;
}

static void test_metadata_and_ohlcv(uint8_t version, bool compressed) {
    dbn::SymbolMapping sm; sm.raw_symbol = "NQ.c.0";
    sm.intervals.push_back({20260201, 20260301, "42002475"});
    std::vector<std::vector<uint8_t>> recs = {
        dbn::record_bytes(bar(1769904000ULL * 1000000000ULL, 25000.25, 25010.5, 24990.0, 25005.75, 1234)),
        dbn::record_bytes(bar(1769904060ULL * 1000000000ULL, 25005.75, 25006.0, 25001.0, 25002.0, 77)),
    };
    auto raw = dbn::build_dbn(version, "GLBX.MDP3", 8, {"NQ.c.0"}, {sm}, recs);
    auto buf = compressed ? dbn::zstd_compress(raw) : raw;
    dbn::Reader r(buf, compressed);
    const auto& m = r.metadata();
    CHECK(m.version == version);
    CHECK(m.dataset == "GLBX.MDP3");
    CHECK(m.schema == 8);
    CHECK(m.symbols.size() == 1 && m.symbols[0] == "NQ.c.0");
    CHECK(m.mappings.size() == 1 && m.mappings[0].intervals.size() == 1 &&
          m.mappings[0].intervals[0].symbol == "42002475" && m.mappings[0].intervals[0].start_date == 20260201);
    auto* h = r.next(); auto* o = dbn::Reader::as_ohlcv(h);
    CHECK(o != nullptr);
    CHECK(o && std::fabs(dbn::px(o->open) - 25000.25) < 1e-9 && std::fabs(dbn::px(o->close) - 25005.75) < 1e-9);
    CHECK(o && o->volume == 1234 && h->ts_event == 1769904000ULL * 1000000000ULL);
    CHECK(dbn::Reader::as_mbo(h) == nullptr);
    h = r.next(); o = dbn::Reader::as_ohlcv(h);
    CHECK(o && o->volume == 77 && std::fabs(dbn::px(o->low) - 25001.0) < 1e-9);
    CHECK(r.next() == nullptr);
    CHECK(r.records_read() == 2);
}

static void test_unknown_record_skipped() {
    // A 24-byte record of an unknown rtype between two bars must be skipped by its length byte.
    std::vector<uint8_t> unk(24, 0); unk[0] = 6; unk[1] = 0x99;
    auto raw = dbn::build_dbn(1, "GLBX.MDP3", 8, {}, {},
        {dbn::record_bytes(bar(1, 1, 1, 1, 1, 1)), unk, dbn::record_bytes(bar(2, 2, 2, 2, 2, 2))});
    dbn::Reader r(raw, false);
    // (a record pointer is valid until the next call — check each one before reading on)
    auto* a = r.next();
    CHECK(dbn::Reader::as_ohlcv(a) && a->ts_event == 1);
    auto* b = r.next();
    CHECK(b && b->rtype == 0x99 && !dbn::Reader::as_ohlcv(b) && r.current_length() == 24);
    auto* c = r.next();
    CHECK(dbn::Reader::as_ohlcv(c) && c->ts_event == 2);
    CHECK(r.next() == nullptr);
    // Truncated tail → throws, not a silent short record.
    auto cut = raw; cut.resize(cut.size() - 5);
    dbn::Reader r2(cut, false);
    bool threw = false;
    try { while (r2.next()) {} } catch (const std::exception&) { threw = true; }
    CHECK(threw);
}

static void test_book() {
    hist::MboBook b; hist::MboBook::Counters c;
    b.apply(mbo(1, 1, 'A', 'B', 100.00, 5, 1), c);
    b.apply(mbo(2, 1, 'A', 'B', 100.00, 3, 2), c);
    b.apply(mbo(3, 1, 'A', 'B', 99.75, 7, 3), c);
    b.apply(mbo(4, 1, 'A', 'A', 100.25, 4, 4), c);
    b.apply(mbo(5, 1, 'A', 'A', 100.50, 9, 5), c);
    CHECK(dbn::px(b.best_bid()) == 100.00 && b.bid_size() == 8);
    CHECK(dbn::px(b.best_ask()) == 100.25 && b.ask_size() == 4);
    CHECK(b.bid_orders() == 3 && b.ask_orders() == 2);
    // partial cancel, then full cancel empties the level
    b.apply(mbo(6, 1, 'C', 'B', 100.00, 2, 1), c);
    CHECK(b.bid_size() == 6);
    b.apply(mbo(7, 1, 'C', 'B', 100.00, 3, 1), c);
    b.apply(mbo(8, 1, 'C', 'B', 100.00, 3, 2), c);
    CHECK(dbn::px(b.best_bid()) == 99.75 && b.bid_size() == 7 && b.bid_orders() == 1);
    // modify: price move and size change
    b.apply(mbo(9, 1, 'M', 'A', 100.25, 2, 4), c);
    CHECK(b.ask_size() == 2);
    b.apply(mbo(10, 1, 'M', 'A', 100.75, 2, 4), c);
    CHECK(dbn::px(b.best_ask()) == 100.50 && b.ask_size() == 9);
    // fill then cancel = executed delete
    b.apply(mbo(11, 1, 'F', 'A', 100.50, 9, 5), c);
    b.apply(mbo(12, 1, 'C', 'A', 100.50, 9, 5), c);
    CHECK(c.executed_deletes == 1);
    CHECK(dbn::px(b.best_ask()) == 100.75);
    // unknown cancel is counted, not applied; modify of unknown order = add
    b.apply(mbo(13, 1, 'C', 'B', 1.0, 1, 999), c);
    CHECK(c.unknown_cancel == 1);
    b.apply(mbo(14, 1, 'M', 'B', 99.50, 4, 77), c);
    CHECK(c.unknown_modify == 1 && b.bid_orders() == 2);
    uint64_t bq = 0, aq = 0; b.depth_near_mid(0.01, bq, aq);
    CHECK(bq == 11 && aq == 2);
    b.apply(mbo(15, 1, 'R', 'N', 0, 0, 0), c);
    CHECK(!b.has_bid() && !b.has_ask() && b.order_count() == 0);
}

// Feed: trades become ticks with the aggressor side, quotes only at F_LAST, post-trade book
// stamped after the trade, window filter, roll to a new instrument.
static void test_feed() {
    const uint64_t s = 1'000'000'000ULL;   // 1 s in ns
    std::vector<std::vector<uint8_t>> recs;
    auto add = [&](const dbn::MboMsg& m) { recs.push_back(dbn::record_bytes(m)); };
    add(mbo(10 * s, 1, 'R', 'N', 0, 0, 0, dbn::kFlagSnapshot));
    add(mbo(10 * s, 1, 'A', 'B', 100.00, 5, 1, dbn::kFlagSnapshot));
    add(mbo(10 * s, 1, 'A', 'A', 100.25, 3, 2, dbn::kFlagSnapshot | dbn::kFlagLast));  // snapshot ends
    // t=20s: buy aggressor takes 3 at 100.25 (T, F, C in one event)
    add(mbo(20 * s, 1, 'T', 'B', 100.25, 3, 0, 0));
    add(mbo(20 * s, 1, 'F', 'A', 100.25, 3, 2, 0));
    add(mbo(20 * s, 1, 'C', 'A', 100.25, 3, 2, 0));
    add(mbo(20 * s, 1, 'A', 'A', 100.50, 6, 3, dbn::kFlagLast));
    // t=21s: size-only change (held), then a sell trade flushes it first
    add(mbo(21 * s, 1, 'A', 'B', 100.00, 2, 4, dbn::kFlagLast));
    add(mbo(21 * s + 500000, 1, 'T', 'A', 100.00, 1, 0, dbn::kFlagLast));
    // t=30s: roll to instrument 2 — clear + snapshot of the new contract
    add(mbo(30 * s, 2, 'R', 'N', 0, 0, 0, dbn::kFlagSnapshot));
    add(mbo(30 * s, 2, 'A', 'B', 200.00, 1, 10, dbn::kFlagSnapshot));
    add(mbo(30 * s, 2, 'A', 'A', 200.50, 1, 11, dbn::kFlagSnapshot | dbn::kFlagLast));
    add(mbo(31 * s, 2, 'T', 'B', 200.50, 1, 0, dbn::kFlagLast));
    add(mbo(99 * s, 2, 'T', 'B', 201.00, 1, 0, dbn::kFlagLast));   // past the window
    auto raw = dbn::build_dbn(1, "GLBX.MDP3", 0, {"NQ.c.0"}, {}, recs);
    const std::string path = "/tmp/test_dbn_feed_" + std::to_string(::getpid()) + ".dbn.zst";
    { auto z = dbn::zstd_compress(raw); FILE* f = std::fopen(path.c_str(), "wb"); std::fwrite(z.data(), 1, z.size(), f); std::fclose(f); }

    std::vector<hist::MinuteRow> mins;
    hist::MboFeed feed({path}, 5'000'000LL, 50'000'000LL);
    feed.set_minute_sink([&](const hist::MinuteRow& m) { mins.push_back(m); });
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes;
    while (feed.next_batch(ticks, quotes, 1)) {}
    std::remove(path.c_str());

    CHECK(ticks.size() == 3);
    if (ticks.size() == 3) {
        CHECK(ticks[0].ts_us == 20'000'000 && ticks[0].price == 100.25 && ticks[0].size == 3 && ticks[0].is_buy);
        CHECK(ticks[1].price == 100.00 && !ticks[1].is_buy);
        CHECK(ticks[2].price == 200.50 && ticks[2].is_buy);
    }
    // quotes: snapshot top (10s), post-trade book (stamped after the 20s trade), held size change
    // flushed before the 21.5s trade, new contract's book after the roll.
    CHECK(quotes.size() == 4);
    if (quotes.size() == 4) {
        CHECK(quotes[0].ts_us == 10'000'000 && quotes[0].bid == 100.00 && quotes[0].ask == 100.25 && quotes[0].ask_sz == 3);
        CHECK(quotes[1].ts_us == 20'000'001 && quotes[1].ask == 100.50 && quotes[1].ask_sz == 6);
        CHECK(quotes[2].ts_us == 21'000'000 && quotes[2].bid_sz == 7 && quotes[2].ts_us <= ticks[1].ts_us);
        CHECK(quotes[3].bid == 200.00 && quotes[3].ask == 200.50);
    }
    CHECK(feed.stats().instrument_switches == 1);
    CHECK(feed.stats().trades == 3);   // the 99 s trade is past to_us: stream stops before it
    // minute rows: everything sits in minute 0 here (0..59 s), first minute flagged incomplete
    CHECK(mins.size() == 1);
    if (!mins.empty()) {
        CHECK(mins[0].trades == 3 && mins[0].volume == 5 && mins[0].buy_volume == 4);
        CHECK(!mins[0].complete);
        CHECK(mins[0].quotes == 4);
    }
}

// A file that starts mid-session (no clear/snapshot first) gives ticks but no quotes until its first 'R'.
static void test_unsynced_start() {
    const uint64_t s = 1'000'000'000ULL;
    std::vector<std::vector<uint8_t>> recs;
    auto add = [&](const dbn::MboMsg& m) { recs.push_back(dbn::record_bytes(m)); };
    add(mbo(10 * s, 1, 'A', 'B', 100.00, 5, 1));
    add(mbo(10 * s, 1, 'A', 'A', 100.25, 3, 2));
    add(mbo(11 * s, 1, 'T', 'B', 100.25, 1, 0));
    add(mbo(20 * s, 1, 'R', 'N', 0, 0, 0, dbn::kFlagSnapshot));
    add(mbo(20 * s, 1, 'A', 'B', 101.00, 1, 3, dbn::kFlagSnapshot));
    add(mbo(20 * s, 1, 'A', 'A', 101.25, 1, 4, dbn::kFlagSnapshot | dbn::kFlagLast));
    add(mbo(21 * s, 1, 'T', 'B', 101.25, 1, 0));
    auto raw = dbn::build_dbn(1, "GLBX.MDP3", 0, {}, {}, recs);
    const std::string path = "/tmp/test_dbn_unsync_" + std::to_string(::getpid()) + ".dbn";
    { FILE* f = std::fopen(path.c_str(), "wb"); std::fwrite(raw.data(), 1, raw.size(), f); std::fclose(f); }
    hist::MboFeed feed({path}, 0, 100'000'000LL);
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> q;
    while (feed.next_batch(ticks, q, 10)) {}
    std::remove(path.c_str());
    CHECK(ticks.size() == 2);
    CHECK(q.size() == 1 && q[0].bid == 101.00 && q[0].ts_us >= 20'000'000);
    CHECK(feed.stats().unsynced_records == 3);
}

// Binance aggTrades rows: crypto_backfill.sh semantics (size × 1000, is_buy = NOT maker, ms → µs).
static void test_aggtrades() {
    hist::Tick t{}; uint64_t id = 0;
    CHECK(!hist::parse_agg_line("agg_trade_id,price,quantity,first_trade_id,last_trade_id,transact_time,is_buyer_maker\n", t));
    CHECK(hist::parse_agg_line("2834513012,62123.40,0.015,5100,5102,1727740800123,false\n", t, &id));
    CHECK(id == 2834513012ULL && t.ts_us == 1727740800123000LL && t.price == 62123.40 && t.size == 15 && t.is_buy);
    CHECK(hist::parse_agg_line("7,2500.01,1.2345,1,1,1727740800124,true", t));
    CHECK(t.size == 1235 && !t.is_buy);
    CHECK(hist::parse_agg_line("8,1,0.001,1,1,1727740800124567,True", t) && t.ts_us == 1727740800124567LL);   // µs stamps
    CHECK(!hist::parse_agg_line("9,1,0.001,1,1,1727740800124,maybe", t));
    const std::string path = "/tmp/test_dbn_agg_" + std::to_string(::getpid()) + ".csv";
    { FILE* f = std::fopen(path.c_str(), "w");
      std::fputs("agg_trade_id,price,quantity,first_trade_id,last_trade_id,transact_time,is_buyer_maker\n"
                 "1,100.0,0.010,1,1,1000,false\n2,101.0,0.020,2,2,2000,true\n3,99.5,0.003,3,3,1500,false\n"
                 "4,98.0,0.001,4,4,9000,false\n", f); std::fclose(f); }
    hist::AggTradesFeed feed({path}, 1'000'000LL, 5'000'000LL);   // (1 s, 5 s] in µs → rows 2 and 3
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> q;
    while (feed.next_batch(ticks, q, 1)) {}
    std::remove(path.c_str());
    CHECK(ticks.size() == 2);
    if (ticks.size() == 2) {
        CHECK(ticks[0].ts_us == 2'000'000 && ticks[0].size == 20 && !ticks[0].is_buy);
        CHECK(ticks[1].ts_us == 2'000'000 && ticks[1].price == 99.5);   // out-of-order stamp clamped forward
    }
    CHECK(q.empty());
}

// OHLCV-1m bars → 4 synthetic prints (bar_feed.hpp): path rule, sizes, aggressor side, window, duplicate minutes.
static void test_bar_feed() {
    auto up = hist::bar_to_ticks(60'000'000, 100.0, 105.0, 98.0, 104.0, 10, 99.0);   // close ≥ open: O, L, H, C
    CHECK(up[0].price == 100.0 && up[1].price == 98.0 && up[2].price == 105.0 && up[3].price == 104.0);
    CHECK(up[0].ts_us == 60'100'000 && up[1].ts_us == 80'000'000 && up[2].ts_us == 100'000'000 && up[3].ts_us == 119'900'000);
    CHECK(up[0].size + up[1].size + up[2].size + up[3].size == 10 && up[3].size == 4);
    CHECK(up[0].is_buy && !up[1].is_buy && up[2].is_buy && !up[3].is_buy);
    auto dn = hist::bar_to_ticks(0, 100.0, 101.0, 95.0, 96.0, 2, 0.0);               // close < open: O, H, L, C
    CHECK(dn[1].price == 101.0 && dn[2].price == 95.0 && dn[0].size == 1 && dn[3].size == 1);

    std::vector<std::vector<uint8_t>> recs = {
        dbn::record_bytes(bar(60ULL * 1000000000ULL, 100, 101, 99, 100.5, 8)),
        dbn::record_bytes(bar(60ULL * 1000000000ULL, 500, 500, 500, 500, 8)),        // same minute (roll overlap): dropped
        dbn::record_bytes(bar(120ULL * 1000000000ULL, 100.5, 102, 100, 101, 8)),
        dbn::record_bytes(bar(180ULL * 1000000000ULL, 101, 101, 101, 101, 8)),       // after the window
    };
    auto raw = dbn::build_dbn(1, "GLBX.MDP3", 8, {"NQ.c.0"}, {}, recs);
    const std::string path = "/tmp/test_dbn_bars_" + std::to_string(::getpid()) + ".dbn.zst";
    { auto z = dbn::zstd_compress(raw); FILE* fp = std::fopen(path.c_str(), "wb"); std::fwrite(z.data(), 1, z.size(), fp); std::fclose(fp); }
    hist::OhlcvBarFeed feed({path}, 60'000'000LL, 180'000'000LL);   // (60 s, 180 s]
    std::vector<hist::Tick> ticks; std::deque<hist::Quote> q;
    while (feed.next_batch(ticks, q, 3)) {}
    std::remove(path.c_str());
    CHECK(ticks.size() == 8);
    bool mono = true; for (size_t i = 1; i < ticks.size(); ++i) mono = mono && ticks[i].ts_us >= ticks[i - 1].ts_us;
    CHECK(mono);
    CHECK(!ticks.empty() && ticks.front().price == 100.0 && ticks.back().price == 101.0);
    bool no_dup = true; for (auto& t : ticks) no_dup = no_dup && t.price != 500.0;
    CHECK(no_dup);
    CHECK(q.empty());
    CHECK(feed.summary().find("1 duplicate minutes") != std::string::npos);
}

// Real file check (skipped when the owned history is not on this box).
static void test_real_file() {
    const std::string f = "/home/theone/Desktop/bot/data/raw/ohlcv-1m/2010-07.dbn.zst";
    if (!std::filesystem::exists(f)) { std::printf("SKIP real-file check (%s not present)\n", f.c_str()); return; }
    dbn::Reader r(f);
    CHECK(r.metadata().version == 1);
    CHECK(r.metadata().dataset == "GLBX.MDP3");
    CHECK(!r.metadata().symbols.empty() && r.metadata().symbols[0] == "NQ.c.0");
    auto* h = r.next(); auto* o = dbn::Reader::as_ohlcv(h);
    CHECK(o && h->rtype == dbn::kOhlcv1m);
    // First bar of the file (cross-checked with the databento Python package, 2026-10-10):
    // 2010-07-01 00:00 UTC, instrument 26715.
    CHECK(o && h->ts_event == 1277942400000000000ULL);
    CHECK(o && h->instrument_id == 26715);
    CHECK(o && o->low <= o->open && o->open <= o->high && o->low <= o->close && o->close <= o->high);
}

int main() {
    test_metadata_and_ohlcv(1, false);
    test_metadata_and_ohlcv(1, true);
    test_metadata_and_ohlcv(2, true);
    test_unknown_record_skipped();
    test_book();
    test_feed();
    test_unsynced_start();
    test_aggtrades();
    test_bar_feed();
    test_real_file();
    std::printf("test_dbn: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
