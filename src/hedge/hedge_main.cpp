/*  ═══════════════════════════════════════════════════════════════════════════
    hedge_main.cpp — cross-account hedge coordinator

    Reads hedge_pairs (dashboard-managed bindings: anchor account → hedge account)
    and keeps each hedge account's exchange net position at

        target = clamp(round(-anchor_net * ratio), -max_qty, +max_qty)

    summed over the enabled pairs sharing that hedge account.

    Anchor feed: live_account_net, upserted by the anchor's nq_executor on every
    tid=451 AccountPnLPositionUpdate (exchange truth, no extra Rithmic session).
    Hedge leg: the coordinator opens its OWN ORDER_PLANT + PNL_PLANT sessions on
    the hedge account (credentials from config/<label>_config.json, same loader
    as the executor) and trades it to target with aggressive LIMIT orders
    (prop routes may reject MARKET — see executor_main.cpp send_unwind).

    Operational rules:
      • A hedge account must be DEDICATED to hedging — no nq_executor may run on
        it (two processes would fight over the same Rithmic sessions).
      • A Postgres advisory lock ("hedge_coordinator:<label>") keeps two
        coordinators off the same hedge account.
      • Disabling a pair flattens its contribution (target → 0).
      • Shutdown never auto-flattens: open hedge positions are logged, not closed.

    Build:  cd build && cmake .. && make hedge_coordinator -j4
    Usage:  ./hedge_coordinator [--config-dir config] [--poll-ms 500] [--dry-run]
    ═══════════════════════════════════════════════════════════════════════════ */
#include "../execution/orb_config.hpp"   // OrbConfig (hedge account creds) + NQ_TICK_SIZE
#include "log.hpp"

#include "rithmic.pb.h"

#include <libpq-fe.h>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace asio      = boost::asio;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace ssl       = asio::ssl;
using tcp           = asio::ip::tcp;

static std::atomic<bool> g_running{true};

static void handle_signal(int) { g_running = false; }

// ─── Framing / heartbeat helpers (same wire protocol as executor_main.cpp) ────
template <class Msg>
static std::string proto_frame(const Msg& msg) {
    std::string payload = msg.SerializeAsString();
    uint32_t sz = static_cast<uint32_t>(payload.size());
    uint32_t be = __builtin_bswap32(sz);
    std::string wire(reinterpret_cast<char*>(&be), 4);
    wire += payload;
    return wire;
}

static std::string proto_strip(const std::string& wire) {
    if (wire.size() < 4) throw std::runtime_error("Message too short");
    return wire.substr(4);
}

static int32_t hb_ssboe_now() {
    int64_t secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (secs > INT32_MAX) secs = INT32_MAX;
    return static_cast<int32_t>(secs);
}

static int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ─── WebSocket helpers ────────────────────────────────────────────────────────
using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

static asio::awaitable<std::unique_ptr<WsStream>>
connect_ws(asio::io_context& ioc, ssl::context& ssl_ctx, const std::string& url) {
    std::string host, port;
    std::string u = url;
    if (u.substr(0, 6) == "wss://") u = u.substr(6);
    auto colon = u.find(':');
    if (colon != std::string::npos) {
        host = u.substr(0, colon);
        port = u.substr(colon + 1);
    } else { host = u; port = "443"; }

    auto ex = co_await asio::this_coro::executor;
    tcp::resolver resolver(ex);
    auto results = co_await resolver.async_resolve(host, port, asio::use_awaitable);
    auto ws = std::make_unique<WsStream>(ex, ssl_ctx);
    co_await beast::get_lowest_layer(*ws).async_connect(results, asio::use_awaitable);
    beast::get_lowest_layer(*ws).socket().set_option(asio::ip::tcp::no_delay(true));
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SSL SNI failed");
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, asio::use_awaitable);
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req){
        req.set(beast::http::field::user_agent, "hedge_coordinator/1.0");
    }));
    co_await ws->async_handshake(host + ":" + port, "/", asio::use_awaitable);
    co_return ws;
}

// Serialized per-stream write queue (Beast forbids overlapping async_write) —
// same shape as the executor's WsWriteQueue.
class WsWriteQueue {
public:
    void attach(WsStream* ws) { ws_ = ws; }
    void detach() {
        ws_ = nullptr;
        fail_all(asio::error::make_error_code(asio::error::not_connected));
    }
    void enqueue(std::string data) {
        submit(std::move(data), [](boost::system::error_code ec) {
            if (ec) LOG("[HEDGE-WS] queued write failed: %s", ec.message().c_str());
        });
    }

private:
    struct MoveOnlyHandler {
        struct Base { virtual void call(boost::system::error_code) = 0; virtual ~Base() = default; };
        template <class F> struct Impl : Base {
            F f; explicit Impl(F&& fn) : f(std::move(fn)) {}
            void call(boost::system::error_code ec) override { std::move(f)(ec); }
        };
        std::unique_ptr<Base> p;
        MoveOnlyHandler() = default;
        template <class F, class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, MoveOnlyHandler>>>
        MoveOnlyHandler(F&& f) : p(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}
        void operator()(boost::system::error_code ec) { p->call(ec); }
    };
    using Handler = MoveOnlyHandler;
    struct Item { std::string data; Handler handler; };

    void submit(std::string data, Handler handler) {
        if (!ws_) { handler(asio::error::make_error_code(asio::error::not_connected)); return; }
        q_.push_back(Item{std::move(data), std::move(handler)});
        if (!busy_) pump();
    }
    void pump() {
        if (!ws_ || q_.empty()) { busy_ = false; return; }
        busy_ = true;
        ws_->binary(true);
        ws_->async_write(asio::buffer(q_.front().data),
            [this](boost::system::error_code ec, std::size_t) {
                Item item = std::move(q_.front());
                q_.pop_front();
                item.handler(ec);
                if (ec) { fail_all(ec); busy_ = false; return; }
                pump();
            });
    }
    void fail_all(boost::system::error_code ec) {
        while (!q_.empty()) { Handler h = std::move(q_.front().handler); q_.pop_front(); h(ec); }
    }

    WsStream*        ws_   = nullptr;
    bool             busy_ = false;
    std::deque<Item> q_;
};

static asio::awaitable<void> sleep_ms(int ms) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer t(ex, std::chrono::milliseconds(ms));
    co_await t.async_wait(asio::use_awaitable);
}

// ─── DB layer ─────────────────────────────────────────────────────────────────
struct HedgePair {
    int         id = 0;
    std::string anchor_label;
    std::string hedge_label;
    std::string symbol;
    double      ratio   = 1.0;
    int         max_qty = 10;
    bool        enabled = false;
};

