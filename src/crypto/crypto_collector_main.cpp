// crypto_collector — crypto market data for rithmic_engine (founder 2026-10-10).
//
//   crypto_collector [--binance] [--bitstamp] [--dry-run] [--duration SECONDS] [--verbose] [--fanout-port N]
//
// (no feed flag = both feeds)
//
// 1. Binance USDⓈ-M futures (no auth). Since Binance's 2025 URL split, aggTrade lives on the
//    /market route and bookTicker on the /public route; an unrouted connection only receives
//    /public streams, so a single legacy /stream URL would silently get no trades. Two sockets:
//      wss://fstream.binance.com/market/stream?streams=btcusdt@aggTrade/ethusdt@aggTrade
//      wss://fstream.binance.com/public/stream?streams=btcusdt@bookTicker/ethusdt@bookTicker
//    aggTrade → ticks, bookTicker → bbo (last quote per symbol per 100 ms). Row rules in
//    crypto_feed.hpp (same table format as the NQ collector and the binance_hist backfill).
// 2. Bitstamp market-by-order (no auth), wss://ws.bitstamp.net, live_orders_btcusd/ethusd.
//    Every message is archived raw, forever, to data/mbo/bitstamp/<pair>/<UTC date>.jsonl.zst;
//    an in-memory order book per pair yields one mbo_1m row per minute.
//
// Postgres: ONE connection (writer thread), synchronous_commit=off, batched UNNEST inserts every
// 250 ms or 5000 rows. Queues are bounded: when full, OUR rows are dropped and counted — the
// collector never blocks, and never competes with the NQ collector's writer by piling up.
// The connection is RAII-owned and re-opened with backoff after any error / Postgres restart.
// Fan-out (2026-10-10, founder: "has to be tick"): every parsed aggTrade, EVERY bookTicker (not the
// 100 ms DB throttle) and every closed mbo_1m minute is also sent as one JSON line to the clients of a
// 127.0.0.1-only TCP server (src/tick_fanout.hpp; port 8197, env CRYPTO_FANOUT_PORT or --fanout-port,
// 0/off disables). It never blocks the feed and shares nothing with the DB write path; the dashboard's
// /ws/market relays it to the browser.
// Logs go to stdout (the systemd unit appends them to data/logs/crypto_collector.log) —
// deliberately NOT through log.hpp, whose Logger also writes data/logs/collector.log.

#include "config.hpp"
#include "crypto/crypto_feed.hpp"
#include "tick_fanout.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <libpq-fe.h>
#include <zstd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace asio      = boost::asio;
namespace ssl       = asio::ssl;
namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;
using tcp           = asio::ip::tcp;
using asio::use_awaitable;
using json          = nlohmann::json;

// ── logging (stdout only) ────────────────────────────────────────────────────
static void logf(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::time_t t = std::time(nullptr);
    char ts[32]; std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::localtime(&t));
    std::printf("%s [crypto] %s\n", ts, msg);
    std::fflush(stdout);
}

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

static std::string format_ts(int64_t us) {
    time_t secs = static_cast<time_t>(us / 1'000'000);
    int micros  = static_cast<int>(us % 1'000'000);
    struct tm t; gmtime_r(&secs, &t);
    char buf[32]; std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", &t);
    char out[48]; std::snprintf(out, sizeof out, "%s.%06d+00", buf, micros);
    return out;
}

static std::string fmt_double(double v) {
    char b[40]; std::snprintf(b, sizeof b, "%.10g", v); return b;
}

// ── stats ────────────────────────────────────────────────────────────────────
struct Stats {
    std::atomic<int64_t> bn_trade_msgs{0}, bn_quote_msgs{0}, bn_quotes_kept{0};
    std::atomic<int64_t> bs_msgs{0}, bs_events{0}, bs_skipped{0}, bs_snapshots{0};
    std::atomic<int64_t> ticks_written{0}, bbo_written{0}, mbo_written{0};
    std::atomic<int64_t> ticks_dropped{0}, bbo_dropped{0}, mbo_dropped{0};
    std::atomic<int64_t> reconnects{0}, db_errors{0}, raw_bytes_in{0};
};
static Stats g_stats;

// ── bounded write queue (never blocks the feeds) ─────────────────────────────
class WriteQueue {
public:
    static constexpr size_t CAP = 200'000;        // per kind; beyond this our rows are dropped
    static constexpr size_t BATCH = 5000;

