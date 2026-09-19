#include "client.hpp"
#include "log.hpp"

// Generated protobuf headers (built into cmake binary dir)
#include "rithmic.pb.h"

#include <boost/asio/experimental/awaitable_operators.hpp>
#include <boost/asio/experimental/parallel_group.hpp>  // used by connection test
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace asio_exp = boost::asio::experimental;
using namespace asio_exp::awaitable_operators;

// BestBidOffer presence_bits (proto/rithmic.proto:137)
constexpr int32_t kPresenceBid = 0x1;
constexpr int32_t kPresenceAsk = 0x2;

// ── Constructor ────────────────────────────────────────────────────

RithmicClient::RithmicClient(asio::io_context& ioc, const Config& cfg)
    : ioc_(ioc), ssl_ctx_(ssl::context::tls_client), cfg_(cfg)
{
    // Load Rithmic's custom CA certificate
    ssl_ctx_.load_verify_file(cfg_.cert_path);
    ssl_ctx_.set_verify_mode(ssl::verify_peer);
}

// ── connect_ws ─────────────────────────────────────────────────────

asio::awaitable<std::unique_ptr<RithmicClient::WsStream>>
RithmicClient::connect_ws() {
    // Parse "wss://host:port" from cfg_.url
    std::string url = cfg_.url;
    if (url.substr(0, 6) == "wss://") url = url.substr(6);
    std::string host, port;
    auto colon = url.find(':');
    if (colon != std::string::npos) {
        host = url.substr(0, colon);
        port = url.substr(colon + 1);
    } else {
        host = url;
        port = "443";
    }

    auto ex = co_await asio::this_coro::executor;

    // Resolve
    tcp::resolver resolver(ex);
    auto results = co_await resolver.async_resolve(host, port, use_awaitable);

    // Create stream
    auto ws = std::make_unique<WsStream>(ex, ssl_ctx_);

    // TCP connect
    co_await beast::get_lowest_layer(*ws).async_connect(results, use_awaitable);

    // TCP_NODELAY — disable Nagle's algorithm so small frames go out immediately
    // instead of being buffered for up to 40ms waiting for more data.
    // SO_RCVBUF/SO_SNDBUF — bump kernel socket buffers to absorb tick bursts.
    {
        auto& sock = beast::get_lowest_layer(*ws).socket();
        sock.set_option(asio::ip::tcp::no_delay(true));
        sock.set_option(asio::socket_base::receive_buffer_size(1 << 20));  // 1 MB
        sock.set_option(asio::socket_base::send_buffer_size(256 << 10));   // 256 KB
    }

    // SNI
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SSL_set_tlsext_host_name failed");

    // SSL handshake
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);

    // WS handshake
    ws->set_option(websocket::stream_base::decorator([&](websocket::request_type& req) {
        req.set(http::field::user_agent, "rithmic_engine/1.0");
    }));
    co_await ws->async_handshake(host + ":" + port, "/", use_awaitable);

    co_return ws;
}

// ── send helpers ───────────────────────────────────────────────────

static asio::awaitable<void> ws_write(RithmicClient::WsStream& ws,
                                      const std::string& data) {
    ws.binary(true);
    co_await ws.async_write(asio::buffer(data), use_awaitable);
}

// ── read_with_timeout ──────────────────────────────────────────────

asio::awaitable<std::string>
RithmicClient::read_with_timeout(WsStream& ws, int timeout_s,
                                 const char* waiting_for) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer deadline(ex);
    deadline.expires_after(std::chrono::seconds(timeout_s));

    beast::flat_buffer buf;
    auto [order, rd_ec, rd_n, tm_ec] =
        co_await asio_exp::make_parallel_group(
            ws.async_read(buf, asio::deferred),
            deadline.async_wait(asio::deferred)
        ).async_wait(asio_exp::wait_for_one(), use_awaitable);

    if (order[0] == 1)
        throw std::runtime_error(
            std::string("Rithmic: timed out (") + std::to_string(timeout_s) +
            "s) waiting for " + waiting_for);
    if (rd_ec) throw beast::system_error(rd_ec);

    co_return strip_header(beast::buffers_to_string(buf.data()));
}

