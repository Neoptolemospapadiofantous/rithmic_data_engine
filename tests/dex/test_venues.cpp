/*  test_venues.cpp — Extended / Aster / Paradex message parsing (src/dex/venue_feeds.hpp): books from snapshots
    and deltas, sequence gaps, trade sides, the Paradex SBE decoder on hand-built frames, and the archive loader's
    venue dispatch (src/dex/hl_events.hpp). No network, no DB.                                                    */
#include "dex/hl_events.hpp"

#include <unistd.h>

#include <cmath>
#include <cstring>
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

using json = nlohmann::json;
namespace ext = venue::extended;
namespace ast = venue::aster;
namespace pdx = venue::paradex;

// ── Extended ─────────────────────────────────────────────────────────────────────────────────────────────
static json ext_snap(int64_t ts, int64_t seq) {
    return json::parse(R"({"type":"SNAPSHOT","data":{"t":"SNAPSHOT","m":"AAVE-USD","b":[{"q":"3.0","p":"100"},{"q":"4.0","p":"99"}],"a":[{"q":"2.0","p":"101"},{"q":"5.0","p":"102"}]},"ts":)" +
                       std::to_string(ts) + R"(,"seq":)" + std::to_string(seq) + "}");
}
static json ext_delta(int64_t ts, int64_t seq, const std::string& b, const std::string& a) {
    return json::parse(R"({"type":"DELTA","data":{"t":"DELTA","m":"AAVE-USD","b":)" + b + R"(,"a":)" + a + R"(,"d":"f"},"ts":)" + std::to_string(ts) + R"(,"seq":)" + std::to_string(seq) + "}");
}

TEST(extended_snapshot_then_deltas_use_the_absolute_size) {
    ext::Book b;
    ASSERT(b.apply(ext_snap(1000, 1)) == venue::Apply::Snapshot);
    ASSERT(b.valid());
    ASSERT_EQ(b.market(), std::string("AAVE-USD"));
    // q is the CHANGE in a delta (-1.1), c the size after it: 100 → 1.9, 99 removed (c 0), new ask 100.5 at 1.0
    ASSERT(b.apply(ext_delta(1001, 2, R"([{"q":"-1.1","p":"100","c":"1.9"},{"q":"-4.0","p":"99","c":"0"}])", R"([{"q":"1.0","p":"100.5","c":"1.0"}])")) == venue::Apply::Delta);
    hl::Book t = b.top(5);
    ASSERT_EQ(t.t_ms, int64_t{1001});
    ASSERT_EQ(t.bids.size(), size_t{1});
    ASSERT_NEAR(t.bids[0].sz, 1.9, 1e-12);
    ASSERT_NEAR(t.asks[0].px, 100.5, 1e-12);
    ASSERT_EQ(t.asks.size(), size_t{3});
    ASSERT_EQ(b.seq(), int64_t{2});
}

TEST(extended_seq_gap_invalidates_until_the_next_snapshot) {
    ext::Book b;
    b.apply(ext_snap(1000, 1));
    ASSERT(b.apply(ext_delta(1001, 3, "[]", "[]")) == venue::Apply::Gap);   // 1 → 3 skipped 2
    ASSERT(!b.valid());
    ASSERT(b.apply(ext_delta(1002, 4, "[]", "[]")) == venue::Apply::Gap);
    ASSERT(b.apply(ext_snap(1003, 7)) == venue::Apply::Snapshot);
    ASSERT(b.valid());
    ASSERT(b.apply(ext_delta(1004, 8, "[]", "[]")) == venue::Apply::Delta);
    ext::Book fresh;
    ASSERT(fresh.apply(ext_delta(1, 1, "[]", "[]")) == venue::Apply::Gap);  // a delta before any snapshot
}

