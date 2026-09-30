/*  ═══════════════════════════════════════════════════════════════════════════
    executor_main.cpp — NQ Micro ORB Execution Engine entry point

    Architecture:
        Rithmic MD plant (WebSocket/protobuf)
            → LastTrade ticks → OrbStrategy → signal
            → OrderManager  → ORDER_PLANT send (or dry_run log)
            → on fill       → OrderManager state machine
            → RiskManager   → halt if limits breached
            → OrbDB         → live_trades / live_sessions rows written

    Connection sequence (mirrors existing rithmic_engine/src/client.cpp):
        1. Connect MD plant WS → system_info → login (TICKER_PLANT) → subscribe NQ
        2. Receive LastTrade loop → on_tick → OrbStrategy
        3. In live mode: connect ORDER_PLANT WS → login (ORDER_PLANT)
        4. Heartbeat timer (30s) on both plants

    The order plant connection is a second WS stream using the same protobuf
    protocol.  RequestNewOrder (312) / RithmicOrderNotification (351, internal acks) /
    ExchangeOrderNotification (352, fills with fill_price) / AccountPnLPositionUpdate (451)
    messages are all defined in rithmic.proto.  Template 308 subscription is sent after
    login to activate fill/reject delivery.

    Build:
        cd ~/rithmic_engine/build
        cmake .. -DCMAKE_BUILD_TYPE=Release
        make nq_executor -j4

    Usage:
        ./nq_executor --config config/orb_config.json [--dry-run]
    ═══════════════════════════════════════════════════════════════════════════ */

#include "orb_config.hpp"
#include "orb_strategy.hpp"
#include "trend_strategy.hpp"
#include "mtf_scalper_strategy.hpp"
#include "../paper/paper_quote.hpp"   // top-of-book state + entry gates shared with the paper fleet
#include <deque>
#include <type_traits>
#include <climits>
#include <fstream>
#include <sstream>
#include "order_manager.hpp"
#include "notification_router.hpp"
#include "risk_manager.hpp"
#include "latency_logger.hpp"
#include "orb_db.hpp"
#ifdef USE_RAPI_SDK
#include "sdk_md_feed.hpp"
#endif

// Reuse existing client/db infrastructure
#include "client.hpp"
#include "config.hpp"
#include "db.hpp"
#include "log.hpp"
#include "../audit.hpp"

#include "rithmic.pb.h"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

namespace asio      = boost::asio;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace ssl       = asio::ssl;
using tcp           = asio::ip::tcp;
namespace fs        = std::filesystem;

// ─── Globals ──────────────────────────────────────────────────────────────────
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_flatten_requested{false}; // set by signal handler; acted on in eod_loop
// Shutdown drain: g_running is false (no new ticks, no new entries) but the ORDER/PNL
// plant readers and the 1 s housekeeping loop keep running until the drain ends, so exit
// fills, cancel ACKs, "Cancellation Failed", late server ids and reconciler unwinds are
// still processed. Before 2026-09-24 they all stopped the moment SIGTERM arrived.
static std::atomic<bool> g_draining{false};
// ── Live fire drill (--drill orphan) ──────────────────────────────────────────
// Reproduces the 2026-09-23 failure shape on the real account with ONE contract:
// once the exchange confirms FLAT, the executor sends an order it does NOT track
// (foreign user_tag). The fill must be routed into the stale-stop guards (ghost
// halt) and the PNL-plant reconciliation must unwind it within the grace window.
// The strategy is halted for the whole run; the process exits 0 on PASS, 2 on FAIL.
static std::string        g_drill;                 // "" = normal run
static std::atomic<bool>  g_drill_sent{false};
static std::atomic<bool>  g_drill_passed{false};
static std::atomic<int64_t> g_drill_sent_ms{0};
static std::atomic<int>   g_exit_code{0};
// Delay before the outer cycle loop reconnects. Normally 10s; a session that
// ends on an ORDER_PLANT login refusal raises it so we do not hammer Rithmic
// with a fresh login every 13s (rp_code=13 = "too many rapid logins /
// duplicate session", which such a loop then keeps triggering on its own).
static constexpr int kReconnectDelayNormalS  = 10;
static constexpr int kReconnectDelayRefusedS = 300;
static std::atomic<int> g_reconnect_delay_s{kReconnectDelayNormalS};

// ─── Position DB write helper ─────────────────────────────────────────────────
// Reads current state from order_mgr + strategy and issues an UPSERT to
// live_position. Safe to call at any frequency — OrbDB::write_position never throws.
template <class Strategy>
static void flush_position(OrbDB* db,
                            const std::string& today,
                            const OrderManager& order_mgr,
                            const Strategy& strategy,
                            bool op_connected,
                            double point_value = 2.0,
                            bool md_connected = false) {
    if (!db || !db->is_connected()) return;

    Position snap = order_mgr.position_snapshot();
    double last_px = strategy.last_price();

    // Build state string
    std::string state_str;
    switch (snap.state) {
        case PosState::FLAT:          state_str = "FLAT";          break;
        case PosState::PENDING_ENTRY: state_str = "PENDING_ENTRY"; break;
        case PosState::LONG:          state_str = "LONG";          break;
        case PosState::SHORT:         state_str = "SHORT";         break;
        case PosState::PENDING_EXIT:  state_str = "PENDING_EXIT";  break;
    }

    std::string dir_str;
    if (snap.direction == OrbSignal::BUY)  dir_str = "LONG";
    if (snap.direction == OrbSignal::SELL) dir_str = "SHORT";

    double unreal_pts = 0.0;
    double unreal_usd = 0.0;
    if ((snap.state == PosState::LONG || snap.state == PosState::SHORT) &&
        snap.entry_price > 0.0 && last_px > 0.0) {
        unreal_pts = (snap.state == PosState::LONG)
            ? (last_px - snap.entry_price)
            : (snap.entry_price - last_px);
        // Unrealized P&L scales with position size; commission is charged only
        // at close (see OrderManager pnl_usd) — keep it out of unrealized.
        unreal_usd = unreal_pts * point_value * snap.qty;
    }

    // entry_time: format fill_time as UTC string (empty if FLAT/PENDING)
    std::string entry_time_str;
    if (snap.entry_price > 0.0 &&
        (snap.state == PosState::LONG || snap.state == PosState::SHORT ||
         snap.state == PosState::PENDING_EXIT)) {
        // fill_time is a steady_clock point; we approximate wall time as now - elapsed
        auto elapsed = std::chrono::steady_clock::now() - snap.fill_time;
        auto fill_wall = std::chrono::system_clock::now() - elapsed;
        time_t fill_tt = std::chrono::system_clock::to_time_t(fill_wall);
        struct tm utc_tm;
        gmtime_r(&fill_tt, &utc_tm);
        char tbuf[32];
        std::strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S", &utc_tm);
        entry_time_str = tbuf;
    }

    const auto& sess = strategy.session();

    db->write_position(today,
                       state_str,
                       dir_str,
                       snap.entry_price,
                       entry_time_str,
                       last_px,
                       unreal_pts,
                       unreal_usd,
                       snap.sl_price,
                       strategy.orb_set() ? strategy.orb_high() : 0.0,
                       strategy.orb_set() ? strategy.orb_low()  : 0.0,
                       strategy.orb_set(),
                       sess.trades_today,
                       md_connected,
                       op_connected,
                       snap.be_triggered,
                       snap.trailing_active);
}

static void handle_signal(int /*sig*/) {
    // Only async-signal-safe operations here — no mutexes, no LOG.
    // Flatten is deferred to eod_loop which checks g_flatten_requested each second.
    g_running          = false;
    g_flatten_requested = true;
}

// ─── Framing helpers (mirrors RithmicClient::frame / strip_header) ────────────
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

// RequestHeartbeat.ssboe is int32 in the proto (proto/rithmic.proto:77), so a true
// int64 epoch is impossible without a proto change. Clamp instead of truncating:
// epoch-seconds fits int32 until 2038; past that the value saturates rather than
// wrapping negative (which some Rithmic parsers reject).
static int32_t hb_ssboe_now() {
    int64_t secs = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    if (secs > INT32_MAX) secs = INT32_MAX;
    return static_cast<int32_t>(secs);
}

// ─── ET time helpers ─────────────────────────────────────────────────────────
static void current_et(int& h, int& m) {
    auto now = std::chrono::system_clock::now();
    time_t tt = std::chrono::system_clock::to_time_t(now);
    struct tm utc_tm;
    gmtime_r(&tt, &utc_tm);
    time_t et_t = tt - us_et_offset(utc_tm) * 3600;
    struct tm et_tm;
    gmtime_r(&et_t, &et_tm);
    h = et_tm.tm_hour;
    m = et_tm.tm_min;
}

static std::string read_text_file(const std::string& path) {
    std::ifstream f(path);
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

// The executor's "today" is the TRADING date (rolls at 18:00 ET, when CME opens the
// next day and the prop firm resets its daily loss limit) — see trading_date_str.
static std::string today_date_str() {
    return trading_date_str(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

// ET wall time of a tick (for the regime gate's RTH window) — same arithmetic as paper_main.
static void tick_et_hm(int64_t us, int& h, int& m) {
    time_t tt = static_cast<time_t>(us / 1'000'000LL);
    struct tm tm_utc;
    gmtime_r(&tt, &tm_utc);
    int64_t et = (int64_t)tt - us_et_offset(tm_utc) * 3600LL;
    h = (int)((et / 3600) % 24);
    if (h < 0) h += 24;
    m = (int)((et % 3600) / 60);
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
    {
        auto& sock = beast::get_lowest_layer(*ws).socket();
        sock.set_option(asio::ip::tcp::no_delay(true));
        sock.set_option(asio::socket_base::receive_buffer_size(1 << 20));
        sock.set_option(asio::socket_base::send_buffer_size(256 << 10));
    }
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SSL SNI failed");
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, asio::use_awaitable);
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req){
        req.set(beast::http::field::user_agent, "nq_executor/1.0");
    }));
    co_await ws->async_handshake(host + ":" + port, "/", asio::use_awaitable);
    co_return ws;
}

static asio::awaitable<void> ws_write(WsStream& ws, const std::string& data) {
    ws.binary(true);
    co_await ws.async_write(asio::buffer(data), asio::use_awaitable);
}

// ─── Serialized per-stream write queue ────────────────────────────────────────
// Beast forbids overlapping async_write calls on the same stream. heartbeat_loop,
// op_loop, eod_loop and OrderPlant callbacks all write to the shared MD /
// ORDER_PLANT streams, so every write goes through this queue: at most one
// async_write per stream is ever in flight.
class WsWriteQueue {
public:
    void attach(WsStream* ws) { ws_ = ws; }

    // Detach before the stream is closed/replaced: queued and future writes fail
    // immediately with not_connected instead of touching a dead stream.
    void detach() {
        ws_ = nullptr;
        fail_all(asio::error::make_error_code(asio::error::not_connected));
    }

    // Awaitable write: suspends until this message has been written (or failed).
    asio::awaitable<void> write(std::string data) {
        if (!ws_)
            throw beast::system_error(
                asio::error::make_error_code(asio::error::not_connected),
                "ws write: stream not attached");
        auto token = asio::use_awaitable;
        co_await asio::async_initiate<asio::use_awaitable_t<>,
                                      void(boost::system::error_code)>(
            [this, data = std::move(data)](auto handler) mutable {
                submit(std::move(data), Handler(std::move(handler)));
            },
            token);
    }

    // Fire-and-forget write for non-coroutine callers (OrderPlant send paths).
    void enqueue(std::string data) {
        submit(std::move(data), [](boost::system::error_code ec) {
            if (ec) LOG("[WS] queued write failed: %s", ec.message().c_str());
        });
    }

private:
    // Move-only type-erased completion handler — asio's use_awaitable handler is
    // not copyable, so std::function cannot hold it.
    struct MoveOnlyHandler {
        struct Base {
            virtual void call(boost::system::error_code) = 0;
            virtual ~Base() = default;
        };
        template <class F>
        struct Impl : Base {
            F f;
            explicit Impl(F&& fn) : f(std::move(fn)) {}
            void call(boost::system::error_code ec) override { std::move(f)(ec); }
        };
        std::unique_ptr<Base> p;
        MoveOnlyHandler() = default;
        template <class F,
                  class = std::enable_if_t<!std::is_same_v<std::decay_t<F>, MoveOnlyHandler>>>
        MoveOnlyHandler(F&& f) : p(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}
        void operator()(boost::system::error_code ec) { p->call(ec); }
    };
    using Handler = MoveOnlyHandler;
    struct Item { std::string data; Handler handler; };

    void submit(std::string data, Handler handler) {
        if (!ws_) {
            handler(asio::error::make_error_code(asio::error::not_connected));
            return;
        }
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
                if (ec) {
                    // Stream is broken: fail every queued write with the same error.
                    fail_all(ec);
                    busy_ = false;
                    return;
                }
                pump();
            });
    }

    void fail_all(boost::system::error_code ec) {
        while (!q_.empty()) {
            Handler h = std::move(q_.front().handler);
            q_.pop_front();
            h(ec);
        }
    }

    WsStream*        ws_   = nullptr;
    bool             busy_ = false;
    std::deque<Item> q_;
};

// ─── Order plant send helper ──────────────────────────────────────────────────
// Wraps WsStream writes with mutex (called from io_context coroutine only —
// single-threaded io_context means no contention, but we keep the mutex for
// safety in case of future threading changes).
struct OrderPlant {
    std::unique_ptr<WsStream>       ws;
    WsWriteQueue*                   write_q = nullptr;  // serialized writes (Beast forbids overlapping async_write)
    std::mutex                      send_mu;
    bool                            connected = false;
    std::string                     account_id;
    std::string                     fcm_id;
    std::string                     ib_id;
    std::string                     trade_route = "simulator";  // Legends Trading route; NEVER use "Rithmic Order Routing"
    std::string                     trade_symbol;  // front-month contract e.g. NQM6
    std::unordered_set<std::string> pending_cancels_;  // queued cancels from disconnected periods

    // Send RequestNewOrder (template 312)
    // Returns basket_id if sent, empty string on error
    std::string send_new_order(const std::string& basket_id,
                               const std::string& symbol,
                               const std::string& exchange,
                               int qty,
                               int order_type,   // 2=MKT, 1=LMT, 4=STOP_MARKET
                               bool is_buy,
                               double price,
                               const std::string& user_tag,
                               bool dry_run) {
        if (dry_run) {
            LOG("[ORDER_PLANT] [DRY_RUN] %s %s qty=%d basket=%s",
                is_buy ? "BUY" : "SELL", symbol.c_str(), qty, basket_id.c_str());
            return basket_id;
        }
        if (!connected || !ws) {
            LOG("[ORDER_PLANT] Not connected — cannot send order basket=%s", basket_id.c_str());
            return "";
        }

        rti::RequestNewOrder req;
        req.set_template_id(312);
        req.set_fcm_id(fcm_id);
        req.set_ib_id(ib_id);
        req.set_account_id(account_id);
        req.set_symbol(symbol);
        req.set_exchange(exchange);
        req.set_quantity(qty);
        req.set_order_type(order_type);
        req.set_transaction_type(is_buy ? 1 : 2);
        // Rithmic fields by order type:
        //   LIMIT (1)       -> price
        //   MARKET (2)      -> neither
        //   STOP_LIMIT (3)  -> price + trigger_price
        //   STOP_MARKET (4) -> trigger_price
        if (order_type == 1) req.set_price(price);
        if (order_type == 3) { req.set_price(price); req.set_trigger_price(price); }
        if (order_type == 4) req.set_trigger_price(price);
        // basket_id field removed from canonical proto; use user_tag for client-side tracking
        req.set_user_tag(basket_id);
        req.set_duration(rti::RequestNewOrder::DAY);
        req.set_manual_or_auto_select(rti::RequestNewOrder::AUTO);
        req.set_trade_route(trade_route);

        try {
            std::string wire = proto_frame(req);
            // Route through the per-stream write queue — a blocking write here would
            // overlap with in-flight async writes (heartbeats), which Beast forbids.
            if (write_q) write_q->enqueue(std::move(wire));
            else         ws->write(asio::buffer(wire));  // fallback: queue not attached yet
            LOG("[ORDER_PLANT] RequestNewOrder sent: basket=%s %s %s qty=%d "
                "order_type=%d price=%.2f fcm=%s ib=%s acct=%s route='%s' dur=DAY auto=AUTO",
                basket_id.c_str(), is_buy ? "BUY" : "SELL", symbol.c_str(), qty,
                order_type, price, fcm_id.c_str(), ib_id.c_str(), account_id.c_str(),
                trade_route.c_str());
            return basket_id;
        } catch (std::exception& e) {
            LOG("[ORDER_PLANT] ERROR sending order: %s", e.what());
            return "";
        }
    }

    // Send RequestCancelOrder (template 316)
    void send_cancel(const std::string& basket_id, const std::string& account_id_str) {
        if (!connected || !ws) {
            // Queue the cancel; it will be drained when the order plant reconnects.
            pending_cancels_.insert(basket_id);
            LOG("[ORDER_PLANT] Disconnected — queued cancel for basket=%s "
                "(pending_cancels size=%zu)",
                basket_id.c_str(), pending_cancels_.size());
            return;
        }
        rti::RequestCancelOrder req;
        req.set_template_id(316);
        req.set_basket_id(basket_id);
        req.set_account_id(account_id_str);
        req.set_fcm_id(fcm_id);
        req.set_ib_id(ib_id);
        req.set_manual_or_auto(2);  // AUTO — omitted → rp_code=1045, cancel silently refused
        try {
            if (write_q) write_q->enqueue(proto_frame(req));
            else         ws->write(asio::buffer(proto_frame(req)));
            LOG("[ORDER_PLANT] RequestCancelOrder sent: basket=%s", basket_id.c_str());
        } catch (std::exception& e) {
            LOG("[ORDER_PLANT] ERROR sending cancel: %s", e.what());
            return;
        }
        // Re-subscribe (tid=308) to prompt Rithmic to flush queued cancel ACKs.
        // Rithmic batches cancel notifications and delivers them on next order activity;
        // a re-subscription acts as that trigger without submitting a real order.
        flush_order_notifications();
    }

    // Re-send RequestSubscribeForOrderUpdates (tid=308) to prompt Rithmic to
    // flush any queued cancel ACKs. Rithmic holds cancel notifications until the
    // next order activity event; this triggers that flush cheaply.
    void flush_order_notifications() {
        if (!connected || !ws) return;
        rti::RequestSubscribeForOrderUpdates sub;
        sub.set_template_id(308);
        sub.set_fcm_id(fcm_id);
        sub.set_ib_id(ib_id);
        sub.set_account_id(account_id);
        try {
            if (write_q) write_q->enqueue(proto_frame(sub));
            else         ws->write(asio::buffer(proto_frame(sub)));
            LOG("[ORDER_PLANT] Sent tid=308 flush to prompt cancel ACK delivery");
        } catch (std::exception& e) {
            LOG("[ORDER_PLANT] WARNING: flush_order_notifications failed: %s", e.what());
        }
    }

    // Drain any cancels that were queued while the order plant was disconnected.
    // Call this immediately after order_plant->connected is set to true and the
    // subscription is in place, so the exchange receives the cancels promptly.
    void drain_pending_cancels() {
        if (pending_cancels_.empty()) return;
        std::size_t count = pending_cancels_.size();
        LOG("[ORDER_PLANT] Draining %zu queued cancel(s) after reconnect", count);
        for (const auto& bid : pending_cancels_) {
            rti::RequestCancelOrder req;
            req.set_template_id(316);
            req.set_basket_id(bid);
            req.set_account_id(account_id);
            req.set_fcm_id(fcm_id);
            req.set_ib_id(ib_id);
            req.set_manual_or_auto(2);  // AUTO — omitted → rp_code=1045, cancel silently refused
            try {
                if (write_q) write_q->enqueue(proto_frame(req));
                else         ws->write(asio::buffer(proto_frame(req)));
                LOG("[ORDER_PLANT] [DRAIN] RequestCancelOrder sent: basket=%s", bid.c_str());
            } catch (std::exception& e) {
                LOG("[ORDER_PLANT] [DRAIN] ERROR sending cancel for basket=%s: %s",
                    bid.c_str(), e.what());
            }
        }
        pending_cancels_.clear();
        LOG("[ORDER_PLANT] Drained %zu pending cancel(s)", count);
    }
};

