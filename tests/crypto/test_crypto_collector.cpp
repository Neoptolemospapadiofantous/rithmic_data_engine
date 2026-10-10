/*  test_crypto_collector.cpp — parser, throttle, order-book and minute-summary tests for
    crypto_collector (src/crypto/crypto_feed.hpp). No network, no DB, no files.         */
#include "crypto/crypto_feed.hpp"
#include "tick_fanout.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <sstream>
#include <thread>

#include <cmath>
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
using namespace crypto;

static const int64_t T0 = 1791625080000000LL;   // 2026-10-10 10:18:00 UTC, a minute boundary

TEST(agg_trade_buyer_aggressor) {
    auto j = json::parse(R"({"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E":1791625105100,
        "s":"BTCUSDT","a":2999999999,"p":"82847.60","q":"0.125","f":1,"l":3,"T":1791625105099,"m":false}})");
    auto r = parse_agg_trade(j);
    ASSERT(r.has_value());
    ASSERT_EQ(r->symbol, std::string("BTCUSDT"));
    ASSERT_EQ(r->exchange, std::string("BINANCE"));
    ASSERT_EQ(r->source, std::string("binance_fut"));
    ASSERT_NEAR(r->price, 82847.60, 1e-9);
    ASSERT_EQ(r->size, 125);                         // 0.125 BTC = 125 lots of 0.001
    ASSERT_EQ(r->ts_us, 1791625105099000LL);
    ASSERT_EQ(r->seq, 999999999);                    // agg id % 2e9
    ASSERT(r->is_buy);                               // m=false → buyer was taker
    ASSERT_EQ(r->side(), 'B');
}

TEST(agg_trade_seller_aggressor_and_rounding) {
    auto j = json::parse(R"({"e":"aggTrade","s":"ETHUSDT","a":7,"p":"2500.01","q":"0.0029999","T":1,"m":true})");
    auto r = parse_agg_trade(j);
    ASSERT(r.has_value());
    ASSERT(!r->is_buy);
    ASSERT_EQ(r->side(), 'A');                       // db.cpp convention: 'A', not 'S'
    ASSERT_EQ(r->size, 3);                           // llround(2.9999)
    ASSERT_EQ(r->seq, 7);
}

TEST(agg_trade_rejects_other_events) {
    ASSERT(!parse_agg_trade(json::parse(R"({"e":"bookTicker","s":"BTCUSDT"})")).has_value());
    ASSERT(!parse_agg_trade(json::parse(R"({"result":null,"id":1})")).has_value());
    ASSERT(!parse_agg_trade(json::parse(R"({"e":"aggTrade","s":"BTCUSDT","p":"1","q":"0","T":5,"a":1})")).has_value());
}

TEST(book_ticker_parse) {
    auto j = json::parse(R"({"stream":"btcusdt@bookTicker","data":{"e":"bookTicker","u":1,"E":1791625105200,
        "T":1791625105198,"s":"BTCUSDT","b":"82847.50","B":"3.456","a":"82847.60","A":"0.010"}})");
    auto q = parse_book_ticker(j);
    ASSERT(q.has_value());
    ASSERT_NEAR(q->bid_price, 82847.50, 1e-9);
    ASSERT_NEAR(q->ask_price, 82847.60, 1e-9);
    ASSERT_EQ(q->bid_size, 3456);
    ASSERT_EQ(q->ask_size, 10);
    ASSERT_EQ(q->ts_us, 1791625105198000LL);         // transaction time T, not event time E
}

TEST(quote_throttle_keeps_last_per_100ms) {
    QuoteThrottle th;
    auto mk = [](const char* s, int64_t ms, double bid) {
        BboRow r; r.symbol = s; r.ts_us = ms * 1000; r.bid_price = bid; r.ask_price = bid + 1; return r; };
    ASSERT(!th.push(mk("BTCUSDT", 1000, 1)).has_value());
    ASSERT(!th.push(mk("BTCUSDT", 1050, 2)).has_value());
    ASSERT(!th.push(mk("ETHUSDT", 1060, 9)).has_value());        // other symbol, own slot
    ASSERT(!th.push(mk("BTCUSDT", 1099, 3)).has_value());
    auto out = th.push(mk("BTCUSDT", 1100, 4));                  // next bucket → emit last of previous
    ASSERT(out.has_value());
    ASSERT_NEAR(out->bid_price, 3, 1e-9);
    auto due = th.flush_due(1150 * 1000);                        // ETH bucket 10 ended; BTC bucket 11 not
    ASSERT_EQ(due.size(), size_t(1));
    ASSERT_EQ(due[0].symbol, std::string("ETHUSDT"));
    due = th.flush_due(1200 * 1000);
    ASSERT_EQ(due.size(), size_t(1));
    ASSERT_NEAR(due[0].bid_price, 4, 1e-9);
    ASSERT(th.flush_due(5000 * 1000).empty());
}