// The recorder's day file opens with a snapshot taken AFTER the midnight update was applied, then archives that same
// update: replaying the new file must not call it a gap (until 2026-10-26 hours of every day were lost).
TEST(extended_day_roll_repeat_is_not_a_gap) {
    venue::extended::Book live;
    live.apply(ext_snap(1000, 1));
    live.apply(ext_delta(1001, 2, "[]", "[]"));          // the update that crossed midnight, applied first
    venue::extended::Book replay;
    ASSERT(replay.apply(live.snapshot_message()) == venue::Apply::Snapshot);
    ASSERT(replay.apply(ext_delta(1001, 2, "[]", "[]")) == venue::Apply::Ignored);
    ASSERT(replay.valid());
    ASSERT(replay.apply(ext_delta(1002, 3, "[]", "[]")) == venue::Apply::Delta);
}

TEST(extended_synthetic_snapshot_replays_to_the_same_book) {
    ext::Book b;
    b.apply(ext_snap(1000, 1));
    b.apply(ext_delta(1001, 2, R"([{"q":"1","p":"100.5","c":"1"}])", "[]"));
    ext::Book c;
    const json s = b.snapshot_message();
    ASSERT(s.value("synthetic", false));
    ASSERT(c.apply(s) == venue::Apply::Snapshot);
    hl::Book x = b.top(10), y = c.top(10);
    ASSERT_EQ(x.bids.size(), y.bids.size());
    ASSERT_NEAR(x.bids[0].px, y.bids[0].px, 1e-12);
    ASSERT_NEAR(x.asks[1].sz, y.asks[1].sz, 1e-12);
    ASSERT_EQ(c.seq(), int64_t{2});
}

TEST(extended_trades_carry_the_taker_side_and_skip_deleverage) {
    const json m = json::parse(R"({"ts":5,"data":[{"i":1,"m":"AAVE-USD","S":"SELL","tT":"TRADE","T":1792943576521,"p":"80557","q":"0.001"},
        {"i":2,"m":"AAVE-USD","S":"BUY","tT":"LIQUIDATION","T":1792943576522,"p":"80558","q":"0.5"},
        {"i":3,"m":"AAVE-USD","S":"BUY","tT":"DELEVERAGE","T":1792943576523,"p":"80559","q":"9"}],"seq":2})");
    ASSERT(ext::is_trades(m));
    ASSERT(!ext::is_book(m));
    auto tr = ext::parse_trades(m);
    ASSERT_EQ(tr.size(), size_t{2});
    ASSERT(!tr[0].buy_aggr);
    ASSERT(tr[1].buy_aggr);
    ASSERT_EQ(tr[0].t_ms, int64_t{1792943576521});
    ASSERT_EQ(tr[0].coin, std::string("AAVE"));
    ASSERT_EQ(ext::market_for("AAVE"), std::string("AAVE-USD"));
}

// ── Aster ────────────────────────────────────────────────────────────────────────────────────────────────
TEST(aster_depth20_is_a_complete_sorted_book) {
    const json m = json::parse(R"({"stream":"btcusdt@depth20@100ms","data":{"e":"depthUpdate","E":1792944122185,"T":1792944122150,"s":"BTCUSDT","U":1,"u":2,"pu":0,
        "b":[["80568.1","0.008"],["80567.8","0.001"],["80566.1","1.215"]],"a":[["80568.2","0.938"],["80569.6","0.027"]]}})");
    auto b = ast::parse_depth(m);
    ASSERT(b.has_value() && b->valid());
    ASSERT_EQ(b->t_ms, int64_t{1792944122150});          // matching-engine time, not publish time
    ASSERT_EQ(b->bids.size(), size_t{3});
    ASSERT_NEAR(b->bids[0].px, 80568.1, 1e-9);
    ASSERT_NEAR(b->asks[1].px, 80569.6, 1e-9);
    ASSERT_EQ(ast::market_of_stream(ast::stream_of(m)), std::string("BTCUSDT"));
    ASSERT_EQ(ast::market_for("aave"), std::string("AAVEUSDT"));
    ASSERT_EQ(ast::streams_target({"AAVEUSDT", "BNBUSDT"}), std::string("/stream?streams=aaveusdt@depth20@100ms/aaveusdt@aggTrade/bnbusdt@depth20@100ms/bnbusdt@aggTrade"));
    ASSERT(!ast::parse_depth(json::parse(R"({"stream":"x@aggTrade","data":{"e":"aggTrade"}})")).has_value());
}