class HedgeDB {
public:
    explicit HedgeDB(const std::string& connstr) : connstr_(connstr) {
        conn_ = PQconnectdb(connstr_.c_str());
        if (!conn_ || PQstatus(conn_) != CONNECTION_OK)
            throw std::runtime_error(std::string("HedgeDB connect failed: ") +
                                     (conn_ ? PQerrorMessage(conn_) : "null conn"));
    }
    ~HedgeDB() { if (conn_) PQfinish(conn_); }

    bool is_connected() const { return conn_ && PQstatus(conn_) == CONNECTION_OK; }
    void reconnect() {
        PQreset(conn_);
        if (!is_connected()) LOG("[HEDGE-DB] reconnect failed: %s", PQerrorMessage(conn_));
    }

    void ensure_schema() {
        exec(R"(
            CREATE TABLE IF NOT EXISTS hedge_pairs (
                id              SERIAL PRIMARY KEY,
                anchor_label    TEXT NOT NULL,
                hedge_label     TEXT NOT NULL,
                symbol          TEXT NOT NULL DEFAULT 'MNQ',
                ratio           DOUBLE PRECISION NOT NULL DEFAULT 1.0,
                max_qty         INT NOT NULL DEFAULT 10,
                enabled         BOOLEAN NOT NULL DEFAULT FALSE,
                created_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
                UNIQUE (anchor_label, hedge_label, symbol),
                CHECK (anchor_label <> hedge_label),
                CHECK (ratio > 0),
                CHECK (max_qty > 0)
            ))");
        exec(R"(
            CREATE TABLE IF NOT EXISTS hedge_status (
                pair_id             INT PRIMARY KEY REFERENCES hedge_pairs(id) ON DELETE CASCADE,
                anchor_net          INT NOT NULL DEFAULT 0,
                hedge_net           INT NOT NULL DEFAULT 0,
                target_net          INT NOT NULL DEFAULT 0,
                anchor_feed_age_ms  INT,
                last_action         TEXT,
                last_error          TEXT,
                updated_at          TIMESTAMPTZ NOT NULL DEFAULT NOW()
            ))");
        exec(R"(
            CREATE TABLE IF NOT EXISTS live_account_net (
                account_label   TEXT PRIMARY KEY,
                net_qty         INT NOT NULL DEFAULT 0,
                balance         DOUBLE PRECISION,
                day_pnl         DOUBLE PRECISION,
                updated_at      TIMESTAMPTZ NOT NULL DEFAULT NOW()
            ))");
    }

    // One coordinator per hedge account (session-level lock; released on disconnect).
    bool acquire_account_lock(const std::string& hedge_label) {
        int64_t key = lock_key(hedge_label);
        std::string ks = std::to_string(key);
        const char* params[1] = { ks.c_str() };
        PGresult* res = query("SELECT pg_try_advisory_lock($1::bigint)", 1, params);
        if (!res) return false;
        bool ok = PQntuples(res) > 0 && PQgetvalue(res, 0, 0)[0] == 't';
        PQclear(res);
        return ok;
    }
    void release_account_lock(const std::string& hedge_label) {
        std::string ks = std::to_string(lock_key(hedge_label));
        const char* params[1] = { ks.c_str() };
        PGresult* res = query("SELECT pg_advisory_unlock($1::bigint)", 1, params);
        if (res) PQclear(res);
    }

    std::vector<HedgePair> load_pairs() {
        std::vector<HedgePair> out;
        PGresult* res = query(
            "SELECT id, anchor_label, hedge_label, symbol, ratio, max_qty, enabled"
            " FROM hedge_pairs ORDER BY id", 0, nullptr);
        if (!res) return out;
        for (int i = 0; i < PQntuples(res); ++i) {
            HedgePair p;
            p.id          = std::atoi(PQgetvalue(res, i, 0));
            p.anchor_label = PQgetvalue(res, i, 1);
            p.hedge_label  = PQgetvalue(res, i, 2);
            p.symbol       = PQgetvalue(res, i, 3);
            p.ratio        = std::atof(PQgetvalue(res, i, 4));
            p.max_qty      = std::atoi(PQgetvalue(res, i, 5));
            p.enabled      = PQgetvalue(res, i, 6)[0] == 't';
            out.push_back(std::move(p));
        }
        PQclear(res);
        return out;
    }

    // Anchor exposure feed written by nq_executor on every tid=451.
    // Returns {net_qty, feed_age_ms}; nullopt when no row exists.
    std::optional<std::pair<int, int64_t>> anchor_feed(const std::string& label) {
        const char* params[1] = { label.c_str() };
        PGresult* res = query(
            "SELECT net_qty, (EXTRACT(EPOCH FROM (NOW() - updated_at)) * 1000)::bigint"
            " FROM live_account_net WHERE account_label=$1", 1, params);
        if (!res) return std::nullopt;
        std::optional<std::pair<int, int64_t>> out;
        if (PQntuples(res) > 0)
            out = { std::atoi(PQgetvalue(res, 0, 0)), std::atoll(PQgetvalue(res, 0, 1)) };
        PQclear(res);
        return out;
    }

    // Reference price for aggressive-limit hedge orders: the anchor executor's
    // last reported price. 0.0 when nothing fresh is available (→ MARKET order).
    double ref_price(const std::string& anchor_label) {
        const char* params[1] = { anchor_label.c_str() };
        PGresult* res = query(
            "SELECT current_price FROM live_position"
            " WHERE account_label=$1 AND current_price IS NOT NULL AND current_price > 0"
            " ORDER BY last_updated DESC NULLS LAST, session_date DESC LIMIT 1", 1, params);
        if (!res) return 0.0;
        double px = 0.0;
        if (PQntuples(res) > 0) px = std::atof(PQgetvalue(res, 0, 0));
        PQclear(res);
        return px;
    }