TEST(bitstamp_event_parse) {
    auto j = json::parse(R"({"data":{"id":2059428068261889,"id_str":"2059428068261889","order_type":1,
        "datetime":"1791625105","microtimestamp":"1791625105977435","amount":0.25,"amount_str":"0.25000000",
        "price":82850,"price_str":"82850"},"channel":"live_orders_btcusd","event":"order_created"})");
    auto e = parse_bitstamp_event(j);
    ASSERT(e.has_value());
    ASSERT(e->kind == MboKind::Created);
    ASSERT_EQ(e->id, 2059428068261889ULL);
    ASSERT(!e->is_bid);
    ASSERT_NEAR(e->price, 82850, 1e-9);
    ASSERT_NEAR(e->amount, 0.25, 1e-12);
    ASSERT_EQ(e->micro_ts, 1791625105977435LL);
    ASSERT_EQ(e->channel, std::string("live_orders_btcusd"));
    ASSERT(!parse_bitstamp_event(json::parse(R"({"event":"bts:subscription_succeeded","channel":"live_orders_btcusd","data":{}})")).has_value());
}

TEST(bitstamp_snapshot_parse) {
    auto j = json::parse(R"({"timestamp":"1791625105","microtimestamp":"1791625105977435",
        "bids":[["100.00","1.0","11"],["99.50","2.0","12"]],"asks":[["100.10","0.5","21"]]})");
    auto s = parse_bitstamp_snapshot(j);
    ASSERT(s.has_value());
    ASSERT_EQ(s->micro_ts, 1791625105977435LL);
    ASSERT_EQ(s->orders.size(), size_t(3));
    ASSERT_EQ(s->orders[0].first, 11ULL);
    ASSERT(s->orders[0].second.is_bid);
    ASSERT(!s->orders[2].second.is_bid);
    // group=1 style levels (no order id) are refused: the book needs ids
    ASSERT(!parse_bitstamp_snapshot(json::parse(R"({"microtimestamp":"5","bids":[["1","1"]],"asks":[]})")).has_value());
}

static MboEvent ev(MboKind k, uint64_t id, bool bid, double px, double amt, int64_t ts) {
    MboEvent e; e.kind = k; e.id = id; e.is_bid = bid; e.price = px; e.amount = amt; e.micro_ts = ts; return e;
}

static Snapshot base_snapshot(int64_t ts) {
    Snapshot s; s.micro_ts = ts;
    s.orders = {{11, {true, px_key(100.00), 1.0}}, {12, {true, px_key(99.50), 2.0}},
                {21, {false, px_key(100.10), 0.5}}, {22, {false, px_key(101.00), 3.0}}};
    return s;
}

TEST(resync_skips_events_at_or_before_snapshot) {
    MboBook b("BITSTAMP", "BTCUSD");
    bool skipped = false;
    b.apply(ev(MboKind::Created, 1, true, 100, 1, T0 + 10), &skipped);
    ASSERT(skipped);                                              // not synced yet → ignored
    b.load_snapshot(base_snapshot(T0 + 1000));
    b.apply(ev(MboKind::Deleted, 11, true, 100, 1.0, T0 + 999), &skipped);
    ASSERT(skipped);                                              // older than snapshot
    b.apply(ev(MboKind::Deleted, 11, true, 100, 1.0, T0 + 1000), &skipped);
    ASSERT(skipped);                                              // equal → already in snapshot
    ASSERT_EQ(b.order_count(), size_t(4));
    b.apply(ev(MboKind::Deleted, 11, true, 100, 1.0, T0 + 1001), &skipped);
    ASSERT(!skipped);
    ASSERT_EQ(b.order_count(), size_t(3));
    ASSERT_NEAR(b.best_bid(), 99.50, 1e-9);
}