// ── get_system_info ────────────────────────────────────────────────

asio::awaitable<void> RithmicClient::get_system_info(WsStream& ws) {
    rti::RequestRithmicSystemInfo req;
    req.set_template_id(16);
    co_await ws_write(ws, frame(req));

    // Wait for response with template_id == 17 (15s timeout per read —
    // a server that accepts the WS handshake but never replies must not
    // hang us forever)
    for (;;) {
        auto payload = co_await read_with_timeout(ws, 15,
                                                  "ResponseRithmicSystemInfo (17)");

        rti::Base base;
        base.ParseFromString(payload);
        if (base.template_id() != 17) continue;

        rti::ResponseRithmicSystemInfo resp;
        resp.ParseFromString(payload);

        bool found = false;
        for (auto& sn : resp.system_name())
            if (sn == cfg_.system_name) { found = true; break; }

        if (!found) {
            std::string avail;
            for (auto& sn : resp.system_name()) avail += sn + " ";
            throw std::runtime_error(
                "System name '" + cfg_.system_name +
                "' not found. Available: " + avail);
        }
        co_return;
    }
}

// ── login ──────────────────────────────────────────────────────────

asio::awaitable<void> RithmicClient::login(WsStream& ws) {
    rti::RequestLogin req;
    req.set_template_id(10);
    req.set_template_version("3.9");
    req.set_user(cfg_.user);
    req.set_password(cfg_.password);
    req.set_system_name(cfg_.system_name);
    req.set_app_name(cfg_.app_name);
    req.set_app_version(cfg_.app_version);
    req.set_infra_type(rti::RequestLogin::TICKER_PLANT);
    co_await ws_write(ws, frame(req));

    // Wait for response with template_id == 11 (15s timeout per read)
    for (;;) {
        auto payload = co_await read_with_timeout(ws, 15, "ResponseLogin (11)");

        rti::Base base;
        base.ParseFromString(payload);
        if (base.template_id() != 11) continue;

        rti::ResponseLogin resp;
        resp.ParseFromString(payload);

        if (!resp.rp_code().empty() && resp.rp_code(0) != "0")
            throw LoginError(resp.rp_code(0));

        if (resp.heartbeat_interval() > 0)
            heartbeat_interval_ = resp.heartbeat_interval();

        LOG("Login OK — heartbeat interval: %.0fs", heartbeat_interval_);
        co_return;
    }
}

// ── send_heartbeat ─────────────────────────────────────────────────

asio::awaitable<void> RithmicClient::send_heartbeat(WsStream& ws) {
    using namespace std::chrono;
    auto now  = system_clock::now().time_since_epoch();
    auto secs = duration_cast<seconds>(now).count();
    auto usec = duration_cast<microseconds>(now).count() % 1'000'000;

    rti::RequestHeartbeat req;
    req.set_template_id(18);
    req.set_ssboe(static_cast<int32_t>(secs));
    req.set_usecs(static_cast<int32_t>(usec));
    co_await ws_write(ws, frame(req));
}

// ── subscribe / unsubscribe ────────────────────────────────────────

asio::awaitable<void> RithmicClient::subscribe(WsStream& ws,
                                                const std::string& symbol,
                                                const std::string& exchange) {
    rti::RequestMarketDataUpdate req;
    req.set_template_id(100);
    req.set_symbol(symbol);
    req.set_exchange(exchange);
    req.set_request(rti::RequestMarketDataUpdate::SUBSCRIBE);
    req.set_update_bits(1 | 2);  // LAST_TRADE | BBO
    co_await ws_write(ws, frame(req));
    LOG("Subscribed to %s/%s (LAST_TRADE|BBO)", symbol.c_str(), exchange.c_str());
}

asio::awaitable<void> RithmicClient::subscribe_depth(WsStream& ws,
                                                      const std::string& symbol,
                                                      const std::string& exchange) {
    rti::RequestMarketDataUpdate req;
    req.set_template_id(100);
    req.set_symbol(symbol);
    req.set_exchange(exchange);
    req.set_request(rti::RequestMarketDataUpdate::SUBSCRIBE);
    req.set_update_bits(64);  // DEPTH_BY_ORDER
    co_await ws_write(ws, frame(req));
    LOG("Subscribed depth-by-order for %s/%s", symbol.c_str(), exchange.c_str());
}