    // action: nullptr → keep, else replace.  error: nullptr → keep, "" → clear, else set.
    // hedge_known=false → hedge_net column keeps its last value (coordinator has no
    // exchange truth yet — writing 0 would falsely read as "flat").
    void write_status(int pair_id, int anchor_net, bool hedge_known, int hedge_net,
                      int target_net, int64_t anchor_age_ms,
                      const char* action, const char* error) {
        char an[16], hn[16], tn[16], age[24];
        snprintf(an,  sizeof(an),  "%d", anchor_net);
        snprintf(hn,  sizeof(hn),  "%d", hedge_net);
        snprintf(tn,  sizeof(tn),  "%d", target_net);
        snprintf(age, sizeof(age), "%lld", (long long)anchor_age_ms);
        const char* age_param = anchor_age_ms >= 0 ? age : nullptr;
        const char* hn_param  = hedge_known ? hn : nullptr;
        char pid[16];
        snprintf(pid, sizeof(pid), "%d", pair_id);
        const char* params[7] = { pid, an, hn_param, tn, age_param, action, error };
        exec_params(
            "INSERT INTO hedge_status (pair_id, anchor_net, hedge_net, target_net,"
            " anchor_feed_age_ms, last_action, last_error, updated_at)"
            " VALUES ($1::int, $2::int, $3::int, $4::int, $5::bigint, $6, $7, NOW())"
            " ON CONFLICT (pair_id) DO UPDATE SET"
            "  anchor_net=EXCLUDED.anchor_net,"
            "  hedge_net=COALESCE(EXCLUDED.hedge_net, hedge_status.hedge_net),"
            "  target_net=EXCLUDED.target_net, anchor_feed_age_ms=EXCLUDED.anchor_feed_age_ms,"
            "  last_action=COALESCE(EXCLUDED.last_action, hedge_status.last_action),"
            "  last_error=CASE WHEN $7 IS NULL THEN hedge_status.last_error ELSE EXCLUDED.last_error END,"
            "  updated_at=NOW()",
            7, params);
    }

    void write_account_net(const std::string& label, int net_qty,
                           double balance, double day_pnl) {
        char nq[16], bal[32], dpnl[32];
        snprintf(nq, sizeof(nq), "%d", net_qty);
        const char* bal_param  = nullptr;
        const char* dpnl_param = nullptr;
        if (!std::isnan(balance)) { snprintf(bal,  sizeof(bal),  "%.4f", balance); bal_param  = bal; }
        if (!std::isnan(day_pnl)) { snprintf(dpnl, sizeof(dpnl), "%.4f", day_pnl); dpnl_param = dpnl; }
        const char* params[4] = { label.c_str(), nq, bal_param, dpnl_param };
        exec_params(
            "INSERT INTO live_account_net (account_label, net_qty, balance, day_pnl, updated_at)"
            " VALUES ($1, $2::int, $3::double precision, $4::double precision, NOW())"
            " ON CONFLICT (account_label) DO UPDATE SET"
            "  net_qty=EXCLUDED.net_qty, balance=EXCLUDED.balance,"
            "  day_pnl=EXCLUDED.day_pnl, updated_at=NOW()",
            4, params);
    }

    // Same row shape as OrbDB::write_order_event — the audit trail for hedge orders.
    void write_order_event(const std::string& account_label, const char* kind,
                           const std::string& basket_id, const std::string& user_tag,
                           int notify_type, const std::string& status, const char* side,
                           int qty, double price, double fill_price, int fill_qty,
                           int total_fill, const std::string& rp_code,
                           const std::string& detail) {
        char nt[16], q[16], px[32], fpx[32], fq[16], tf[16];
        snprintf(nt,  sizeof(nt),  "%d", notify_type);
        snprintf(q,   sizeof(q),   "%d", qty);
        snprintf(px,  sizeof(px),  "%.4f", price);
        snprintf(fpx, sizeof(fpx), "%.4f", fill_price);
        snprintf(fq,  sizeof(fq),  "%d", fill_qty);
        snprintf(tf,  sizeof(tf),  "%d", total_fill);
        const char* params[16] = {
            account_label.c_str(), "HEDGE", kind,
            basket_id.empty() ? nullptr : basket_id.c_str(),
            nullptr,  // orig_basket_id
            user_tag.empty() ? nullptr : user_tag.c_str(),
            nt, status.empty() ? nullptr : status.c_str(), side,
            q, px, fpx, fq, tf,
            rp_code.empty() ? nullptr : rp_code.c_str(),
            detail.empty() ? nullptr : detail.c_str() };
        exec_params(
            "INSERT INTO live_order_events (account_label, strategy, kind, basket_id, orig_basket_id,"
            " user_tag, notify_type, status, side, qty, price, fill_price, fill_qty, total_fill,"
            " rp_code, detail) VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16)",
            16, params);
    }

private:
    static int64_t lock_key(const std::string& hedge_label) {
        const std::string s = "hedge_coordinator:" + hedge_label;
        uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : s) { h ^= c; h *= 1099511628211ULL; }
        return (int64_t)h;
    }
    void exec(const char* sql) {
        if (!is_connected()) reconnect();
        PGresult* res = PQexec(conn_, sql);
        if (!res) throw std::runtime_error(std::string("PQexec null: ") + PQerrorMessage(conn_));
        ExecStatusType st = PQresultStatus(res);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error("[HEDGE-DB] SQL error: " + err);
        }
        PQclear(res);
    }
    void exec_params(const char* sql, int n, const char* const* params) {
        if (!is_connected()) reconnect();
        if (!is_connected()) return;
        PGresult* res = PQexecParams(conn_, sql, n, nullptr, params, nullptr, nullptr, 0);
        if (!res) { LOG("[HEDGE-DB] exec_params null: %s", PQerrorMessage(conn_)); return; }
        ExecStatusType st = PQresultStatus(res);
        if (st != PGRES_COMMAND_OK && st != PGRES_TUPLES_OK)
            LOG("[HEDGE-DB] exec_params failed: %s", PQerrorMessage(conn_));
        PQclear(res);
    }
    PGresult* query(const char* sql, int n, const char* const* params) {
        if (!is_connected()) reconnect();
        if (!is_connected()) return nullptr;
        PGresult* res = PQexecParams(conn_, sql, n, nullptr, params, nullptr, nullptr, 0);
        if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
            if (res) PQclear(res);
            return nullptr;
        }
        return res;
    }

    std::string connstr_;
    PGconn*     conn_ = nullptr;
};

// ─── Hedge account session state ──────────────────────────────────────────────
struct HedgeAcct {
    std::string label;
    OrbConfig   cfg;

    std::unique_ptr<WsStream> op_ws;
    std::unique_ptr<WsStream> pnl_ws;
    WsWriteQueue              op_q;
    WsWriteQueue              pnl_q;
    bool                      op_connected  = false;
    bool                      pnl_connected = false;
    bool                      broken        = false;   // any fatal read/write → reconnect
    int                       reconnect_delay_s = 10;

    std::string fcm_id;
    std::string ib_id;
    std::string trade_route;
    std::string trade_symbol;

    // Exchange truth for the hedge account (tid=451). unknown until first update.
    int     hedge_net      = INT_MIN;
    int64_t last_451_ms    = 0;

    // At most one working hedge order per account.
    bool        inflight        = false;
    std::string inflight_tag;
    std::string inflight_basket;   // server basket id once tid=351 maps it
    int64_t     inflight_sent_ms = 0;
    bool        cancel_sent     = false;
    int64_t     last_order_ms   = 0;

    std::string last_action;
    std::string last_error;