TEST(book_create_change_delete) {
    MboBook b("BITSTAMP", "BTCUSD");
    b.load_snapshot(base_snapshot(T0));
    ASSERT_NEAR(b.best_bid(), 100.00, 1e-9);
    ASSERT_NEAR(b.best_ask(), 100.10, 1e-9);
    b.apply(ev(MboKind::Created, 31, true, 100.05, 0.7, T0 + 1));
    ASSERT_NEAR(b.best_bid(), 100.05, 1e-9);
    b.apply(ev(MboKind::Changed, 21, false, 100.10, 0.2, T0 + 2));         // partial fill
    ASSERT_NEAR(b.qty_at(false, 100.10), 0.2, 1e-12);
    b.apply(ev(MboKind::Created, 23, false, 100.10, 0.3, T0 + 3));
    ASSERT_NEAR(b.qty_at(false, 100.10), 0.5, 1e-12);
    b.apply(ev(MboKind::Deleted, 21, false, 100.10, 0.0, T0 + 4));
    ASSERT_NEAR(b.qty_at(false, 100.10), 0.3, 1e-12);
    ASSERT_NEAR(b.best_ask(), 100.10, 1e-9);
    b.apply(ev(MboKind::Deleted, 23, false, 100.10, 0.3, T0 + 5));
    ASSERT_NEAR(b.best_ask(), 101.00, 1e-9);
    ASSERT_EQ(b.order_count(), size_t(4));                                // 11,12,22,31
}

TEST(minute_summary_math) {
    MboBook b("BITSTAMP", "BTCUSD");
    b.load_snapshot(base_snapshot(T0));
    // minute 0 (T0 .. T0+60s)
    b.apply(ev(MboKind::Created, 31, true, 100.02, 0.5, T0 + 1'000'000));
    b.apply(ev(MboKind::Created, 32, false, 100.08, 1.5, T0 + 2'000'000));
    b.apply(ev(MboKind::Changed, 32, false, 100.08, 1.0, T0 + 3'000'000));
    b.apply(ev(MboKind::Deleted, 31, true, 100.02, 0.5, T0 + 4'000'000));   // cancelled (amount left)
    b.apply(ev(MboKind::Deleted, 21, false, 100.10, 0.0, T0 + 5'000'000));  // executed (amount 0)
    // first event of the next minute closes minute 0 (state BEFORE that event)
    auto rows = b.apply(ev(MboKind::Created, 33, true, 50.0, 9.0, T0 + 61'000'000));
    ASSERT_EQ(rows.size(), size_t(1));
    const auto& r = rows[0];
    ASSERT_EQ(r.minute_us, T0);
    ASSERT_EQ(r.adds, 2); ASSERT_EQ(r.changes, 1); ASSERT_EQ(r.deletes, 2); ASSERT_EQ(r.executed_deletes, 1);
    ASSERT_NEAR(r.add_qty, 2.0, 1e-12);
    ASSERT_NEAR(r.delete_qty, 0.5 + 0.5, 1e-12);     // remaining amounts from the book
    // book now: bids 11@100.00(1.0) 12@99.50(2.0); asks 32@100.08(1.0) 22@101.00(3.0)
    ASSERT_EQ(r.bid_orders, 2); ASSERT_EQ(r.ask_orders, 2);
    ASSERT_NEAR(r.best_bid, 100.00, 1e-9); ASSERT_NEAR(r.best_ask, 100.08, 1e-9);
    // mid 100.04, ±10 bps → [99.93996, 100.14004]: bid 100.00 (1.0), ask 100.08 (1.0)
    ASSERT_NEAR(r.bid_qty_10bps, 1.0, 1e-12); ASSERT_NEAR(r.ask_qty_10bps, 1.0, 1e-12);
    ASSERT_NEAR(r.order_count_imbalance, 0.0, 1e-12);
    ASSERT_NEAR(r.qty_imbalance, 0.0, 1e-12);
    ASSERT(r.cancel_to_add.has_value()); ASSERT_NEAR(*r.cancel_to_add, 0.5, 1e-12);   // 1 cancel / 2 adds
    ASSERT(r.avg_order_qty.has_value()); ASSERT_NEAR(*r.avg_order_qty, 1.0, 1e-12);
    ASSERT(!r.complete);                              // first minute after the initial snapshot is partial
    // quiet market: tick() closes minute 1; the snapshot-synced minute is complete
    auto rows2 = b.tick(T0 + 125'000'000);
    ASSERT_EQ(rows2.size(), size_t(1));
    ASSERT_EQ(rows2[0].minute_us, T0 + 60'000'000);
    ASSERT_EQ(rows2[0].adds, 1);
    ASSERT_EQ(rows2[0].bid_orders, 3);
    ASSERT_NEAR(rows2[0].order_count_imbalance, (3.0 - 2.0) / 5.0, 1e-12);
    ASSERT(rows2[0].complete);
    ASSERT(b.tick(T0 + 125'000'000).empty());
}