    void push(crypto::TickRow r) {
        std::lock_guard lk(mu_);
        if (ticks_.size() >= CAP) { ++g_stats.ticks_dropped; return; }
        ticks_.push_back(std::move(r));
        if (ticks_.size() >= BATCH) cv_.notify_one();
    }
    void push(crypto::BboRow r) {
        std::lock_guard lk(mu_);
        if (bbo_.size() >= CAP) { ++g_stats.bbo_dropped; return; }
        bbo_.push_back(std::move(r));
    }
    void push(crypto::Mbo1m r) {
        std::lock_guard lk(mu_);
        if (mbo_.size() >= 10'000) { ++g_stats.mbo_dropped; return; }
        mbo_.push_back(std::move(r));
        cv_.notify_one();
    }
    // Wait up to `ms`, then take everything queued.
    void take(std::vector<crypto::TickRow>& t, std::vector<crypto::BboRow>& b,
              std::vector<crypto::Mbo1m>& m, int ms) {
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(ms),
                     [&] { return stop_ || ticks_.size() >= BATCH || bbo_.size() >= BATCH; });
        for (auto& r : ticks_) t.push_back(std::move(r));
        for (auto& r : bbo_)   b.push_back(std::move(r));
        for (auto& r : mbo_)   m.push_back(std::move(r));
        ticks_.clear(); bbo_.clear(); mbo_.clear();
    }
    void stop() { std::lock_guard lk(mu_); stop_ = true; cv_.notify_all(); }
    bool stopped() { std::lock_guard lk(mu_); return stop_; }
    size_t depth() { std::lock_guard lk(mu_); return ticks_.size() + bbo_.size() + mbo_.size(); }
private:
    std::mutex mu_;
    std::condition_variable cv_;
    std::vector<crypto::TickRow> ticks_;
    std::vector<crypto::BboRow> bbo_;
    std::vector<crypto::Mbo1m> mbo_;
    bool stop_ = false;
};

// ── Postgres writer (one RAII connection, reconnect with backoff) ────────────
struct PgConnDeleter { void operator()(PGconn* c) const { if (c) PQfinish(c); } };
using PgConn = std::unique_ptr<PGconn, PgConnDeleter>;
struct PgResDeleter { void operator()(PGresult* r) const { if (r) PQclear(r); } };
using PgRes = std::unique_ptr<PGresult, PgResDeleter>;

static const char* MBO_1M_DDL = R"sql(
CREATE TABLE IF NOT EXISTS mbo_1m (
    venue                 VARCHAR(16)      NOT NULL,
    symbol                VARCHAR(16)      NOT NULL,
    ts_minute             TIMESTAMPTZ      NOT NULL,
    adds                  INTEGER          NOT NULL,
    changes               INTEGER          NOT NULL,
    deletes               INTEGER          NOT NULL,
    executed_deletes      INTEGER          NOT NULL,
    add_qty               DOUBLE PRECISION NOT NULL,
    delete_qty            DOUBLE PRECISION NOT NULL,
    bid_orders            INTEGER          NOT NULL,
    ask_orders            INTEGER          NOT NULL,
    bid_qty_top10bps      DOUBLE PRECISION NOT NULL,
    ask_qty_top10bps      DOUBLE PRECISION NOT NULL,
    best_bid              DOUBLE PRECISION,
    best_ask              DOUBLE PRECISION,
    order_count_imbalance DOUBLE PRECISION NOT NULL,
    qty_imbalance         DOUBLE PRECISION NOT NULL,
    cancel_to_add_ratio   DOUBLE PRECISION,
    avg_order_qty         DOUBLE PRECISION,
    complete              BOOLEAN          NOT NULL DEFAULT TRUE,
    PRIMARY KEY (venue, symbol, ts_minute)
))sql";

class PgWriter {
public:
    PgWriter(std::string connstr, WriteQueue& q) : connstr_(std::move(connstr)), q_(q) {}

    void run() {
        std::vector<crypto::TickRow> t; std::vector<crypto::BboRow> b; std::vector<crypto::Mbo1m> m;
        int backoff_s = 1;
        auto next_try = std::chrono::steady_clock::now();
        while (true) {
            const bool stopping = q_.stopped();
            q_.take(t, b, m, 250);
            // Hold at most CAP rows across outages; the rest are dropped and counted.
            trim(t, g_stats.ticks_dropped); trim(b, g_stats.bbo_dropped); trim(m, g_stats.mbo_dropped);
            if (!t.empty() || !b.empty() || !m.empty()) {
                if (!conn_ && std::chrono::steady_clock::now() >= next_try) {
                    if (connect()) backoff_s = 1;
                    else {
                        next_try = std::chrono::steady_clock::now() + std::chrono::seconds(backoff_s);
                        backoff_s = std::min(backoff_s * 2, 30);
                    }
                }
                if (conn_) {
                    bool ok = write_ticks(t) && write_bbo(b) && write_mbo(m);
                    if (!ok) {
                        ++g_stats.db_errors;
                        conn_.reset();                    // RAII: PQfinish now; retry next loop
                        next_try = std::chrono::steady_clock::now() + std::chrono::seconds(backoff_s);
                        backoff_s = std::min(backoff_s * 2, 30);
                    }
                }
            }
            if (stopping) {
                if (!t.empty() || !b.empty() || !m.empty())
                    logf("writer: stopping with %zu ticks / %zu bbo / %zu mbo rows unwritten",
                         t.size(), b.size(), m.size());
                break;
            }
        }
        conn_.reset();
    }

private:
    template <class V> static void trim(V& v, std::atomic<int64_t>& dropped) {
        if (v.size() > WriteQueue::CAP) {
            dropped += static_cast<int64_t>(v.size() - WriteQueue::CAP);
            v.erase(v.begin(), v.begin() + static_cast<long>(v.size() - WriteQueue::CAP));
        }
    }