    int  sim_net = 0;   // --dry-run only
};

static void acct_teardown(HedgeAcct& a) {
    a.op_q.detach();
    a.pnl_q.detach();
    if (a.op_ws)  { try { a.op_ws->close(websocket::close_code::normal); }  catch (...) {} }
    if (a.pnl_ws) { try { a.pnl_ws->close(websocket::close_code::normal); } catch (...) {} }
    a.op_ws.reset();
    a.pnl_ws.reset();
    a.op_connected = a.pnl_connected = false;
    // The order we had in flight may or may not be working; the tid=351 startup
    // snapshot after reconnect cancels any leftover HEDGE-* order, and tid=451
    // re-anchors the net. Do not trade until both arrive.
    a.inflight   = false;
    a.hedge_net  = INT_MIN;
}

// ─── Order send ───────────────────────────────────────────────────────────────
// Aggressive LIMIT crossing the spread (50 ticks), MARKET only with no price
// reference — mirrors the executor's send_unwind rationale.
static void send_hedge_order(HedgeDB& db, HedgeAcct& a, int diff, double ref_px) {
    const bool is_buy = diff > 0;
    int qty = std::min(std::abs(diff), 50);
    a.inflight_tag = "HEDGE-" + a.label + "-" + std::to_string(now_ms());
    a.last_action  = std::string(is_buy ? "BUY " : "SELL ") + std::to_string(qty);

    if (!a.op_connected || !a.op_ws) {
        LOG("[HEDGE] %s: cannot send %s — order plant down", a.label.c_str(), a.last_action.c_str());
        a.last_error = "order plant disconnected";
        return;
    }

    int    order_type = 2;  // MARKET
    double limit_px   = 0.0;
    if (ref_px > 0.0) {
        order_type = 1;     // LIMIT
        limit_px   = is_buy ? ref_px + 50 * NQ_TICK_SIZE
                            : ref_px - 50 * NQ_TICK_SIZE;
    } else {
        LOG("[HEDGE] %s: WARNING no price reference — sending MARKET", a.label.c_str());
    }

    rti::RequestNewOrder req;
    req.set_template_id(312);
    req.set_fcm_id(a.fcm_id);
    req.set_ib_id(a.ib_id);
    req.set_account_id(a.cfg.account_id);
    req.set_symbol(a.trade_symbol);
    req.set_exchange(a.cfg.exchange);
    req.set_quantity(qty);
    req.set_order_type(order_type);
    if (order_type == 1) req.set_price(limit_px);
    req.set_transaction_type(is_buy ? 1 : 2);
    req.set_user_tag(a.inflight_tag);
    req.set_duration(rti::RequestNewOrder::DAY);
    req.set_manual_or_auto_select(rti::RequestNewOrder::AUTO);
    req.set_trade_route(a.trade_route);

    a.op_q.enqueue(proto_frame(req));
    a.inflight         = true;
    a.inflight_basket.clear();
    a.inflight_sent_ms = now_ms();
    a.cancel_sent      = false;
    a.last_order_ms    = a.inflight_sent_ms;
    if (order_type == 1) {
        char px[32]; snprintf(px, sizeof(px), " @ LMT %.2f", limit_px);
        a.last_action += px;
    } else {
        a.last_action += " @ MKT";
    }
    LOG("[HEDGE] %s: sent %s tag=%s acct=%s route='%s'",
        a.label.c_str(), a.last_action.c_str(), a.inflight_tag.c_str(),
        a.cfg.account_id.c_str(), a.trade_route.c_str());
    db.write_order_event(a.label, "new_order_sent", "", a.inflight_tag, 312, "",
                         is_buy ? "BUY" : "SELL", qty, limit_px, 0.0, 0, 0, "", a.trade_symbol);
}

// ─── Login helper shared by both plants ───────────────────────────────────────
// Returns rp_code "0" on success. On failure sets the reconnect delay:
// rp_code=13 (duplicate session / rapid login) → long cadence, hammering keeps it alive.
static asio::awaitable<bool> plant_login(WsStream& ws, const OrbConfig& cfg,
                                         rti::RequestLogin_SysInfraType infra,
                                         const char* plant_name, HedgeAcct& a,
                                         std::string* fcm_out, std::string* ib_out) {
    rti::RequestLogin req;
    req.set_template_id(10);
    req.set_template_version("3.9");
    req.set_user(cfg.rithmic_user);
    req.set_password(cfg.rithmic_password);
    req.set_system_name(cfg.rithmic_system_name);
    req.set_app_name(cfg.app_name + "-HEDGE");
    req.set_app_version(cfg.app_version);
    req.set_infra_type(infra);

    ws.binary(true);
    co_await ws.async_write(asio::buffer(proto_frame(req)), asio::use_awaitable);

    beast::flat_buffer buf;
    beast::get_lowest_layer(ws).expires_after(std::chrono::seconds(15));
    bool ok = false;
    for (;;) {
        buf.clear();
        co_await ws.async_read(buf, asio::use_awaitable);
        auto payload = proto_strip(beast::buffers_to_string(buf.data()));
        rti::Base base;
        if (!base.ParseFromString(payload)) continue;
        if (base.template_id() != 11) continue;
        rti::ResponseLogin resp;
        if (!resp.ParseFromString(payload)) continue;
        ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
        if (!ok) {
            std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
            std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
            LOG("[HEDGE] %s: %s login REJECTED (rp_code=%s %s)",
                a.label.c_str(), plant_name, rpc.c_str(), txt.c_str());
            a.last_error = std::string(plant_name) + " login rejected rp=" + rpc;
            a.reconnect_delay_s = 300;
        } else {
            if (fcm_out && !resp.fcm_id().empty()) *fcm_out = resp.fcm_id();
            if (ib_out  && !resp.ib_id().empty())  *ib_out  = resp.ib_id();
            LOG("[HEDGE] %s: %s login OK", a.label.c_str(), plant_name);
        }
        break;
    }
    beast::get_lowest_layer(ws).expires_never();
    if (ok) {
        // Server requires an immediate heartbeat after login.
        rti::RequestHeartbeat hb;
        hb.set_template_id(18);
        hb.set_ssboe(hb_ssboe_now());
        ws.binary(true);
        co_await ws.async_write(asio::buffer(proto_frame(hb)), asio::use_awaitable);
    }
    co_return ok;
}