asio::awaitable<void> RithmicClient::unsubscribe(WsStream& ws,
                                                   const std::string& symbol,
                                                   const std::string& exchange) {
    rti::RequestMarketDataUpdate req;
    req.set_template_id(100);
    req.set_symbol(symbol);
    req.set_exchange(exchange);
    req.set_request(rti::RequestMarketDataUpdate::UNSUBSCRIBE);
    req.set_update_bits(1);
    co_await ws_write(ws, frame(req));
}

asio::awaitable<void> RithmicClient::send_logout(WsStream& ws) {
    rti::RequestLogout req;
    req.set_template_id(12);
    co_await ws_write(ws, frame(req));
}

// ── ConnectionTestResult::print ────────────────────────────────────

void ConnectionTestResult::print() const {
    for (auto& s : steps) {
        if (s.ms >= 0)
            std::printf("  %s  %-44s  %5lld ms  %s\n",
                s.ok ? "✓" : "✗", s.name.c_str(),
                (long long)s.ms, s.detail.c_str());
        else
            std::printf("  %s  %-44s  (no time)  %s\n",
                s.ok ? "✓" : "✗", s.name.c_str(), s.detail.c_str());
    }
    if (first_price > 0) {
        std::printf("\n");
        std::printf("  First tick  price=%.2f  qty=%lld  %s\n",
            first_price, (long long)first_size, first_is_buy ? "BUY" : "SELL");
        if (wire_latency_us > 0)
            std::printf("  Wire latency  %lld µs  (%.1f ms)\n",
                (long long)wire_latency_us, wire_latency_us / 1000.0);
        if (db_total_ticks > 0)
            std::printf("  DB total rows  %lld\n", (long long)db_total_ticks);
    }
}

// ── run_connection_test ────────────────────────────────────────────