// ─── Main executor coroutine ──────────────────────────────────────────────────
// risk, strategy, and today are owned by main() and survive reconnects.
template <class Strategy>
asio::awaitable<void> run_executor(const OrbConfig& orb_cfg,
                                   asio::io_context& ioc_ref,
                                   RiskManager& risk,
                                   Strategy& strategy,
                                   std::string& today,
                                   Position& carried_pos) {
    constexpr bool kOrb   = std::is_same_v<Strategy, OrbStrategy>;
    constexpr bool kTrend = std::is_same_v<Strategy, TrendStrategy>;
    constexpr bool kMtf   = std::is_same_v<Strategy, MtfScalperStrategy>;
    // ── Component construction ────────────────────────────────────────────────
    // tick_value = point_value × tick_size (NQ: 20.0×0.25=$5.00, MNQ: 2.0×0.25=$0.50)
    LatencyLogger lat(orb_cfg.point_value * NQ_TICK_SIZE);
    OrderManager  order_mgr(orb_cfg, risk, lat);
    // Top of book from the collector's bbo table (pg feed only), merged into the tick
    // stream by time. Drives the OrbConfig book entry gates (the paper fleet's overlays —
    // spread_gate_*, imbalance_min, imbalance_max, microprice_lead) and, for the trend
    // engine, book_imbalance / book_fade signals via strategy.on_quote().
    paper::QuoteState book;
    int64_t last_tick_us = 0;
    // Session-shape regime gate (paper_quote.hpp RegimeState) — declared here because the
    // signal handler below reads it; its ATR is seeded once the DB exists (seed_regime_atr).
    paper::RegimeState regime;
    const std::string regime_symbol = orb_cfg.md_feed_symbol.empty() ? std::string("NQ") : orb_cfg.md_feed_symbol;
    order_mgr.set_regime(&regime);   // exit-side regime: trail step / target by session shape

    // ── Reconnect reconciliation (#2) ─────────────────────────────────────────
    // If the previous session ended with an open position (e.g. disconnect while
    // LONG), the exchange stop order may or may not have fired. We cannot query
    // the exchange here, so we halt new entries and force a manual check.
    bool carried_nonflat = (carried_pos.state != PosState::FLAT);
    if (carried_nonflat) {
        LOG("[EXECUTOR] CRITICAL: reconnecting with non-flat carried position "
            "(state=%d dir=%s entry=%.2f sl=%.2f) — halting new entries. "
            "Verify exchange position manually; delete halt if flat.",
            (int)carried_pos.state,
            carried_pos.direction == OrbSignal::BUY ? "LONG" : "SHORT",
            carried_pos.entry_price, carried_pos.sl_price);
        strategy.halt_trading("reconnect_unreconciled_position");
    }
    carried_pos = Position{};  // reset; will be populated again at session end

    // Trend engine only (see the callback): set when a signal left the order manager FLAT.
    std::string strategy_unsettled;
    // Trend engine only: true while the pg feed replays warmup_minutes of history — the
    // strategy builds its bars, every signal it emits is dropped (never an order).
    bool warming_up = false;
    int  warmup_signals_dropped = 0;
    auto settle_strategy = [&]() {
        if constexpr (!kOrb) {
            if (strategy_unsettled.empty()) return;
            if (order_mgr.position_snapshot().state == PosState::FLAT && strategy.session().in_position) {
                LOG("[EXECUTOR] trend engine released — order manager stayed FLAT (%s)",
                    strategy_unsettled.c_str());
                strategy.notify_trade_filled(OrbSignal::FLATTEN_EOD, "not_executed:" + strategy_unsettled);
            }
            strategy_unsettled.clear();
        }
    };

    // Wire strategy → order_mgr
    strategy.set_signal_callback(
        [&](OrbSignal sig, double price, const std::string& reason) {
            if (warming_up) { ++warmup_signals_dropped; return; }   // history replay: no orders
            if (sig == OrbSignal::FLATTEN_EOD) {
                Position eod_snap = order_mgr.position_snapshot();
                const char* eod_ss;
                switch (eod_snap.state) {
                    case PosState::FLAT:          eod_ss = "FLAT";          break;
                    case PosState::PENDING_ENTRY: eod_ss = "PENDING_ENTRY"; break;
                    case PosState::LONG:          eod_ss = "LONG";          break;
                    case PosState::SHORT:         eod_ss = "SHORT";         break;
                    case PosState::PENDING_EXIT:  eod_ss = "PENDING_EXIT";  break;
                    default:                      eod_ss = "?";             break;
                }
                LOG("[EXECUTOR] EOD_FLATTEN signal — pos=%s entry=%.2f sl=%.2f "
                    "basket_entry=%s basket_exit=%s px=%.2f",
                    eod_ss, eod_snap.entry_price, eod_snap.sl_price,
                    eod_snap.basket_id_entry.c_str(), eod_snap.basket_id_exit.c_str(),
                    price);
            }
            if (kOrb && orb_cfg.max_entry_offset > 0.0 && sig != OrbSignal::FLATTEN_EOD) {
                double orb_level = (sig == OrbSignal::BUY) ? strategy.orb_high() : strategy.orb_low();
                double offset = (sig == OrbSignal::BUY) ? (price - orb_level) : (orb_level - price);
                if (offset > orb_cfg.max_entry_offset) {
                    LOG("[EXECUTOR] Signal SKIPPED — chase %.2fpt > max_entry_offset=%.2fpt "
                        "(orb=%.2f px=%.2f reason=%s)",
                        offset, orb_cfg.max_entry_offset, orb_level, price, reason.c_str());
                    strategy.notify_trade_filled(sig);  // reset in_position for re-entry
                    return;
                }
            }
            if (sig == OrbSignal::BUY || sig == OrbSignal::SELL) {
                // Book entry gates — the same QuoteState::gate() the paper brokers apply, so a
                // gated paper variant (__sg/__imb/__micro/__inv/__all) behaves the same live.
                // No gate configured → always "" and nothing changes for ORB/trend/mtf.
                std::string blocked = book.gate(orb_cfg, sig == OrbSignal::BUY ? 1 : -1,
                                                last_tick_us, NQ_TICK_SIZE);
                if (blocked.empty()) blocked = regime.gate(orb_cfg, sig == OrbSignal::BUY ? 1 : -1);
                if (!blocked.empty()) {
                    LOG("[EXECUTOR] Signal SKIPPED — gate '%s' (imb=%.2f spread=%.1ft fresh=%d | range/ATR=%.2f eff=%.2f move/ATR=%.2f min=%d reason=%s)",
                        blocked.c_str(), book.q.imbalance(), book.q.spread_ticks(NQ_TICK_SIZE),
                        (int)book.fresh(last_tick_us), regime.range_atr(), regime.eff(), regime.move_atr(),
                        regime.minutes, reason.c_str());
                    if constexpr (kOrb) strategy.notify_trade_filled(sig);   // reset in_position for re-entry
                    else strategy_unsettled = "book_gate:" + blocked;         // settle_strategy() releases the engine
                    return;
                }
            }
            double boundary = (sig == OrbSignal::BUY)  ? strategy.orb_high()
                            : (sig == OrbSignal::SELL) ? strategy.orb_low()
                            : 0.0;
            if constexpr (kMtf) {
                // The strategy's own stop DISTANCE at signal time (its cur_stop() is
                // already set inside open_position() before this callback fires) —
                // the entry order gets this instead of cfg_.sl_points. Only r_unit is
                // taken from current_bracket(): position SIZE stays cfg_.qty, same as
                // ORB/Trend — the strategy's own risk_pct/qty_calc() dynamic sizing is
                // deliberately not wired live (a fixed, auditable qty per account is
                // the established convention here; see config's qty/qty_max).
                double sl_dist = (sig == OrbSignal::BUY || sig == OrbSignal::SELL)
                    ? strategy.current_bracket().r_unit : 0.0;
                order_mgr.on_signal(sig, price, reason, boundary, sl_dist);
            } else {
                order_mgr.on_signal(sig, price, reason, boundary);
            }
            if constexpr (!kOrb) {
                // The trend engine books its own position right AFTER this callback returns.
                // If the order manager did not act (entry rejected by risk/halt/not-FLAT, or a
                // managed exit while already flat) nothing will ever report the trade closed,
                // and the engine would sit "in position" for the rest of the day. Record it
                // here; settle_strategy() releases the engine once emit() has finished.
                if (order_mgr.position_snapshot().state == PosState::FLAT)
                    strategy_unsettled = reason.empty() ? "no_order" : reason;
            }
        }
    );

    // ── DB setup ──────────────────────────────────────────────────────────────
    std::unique_ptr<OrbDB> db;
    try {
        db = std::make_unique<OrbDB>(orb_cfg.pg_connstr(), orb_cfg.symbol,
                                     orb_cfg.account_label, orb_cfg.strategy);
        LOG("[EXECUTOR] OrbDB connected");
        // Single-instance guard: a second executor for the same account+strategy
        // must refuse to trade (dry-run instances don't take the lock).
        if (!orb_cfg.dry_run) {
            if (!db->acquire_instance_lock(orb_cfg.account_label, orb_cfg.strategy)) {
                LOG("[EXECUTOR] FATAL: another executor already holds the instance lock "
                    "for account=%s strategy=%s — refusing to trade, exiting",
                    orb_cfg.account_label.c_str(), orb_cfg.strategy.c_str());
                g_running = false;
                co_return;
            }
        }
        // Seed risk manager with historical P&L and peak equity.
        if (today.empty()) {  // only on first startup, not reconnects
            double hist_pnl  = db->get_total_pnl();
            double hist_peak = db->get_peak_equity(orb_cfg.starting_balance);
            risk.seed_total_profit(hist_pnl);
            // Seed peak first: set_equity raises peak_ if arg > current peak_.
            // Then set current equity — peak_ stays at hist_peak when equity < hist_peak
            // (the normal case after a drawdown), giving correct trailing drawdown distance.
            risk.set_equity(hist_peak);
            risk.set_equity(orb_cfg.starting_balance + hist_pnl);
            LOG("[EXECUTOR] [RISK-SEED] account-wide label=%s: equity=%.2f peak=%.2f "
                "room=%.2f (cap %.0f) — synthetic gauge; the broker balance check below is "
                "the one that matches the prop firm",
                OrbConfig::base_label(orb_cfg.account_label).c_str(),
                orb_cfg.starting_balance + hist_pnl, hist_peak,
                (orb_cfg.starting_balance + hist_pnl) - (hist_peak - orb_cfg.trailing_drawdown_cap),
                orb_cfg.trailing_drawdown_cap);
            // Seed today's realized P&L so a restarted process cannot re-spend the
            // daily loss limit already consumed before the restart.
            risk.seed_daily_pnl(db->seed_daily_pnl(orb_cfg.account_label, today_date_str()));
        }
    } catch (std::exception& e) {
        LOG("[EXECUTOR] WARNING: OrbDB failed (%s) — trades will not be persisted", e.what());
    }
    // Session-shape regime gate (paper_quote.hpp RegimeState): the same gate the paper
    // brokers apply, so a regime-gated paper variant (__rg_*) behaves the same live. ATR
    // comes from session_stats for the collector's feed symbol; set at session start and
    // on the date rollover below.
    auto seed_regime_atr = [&](const std::string& d) {
        const double atr = (db && db->is_connected()) ? db->session_atr14_before(regime_symbol, d) : 0.0;
        regime.set_atr(d, atr);
        if (paper::RegimeState::any_gate(orb_cfg))
            LOG("[EXECUTOR] Regime gate: date=%s prior ATR14=%.1f pts%s", d.c_str(), atr,
                atr > 0.0 ? "" : " (none — regime-gated entries stay blocked)");
    };

    // ── AuditLog setup ────────────────────────────────────────────────────────
    PGconn* audit_conn = PQconnectdb(orb_cfg.pg_connstr().c_str());
    if (!audit_conn || PQstatus(audit_conn) != CONNECTION_OK) {
        LOG("[EXECUTOR] WARNING: AuditLog DB connect failed — audit disabled");
        if (audit_conn) { PQfinish(audit_conn); audit_conn = nullptr; }
    }
    if (audit_conn) {
        // Ensure audit_log table exists (not created by OrbDB schema — executor owns this)
        auto pg_exec_silent = [&](const char* sql) {
            PGresult* r = PQexec(audit_conn, sql);
            if (r && PQresultStatus(r) != PGRES_COMMAND_OK &&
                PQresultStatus(r) != PGRES_TUPLES_OK)
                LOG("[EXECUTOR] audit schema: %s", PQresultErrorMessage(r));
            PQclear(r);
        };
        pg_exec_silent(
            "CREATE TABLE IF NOT EXISTS audit_log ("
            "  id       BIGSERIAL PRIMARY KEY,"
            "  ts       TIMESTAMPTZ DEFAULT NOW(),"
            "  source   VARCHAR(32) DEFAULT 'engine',"
            "  event    VARCHAR(64) NOT NULL,"
            "  severity VARCHAR(8) DEFAULT 'INFO',"
            "  details  TEXT"
            ");");
        pg_exec_silent("CREATE INDEX IF NOT EXISTS idx_audit_ts  ON audit_log(ts);");
        pg_exec_silent("CREATE INDEX IF NOT EXISTS idx_audit_sev ON audit_log(severity, ts DESC);");
    }
    AuditLog audit_log(audit_conn);

    // ── Order plant setup ─────────────────────────────────────────────────────
    auto order_plant = std::make_shared<OrderPlant>();
    order_plant->account_id = ""; // populated after login
    order_plant->fcm_id     = ""; // populated after login
    order_plant->ib_id      = ""; // populated after login

    // Wire order_mgr → order_plant (uses order_plant->trade_symbol for the specific contract)
    order_mgr.set_order_callback(
        [&order_plant, &orb_cfg, &audit_log, &db](const std::string& basket_id,
                                 const std::string& /*symbol*/,
                                 const std::string& exchange,
                                 int qty, int order_type, bool is_buy,
                                 double price, const std::string& user_tag) -> bool {
            const std::string& sym = order_plant->trade_symbol.empty()
                                   ? orb_cfg.symbol : order_plant->trade_symbol;
            std::string result = order_plant->send_new_order(
                basket_id, sym, exchange, qty, order_type,
                is_buy, price, user_tag, orb_cfg.dry_run);
            if (!result.empty()) {
                audit_log.info("order.submitted",
                    basket_id + " " + (is_buy ? "BUY" : "SELL"));
            }
            if (db) db->write_order_event(result.empty() ? "new_order_failed" : "new_order_sent",
                                          basket_id, "", user_tag, order_type, "",
                                          is_buy ? "BUY" : "SELL", qty, price, 0.0, 0, 0, "",
                                          orb_cfg.dry_run ? "dry_run" : sym);
            return !result.empty();
        }
    );
    order_mgr.set_cancel_callback(
        [&order_plant, &db](const std::string& basket_id) {
            order_plant->send_cancel(basket_id, order_plant->account_id);
            if (db) db->write_order_event("cancel_sent", basket_id, "", "", 316, "", "",
                                          0, 0.0, 0.0, 0, 0, "", "");
        }
    );
    // ── SSL context, executor refs, and trade symbol (shared setup) ─────────────
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.load_verify_file("certs/rithmic_ssl_cert_auth_params");
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    auto ex = co_await asio::this_coro::executor;
    asio::io_context& ioc = static_cast<asio::io_context&>(ex.context());

    std::string trade_symbol = orb_cfg.trade_contract.empty() ? orb_cfg.symbol : orb_cfg.trade_contract;
    order_plant->trade_symbol = trade_symbol;
    LOG("[EXECUTOR] Trading contract: %s", trade_symbol.c_str());

    // md_ws is only used in WebSocket mode; stays null in SDK mode.
    std::unique_ptr<WsStream> md_ws;

    // Per-stream write queues: serialize every write to the shared MD and
    // ORDER_PLANT streams (finding: Beast forbids overlapping async_write).
    WsWriteQueue md_write_q;
    WsWriteQueue op_write_q;
    WsWriteQueue pnl_write_q;   // PNL_PLANT stream (position / P&L updates)

    // pg mode: true while the collector's ticks are arriving; the stale-feed
    // watchdog clears it. md_up() is what the UI/DB see as "MD connected".
    std::atomic<bool> pg_feed_fresh{false};
    auto md_up = [&]() -> bool {
        return orb_cfg.md_from_pg() ? pg_feed_fresh.load() : bool(md_ws);
    };

#ifndef USE_RAPI_SDK
    // ── MD plant connection (WebSocket — skipped when USE_RAPI_SDK is set) ───────
    // In SDK mode the native R|API+ TCP feed owns the AMP session; opening a
    // WebSocket session simultaneously triggers a FORCED LOGOUT storm.
    // In pg mode there is no MD session either — the collector owns it.
    if (orb_cfg.md_from_pg()) {
        LOG("[PG-FEED] MD provider=pg: no Rithmic MD session; ticks come from the "
            "collector's Postgres feed (symbol=%s, poll=%dms)",
            orb_cfg.md_feed_symbol.c_str(), orb_cfg.md_poll_ms);
    } else {
    LOG("[EXECUTOR] Connecting to MD plant: %s", orb_cfg.md_url.c_str());
    try {
        md_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.md_url);
        LOG("[EXECUTOR] MD plant WS connected");
    } catch (std::exception& e) {
        LOG("[EXECUTOR] FATAL: MD plant connect failed: %s", e.what());
        co_return;
    }

    // System info — log available systems to help diagnose login failures
    {
        rti::RequestRithmicSystemInfo req;
        req.set_template_id(16);
        co_await ws_write(*md_ws, proto_frame(req));
        beast::flat_buffer buf;
        for (;;) {
            buf.clear();
            co_await md_ws->async_read(buf, asio::use_awaitable);
            auto payload = proto_strip(beast::buffers_to_string(buf.data()));
            rti::Base base;
            if (!base.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
            if (base.template_id() == 17) {
                rti::ResponseRithmicSystemInfo sinfo;
                if (sinfo.ParseFromString(payload)) {
                    std::string avail;
                    for (auto& sn : sinfo.system_name()) avail += "[" + sn + "] ";
                    LOG("[EXECUTOR] MD plant available systems: %s", avail.c_str());
                    bool found = false;
                    for (auto& sn : sinfo.system_name())
                        if (sn == orb_cfg.md_system_name) { found = true; break; }
                    if (!found)
                        LOG("[EXECUTOR] WARNING: MD system '%s' NOT in list — login will likely fail",
                            orb_cfg.md_system_name.c_str());
                }
                break;
            }
        }
    }

    // Rithmic protocol: close probe connection, reconnect for login
    try { md_ws->close(websocket::close_code::normal); } catch (...) {}
    md_ws.reset();
    try {
        md_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.md_url);
        LOG("[EXECUTOR] MD plant reconnected for login");
    } catch (std::exception& e) {
        LOG("[EXECUTOR] FATAL: MD plant reconnect failed: %s", e.what());
        co_return;
    }
    md_write_q.attach(md_ws.get());

    // MD plant login — use AMP credentials (separate session from Legends ORDER_PLANT)
    {
        LOG("[EXECUTOR] MD Login: user=*** system=%s",
            orb_cfg.md_system_name.c_str());
        rti::RequestLogin req;
        req.set_template_id(10);
        req.set_template_version("3.9");
        req.set_user(orb_cfg.md_user);
        req.set_password(orb_cfg.md_password);
        req.set_system_name(orb_cfg.md_system_name);
        req.set_app_name(orb_cfg.app_name + "-MD");
        req.set_app_version(orb_cfg.app_version);
        req.set_infra_type(rti::RequestLogin::TICKER_PLANT);
        co_await md_write_q.write(proto_frame(req));

        beast::flat_buffer buf;
        beast::get_lowest_layer(*md_ws).expires_after(std::chrono::seconds(15));
        for (;;) {
            buf.clear();
            try {
                co_await md_ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                // Network fault during login (timeout, reset) — transient: co_return
                // and let the outer while-loop reconnect and retry in 10s.
                LOG("[EXECUTOR] MD login read error (network): %s — will retry via reconnect",
                    e.what());
                co_return;
            }
            std::string payload;
            try {
                payload = proto_strip(beast::buffers_to_string(buf.data()));
            } catch (std::exception& e) {
                LOG("[EXECUTOR] MD login: malformed frame (%s) — skipping", e.what());
                continue;
            }
            rti::Base base;
            if (!base.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
            if (base.template_id() == 11) {
                rti::ResponseLogin resp;
                if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                bool ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                if (!ok) {
                    // The server ANSWERED the login — this is an auth/config rejection,
                    // not a network fault. Retrying with the same credentials would
                    // loop forever, so stop the process instead.
                    std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                    std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
                    LOG("[EXECUTOR] FATAL: MD login REJECTED by server (rp_code=%s %s) — "
                        "terminal, not retrying. Check MD credentials/system name.",
                        rpc.c_str(), txt.c_str());
                    audit_log.info("session.md_login_rejected",
                        "MD auth rejected rp_code=" + rpc + " " + txt);
                    g_running = false;
                    co_return;
                }
                double hb_interval = resp.heartbeat_interval();
                LOG("[EXECUTOR] MD plant login OK (heartbeat_interval=%.0fs)", hb_interval);
                audit_log.info("session.md_login", "MD plant login OK");
                break;
            }
        }
        beast::get_lowest_layer(*md_ws).expires_never();
        // Server requires immediate heartbeat after login
        {
            rti::RequestHeartbeat hb;
            hb.set_template_id(18);
            hb.set_ssboe(hb_ssboe_now());
            co_await md_write_q.write(proto_frame(hb));
        }
    }

    // Subscribe to NQ last trade
    {
        rti::RequestMarketDataUpdate req;
        req.set_template_id(100);
        req.set_symbol(trade_symbol);
        req.set_exchange(orb_cfg.exchange);
        req.set_request(rti::RequestMarketDataUpdate::SUBSCRIBE);
        req.set_update_bits(1);  // LAST_TRADE
        co_await md_write_q.write(proto_frame(req));
        LOG("[EXECUTOR] Subscribed to %s/%s last trade",
            trade_symbol.c_str(), orb_cfg.exchange.c_str());
    }
    }  // !md_from_pg()
#endif  // !USE_RAPI_SDK

    // ── ORDER_PLANT connection (live mode only) ───────────────────────────────
    std::unique_ptr<WsStream> op_ws;
    // fcm/ib as Rithmic reports them for this login (ResponseLogin, refined by
    // the account list below). Config values are only the fallback: a wrong
    // hand-typed pair makes every order-plant request for the account fail
    // with rp_code=1088 "user has no permission to this account".
    std::string fcm_id_r = orb_cfg.fcm_id;
    std::string ib_id_r  = orb_cfg.ib_id;
    if (!orb_cfg.dry_run) {
        LOG("[EXECUTOR] Connecting to ORDER_PLANT: %s", orb_cfg.rithmic_url.c_str());
        try {
            // System info probe
            auto probe = co_await connect_ws(ioc, ssl_ctx, orb_cfg.rithmic_url);
            {
                rti::RequestRithmicSystemInfo req;
                req.set_template_id(16);
                co_await ws_write(*probe, proto_frame(req));
                beast::flat_buffer buf;
                for (;;) {
                    buf.clear();
                    co_await probe->async_read(buf, asio::use_awaitable);
                    auto payload = proto_strip(beast::buffers_to_string(buf.data()));
                    rti::Base base;
                    if (!base.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (base.template_id() == 17) {
                        rti::ResponseRithmicSystemInfo sinfo;
                        if (!sinfo.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                        std::string avail;
                        for (auto& sn : sinfo.system_name()) avail += "[" + sn + "] ";
                        LOG("[EXECUTOR] ORDER_PLANT available systems: %s", avail.c_str());
                        bool found = false;
                        for (auto& sn : sinfo.system_name())
                            if (sn == orb_cfg.rithmic_system_name) { found = true; break; }
                        if (!found)
                            LOG("[EXECUTOR] WARNING: '%s' NOT in system list — login will likely fail",
                                orb_cfg.rithmic_system_name.c_str());
                        break;
                    }
                }
            }
            try { probe->close(websocket::close_code::normal); } catch (...) {}
            probe.reset();

            // Reconnect for login
            op_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.rithmic_url);
            {
                rti::RequestLogin req;
                req.set_template_id(10);
                req.set_template_version("3.9");
                req.set_user(orb_cfg.rithmic_user);
                req.set_password(orb_cfg.rithmic_password);
                req.set_system_name(orb_cfg.rithmic_system_name);
                req.set_app_name(orb_cfg.app_name);
                req.set_app_version(orb_cfg.app_version);
                req.set_infra_type(rti::RequestLogin::ORDER_PLANT);
                LOG("[EXECUTOR] ORDER_PLANT Login: user=*** system=%s",
                    orb_cfg.rithmic_system_name.c_str());
                co_await ws_write(*op_ws, proto_frame(req));

                beast::flat_buffer buf;
                beast::get_lowest_layer(*op_ws).expires_after(std::chrono::seconds(15));
                for (;;) {
                    buf.clear();
                    co_await op_ws->async_read(buf, asio::use_awaitable);
                    auto payload = proto_strip(beast::buffers_to_string(buf.data()));
                    rti::Base base;
                    if (!base.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (base.template_id() == 11) {
                        rti::ResponseLogin resp;
                        if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                        bool ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                        if (!ok) {
                            // The server ANSWERED the login — auth/config refusal,
                            // not a network fault. rp_code 13 is returned both for
                            // "too many rapid logins / duplicate session" (clears
                            // by itself) and for bad credentials, so retry on a
                            // long cadence rather than every 10s: rapid retries
                            // are exactly what keeps 13 coming back.
                            std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                            std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
                            LOG("[EXECUTOR] FATAL: ORDER_PLANT login REJECTED by server "
                                "(rp_code=%s %s) — retrying in %ds. Check %s_USER/"
                                "%s_PASSWORD (an unquoted password with shell "
                                "metacharacters is truncated when the env file is "
                                "sourced) and that no other session holds this login.",
                                rpc.c_str(), txt.c_str(), kReconnectDelayRefusedS,
                                orb_cfg.order_env_prefix.c_str(),
                                orb_cfg.order_env_prefix.c_str());
                            audit_log.error("session.order_plant_login_rejected",
                                "ORDER_PLANT auth rejected rp_code=" + rpc + " " + txt);
                            g_reconnect_delay_s = kReconnectDelayRefusedS;
                            co_return;
                        }
                        g_reconnect_delay_s = kReconnectDelayNormalS;
                        if (!resp.fcm_id().empty()) fcm_id_r = resp.fcm_id();
                        if (!resp.ib_id().empty())  ib_id_r  = resp.ib_id();
                        LOG("[EXECUTOR] ORDER_PLANT login OK unique_user_id=%s fcm_id='%s' ib_id='%s'%s",
                            resp.unique_user_id().c_str(), fcm_id_r.c_str(), ib_id_r.c_str(),
                            (fcm_id_r != orb_cfg.fcm_id || ib_id_r != orb_cfg.ib_id)
                                ? " (differs from config — using the server's values)" : "");
                        audit_log.info("session.order_plant_login", "ORDER_PLANT login OK");
                        break;
                    }
                }
                beast::get_lowest_layer(*op_ws).expires_never();
            }
            // Server requires immediate heartbeat after login
            {
                rti::RequestHeartbeat hb;
                hb.set_template_id(18);
                hb.set_ssboe(hb_ssboe_now());
                co_await ws_write(*op_ws, proto_frame(hb));
            }
            order_plant->ws         = std::move(op_ws);
            op_write_q.attach(order_plant->ws.get());
            order_plant->write_q    = &op_write_q;
            order_plant->account_id  = orb_cfg.account_id;
            order_plant->fcm_id      = fcm_id_r;
            order_plant->ib_id       = ib_id_r;
            order_plant->trade_route = orb_cfg.trade_route;  // fallback; overridden below
            order_plant->connected   = true;
            LOG("[EXECUTOR] ORDER_PLANT connected — live orders enabled (account=%s)",
                orb_cfg.account_id.c_str());

            // Query available trade routes (tid=310) — discovers the correct route
            // string for this FCM/account instead of relying on a hardcoded config value.
            // subscribe_for_updates=false: one-shot query; ongoing updates would be
            // unhandled noise in op_loop (no tid=311 case there).
            {
                rti::RequestTradeRoutes tr_req;
                tr_req.set_template_id(310);
                tr_req.set_subscribe_for_updates(false);
                co_await op_write_q.write(proto_frame(tr_req));
                LOG("[ORDER_PLANT] Sent RequestTradeRoutes (tid=310)");

                beast::flat_buffer tr_buf;
                bool route_found = false;
                for (int i = 0; i < 50; ++i) {
                    tr_buf.clear();
                    beast::get_lowest_layer(*order_plant->ws).expires_after(
                        std::chrono::seconds(5));
                    try {
                        co_await order_plant->ws->async_read(tr_buf, asio::use_awaitable);
                    } catch (std::exception& e) {
                        LOG("[ORDER_PLANT] Trade route read timeout/error: %s — "
                            "using config fallback '%s'",
                            e.what(), order_plant->trade_route.c_str());
                        // Reconnect: timer killed the socket, can't reuse it.
                        break;
                    }
                    beast::get_lowest_layer(*order_plant->ws).expires_never();
                    auto pl = proto_strip(beast::buffers_to_string(tr_buf.data()));
                    rti::Base base; if (!base.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (base.template_id() != 311) continue;
                    rti::ResponseTradeRoutes tr;
                    if (!tr.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    LOG("[ORDER_PLANT] ResponseTradeRoutes: exch=%s route='%s' "
                        "status='%s' is_default=%d fcm=%s ib=%s rp=%s",
                        tr.exchange().c_str(), tr.trade_route().c_str(),
                        tr.status().c_str(), (int)tr.is_default(),
                        tr.fcm_id().c_str(), tr.ib_id().c_str(),
                        tr.rp_code().empty() ? "" : tr.rp_code(0).c_str());
                    if (tr.exchange() == orb_cfg.exchange && !tr.trade_route().empty()) {
                        // NEVER adopt "Rithmic Order Routing" — on Legends accounts it
                        // silently cancels every order (rp_code=1043, notify_type=15,
                        // total_fill=0; order never reaches the exchange).
                        if (tr.trade_route() == "Rithmic Order Routing") {
                            LOG("[ORDER_PLANT] Ignoring forbidden route 'Rithmic Order Routing' "
                                "(silent-cancel route on Legends) — keeping '%s'",
                                order_plant->trade_route.c_str());
                        } else if (!route_found || tr.is_default()) {
                            order_plant->trade_route = tr.trade_route();
                            route_found = true;
                        }
                    }
                    // rp_code="0" = normal end-of-list; other non-empty codes (e.g. "1043"
                    // = no routes on new accounts) are also terminal — break on any.
                    if (!tr.rp_code().empty()) break;
                }
                if (route_found)
                    LOG("[ORDER_PLANT] Resolved trade_route='%s'", order_plant->trade_route.c_str());
                else
                    LOG("[ORDER_PLANT] WARNING: no CME route found (rp_code=1043) — using Legends fallback '%s'",
                        order_plant->trade_route.c_str());
            }

            // Verify the configured account against the login's account list
            // (tid=302 RequestAccountList → 303) and adopt the fcm_id/ib_id
            // Rithmic reports for it. 2026-09-21: the Tradeify config carried
            // fcm/ib "Tradeify"/"Tradeify" and every order-plant request for
            // the account came back 1088 "user has no permission to this
            // account" — orders would have been refused and fills never seen.
            {
                rti::RequestAccountList al_req;
                al_req.set_template_id(302);
                if (!fcm_id_r.empty()) al_req.set_fcm_id(fcm_id_r);
                if (!ib_id_r.empty())  al_req.set_ib_id(ib_id_r);
                al_req.set_user_type(3);  // USER_TYPE_TRADER
                co_await op_write_q.write(proto_frame(al_req));
                LOG("[ORDER_PLANT] Sent RequestAccountList (tid=302)");

                beast::flat_buffer al_buf;
                bool got_list = false, matched = false;
                std::string avail;
                for (int i = 0; i < 50; ++i) {
                    al_buf.clear();
                    beast::get_lowest_layer(*order_plant->ws).expires_after(
                        std::chrono::seconds(5));
                    try {
                        co_await order_plant->ws->async_read(al_buf, asio::use_awaitable);
                    } catch (std::exception& e) {
                        LOG("[ORDER_PLANT] Account list read timeout/error: %s — "
                            "keeping fcm_id='%s' ib_id='%s'",
                            e.what(), fcm_id_r.c_str(), ib_id_r.c_str());
                        break;
                    }
                    beast::get_lowest_layer(*order_plant->ws).expires_never();
                    auto pl = proto_strip(beast::buffers_to_string(al_buf.data()));
                    rti::Base base; if (!base.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (base.template_id() != 303) continue;
                    rti::ResponseAccountList al;
                    if (!al.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (!al.account_id().empty()) {
                        got_list = true;
                        avail += al.account_id() + "(" + al.fcm_id() + "/" + al.ib_id() + ") ";
                        LOG("[ORDER_PLANT] ResponseAccountList: account=%s name='%s' fcm=%s ib=%s rp=%s",
                            al.account_id().c_str(), al.account_name().c_str(),
                            al.fcm_id().c_str(), al.ib_id().c_str(),
                            al.rp_code().empty() ? "" : al.rp_code(0).c_str());
                        if (al.account_id() == orb_cfg.account_id) {
                            matched = true;
                            if (al.fcm_id() != fcm_id_r || al.ib_id() != ib_id_r) {
                                LOG("[ORDER_PLANT] Adopting fcm_id='%s' ib_id='%s' for account %s "
                                    "(was '%s'/'%s')",
                                    al.fcm_id().c_str(), al.ib_id().c_str(),
                                    orb_cfg.account_id.c_str(),
                                    fcm_id_r.c_str(), ib_id_r.c_str());
                                fcm_id_r = al.fcm_id();
                                ib_id_r  = al.ib_id();
                                order_plant->fcm_id = fcm_id_r;
                                order_plant->ib_id  = ib_id_r;
                            }
                        }
                    } else if (!al.rp_code().empty() && al.rp_code(0) != "0") {
                        LOG("[ORDER_PLANT] RequestAccountList rejected (rp_code=%s %s)",
                            al.rp_code(0).c_str(),
                            al.rp_code().size() > 1 ? al.rp_code(1).c_str() : "");
                    }
                    // rp_code present = terminal message of the list.
                    if (!al.rp_code().empty()) break;
                }
                if (got_list && !matched) {
                    LOG("[EXECUTOR] FATAL: account_id '%s' is not in this login's account list "
                        "[%s] — every order and the order-update subscription would be refused "
                        "(rp_code=1088). Fix account_id in the config or %s_ACCOUNT. Retrying in %ds.",
                        orb_cfg.account_id.c_str(), avail.c_str(),
                        orb_cfg.order_env_prefix.c_str(), kReconnectDelayRefusedS);
                    audit_log.error("session.account_not_permitted",
                        "account_id " + orb_cfg.account_id + " not in login's account list [" + avail + "]");
                    g_reconnect_delay_s = kReconnectDelayRefusedS;
                    co_return;
                }
                if (!got_list)
                    LOG("[ORDER_PLANT] WARNING: account list unavailable — using fcm_id='%s' ib_id='%s'",
                        fcm_id_r.c_str(), ib_id_r.c_str());
            }

            // Subscribe to order updates (template 308 = RequestSubscribeForOrderUpdates).
            // Without this subscription Rithmic will NOT deliver tid=351/352 notifications.
            {
                rti::RequestSubscribeForOrderUpdates sub;
                sub.set_template_id(308);
                sub.set_fcm_id(fcm_id_r);
                sub.set_ib_id(ib_id_r);
                sub.set_account_id(orb_cfg.account_id);
                try {
                    co_await op_write_q.write(proto_frame(sub));
                    LOG("[EXECUTOR] Sent RequestSubscribeForOrderUpdates (tid=308)");
                } catch (std::exception& e) {
                    LOG("[EXECUTOR] WARNING: Failed to send order update subscription: %s", e.what());
                }
            }

            // Drain any cancels that were queued while the order plant was offline.
            // Must run after connected=true and after tid=308 subscription so the
            // exchange can immediately ACK/confirm the cancel notifications.
            order_plant->drain_pending_cancels();

            // Reconnect-with-position reconciliation (#1): entries were halted at the
            // top of this coroutine. Subscribe to PnL/position updates so the tid=451
            // snapshot can confirm net_qty and auto-unwind — the same auto-recovery the
            // fresh-start path below uses. Without this the halt persisted forever.
            // Position/P&L subscription is sent on the PNL_PLANT session below.
            // (It was sent here, on the ORDER_PLANT, until 2026-09-23 — and was
            // never answered: Rithmic serves tid=400/401/451 on the PNL plant.)

        } catch (std::exception& e) {
            LOG("[EXECUTOR] FATAL: ORDER_PLANT connect failed: %s", e.what());
            co_return;
        }
    }


    // ── PNL_PLANT connection (live mode only) ─────────────────────────────────
    // Position and P&L updates (tid=400 → 401 → 451 stream) are served by
    // Rithmic's PNL plant, NOT the order plant. Until 2026-09-23 the subscribe
    // went out on the order plant and was never answered, so the executor had
    // no source of truth for what the account held — an orphaned stop's fill
    // left it long 2 MNQ for 32 minutes. Without this session entries stay
    // halted: no position feed, no trading.
    std::unique_ptr<WsStream> pnl_ws;
    bool pnl_connected = false;
    if (!orb_cfg.dry_run) {
        LOG("[EXECUTOR] Connecting to PNL_PLANT: %s", orb_cfg.rithmic_url.c_str());
        try {
            pnl_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.rithmic_url);
            rti::RequestLogin req;
            req.set_template_id(10);
            req.set_template_version("3.9");
            req.set_user(orb_cfg.rithmic_user);
            req.set_password(orb_cfg.rithmic_password);
            req.set_system_name(orb_cfg.rithmic_system_name);
            req.set_app_name(orb_cfg.app_name);
            req.set_app_version(orb_cfg.app_version);
            req.set_infra_type(rti::RequestLogin::PNL_PLANT);
            co_await ws_write(*pnl_ws, proto_frame(req));
            beast::flat_buffer buf;
            beast::get_lowest_layer(*pnl_ws).expires_after(std::chrono::seconds(15));
            bool login_ok = false;
            for (;;) {
                buf.clear();
                co_await pnl_ws->async_read(buf, asio::use_awaitable);
                auto payload = proto_strip(beast::buffers_to_string(buf.data()));
                rti::Base base;
                if (!base.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                if (base.template_id() == 11) {
                    rti::ResponseLogin resp;
                    if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    login_ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                    if (!login_ok) {
                        std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                        std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
                        LOG("[EXECUTOR] CRITICAL: PNL_PLANT login REJECTED (rp_code=%s %s)",
                            rpc.c_str(), txt.c_str());
                        audit_log.error("session.pnl_plant_login_rejected",
                            "PNL_PLANT auth rejected rp_code=" + rpc + " " + txt);
                    } else {
                        LOG("[EXECUTOR] PNL_PLANT login OK unique_user_id=%s",
                            resp.unique_user_id().c_str());
                    }
                    break;
                }
            }
            beast::get_lowest_layer(*pnl_ws).expires_never();
            if (login_ok) {
                rti::RequestHeartbeat hb;
                hb.set_template_id(18);
                hb.set_ssboe(hb_ssboe_now());
                co_await ws_write(*pnl_ws, proto_frame(hb));
                pnl_write_q.attach(pnl_ws.get());
                pnl_connected = true;
                rti::RequestPnLPositionUpdates pnl_req;
                pnl_req.set_template_id(400);
                pnl_req.set_request(rti::RequestPnLPositionUpdates::SUBSCRIBE);
                pnl_req.set_fcm_id(fcm_id_r);
                pnl_req.set_ib_id(ib_id_r);
                pnl_req.set_account_id(orb_cfg.account_id);
                co_await pnl_write_q.write(proto_frame(pnl_req));
                LOG("[EXECUTOR] PNL_PLANT connected — RequestPnLPositionUpdates SUBSCRIBE sent "
                    "(acct=%s fcm=%s ib=%s); awaiting tid=401 ack + tid=451 snapshot",
                    orb_cfg.account_id.c_str(), fcm_id_r.c_str(), ib_id_r.c_str());
                // The subscription only streams on change; an idle account can stay
                // silent for minutes. Force an initial snapshot so the reconciler
                // starts from the exchange's truth, not from silence.
                rti::RequestPnLPositionSnapshot snap_req;
                snap_req.set_template_id(402);
                snap_req.set_fcm_id(fcm_id_r);
                snap_req.set_ib_id(ib_id_r);
                snap_req.set_account_id(orb_cfg.account_id);
                co_await pnl_write_q.write(proto_frame(snap_req));
                LOG("[EXECUTOR] PNL_PLANT RequestPnLPositionSnapshot (tid=402) sent");
            }
        } catch (std::exception& e) {
            LOG("[EXECUTOR] CRITICAL: PNL_PLANT connect failed: %s", e.what());
        }
        if (!pnl_connected) {
            strategy.halt_trading("pnl_plant_unavailable");
            audit_log.error("session.pnl_plant",
                "PNL_PLANT unavailable — no position feed, entries halted");
        }
    }

    // ── ORDER_PLANT fill receive loop ─────────────────────────────────────────
    // Carried-position reconnect (#1): a working order in the tid=351 startup
    // snapshot may be the carried position's protective stop. Defer those Case-A
    // cancels until the tid=451 snapshot confirms net_qty==0 (stale → cancel) or
    // net_qty!=0 (position live — the stop must stay).
    std::vector<std::string> deferred_snapshot_cancels;
    bool defer_snapshot_cancels = carried_nonflat;

    // Exchange-vs-memory position reconciliation state (fed by every tid=451 update).
    NetReconciler net_recon;
    bool   net_halt_active    = false;
    bool   broker_loss_halted = false;
    double last_broker_bal    = std::nan("");
    double last_broker_dpnl   = std::nan("");
    // Prop-firm trailing drawdown on the broker's balance (notif::broker_hwm): the mark
    // is persisted per live label (live_account_hwm) so a restart cannot forget it.
    bool   broker_dd_halted   = false;
    double last_broker_hwm_logged_bal = std::nan("");
    double broker_hwm         = notif::broker_hwm(
        (db && db->is_connected()) ? db->get_account_hwm() : std::nan(""),
        orb_cfg.starting_balance, std::nan(""));
    LOG("[EXECUTOR] [BROKER-HWM] seed=%.2f (label=%s, cap=%.0f) — halt when balance <= %.2f",
        broker_hwm, OrbConfig::base_label(orb_cfg.account_label).c_str(),
        orb_cfg.trailing_drawdown_cap, broker_hwm - orb_cfg.trailing_drawdown_cap);
    int         last_exch_net = INT_MIN;   // last tid=451 net (reconciler re-check each second)
    std::string last_unwind_basket;        // last unwind sent — cancelled before a retry

    // Send an order that closes `qty` contracts the exchange holds and we do not
    // (startup ghost, continuous mismatch). Aggressive LIMIT off the last price
    // (prop routes may reject MARKET); MARKET only when no price exists yet. The
    // basket is registered so its fill is recognised as a correction.
    auto send_unwind = [&](int qty, bool unwind_is_buy, const char* why)
        -> asio::awaitable<bool> {
        if (qty <= 0 || !order_plant->connected || !order_plant->ws) co_return false;
        std::string basket_id = orb_cfg.symbol + "-unwind-" +
            std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        constexpr double UNWIND_TICK = 0.25;
        constexpr int    UNWIND_OFFSET_TICKS = 50; // ~12.5 pts: crosses the spread
        double last_px = strategy.last_price();
        int    order_type = 2; // MARKET
        double limit_px   = 0.0;
        if (last_px > 0.0) {
            order_type = 1; // LIMIT
            limit_px = unwind_is_buy ? last_px + UNWIND_OFFSET_TICKS * UNWIND_TICK
                                     : last_px - UNWIND_OFFSET_TICKS * UNWIND_TICK;
        } else {
            LOG("[EXECUTOR] [%s] WARNING: no price ref for unwind — sending MARKET "
                "(may be rejected; close via RTrader if so)", why);
        }
        rti::RequestNewOrder req;
        req.set_template_id(312);
        req.set_fcm_id(fcm_id_r);
        req.set_ib_id(ib_id_r);
        req.set_account_id(orb_cfg.account_id);
        req.set_symbol(trade_symbol);
        req.set_exchange(orb_cfg.exchange);
        req.set_quantity(qty);
        req.set_order_type(order_type);
        if (order_type == 1) req.set_price(limit_px);
        req.set_transaction_type(unwind_is_buy ? 1 : 2);
        req.set_user_tag(basket_id);
        req.set_duration(rti::RequestNewOrder::DAY);
        req.set_manual_or_auto_select(rti::RequestNewOrder::AUTO);
        req.set_trade_route(order_plant->trade_route);
        try {
            co_await op_write_q.write(proto_frame(req));
            LOG("[EXECUTOR] [%s] unwind sent: %s %s px=%.2f qty=%d basket=%s",
                why, unwind_is_buy ? "BUY" : "SELL", order_type == 1 ? "LIMIT" : "MARKET",
                limit_px, qty, basket_id.c_str());
            order_mgr.register_unwind_basket(basket_id);
            last_unwind_basket = basket_id;
            co_return true;
        } catch (std::exception& e) {
            LOG("[EXECUTOR] [%s] unwind send FAILED: %s — MANUAL INTERVENTION REQUIRED",
                why, e.what());
            co_return false;
        }
    };
    // ── Exchange-vs-memory reconciliation ─────────────────────────────────────
    // Run on every tid=451 update AND once a second from eod_loop while the last known
    // exchange net disagrees with us (451 updates only arrive on account changes, so a
    // resting unwind would otherwise never be retried).
    auto reconcile_net = [&](int net, bool consistent, int64_t now_ms) -> asio::awaitable<void> {
        auto verdict = net_recon.observe(consistent, now_ms, orb_cfg.net_mismatch_grace_ms,
                                         /*retry_ms=*/15000);
        // Exchange flat and we are flat: nothing is open, so a ghost-fill halt from an
        // unknown fill that has since netted out must not block entries for the rest of
        // the session (it only cleared on a snapshot or after the reconciler had acted).
        if (net == 0 && consistent && order_mgr.is_flat()) order_mgr.confirm_exchange_flat();
        if (verdict == NetReconciler::Verdict::MISMATCH_ACT) {
            auto snap = order_mgr.position_snapshot();
            auto plan = notif::plan_unwind(net, snap, orb_cfg.qty);
            int expected = plan.expected;
            int diff = net - expected;
            LOG("[EXECUTOR] CRITICAL: [NET-RECON] exchange net=%d but we hold %d "
                "(state=%d) for >%dms — unwinding %d and halting entries",
                net, expected, (int)snap.state, orb_cfg.net_mismatch_grace_ms,
                std::abs(diff));
            audit_log.error("position.net_mismatch",
                "exchange net " + std::to_string(net) + " vs ours " +
                std::to_string(expected));
            strategy.halt_trading("exchange_net_mismatch");
            net_halt_active = true;
            if (plan.adopt_flat) {
                // Our tracked position is gone from the exchange: cancel the stop and
                // close the record — do not re-enter to match a book that is wrong.
                order_mgr.adopt_external_close(strategy.last_price(), "external_close");
                flush_position(db.get(), today, order_mgr, strategy,
                               orb_cfg.dry_run || order_plant->connected,
                               orb_cfg.point_value, md_up());
            }
            // A retry: the previous unwind is still resting (or was rejected) — cancel it by
            // server id first so a late fill cannot overshoot the fresh one.
            if (!last_unwind_basket.empty()) {
                const std::string cid = order_mgr.routable_id(last_unwind_basket);
                LOG("[EXECUTOR] [NET-RECON] mismatch persists — cancelling previous unwind %s (id %s)",
                    last_unwind_basket.c_str(), cid.c_str());
                if (!orb_cfg.dry_run && order_plant->connected) order_plant->send_cancel(cid, orb_cfg.account_id);
                last_unwind_basket.clear();
            }
            bool sent = plan.qty > 0 ? co_await send_unwind(plan.qty, plan.is_buy, "NET-RECON")
                                     : true;
            int notify_rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                         ": POSITION MISMATCH exchange net=" + std::to_string(net) +
                         " ours=" + std::to_string(expected) +
                         (sent ? " — unwind sent, entries halted" :
                                 " — UNWIND SEND FAILED, close manually") +
                         "\" >/dev/null 2>&1 &").c_str());
            (void)notify_rc;  // fail-open
        } else if (verdict == NetReconciler::Verdict::OK) {
            last_unwind_basket.clear();
        }
        if (verdict == NetReconciler::Verdict::OK && net_halt_active) {
            net_halt_active = false;
            if (net == 0) order_mgr.confirm_exchange_flat();
            LOG("[EXECUTOR] [NET-RECON] exchange net=%d consistent again — mismatch cleared",
                net);
            if (g_drill_sent && !g_drill_passed.exchange(true)) {
                int64_t took = now_ms - g_drill_sent_ms;
                LOG("[DRILL] PASS: untracked position detected and closed; exchange flat "
                    "again %lld ms after the orphan order — exiting", (long long)took);
                int notify_rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                    ": DRILL PASS — orphan position closed in " + std::to_string(took) +
                    " ms\" >/dev/null 2>&1 &").c_str());
                (void)notify_rc;
                g_running = false;
                ioc_ref.stop();
            }
            if (strategy.session().halt_reason == "exchange_net_mismatch" ||
                strategy.session().halt_reason == "unowned_fill_in_trade")
                strategy.unhalt_trading("exchange_net_consistent");
        }
    };

    // ── Position / P&L update (tid=451) ──────────────────────────────────────
    // Shared by pnl_loop (the PNL plant, where Rithmic actually serves these)
    // and op_loop (kept as a fallback should the order plant ever deliver one).
    auto handle_pos_update = [&](const std::string& payload) -> asio::awaitable<void> {
        // AccountPnLPositionUpdate — real-time position/PnL from exchange.
        // Startup snapshot (is_snapshot=true) shows current net position;
        // used to detect ghost positions left by previous session.
        rti::AccountPnLPositionUpdate pos_upd;
        if (!pos_upd.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed tid=451"); co_return; }

        LOG("[EXECUTOR] [POS-UPDATE] tid=451 is_snap=%d acct=%s "
            "fill_buy=%d fill_sell=%d net_qty=%d open_pnl='%s'",
            (int)pos_upd.is_snapshot(),
            pos_upd.account_id().c_str(),
            pos_upd.fill_buy_qty(), pos_upd.fill_sell_qty(),
            pos_upd.net_quantity(),
            pos_upd.open_position_pnl().c_str());

        if (pos_upd.account_id() != orb_cfg.account_id) co_return;
        const int  net        = pos_upd.net_quantity();
        const bool consistent = order_mgr.net_qty_consistent(net);
        const int64_t now_ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // ── Broker truth: balance and day P&L ─────────────────────────────
        // The risk manager's equity is synthetic (starting balance + our own
        // trade log). The broker's day P&L includes positions we never knew
        // about — on 2026-09-23 that was $598 on an orphaned long while our
        // log said -$25. The prop firm's own limit tripped at -$600; ours
        // (daily_loss_limit) is checked here against THEIR number so we act
        // first.
        auto to_d = notif::parse_decimal;
        double broker_bal  = to_d(pos_upd.account_balance());
        double broker_dpnl = to_d(pos_upd.day_pnl());
        if (std::isnan(broker_dpnl)) {
            double o = to_d(pos_upd.day_open_pnl()), c = to_d(pos_upd.day_closed_pnl());
            if (!std::isnan(o) || !std::isnan(c))
                broker_dpnl = (std::isnan(o) ? 0.0 : o) + (std::isnan(c) ? 0.0 : c);
        }
        if ((!std::isnan(broker_bal)  && broker_bal  != last_broker_bal) ||
            (!std::isnan(broker_dpnl) && broker_dpnl != last_broker_dpnl)) {
            last_broker_bal  = broker_bal;
            last_broker_dpnl = broker_dpnl;
            LOG("[EXECUTOR] [BROKER] balance=%.2f day_pnl=%.2f exchange_net=%d "
                "ours_consistent=%d",
                broker_bal, broker_dpnl, net, (int)consistent);
        }
        if (!broker_loss_halted &&
            notif::broker_loss_breached(broker_dpnl, orb_cfg.daily_loss_limit)) {
            broker_loss_halted = true;
            LOG("[EXECUTOR] CRITICAL: broker day_pnl %.2f <= daily_loss_limit %.2f — "
                "halting and flattening", broker_dpnl, orb_cfg.daily_loss_limit);
            risk.halt_external("broker_day_pnl " + std::to_string(broker_dpnl) +
                      " <= limit " + std::to_string(orb_cfg.daily_loss_limit));
            strategy.halt_trading("broker_day_pnl_limit");
            if (!order_mgr.is_flat())
                order_mgr.flatten_now("broker_day_pnl_limit", strategy.last_price());
            audit_log.error("risk.broker_day_pnl",
                "broker day_pnl " + std::to_string(broker_dpnl));
            int notify_rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                         ": BROKER DAY P&L " + std::to_string((int)broker_dpnl) +
                         " hit daily_loss_limit — halted + flattened\" "
                         ">/dev/null 2>&1 &").c_str());
            (void)notify_rc;  // fail-open: grid-notify absent is not an executor error
        }

        // ── Broker trailing drawdown (the prop firm's own account-killing rule) ──
        // HWM = max(starting balance, persisted mark, every balance reported); new
        // entries halt once balance <= HWM - trailing_drawdown_cap. The synthetic
        // gauge in RiskManager cannot see fees, liquidations or other instances.
        if (!std::isnan(broker_bal)) {
            const double hwm_now = notif::broker_hwm(broker_hwm, orb_cfg.starting_balance, broker_bal);
            if (hwm_now > broker_hwm) {
                broker_hwm = hwm_now;
                LOG("[EXECUTOR] [BROKER-HWM] new high-water mark %.2f — halt level now %.2f",
                    broker_hwm, broker_hwm - orb_cfg.trailing_drawdown_cap);
                if (db && db->is_connected() && !orb_cfg.dry_run) {
                    try { db->set_account_hwm(broker_hwm); }
                    catch (std::exception& e) { LOG("[EXECUTOR] set_account_hwm failed: %s", e.what()); }
                }
            }
            const double room = notif::broker_drawdown_room(broker_bal, broker_hwm, orb_cfg.trailing_drawdown_cap);
            if (broker_bal != last_broker_hwm_logged_bal) {
                last_broker_hwm_logged_bal = broker_bal;
                LOG("[EXECUTOR] [BROKER-HWM] balance=%.2f hwm=%.2f room=%.2f before the %.0f trailing cap",
                    broker_bal, broker_hwm, room, orb_cfg.trailing_drawdown_cap);
            }
            if (!broker_dd_halted &&
                notif::broker_drawdown_breached(broker_bal, broker_hwm, orb_cfg.trailing_drawdown_cap)) {
                broker_dd_halted = true;
                LOG("[EXECUTOR] CRITICAL: broker balance %.2f <= hwm %.2f - cap %.0f — "
                    "halting new entries (prop-firm trailing drawdown)",
                    broker_bal, broker_hwm, orb_cfg.trailing_drawdown_cap);
                risk.halt_external("broker_trailing_drawdown balance " + std::to_string(broker_bal) +
                                   " <= hwm " + std::to_string(broker_hwm) + " - cap " +
                                   std::to_string(orb_cfg.trailing_drawdown_cap));
                strategy.halt_trading("broker_trailing_drawdown");
                audit_log.error("risk.broker_trailing_drawdown",
                    "balance " + std::to_string(broker_bal) + " hwm " + std::to_string(broker_hwm));
                int rc2 = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                             ": BROKER BALANCE " + std::to_string((int)broker_bal) +
                             " at the trailing drawdown cap (hwm " + std::to_string((int)broker_hwm) +
                             ") — entries halted\" >/dev/null 2>&1 &").c_str());
                (void)rc2;
            }
        }

        // ── Startup / reconnect snapshot ───────────────────────────────────
        if (pos_upd.is_snapshot()) {
            if (net != 0 && consistent) {
                // The exchange holds exactly what we believe we hold (reconnect
                // mid-trade). Keep the deferred working orders — they are its
                // protective stops.
                if (defer_snapshot_cancels) {
                    defer_snapshot_cancels = false;
                    LOG("[EXECUTOR] [STARTUP-RECON] net_qty=%d matches our position — "
                        "keeping %zu deferred working order(s) (protective stops)",
                        net, deferred_snapshot_cancels.size());
                    deferred_snapshot_cancels.clear();
                }
                if (strategy.session().halt_reason.rfind("startup_", 0) == 0 ||
                    strategy.session().halt_reason == "reconnect_unreconciled_position")
                    strategy.unhalt_trading("startup_position_matches_exchange");
            } else if (net != 0 && !net_recon.acted) {
                // Position on the exchange that we do NOT hold in memory.
                if (defer_snapshot_cancels) {
                    // The position is about to be UNWOUND (this process does not own it),
                    // so the previous cycle's stops protect nothing: left working, one would
                    // OPEN a position when it fires. Cancel them by server id (the snapshot
                    // carries it) together with the unwind.
                    defer_snapshot_cancels = false;
                    LOG("[EXECUTOR] [STARTUP-RECON] net_qty=%d — position will be unwound: "
                        "cancelling %zu deferred working order(s) of the previous cycle",
                        net, deferred_snapshot_cancels.size());
                    for (const auto& bid : deferred_snapshot_cancels)
                        order_plant->send_cancel(bid, orb_cfg.account_id);
                    deferred_snapshot_cancels.clear();
                }
                bool ghost_is_long = (net > 0);
                LOG("[EXECUTOR] [STARTUP-RECON] GHOST POSITION CONFIRMED: "
                    "net_qty=%d (%s %d) — sending immediate unwind",
                    net, ghost_is_long ? "LONG" : "SHORT", std::abs(net));
                bool unwind_sent = co_await send_unwind(std::abs(net), !ghost_is_long,
                                                        "STARTUP-RECON");
                // Arm the continuous reconciler so it does not fire a second
                // unwind for the same mismatch during the grace window.
                net_recon.mismatch_since_ms = now_ms;
                net_recon.acted = true;
                net_recon.acted_ms = now_ms;
                net_halt_active = true;
                // Only unhalt strategy if the unwind order was actually dispatched.
                // If send failed, stay halted — operator must confirm flat and restart.
                if (unwind_sent)
                    strategy.unhalt_trading("startup_ghost_position_cleared");
                else
                    strategy.halt_trading("startup_ghost_unwind_failed");
            } else if (net == 0) {
                LOG("[EXECUTOR] [STARTUP-RECON] net_qty=0 — exchange confirmed FLAT");
            if (g_drill == "orphan" && !g_drill_sent.exchange(true)) {
                // The untracked order: 1 contract, aggressive LIMIT off the last price,
                // a user_tag the order manager has never seen. Exactly the shape of
                // the orphaned stop's fill on 2026-09-23.
                double last_px = strategy.last_price();
                std::string tag = "DRILL-orphan-" + std::to_string(now_ms);
                rti::RequestNewOrder req;
                req.set_template_id(312);
                req.set_fcm_id(fcm_id_r);
                req.set_ib_id(ib_id_r);
                req.set_account_id(orb_cfg.account_id);
                req.set_symbol(trade_symbol);
                req.set_exchange(orb_cfg.exchange);
                req.set_quantity(1);
                if (last_px > 0.0) { req.set_order_type(1); req.set_price(last_px + 50 * 0.25); }
                else               { req.set_order_type(2); }
                req.set_transaction_type(1);   // BUY
                req.set_user_tag(tag);
                req.set_duration(rti::RequestNewOrder::DAY);
                req.set_manual_or_auto_select(rti::RequestNewOrder::AUTO);
                req.set_trade_route(order_plant->trade_route);
                try {
                    co_await op_write_q.write(proto_frame(req));
                    g_drill_sent_ms = now_ms;
                    LOG("[DRILL] orphan order sent: BUY 1 %s tag=%s px_ref=%.2f — expecting: "
                        "unowned fill → ghost halt; tid=451 net=+1; NET-RECON unwind after "
                        "%dms; net=0 → PASS", trade_symbol.c_str(), tag.c_str(), last_px,
                        orb_cfg.net_mismatch_grace_ms);
                } catch (std::exception& e) {
                    LOG("[DRILL] FAIL: could not send the orphan order: %s", e.what());
                    g_exit_code = 2; g_running = false;
                }
            }
                // Exchange is flat: any deferred working orders from the tid=351
                // snapshot are stale (carried position no longer exists) — cancel now.
                if (!deferred_snapshot_cancels.empty()) {
                    LOG("[EXECUTOR] [STARTUP-RECON] draining %zu deferred snapshot "
                        "cancel(s) (stale working orders)",
                        deferred_snapshot_cancels.size());
                    for (const auto& bid : deferred_snapshot_cancels)
                        order_plant->send_cancel(bid, orb_cfg.account_id);
                    deferred_snapshot_cancels.clear();
                }
                defer_snapshot_cancels = false;
                // Clear ghost-fill halt in OrderManager (covers: stale stop fired
                // then manually closed via RTrader before this snapshot arrived).
                order_mgr.confirm_exchange_flat();
                // If strategy was halted waiting for position confirm, clear it
                if (strategy.session().halt_reason.rfind("startup_", 0) == 0 ||
                    strategy.session().halt_reason == "reconnect_unreconciled_position")
                    strategy.unhalt_trading("startup_position_confirmed_flat");
                // Immediately persist FLAT state to DB so a crash within the next
                // 5s doesn't leave a stale LONG/SHORT for the next restart to find.
                flush_position(db.get(), today, order_mgr, strategy,
                               orb_cfg.dry_run || order_plant->connected,
                               orb_cfg.point_value, md_up());
            }
        }

        // ── Continuous reconciliation (every update, snapshot or not) ──────
        // A mismatch that outlives the grace window is acted on once: unwind
        // the difference, halt entries, alert. Re-arms when the exchange and
        // the order manager agree again. This is what would have closed the
        // 2026-09-23 orphan long within seconds instead of 32 minutes.
        last_exch_net = net;
        co_await reconcile_net(net, consistent, now_ms);

    };

    auto op_loop = [&]() -> asio::awaitable<void> {
        if (!order_plant->connected || !order_plant->ws) co_return;
        beast::flat_buffer buf;
        while (g_running || g_draining) {
            buf.clear();
            try {
                co_await order_plant->ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                if (!g_running && !g_draining) co_return;
                LOG("[EXECUTOR] ORDER_PLANT read error: %s — triggering full reconnect", e.what());
                // Capture position state HERE: ioc_ref.stop() destroys the suspended
                // run_executor frame, so the normal capture after md_loop never runs.
                // Without this, a reconnect while LONG/SHORT starts the next cycle
                // believing FLAT and both reconciliation paths are skipped.
                carried_pos = order_mgr.position_snapshot();
                ioc_ref.stop();  // kill md_loop and all timers → outer loop reconnects
                co_return;
            }
            std::string payload;
            try {
                payload = proto_strip(beast::buffers_to_string(buf.data()));
            } catch (std::exception& e) {
                // Malformed short frame — must not propagate: this coroutine is spawned
                // detached, so an uncaught throw here is std::terminate.
                LOG("[EXECUTOR] ORDER_PLANT malformed frame (%s) len=%zu — skipping message",
                    e.what(), buf.size());
                continue;
            }
            rti::Base base;
            if (!base.ParseFromString(payload)) continue;

            int tid = base.template_id();
            LOG("[EXECUTOR] op_loop tid=%d len=%zu", tid, payload.size());

            if (tid == 18) {
                // Server-sent RequestHeartbeat — respond with ResponseHeartbeat (tid=19)
                rti::ResponseHeartbeat hb_resp;
                hb_resp.set_template_id(19);
                try {
                    co_await op_write_q.write(proto_frame(hb_resp));
                } catch (...) {
                    LOG("[EXECUTOR] ORDER_PLANT heartbeat response send failed");
                }
            } else if (tid == 19) {
                // ResponseHeartbeat — server acked our heartbeat, no action
            } else if (tid == 309) {
                // ResponseSubscribeForOrderUpdates — subscription ack
                rti::ResponseSubscribeForOrderUpdates resp;
                if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
                LOG("[EXECUTOR] Order update subscription %s (rp_code=%s %s)",
                    rpc == "0" ? "OK" : "FAILED", rpc.c_str(), txt.c_str());
                if (rpc != "0")
                    audit_log.error("session.order_updates_rejected",
                        "RequestSubscribeForOrderUpdates rejected rp_code=" + rpc + " " + txt);

            } else if (tid == 313 || tid == 315) {
                // 313 = preliminary gateway ack (rq_handler_rp_code only)
                // 315 = final response for both RequestNewOrder and RequestModifyOrder
                rti::ResponseNewOrder resp;
                if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                if (resp.rp_code().empty()) {
                    LOG("[EXECUTOR] ResponseNewOrder (ack) basket=%s",
                        resp.basket_id().c_str());
                    if (db) db->write_order_event("gateway_ack", resp.basket_id(), "", "", tid, "", "",
                                                  0, 0.0, 0.0, 0, 0, "", "");
                } else {
                    std::string rpc = resp.rp_code(0);
                    LOG("[EXECUTOR] ResponseNewOrder basket=%s rp_code=%s",
                        resp.basket_id().c_str(), rpc.c_str());
                    if (db) db->write_order_event(rpc != "0" ? "gateway_reject" : "gateway_ack",
                                                  resp.basket_id(), "", "", tid, "", "",
                                                  0, 0.0, 0.0, 0, 0, rpc, "");
                    if (rpc != "0") {
                        LOG("[EXECUTOR] Order REJECTED at gateway: basket=%s code=%s",
                            resp.basket_id().c_str(), rpc.c_str());
                        // ResponseNewOrder has NO user_tag field — basket_id is the
                        // server-assigned ID. on_order_rejected resolves it to our
                        // client basket via the server→client map (map_server_basket).
                        order_mgr.on_order_rejected(resp.basket_id(),
                                                    "gateway_reject_" + rpc);
                    }
                }

            } else if (tid == 351) {
                // RithmicOrderNotification — internal acks and, for Legends/paper routing,
                // the authoritative fill (notify_type=15 COMPLETE with total_fill_size>0).
                rti::RithmicOrderNotification notif;
                if (!notif.ParseFromString(payload)) continue;
                LOG("[EXECUTOR] RithmicOrderNotification basket=%s orig=%s notify_type=%d status=%s "
                    "avg_fill=%.2f total_fill=%d unfilled=%d is_snap=%d price=%.2f "
                    "fcm=%s ib=%s acct=%s sym=%s exch=%s user_tag=%s qty=%d",
                    notif.basket_id().c_str(), notif.original_basket_id().c_str(),
                    (int)notif.notify_type(), notif.status().c_str(),
                    notif.avg_fill_price(), notif.total_fill_size(), notif.total_unfilled_size(),
                    (int)notif.is_snapshot(), notif.price(),
                    notif.fcm_id().c_str(), notif.ib_id().c_str(), notif.account_id().c_str(),
                    notif.symbol().c_str(), notif.exchange().c_str(), notif.user_tag().c_str(),
                    notif.quantity());
                if (db) db->write_order_event("rithmic_notify", notif.basket_id(),
                                              notif.original_basket_id(), notif.user_tag(),
                                              (int)notif.notify_type(), notif.status(), "",
                                              notif.quantity(), notif.price(), notif.avg_fill_price(),
                                              0, notif.total_fill_size(), "",
                                              notif.is_snapshot() ? "snapshot" : "");

                // ── Startup order reconciliation ─────────────────────────────────
                // is_snapshot=1 messages arrive after subscribing for order updates
                // and reflect the live order book state from the prior session.
                // Two cases:
                //   A) unfilled_size > 0  → open order (e.g. stop in trigger-pending)
                //      → cancel immediately so it can't fire against new positions
                //   B) fill_size > 0, unfilled_size == 0  → order filled while we were
                //      offline → ghost position on exchange. halt entries so operator
                //      can reconcile via RTrader (AccountPnLPositionUpdate tid=451 will
                //      confirm the net quantity below).
                if (notif.is_snapshot()) {
                    LOG("[EXECUTOR] [STARTUP-RECON] tid=351 snap: basket=%s "
                        "status='%s' notify=%d filled=%d unfilled=%d "
                        "avg_px=%.2f sym=%s tag=%s",
                        notif.basket_id().c_str(), notif.status().c_str(),
                        (int)notif.notify_type(),
                        notif.total_fill_size(), notif.total_unfilled_size(),
                        notif.avg_fill_price(),
                        notif.symbol().c_str(), notif.user_tag().c_str());

                    if (notif.symbol() == trade_symbol) {
                        if (notif.total_unfilled_size() > 0 &&
                            !notif.basket_id().empty()) {
                            if (defer_snapshot_cancels) {
                                // Carried-position reconnect (#1): this open order may be
                                // the carried position's protective stop. Do NOT cancel it
                                // until the tid=451 snapshot confirms the exchange is flat.
                                LOG("[EXECUTOR] [STARTUP-RECON] OPEN ORDER basket=%s "
                                    "status='%s' unfilled=%d — DEFERRING cancel until "
                                    "tid=451 position confirm (carried position)",
                                    notif.basket_id().c_str(), notif.status().c_str(),
                                    notif.total_unfilled_size());
                                deferred_snapshot_cancels.push_back(notif.basket_id());
                                continue;
                            }
                            // Case A: open working order — cancel it
                            LOG("[EXECUTOR] [STARTUP-RECON] OPEN ORDER FOUND "
                                "basket=%s status='%s' unfilled=%d — cancelling",
                                notif.basket_id().c_str(), notif.status().c_str(),
                                notif.total_unfilled_size());
                            rti::RequestCancelOrder cancel_req;
                            cancel_req.set_template_id(316);
                            cancel_req.set_basket_id(notif.basket_id());
                            cancel_req.set_account_id(orb_cfg.account_id);
                            cancel_req.set_fcm_id(fcm_id_r);
                            cancel_req.set_ib_id(ib_id_r);
                            cancel_req.set_manual_or_auto(2);  // AUTO — omitted → rp_code=1045, cancel silently refused
                            try {
                                co_await op_write_q.write(proto_frame(cancel_req));
                                LOG("[EXECUTOR] [STARTUP-RECON] cancel sent basket=%s",
                                    notif.basket_id().c_str());
                            } catch (std::exception& e) {
                                LOG("[EXECUTOR] [STARTUP-RECON] cancel send FAILED: %s",
                                    e.what());
                            }
                        } else if (notif.total_fill_size() > 0 &&
                                   notif.total_unfilled_size() == 0) {
                            // Case B: filled order snap → ghost position indicator.
                            // AccountPnLPositionUpdate (tid=451) will confirm net qty.
                            LOG("[EXECUTOR] [STARTUP-RECON] FILLED ORDER SNAP "
                                "basket=%s fill_qty=%d px=%.2f — possible ghost position! "
                                "Halting entries pending tid=451 position confirm",
                                notif.basket_id().c_str(),
                                notif.total_fill_size(), notif.avg_fill_price());
                            strategy.halt_trading("startup_ghost_position_suspected");
                        }
                    }
                    continue;  // never process snapshots through the live fill path
                }

                // Map server→client basket IDs for entry/exit orders as well: gateway
                // rejects (tid=313/315 ResponseNewOrder) carry only the server basket_id.
                order_mgr.map_server_basket(notif.user_tag(), notif.basket_id());
                // Server basket id for a stop: the live one (trail cancels need it) OR
                // one we already tried to cancel by client id — that cancel failed at
                // Rithmic and the stop is still working; the OM re-sends it by server id.
                order_mgr.on_stop_server_mapped(notif.user_tag(), notif.basket_id());
                // Legends routing delivers fills as COMPLETE (15) on tid=351 rather than
                // ExchangeOrderNotification (352). Detect by total_fill_size > 0.
                if (notif.total_fill_size() > 0) {
                    // Legends can deliver a COMPLETE fill with avg_fill_price=0 but a
                    // valid price field. Dropping those (old guard required avg_fill>0)
                    // left order_mgr stuck LONG/SHORT after a real close — fall back
                    // price → last traded price instead of dropping the fill.
                    double fill_px = notif.avg_fill_price() > 0.0 ? notif.avg_fill_price()
                                   : (notif.price() > 0.0 ? notif.price()
                                                          : strategy.last_price());
                    if (notif.avg_fill_price() <= 0.0)
                        LOG("[EXECUTOR] WARN: tid=351 fill with avg_fill=0 basket=%s "
                            "user_tag=%s — using fallback px=%.2f",
                            notif.basket_id().c_str(), notif.user_tag().c_str(), fill_px);
                    // qty=7 (or any qty > cfg_.qty) on a fill we didn't send means Rithmic
                    // is delivering order notifications for another account on this session.
                    if (notif.total_fill_size() > orb_cfg.qty) {
                        LOG("[EXECUTOR] WARNING: fill qty=%d exceeds expected position size=%d "
                            "— possible multi-account notification basket=%s px=%.2f "
                            "acct=%s user_tag=%s",
                            notif.total_fill_size(), orb_cfg.qty,
                            notif.basket_id().c_str(), fill_px,
                            notif.account_id().c_str(), notif.user_tag().c_str());
                    }
                    const std::string& client_id = notif.user_tag();
                    bool is_entry = order_mgr.is_entry_basket(client_id);
                    bool is_stop  = order_mgr.is_stop_basket(client_id);
                    bool is_exit  = order_mgr.is_exit_basket(client_id);
                    if (is_entry || is_stop || is_exit) {
                        // total_fill_size is CUMULATIVE, not incremental: a repeated
                        // COMPLETE notification (partial-then-complete) or a duplicate
                        // delivery of a fill already processed via tid=352 must be
                        // skipped — re-processing would double-count the fill.
                        if (order_mgr.fill_already_processed(client_id,
                                                             notif.total_fill_size())) {
                            LOG("[EXECUTOR] tid=351 duplicate fill skipped: client=%s "
                                "total_fill=%d (already processed)",
                                client_id.c_str(), notif.total_fill_size());
                            continue;
                        }
                        LOG("[EXECUTOR] tid=351 fill detected: client=%s px=%.2f qty=%d "
                            "entry=%d stop=%d exit=%d",
                            client_id.c_str(), fill_px,
                            notif.total_fill_size(), (int)is_entry, (int)is_stop, (int)is_exit);
                        order_mgr.on_fill_notification(client_id,
                                                       fill_px,
                                                       notif.total_fill_size(),
                                                       is_entry && !is_stop);
                        if constexpr (kMtf) {
                            // Refine the strategy's assumed (bar-close) entry price with
                            // the real fill and recompute its bracket — "never-retreat"
                            // inside notify_entry_filled() keeps any tighter stop that
                            // check_external_stop() already ratcheted in the meantime.
                            if (is_entry && !is_stop)
                                strategy.notify_entry_filled(order_mgr.position_snapshot().direction, fill_px);
                        }
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                       md_up());
                    } else if (notif.account_id().empty() ||
                               notif.account_id() == orb_cfg.account_id) {
                        // A fill on OUR account for an order we do not track: an
                        // orphaned stop whose cancel failed, a broker liquidation, a
                        // manual RTrader order. 2026-09-23: this was logged as "not our
                        // order" while the executor's own orphaned stop filled 2 MNQ.
                        // FLAT → run the order manager's stale-stop guards (cancelled
                        // stop → unwind, unknown → ghost-halt). In a trade → halt
                        // entries; the tid=451 reconciliation unwinds any mismatch.
                        // Classify BEFORE alarming: the tid=351 COMPLETE of a fill the
                        // tid=352 path already booked lands after the basket closed and
                        // reads as unowned on every normal exit (seen live 2026-09-28).
                        if (notif::unowned_fill_is_duplicate(order_mgr, client_id, notif.basket_id(),
                                                             notif.total_fill_size())) {
                            LOG("[EXECUTOR] tid=351 duplicate delivery of a processed fill — skipped "
                                "(user_tag='%s' basket=%s px=%.2f qty=%d)",
                                client_id.c_str(), notif.basket_id().c_str(), fill_px,
                                notif.total_fill_size());
                        } else {
                        LOG("[EXECUTOR] CRITICAL: tid=351 fill on our account for an order we "
                            "do not own (user_tag='%s' basket=%s px=%.2f qty=%d state=%d)",
                            client_id.c_str(), notif.basket_id().c_str(), fill_px,
                            notif.total_fill_size(), (int)order_mgr.state());
                        auto r = notif::handle_unowned_fill(
                            order_mgr, client_id, notif.basket_id(), notif.account_id(),
                            orb_cfg.account_id, fill_px, notif.total_fill_size(),
                            [&](const std::string& why) { strategy.halt_trading(why); });
                        LOG("[EXECUTOR] unowned fill → %s",
                            r == notif::UnownedFill::GUARDS_RUN ? "stale-stop guards run" :
                            r == notif::UnownedFill::HALTED    ? "entries halted (in a trade)" :
                            r == notif::UnownedFill::DUPLICATE ? "duplicate, skipped" : "other account");
                        if (r == notif::UnownedFill::GUARDS_RUN)
                            flush_position(db.get(), today, order_mgr, strategy,
                                           orb_cfg.dry_run || order_plant->connected,
                                           orb_cfg.point_value, md_up());
                        }
                    }
                } else if ((int)notif.notify_type() == 17 ||
                           notif.status() == "Cancellation Failed") {
                    // The order is STILL WORKING. Until 2026-09-23 this fell through
                    // unhandled: a stop cancelled by client id (server id not mapped
                    // yet) kept working, fired 12s after the position was closed, and
                    // the resulting long went unnoticed until the broker liquidated it.
                    LOG("[EXECUTOR] tid=351 CANCEL FAILED: tag=%s basket=%s status=%s — order still live",
                        notif.user_tag().c_str(), notif.basket_id().c_str(), notif.status().c_str());
                    notif::handle_cancel_notification(order_mgr, (int)notif.notify_type(),
                                                      notif.status(), notif.user_tag(),
                                                      notif.basket_id());
                    flush_position(db.get(), today, order_mgr, strategy,
                                   orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                   md_up());
                } else if ((int)notif.notify_type() == 3) {
                    // Cancel ACK on tid=351 — Legends routing delivers cancel confirmations
                    // here rather than on ExchangeOrderNotification (tid=352).
                    // Must call on_cancel_confirmed so cancelled_stops_ is cleared;
                    // without this the stop accumulates in the unwind-guard map forever.
                    const std::string& client_id = notif.user_tag();
                    LOG("[EXECUTOR] tid=351 CANCEL ACK: client=%s basket=%s status=%s",
                        client_id.c_str(), notif.basket_id().c_str(),
                        notif.status().c_str());
                    if (!client_id.empty()) {
                        LOG("[EXECUTOR] Cancel ACK received: client=%s basket=%s notify_type=%d",
                            client_id.c_str(), notif.basket_id().c_str(),
                            (int)notif.notify_type());
                        order_mgr.on_cancel_confirmed(client_id);
                    } else {
                        // External cancellation (RTrader) — user_tag is empty;
                        // resolve the client basket via the exchange basket_id reverse map.
                        order_mgr.on_cancel_confirmed_by_server_basket(notif.basket_id());
                    }

                } else if ((int)notif.notify_type() == 15 && notif.total_fill_size() == 0) {
                    // COMPLETE with no fill = order cancelled/rejected by routing or risk rules.
                    const std::string& client_id = notif.user_tag();
                    if (order_mgr.is_entry_basket(client_id)) {
                        LOG("[EXECUTOR] tid=351 order CANCELLED (no fill): client=%s status=%s — "
                            "returning to FLAT (possible pre-market or risk restriction)",
                            client_id.c_str(), notif.status().c_str());
                        order_mgr.on_order_rejected(client_id, "cancelled_no_fill");
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                       md_up());
                    }
                }

            } else if (tid == 352) {
                // ExchangeOrderNotification — actual exchange fills, rejects, and cancels.
                // This is the authoritative source for fill_price and fill_size.
                rti::ExchangeOrderNotification notif;
                if (!notif.ParseFromString(payload)) continue;
                int notify_type = (int)notif.notify_type();
                LOG("[EXECUTOR] ExchangeOrderNotification type=%d basket=%s "
                    "fill_px=%.2f fill_qty=%d status=%s",
                    notify_type,
                    notif.basket_id().c_str(),
                    notif.fill_price(),
                    notif.fill_size(),
                    notif.status().c_str());
                if (db) db->write_order_event("exchange_notify", notif.basket_id(), "",
                                              notif.user_tag(), notify_type, notif.status(), "",
                                              0, 0.0, notif.fill_price(), notif.fill_size(),
                                              notif.total_fill_size(), "", "");

                // ExchangeOrderNotification::NotifyType::FILL = 5
                // Correlate fills via user_tag (our client-side tracking ID).
                // Rithmic assigns its own basket_id on the response and echoes
                // our user_tag in every notification.
                if (notify_type == 5) {
                    const std::string& client_id = notif.user_tag();
                    // tid=352 fill_size is PER EVENT; the order manager and the fill dedupe
                    // work on the CUMULATIVE quantity (as tid=351 reports it). With per-event
                    // sizes the second of two 1-lot partials looked like a duplicate.
                    const int cum_fill_352 = notif.total_fill_size() > 0 ? notif.total_fill_size()
                                                                         : notif.fill_size();
                    order_mgr.map_server_basket(client_id, notif.basket_id());
                    if (notif.fill_size() > orb_cfg.qty) {
                        LOG("[EXECUTOR] WARNING: fill qty=%d exceeds expected position size=%d "
                            "— possible multi-account notification basket=%s px=%.2f "
                            "user_tag=%s",
                            notif.fill_size(), orb_cfg.qty,
                            notif.basket_id().c_str(), notif.fill_price(),
                            notif.user_tag().c_str());
                    }
                    // Same gating as tid=351: without it, fills for unknown/other-account
                    // tags landed in the FLAT unknown-fill branch and falsely ghost-halted
                    // the engine.
                    bool is_entry = order_mgr.is_entry_basket(client_id);
                    bool is_stop  = order_mgr.is_stop_basket(client_id);
                    bool is_exit  = order_mgr.is_exit_basket(client_id);
                    if (!is_entry && !is_stop && !is_exit) {
                        // Our account, an order we do not track — the exact message the
                        // 2026-09-23 orphaned-stop fill produced ("ignoring (not our order)").
                        if (notif::unowned_fill_is_duplicate(order_mgr, client_id, notif.basket_id(),
                                                             notif.fill_size())) {
                            LOG("[EXECUTOR] tid=352 duplicate delivery of a processed fill — skipped "
                                "(user_tag='%s' basket=%s px=%.2f qty=%d)",
                                client_id.c_str(), notif.basket_id().c_str(), notif.fill_price(),
                                notif.fill_size());
                        } else {
                        LOG("[EXECUTOR] CRITICAL: tid=352 fill for an order we do not own "
                            "(acct=%s user_tag='%s' basket=%s px=%.2f qty=%d state=%d)",
                            notif.account_id().c_str(), client_id.c_str(),
                            notif.basket_id().c_str(), notif.fill_price(), notif.fill_size(),
                            (int)order_mgr.state());
                        auto r = notif::handle_unowned_fill(
                            order_mgr, client_id, notif.basket_id(), notif.account_id(),
                            orb_cfg.account_id, notif.fill_price(), notif.fill_size(),
                            [&](const std::string& why) { strategy.halt_trading(why); });
                        LOG("[EXECUTOR] unowned fill → %s",
                            r == notif::UnownedFill::GUARDS_RUN ? "stale-stop guards run" :
                            r == notif::UnownedFill::HALTED    ? "entries halted (in a trade)" :
                            r == notif::UnownedFill::DUPLICATE ? "duplicate, skipped" : "other account");
                        if (r == notif::UnownedFill::GUARDS_RUN)
                            flush_position(db.get(), today, order_mgr, strategy,
                                           orb_cfg.dry_run || order_plant->connected,
                                           orb_cfg.point_value, md_up());
                        }
                    } else if (order_mgr.fill_already_processed(client_id, cum_fill_352)) {
                        // Duplicate delivery of a fill already processed via tid=351.
                        LOG("[EXECUTOR] tid=352 duplicate fill skipped: client=%s cumulative=%d "
                            "(already processed)",
                            client_id.c_str(), cum_fill_352);
                    } else {
                        if (is_stop) {
                            LOG("[EXECUTOR] Exchange STOP filled client_id=%s (server=%s) px=%.2f — treating as exit",
                                client_id.c_str(), notif.basket_id().c_str(), notif.fill_price());
                        }
                        order_mgr.on_fill_notification(client_id,
                                                       notif.fill_price(),
                                                       cum_fill_352,
                                                       is_entry && !is_stop);
                        if constexpr (kMtf) {
                            if (is_entry && !is_stop)
                                strategy.notify_entry_filled(order_mgr.position_snapshot().direction, notif.fill_price());
                        }
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                       md_up());
                    }
                } else if (notify_type == 2) { // MODIFY ACK
                    LOG("[EXECUTOR] Stop MODIFIED by exchange: client=%s server=%s — trail ACKed",
                        notif.user_tag().c_str(), notif.basket_id().c_str());
                } else if (notify_type == 6) { // REJECT
                    LOG("[EXECUTOR] Order REJECTED by exchange: client=%s server=%s status=%s",
                        notif.user_tag().c_str(), notif.basket_id().c_str(), notif.status().c_str());
                    order_mgr.on_order_rejected(notif.user_tag(), notif.status());
                } else if (notify_type == 3) { // CANCEL
                    LOG("[EXECUTOR] Order CANCELLED by exchange: client=%s server=%s",
                        notif.user_tag().c_str(), notif.basket_id().c_str());
                    order_mgr.on_cancel_confirmed(notif.user_tag());
                } else if (notify_type == 4) { // TRIGGER PENDING (stop armed)
                    LOG("[EXECUTOR] Stop ARMED (trigger pending): client=%s server=%s",
                        notif.user_tag().c_str(), notif.basket_id().c_str());
                    // Late mapping of a stop we already tried to cancel by client id
                    // → the OM re-sends the cancel by server id.
                    order_mgr.on_stop_server_mapped(notif.user_tag(), notif.basket_id());
                }

            } else if (tid == 451) {
                co_await handle_pos_update(payload);
            }
        }
    };

    // ── PNL_PLANT receive loop ────────────────────────────────────────────────
    auto pnl_loop = [&]() -> asio::awaitable<void> {
        if (!pnl_connected || !pnl_ws) co_return;
        beast::flat_buffer buf;
        while (g_running || g_draining) {
            buf.clear();
            try {
                co_await pnl_ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                if (!g_running && !g_draining) co_return;
                LOG("[EXECUTOR] PNL_PLANT read error: %s — position feed lost; "
                    "triggering full reconnect", e.what());
                pnl_connected = false;
                carried_pos = order_mgr.position_snapshot();
                ioc_ref.stop();
                co_return;
            }
            std::string payload;
            try {
                payload = proto_strip(beast::buffers_to_string(buf.data()));
            } catch (std::exception& e) {
                LOG("[EXECUTOR] PNL_PLANT malformed frame: %s", e.what());
                continue;
            }
            rti::Base base;
            if (!base.ParseFromString(payload)) continue;
            int tid = base.template_id();
            if (tid == 19) continue;   // heartbeat
            if (tid == 401) {
                rti::ResponsePnLPositionUpdates r;
                if (r.ParseFromString(payload)) {
                    std::string rpc = r.rp_code().empty() ? "?" : r.rp_code(0);
                    std::string txt = r.rp_code().size() > 1 ? r.rp_code(1) : "";
                    if (rpc == "0") {
                        LOG("[EXECUTOR] PNL_PLANT position subscription OK");
                    } else {
                        LOG("[EXECUTOR] CRITICAL: PNL_PLANT subscription REJECTED "
                            "(rp_code=%s %s) — no position feed, entries halted",
                            rpc.c_str(), txt.c_str());
                        strategy.halt_trading("pnl_subscribe_rejected");
                    }
                }
                continue;
            }
            if (tid == 451) { co_await handle_pos_update(payload); continue; }
            if (tid == 403) {
                rti::ResponsePnLPositionSnapshot r;
                if (r.ParseFromString(payload)) {
                    std::string rpc = r.rp_code().empty() ? "?" : r.rp_code(0);
                    std::string txt = r.rp_code().size() > 1 ? r.rp_code(1) : "";
                    if (rpc == "0")
                        LOG("[EXECUTOR] PNL_PLANT snapshot request OK (tid=403)");
                    else
                        LOG("[EXECUTOR] WARNING: PNL_PLANT snapshot request rejected "
                            "(rp_code=%s %s)", rpc.c_str(), txt.c_str());
                }
                continue;
            }
            if (tid == 450) continue;   // InstrumentPnLPositionUpdate — per-symbol detail, not needed
            LOG("[EXECUTOR] pnl_loop tid=%d len=%zu", tid, payload.size());
        }
    };


    // Session setup — on reconnect, today/risk/strategy already have the day's state.
    // Only initialize today on first run (empty string signals first call).
    bool is_fresh_start = today.empty();  // capture BEFORE the block sets today
    if (today.empty()) {
        today = today_date_str();
        strategy.reset_session();
        risk.reset_daily();
        // Re-seed trades_today from DB so the count survives intra-day restarts.
        // Without this, every restart resets to 0 and max_daily_trades is not enforced
        // correctly across sessions started on the same calendar day.
        if (db && db->is_connected()) {
            int done = db->count_today_trades(today, orb_cfg.cycle_start_epoch);
            if (done > 0) strategy.seed_trades_today(done);
        }
    }

    // Fresh-start reconciliation: if the prior cycle exited while holding a position,
    // live_position.state will show LONG/SHORT/PENDING_EXIT in the DB. The new process
    // starts with a flat in-process state but the exchange may still have an open
    // position. Halt entries and request a position snapshot to auto-verify.
    if (is_fresh_start && db && db->is_connected() && !orb_cfg.dry_run) {
        std::string prior_state = db->read_position_state(today);
        if (prior_state == "LONG" || prior_state == "SHORT" || prior_state == "PENDING_EXIT") {
            LOG("[EXECUTOR] CRITICAL: live_position shows %s from prior cycle — "
                "halting entries. Requesting exchange position snapshot to auto-verify.",
                prior_state.c_str());
            strategy.halt_trading("startup_stale_position_" + prior_state);
        } else if (prior_state == "PENDING_ENTRY") {
            LOG("[EXECUTOR] WARNING: live_position shows PENDING_ENTRY from prior cycle — "
                "snapshot reconciliation should cancel the residual entry order.");
        }
    }
    // The tid=451 position snapshot that verifies the account arrives on the
    // PNL_PLANT session (subscribed right after its login above).

    if (!g_drill.empty()) {
        strategy.halt_trading("drill_mode");
        LOG("[DRILL] mode=%s — strategy halted for the whole run; waiting for the exchange "
            "to confirm FLAT before placing the untracked order", g_drill.c_str());
    }
    seed_regime_atr(today);
    if constexpr (kTrend) {
        const bool ev = db && db->is_connected() && db->calendar_event_day(today);
        strategy.set_event_day(ev);
        strategy.set_day_atr(regime.atr_pts);
        if (ev) LOG("[EXECUTOR] %s is a scheduled-release day (calendar)", today.c_str());
    }
    LOG("[EXECUTOR] Session date: %s  dry_run=%s",
        today.c_str(), orb_cfg.dry_run ? "TRUE" : "FALSE");
    if (orb_cfg.dry_run)
        LOG("[EXECUTOR] *** DRY RUN — no real orders will be sent ***");

    // ── MD tick silence watchdog ──────────────────────────────────────────────
    // Tracks epoch-seconds of last real tick.  eod_loop reconnects MD if silence
    // exceeds tick_timeout_s during the active session window.
    std::atomic<int64_t> last_tick_epoch_s{
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count()
    };

    // ── Heartbeat timer ───────────────────────────────────────────────────────
    asio::steady_timer hb_timer(ex);
    auto heartbeat_loop = [&]() -> asio::awaitable<void> {
        while (g_running || g_draining) {
            hb_timer.expires_after(std::chrono::seconds(5));
            co_await hb_timer.async_wait(asio::use_awaitable);
            if (!g_running && !g_draining) co_return;
            rti::RequestHeartbeat hb;
            hb.set_template_id(18);
            hb.set_ssboe(hb_ssboe_now());
            if (md_ws) {
                try {
                    co_await md_write_q.write(proto_frame(hb));
                } catch (...) {
                    LOG("[EXECUTOR] Heartbeat send failed on MD — WS may be closed");
                }
            }
            // Also heartbeat ORDER_PLANT — Rithmic drops idle connections in ~2 min
            if (order_plant->connected && order_plant->ws) {
                try {
                    co_await op_write_q.write(proto_frame(hb));
                } catch (...) {
                    LOG("[EXECUTOR] Heartbeat send failed on ORDER_PLANT");
                }
            }
            if (pnl_connected && pnl_ws) {
                try {
                    co_await pnl_write_q.write(proto_frame(hb));
                } catch (...) {
                    LOG("[EXECUTOR] Heartbeat send failed on PNL_PLANT");
                }
            }
        }
    };

    // ── EOD/trail check timer (1-second tick) ─────────────────────────────────
    asio::steady_timer eod_timer(ex);
    auto eod_loop = [&]() -> asio::awaitable<void> {
        int pos_write_counter = 0;
        int trail_snapshot_counter = 0; // counts seconds for periodic 10s trail state log
        PosState watchdog_state  = PosState::FLAT;
        int64_t  watchdog_since_s = 0;  // epoch-s when watchdog_state last changed
        int ghost_halt_secs = 0;        // seconds ghost_halted_ has been active this cycle
        int64_t post_close_window_until_s    = 0;  // epoch-s; intensive per-second recancel window
        int64_t last_background_recancel_s   = 0;  // epoch-s; last background 30s recancel fire
        while (g_running || g_draining) {
            eod_timer.expires_after(std::chrono::seconds(1));
            co_await eod_timer.async_wait(asio::use_awaitable);

            // Reconciler re-check (see reconcile_net): only while we disagree with the
            // last exchange net the PNL plant reported.
            if (pnl_connected && last_exch_net != INT_MIN &&
                !order_mgr.net_qty_consistent(last_exch_net)) {
                const int64_t rnow_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                co_await reconcile_net(last_exch_net, false, rnow_ms);
            }

            if (g_drill_sent && !g_drill_passed) {
                int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
                if (now_ms - g_drill_sent_ms > 90'000) {
                    LOG("[DRILL] FAIL: 90s after the orphan order the exchange is still not "
                        "confirmed flat — CLOSE THE POSITION IN RTRADER NOW");
                    int notify_rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                        ": DRILL FAIL — orphan position NOT closed, close it by hand\" "
                        ">/dev/null 2>&1 &").c_str());
                    (void)notify_rc;
                    g_drill_passed = true;   // report once
                    g_exit_code = 2; g_running = false;
                    ioc_ref.stop();
                }
            }
            // Deferred flatten from signal handler (#5 — signal handler is mutex-free)
            if (g_flatten_requested.exchange(false)) {
                LOG("[EXECUTOR] Kill signal — flattening position");
                order_mgr.flatten_now("kill_signal", strategy.last_price());
                audit_log.info("session.eod_flatten", "EOD position flattened (kill signal)");
            }

            if (!g_running && !g_draining) co_return;

            int et_h, et_m;
            current_et(et_h, et_m);
            strategy.check_eod(et_h, et_m);
            settle_strategy();
            if constexpr (!kOrb) {
                // Backstop: the trend engine flattens at its own win_end; the executor also
                // enforces the config's eod_flatten time so a mis-set window can never carry
                // a live position past it.
                static std::string eod_backstop_day;
                if (et_h == orb_cfg.eod_flatten_hour && et_m == orb_cfg.eod_flatten_min &&
                    eod_backstop_day != today &&
                    order_mgr.position_snapshot().state != PosState::FLAT) {
                    eod_backstop_day = today;
                    LOG("[EXECUTOR] trend EOD backstop %02d:%02d ET — flattening", et_h, et_m);
                    order_mgr.flatten_now("eod_backstop", strategy.last_price());
                }
            }

            // MD tick silence watchdog — force-close stale MD WS so md_loop reconnects.
            // Only active during the session window (orb_open - 5 min to eod_flatten).
            // beast::get_lowest_layer close aborts the pending async_read in md_loop.
            if (md_ws && orb_cfg.tick_timeout_s > 0) {
                int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                int64_t silence_s = now_s - last_tick_epoch_s.load();
                int et_min_total  = et_h * 60 + et_m;
                int active_start  = orb_cfg.session_open_hour * 60 + orb_cfg.session_open_min - 5;
                int active_end    = orb_cfg.eod_flatten_hour  * 60 + orb_cfg.eod_flatten_min;
                bool in_position  = !order_mgr.is_flat();
                if ((et_min_total >= active_start && et_min_total <= active_end) || in_position) {
                    if (silence_s >= orb_cfg.tick_timeout_s) {
                        LOG("[EXECUTOR] MD tick silence %lds (timeout=%ds) — force-reconnecting MD",
                            (long)silence_s, orb_cfg.tick_timeout_s);
                        beast::get_lowest_layer(*md_ws).close();
                        last_tick_epoch_s.store(now_s);  // reset so we get a fresh window
                    }
                }
            }

            // pg-feed stale watchdog: the collector (or Postgres) stopped
            // delivering ticks. Halt NEW entries — an open position keeps its
            // exchange-held stop — alert, and let pg_feed_loop unhalt on resume.
            if (orb_cfg.md_from_pg() && orb_cfg.tick_timeout_s > 0) {
                int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                int64_t silence_s = now_s - last_tick_epoch_s.load();
                int et_min_total  = et_h * 60 + et_m;
                int active_start  = orb_cfg.session_open_hour * 60 + orb_cfg.session_open_min - 5;
                int active_end    = orb_cfg.eod_flatten_hour  * 60 + orb_cfg.eod_flatten_min;
                bool in_position  = !order_mgr.is_flat();
                if ((et_min_total >= active_start && et_min_total <= active_end) || in_position) {
                    if (silence_s >= orb_cfg.tick_timeout_s && pg_feed_fresh.exchange(false)) {
                        LOG("[PG-FEED] STALE: no tick for %lds (timeout=%ds) — collector/Postgres "
                            "down? halting new entries until ticks resume%s",
                            (long)silence_s, orb_cfg.tick_timeout_s,
                            in_position ? " (open position keeps its exchange stop)" : "");
                        audit_log.error("feed.stale",
                            "pg feed silent " + std::to_string(silence_s) + "s");
                        strategy.halt_trading("pg_feed_stale");
                        // grid-notify is the grid's Telegram channel; fail-open.
                        int notify_rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                                     ": tick feed stale " + std::to_string(silence_s) +
                                     "s — collector down? new entries halted\" "
                                     ">/dev/null 2>&1 &").c_str());
                        (void)notify_rc;  // fail-open
                    }
                }
            }

            // Date rollover check
            std::string new_date = today_date_str();
            if (new_date != today) {
                today = new_date;
                strategy.reset_session();
                risk.reset_daily();
                pos_write_counter = 0;
                seed_regime_atr(today);
                if constexpr (kTrend) { strategy.set_event_day(db && db->is_connected() && db->calendar_event_day(today)); strategy.set_day_atr(regime.atr_pts); }
                LOG("[EXECUTOR] New trading day: %s", today.c_str());
            }

            // Flush session state to DB
            if (db && db->is_connected()) {
                const auto& sess = strategy.session();
                try {
                    auto rsnap = risk.snapshot();
                    db->upsert_session(today,
                        sess.orb_set ? strategy.orb_high() : 0.0,
                        sess.orb_set ? strategy.orb_low()  : 0.0,
                        sess.trades_today,
                        rsnap.daily_pnl,
                        rsnap.peak_equity,
                        risk.halted(),
                        risk.halt_reason(),
                        rsnap.equity);
                } catch (std::exception& e) {
                    LOG("[EXECUTOR] DB upsert_session failed: %s", e.what());
                    db->reconnect();
                }
            }

            // Check for completed trades → write to DB + immediately flush position
            Position completed;
            if (order_mgr.pop_trade_completed(completed)) {
                // mtf_scalper's cooldown/daily-loss guardrails need the real pnl; ORB/Trend
                // don't take a 3rd argument (their exit logic doesn't depend on it).
                if constexpr (kMtf) strategy.notify_trade_filled(completed.direction, completed.exit_reason, completed.pnl_usd);
                else                strategy.notify_trade_filled(completed.direction, completed.exit_reason);
                // Belt-and-suspenders: re-send cancel for any stop the exchange might
                // still hold, then flush tid=308 to prompt Rithmic to deliver the ACK.
                if (!orb_cfg.dry_run) {
                    order_mgr.recancel_pending_stops();
                    order_plant->flush_order_notifications();
                }
                {
                    int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count();
                    post_close_window_until_s  = now_s + orb_cfg.stop_cooldown_secs;
                    last_background_recancel_s = now_s;  // background retry starts after window
                }
                LOG("[EXECUTOR] Post-trade cleanup window: %ds intensive, then 30s background retry",
                    orb_cfg.stop_cooldown_secs);
                if (db && db->is_connected()) {
                    try {
                        db->write_trade(completed,
                                        order_mgr.last_entry_lat(),
                                        order_mgr.last_exit_lat(),
                                        today);
                    } catch (std::exception& e) {
                        LOG("[EXECUTOR] DB write_trade failed: %s", e.what());
                        db->reconnect();
                    }
                }
                // Immediate position flush after trade close so UI sees FLAT right away
                try {
                    flush_position(db.get(), today, order_mgr, strategy,
                                   orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                   md_up());
                } catch (std::exception& e) {
                    LOG("[EXECUTOR] DB flush_position (trade close) failed: %s", e.what());
                    if (db) db->reconnect();
                }
                pos_write_counter = 0;

                // All daily trades exhausted and position is flat. The strategy already refuses
                // new entries at the limit. Outside cycle_mode the process STAYS UP: exiting in
                // the same tick as the close skipped the post-close recancel window and left
                // nothing reading cancel ACKs / late stop fills (and systemd restarts it anyway).
                if (strategy.session().trades_today >= orb_cfg.max_daily_trades &&
                    order_mgr.position_snapshot().state == PosState::FLAT) {
                    LOG("[EXECUTOR] Daily trade limit reached (%d/%d)%s",
                        strategy.session().trades_today, orb_cfg.max_daily_trades,
                        orb_cfg.cycle_mode ? " — shutting down (cycle complete)"
                                           : " — no new entries today; staying up to guard open orders");
                    if (audit_conn)
                        audit_log.info("session.daily_limit",
                            orb_cfg.cycle_mode ? "cycle complete — clean exit" : "all trades done — idle");
                    if (orb_cfg.cycle_mode) {
                        g_running = false;
                        ioc_ref.stop();
                        co_return;
                    }
                }
            }

            // Attempt DB reconnect if disconnected (avoids prolonged stale-data windows)
            if (db && !db->is_connected()) {
                db->reconnect();
            }

            // Intensive recancel window: every second for stop_cooldown_secs after trade close
            if (post_close_window_until_s > 0) {
                int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (now_s < post_close_window_until_s) {
                    LOG("[EXECUTOR] Post-close recancel tick: %lds remaining",
                        (long)(post_close_window_until_s - now_s));
                    if (!orb_cfg.dry_run) {
                        order_mgr.recancel_pending_stops();
                        order_plant->flush_order_notifications();
                    }
                } else {
                    post_close_window_until_s = 0;
                    LOG("[EXECUTOR] Post-close recancel window expired — ghost-fill guard cleared; "
                        "background 30s retry continues until ACK");
                    order_mgr.clear_post_close_recancels();
                }
            }

            // Background retry: every 30s while unconfirmed server-basket cancels remain.
            // Fires only outside the intensive window to avoid double-sending.
            if (!orb_cfg.dry_run && post_close_window_until_s == 0 &&
                order_mgr.unconfirmed_server_cancels() > 0) {
                int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (now_s - last_background_recancel_s >= 30) {
                    LOG("[EXECUTOR] Background recancel: %d server-basket cancel(s) still unconfirmed",
                        order_mgr.unconfirmed_server_cancels());
                    order_mgr.recancel_pending_stops();
                    order_plant->flush_order_notifications();
                    last_background_recancel_s = now_s;
                }
            }

            // Stale-order alarm: a cancelled stop is normally ACKed in ~150 ms. One still
            // unconfirmed after 60 s may be WORKING at the exchange with nothing behind it
            // (2026-09-21: nine superseded stops stayed live 41–52 min while Rithmic refused
            // the cancels silently). The recancel loops keep retrying; this makes it loud.
            if (!orb_cfg.dry_run) {
                static int64_t stale_cancel_since_s = 0;
                static bool    stale_cancel_alerted = false;
                const int pending = order_mgr.pending_cancelled_stop_count();
                const int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (pending > 0) {
                    if (stale_cancel_since_s == 0) stale_cancel_since_s = now_s;
                    if (!stale_cancel_alerted && now_s - stale_cancel_since_s >= 60) {
                        stale_cancel_alerted = true;
                        LOG("[EXECUTOR] CRITICAL: %d cancelled stop(s) unconfirmed for %llds — may still be "
                            "WORKING at the exchange", pending, (long long)(now_s - stale_cancel_since_s));
                        audit_log.error("order.stale_cancel", std::to_string(pending) + " stop cancel(s) unconfirmed >60s");
                        int rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label + ": " +
                            std::to_string(pending) + " cancelled stop(s) NOT confirmed after 60s — check "
                            "RTrader for working orders\" >/dev/null 2>&1 &").c_str());
                        (void)rc;
                    }
                } else if (stale_cancel_since_s != 0) {
                    if (stale_cancel_alerted) {
                        LOG("[EXECUTOR] stale-cancel alarm cleared — all cancels confirmed");
                        int rc = std::system(("grid-notify \"nq_executor " + orb_cfg.account_label +
                            ": stale stop cancels now confirmed\" >/dev/null 2>&1 &").c_str());
                        (void)rc;
                    }
                    stale_cancel_since_s = 0; stale_cancel_alerted = false;
                }
            }

            // Periodic audit flush (best-effort; only when audit_conn is available)
            if (audit_conn) {
                try { audit_log.flush(); } catch (...) {}
            }

            // Periodic position flush every 1 second + notify live price
            if (++pos_write_counter >= 1) {
                pos_write_counter = 0;
                try {
                    flush_position(db.get(), today, order_mgr, strategy,
                                   orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                   md_up());
                } catch (std::exception& e) {
                    LOG("[EXECUTOR] DB flush_position failed: %s", e.what());
                    if (db) db->reconnect();
                }
                double last_px = strategy.last_price();
                if (db && last_px > 0.0) {
                    try { db->notify_tick(last_px); } catch (...) {}
                }
            }

            // ── Position state watchdog ─────────────────────────────────────────
            // Tracks how long the position has been in each state.
            // PENDING_ENTRY > 5s or PENDING_EXIT > 10s: likely stuck — log CRITICAL.
            // LONG/SHORT > 120s: log progress every 60s so log analysis can
            // correlate open duration with any anomaly at shutdown.
            {
                int64_t now_s = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                Position wsnap = order_mgr.position_snapshot();
                if (wsnap.state != watchdog_state) {
                    watchdog_state   = wsnap.state;
                    watchdog_since_s = now_s;
                }
                int64_t held = now_s - watchdog_since_s;
                if (wsnap.state == PosState::PENDING_ENTRY) {
                    trail_snapshot_counter = 0;  // not in position — reset trail snapshot counter
                    if (held >= 5 && held % 5 == 0)
                        LOG("[EXECUTOR] WARN: PENDING_ENTRY for %lds — entry fill delayed "
                            "basket=%s px=%.2f",
                            (long)held, wsnap.basket_id_entry.c_str(),
                            strategy.last_price());
                    // Entry-order watchdog: a gateway reject that could not be correlated
                    // (or was never delivered) would otherwise leave this state stuck
                    // forever — cancel the entry and revert to FLAT after 10s.
                    if (order_mgr.pending_entry_timeout_check(10)) {
                        LOG("[EXECUTOR] PENDING_ENTRY timeout — entry cancelled, state FLAT");
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected,
                                       orb_cfg.point_value, md_up());
                    }
                } else if (wsnap.state == PosState::LONG || wsnap.state == PosState::SHORT) {
                    if (held >= 120 && held % 60 == 0)
                        LOG("[EXECUTOR] POSITION OPEN %lds: %s entry=%.2f sl=%.2f "
                            "px=%.2f be=%d trailing=%d",
                            (long)held,
                            wsnap.state == PosState::LONG ? "LONG" : "SHORT",
                            wsnap.entry_price, wsnap.sl_price,
                            strategy.last_price(),
                            (int)wsnap.be_triggered, (int)wsnap.trailing_active);
                    // Periodic trail state snapshot every 10 seconds
                    if (++trail_snapshot_counter >= 10) {
                        trail_snapshot_counter = 0;
                        auto trail_elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::steady_clock::now() -
                            wsnap.fill_time).count();
                        // WARNING: trailing_active=true but be_triggered=false should never
                        // happen — trail_delay_secs requires the same mfe >= trail_be_trigger
                        // condition as BE, so BE always fires first.
                        if (wsnap.trailing_active && !wsnap.be_triggered) {
                            LOG("[EXECUTOR] WARNING: trailing_active=true but be_triggered=false "
                                "— invariant violated! entry=%.2f sl=%.2f mfe=%.2f",
                                wsnap.entry_price, wsnap.sl_price, wsnap.mfe);
                        }
                        LOG("[EXECUTOR] Trail state: entry=%.2f sl=%.2f be_triggered=%d "
                            "trailing=%d trail_delay_elapsed=%lds/%ds price=%.2f mfe=%.2f",
                            wsnap.entry_price, wsnap.sl_price,
                            (int)wsnap.be_triggered, (int)wsnap.trailing_active,
                            (long)trail_elapsed, orb_cfg.trail_delay_secs,
                            strategy.last_price(), wsnap.mfe);
                    }
                } else if (wsnap.state == PosState::PENDING_EXIT) {
                    trail_snapshot_counter = 0;  // exiting — reset trail snapshot counter
                    if (held >= 10 && held % 10 == 0) {
                        LOG("[EXECUTOR] CRITICAL: PENDING_EXIT for %lds — exit fill delayed "
                            "basket=%s entry=%.2f reason=%s px=%.2f",
                            (long)held, wsnap.basket_id_exit.c_str(),
                            wsnap.entry_price, wsnap.exit_reason.c_str(),
                            strategy.last_price());
                        // Re-drive the exit: the protective stop was already cancelled
                        // when the exit was initiated, so a resting unfilled exit limit
                        // means the position is naked. Cancel it and re-send at the
                        // current price instead of waiting forever.
                        if (!orb_cfg.dry_run)
                            order_mgr.retry_stuck_exit(strategy.last_price());
                    }
                } else {
                    trail_snapshot_counter = 0;  // FLAT — reset trail snapshot counter
                }
            }

            // Ghost-halt watchdog: re-subscribe to PnL every 30s so the tid=451 handler
            // auto-unwinds any ghost position or confirms FLAT without manual intervention.
            if (order_mgr.is_entry_halted()) {
                ++ghost_halt_secs;
                if (ghost_halt_secs % 30 == 0) {
                    if (order_plant->connected && order_plant->ws) {
                        bool pnl_resubscribed = false;
                        try {
                            rti::RequestPnLPositionUpdates pnl_req;
                            pnl_req.set_template_id(400);
                            pnl_req.set_request(rti::RequestPnLPositionUpdates::SUBSCRIBE);
                            pnl_req.set_fcm_id(fcm_id_r);
                            pnl_req.set_ib_id(ib_id_r);
                            pnl_req.set_account_id(orb_cfg.account_id);
                            co_await op_write_q.write(proto_frame(pnl_req));
                            pnl_resubscribed = true;
                        } catch (...) {}
                        LOG("[EXECUTOR] ENTRY-HALT: re-subscribed PnL for position snapshot "
                            "(halted %ds, resubscribed=%d)", ghost_halt_secs, (int)pnl_resubscribed);
                    }
                    if (ghost_halt_secs >= 60) {
                        LOG("[EXECUTOR] WARNING: entry halt still active after %ds — "
                            "tid=451 should auto-recover; check RTrader if halt persists >5min.",
                            ghost_halt_secs);
                    }
                }
            } else {
                ghost_halt_secs = 0;
            }
        }
    };

    // ── Legends TICKER_PLANT loop (price comparison only) ────────────────────
    // Connects with Legends credentials to a second TICKER_PLANT session and
    // writes legends_price to live_position for side-by-side comparison with the
    // AMP price. Expects FORCED LOGOUTs (Legends allows one session; ORDER_PLANT
    // holds it), so it reconnects after each kick without disrupting the main loop.
    // NOTE: co_await must never be inside a catch block (C++20 restriction) —
    //       errors set a flag, then the timer is awaited outside the catch.
    auto legends_md_loop = [&]() -> asio::awaitable<void> {
        while (g_running) {
            int retry_secs = 0;
            std::unique_ptr<WsStream> l_ws;

            // ── connect (probe) ──────────────────────────────────────────────
            try { l_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.rithmic_url); }
            catch (...) { LOG("[LEGENDS_MD] Connect failed"); retry_secs = 1; }
            if (retry_secs) {
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(retry_secs));
                co_await t.async_wait(asio::use_awaitable); continue;
            }

            // ── system info probe ────────────────────────────────────────────
            try {
                rti::RequestRithmicSystemInfo req; req.set_template_id(16);
                co_await ws_write(*l_ws, proto_frame(req));
                beast::flat_buffer buf;
                for (;;) {
                    buf.clear();
                    co_await l_ws->async_read(buf, asio::use_awaitable);
                    auto pl = proto_strip(beast::buffers_to_string(buf.data()));
                    rti::Base b; if (!b.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (b.template_id() == 17) break;
                }
            } catch (...) { retry_secs = 1; }
            if (retry_secs) {
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(retry_secs));
                co_await t.async_wait(asio::use_awaitable); continue;
            }

            // ── reconnect for login ──────────────────────────────────────────
            try { l_ws->close(websocket::close_code::normal); } catch (...) {}
            l_ws.reset();
            try { l_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.rithmic_url); }
            catch (...) { retry_secs = 1; }
            if (retry_secs) {
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(retry_secs));
                co_await t.async_wait(asio::use_awaitable); continue;
            }

            // ── login ────────────────────────────────────────────────────────
            bool login_ok = false;
            bool auth_rejected = false;  // true = server said no, false = network error
            try {
                rti::RequestLogin req;
                req.set_template_id(10); req.set_template_version("3.9");
                req.set_user(orb_cfg.rithmic_user);
                req.set_password(orb_cfg.rithmic_password);
                req.set_system_name(orb_cfg.rithmic_system_name);
                req.set_app_name(orb_cfg.app_name);
                req.set_app_version(orb_cfg.app_version);
                req.set_infra_type(rti::RequestLogin::TICKER_PLANT);
                co_await ws_write(*l_ws, proto_frame(req));
                beast::flat_buffer buf;
                for (;;) {
                    buf.clear();
                    co_await l_ws->async_read(buf, asio::use_awaitable);
                    auto pl = proto_strip(beast::buffers_to_string(buf.data()));
                    rti::Base b; if (!b.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                    if (b.template_id() == 11) {
                        rti::ResponseLogin resp; if (!resp.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                        login_ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                        if (!login_ok) auth_rejected = true;
                        break;
                    }
                }
            } catch (...) { login_ok = false; }
            if (!login_ok) {
                if (auth_rejected) {
                    LOG("[LEGENDS_MD] Login rejected by server — stopping (check credentials/permissions)");
                    co_return;
                }
                LOG("[LEGENDS_MD] Login failed (network) — retry in 30s");
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(30));
                co_await t.async_wait(asio::use_awaitable); continue;
            }
            LOG("[LEGENDS_MD] Legends TICKER_PLANT connected");

            // Heartbeat immediately after login
            try {
                rti::RequestHeartbeat hb; hb.set_template_id(18);
                hb.set_ssboe(hb_ssboe_now());
                co_await ws_write(*l_ws, proto_frame(hb));
            } catch (...) {}

            // ── subscribe ────────────────────────────────────────────────────
            bool sub_ok = true;
            try {
                rti::RequestMarketDataUpdate sub;
                sub.set_template_id(100);
                sub.set_symbol(trade_symbol);
                sub.set_exchange(orb_cfg.exchange);
                sub.set_request(rti::RequestMarketDataUpdate::SUBSCRIBE);
                sub.set_update_bits(1);  // LAST_TRADE
                co_await ws_write(*l_ws, proto_frame(sub));
            } catch (...) { sub_ok = false; }
            if (!sub_ok) {
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(30));
                co_await t.async_wait(asio::use_awaitable); continue;
            }

            // ── tick read loop ───────────────────────────────────────────────
            beast::flat_buffer buf;
            bool read_err = false;
            int  delay_after = 0;
            while (g_running && !read_err) {
                buf.clear(); read_err = false;
                try { co_await l_ws->async_read(buf, asio::use_awaitable); }
                catch (std::exception& e) {
                    LOG("[LEGENDS_MD] Read error: %s — reconnecting", e.what());
                    read_err = true; delay_after = 1;
                }
                if (read_err) break;

                auto payload = proto_strip(beast::buffers_to_string(buf.data()));
                rti::Base base; if (!base.ParseFromString(payload)) continue;
                int tid = base.template_id();

                if (tid == 150) {
                    rti::LastTrade lt; if (!lt.ParseFromString(payload)) continue;
                    if (lt.trade_price() <= 0.0 || lt.trade_size() <= 0) continue;
                    if (db) db->write_legends_price(today, lt.trade_price());
                } else if (tid == 77) {
                    LOG("[LEGENDS_MD] FORCED LOGOUT — reconnecting in 30s");
                    read_err = true; delay_after = 1;
                } else if (tid == 18) {
                    rti::ResponseHeartbeat hb_resp; hb_resp.set_template_id(19);
                    bool hb_ok = true;
                    try { co_await ws_write(*l_ws, proto_frame(hb_resp)); }
                    catch (...) { hb_ok = false; }
                    if (!hb_ok) { read_err = true; delay_after = 1; }
                }
            }
            if (delay_after > 0) {
                asio::steady_timer t(ex); t.expires_after(std::chrono::seconds(delay_after));
                co_await t.async_wait(asio::use_awaitable);
            }
        }
    };

    // ── Main MD receive loop (self-reconnecting) ──────────────────────────────
    // Never co_return on disconnect — reconnects internally so ORDER_PLANT stays
    // alive. md_ws is reset to nullptr on error; the inner reconnect loop restores
    // it before the next read.
    // Tick handler shared by every feed (WebSocket MD, pg poll): trail/stop
    // check, strategy, and the per-minute visibility log.
    bool first_tick_received = false;
    int  last_log_minute     = -1;
    auto process_tick = [&](const OrbTick& tick) {
                last_tick_us = tick.ts_micros;   // book freshness is judged against the print, not wall time
                { int rh, rm; tick_et_hm(tick.ts_micros, rh, rm); regime.on_tick(today, rh, rm, tick.ts_micros, tick.price, (double)tick.size); }
                // Book exits (book_exit_flip / book_tp_imbalance — the paper fleet's __bx/__btp
                // overlays): the same QuoteState::book_exit() the paper brokers run, as a market
                // flatten. Off unless the config sets them.
                if (orb_cfg.book_exit_flip > 0.0 || orb_cfg.book_tp_imbalance > 0.0) {
                    Position bp = order_mgr.position_snapshot();
                    if (bp.state == PosState::LONG || bp.state == PosState::SHORT) {
                        const int dir = bp.state == PosState::LONG ? 1 : -1;
                        const std::string why = book.book_exit(orb_cfg, dir, last_tick_us,
                                                               (tick.price - bp.entry_price) * dir);
                        if (!why.empty()) {
                            LOG("[EXECUTOR] Book exit '%s' (imb=%.2f) — flattening", why.c_str(), book.q.imbalance());
                            order_mgr.flatten_now(why, tick.price);
                        }
                    }
                }
                // Bracket check on every tick, BEFORE the strategy sees this tick
                // (mirrors PaperBracketBroker's ordering: bracket first, signals fill
                // on the next tick) — flush DB immediately on any SL move.
                if constexpr (kMtf) {
                    // mtf_scalper owns its stop/target math (cur_stop()/cur_tp()); the
                    // host only checks price against them and maintains the resting
                    // exchange stop. check_trail_and_stop's cfg_-driven BE/trail math
                    // (ORB/Trend only) does not apply here.
                    if (order_mgr.check_external_stop(tick.price, strategy.cur_stop())) {
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                       md_up());
                    }
                    double tp = strategy.cur_tp();   // NaN = trailing armed, no target leg
                    if (!std::isnan(tp) && tp > 0.0) {
                        Position psnap = order_mgr.position_snapshot();
                        bool hit = (psnap.direction == OrbSignal::BUY  && tick.price >= tp) ||
                                   (psnap.direction == OrbSignal::SELL && tick.price <= tp);
                        if (hit) {
                            order_mgr.flatten_now("target", tick.price);
                            flush_position(db.get(), today, order_mgr, strategy,
                                           orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                           md_up());
                        }
                    }
                } else {
                    if (order_mgr.check_trail_and_stop(tick.price)) {
                        flush_position(db.get(), today, order_mgr, strategy,
                                       orb_cfg.dry_run || order_plant->connected, orb_cfg.point_value,
                                       md_up());
                    }
                }

                // Feed strategy
                strategy.on_tick(tick);
                settle_strategy();

                // ── Cycle visibility logs ─────────────────────────────────────
                int et_h, et_m;
                current_et(et_h, et_m);

                if (!first_tick_received) {
                    first_tick_received = true;
                    LOG("[EXECUTOR] First tick: px=%.2f ET=%02d:%02d",
                        tick.price, et_h, et_m);
                }

                int cur_min = et_h * 60 + et_m;
                if (cur_min != last_log_minute) {
                    last_log_minute = cur_min;
                    if constexpr (kTrend) {
                        Position psnap = order_mgr.position_snapshot();
                        LOG("[EXECUTOR] TREND ET=%02d:%02d px=%.2f pos_state=%d engine_in_pos=%d sl=%.2f trades=%d/%d book=%s%s",
                            et_h, et_m, tick.price, (int)psnap.state,
                            (int)strategy.session().in_position, psnap.sl_price,
                            strategy.session().trades_today, orb_cfg.max_daily_trades,
                            book.fresh(last_tick_us) ? (std::to_string(book.q.imbalance()).substr(0, 4)).c_str() : "none",
                            strategy.session().risk_halted ? (" HALTED:" + strategy.session().halt_reason).c_str() : "");
                    } else if constexpr (kMtf) {
                        Position psnap = order_mgr.position_snapshot();
                        double tp = strategy.cur_tp();
                        LOG("[EXECUTOR] MTF ET=%02d:%02d px=%.2f pos_state=%d engine_in_pos=%d "
                            "stop=%.2f tp=%s trades=%d/%d%s",
                            et_h, et_m, tick.price, (int)psnap.state,
                            (int)strategy.session().in_position, psnap.sl_price,
                            std::isnan(tp) ? (strategy.in_position() ? "trailing" : "-")
                                           : (std::to_string(tp)).c_str(),
                            strategy.session().trades_today, orb_cfg.max_daily_trades,
                            strategy.session().risk_halted ? (" HALTED:" + strategy.session().halt_reason).c_str() : "");
                    } else if (!strategy.orb_set()) {
                        double oh = strategy.orb_high();
                        double ol = strategy.orb_low();
                        bool   has = (oh > ol) && (ol < 1e10);
                        LOG("[EXECUTOR] ORB building ET=%02d:%02d px=%.2f %s",
                            et_h, et_m, tick.price,
                            has ? (std::string("range=[") +
                                   std::to_string((int)ol) + ".." +
                                   std::to_string((int)oh) + "]").c_str()
                                : "(no range yet)");
                    } else {
                        Position psnap = order_mgr.position_snapshot();
                        const char* pss;
                        switch (psnap.state) {
                            case PosState::FLAT:          pss = "FLAT";          break;
                            case PosState::PENDING_ENTRY: pss = "PENDING_ENTRY"; break;
                            case PosState::LONG:          pss = "LONG";          break;
                            case PosState::SHORT:         pss = "SHORT";         break;
                            case PosState::PENDING_EXIT:  pss = "PENDING_EXIT";  break;
                            default:                      pss = "?";             break;
                        }
                        LOG("[EXECUTOR] ET=%02d:%02d px=%.2f "
                            "orb=[%.2f..%.2f] dist_long=%+.1f dist_short=%+.1f "
                            "pos=%s sl=%.2f trades=%d/%d",
                            et_h, et_m, tick.price,
                            strategy.orb_low(), strategy.orb_high(),
                            tick.price - strategy.orb_high(),
                            strategy.orb_low() - tick.price,
                            pss, psnap.sl_price,
                            strategy.session().trades_today,
                            orb_cfg.max_daily_trades);
                    }
                }
    };

    // ── pg feed: poll the collector's ticks table ─────────────────────────────
    // Watermark starts at "now" so a restart never replays history (the ORB is
    // restored from live_sessions, not rebuilt from old ticks). Rows are read
    // in (ts_event, seq) order exactly as the paper engine does.
    auto pg_feed_loop = [&]() -> asio::awaitable<void> {
        asio::steady_timer t(ex);
        auto now_us = []() -> int64_t {
            return std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
        };
        int64_t watermark_us = now_us() - 1'000'000LL;
        int64_t warmup_until_us = 0;
        if constexpr (!kOrb) {
            if (orb_cfg.warmup_minutes > 0) {
                warmup_until_us = watermark_us;
                watermark_us   -= (int64_t)orb_cfg.warmup_minutes * 60'000'000LL;
                warming_up = true;
                LOG("[PG-FEED] warm-up: replaying the last %d min of ticks into the strategy "
                    "(signals ignored, no orders)", orb_cfg.warmup_minutes);
            }
        }
        // Replay caught up with the present: the engine may believe it is "in position" or
        // have counted replayed entries — release it and restore the real trade count.
        auto end_warmup = [&]() {
            if constexpr (!kOrb) {
                warming_up = false;
                if (strategy.session().in_position)
                    strategy.notify_trade_filled(OrbSignal::FLATTEN_EOD, "warmup_replay");
                int done = (db && db->is_connected()) ? db->count_today_trades(today, orb_cfg.cycle_start_epoch) : 0;
                strategy.seed_trades_today(done);
                LOG("[PG-FEED] warm-up complete — %d replayed signal(s) ignored; live from now, "
                    "trades_today=%d", warmup_signals_dropped, done);
            }
        };
        // Top of book (collector's bbo table), merged into the tick stream by time exactly as
        // paper_main does: every quote up to a print's timestamp is applied BEFORE that print,
        // so the gates and book_imbalance see the book as it stood at the trade. Same watermark
        // as the ticks, so the warm-up replay warms the book too. bbo absent/empty → no quotes,
        // gates report "no_quote" (a gated config on a box without bbo blocks every entry —
        // by design: an ungated fallback would silently be a different strategy).
        int64_t bbo_watermark_us = watermark_us;
        std::deque<paper::Quote> qbuf;
        auto feed_quotes_until = [&](int64_t ts_us) {
            if (qbuf.empty() && db && db->is_connected()) {
                std::string wm = std::to_string(bbo_watermark_us);
                const char* params[2] = { wm.c_str(), orb_cfg.md_feed_symbol.c_str() };
                PGresult* r = PQexecParams(db->raw_conn(),
                    "SELECT (EXTRACT(EPOCH FROM ts_event)*1000000)::bigint, bid_price, bid_size, "
                    "ask_price, ask_size FROM bbo WHERE ts_event > to_timestamp($1::double precision / 1000000.0) "
                    "AND symbol = $2 ORDER BY ts_event LIMIT 5000",
                    2, nullptr, params, nullptr, nullptr, 0);
                if (r && PQresultStatus(r) == PGRES_TUPLES_OK) {
                    for (int i = 0, n = PQntuples(r); i < n; ++i) {
                        paper::Quote q;
                        q.ts_us  = std::atoll(PQgetvalue(r, i, 0));
                        q.bid    = std::atof(PQgetvalue(r, i, 1)); q.bid_sz = std::atoi(PQgetvalue(r, i, 2));
                        q.ask    = std::atof(PQgetvalue(r, i, 3)); q.ask_sz = std::atoi(PQgetvalue(r, i, 4));
                        bbo_watermark_us = q.ts_us;
                        if (q.valid()) qbuf.push_back(q);
                    }
                }
                if (r) PQclear(r);
            }
            while (!qbuf.empty() && qbuf.front().ts_us <= ts_us) {
                const paper::Quote& q = qbuf.front();
                book.on_quote(q, NQ_TICK_SIZE);
                if constexpr (kTrend) strategy.on_quote(q.ts_us, q.bid, q.bid_sz, q.ask, q.ask_sz, NQ_TICK_SIZE);
                qbuf.pop_front();
            }
        };
        bool db_warned = false;
        LOG("[PG-FEED] Polling ticks symbol=%s every %dms (collector feed)%s",
            orb_cfg.md_feed_symbol.c_str(), orb_cfg.md_poll_ms,
            book.any_gate(orb_cfg) ? " + bbo (book gates on)" : " + bbo");
        while (g_running) {
            if (db && db->is_connected()) {
                std::string wm  = std::to_string(watermark_us);
                const char* params[2] = { wm.c_str(), orb_cfg.md_feed_symbol.c_str() };
                PGresult* r = PQexecParams(db->raw_conn(),
                    "SELECT (EXTRACT(EPOCH FROM ts_event)*1000000)::bigint, price, size, is_buy "
                    "FROM ticks WHERE ts_event > to_timestamp($1::double precision / 1000000.0) "
                    // Total order (dedup-key order): `seq` is only a 0..4 batch sub-index, so
                    // (ts_event, seq) alone left same-microsecond ticks in heap order — the
                    // paper replay drifted after a VACUUM (2026-09-26). Same clause as
                    // PaperDb::poll_ticks, so live and paper see the identical tick sequence.
                    "AND symbol = $2 ORDER BY ts_event, seq, price, size LIMIT 5000",
                    2, nullptr, params, nullptr, nullptr, 0);
                if (r && PQresultStatus(r) == PGRES_TUPLES_OK) {
                    db_warned = false;
                    int n = PQntuples(r);
                    // Page boundary: a same-microsecond group cut by LIMIT would lose its tail
                    // (next poll asks for ts_event > last ts). Leave the group for the next poll.
                    int n_use = n;
                    if (n == 5000) {
                        const int64_t last_ts = std::atoll(PQgetvalue(r, n - 1, 0));
                        int k = n;
                        while (k > 1 && std::atoll(PQgetvalue(r, k - 1, 0)) == last_ts) --k;
                        if (std::atoll(PQgetvalue(r, k - 1, 0)) != last_ts) n_use = k;
                    }
                    if (n > 0) {
                        last_tick_epoch_s.store(
                            std::chrono::duration_cast<std::chrono::seconds>(
                                std::chrono::system_clock::now().time_since_epoch()).count());
                        if (!pg_feed_fresh.exchange(true)) {
                            LOG("[PG-FEED] ticks flowing (%d rows)", n);
                            if (strategy.session().risk_halted &&
                                strategy.session().halt_reason == "pg_feed_stale")
                                strategy.unhalt_trading("pg feed resumed");
                        }
                    }
                    for (int i = 0; i < n_use; ++i) {
                        int64_t ts_us = std::atoll(PQgetvalue(r, i, 0));
                        double  px    = std::atof(PQgetvalue(r, i, 1));
                        int64_t sz    = std::atoll(PQgetvalue(r, i, 2));
                        bool    buy   = std::strcmp(PQgetvalue(r, i, 3), "t") == 0;
                        watermark_us  = ts_us;
                        if (px <= 0.0 || sz <= 0) continue;
                        OrbTick tick{ts_us, px, sz, buy};
                        feed_quotes_until(ts_us);
                        if (warming_up) {
                            if (ts_us < warmup_until_us) { strategy.on_tick(tick); continue; }
                            end_warmup();
                        }
                        process_tick(tick);
                    }
                    if (warming_up && n < 5000) end_warmup();   // history exhausted
                } else if (!db_warned) {
                    LOG("[PG-FEED] WARN poll failed: %s (further errors suppressed)",
                        r ? PQresultErrorMessage(r) : "null result");
                    db_warned = true;
                }
                if (r) PQclear(r);
            } else if (!db_warned) {
                LOG("[PG-FEED] WARN no DB connection — cannot read ticks");
                db_warned = true;
            }
            t.expires_after(std::chrono::milliseconds(orb_cfg.md_poll_ms));
            co_await t.async_wait(asio::use_awaitable);
        }
    };

    auto md_loop = [&]() -> asio::awaitable<void> {
        beast::flat_buffer buf;
        while (g_running) {
            // ── reconnect if md_ws is down ─────────────────────────────────
            while (g_running && !md_ws) {
                LOG("[EXECUTOR] MD: reconnecting...");
                bool login_ok = false;
                bool auth_rejected = false;
                try {
                    // Skip system info probe on reconnects — probe close triggers FORCED_LOGOUT
                    // on the subsequent login session (Rithmic server-side session race).
                    // The probe is only needed once at startup (already done above).
                    md_ws = co_await connect_ws(ioc, ssl_ctx, orb_cfg.md_url);
                    md_write_q.attach(md_ws.get());
                    {
                        rti::RequestLogin req; req.set_template_id(10);
                        req.set_template_version("3.9");
                        req.set_user(orb_cfg.md_user);
                        req.set_password(orb_cfg.md_password);
                        req.set_system_name(orb_cfg.md_system_name);
                        req.set_app_name(orb_cfg.app_name + "-MD");
                        req.set_app_version(orb_cfg.app_version);
                        req.set_infra_type(rti::RequestLogin::TICKER_PLANT);
                        co_await md_write_q.write(proto_frame(req));
                        beast::flat_buffer lb;
                        for (;;) {
                            lb.clear();
                            co_await md_ws->async_read(lb, asio::use_awaitable);
                            std::string pl;
                            try {
                                pl = proto_strip(beast::buffers_to_string(lb.data()));
                            } catch (std::exception& e) {
                                LOG("[EXECUTOR] MD reconnect login: malformed frame (%s) — skipping",
                                    e.what());
                                continue;
                            }
                            rti::Base b; if (!b.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                            if (b.template_id() == 11) {
                                rti::ResponseLogin resp; if (!resp.ParseFromString(pl)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                                login_ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                                if (!login_ok) {
                                    // Server answered: auth/config rejection is terminal —
                                    // retrying the same credentials would loop forever.
                                    auth_rejected = true;
                                    LOG("[EXECUTOR] MD reconnect: login REJECTED by server "
                                        "(rp_code=%s) — terminal, not retrying",
                                        resp.rp_code().empty() ? "?" : resp.rp_code(0).c_str());
                                }
                                break;
                            }
                        }
                    }
                    if (login_ok) {
                        // immediate heartbeat required after login
                        rti::RequestHeartbeat hb; hb.set_template_id(18);
                        hb.set_ssboe(hb_ssboe_now());
                        co_await md_write_q.write(proto_frame(hb));
                        // re-subscribe to last trade
                        rti::RequestMarketDataUpdate sub; sub.set_template_id(100);
                        sub.set_symbol(trade_symbol);
                        sub.set_exchange(orb_cfg.exchange);
                        sub.set_request(rti::RequestMarketDataUpdate::SUBSCRIBE);
                        sub.set_update_bits(1);
                        co_await md_write_q.write(proto_frame(sub));
                        LOG("[EXECUTOR] MD reconnect OK — re-subscribed to %s", trade_symbol.c_str());
                    } else {
                        md_write_q.detach();
                        try { md_ws->close(websocket::close_code::normal); } catch (...) {}
                        md_ws.reset();
                    }
                } catch (std::exception& e) {
                    LOG("[EXECUTOR] MD reconnect error: %s", e.what());
                    if (md_ws) {
                        md_write_q.detach();
                        try { md_ws->close(websocket::close_code::normal); } catch (...) {}
                        md_ws.reset();
                    }
                }
                if (auth_rejected) {
                    // Terminal: bad credentials cannot recover by retrying. Stop the whole
                    // executor rather than trading blind (no market data).
                    g_running = false;
                    ioc_ref.stop();
                    co_return;
                }
                if (!md_ws) {
                    asio::steady_timer t(ex);
                    t.expires_after(std::chrono::seconds(5));
                    co_await t.async_wait(asio::use_awaitable);
                }
            }
            if (!g_running) co_return;

            // ── read one message ───────────────────────────────────────────
            buf.clear();
            bool read_error = false;
            try {
                co_await md_ws->async_read(buf, asio::use_awaitable);
            } catch (std::exception& e) {
                if (!g_running) co_return;
                LOG("[EXECUTOR] MD read error: %s — reconnecting", e.what());
                md_write_q.detach();
                try { md_ws->close(websocket::close_code::normal); } catch (...) {}
                md_ws.reset();
                read_error = true;
            }
            if (read_error) continue;  // re-enters reconnect block above

            std::string payload;
            try {
                payload = proto_strip(beast::buffers_to_string(buf.data()));
            } catch (std::exception& e) {
                // Malformed short frame — log and skip; never let it kill the loop.
                LOG("[EXECUTOR] MD malformed frame (%s) len=%zu — skipping message",
                    e.what(), buf.size());
                continue;
            }
            rti::Base base;
            if (!base.ParseFromString(payload)) continue;

            int tid = base.template_id();

            if (tid == 150) {
                // LastTrade
                rti::LastTrade lt;
                if (!lt.ParseFromString(payload)) continue;

                // Filter zero-price / zero-size ticks (Rithmic heartbeat events)
                if (lt.trade_price() <= 0.0 || lt.trade_size() <= 0) continue;

                last_tick_epoch_s.store(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch()).count());

                int64_t ts_us = static_cast<int64_t>(lt.ssboe()) * 1'000'000LL
                              + lt.usecs();
                OrbTick tick{ts_us, lt.trade_price(), lt.trade_size(),
                             lt.aggressor() == rti::LastTrade::BUY};
                process_tick(tick);

            } else if (tid == 18) {
                // Server-sent RequestHeartbeat — must respond with ResponseHeartbeat (tid=19)
                rti::ResponseHeartbeat hb_resp;
                hb_resp.set_template_id(19);
                try {
                    co_await md_write_q.write(proto_frame(hb_resp));
                } catch (...) {
                    LOG("[EXECUTOR] MD heartbeat response send failed");
                }
            } else if (tid == 19) {
                // ResponseHeartbeat — server acked our heartbeat, no action needed
            } else if (tid == 101) {
                // ResponseMarketDataUpdate — subscription ack
                rti::ResponseMarketDataUpdate resp;
                if (!resp.ParseFromString(payload)) { LOG("[EXECUTOR] proto parse failed"); continue; }
                std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                std::string txt = resp.rp_code().size() > 1 ? resp.rp_code(1) : "";
                LOG("[EXECUTOR] MD subscription %s (rp_code=%s %s)",
                    rpc == "0" ? "OK" : "FAILED", rpc.c_str(), txt.c_str());
            } else if (tid == 77) {
                // ForcedLogout — server is closing this session; brief cooldown then reconnect
                LOG("[EXECUTOR] MD: FORCED LOGOUT (tid=77) — reconnecting MD without touching ORDER_PLANT");
                md_write_q.detach();
                try { md_ws->close(websocket::close_code::normal); } catch (...) {}
                md_ws.reset();
                // 3-second non-blocking pause: lets Rithmic expire the old session
                // before we open a new one, breaking the logout storm loop
                {
                    asio::steady_timer t(ex);
                    t.expires_after(std::chrono::seconds(3));
                    co_await t.async_wait(asio::use_awaitable);
                }
            } else if (tid == 11) {
                LOG("[EXECUTOR] Login response on MD loop (template 11) — ignoring");
            } else {
                LOG("[EXECUTOR] MD: unhandled tid=%d len=%zu", tid, payload.size());
            }
        }
    };

    // Signal-responsive flatten: handles SIGTERM/SIGINT immediately from within the
    // ASIO executor so flatten_now() fires without waiting for the next eod_loop tick.
    // This closes any PENDING_ENTRY cancel and LONG/SHORT market exit right away.
    auto signal_loop = [&]() -> asio::awaitable<void> {
        asio::signal_set sigs(ex, SIGINT, SIGTERM);
        int sig = co_await sigs.async_wait(asio::use_awaitable);
        LOG("[EXECUTOR] Signal %d received — pending_cancelled_stops=%d",
            sig, order_mgr.pending_cancelled_stop_count());
        g_draining = true;                        // before g_running: readers must not exit
        g_running = false;
        strategy.halt_trading("shutdown");
        order_mgr.flatten_now("kill_signal", strategy.last_price());   // 0 → stop-anchored price
        if (audit_conn)
            audit_log.info("session.eod_flatten", "kill signal immediate flatten");

        // Phase 1: wait for the market exit fill to confirm (position = FLAT).
        // Typical fill latency: <1s.  Cap at 10s so we don't block SIGKILL (60s).
        {
            constexpr int kMaxTicks = 100;   // 100 × 100 ms = 10 s
            constexpr int kTickMs   = 100;
            int ticks = 0;
            while (!order_mgr.is_flat() && ticks < kMaxTicks) {
                asio::steady_timer t(ex);
                t.expires_after(std::chrono::milliseconds(kTickMs));
                co_await t.async_wait(asio::use_awaitable);
                ++ticks;
            }
            if (order_mgr.is_flat())
                LOG("[EXECUTOR] Phase-1 drain: position FLAT after %d ms "
                    "(pending_cancelled_stops=%d)",
                    ticks * kTickMs, order_mgr.pending_cancelled_stop_count());
            else
                LOG("[EXECUTOR] Phase-1 drain: WARNING — not flat after %d ms "
                    "(pending_cancelled_stops=%d) — proceeding to phase-2",
                    kMaxTicks * kTickMs, order_mgr.pending_cancelled_stop_count());
        }

        // Phase 2: extra window to catch late exchange stop fires.
        // Exchange-native stops can fire up to ~30s after a cancel request.
        // Keep io_context alive so cancelled_stops_ handler can send the unwind.
        // We use 8s: well within the 60s SIGKILL budget, catches virtually all
        // late stop fires observed in practice (typical latency <3s).
        {
            int pending = order_mgr.pending_cancelled_stop_count();
            LOG("[EXECUTOR] Phase-2 drain: holding io_context open 8s for late stop fires "
                "(pending_cancelled_stops=%d)", pending);
            // At least 8 s, then keep going (up to 25 s, inside the unit's 45 s stop budget)
            // while a cancel is unconfirmed or the exchange still disagrees with us — the
            // readers and the reconciler are live during the drain.
            constexpr int kExtraMs     = 8000;
            constexpr int kMaxMs       = 25000;
            constexpr int kExtraTickMs = 500;
            auto unsettled = [&] {
                return order_mgr.pending_cancelled_stop_count() > 0 ||
                       (last_exch_net != INT_MIN && !order_mgr.net_qty_consistent(last_exch_net));
            };
            for (int i = 0; i < kMaxMs / kExtraTickMs && (i < kExtraMs / kExtraTickMs || unsettled()); ++i) {
                asio::steady_timer t(ex);
                t.expires_after(std::chrono::milliseconds(kExtraTickMs));
                co_await t.async_wait(asio::use_awaitable);
                int rem = order_mgr.pending_cancelled_stop_count();
                if (rem != pending) {
                    LOG("[EXECUTOR] Phase-2: pending_cancelled_stops changed %d→%d "
                        "(cancel ACK or unwind fired)", pending, rem);
                    pending = rem;
                }
            }
            int final_pending = order_mgr.pending_cancelled_stop_count();
            if (final_pending > 0)
                LOG("[EXECUTOR] Phase-2 drain complete — %d stop(s) still unconfirmed "
                    "(exchange may fire these after shutdown — check RTrader for ghost positions)",
                    final_pending);
            else
                LOG("[EXECUTOR] Phase-2 drain complete — all cancelled stops confirmed or handled");
        }

        if (last_exch_net != INT_MIN && !order_mgr.net_qty_consistent(last_exch_net))
            LOG("[EXECUTOR] CRITICAL: shutting down with exchange net=%d not matching our state — "
                "the next start's snapshot will unwind it; check RTrader", last_exch_net);
        g_draining = false;
        LOG("[EXECUTOR] Shutdown complete");
        ioc_ref.stop();
    };

    // Run all coroutines concurrently
    // (In a real deployment we'd use parallel_group; for simplicity we spawn
    //  as separate tasks on the same io_context — C++20 coroutines co_spawn)
    asio::co_spawn(ex, signal_loop(),     asio::detached);
    asio::co_spawn(ex, heartbeat_loop(),  asio::detached);
    asio::co_spawn(ex, eod_loop(),        asio::detached);
    asio::co_spawn(ex, op_loop(),         asio::detached);
    asio::co_spawn(ex, pnl_loop(),        asio::detached);
    // legends_md_loop disabled — separate comparison feed; not needed for live trading.
    // asio::co_spawn(ex, legends_md_loop(), asio::detached);

    // Seed ORB range from today's live_sessions row (mid-session restart recovery).
    // Only applies if orb_set=true in the DB AND today's session matches — prevents
    // stale prior-day ranges from triggering false breakout signals on restart.
    if (db && db->is_connected()) {
        std::string orb_qdate = today_date_str();
        std::string q =
            "SELECT orb_high, orb_low FROM live_sessions "
            "WHERE account_label = '" + orb_cfg.account_label + "' "
            "  AND session_date = '" + orb_qdate + "' "
            "  AND instrument = '" + orb_cfg.symbol + "' "
            "  AND strategy = 'ORB' AND orb_high > orb_low LIMIT 1";
        PGresult* r = PQexec(db->raw_conn(), q.c_str());
        if (r && PQresultStatus(r) == PGRES_TUPLES_OK && PQntuples(r) == 1) {
            double sh = std::atof(PQgetvalue(r, 0, 0));
            double sl = std::atof(PQgetvalue(r, 0, 1));
            if (sh > sl && sh > 0.0) {
                if constexpr (kOrb) strategy.seed_orb_range(sh, sl);
                LOG("[EXECUTOR] ORB seeded high=%.2f low=%.2f — first real tick will anchor price, cross detection armed", sh, sl);
            }
        }
        if (r) PQclear(r);
    }

    // ── Persist cancelled stops across restarts ───────────────────────────────
    // Any stop cancel sent but not yet ACKed must survive restarts so that late
    // exchange fires are recognised and unwound rather than silently dropped.
    if (db && db->is_connected() && !orb_cfg.dry_run) {
        PGconn* pg = db->raw_conn();

        // Create table if not present (server_basket_id added for startup recancel via server ID)
        PQexec(pg,
            "CREATE TABLE IF NOT EXISTS pending_stop_cancels ("
            "  basket_id        TEXT PRIMARY KEY,"
            "  account_label    TEXT NOT NULL,"
            "  instrument       TEXT NOT NULL,"
            "  was_buy_stop     BOOL NOT NULL,"
            "  cancelled_at     TIMESTAMPTZ DEFAULT NOW(),"
            "  server_basket_id TEXT"
            ")");
        PQexec(pg,
            "ALTER TABLE pending_stop_cancels "
            "ADD COLUMN IF NOT EXISTS server_basket_id TEXT");

        // Prune rows older than 24 h — orders cannot survive across a full trading day;
        // if the cancel ACK never arrived by now the order is expired or already gone.
        PQexec(pg,
            ("DELETE FROM pending_stop_cancels "
             "WHERE account_label = '" + orb_cfg.account_label + "' "
             "  AND instrument = '" + orb_cfg.symbol + "' "
             "  AND cancelled_at < NOW() - INTERVAL '24 hours'").c_str());

        // Seed cancelled_stops_ (and server reverse map) from any rows left by a previous process
        {
            std::string q2 =
                "SELECT basket_id, was_buy_stop, server_basket_id FROM pending_stop_cancels "
                "WHERE account_label = '" + orb_cfg.account_label + "' "
                "  AND instrument = '" + orb_cfg.symbol + "'";
            PGresult* r2 = PQexec(pg, q2.c_str());
            if (r2 && PQresultStatus(r2) == PGRES_TUPLES_OK) {
                int n = PQntuples(r2);
                for (int i = 0; i < n; ++i) {
                    std::string bid    = PQgetvalue(r2, i, 0);
                    bool was_buy       = std::string(PQgetvalue(r2, i, 1)) == "t";
                    std::string svid   = PQgetisnull(r2, i, 2) ? "" : PQgetvalue(r2, i, 2);
                    order_mgr.seed_cancelled_stops(bid, was_buy);
                    if (!svid.empty()) order_mgr.seed_server_stop_cancel(svid, bid);
                }
                if (n > 0)
                    LOG("[EXECUTOR] STARTUP-RECON: loaded %d pending stop cancel(s) from DB", n);
            }
            if (r2) PQclear(r2);
        }

        // Wire up persist/remove/server-id callbacks so future cancels update the DB
        order_mgr.set_cancel_persist_callbacks(
            [pg, &orb_cfg](const std::string& bid, bool was_buy) {
                std::string q =
                    "INSERT INTO pending_stop_cancels "
                    "(basket_id, account_label, instrument, was_buy_stop) VALUES ('" +
                    bid + "','" + orb_cfg.account_label + "','" + orb_cfg.symbol + "'," +
                    (was_buy ? "TRUE" : "FALSE") + ") ON CONFLICT DO NOTHING";
                PQexec(pg, q.c_str());
                LOG("[DB] pending_stop_cancels INSERT basket=%s", bid.c_str());
            },
            [pg](const std::string& bid) {
                std::string q =
                    "DELETE FROM pending_stop_cancels WHERE basket_id = '" + bid + "'";
                PQexec(pg, q.c_str());
                LOG("[DB] pending_stop_cancels DELETE basket=%s", bid.c_str());
            },
            [pg](const std::string& client_id, const std::string& server_id) {
                std::string q =
                    "UPDATE pending_stop_cancels SET server_basket_id = '" + server_id +
                    "' WHERE basket_id = '" + client_id + "'";
                PQexec(pg, q.c_str());
                LOG("[DB] pending_stop_cancels UPDATE server_basket_id=%s for basket=%s",
                    server_id.c_str(), client_id.c_str());
            }
        );

        // Fire startup RECANCEL for trail-cancel orphans from the previous session.
        // DB seeds are now loaded + callbacks wired + order plant connected — safe to send.
        if (!orb_cfg.dry_run && order_mgr.pending_cancelled_stop_count() > 0) {
            LOG("[EXECUTOR] STARTUP-RECON: firing recancel for %d DB-seeded pending stop(s) "
                "(trail-cancel orphans from previous session)",
                order_mgr.pending_cancelled_stop_count());
            order_mgr.recancel_pending_stops();
            order_plant->flush_order_notifications();
        }
    }

#ifdef USE_RAPI_SDK
    // ── Native R|API+ TCP market-data feed ───────────────────────────────────
    // SdkMdFeed::TradePrint() posts ticks into this ASIO executor via
    // asio::post(), so all strategy/order_mgr calls remain single-threaded.
    {
        SdkConnParams sdk_conn = SdkConnParams::from_env();
        SdkMdFeed sdk_feed(orb_cfg, sdk_conn, ex, strategy, order_mgr);
        if (!sdk_feed.start()) {
            if (sdk_feed.auth_rejected()) {
                LOG("[EXECUTOR] SDK MD login rejected by server — stopping (check credentials/permissions)");
                g_running = false;
            } else {
                LOG("[EXECUTOR] SDK MD feed failed to start — halting");
            }
            ioc_ref.stop();
            co_return;
        }
        LOG("[SDK_MD] Native R|API+ TCP feed active — MD loop replaced");
        // Park here: SDK runs in its own thread; heartbeat/eod/op loops keep
        // the ASIO executor alive.  Stop when g_running is cleared.
        while (g_running) {
            asio::steady_timer t(ex);
            t.expires_after(std::chrono::seconds(1));
            co_await t.async_wait(asio::use_awaitable);
        }
        sdk_feed.stop();
    }
#else
    if (orb_cfg.md_from_pg()) co_await pg_feed_loop();
    else                      co_await md_loop();
#endif

    carried_pos = order_mgr.position_snapshot();  // preserve state across reconnects (#2)
    {
        const char* cps;
        switch (carried_pos.state) {
            case PosState::FLAT:          cps = "FLAT";          break;
            case PosState::PENDING_ENTRY: cps = "PENDING_ENTRY"; break;
            case PosState::LONG:          cps = "LONG";          break;
            case PosState::SHORT:         cps = "SHORT";         break;
            case PosState::PENDING_EXIT:  cps = "PENDING_EXIT";  break;
            default:                      cps = "?";             break;
        }
        LOG("[EXECUTOR] Main loop exited — carried_pos=%s entry=%.2f sl=%.2f",
            cps, carried_pos.entry_price, carried_pos.sl_price);
    }

    // Final audit flush before shutdown
    if (audit_conn) {
        try { audit_log.flush(); } catch (...) {}
        PQfinish(audit_conn);
        audit_conn = nullptr;
    }

    // Only stop the io_context for a normal reconnect cycle.
    // On signal shutdown (g_running=false), signal_loop owns the stop after Phase-2 drain.
    // Calling ioc_ref.stop() here on shutdown would race with and abort Phase-2.
    if (g_running)
        ioc_ref.stop();
}