TEST(aster_aggtrade_side_is_the_taker) {
    auto buy = ast::parse_trades(json::parse(R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E":2,"s":"BTCUSDT","a":1,"p":"100.5","q":"0.2","f":1,"l":1,"T":1792944122150,"m":false}})"));
    auto sell = ast::parse_trades(json::parse(R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E":2,"s":"BTCUSDT","a":2,"p":"100.4","q":"0.3","f":2,"l":2,"T":1792944122151,"m":true}})"));
    ASSERT_EQ(buy.size(), size_t{1});
    ASSERT(buy[0].buy_aggr);          // buyer was the taker
    ASSERT(!sell[0].buy_aggr);        // buyer was the maker → the taker sold
    ASSERT_NEAR(sell[0].sz, 0.3, 1e-12);
    ASSERT_EQ(sell[0].t_ms, int64_t{1792944122151});
}

// ── Paradex SBE ──────────────────────────────────────────────────────────────────────────────────────────
static void put_u16(std::string& s, uint16_t v) { s.append(reinterpret_cast<const char*>(&v), 2); }
static void put_i64(std::string& s, int64_t v) { s.append(reinterpret_cast<const char*>(&v), 8); }
static void put_u8(std::string& s, uint8_t v) { s.push_back(static_cast<char>(v)); }
static void put_str(std::string& s, const std::string& v) { put_u8(s, static_cast<uint8_t>(v.size())); s += v; }

// BookEvent (template 3): header + 89-byte root block + bids group + asks group + market
static std::string book_frame(int64_t ts_us, int64_t seq, bool snapshot, const std::vector<std::pair<int64_t, int64_t>>& bids,
                              const std::vector<std::pair<int64_t, int64_t>>& asks, const std::string& market) {
    std::string f;
    put_u16(f, 89); put_u16(f, 3); put_u16(f, 1); put_u16(f, 1);
    put_i64(f, ts_us); put_i64(f, seq); put_u8(f, snapshot ? 0 : 1);
    for (int i = 0; i < 9; ++i) put_i64(f, INT64_MIN);           // best-price fields, null on the non-interactive feeds
    for (const auto* g : {&bids, &asks}) {
        put_u16(f, 16); put_u16(f, static_cast<uint16_t>(g->size()));
        for (const auto& [p, s] : *g) { put_i64(f, p); put_i64(f, s); }
    }
    put_str(f, market);
    return f;
}
// TradeEvent (template 1): header + 50-byte root block + market (+ tradeIdStr in schema version 2, absent here)
static std::string trade_frame(int64_t ts_us, int64_t seq, uint8_t side, int64_t px, int64_t sz, int64_t created_us, uint8_t type, const std::string& market) {
    std::string f;
    put_u16(f, 50); put_u16(f, 1); put_u16(f, 1); put_u16(f, 1);
    put_i64(f, ts_us); put_i64(f, seq); put_i64(f, 123456789); put_u8(f, side); put_i64(f, px); put_i64(f, sz); put_i64(f, created_us); put_u8(f, type);
    put_str(f, market);
    return f;
}

TEST(paradex_sbe_book_frame_decodes_prices_at_1e8) {
    const std::string f = book_frame(1792944261732495, 42, true, {{10000000000, 300000000}, {9900000000, 400000000}}, {{10100000000, 200000000}}, "AAVE-USD-PERP");
    json m = pdx::sbe::decode(f);
    ASSERT(!m.is_null());
    ASSERT_EQ(m.value("c", ""), std::string("order_book"));
    ASSERT_EQ(m.value("m", ""), std::string("AAVE-USD-PERP"));
    ASSERT_EQ(m.value("pkg", ""), std::string("s"));
    ASSERT_EQ(m.value("seq", int64_t{0}), int64_t{42});
    ASSERT_EQ(m.value("ts", int64_t{0}), int64_t{1792944261732495});
    ASSERT_EQ(m["bids"].size(), size_t{2});
    ASSERT_EQ(m["bids"][0][0].get<std::string>(), std::string("100"));
    ASSERT_EQ(m["bids"][1][1].get<std::string>(), std::string("4"));
    ASSERT_EQ(m["asks"][0][0].get<std::string>(), std::string("101"));
    // a bigger root block (a later schema version appending fields) is skipped by blockLength, not by assumption
    std::string g = f; uint16_t big = 97; std::memcpy(g.data(), &big, 2); g.insert(8 + 89, 8, '\0');
    json m2 = pdx::sbe::decode(g);
    ASSERT_EQ(m2["asks"][0][0].get<std::string>(), std::string("101"));
    ASSERT(pdx::sbe::decode(std::string(f, 0, 30)).is_null());        // truncated → null, never a throw
    ASSERT(pdx::sbe::decode(std::string("\x08\x00\x02\x00\x01\x00\x01\x00", 8)).is_null());   // BBO: not decoded
}