    bool exec(const char* sql) {
        PgRes r(PQexec(conn_.get(), sql));
        auto s = r ? PQresultStatus(r.get()) : PGRES_FATAL_ERROR;
        if (s != PGRES_COMMAND_OK && s != PGRES_TUPLES_OK) {
            logf("DB error: %s", conn_ ? PQerrorMessage(conn_.get()) : "no connection");
            return false;
        }
        return true;
    }

    bool connect() {
        PgConn c(PQconnectdb(connstr_.c_str()));
        if (!c || PQstatus(c.get()) != CONNECTION_OK) {
            logf("Postgres connect failed: %s", c ? PQerrorMessage(c.get()) : "out of memory");
            return false;                                 // c's deleter closes it
        }
        conn_ = std::move(c);
        if (!exec("SET synchronous_commit = off") || !exec("SET statement_timeout = '30s'") ||
            !exec(MBO_1M_DDL)) {
            conn_.reset();
            return false;
        }
        logf("Postgres connected (1 connection, synchronous_commit=off)");
        return true;
    }

    // Postgres array literal element: quote + backslash-escape.
    static void elem(std::string& out, const std::string& v) {
        out += '"';
        for (char c : v) { if (c == '"' || c == '\\') out += '\\'; out += c; }
        out += '"';
    }

    bool run_params(const char* sql, std::vector<std::string>& arrs, const char* what,
                    std::atomic<int64_t>& written) {
        std::vector<const char*> p;
        for (auto& a : arrs) { a = '{' + a + '}'; p.push_back(a.c_str()); }
        PgRes r(PQexecParams(conn_.get(), sql, static_cast<int>(p.size()), nullptr, p.data(),
                             nullptr, nullptr, 0));
        auto s = r ? PQresultStatus(r.get()) : PGRES_FATAL_ERROR;
        if (s != PGRES_COMMAND_OK) {
            logf("DB error writing %s: %s", what, r ? PQresultErrorMessage(r.get()) : PQerrorMessage(conn_.get()));
            return false;
        }
        const char* tag = PQcmdTuples(r.get());
        written += tag ? std::atoll(tag) : 0;
        return true;
    }