asio::awaitable<ConnectionTestResult>
RithmicClient::run_connection_test(TickDB& db, int n_ticks) {
    ConnectionTestResult result;
    using clock = std::chrono::steady_clock;

    auto ms_since = [](clock::time_point t0) -> int64_t {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now() - t0).count();
    };
    auto push = [&](std::string name, int64_t ms, bool ok, std::string detail = "") {
        result.steps.push_back({std::move(name), ms, ok, std::move(detail)});
    };

    // ── Step 1: TCP + SSL + WS + system info probe ─────────────────
    clock::time_point t = clock::now();
    try {
        auto probe = co_await connect_ws();
        push("TCP + SSL + WebSocket connect", ms_since(t), true, cfg_.url);
        t = clock::now();
        co_await get_system_info(*probe);
        push("RequestRithmicSystemInfo (16→17)", ms_since(t), true,
             "system=" + cfg_.system_name);
        // Await the close — a detached close would run its completion
        // handler against a destroyed stream once probe leaves scope.
        beast::get_lowest_layer(*probe).expires_after(std::chrono::seconds(10));
        boost::system::error_code close_ec;
        co_await probe->async_close(websocket::close_code::normal,
                                    asio::redirect_error(use_awaitable, close_ec));
    } catch (std::exception& e) {
        push("Connect / SystemInfo", ms_since(t), false, e.what());
        co_return result;
    }

    // ── Step 2: Login ──────────────────────────────────────────────
    std::unique_ptr<WsStream> ws;
    t = clock::now();
    try {
        ws = co_await connect_ws();
        co_await login(*ws);
        push("RequestLogin (10→11)", ms_since(t), true,
             "hb=" + std::to_string(static_cast<int>(heartbeat_interval_)) + "s");
    } catch (std::exception& e) {
        push("RequestLogin (10→11)", ms_since(t), false, e.what());
        co_return result;
    }

    // ── Step 3: Subscribe ──────────────────────────────────────────
    t = clock::now();
    try {
        co_await subscribe(*ws, cfg_.symbol, cfg_.exchange);
        push("RequestMarketDataUpdate (100→101)", ms_since(t), true,
             cfg_.symbol + "/" + cfg_.exchange);
    } catch (std::exception& e) {
        push("Subscribe", ms_since(t), false, e.what());
        co_return result;
    }

    // ── Step 4: Receive N ticks (60s timeout) ─────────────────────
    std::vector<TickRow> ticks;
    t = clock::now();
    {
        auto ex = co_await asio::this_coro::executor;
        asio::steady_timer deadline(ex);
        deadline.expires_after(std::chrono::seconds(60));
        bool timed_out  = false;
        bool read_error = false;
        std::string read_errmsg;

        while (static_cast<int>(ticks.size()) < n_ticks && !timed_out && !read_error) {
            beast::flat_buffer buf;
            auto [order, rd_ec, rd_n, tm_ec] =
                co_await asio_exp::make_parallel_group(
                    ws->async_read(buf, asio::deferred),
                    deadline.async_wait(asio::deferred)
                ).async_wait(asio_exp::wait_for_one(), use_awaitable);

            if (order[0] == 1) { timed_out = true; break; }
            if (rd_ec)         { read_error = true; read_errmsg = rd_ec.message(); break; }

            auto payload = strip_header(beast::buffers_to_string(buf.data()));
            rti::Base base; base.ParseFromString(payload);
            if (base.template_id() != 150) continue;

            rti::LastTrade lt; lt.ParseFromString(payload);
            if (lt.trade_price() <= 0 || lt.trade_size() <= 0) continue;

            int64_t ts_us  = static_cast<int64_t>(lt.ssboe()) * 1'000'000LL + lt.usecs();
            bool    is_buy = (lt.aggressor() == rti::LastTrade::BUY);

            std::string sym  = lt.symbol().empty()  ? cfg_.symbol   : lt.symbol();
            std::string exch = lt.exchange().empty() ? cfg_.exchange : lt.exchange();

            if (ticks.empty()) {
                result.first_price   = lt.trade_price();
                result.first_size    = lt.trade_size();
                result.first_is_buy  = is_buy;
                auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                result.wire_latency_us = now_us - ts_us;
            }
            ticks.push_back({ts_us, lt.trade_price(), lt.trade_size(),
                             is_buy, sym, exch});
        }

        int got = static_cast<int>(ticks.size());
        if (read_error) {
            push("Receive ticks", ms_since(t), false, read_errmsg);
        } else if (timed_out && got == 0) {
            push("Receive ticks (60s timeout)", ms_since(t), false,
                 "no ticks received — market may be closed");
        } else {
            std::string det = "first price=" + std::to_string(result.first_price);
            if (result.wire_latency_us > 0)
                det += " wire=" + std::to_string(result.wire_latency_us / 1000) + "ms";
            push("Receive " + std::to_string(got) + "/" + std::to_string(n_ticks) + " ticks",
                 ms_since(t), true, det);
        }
    }

    // ── Step 5: DB write ───────────────────────────────────────────
    if (!ticks.empty()) {
        t = clock::now();
        try {
            int written = db.write(ticks);
            push("DB write — UNNEST batch INSERT", ms_since(t), true,
                 std::to_string(written) + "/" + std::to_string(ticks.size()) + " new rows");
        } catch (std::exception& e) {
            push("DB write", ms_since(t), false, e.what());
        }
    }

    // ── Step 6: DB read-back ───────────────────────────────────────
    t = clock::now();
    try {
        result.db_total_ticks = db.row_count();
        push("DB read — COUNT(*) ticks", ms_since(t), true,
             std::to_string(result.db_total_ticks) + " total rows");
    } catch (std::exception& e) {
        push("DB read", ms_since(t), false, e.what());
    }

    // ── Clean shutdown ─────────────────────────────────────────────
    try {
        co_await unsubscribe(*ws, cfg_.symbol, cfg_.exchange);
        co_await send_logout(*ws);
        // Await the close so the handler never outlives the stream
        beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(10));
        boost::system::error_code close_ec;
        co_await ws->async_close(websocket::close_code::normal,
                                 asio::redirect_error(use_awaitable, close_ec));
    } catch (...) {}

    co_return result;
}

// ── dispatch_message ───────────────────────────────────────────────