TEST(paradex_sbe_trade_frame_and_filter) {
    json fill = pdx::sbe::decode(trade_frame(10, 7, 1, 10050000000, 20000000, 9, 1, "BTC-USD-PERP"));
    ASSERT_EQ(fill.value("c", ""), std::string("trades"));
    ASSERT_EQ(fill.value("side", ""), std::string("BUY"));
    ASSERT_EQ(fill.value("price", ""), std::string("100.5"));
    ASSERT_EQ(fill.value("size", ""), std::string("0.2"));
    ASSERT_EQ(fill.value("type", ""), std::string("FILL"));
    ASSERT_EQ(fill.value("m", ""), std::string("BTC-USD-PERP"));
    auto tr = pdx::parse_trades(fill);
    ASSERT_EQ(tr.size(), size_t{1});
    ASSERT(tr[0].buy_aggr);
    ASSERT_EQ(tr[0].t_ms, int64_t{0});           // created_at 9 µs → 0 ms
    json settle = pdx::sbe::decode(trade_frame(10, 8, 2, 10050000000, 20000000, 10000, 4, "BTC-USD-PERP"));
    ASSERT_EQ(settle.value("type", ""), std::string("SETTLE_MARKET"));
    ASSERT(pdx::parse_trades(settle).empty());
    json liq = pdx::sbe::decode(trade_frame(10, 9, 2, 10050000000, 20000000, 10000, 2, "BTC-USD-PERP"));
    ASSERT_EQ(pdx::parse_trades(liq).size(), size_t{1});
    ASSERT(!pdx::parse_trades(liq)[0].buy_aggr);
}

TEST(paradex_book_applies_deltas_in_sequence) {
    pdx::Book b;
    ASSERT(b.apply(pdx::sbe::decode(book_frame(1000000, 1, true, {{10000000000, 300000000}}, {{10100000000, 200000000}}, "AAVE-USD-PERP"))) == venue::Apply::Snapshot);
    ASSERT(b.valid());
    ASSERT_EQ(b.t_ms(), int64_t{1000});
    // delta: remove the 100 bid (size 0), add 99.5
    ASSERT(b.apply(pdx::sbe::decode(book_frame(2000000, 2, false, {{10000000000, 0}, {9950000000, 100000000}}, {}, "AAVE-USD-PERP"))) == venue::Apply::Delta);
    hl::Book t = b.top(5);
    ASSERT_EQ(t.bids.size(), size_t{1});
    ASSERT_NEAR(t.bids[0].px, 99.5, 1e-9);
    ASSERT(b.apply(pdx::sbe::decode(book_frame(2500000, 2, false, {}, {}, "AAVE-USD-PERP"))) == venue::Apply::Ignored);   // exact repeat: already in the book (day-roll snapshot)
    ASSERT(b.valid());
    ASSERT(b.apply(pdx::sbe::decode(book_frame(3000000, 1, false, {}, {}, "AAVE-USD-PERP"))) == venue::Apply::Gap);   // step back
    ASSERT(!b.valid());
    pdx::Book c;
    ASSERT(c.apply(b.snapshot_message()) == venue::Apply::Snapshot);
    ASSERT_EQ(c.top(5).bids.size(), size_t{1});
    ASSERT_EQ(pdx::market_for("AAVE"), std::string("AAVE-USD-PERP"));
    ASSERT_EQ(pdx::book_channel("AAVE-USD-PERP"), std::string("order_book.AAVE-USD-PERP.deltas"));
    ASSERT_EQ(pdx::book_channel("AAVE-USD-PERP", "snapshot"), std::string("order_book.AAVE-USD-PERP.snapshot@15@100ms"));
    ASSERT(pdx::subscribe_message("trades.X", 3).find(R"("id":3)") != std::string::npos);
}