    bool write_ticks(std::vector<crypto::TickRow>& rows) {
        if (rows.empty()) return true;
        std::vector<std::string> a(9);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            if (i) for (auto& s : a) s += ',';
            a[0] += '"' + format_ts(r.ts_us) + '"';
            elem(a[1], r.symbol); elem(a[2], r.exchange);
            a[3] += fmt_double(r.price); a[4] += std::to_string(r.size); a[5] += std::to_string(r.seq);
            a[6] += r.is_buy ? "\"B\"" : "\"A\"";
            a[7] += r.is_buy ? "true" : "false";
            elem(a[8], r.source);
        }
        const char* sql =
            "INSERT INTO ticks (ts_event, symbol, exchange, price, size, seq, side, is_buy, source)"
            " SELECT * FROM unnest($1::timestamptz[], $2::varchar[], $3::varchar[], $4::float8[],"
            "  $5::int8[], $6::int4[], $7::char[], $8::bool[], $9::varchar[])"
            " ON CONFLICT (symbol, exchange, ts_event, price, size, seq) DO NOTHING";
        if (!run_params(sql, a, "ticks", g_stats.ticks_written)) return false;
        rows.clear();
        return true;
    }

    bool write_bbo(std::vector<crypto::BboRow>& rows) {
        if (rows.empty()) return true;
        std::vector<std::string> a(8);
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            if (i) for (auto& s : a) s += ',';
            a[0] += '"' + format_ts(r.ts_us) + '"';
            elem(a[1], r.symbol); elem(a[2], r.exchange);
            a[3] += fmt_double(r.bid_price); a[4] += std::to_string(r.bid_size);
            a[5] += fmt_double(r.ask_price); a[6] += std::to_string(r.ask_size);
            elem(a[7], r.source);
        }
        // bid_orders / ask_orders stay NULL: Binance gives no order counts.
        const char* sql =
            "INSERT INTO bbo (ts_event, symbol, exchange, bid_price, bid_size, ask_price, ask_size, source)"
            " SELECT * FROM unnest($1::timestamptz[], $2::varchar[], $3::varchar[], $4::float8[],"
            "  $5::int4[], $6::float8[], $7::int4[], $8::varchar[])"
            " ON CONFLICT (symbol, exchange, ts_event) DO NOTHING";
        if (!run_params(sql, a, "bbo", g_stats.bbo_written)) return false;
        rows.clear();
        return true;
    }

    bool write_mbo(std::vector<crypto::Mbo1m>& rows) {
        if (rows.empty()) return true;
        std::vector<std::string> a(20);
        auto opt = [](const std::optional<double>& v) { return v ? fmt_double(*v) : std::string("NULL"); };
        auto px  = [](double v) { return v > 0 ? fmt_double(v) : std::string("NULL"); };
        for (size_t i = 0; i < rows.size(); ++i) {
            const auto& r = rows[i];
            if (i) for (auto& s : a) s += ',';
            elem(a[0], r.venue); elem(a[1], r.symbol);
            a[2] += '"' + format_ts(r.minute_us) + '"';
            a[3] += std::to_string(r.adds); a[4] += std::to_string(r.changes);
            a[5] += std::to_string(r.deletes); a[6] += std::to_string(r.executed_deletes);
            a[7] += fmt_double(r.add_qty); a[8] += fmt_double(r.delete_qty);
            a[9] += std::to_string(r.bid_orders); a[10] += std::to_string(r.ask_orders);
            a[11] += fmt_double(r.bid_qty_10bps); a[12] += fmt_double(r.ask_qty_10bps);
            a[13] += px(r.best_bid); a[14] += px(r.best_ask);
            a[15] += fmt_double(r.order_count_imbalance); a[16] += fmt_double(r.qty_imbalance);
            a[17] += opt(r.cancel_to_add); a[18] += opt(r.avg_order_qty);
            a[19] += r.complete ? "true" : "false";
        }
        const char* sql =
            "INSERT INTO mbo_1m (venue, symbol, ts_minute, adds, changes, deletes, executed_deletes,"
            "  add_qty, delete_qty, bid_orders, ask_orders, bid_qty_top10bps, ask_qty_top10bps,"
            "  best_bid, best_ask, order_count_imbalance, qty_imbalance, cancel_to_add_ratio,"
            "  avg_order_qty, complete)"
            " SELECT * FROM unnest($1::varchar[], $2::varchar[], $3::timestamptz[], $4::int4[],"
            "  $5::int4[], $6::int4[], $7::int4[], $8::float8[], $9::float8[], $10::int4[],"
            "  $11::int4[], $12::float8[], $13::float8[], $14::float8[], $15::float8[],"
            "  $16::float8[], $17::float8[], $18::float8[], $19::float8[], $20::bool[])"
            " ON CONFLICT (venue, symbol, ts_minute) DO UPDATE SET"
            "  adds = excluded.adds, changes = excluded.changes, deletes = excluded.deletes,"
            "  executed_deletes = excluded.executed_deletes, add_qty = excluded.add_qty,"
            "  delete_qty = excluded.delete_qty, bid_orders = excluded.bid_orders,"
            "  ask_orders = excluded.ask_orders, bid_qty_top10bps = excluded.bid_qty_top10bps,"
            "  ask_qty_top10bps = excluded.ask_qty_top10bps, best_bid = excluded.best_bid,"
            "  best_ask = excluded.best_ask, order_count_imbalance = excluded.order_count_imbalance,"
            "  qty_imbalance = excluded.qty_imbalance, cancel_to_add_ratio = excluded.cancel_to_add_ratio,"
            "  avg_order_qty = excluded.avg_order_qty, complete = excluded.complete";
        if (!run_params(sql, a, "mbo_1m", g_stats.mbo_written)) return false;
        rows.clear();
        return true;
    }

    std::string connstr_;
    WriteQueue& q_;
    PgConn conn_;
};

// ── raw MBO archive: one zstd stream per pair, rolled at UTC midnight ───────
// Appending to an existing day file after a restart starts a new zstd frame; concatenated
// frames decode as one stream (zstdcat). Flushed every 2 s → a crash loses ~2 s at most.
// Retention: forever — nothing here ever deletes.
class ZstdDaily {
public:
    explicit ZstdDaily(std::filesystem::path dir) : dir_(std::move(dir)) {}
    ~ZstdDaily() { close(); }
    ZstdDaily(const ZstdDaily&) = delete;
    ZstdDaily& operator=(const ZstdDaily&) = delete;

    void write_line(const std::string& line, int64_t at_us) {
        const std::string day = utc_date(at_us);
        if (day != day_) { close(); open(day); }
        if (!fp_) return;
        ZSTD_inBuffer in{line.data(), line.size(), 0};
        while (in.pos < in.size) drive(in, ZSTD_e_continue);
        static const char nl = '\n';
        ZSTD_inBuffer in2{&nl, 1, 0};
        drive(in2, ZSTD_e_continue);
    }
    void flush() {
        if (!fp_) return;
        ZSTD_inBuffer in{nullptr, 0, 0};
        while (drive(in, ZSTD_e_flush) != 0) {}
        std::fflush(fp_);
    }
    void close() {
        if (fp_) {
            ZSTD_inBuffer in{nullptr, 0, 0};
            while (drive(in, ZSTD_e_end) != 0) {}
            std::fclose(fp_); fp_ = nullptr;
        }
        if (cctx_) { ZSTD_freeCCtx(cctx_); cctx_ = nullptr; }
        day_.clear();
    }
    std::filesystem::path current() const { return dir_ / (day_ + ".jsonl.zst"); }