// ─── Per-account session (ORDER_PLANT + PNL_PLANT) ────────────────────────────
static asio::awaitable<void> run_session(HedgeDB& db, HedgeAcct& a,
                                         ssl::context& ssl_ctx) {
    a.reconnect_delay_s = 10;
    a.fcm_id = a.cfg.fcm_id;
    a.ib_id  = a.cfg.ib_id;
    a.trade_route  = a.cfg.trade_route;
    a.trade_symbol = a.cfg.trade_contract.empty() ? a.cfg.symbol : a.cfg.trade_contract;

    // One coordinator per hedge account.
    if (!db.acquire_account_lock(a.label)) {
        LOG("[HEDGE] %s: FATAL another hedge_coordinator holds this account — retry in 60s",
            a.label.c_str());
        a.reconnect_delay_s = 60;
        co_return;
    }
    struct LockGuard {
        HedgeDB& db; const std::string& label;
        ~LockGuard() { db.release_account_lock(label); }
    } lock_guard{db, a.label};

    auto ex = co_await asio::this_coro::executor;
    asio::io_context& ioc = static_cast<asio::io_context&>(ex.context());

    // ── ORDER_PLANT ─────────────────────────────────────────────────────────
    a.op_ws = co_await connect_ws(ioc, ssl_ctx, a.cfg.rithmic_url);
    if (!co_await plant_login(*a.op_ws, a.cfg, rti::RequestLogin::ORDER_PLANT,
                              "ORDER_PLANT", a, &a.fcm_id, &a.ib_id)) {
        acct_teardown(a);
        co_return;
    }
    a.op_q.attach(a.op_ws.get());
    a.op_connected = true;

    // Verify the configured account against the login's account list (tid=302 → 303)
    // and adopt the fcm/ib Rithmic reports for it — config values are only a fallback.
    {
        rti::RequestAccountList al_req;
        al_req.set_template_id(302);
        if (!a.fcm_id.empty()) al_req.set_fcm_id(a.fcm_id);
        if (!a.ib_id.empty())  al_req.set_ib_id(a.ib_id);
        al_req.set_user_type(3);  // USER_TYPE_TRADER
        a.op_q.enqueue(proto_frame(al_req));

        beast::flat_buffer buf;
        bool matched = false, terminal = false;
        for (int i = 0; i < 50 && !terminal; ++i) {
            buf.clear();
            beast::get_lowest_layer(*a.op_ws).expires_after(std::chrono::seconds(5));
            try {
                co_await a.op_ws->async_read(buf, asio::use_awaitable);
            } catch (...) { break; }
            beast::get_lowest_layer(*a.op_ws).expires_never();
            auto pl = proto_strip(beast::buffers_to_string(buf.data()));
            rti::Base base; if (!base.ParseFromString(pl)) continue;
            if (base.template_id() != 303) continue;
            rti::ResponseAccountList al;
            if (!al.ParseFromString(pl)) continue;
            if (al.account_id() == a.cfg.account_id) {
                matched = true;
                a.fcm_id = al.fcm_id();
                a.ib_id  = al.ib_id();
            }
            if (!al.rp_code().empty()) terminal = true;
        }
        if (!matched) {
            LOG("[HEDGE] %s: FATAL account_id '%s' not confirmed in login's account list — "
                "orders would be refused (rp_code=1088). Check %s_ACCOUNT.",
                a.label.c_str(), a.cfg.account_id.c_str(), a.cfg.order_env_prefix.c_str());
            a.last_error = "account not in login's account list";
            a.reconnect_delay_s = 300;
            acct_teardown(a);
            co_return;
        }
    }

    // Resolve the trade route (tid=310 → 311). NEVER adopt "Rithmic Order Routing" —
    // on Legends it silently cancels every order.
    {
        rti::RequestTradeRoutes tr_req;
        tr_req.set_template_id(310);
        tr_req.set_subscribe_for_updates(false);
        a.op_q.enqueue(proto_frame(tr_req));

        beast::flat_buffer buf;
        bool route_found = false;
        for (int i = 0; i < 50; ++i) {
            buf.clear();
            beast::get_lowest_layer(*a.op_ws).expires_after(std::chrono::seconds(5));
            try {
                co_await a.op_ws->async_read(buf, asio::use_awaitable);
            } catch (...) { break; }
            beast::get_lowest_layer(*a.op_ws).expires_never();
            auto pl = proto_strip(beast::buffers_to_string(buf.data()));
            rti::Base base; if (!base.ParseFromString(pl)) continue;
            if (base.template_id() != 311) continue;
            rti::ResponseTradeRoutes tr;
            if (!tr.ParseFromString(pl)) continue;
            if (tr.exchange() == a.cfg.exchange && !tr.trade_route().empty() &&
                tr.trade_route() != "Rithmic Order Routing") {
                if (!route_found || tr.is_default()) {
                    a.trade_route = tr.trade_route();
                    route_found = true;
                }
            }
            if (!tr.rp_code().empty()) break;
        }
        LOG("[HEDGE] %s: trade_route='%s'%s", a.label.c_str(), a.trade_route.c_str(),
            route_found ? "" : " (config fallback — no route list)");
    }

    // Subscribe to order updates (tid=308) — without it no 351/352 notifications arrive.
    {
        rti::RequestSubscribeForOrderUpdates sub;
        sub.set_template_id(308);
        sub.set_fcm_id(a.fcm_id);
        sub.set_ib_id(a.ib_id);
        sub.set_account_id(a.cfg.account_id);
        a.op_q.enqueue(proto_frame(sub));
    }

    // ── PNL_PLANT ───────────────────────────────────────────────────────────
    try {
        a.pnl_ws = co_await connect_ws(ioc, ssl_ctx, a.cfg.rithmic_url);
    } catch (std::exception& e) {
        LOG("[HEDGE] %s: PNL_PLANT connect failed: %s", a.label.c_str(), e.what());
        acct_teardown(a);
        co_return;
    }
    if (!co_await plant_login(*a.pnl_ws, a.cfg, rti::RequestLogin::PNL_PLANT,
                              "PNL_PLANT", a, nullptr, nullptr)) {
        acct_teardown(a);
        co_return;
    }
    a.pnl_q.attach(a.pnl_ws.get());
    a.pnl_connected = true;
    {
        rti::RequestPnLPositionUpdates sub;
        sub.set_template_id(400);
        sub.set_request(rti::RequestPnLPositionUpdates::SUBSCRIBE);
        sub.set_fcm_id(a.fcm_id);
        sub.set_ib_id(a.ib_id);
        sub.set_account_id(a.cfg.account_id);
        a.pnl_q.enqueue(proto_frame(sub));
        // Force an initial snapshot — the subscription only streams on change.
        rti::RequestPnLPositionSnapshot snap;
        snap.set_template_id(402);
        snap.set_fcm_id(a.fcm_id);
        snap.set_ib_id(a.ib_id);
        snap.set_account_id(a.cfg.account_id);
        a.pnl_q.enqueue(proto_frame(snap));
    }
    LOG("[HEDGE] %s: session up (acct=%s contract=%s route='%s')",
        a.label.c_str(), a.cfg.account_id.c_str(), a.trade_symbol.c_str(),
        a.trade_route.c_str());

    // ── Read loops + heartbeat (detached; `broken` ends the session) ─────────
    auto op_loop = [&db, &a]() -> asio::awaitable<void> {
        beast::flat_buffer buf;
        while (g_running && !a.broken && a.op_ws) {
            buf.clear();
            try {
                co_await a.op_ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                if (g_running && !a.broken)
                    LOG("[HEDGE] %s: ORDER_PLANT read error: %s — reconnecting",
                        a.label.c_str(), e.what());
                a.broken = true;
                co_return;
            }
            std::string payload;
            try { payload = proto_strip(beast::buffers_to_string(buf.data())); }
            catch (...) { continue; }
            rti::Base base;
            if (!base.ParseFromString(payload)) continue;
            const int tid = base.template_id();

            if (tid == 18) {           // server heartbeat → respond
                rti::ResponseHeartbeat hb;
                hb.set_template_id(19);
                a.op_q.enqueue(proto_frame(hb));
            } else if (tid == 19 || tid == 309) {
                // heartbeat ack / order-update subscription ack
            } else if (tid == 313 || tid == 315) {
                rti::ResponseNewOrder resp;
                if (!resp.ParseFromString(payload)) continue;
                const std::string rpc = resp.rp_code().empty() ? "" : resp.rp_code(0);
                db.write_order_event(a.label, rpc.empty() || rpc == "0" ? "gateway_ack" : "gateway_reject",
                                     resp.basket_id(), "", tid, "", "", 0, 0.0, 0.0, 0, 0, rpc, "");
                if (!rpc.empty() && rpc != "0") {
                    LOG("[HEDGE] %s: order REJECTED at gateway basket=%s rp=%s",
                        a.label.c_str(), resp.basket_id().c_str(), rpc.c_str());
                    a.last_error = "gateway reject rp=" + rpc;
                    a.inflight   = false;   // never reached the market
                }
            } else if (tid == 351) {
                rti::RithmicOrderNotification n;
                if (!n.ParseFromString(payload)) continue;
                db.write_order_event(a.label, "rithmic_notify", n.basket_id(), n.user_tag(),
                                     (int)n.notify_type(), n.status(), "",
                                     n.quantity(), n.price(), n.avg_fill_price(),
                                     0, n.total_fill_size(), "",
                                     n.is_snapshot() ? "snapshot" : "");
                if (n.is_snapshot()) {
                    // Leftover working HEDGE-* order from a crashed/killed session — cancel it.
                    if (n.user_tag().rfind("HEDGE-", 0) == 0 && n.total_unfilled_size() > 0 &&
                        !n.basket_id().empty()) {
                        LOG("[HEDGE] %s: startup snapshot shows working HEDGE order basket=%s "
                            "unfilled=%d — cancelling", a.label.c_str(), n.basket_id().c_str(),
                            n.total_unfilled_size());
                        rti::RequestCancelOrder cxl;
                        cxl.set_template_id(316);
                        cxl.set_basket_id(n.basket_id());
                        cxl.set_account_id(a.cfg.account_id);
                        cxl.set_fcm_id(a.fcm_id);
                        cxl.set_ib_id(a.ib_id);
                        cxl.set_manual_or_auto(2);  // AUTO — omitted → rp_code=1045 silent refusal
                        a.op_q.enqueue(proto_frame(cxl));
                    }
                    continue;
                }
                if (a.inflight && n.user_tag() == a.inflight_tag) {
                    if (!n.basket_id().empty()) a.inflight_basket = n.basket_id();
                    const int nt = (int)n.notify_type();
                    if (n.total_fill_size() > 0) {
                        LOG("[HEDGE] %s: hedge order FILLED tag=%s qty=%d avg_px=%.2f",
                            a.label.c_str(), a.inflight_tag.c_str(),
                            n.total_fill_size(), n.avg_fill_price());
                        char act[64];
                        snprintf(act, sizeof(act), "filled %d @ %.2f",
                                 n.total_fill_size(), n.avg_fill_price());
                        a.last_action = act;
                    }
                    if (nt == 15) {                     // COMPLETE (filled or cancelled out)
                        a.inflight = false;
                    } else if (nt == 3) {               // cancel ACK
                        if (a.cancel_sent) a.inflight = false;
                    } else if (nt == 17 || n.status() == "Cancellation Failed") {
                        a.cancel_sent = false;          // still working
                        LOG("[HEDGE] %s: cancel FAILED tag=%s — order still working",
                            a.label.c_str(), a.inflight_tag.c_str());
                    }
                }
            } else if (tid == 352) {
                rti::ExchangeOrderNotification n;
                if (!n.ParseFromString(payload)) continue;
                db.write_order_event(a.label, "exchange_notify", n.basket_id(), n.user_tag(),
                                     (int)n.notify_type(), n.status(), "",
                                     0, 0.0, n.fill_price(), n.fill_size(),
                                     n.total_fill_size(), "", "");
                const int nt = (int)n.notify_type();
                if (a.inflight && n.user_tag() == a.inflight_tag) {
                    if (!n.basket_id().empty()) a.inflight_basket = n.basket_id();
                    if (nt == 6) {          // REJECT
                        LOG("[HEDGE] %s: hedge order REJECTED by exchange: %s",
                            a.label.c_str(), n.status().c_str());
                        a.last_error = "exchange reject: " + n.status();
                        a.inflight   = false;
                    }
                }
            }
        }
    };

    auto pnl_loop = [&db, &a]() -> asio::awaitable<void> {
        beast::flat_buffer buf;
        int64_t last_snap_ms = now_ms();
        while (g_running && !a.broken && a.pnl_ws) {
            buf.clear();
            // Re-request a snapshot every 60 s as a safety net (451 streams on change only).
            if (now_ms() - last_snap_ms > 60000) {
                last_snap_ms = now_ms();
                rti::RequestPnLPositionSnapshot snap;
                snap.set_template_id(402);
                snap.set_fcm_id(a.fcm_id);
                snap.set_ib_id(a.ib_id);
                snap.set_account_id(a.cfg.account_id);
                a.pnl_q.enqueue(proto_frame(snap));
            }
            try {
                co_await a.pnl_ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                if (g_running && !a.broken)
                    LOG("[HEDGE] %s: PNL_PLANT read error: %s — reconnecting",
                        a.label.c_str(), e.what());
                a.broken = true;
                co_return;
            }
            std::string payload;
            try { payload = proto_strip(beast::buffers_to_string(buf.data())); }
            catch (...) { continue; }
            rti::Base base;
            if (!base.ParseFromString(payload)) continue;
            const int tid = base.template_id();
            if (tid == 18) {
                rti::ResponseHeartbeat hb;
                hb.set_template_id(19);
                a.pnl_q.enqueue(proto_frame(hb));
            } else if (tid == 451) {
                rti::AccountPnLPositionUpdate upd;
                if (!upd.ParseFromString(payload)) continue;
                if (upd.account_id() != a.cfg.account_id) continue;
                const int net = upd.net_quantity();
                if (net != a.hedge_net)
                    LOG("[HEDGE] %s: exchange net %d → %d", a.label.c_str(),
                        a.hedge_net == INT_MIN ? 0 : a.hedge_net, net);
                a.hedge_net   = net;
                a.last_451_ms = now_ms();
                auto to_d = [](const std::string& s) {
                    return s.empty() ? std::nan("") : std::strtod(s.c_str(), nullptr);
                };
                double bal  = to_d(upd.account_balance());
                double dpnl = to_d(upd.day_pnl());
                if (std::isnan(dpnl)) {
                    double o = to_d(upd.day_open_pnl()), c = to_d(upd.day_closed_pnl());
                    if (!std::isnan(o) || !std::isnan(c))
                        dpnl = (std::isnan(o) ? 0.0 : o) + (std::isnan(c) ? 0.0 : c);
                }
                db.write_account_net(a.label, net, bal, dpnl);
            }
        }
    };

    auto hb_loop = [&a]() -> asio::awaitable<void> {
        while (g_running && !a.broken) {
            co_await sleep_ms(30000);
            if (!g_running || a.broken) co_return;
            rti::RequestHeartbeat hb;
            hb.set_template_id(18);
            hb.set_ssboe(hb_ssboe_now());
            if (a.op_connected)  a.op_q.enqueue(proto_frame(hb));
            if (a.pnl_connected) a.pnl_q.enqueue(proto_frame(hb));
        }
    };

    auto ioc_ptr = &ioc;
    asio::co_spawn(*ioc_ptr, op_loop(), asio::detached);
    asio::co_spawn(*ioc_ptr, pnl_loop(), asio::detached);
    asio::co_spawn(*ioc_ptr, hb_loop(), asio::detached);

    // Session watchdog: return (→ supervisor reconnects) when broken or stopping.
    while (g_running && !a.broken) co_await sleep_ms(1000);
    acct_teardown(a);
}