// ── archive loader dispatch ──────────────────────────────────────────────────────────────────────────────
TEST(binance_book_ticker_and_trades_parse_and_replay) {
    namespace bn = venue::binance;
    ASSERT(bn::streams_target({"AVAXUSDT", "BNBUSDT"}) == "/stream?streams=avaxusdt@bookTicker/avaxusdt@trade/bnbusdt@bookTicker/bnbusdt@trade");
    const json bt = json::parse(R"({"stream":"avaxusdt@bookTicker","data":{"e":"bookTicker","s":"AVAXUSDT","b":"8.7410","B":"564","a":"8.7420","A":"212","T":1000,"E":1001}})");
    auto b = bn::parse_book_ticker(bt);
    ASSERT(b && b->valid());
    ASSERT_EQ(b->t_ms, int64_t{1000});                                  // the match time T, not the event time E
    ASSERT_NEAR(b->bids[0].px, 8.741, 1e-12); ASSERT_NEAR(b->asks[0].sz, 212, 1e-12);
    ASSERT(!bn::parse_book_ticker(json::parse(R"({"data":{"e":"trade"}})")));
    // raw trade: m = buyer is maker → the taker SOLD; only MARKET prints count
    auto t = bn::parse_trades(json::parse(R"({"data":{"e":"trade","s":"AVAXUSDT","p":"8.749","q":"22","T":1002,"m":true,"X":"MARKET"}})"));
    ASSERT_EQ(t.size(), size_t{1}); ASSERT(!t[0].buy_aggr); ASSERT_EQ(t[0].t_ms, int64_t{1002});
    ASSERT(bn::parse_trades(json::parse(R"({"data":{"e":"trade","s":"AVAXUSDT","p":"8.7","q":"1","T":1003,"m":false,"X":"INSURANCE_FUND"}})")).empty());
    ASSERT_EQ(bn::parse_trades(json::parse(R"({"data":{"e":"aggTrade","s":"AVAXUSDT","p":"8.7","q":"1","T":1004,"m":false}})")).size(), size_t{1});
    // the archive loader replays a "v":"binance" file with OUR receive time attached
    auto dir = std::filesystem::temp_directory_path() / ("venues_bn_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    {
        ZstdDaily f(dir / "AVAX", {}, 3);
        const int64_t r = 1'700'000'000'000'000;
        f.write_line(R"({"r":)" + std::to_string(r) + R"(,"v":"binance","m":)" + bt.dump() + "}", r);
        f.write_line(R"({"r":)" + std::to_string(r + 131000) + R"(,"v":"binance","m":{"stream":"avaxusdt@trade","data":{"e":"trade","s":"AVAXUSDT","p":"8.749","q":"22","T":1002,"m":false,"X":"MARKET"}}})", r + 131000);
        f.close();
    }
    auto days = hl::list_days(dir);
    int64_t bad = 0;
    auto ev = hl::load_day(days.at(0), &bad);
    ASSERT_EQ(bad, int64_t{0});
    ASSERT_EQ(ev.size(), size_t{2});
    ASSERT(ev[0].is_book && ev[0].r_ms == 1'700'000'000'000);
    ASSERT(!ev[1].is_book && ev[1].trade.buy_aggr && ev[1].r_ms == 1'700'000'000'131);
    std::filesystem::remove_all(dir);
}

static std::filesystem::path tmpdir() {
    auto p = std::filesystem::temp_directory_path() / ("venues_test_" + std::to_string(getpid()));
    std::filesystem::create_directories(p);
    return p;
}