TEST(desync_marks_minute_partial_and_no_adds_gives_null_ratios) {
    MboBook b("BITSTAMP", "ETHUSD");
    b.load_snapshot(base_snapshot(T0));
    b.apply(ev(MboKind::Changed, 22, false, 101.0, 2.0, T0 + 1));
    b.tick(T0 + 61'000'000);                          // close partial first minute
    b.desync();
    b.load_snapshot(base_snapshot(T0 + 62'000'000));
    auto rows = b.tick(T0 + 121'000'000);
    ASSERT_EQ(rows.size(), size_t(1));
    ASSERT(!rows[0].complete);                        // a reconnect happened inside it
    ASSERT(!rows[0].cancel_to_add.has_value());
    ASSERT(!rows[0].avg_order_qty.has_value());
}

// ── tick fan-out (src/tick_fanout.hpp) ────────────────────────────────────────────────────────────────

TEST(fanout_trade_line_is_json_and_matches_db_row) {
    auto j = json::parse(R"({"e":"aggTrade","E":1791625105100,"s":"BTCUSDT","a":2999999999,"p":"82847.60",
        "q":"0.125","T":1791625105099,"m":false})");
    auto r = parse_agg_trade(j);
    ASSERT(r.has_value());
    std::string ln = fanout::make_line(7, fanout::trade_body(r->symbol, r->ts_us, r->price, r->size, r->is_buy,
                                                              r->seq, 1791625105100LL, 1791625105150000LL));
    ASSERT(!ln.empty() && ln.back() == '\n');
    auto o = json::parse(ln);
    ASSERT_EQ(o["n"].get<int>(), 7);
    ASSERT_EQ(o["k"].get<std::string>(), std::string("t"));
    ASSERT_EQ(o["s"].get<std::string>(), std::string("BTCUSDT"));
    ASSERT_EQ(o["ts"].get<int64_t>(), r->ts_us);            // same value the DB row stores
    ASSERT_EQ(o["q"].get<int64_t>(), 125);                  // stored lots, not coins
    ASSERT_EQ(o["seq"].get<int64_t>(), 999999999);
    ASSERT_EQ(o["b"].get<int>(), 1);
    ASSERT_EQ(o["E"].get<int64_t>(), 1791625105100LL);
    ASSERT_NEAR(o["p"].get<double>(), 82847.6, 1e-9);
}

TEST(fanout_quote_and_minute_lines) {
    auto q = json::parse(fanout::make_line(1, fanout::quote_body("ETH\"X", 5, 2500.5, 10, NAN, 0, 0, 9)));
    ASSERT_EQ(q["s"].get<std::string>(), std::string("ETH\"X"));     // escaped
    ASSERT(q["ap"].is_null());                                        // missing side → null
    ASSERT(q["E"].is_null());                                         // no event time → null
    ASSERT_EQ(q["bq"].get<int64_t>(), 10);
    auto m = json::parse(fanout::make_line(2, fanout::minute_body("BTCUSD", "BITSTAMP", T0, false, 1)));
    ASSERT_EQ(m["k"].get<std::string>(), std::string("m"));
    ASSERT_EQ(m["minute"].get<int64_t>(), T0);
    ASSERT(!m["complete"].get<bool>());
}

TEST(fanout_rithmic_seq_rank_matches_db_writer) {
    // db.cpp: identical (symbol, ts, price, size) rows in one batch get seq 0, 1, 2…; anything else 0
    int r = fanout::next_rank(false, 0);
    ASSERT_EQ(r, 0);
    r = fanout::next_rank(true, r); ASSERT_EQ(r, 1);
    r = fanout::next_rank(true, r); ASSERT_EQ(r, 2);
    r = fanout::next_rank(false, r); ASSERT_EQ(r, 0);
}

TEST(fanout_line_queue_bounded_and_never_waits) {
    fanout::LineQueue q(3);
    bool empty = false;
    ASSERT(q.try_push("a", &empty) && empty);
    ASSERT(q.try_push("b", &empty) && !empty);
    ASSERT(q.try_push("c"));
    ASSERT(!q.try_push("d"));                                // full → dropped, not blocked
    std::vector<std::string> out;
    q.take(out);
    ASSERT_EQ(out.size(), size_t(3));
    ASSERT(q.try_push("e", &empty) && empty);
}

TEST(fanout_client_buffer_cap) {
    fanout::ClientBuf b(10);
    ASSERT(b.append("12345"));
    ASSERT(b.append("67890"));
    ASSERT(!b.append("x"));                                  // over cap → caller drops the client
    b.consume(4);
    ASSERT_EQ(b.pending(), size_t(6));
    ASSERT(b.append("abcd"));
    ASSERT_EQ(std::string(b.data(), b.pending()), std::string("567890abcd"));
}