// Supervisor: keep the account's session alive while the process runs.
static asio::awaitable<void> account_supervisor(HedgeDB& db,
                                                std::shared_ptr<HedgeAcct> acct,
                                                ssl::context& ssl_ctx) {
    while (g_running) {
        acct->broken = false;
        try {
            co_await run_session(db, *acct, ssl_ctx);
        } catch (std::exception& e) {
            LOG("[HEDGE] %s: session error: %s", acct->label.c_str(), e.what());
            acct->last_error = e.what();
            acct_teardown(*acct);
        }
        if (!g_running) break;
        LOG("[HEDGE] %s: reconnecting in %ds", acct->label.c_str(), acct->reconnect_delay_s);
        co_await sleep_ms(acct->reconnect_delay_s * 1000);
    }
}

// ─── Convergence loop ─────────────────────────────────────────────────────────
static asio::awaitable<void> convergence_loop(HedgeDB& db, ssl::context& ssl_ctx,
                                              const std::string& config_dir,
                                              int poll_ms, bool dry_run) {
    std::map<std::string, std::shared_ptr<HedgeAcct>> accts;
    auto ex = co_await asio::this_coro::executor;
    asio::io_context& ioc = static_cast<asio::io_context&>(ex.context());

    while (g_running) {
        std::vector<HedgePair> pairs;
        bool load_failed = false;
        try { pairs = db.load_pairs(); }
        catch (std::exception& e) {
            LOG("[HEDGE] load_pairs failed: %s — retrying", e.what());
            load_failed = true;
        }
        if (load_failed) {
            co_await sleep_ms(poll_ms);
            continue;
        }

        // Start sessions for hedge accounts that have pairs.
        for (const auto& p : pairs) {
            if (accts.count(p.hedge_label)) continue;
            auto acct = std::make_shared<HedgeAcct>();
            acct->label = p.hedge_label;
            try {
                acct->cfg = OrbConfig::from_file(config_dir + "/" + p.hedge_label + "_config.json");
            } catch (std::exception& e) {
                LOG("[HEDGE] %s: config load failed: %s", p.hedge_label.c_str(), e.what());
                acct->last_error = std::string("config: ") + e.what();
                accts[p.hedge_label] = acct;   // remembered → error visible in status
                continue;
            }
            if (acct->cfg.account_id.empty() || acct->cfg.rithmic_user.empty()) {
                LOG("[HEDGE] %s: config has no account_id / credentials (%s_ACCOUNT / %s_USER)",
                    p.hedge_label.c_str(), acct->cfg.order_env_prefix.c_str(),
                    acct->cfg.order_env_prefix.c_str());
                acct->last_error = "missing account_id or credentials";
                accts[p.hedge_label] = acct;
                continue;
            }
            LOG("[HEDGE] %s: starting session (WARNING: no nq_executor may run on this account)",
                p.hedge_label.c_str());
            accts[p.hedge_label] = acct;
            if (!dry_run)
                asio::co_spawn(ioc, account_supervisor(db, acct, ssl_ctx), asio::detached);
        }

        // Per-pair targets; per-account aggregate target + reference price.
        std::map<std::string, int>    acct_target;
        std::map<std::string, double> acct_ref_px;
        std::map<std::string, bool>   acct_has_enabled;
        std::map<std::string, bool>   acct_hold;   // stale/missing anchor feed → don't trade
        for (const auto& p : pairs) {
            auto it = accts.find(p.hedge_label);
            if (it == accts.end()) continue;
            HedgeAcct& a = *it->second;

            int anchor_net = 0;
            int64_t age_ms = -1;
            auto feed = db.anchor_feed(p.anchor_label);
            bool stale = false;
            if (feed) {
                anchor_net = feed->first;
                age_ms     = feed->second;
                stale      = age_ms > 15000 && anchor_net != 0;
            }

            int target = 0;
            std::string err;
            if (p.enabled) {
                if (!feed) {
                    // The anchor executor has never published (old build or not running) —
                    // its true position is unknown, so trading the hedge leg is unsafe.
                    err = "no anchor feed — is the anchor running the updated nq_executor?";
                    acct_hold[p.hedge_label] = true;
                } else if (stale) {
                    err = "anchor feed stale (" + std::to_string(age_ms / 1000) + "s) — holding, not trading";
                    acct_hold[p.hedge_label] = true;
                } else {
                    target = (int)std::lround(-anchor_net * p.ratio);
                    target = std::clamp(target, -p.max_qty, p.max_qty);
                    acct_target[p.hedge_label] += target;
                    acct_has_enabled[p.hedge_label] = true;
                    if (acct_ref_px[p.hedge_label] <= 0.0)
                        acct_ref_px[p.hedge_label] = db.ref_price(p.anchor_label);
                }
            }
            // hedge_net unknown (no tid=451 yet) → leave the last known value in
            // hedge_status rather than writing a misleading 0.
            const bool hnet_known = dry_run || a.hedge_net != INT_MIN;
            const int  hnet = dry_run ? a.sim_net : (hnet_known ? a.hedge_net : 0);
            db.write_status(p.id, anchor_net, hnet_known, hnet, target, age_ms,
                            nullptr,
                            !err.empty() ? err.c_str()
                            : (!a.last_error.empty() ? a.last_error.c_str() : ""));
        }

        // Converge each hedge account toward its aggregate target.
        for (auto& [label, acct] : accts) {
            HedgeAcct& a = *acct;
            const bool managed = acct_has_enabled.count(label) > 0 ||
                                 std::any_of(pairs.begin(), pairs.end(),
                                     [&](const HedgePair& p){ return p.hedge_label == label; });

            // In-flight timeout: re-quote — cancel the resting order so a fresh
            // aggressive limit replaces it.
            if (a.inflight && now_ms() - a.inflight_sent_ms > 10000 && !a.cancel_sent) {
                if (!a.inflight_basket.empty() && a.op_connected) {
                    LOG("[HEDGE] %s: hedge order %s unfilled after 10s — cancelling to re-quote",
                        label.c_str(), a.inflight_tag.c_str());
                    rti::RequestCancelOrder cxl;
                    cxl.set_template_id(316);
                    cxl.set_basket_id(a.inflight_basket);
                    cxl.set_account_id(a.cfg.account_id);
                    cxl.set_fcm_id(a.fcm_id);
                    cxl.set_ib_id(a.ib_id);
                    cxl.set_manual_or_auto(2);
                    a.op_q.enqueue(proto_frame(cxl));
                    a.cancel_sent = true;
                } else if (now_ms() - a.inflight_sent_ms > 30000) {
                    LOG("[HEDGE] %s: WARNING hedge order %s unresolved after 30s without a "
                        "server basket id — clearing in-flight state", label.c_str(),
                        a.inflight_tag.c_str());
                    a.inflight = false;
                }
            }

            // Converge while the account has ANY pair in the table: disabled pairs
            // contribute target 0, so disabling flattens the hedge leg. An account
            // whose pairs were all deleted is left untouched (API refuses to delete
            // a pair with an open hedge leg).
            if (!managed || acct_hold.count(label)) continue;
            if (a.inflight) continue;
            if (now_ms() - a.last_order_ms < 3000) continue;           // pacing

            const int target = acct_target[label];   // 0 when every pair is disabled
            if (dry_run) {
                if (a.sim_net != target) {
                    LOG("[HEDGE] [DRY_RUN] %s: would trade %d → %d (%s %d)",
                        label.c_str(), a.sim_net, target,
                        target > a.sim_net ? "BUY" : "SELL", std::abs(target - a.sim_net));
                    a.sim_net = target;
                }
                continue;
            }
            if (a.hedge_net == INT_MIN) continue;                       // no exchange truth yet
            if (!a.op_connected || !a.pnl_connected) continue;
            const int diff = target - a.hedge_net;
            if (diff == 0) continue;
            send_hedge_order(db, a, diff, acct_ref_px[label]);
        }

        co_await sleep_ms(poll_ms);
    }

    // Shutdown: never auto-flatten — log what stays open.
    for (auto& [label, acct] : accts) {
        if (acct->hedge_net != INT_MIN && acct->hedge_net != 0)
            LOG("[HEDGE] %s: shutdown with OPEN hedge position net=%d — manage it manually",
                label.c_str(), acct->hedge_net);
    }
}