void RithmicClient::dispatch_message(const std::string& payload) {
    rti::Base base;
    if (!base.ParseFromString(payload)) {
        LOG("WARN: dropping malformed frame (%zu bytes, header unparseable)",
            payload.size());
        return;
    }

    if (base.template_id() == 150) {
        rti::LastTrade lt;
        if (!lt.ParseFromString(payload)) {
            LOG("WARN: dropping malformed LastTrade (template 150)");
            return;
        }

        if (lt.trade_price() <= 0 || lt.trade_size() <= 0) return;

        using namespace std::chrono;
        int64_t ts_micros =
            static_cast<int64_t>(lt.ssboe()) * 1'000'000LL +
            static_cast<int64_t>(lt.usecs());

        bool is_buy = (lt.aggressor() == rti::LastTrade::BUY);

        // Use symbol/exchange from message if present, fall back to config
        std::string sym  = lt.symbol().empty()   ? cfg_.symbol   : lt.symbol();
        std::string exch = lt.exchange().empty()  ? cfg_.exchange : lt.exchange();

        if (on_tick_)
            on_tick_(TickRow{ts_micros, lt.trade_price(),
                             lt.trade_size(), is_buy,
                             std::move(sym), std::move(exch)});

    } else if (base.template_id() == 151) {
        rti::BestBidOffer bbo;
        if (!bbo.ParseFromString(payload)) {
            LOG("WARN: dropping malformed BestBidOffer (template 151)");
            return;
        }

        // presence_bits marks which sides carry real values; absent sides
        // arrive as protobuf-default 0. Deliver them as nullopt so the db
        // layer writes NULL instead of a bogus 0.0 price. One-sided updates
        // (bid-only or ask-only on partial fills) are accepted.
        std::optional<double> bid_price, ask_price;
        if ((bbo.presence_bits() & kPresenceBid) && bbo.bid_price() > 0)
            bid_price = bbo.bid_price();
        if ((bbo.presence_bits() & kPresenceAsk) && bbo.ask_price() > 0)
            ask_price = bbo.ask_price();
        if (!bid_price && !ask_price) return;

        int64_t ts_us = static_cast<int64_t>(bbo.ssboe()) * 1'000'000LL +
                        static_cast<int64_t>(bbo.usecs());

        std::string sym  = bbo.symbol().empty()   ? cfg_.symbol   : bbo.symbol();
        std::string exch = bbo.exchange().empty()  ? cfg_.exchange : bbo.exchange();

        if (on_bbo_)
            on_bbo_(BBORow{ts_us,
                           bid_price, bbo.bid_size(), bbo.bid_orders(),
                           ask_price, bbo.ask_size(), bbo.ask_orders(),
                           std::move(sym), std::move(exch)});

    } else if (base.template_id() == 160) {
        rti::DepthByOrder dbo;
        if (!dbo.ParseFromString(payload)) {
            LOG("WARN: dropping malformed DepthByOrder (template 160)");
            return;
        }

        if (dbo.depth_price() <= 0) return;

        // prev_depth_price is only meaningful when prev_depth_price_flag is
        // set; otherwise deliver nullopt so the db layer writes NULL.
        std::optional<double> prev_price;
        if (dbo.prev_depth_price_flag())
            prev_price = dbo.prev_depth_price();

        int64_t ts_us  = static_cast<int64_t>(dbo.ssboe()) * 1'000'000LL +
                         static_cast<int64_t>(dbo.usecs());
        int64_t src_ns = static_cast<int64_t>(dbo.source_ssboe()) * 1'000'000'000LL +
                         static_cast<int64_t>(dbo.source_nsecs());

        if (on_depth_)
            on_depth_(DepthRow{ts_us, src_ns, dbo.sequence_number(),
                               static_cast<int8_t>(dbo.update_type()),
                               static_cast<int8_t>(dbo.transaction_type()),
                               dbo.depth_price(), prev_price,
                               dbo.depth_size(), dbo.exchange_order_id(),
                               dbo.symbol().empty()   ? cfg_.symbol   : dbo.symbol(),
                               dbo.exchange().empty()  ? cfg_.exchange : dbo.exchange()});
    } else if (base.template_id() == 18) {
        // Inbound RequestHeartbeat (18) — Rithmic keepalive ping. The client
        // replies with its own RequestHeartbeat (18); ResponseHeartbeat (19)
        // is the SERVER's reply to OUR heartbeat, never sent by us.
        hb_response_pending_.store(true);
    }
    // template_id 19 = ResponseHeartbeat — server's ack of our RequestHeartbeat(18); ignored
    // template_id 101 = ResponseMarketDataUpdate — silently ignored
}