TEST(loader_replays_every_venue_from_its_tag) {
    auto dir = tmpdir();
    {
        ZstdDaily f(dir / "AAVE", {}, 3);
        const int64_t r = 1'700'000'000'000'000;
        f.write_line(R"({"r":)" + std::to_string(r) + R"(,"v":"extended","m":)" + ext_snap(1000, 1).dump() + "}", r);
        f.write_line(R"({"r":)" + std::to_string(r + 1000) + R"(,"v":"extended","m":)" + ext_delta(1001, 2, R"([{"q":"1","p":"100.5","c":"1"}])", "[]").dump() + "}", r + 1000);
        f.write_line(R"({"r":)" + std::to_string(r + 1500) + R"(,"v":"extended","snap":true,"m":{"ts":1000,"data":[{"i":0,"m":"AAVE-USD","S":"BUY","tT":"TRADE","T":5,"p":"90","q":"9"}],"seq":2}})", r + 1500);   // replay: dropped
        f.write_line(R"({"r":)" + std::to_string(r + 2000) + R"(,"v":"extended","m":{"ts":1001,"data":[{"i":1,"m":"AAVE-USD","S":"BUY","tT":"TRADE","T":1001,"p":"101","q":"0.5"}],"seq":3}})", r + 2000);
        f.write_line(R"({"r":)" + std::to_string(r + 3000) + R"(,"v":"aster","m":{"stream":"aaveusdt@depth20@100ms","data":{"e":"depthUpdate","E":1003,"T":1002,"s":"AAVEUSDT","b":[["100","1"]],"a":[["101","1"]]}}})", r + 3000);
        f.write_line(R"({"r":)" + std::to_string(r + 4000) + R"(,"v":"aster","m":{"stream":"aaveusdt@aggTrade","data":{"e":"aggTrade","E":1003,"s":"AAVEUSDT","a":1,"p":"100","q":"2","f":1,"l":1,"T":1003,"m":true}}})", r + 4000);
        f.write_line(R"({"r":)" + std::to_string(r + 5000) + R"(,"v":"paradex","m":)" + pdx::sbe::decode(book_frame(1004000, 1, true, {{10000000000, 100000000}}, {{10100000000, 100000000}}, "AAVE-USD-PERP")).dump() + "}", r + 5000);
        f.write_line(R"({"r":)" + std::to_string(r + 6000) + R"(,"v":"paradex","m":)" + pdx::sbe::decode(trade_frame(1005000, 2, 2, 10000000000, 100000000, 1005000, 1, "AAVE-USD-PERP")).dump() + "}", r + 6000);
        f.close();
    }
    auto days = hl::list_days(dir);
    ASSERT_EQ(days.size(), size_t{1});
    int64_t bad = 0;
    auto ev = hl::load_day(days[0], &bad);
    ASSERT_EQ(bad, int64_t{0});
    ASSERT_EQ(ev.size(), size_t{7});
    int books = 0, trades = 0;
    for (const auto& e : ev) (e.is_book ? books : trades)++;
    ASSERT_EQ(books, 4);
    ASSERT_EQ(trades, 3);
    ASSERT_EQ(ev.front().t_ms, int64_t{1000});
    ASSERT_EQ(ev.back().t_ms, int64_t{1005});
    // same time: trades before books — the Extended trade at 1001 precedes the book after the delta at 1001
    ASSERT(!ev[1].is_book && ev[1].t_ms == 1001 && ev[1].trade.buy_aggr);
    ASSERT(ev[2].is_book && ev[2].t_ms == 1001);
    ASSERT_NEAR(ev[2].book.bids[0].px, 100.5, 1e-9);        // the delta applied (it added a 100.5 bid)
    ASSERT(ev[3].is_book && ev[3].t_ms == 1002);            // Aster depth20 uses T, not E
    std::filesystem::remove_all(dir);
}

// ── mm_paper --venue extended (2026-10-29): the live paper path must turn Extended's messages into the SAME books and
// trades the replay (hl_events.hpp load_archive, used by scripts/mm_venues.sh) builds from the archived copy. ────────