    static std::string utc_date(int64_t us) {
        time_t s = static_cast<time_t>(us / 1'000'000); struct tm t; gmtime_r(&s, &t);
        char b[16]; std::strftime(b, sizeof b, "%Y-%m-%d", &t); return b;
    }

private:
    void open(const std::string& day) {
        std::error_code ec;
        std::filesystem::create_directories(dir_, ec);
        day_ = day;
        fp_ = std::fopen(current().c_str(), "ab");
        if (!fp_) { logf("raw archive: cannot open %s", current().c_str()); day_.clear(); return; }
        cctx_ = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_compressionLevel, 9);
        ZSTD_CCtx_setParameter(cctx_, ZSTD_c_checksumFlag, 1);
        out_.resize(ZSTD_CStreamOutSize());
        logf("raw archive: writing %s", current().c_str());
    }
    size_t drive(ZSTD_inBuffer& in, ZSTD_EndDirective mode) {
        ZSTD_outBuffer o{out_.data(), out_.size(), 0};
        size_t rem = ZSTD_compressStream2(cctx_, &o, &in, mode);
        if (ZSTD_isError(rem)) { logf("zstd error: %s", ZSTD_getErrorName(rem)); return 0; }
        if (o.pos) std::fwrite(out_.data(), 1, o.pos, fp_);
        return rem;
    }
    std::filesystem::path dir_;
    std::string day_;
    std::FILE* fp_ = nullptr;
    ZSTD_CCtx* cctx_ = nullptr;
    std::vector<char> out_;
};

// ── options ──────────────────────────────────────────────────────────────────
struct Opts {
    bool binance = false, bitstamp = false, dry_run = false, verbose = false;
    int duration_s = 0;
    int fanout_port = -1;                       // -1 = env CRYPTO_FANOUT_PORT, default 8197
};
static Opts g_opt;
static WriteQueue g_queue;
static std::unique_ptr<fanout::Server> g_fan;

using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

static asio::awaitable<std::unique_ptr<WsStream>>
ws_connect(ssl::context& ctx, const std::string& host, const std::string& target) {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(host, "443", use_awaitable);
    auto ws = std::make_unique<WsStream>(ex, ctx);
    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(15));
    co_await beast::get_lowest_layer(*ws).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SNI failed");
    ws->next_layer().set_verify_callback(ssl::host_name_verification(host));
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);
    beast::get_lowest_layer(*ws).expires_never();
    websocket::stream_base::timeout to{};
    to.handshake_timeout = std::chrono::seconds(15);
    to.idle_timeout      = std::chrono::seconds(30);   // ping at 15 s of silence, drop at 30 s
    to.keep_alive_pings  = true;
    ws->set_option(to);
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req) {
        req.set(http::field::user_agent, "rithmic_engine-crypto_collector/1.0");
    }));
    ws->read_message_max(64 << 20);
    co_await ws->async_handshake(host, target, use_awaitable);
    co_return ws;
}

static asio::awaitable<std::string>
https_get(ssl::context& ctx, const std::string& host, const std::string& target) {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(host, "443", use_awaitable);
    beast::ssl_stream<beast::tcp_stream> s(ex, ctx);
    beast::get_lowest_layer(s).expires_after(std::chrono::seconds(20));
    co_await beast::get_lowest_layer(s).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(s.native_handle(), host.c_str())) throw std::runtime_error("SNI failed");
    s.set_verify_callback(ssl::host_name_verification(host));
    co_await s.async_handshake(ssl::stream_base::client, use_awaitable);
    http::request<http::empty_body> req{http::verb::get, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "rithmic_engine-crypto_collector/1.0");
    co_await http::async_write(s, req, use_awaitable);
    beast::flat_buffer buf;
    http::response_parser<http::string_body> p;
    p.body_limit(64u << 20);
    co_await http::async_read(s, buf, p, use_awaitable);
    if (p.get().result() != http::status::ok)
        throw std::runtime_error("HTTP " + std::to_string(p.get().result_int()) + " for " + target);
    beast::error_code ec;
    beast::get_lowest_layer(s).expires_after(std::chrono::seconds(3));
    co_await s.async_shutdown(asio::redirect_error(use_awaitable, ec));   // best effort
    co_return p.release().body();
}

static asio::awaitable<void> sleep_s(int s) {
    asio::steady_timer t(co_await asio::this_coro::executor);
    t.expires_after(std::chrono::seconds(s));
    co_await t.async_wait(use_awaitable);
}

// Reconnect loop with exponential backoff (1 → 60 s; reset after a session lasting 60 s+).
template <class F>
static asio::awaitable<void> run_forever(const char* name, F session) {
    int backoff = 1;
    while (true) {
        auto t0 = std::chrono::steady_clock::now();
        std::string why;
        try { co_await session(); why = "closed"; }
        catch (const std::exception& e) { why = e.what(); }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
        ++g_stats.reconnects;
        logf("%s: disconnected (%s) — reconnecting in %d s", name, why.c_str(), backoff);
        co_await sleep_s(backoff);
        backoff = std::min(backoff * 2, 60);
    }
}

// ── Binance ──────────────────────────────────────────────────────────────────
static crypto::QuoteThrottle g_throttle;