// ── receive_loop / link_watchdog ───────────────────────────────────
//
// The read is NEVER cancelled locally. Cancelling a Beast websocket read
// (parallel_group wait_for_one, or tcp_stream expires_after firing under
// it) poisons the stream — the next op fails with "Operation canceled
// [system:125] check_stop_now" — which used to sever the connection every
// heartbeat interval whenever the market went quiet (e.g. overnight and
// weekends), and the resulting rapid login loop tripped Rithmic's login
// rate limit (rp_code 13) and killed the collector for good.
//
// Instead, a sibling coroutine (link_watchdog) owns two duties on a 5s
// cadence while receive_loop runs:
//   1. Heartbeats — replies to server requests and proactive pings, which
//      must flow even when zero reads complete (pure silence). Rithmic acks
//      heartbeats, and those acks count as inbound activity.
//   2. Silence kill — if NOTHING has arrived for 2.5× the negotiated
//      heartbeat interval despite our pings, the connection is truly dead:
//      the watchdog closes the socket, failing the read and triggering the
//      outer reconnect loop (flagged via silence_killed_).

static int64_t mono_now_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

asio::awaitable<void> RithmicClient::receive_loop(WsStream& ws) {
    last_rx_mono_s_.store(mono_now_s());

    while (running_) {
        beast::flat_buffer buf;
        boost::system::error_code ec;

        co_await ws.async_read(buf, asio::redirect_error(use_awaitable, ec));

        // Never throw out of here: the || operator is wait_for_one_success, so
        // a thrown error would NOT complete the group — the watchdog would run
        // to its silence limit first, delaying every routine disconnect by
        // minutes and discarding the real error. Record + return instead;
        // run() rethrows after the race completes.
        if (ec) {
            link_error_ = ec.message();
            co_return;
        }

        last_rx_mono_s_.store(mono_now_s());

        try {
            dispatch_message(strip_header(beast::buffers_to_string(buf.data())));
        } catch (std::exception& e) {
            link_error_ = std::string("frame dispatch: ") + e.what();
            co_return;
        }

        // Heartbeat replies AND proactive heartbeats are sent by the sibling
        // link_watchdog coroutine: heartbeat sends must keep flowing during
        // total silence, when this loop is parked inside async_read. The
        // watchdog is the only heartbeat writer while both run, so no
        // concurrent async_write can race another (Beast full-duplex allows
        // exactly one outstanding read + one outstanding write).
    }
}

asio::awaitable<void> RithmicClient::link_watchdog(WsStream& ws) {
    auto ex = co_await asio::this_coro::executor;
    asio::steady_timer t(ex);
    const int64_t limit_s = std::max<int64_t>(
        30, static_cast<int64_t>(heartbeat_interval_ * 2.5));
    const double hb_every_s = heartbeat_interval_ * 0.9;
    auto last_hb = std::chrono::steady_clock::now();

    while (running_) {
        t.expires_after(std::chrono::seconds(5));
        boost::system::error_code ec;
        co_await t.async_wait(asio::redirect_error(use_awaitable, ec));
        if (ec) co_return;   // cancelled — sibling receive_loop finished

        // Heartbeat duty: reply to server requests (template 18, flagged by
        // dispatch_message) and proactively ping on schedule. Without this the
        // client goes fully quiet during market silence and the server treats
        // the line as dead — observed as zero inbound data for hours on a
        // weekend, which then tripped the silence kill below on every session.
        auto now = std::chrono::steady_clock::now();
        double since_hb = std::chrono::duration<double>(now - last_hb).count();
        if (hb_response_pending_.exchange(false) || since_hb >= hb_every_s) {
            try {
                co_await send_heartbeat(ws);
            } catch (std::exception& e) {
                // Write failed — the line is dead; close so the pending read
                // fails too and the race completes with the real cause.
                link_error_ = std::string("heartbeat write: ") + e.what();
                boost::system::error_code cec;
                beast::get_lowest_layer(ws).socket().close(cec);
                co_return;
            }
            last_hb = now;
        }

        int64_t silence = mono_now_s() - last_rx_mono_s_.load();
        if (silence > limit_s) {
            LOG("No inbound data for %llds (limit %llds) — connection dead, forcing reconnect",
                static_cast<long long>(silence), static_cast<long long>(limit_s));
            silence_killed_.store(true);
            beast::get_lowest_layer(ws).socket().close(ec);
            co_return;
        }
    }
}