TEST(extended_market_rules_from_info_markets) {
    const json j = json::parse(R"({"status":"OK","data":[
        {"name":"AVAX-USD","status":"ACTIVE","assetPrecision":1,"tradingConfig":{"minOrderSize":"1","minOrderSizeChange":"0.1","minPriceChange":"0.001"}},
        {"name":"BTC-USD","status":"ACTIVE","tradingConfig":{"minOrderSize":"0.0001","minOrderSizeChange":"0.00001","minPriceChange":"1"}},
        {"name":"OLD-USD","status":"DELISTED","tradingConfig":{"minOrderSize":"1","minOrderSizeChange":"1","minPriceChange":"0.1"}},
        {"name":"BAD-USD","status":"ACTIVE"}]})");
    const auto r = ext::parse_markets(j);
    ASSERT_EQ(r.size(), size_t{2});                              // delisted and config-less markets are not tradable
    const auto& a = r.at("AVAX-USD");
    ASSERT_NEAR(a.tick, 0.001, 1e-15);
    ASSERT_NEAR(a.size_step, 0.1, 1e-15);
    ASSERT_NEAR(a.min_size, 1.0, 1e-15);
    ASSERT_NEAR(r.at("BTC-USD").size_step, 0.00001, 1e-18);
    ASSERT(ext::parse_markets(json::parse("[]")).empty());
    // the paper quote size on those rules: $11 of AVAX at $20 = 0.55 coin → raised to the 1-coin minimum; at $5, 2.2 → 2.2
    hl::Params p; p.order_usd = 11; p.size_step = a.size_step; p.min_base = a.min_size;
    ASSERT_NEAR(hl::lot_qty(p, 20), 1.0, 1e-12);
    ASSERT_NEAR(hl::lot_qty(p, 5), 2.2, 1e-12);
    ASSERT_NEAR(hl::lot_qty(p, 4.9), 2.3, 1e-12);               // 2.2449 → up to the 0.1 grid, never down
}