// ─── main ─────────────────────────────────────────────────────────────────────
static void load_dotenv_local(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        auto b = line.find_first_not_of(" \t\r\n");
        if (b == std::string::npos || line[b] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(b, eq - b);
        std::string v = line.substr(eq + 1);
        auto vb = v.find_first_not_of(" \t\r\n\"");
        auto ve = v.find_last_not_of(" \t\r\n\"");
        v = (vb == std::string::npos) ? "" : v.substr(vb, ve - vb + 1);
        auto ke = k.find_last_not_of(" \t\r\n");
        k = k.substr(0, ke + 1);
        if (!k.empty() && !std::getenv(k.c_str())) setenv(k.c_str(), v.c_str(), 0);
    }
}

int main(int argc, char** argv) {
    std::string config_dir = "config";
    int  poll_ms = 500;
    bool dry_run = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config-dir" && i + 1 < argc) config_dir = argv[++i];
        else if (arg == "--poll-ms" && i + 1 < argc) poll_ms = std::atoi(argv[++i]);
        else if (arg == "--dry-run") dry_run = true;
        else if (arg == "--help") {
            printf("Usage: hedge_coordinator [--config-dir config] [--poll-ms 500] [--dry-run]\n");
            return 0;
        }
    }

    load_dotenv_local(".env");
    const char* pg_pw = std::getenv("PG_PASSWORD");
    if (!pg_pw || !pg_pw[0]) pg_pw = std::getenv("RITHMIC_PG_PASSWORD");
    std::string connstr = std::string("host=localhost port=5432 dbname=rithmic"
                                      " user=rithmic_user connect_timeout=10"
                                      " application_name=hedge_coordinator password='") +
                          (pg_pw ? pg_pw : "") + "'";

    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    LOG("[HEDGE] hedge_coordinator starting (config-dir=%s poll=%dms%s)",
        config_dir.c_str(), poll_ms, dry_run ? " DRY-RUN" : "");

    std::unique_ptr<HedgeDB> db;
    try {
        db = std::make_unique<HedgeDB>(connstr);
        db->ensure_schema();
    } catch (std::exception& e) {
        LOG("[HEDGE] FATAL: database unavailable: %s", e.what());
        return 1;
    }

    asio::io_context ioc;
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.load_verify_file("certs/rithmic_ssl_cert_auth_params");
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    asio::co_spawn(ioc, convergence_loop(*db, ssl_ctx, config_dir, poll_ms, dry_run),
                   asio::detached);

    while (g_running) {
        ioc.run_for(std::chrono::milliseconds(500));
        if (ioc.stopped()) break;
    }
    ioc.stop();
    LOG("[HEDGE] hedge_coordinator stopped");
    return 0;
}