// ── run (main loop) ────────────────────────────────────────────────

asio::awaitable<void> RithmicClient::run() {
    int attempt = 0;
    auto ex = co_await asio::this_coro::executor;

    while (running_) {
        int  delay_s   = 0;
        bool had_error = false;
        std::chrono::steady_clock::time_point connected_at{};

        try {
            // ── Step 1: system info probe ──────────────────────────
            {
                auto ws = co_await connect_ws();
                co_await get_system_info(*ws);
                // Await the close so its handler never outlives the stream
                beast::get_lowest_layer(*ws).expires_after(
                    std::chrono::seconds(10));
                boost::system::error_code close_ec;
                co_await ws->async_close(websocket::close_code::normal,
                    asio::redirect_error(use_awaitable, close_ec));
            }

            // ── Step 2: real session ───────────────────────────────
            {
                auto ws = co_await connect_ws();
                co_await login(*ws);
                connected_at = std::chrono::steady_clock::now();
                co_await send_heartbeat(*ws);

                std::string contract = cfg_.symbol;
                LOG("Connected — streaming %s on %s",
                    contract.c_str(), cfg_.exchange.c_str());

                co_await subscribe(*ws, contract, cfg_.exchange);
                co_await subscribe_depth(*ws, contract, cfg_.exchange);
                silence_killed_.store(false);
                link_error_.clear();
                co_await (receive_loop(*ws) || link_watchdog(*ws));
                if (!link_error_.empty()) {
                    std::string msg;
                    std::swap(msg, link_error_);
                    throw std::runtime_error(msg);
                }
                if (silence_killed_.load())
                    throw std::runtime_error("no inbound data — connection dead");

                // Clean shutdown
                co_await unsubscribe(*ws, contract, cfg_.exchange);
                co_await send_logout(*ws);
                beast::get_lowest_layer(*ws).expires_after(
                    std::chrono::seconds(10));
                boost::system::error_code close_ec;
                co_await ws->async_close(websocket::close_code::normal,
                    asio::redirect_error(use_awaitable, close_ec));
            }

            attempt = 0;

        } catch (LoginError& e) {
            // rp_code 13 = too many rapid logins / duplicate session —
            // transient. Retry on a long, fixed cadence instead of dying;
            // a collector that exits here collects nothing until someone
            // restarts it. Genuine auth failures (bad credentials) stay
            // terminal so misconfiguration surfaces immediately.
            if (e.code != "13") throw;
            LOG("Login transiently refused (rp_code %s) — retrying in 300s",
                e.code.c_str());
            delay_s   = 300;
            had_error = true;
        } catch (std::exception& e) {
            // co_await is not allowed inside catch — record and act after
            if (!running_) break;
            // A session that stayed up >= 60s was healthy — decay the
            // backoff so a flap loop doesn't pin the delay at 300s.
            if (connected_at != std::chrono::steady_clock::time_point{} &&
                std::chrono::steady_clock::now() - connected_at >=
                    std::chrono::seconds(60))
                attempt = 0;
            delay_s = std::min(30 * (1 << std::min(attempt, 4)), 300);
            LOG("Disconnected: %s — reconnecting in %ds", e.what(), delay_s);
            ++attempt;
            had_error = true;
        }

        if (had_error && running_) {
            asio::steady_timer t(ex);
            t.expires_after(std::chrono::seconds(delay_s));
            co_await t.async_wait(use_awaitable);
        }
    }
}