TEST(extended_paper_path_matches_the_replay) {
    // the wire messages of one connection, in arrival order (book stream and trades stream interleaved)
    std::vector<std::pair<bool, json>> wire = {
        {true, ext_snap(1000, 1)},
        {false, json::parse(R"({"ts":1000,"data":[{"i":0,"m":"AAVE-USD","S":"SELL","tT":"TRADE","T":5,"p":"90","q":"9"}],"seq":1})")},   // the connection's replay
        {true, ext_delta(1001, 2, R"([{"q":"1","p":"100.5","c":"1"}])", "[]")},
        {false, json::parse(R"({"ts":1001,"data":[{"i":1,"m":"AAVE-USD","S":"BUY","tT":"TRADE","T":1001,"p":"101","q":"0.5"},{"i":2,"m":"AAVE-USD","S":"SELL","tT":"DELEVERAGE","T":1001,"p":"100","q":"3"}],"seq":2})")},
        {true, ext_delta(1002, 3, "[]", R"([{"q":"-2","p":"101","c":"0"}])")},
        {true, ext_delta(1002, 3, "[]", "[]")},                  // exact repeat: no event on either path
        {false, json::parse(R"({"ts":1003,"data":[{"i":3,"m":"AAVE-USD","S":"SELL","tT":"LIQUIDATION","T":1003,"p":"100.5","q":"0.2"}],"seq":3})")},
    };
    // the paper path (mm_paper_main ext_msg): book_event + parse_trades, first trades message of the connection dropped
    std::vector<hl::Event> paper;
    {
        ext::Book book;
        bool first_trades = true;
        int64_t seq = 0;
        for (const auto& [is_book, m] : wire) {
            if (is_book) {
                venue::Apply r;
                if (auto b = ext::book_event(book, m, &r)) { hl::Event e; e.t_ms = b->t_ms; e.seq = seq++; e.is_book = true; e.book = *b; paper.push_back(e); }
                ASSERT(r != venue::Apply::Gap);
            } else if (ext::is_trades(m)) {
                if (first_trades) { first_trades = false; continue; }
                for (auto& t : ext::parse_trades(m)) { t.coin.clear(); hl::Event e; e.t_ms = t.t_ms; e.seq = seq++; e.trade = t; paper.push_back(e); }
            }
        }
        hl::sort_events(paper);
    }
    // the replay path: the same messages as dex_recorder archives them (first trades message tagged "snap":true)
    auto dir = std::filesystem::temp_directory_path() / ("venues_paper_" + std::to_string(getpid()));
    std::filesystem::create_directories(dir);
    {
        ZstdDaily f(dir / "AAVE", {}, 3);
        int64_t r = 1'700'000'000'000'000;
        bool first_trades = true;
        for (const auto& [is_book, m] : wire) {
            const bool snap = !is_book && first_trades;
            if (!is_book) first_trades = false;
            f.write_line(R"({"r":)" + std::to_string(r) + R"(,"v":"extended")" + (snap ? R"(,"snap":true)" : "") + R"(,"m":)" + m.dump() + "}", r);
            r += 1000;
        }
        f.close();
    }
    const auto days = hl::list_days(dir);
    ASSERT_EQ(days.size(), size_t{1});
    const auto replay = hl::load_day(days[0]);
    std::filesystem::remove_all(dir);
    ASSERT_EQ(paper.size(), replay.size());
    ASSERT_EQ(paper.size(), size_t{5});                           // 3 books (repeat skipped), 2 trades (replay + DELEVERAGE dropped)
    for (size_t i = 0; i < paper.size(); ++i) {
        const auto& a = paper[i];
        const auto& b = replay[i];
        ASSERT_EQ(a.is_book, b.is_book);
        ASSERT_EQ(a.t_ms, b.t_ms);
        if (a.is_book) {
            ASSERT_EQ(a.book.bids.size(), b.book.bids.size());
            ASSERT_EQ(a.book.asks.size(), b.book.asks.size());
            for (size_t k = 0; k < a.book.bids.size(); ++k) { ASSERT_EQ(a.book.bids[k].px, b.book.bids[k].px); ASSERT_EQ(a.book.bids[k].sz, b.book.bids[k].sz); }
            for (size_t k = 0; k < a.book.asks.size(); ++k) { ASSERT_EQ(a.book.asks[k].px, b.book.asks[k].px); ASSERT_EQ(a.book.asks[k].sz, b.book.asks[k].sz); }
        } else {
            ASSERT_EQ(a.trade.px, b.trade.px);
            ASSERT_EQ(a.trade.sz, b.trade.sz);
            ASSERT_EQ(a.trade.buy_aggr, b.trade.buy_aggr);
            ASSERT_EQ(a.trade.coin, b.trade.coin);
        }
    }
    // spot checks on the converted values
    ASSERT(paper[0].is_book && paper[0].t_ms == 1000 && paper[0].book.bids[0].px == 100);
    ASSERT(!paper[1].is_book && paper[1].trade.buy_aggr && paper[1].trade.px == 101);       // trade before the book at 1001
    ASSERT(paper[2].is_book && paper[2].book.bids[0].px == 100.5);
    ASSERT(paper[3].is_book && paper[3].book.asks[0].px == 102);                              // the 101 ask removed (c 0)
    ASSERT(!paper[4].is_book && !paper[4].trade.buy_aggr && paper[4].trade.sz == 0.2);       // LIQUIDATION kept
    // a gap is reported (mm_paper reconnects) and yields no book
    ext::Book g;
    venue::Apply r;
    ext::book_event(g, ext_snap(1, 1), &r);
    ASSERT(!ext::book_event(g, ext_delta(2, 5, "[]", "[]"), &r) && r == venue::Apply::Gap);
}

int main() {
    RUN(extended_snapshot_then_deltas_use_the_absolute_size);
    RUN(extended_seq_gap_invalidates_until_the_next_snapshot);
    RUN(extended_day_roll_repeat_is_not_a_gap);
    RUN(extended_synthetic_snapshot_replays_to_the_same_book);
    RUN(extended_trades_carry_the_taker_side_and_skip_deleverage);
    RUN(aster_depth20_is_a_complete_sorted_book);
    RUN(aster_aggtrade_side_is_the_taker);
    RUN(paradex_sbe_book_frame_decodes_prices_at_1e8);
    RUN(paradex_sbe_trade_frame_and_filter);
    RUN(paradex_book_applies_deltas_in_sequence);
    RUN(loader_replays_every_venue_from_its_tag);
    RUN(binance_book_ticker_and_trades_parse_and_replay);
    RUN(extended_market_rules_from_info_markets);
    RUN(extended_paper_path_matches_the_replay);
    std::cout << tests_run - tests_failed << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