static asio::awaitable<void> binance_session(ssl::context& ctx, std::string target, bool trades) {
    auto ws = co_await ws_connect(ctx, "fstream.binance.com", target);
    logf("binance %s: connected %s", trades ? "trades" : "quotes", target.c_str());
    beast::flat_buffer buf;
    int shown = 0;
    while (true) {
        buf.clear();
        co_await ws->async_read(buf, use_awaitable);
        std::string msg = beast::buffers_to_string(buf.data());
        if (g_opt.verbose && shown < 2) { ++shown; logf("sample: %.400s", msg.c_str()); }
        const int64_t recv = now_us();
        json j = json::parse(msg, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        if (trades) {
            ++g_stats.bn_trade_msgs;
            auto r = crypto::parse_agg_trade(j);
            if (r && g_fan && g_fan->listening())
                g_fan->publish(fanout::trade_body(r->symbol, r->ts_us, r->price, r->size, r->is_buy, r->seq,
                                                  crypto::inum(crypto::at(crypto::binance_payload(j), "E")), recv));
            if (r && !g_opt.dry_run) g_queue.push(std::move(*r));
        } else {
            ++g_stats.bn_quote_msgs;
            if (auto q = crypto::parse_book_ticker(j)) {
                if (g_fan && g_fan->listening())
                    g_fan->publish(fanout::quote_body(q->symbol, q->ts_us, q->bid_price, q->bid_size, q->ask_price,
                                                      q->ask_size, crypto::inum(crypto::at(crypto::binance_payload(j), "E")),
                                                      recv));
                if (auto out = g_throttle.push(*q)) {
                    ++g_stats.bn_quotes_kept;
                    if (!g_opt.dry_run) g_queue.push(std::move(*out));
                }
            }
        }
    }
}

// ── Bitstamp MBO ─────────────────────────────────────────────────────────────
struct Pair {
    std::string pair, symbol;                    // "btcusd", "BTCUSD"
    crypto::MboBook book;
    std::unique_ptr<ZstdDaily> raw;
};
static std::vector<std::unique_ptr<Pair>> g_pairs;

static Pair* pair_for_channel(const std::string& ch) {
    for (auto& p : g_pairs) if (ch == "live_orders_" + p->pair) return p.get();
    return nullptr;
}

static void archive(Pair& p, const std::string& body_json, bool snapshot, int64_t recv) {
    if (g_opt.dry_run || !p.raw) return;
    std::string line;
    line.reserve(body_json.size() + 48);
    line += snapshot ? "{\"snapshot\":true,\"r\":" : "{\"r\":";
    line += std::to_string(recv);
    line += snapshot ? ",\"pair\":\"" + p.pair + "\",\"m\":" : ",\"m\":";
    for (char c : body_json) if (c != '\n' && c != '\r') line += c;   // one message = one line
    line += '}';
    p.raw->write_line(line, recv);
}

static void push_rows(std::vector<crypto::Mbo1m>&& rows) {
    for (auto& r : rows) {
        if (g_opt.verbose || g_opt.dry_run)
            logf("mbo_1m %s %s adds=%lld chg=%lld del=%lld exec=%lld bid_o=%lld ask_o=%lld bb=%.2f ba=%.2f q10 %.4f/%.4f%s",
                 r.symbol.c_str(), format_ts(r.minute_us).c_str(), (long long)r.adds, (long long)r.changes,
                 (long long)r.deletes, (long long)r.executed_deletes, (long long)r.bid_orders,
                 (long long)r.ask_orders, r.best_bid, r.best_ask, r.bid_qty_10bps, r.ask_qty_10bps,
                 r.complete ? "" : " (partial)");
        if (g_fan && g_fan->listening())
            g_fan->publish(fanout::minute_body(r.symbol, r.venue, r.minute_us, r.complete, now_us()));
        if (!g_opt.dry_run) g_queue.push(std::move(r));
    }
}

static asio::awaitable<void> snapshot_pair(ssl::context& ctx, Pair& p) {
    const int64_t sent = now_us();
    std::string body = co_await https_get(ctx, "www.bitstamp.net",
                                          "/api/v2/order_book/" + p.pair + "/?group=2");
    json j = json::parse(body, nullptr, false);
    auto snap = crypto::parse_bitstamp_snapshot(j);
    if (!snap) throw std::runtime_error("bad order_book snapshot for " + p.pair);
    archive(p, body, true, now_us());
    const size_t before = p.book.order_count();
    const bool was_synced = p.book.synced();
    const double bb = p.book.best_bid(), ba = p.book.best_ask();
    p.book.load_snapshot(*snap);
    ++g_stats.bs_snapshots;
    logf("bitstamp %s: snapshot %zu orders, bid %.2f ask %.2f, snapshot age %.2f s%s",
         p.pair.c_str(), snap->orders.size(), p.book.best_bid(), p.book.best_ask(),
         (sent - snap->micro_ts) / 1e6,
         was_synced ? (" (resync; book had " + std::to_string(before) + " orders, bid " +
                       fmt_double(bb) + " ask " + fmt_double(ba) + ")").c_str() : "");
}

static asio::awaitable<void> bitstamp_session(ssl::context& ctx) {
    for (auto& p : g_pairs) p->book.desync();
    auto ws = co_await ws_connect(ctx, "ws.bitstamp.net", "/");
    ws->text(true);
    for (auto& p : g_pairs) {
        json sub = {{"event", "bts:subscribe"}, {"data", {{"channel", "live_orders_" + p->pair}}}};
        co_await ws->async_write(asio::buffer(sub.dump()), use_awaitable);
    }
    logf("bitstamp: connected, subscribed live_orders for %zu pairs", g_pairs.size());
    // Snapshot AFTER subscribing: events that arrive meanwhile wait in the socket and are
    // applied afterwards, minus those already in the snapshot (microtimestamp rule).
    for (auto& p : g_pairs) co_await snapshot_pair(ctx, *p);
    auto last_snap = std::chrono::steady_clock::now();

    beast::flat_buffer buf;
    int shown = 0;
    while (true) {
        // Periodic re-snapshot: no sequence numbers exist, so this is the only drift check.
        if (std::chrono::steady_clock::now() - last_snap > std::chrono::minutes(30)) {
            for (auto& p : g_pairs) co_await snapshot_pair(ctx, *p);
            last_snap = std::chrono::steady_clock::now();
        }
        buf.clear();
        co_await ws->async_read(buf, use_awaitable);
        const int64_t recv = now_us();
        std::string msg = beast::buffers_to_string(buf.data());
        ++g_stats.bs_msgs;
        g_stats.raw_bytes_in += static_cast<int64_t>(msg.size());
        if (g_opt.verbose && shown < 6) { ++shown; logf("sample: %.400s", msg.c_str()); }
        json j = json::parse(msg, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        const std::string ev = j.value("event", "");
        if (ev == "bts:request_reconnect") throw std::runtime_error("server requested reconnect");
        Pair* p = pair_for_channel(j.value("channel", ""));
        if (!p) continue;
        archive(*p, msg, false, recv);
        if (auto e = crypto::parse_bitstamp_event(j)) {
            ++g_stats.bs_events;
            bool skipped = false;
            push_rows(p->book.apply(*e, &skipped));
            if (skipped) ++g_stats.bs_skipped;
        }
    }
}

// ── housekeeping timers ──────────────────────────────────────────────────────
static asio::awaitable<void> housekeeping() {
    asio::steady_timer t(co_await asio::this_coro::executor);
    int64_t ticks = 0;
    auto last_summary = std::chrono::steady_clock::now();
    while (true) {
        t.expires_after(std::chrono::milliseconds(250));
        co_await t.async_wait(use_awaitable);
        ++ticks;
        const int64_t now = now_us();
        for (auto& q : g_throttle.flush_due(now)) {
            ++g_stats.bn_quotes_kept;
            if (!g_opt.dry_run) g_queue.push(std::move(q));
        }
        if (ticks % 4 == 0)                                   // 1 s: close quiet minutes (2 s grace)
            for (auto& p : g_pairs) push_rows(p->book.tick(now - 2'000'000));
        if (ticks % 8 == 0)                                   // 2 s: raw archive flush
            for (auto& p : g_pairs) if (p->raw) p->raw->flush();
        const bool summary_due = std::chrono::steady_clock::now() - last_summary >=
                                 std::chrono::seconds(g_opt.dry_run ? 10 : 60);
        if (summary_due) {
            last_summary = std::chrono::steady_clock::now();
            logf("summary: binance trades=%lld quotes=%lld kept=%lld | bitstamp msgs=%lld events=%lld "
                 "skipped=%lld snapshots=%lld | written ticks=%lld bbo=%lld mbo_1m=%lld | dropped "
                 "ticks=%lld bbo=%lld mbo=%lld | queue=%zu db_errors=%lld reconnects=%lld | fanout clients=%llu "
                 "lines=%llu dropped=%llu",
                 (long long)g_stats.bn_trade_msgs, (long long)g_stats.bn_quote_msgs,
                 (long long)g_stats.bn_quotes_kept, (long long)g_stats.bs_msgs,
                 (long long)g_stats.bs_events, (long long)g_stats.bs_skipped,
                 (long long)g_stats.bs_snapshots, (long long)g_stats.ticks_written,
                 (long long)g_stats.bbo_written, (long long)g_stats.mbo_written,
                 (long long)g_stats.ticks_dropped, (long long)g_stats.bbo_dropped,
                 (long long)g_stats.mbo_dropped, g_queue.depth(), (long long)g_stats.db_errors,
                 (long long)g_stats.reconnects,
                 (unsigned long long)(g_fan ? g_fan->stats().clients.load() : 0),
                 (unsigned long long)(g_fan ? g_fan->stats().published.load() : 0),
                 (unsigned long long)(g_fan ? g_fan->stats().dropped.load() : 0));
        }
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--binance") g_opt.binance = true;
        else if (a == "--bitstamp") g_opt.bitstamp = true;
        else if (a == "--dry-run") g_opt.dry_run = true;
        else if (a == "--verbose") g_opt.verbose = true;
        else if (a == "--duration" && i + 1 < argc) g_opt.duration_s = std::atoi(argv[++i]);
        else if (a == "--fanout-port" && i + 1 < argc) g_opt.fanout_port = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr,
                "usage: crypto_collector [--binance] [--bitstamp] [--dry-run] [--duration S] [--verbose] [--fanout-port N]\n"
                "  no feed flag = both feeds; --dry-run connects and counts, writes nothing\n"
                "  --fanout-port: 127.0.0.1 tick fan-out port (default env CRYPTO_FANOUT_PORT or 8197; 0 = off)\n");
            return 2;
        }
    }
    if (!g_opt.binance && !g_opt.bitstamp) g_opt.binance = g_opt.bitstamp = true;

    for (const char* pr : {"btcusd", "ethusd"}) {
        auto p = std::make_unique<Pair>(Pair{pr, "", crypto::MboBook("BITSTAMP", ""), nullptr});
        p->symbol = p->pair; for (auto& c : p->symbol) c = static_cast<char>(std::toupper(c));
        p->book = crypto::MboBook("BITSTAMP", p->symbol);
        if (!g_opt.dry_run && g_opt.bitstamp)
            p->raw = std::make_unique<ZstdDaily>(std::filesystem::path("data/mbo/bitstamp") / p->pair);
        g_pairs.push_back(std::move(p));
    }

    std::unique_ptr<PgWriter> writer;
    std::thread writer_thread;
    if (!g_opt.dry_run) {
        Config cfg = Config::from_env(".env");
        if (cfg.pg_password.empty()) { logf("PG_PASSWORD not set (.env)"); return 1; }
        std::string connstr = "host=" + Config::pg_value(cfg.pg_host) + " port=" + Config::pg_value(cfg.pg_port) +
            " dbname=" + Config::pg_value(cfg.pg_db) + " user=" + Config::pg_value(cfg.pg_user) +
            " password=" + Config::pg_value(cfg.pg_password) +
            " connect_timeout=10 application_name=crypto_collector";
        writer = std::make_unique<PgWriter>(connstr, g_queue);
        writer_thread = std::thread([&] { writer->run(); });
    }

    const int fan_port = g_opt.fanout_port >= 0 ? g_opt.fanout_port : fanout::port_from_env("CRYPTO_FANOUT_PORT", 8197);
    if (fan_port > 0) {
        g_fan = std::make_unique<fanout::Server>("crypto", fan_port, [](const std::string& s) { logf("%s", s.c_str()); });
        g_fan->start();
    }
    logf("crypto_collector starting: binance=%d bitstamp=%d dry_run=%d fanout_port=%d", g_opt.binance, g_opt.bitstamp,
         g_opt.dry_run, fan_port);

    asio::io_context ioc(1);
    ssl::context ctx(ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);

    auto on_done = [](std::exception_ptr e) {
        if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); }
    };
    if (g_opt.binance) {
        asio::co_spawn(ioc, run_forever("binance trades", [&]() {
            return binance_session(ctx, "/market/stream?streams=btcusdt@aggTrade/ethusdt@aggTrade", true); }),
            on_done);
        asio::co_spawn(ioc, run_forever("binance quotes", [&]() {
            return binance_session(ctx, "/public/stream?streams=btcusdt@bookTicker/ethusdt@bookTicker", false); }),
            on_done);
    }
    if (g_opt.bitstamp)
        asio::co_spawn(ioc, run_forever("bitstamp", [&]() { return bitstamp_session(ctx); }), on_done);
    asio::co_spawn(ioc, housekeeping(), on_done);

    asio::signal_set sigs(ioc, SIGINT, SIGTERM);
    sigs.async_wait([&](const boost::system::error_code&, int sig) {
        logf("signal %d — stopping", sig); ioc.stop(); });
    asio::steady_timer limit(ioc);
    if (g_opt.duration_s > 0) {
        limit.expires_after(std::chrono::seconds(g_opt.duration_s));
        limit.async_wait([&](const boost::system::error_code& ec) {
            if (!ec) { logf("--duration %d s reached — stopping", g_opt.duration_s); ioc.stop(); } });
    }

    ioc.run();

    for (auto& p : g_pairs) if (p->raw) p->raw->close();    // end the zstd frames cleanly
    if (writer) { g_queue.stop(); writer_thread.join(); }
    if (g_fan) g_fan->stop();
    logf("final: binance trades=%lld quotes=%lld kept=%lld | bitstamp msgs=%lld events=%lld skipped=%lld "
         "raw_bytes_in=%lld | written ticks=%lld bbo=%lld mbo_1m=%lld | dropped %lld/%lld/%lld",
         (long long)g_stats.bn_trade_msgs, (long long)g_stats.bn_quote_msgs, (long long)g_stats.bn_quotes_kept,
         (long long)g_stats.bs_msgs, (long long)g_stats.bs_events, (long long)g_stats.bs_skipped,
         (long long)g_stats.raw_bytes_in, (long long)g_stats.ticks_written, (long long)g_stats.bbo_written,
         (long long)g_stats.mbo_written, (long long)g_stats.ticks_dropped, (long long)g_stats.bbo_dropped,
         (long long)g_stats.mbo_dropped);
    return 0;
}