// ─── Entry point ──────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    // SIGINT/SIGTERM are handled by asio::signal_set inside the session coroutine
    // once ORDER_PLANT is up (immediate flatten_now rather than waiting up to 1s
    // for the eod_loop tick). Outside that window — the connect/login phase and
    // the reconnect sleep between cycles — handle_signal just clears g_running so
    // the cycle loop exits after the current attempt. These used to be SIG_IGN,
    // which made `pkill -SIGTERM` a no-op for an executor stuck retrying a login.
    std::signal(SIGINT,  handle_signal);
    std::signal(SIGTERM, handle_signal);

    // ── Parse args ────────────────────────────────────────────────────────────
    std::string config_path = "config/orb_config.json";
    bool force_dry_run = false;
    // --check-config: load + validate the config (account/risk fields, engine-specific
    // checks) and exit 0 without connecting anywhere — used by scripts/rotate_handoff.sh
    // to prove a generated hand-off config before installing it.
    bool check_only = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc)
            config_path = argv[++i];
        else if (std::strcmp(argv[i], "--dry-run") == 0)
            force_dry_run = true;
        else if (std::strcmp(argv[i], "--check-config") == 0)
            check_only = true;
        else if (std::strcmp(argv[i], "--drill") == 0 && i + 1 < argc)
            g_drill = argv[++i];
    }
    if (!g_drill.empty() && g_drill != "orphan") {
        std::fprintf(stderr, "FATAL: unknown --drill '%s' (known: orphan)\n", g_drill.c_str());
        return 1;
    }

    LOG("[EXECUTOR] NQ ORB Execution Engine starting");
    LOG("[EXECUTOR] Config: %s", config_path.c_str());

    // ── Load ORB config ───────────────────────────────────────────────────────
    OrbConfig orb_cfg;
    try {
        orb_cfg = OrbConfig::from_file(config_path);
    } catch (std::exception& e) {
        std::fprintf(stderr, "FATAL: Cannot load config: %s\n", e.what());
        return 1;
    }
    if (force_dry_run) orb_cfg.dry_run = true;
    // Dry-run rows never land in the live history: the label must end in "_dry".
    if (orb_cfg.apply_dry_run_label() || orb_cfg.dry_label_forced)
        LOG("[EXECUTOR] WARN: dry_run=true but account_label did not end in \"_dry\" — "
            "writing under label '%s' so simulated fills stay out of the live tables",
            orb_cfg.account_label.c_str());

    // In cycle mode each cycle sets session_open = NOW(), so last_entry_hour
    // (designed for RTH "stop entering after 1 PM") would block every cycle
    // that starts at or after that hour. Override to 23 so entries are gated
    // only by eod_flatten_hour, not by this RTH-specific cutoff.
    if (orb_cfg.cycle_mode) orb_cfg.last_entry_hour = 23;

    LOG("[EXECUTOR] symbol=%s exchange=%s orb_min=%d sl=%.1fpts trail_step=%.1fpts",
        orb_cfg.symbol.c_str(), orb_cfg.exchange.c_str(),
        orb_cfg.orb_minutes, orb_cfg.sl_points, orb_cfg.trail_step);
    LOG("[EXECUTOR] max_daily_trades=%d last_entry=%02d:%02d ET dry_run=%s",
        orb_cfg.max_daily_trades, orb_cfg.last_entry_hour, orb_cfg.last_entry_min,
        orb_cfg.dry_run ? "true" : "false");
    LOG("[EXECUTOR] risk: trailing_dd_cap=$%.0f consistency_cap=%.0f%%",
        orb_cfg.trailing_drawdown_cap, orb_cfg.consistency_cap_pct * 100.0);

    // ── Validate order-plant credentials ─────────────────────────────────────
    if (orb_cfg.rithmic_user.empty()) {
        std::fprintf(stderr, "Config error: %s_USER not set\n", orb_cfg.order_env_prefix.c_str()); return 1;
    }
    if (orb_cfg.rithmic_password.empty()) {
        std::fprintf(stderr, "Config error: %s_PASSWORD not set\n", orb_cfg.order_env_prefix.c_str()); return 1;
    }

    // ── Validate config sanity ────────────────────────────────────────────────
    if (orb_cfg.sl_points <= 0) {
        std::fprintf(stderr, "FATAL: sl_points must be > 0\n");
        return 1;
    }
    if (orb_cfg.trailing_drawdown_cap <= 0) {
        std::fprintf(stderr, "FATAL: trailing_drawdown_cap must be > 0\n");
        return 1;
    }

    // ── Run ───────────────────────────────────────────────────────────────────
    // Hoist session components so they survive reconnects.
    RiskManager  risk(orb_cfg, orb_cfg.starting_balance);
    std::string  today;        // empty = first run; triggers reset_session/reset_daily
    Position     carried_pos;  // non-FLAT on reconnect → halt + warn (#2)

    // The session loop is identical for every engine; the strategy object is built once
    // here and survives reconnects exactly like risk/today/carried_pos.
    auto run_session_loop = [&](auto& strategy) -> int {
    int exit_code = 0;
    int cycle     = 0;
    while (g_running) {
        ++cycle;
        LOG("[EXECUTOR] ======== CYCLE %d START ========", cycle);
        try {
            asio::io_context ioc(1);
            asio::co_spawn(ioc,
                run_executor(orb_cfg, ioc, risk, strategy, today, carried_pos),
                [&](std::exception_ptr ep) {
                    if (ep) {
                        try { std::rethrow_exception(ep); }
                        catch (std::exception& e) {
                            LOG("[EXECUTOR] Coroutine exception: %s", e.what());
                        }
                        g_running = false;  // unhandled exception — stop
                    }
                    // Normal exit (MD disconnect) → outer while loop reconnects
                });
            ioc.run();
        } catch (std::exception& e) {
            LOG("[EXECUTOR] io_context exception: %s", e.what());
            exit_code = 1;
        }

        {
            const char* cs;
            switch (carried_pos.state) {
                case PosState::FLAT:          cs = "FLAT";          break;
                case PosState::PENDING_ENTRY: cs = "PENDING_ENTRY"; break;
                case PosState::LONG:          cs = "LONG";          break;
                case PosState::SHORT:         cs = "SHORT";         break;
                case PosState::PENDING_EXIT:  cs = "PENDING_EXIT";  break;
                default:                      cs = "?";             break;
            }
            LOG("[EXECUTOR] ======== CYCLE %d END — carried_pos=%s entry=%.2f "
                "g_running=%s ========",
                cycle, cs, carried_pos.entry_price,
                g_running.load() ? "true" : "false");
        }

        if (g_running) {
            const int delay_s = g_reconnect_delay_s.load();
            LOG("[EXECUTOR] Reconnecting in %ds...", delay_s);
            // The session's asio::signal_set restores the default disposition
            // when it is destroyed; re-arm our flag handler so SIGTERM during
            // the sleep stops the loop instead of killing the process outright.
            std::signal(SIGINT,  handle_signal);
            std::signal(SIGTERM, handle_signal);
            // Sleep in 1s slices so SIGTERM still stops us promptly during a
            // long post-refusal backoff.
            for (int i = 0; i < delay_s && g_running; ++i)
                std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return exit_code;
    };

    // Book entry gates (the paper fleet's overlay keys) are applied live only from the pg feed.
    if (paper::QuoteState{}.any_gate(orb_cfg) && !orb_cfg.md_from_pg()) {
        std::fprintf(stderr, "FATAL: book gates (spread_gate_*/imbalance_min/imbalance_max/microprice_lead) "
                             "need the collector's bbo stream — set RITHMIC_MD_PROVIDER=pg\n");
        return 1;
    }

    int exit_code = 0;
    if (orb_cfg.engine == "trend") {
        const TrendConfig tcfg = TrendConfig::from_json_string(read_text_file(config_path));
        // No ES reference feed live (rs_continuation). The book (bbo) is only on the pg feed.
        if (tcfg.mode == "rs_continuation") {
            std::fprintf(stderr, "FATAL: trend mode 'rs_continuation' needs the ES reference feed the live "
                                 "executor does not provide\n");
            return 1;
        }
        if (tcfg.mode == "book_imbalance" && !orb_cfg.md_from_pg()) {
            std::fprintf(stderr, "FATAL: trend mode 'book_imbalance' needs the collector's bbo stream — "
                                 "set RITHMIC_MD_PROVIDER=pg\n");
            return 1;
        }
        LOG("[EXECUTOR] engine=trend mode=%s tf=%dm window=%04d-%04d strategy_tag=%s",
            tcfg.mode.c_str(), tcfg.tf_min, tcfg.win_start, tcfg.win_end, orb_cfg.strategy.c_str());
        if (check_only) { LOG("[EXECUTOR] --check-config OK (engine=trend)"); return 0; }
        TrendStrategy strategy(tcfg, orb_cfg);
        exit_code = run_session_loop(strategy);
    } else if (orb_cfg.engine == "mtf_scalper") {
        const MtfScalperConfig mcfg = MtfScalperConfig::from_json_string(read_text_file(config_path));
        // The live executor feeds trades only — no ES/reference bars and no BBO quotes,
        // same constraint as trend's rs_continuation/book_imbalance above. trigger_mode
        // "smt" needs a reference feed to ever fire at all (it would just never enter);
        // use_smt_entry/use_im_filter degrade gracefully (logged, inert) so are not fatal.
        if (mcfg.trigger_mode == "smt") {
            std::fprintf(stderr, "FATAL: mtf_scalper trigger_mode 'smt' needs a reference feed "
                                  "the live executor does not provide — it would never enter\n");
            return 1;
        }
        // The order manager ignores a BUY/SELL while not FLAT, but the strategy would already
        // track the new leg and push ITS stop onto the old position (wrong side of the
        // market → forced exit → phantom leg for the session). No reversal support live.
        if (mcfg.allow_flips) {
            std::fprintf(stderr, "FATAL: mtf_scalper live needs \"allow_flips\": false in the config "
                                  "— the executor cannot reverse a position\n");
            return 1;
        }
        LOG("[EXECUTOR] engine=mtf_scalper trigger_mode=%s session=%s strategy_tag=%s",
            mcfg.trigger_mode.c_str(), mcfg.session_window.c_str(), orb_cfg.strategy.c_str());
        if (check_only) { LOG("[EXECUTOR] --check-config OK (engine=mtf_scalper)"); return 0; }
        MtfScalperStrategy strategy(mcfg);
        exit_code = run_session_loop(strategy);
    } else {
        if (check_only) { LOG("[EXECUTOR] --check-config OK (engine=orb)"); return 0; }
        OrbStrategy strategy(orb_cfg);
        exit_code = run_session_loop(strategy);
    }

    LOG("[EXECUTOR] Shutdown complete");
    if (g_exit_code != 0) return g_exit_code;   // drill verdict
    return exit_code;
}