static int connect_local(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(static_cast<uint16_t>(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) { ::close(fd); return -1; }
    return fd;
}

static std::vector<std::string> read_lines(int fd, size_t want, int timeout_ms) {
    std::vector<std::string> out; std::string acc; char b[65536];
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (out.size() < want && std::chrono::steady_clock::now() < end) {
        pollfd p{fd, POLLIN, 0};
        if (::poll(&p, 1, 50) <= 0) continue;
        ssize_t r = ::recv(fd, b, sizeof b, 0);
        if (r <= 0) break;
        acc.append(b, static_cast<size_t>(r));
        size_t pos;
        while ((pos = acc.find('\n')) != std::string::npos) { out.push_back(acc.substr(0, pos)); acc.erase(0, pos + 1); }
    }
    return out;
}

static bool wait_for(const std::function<bool()>& f, int ms = 2000) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    return f();
}

TEST(fanout_server_delivers_every_line_in_order) {
    fanout::Server s("test", 0);
    ASSERT(!s.publish("\"k\":\"x\""));                       // not started, nobody listening → no work
    s.start(/*ephemeral=*/true);
    ASSERT(wait_for([&] { return s.bound_port() > 0; }));
    int fd = connect_local(s.bound_port());
    ASSERT(fd >= 0);
    ASSERT(wait_for([&] { return s.listening(); }));
    const int N = 5000;
    int queued = 0;
    for (int i = 0; i < N; ++i)
        if (s.publish(fanout::trade_body("BTCUSDT", 1000 + i, 1.0, 1, true, i, 0, 0))) ++queued;
    auto lines = read_lines(fd, static_cast<size_t>(queued) + 1, 5000);
    ASSERT_EQ(lines.size(), static_cast<size_t>(queued) + 1);
    ASSERT_EQ(json::parse(lines[0])["k"].get<std::string>(), std::string("hello"));
    // every queued line arrives, in publish order; `n` gaps only where publish() reported a drop
    ASSERT_EQ(static_cast<uint64_t>(queued), s.stats().published.load());
    ASSERT_EQ(static_cast<uint64_t>(N - queued), s.stats().dropped.load());
    int64_t prev = -1;
    for (size_t i = 1; i < lines.size(); ++i) {
        auto o = json::parse(lines[i]);
        ASSERT(o["n"].get<int64_t>() > prev);
        prev = o["n"].get<int64_t>();
    }
    ::close(fd);
    ASSERT(wait_for([&] { return !s.listening(); }));
    s.stop();
}

TEST(fanout_slow_client_is_dropped_not_the_feed) {
    fanout::Server s("test", 0, {}, 1 << 16, /*client_cap=*/4096);
    s.start(true);
    ASSERT(wait_for([&] { return s.bound_port() > 0; }));
    int slow = connect_local(s.bound_port());
    int rcv = 1024; ::setsockopt(slow, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof rcv);
    ASSERT(wait_for([&] { return s.listening(); }));
    std::string big(400, 'x');
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 20000 && s.listening(); ++i) s.publish("\"k\":\"t\",\"pad\":\"" + big + "\"");
    auto took = std::chrono::steady_clock::now() - t0;
    ASSERT(took < std::chrono::seconds(2));                  // publish never waited on the slow reader
    ASSERT(wait_for([&] { return s.stats().clients_dropped.load() >= 1; }, 3000));
    ASSERT(!s.listening());
    ::close(slow);
    s.stop();
}

int main() {
    RUN(agg_trade_buyer_aggressor);
    RUN(agg_trade_seller_aggressor_and_rounding);
    RUN(agg_trade_rejects_other_events);
    RUN(book_ticker_parse);
    RUN(quote_throttle_keeps_last_per_100ms);
    RUN(bitstamp_event_parse);
    RUN(bitstamp_snapshot_parse);
    RUN(resync_skips_events_at_or_before_snapshot);
    RUN(book_create_change_delete);
    RUN(minute_summary_math);
    RUN(desync_marks_minute_partial_and_no_adds_gives_null_ratios);
    RUN(fanout_trade_line_is_json_and_matches_db_row);
    RUN(fanout_quote_and_minute_lines);
    RUN(fanout_rithmic_seq_rank_matches_db_writer);
    RUN(fanout_line_queue_bounded_and_never_waits);
    RUN(fanout_client_buffer_cap);
    RUN(fanout_server_delivers_every_line_in_order);
    RUN(fanout_slow_client_is_dropped_not_the_feed);
    std::cout << tests_run - tests_failed << "/" << tests_run << " passed\n";
    return tests_failed ? 1 : 0;
}
