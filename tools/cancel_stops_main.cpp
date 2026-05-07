/*  cancel_stops_main.cpp
    Reads every row from pending_stop_cancels and sends RequestCancelOrder
    for each basket.  Safe to run alongside a live nq_executor — ORDER_PLANT
    supports multiple concurrent sessions.

    Usage (on Oracle or locally):
        ./cancel_stops [--account-label LABEL]

    Credentials are read from environment (same vars as nq_executor):
        RITHMIC_LEGENDS_USER / _PASSWORD / _SYSTEM / _URL
        RITHMIC_LEGENDS_ACCOUNT  (used as fallback account_id)
        PG_HOST / PG_PORT / PG_DB / PG_USER / PG_PASSWORD

    The tool exits after sending all cancels (no fill loop — we just need
    to push the cancel requests into the exchange queue).
*/

#include "rithmic.pb.h"
#include "../src/execution/log.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <libpq-fe.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace asio      = boost::asio;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
namespace ssl       = asio::ssl;
using tcp           = asio::ip::tcp;

// ─── helpers ─────────────────────────────────────────────────────────────────

static std::string env(const char* key, const char* fallback = "") {
    const char* v = std::getenv(key);
    return v ? v : fallback;
}

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

using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

// ─── main ────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    std::string account_label_filter;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--account-label") == 0 && i + 1 < argc)
            account_label_filter = argv[++i];
    }

    // ── Credentials ──────────────────────────────────────────────────────────
    std::string op_user    = env("RITHMIC_LEGENDS_USER");
    std::string op_pass    = env("RITHMIC_LEGENDS_PASSWORD");
    std::string op_system  = env("RITHMIC_LEGENDS_SYSTEM", "LegendsTrading");
    std::string op_url     = env("RITHMIC_LEGENDS_URL");
    std::string account_id = env("RITHMIC_LEGENDS_ACCOUNT");
    std::string app_name   = env("RITHMIC_APP_NAME", "cancel_stops");
    std::string app_ver    = env("RITHMIC_APP_VERSION", "1.0");
    // fcm_id and ib_id are static for Legends — same as orb_config.json
    std::string fcm_id     = env("RITHMIC_FCM_ID",  "LegendsTrading");
    std::string ib_id      = env("RITHMIC_IB_ID",   "LegendsTrading");

    if (op_user.empty() || op_url.empty()) {
        std::fprintf(stderr, "FATAL: RITHMIC_LEGENDS_USER / _URL not set\n");
        return 1;
    }

    // ── Read basket IDs from DB ───────────────────────────────────────────────
    std::string pg_connstr =
        "host="     + env("PG_HOST", "127.0.0.1") +
        " port="    + env("PG_PORT", "5432") +
        " dbname="  + env("PG_DB",   "rithmic") +
        " user="    + env("PG_USER", "rithmic_user") +
        " password="+ env("PG_PASSWORD", "");

    PGconn* pg = PQconnectdb(pg_connstr.c_str());
    if (!pg || PQstatus(pg) != CONNECTION_OK) {
        std::fprintf(stderr, "FATAL: DB connect failed: %s\n",
                     pg ? PQerrorMessage(pg) : "null");
        if (pg) PQfinish(pg);
        return 1;
    }

    std::string q = "SELECT basket_id, account_label, instrument, was_buy_stop "
                    "FROM pending_stop_cancels";
    if (!account_label_filter.empty())
        q += " WHERE account_label = '" + account_label_filter + "'";
    q += " ORDER BY cancelled_at";

    PGresult* res = PQexec(pg, q.c_str());
    if (!res || PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::fprintf(stderr, "FATAL: DB query failed: %s\n", PQerrorMessage(pg));
        PQfinish(pg);
        return 1;
    }

    struct StopRow { std::string basket_id, account_label, instrument; bool was_buy; };
    std::vector<StopRow> stops;
    for (int i = 0; i < PQntuples(res); ++i) {
        stops.push_back({
            PQgetvalue(res, i, 0),
            PQgetvalue(res, i, 1),
            PQgetvalue(res, i, 2),
            std::string(PQgetvalue(res, i, 3)) == "t"
        });
    }
    PQclear(res);
    PQfinish(pg);

    if (stops.empty()) {
        LOG("[CANCEL_STOPS] No pending stops in DB — nothing to do");
        return 0;
    }
    LOG("[CANCEL_STOPS] Found %zu pending stop(s) to cancel", stops.size());
    for (const auto& s : stops)
        LOG("[CANCEL_STOPS]   basket=%s label=%s sym=%s dir=%s",
            s.basket_id.c_str(), s.account_label.c_str(), s.instrument.c_str(),
            s.was_buy ? "BUY-stop(SHORT)" : "SELL-stop(LONG)");

    // ── Parse ORDER_PLANT URL ─────────────────────────────────────────────────
    std::string host, port;
    {
        std::string u = op_url;
        if (u.substr(0, 6) == "wss://") u = u.substr(6);
        auto colon = u.find(':');
        if (colon != std::string::npos) { host = u.substr(0, colon); port = u.substr(colon + 1); }
        else                            { host = u; port = "443"; }
    }

    // ── Boost.Asio / Beast setup ──────────────────────────────────────────────
    asio::io_context ioc(1);
    ssl::context ssl_ctx(ssl::context::tls_client);
    ssl_ctx.load_verify_file("certs/rithmic_ssl_cert_auth_params");
    ssl_ctx.set_verify_mode(ssl::verify_peer);

    tcp::resolver resolver(ioc);
    auto results = resolver.resolve(host, port);

    auto ws = std::make_unique<WsStream>(ioc, ssl_ctx);
    beast::get_lowest_layer(*ws).connect(results);
    beast::get_lowest_layer(*ws).socket().set_option(asio::ip::tcp::no_delay(true));
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
        std::fprintf(stderr, "FATAL: SSL SNI\n"); return 1;
    }
    ws->next_layer().handshake(ssl::stream_base::client);
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req){
        req.set(beast::http::field::user_agent, "cancel_stops/1.0");
    }));
    ws->handshake(host + ":" + port, "/");
    ws->binary(true);
    LOG("[CANCEL_STOPS] WS connected to %s:%s", host.c_str(), port.c_str());

    auto ws_write = [&](const std::string& data) {
        ws->write(asio::buffer(data));
    };
    auto ws_read = [&]() -> std::string {
        beast::flat_buffer buf;
        ws->read(buf);
        return proto_strip(beast::buffers_to_string(buf.data()));
    };

    // ── System info probe ─────────────────────────────────────────────────────
    {
        rti::RequestRithmicSystemInfo req;
        req.set_template_id(16);
        ws_write(proto_frame(req));
        for (;;) {
            auto pl = ws_read();
            rti::Base b; if (!b.ParseFromString(pl)) continue;
            if (b.template_id() == 17) {
                rti::ResponseRithmicSystemInfo info; info.ParseFromString(pl);
                std::string avail;
                for (auto& s : info.system_name()) avail += "[" + s + "] ";
                LOG("[CANCEL_STOPS] Available systems: %s", avail.c_str());
                break;
            }
        }
    }

    // ── Reconnect for login ───────────────────────────────────────────────────
    try { ws->close(websocket::close_code::normal); } catch (...) {}
    ws = std::make_unique<WsStream>(ioc, ssl_ctx);
    beast::get_lowest_layer(*ws).connect(results);
    beast::get_lowest_layer(*ws).socket().set_option(asio::ip::tcp::no_delay(true));
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        { std::fprintf(stderr, "FATAL: SSL SNI\n"); return 1; }
    ws->next_layer().handshake(ssl::stream_base::client);
    ws->set_option(websocket::stream_base::decorator([](websocket::request_type& req){
        req.set(beast::http::field::user_agent, "cancel_stops/1.0");
    }));
    ws->handshake(host + ":" + port, "/");
    ws->binary(true);

    // ── Login ─────────────────────────────────────────────────────────────────
    {
        rti::RequestLogin req;
        req.set_template_id(10);
        req.set_template_version("3.9");
        req.set_user(op_user);
        req.set_password(op_pass);
        req.set_system_name(op_system);
        req.set_app_name(app_name);
        req.set_app_version(app_ver);
        req.set_infra_type(rti::RequestLogin::ORDER_PLANT);
        ws_write(proto_frame(req));

        beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(15));
        for (;;) {
            auto pl = ws_read();
            rti::Base b; if (!b.ParseFromString(pl)) continue;
            if (b.template_id() == 11) {
                rti::ResponseLogin resp; resp.ParseFromString(pl);
                bool ok = !resp.rp_code().empty() && resp.rp_code(0) == "0";
                if (!ok) {
                    std::fprintf(stderr, "FATAL: ORDER_PLANT login failed rp_code=%s\n",
                                 resp.rp_code().empty() ? "?" : resp.rp_code(0).c_str());
                    return 1;
                }
                LOG("[CANCEL_STOPS] ORDER_PLANT login OK uid=%s (fcm=%s ib=%s)",
                    resp.unique_user_id().c_str(), fcm_id.c_str(), ib_id.c_str());
                break;
            }
        }
        beast::get_lowest_layer(*ws).expires_never();
    }

    // Immediate heartbeat required after login
    {
        rti::RequestHeartbeat hb;
        hb.set_template_id(18);
        auto ts = std::chrono::system_clock::now().time_since_epoch();
        hb.set_ssboe(static_cast<int32_t>(
            std::chrono::duration_cast<std::chrono::seconds>(ts).count()));
        ws_write(proto_frame(hb));
    }

    // ── Subscribe for order updates (required before cancel) ─────────────────
    {
        rti::RequestSubscribeForOrderUpdates sub;
        sub.set_template_id(308);
        sub.set_fcm_id(fcm_id);
        sub.set_ib_id(ib_id);
        sub.set_account_id(account_id);
        ws_write(proto_frame(sub));
        LOG("[CANCEL_STOPS] Sent RequestSubscribeForOrderUpdates");
        // Wait briefly for the subscription ACK before sending cancels
        beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(3));
        try {
            for (;;) {
                auto pl = ws_read();
                rti::Base b; if (!b.ParseFromString(pl)) continue;
                if (b.template_id() == 309) {
                    rti::ResponseSubscribeForOrderUpdates resp; resp.ParseFromString(pl);
                    std::string rpc = resp.rp_code().empty() ? "?" : resp.rp_code(0);
                    LOG("[CANCEL_STOPS] Subscription ACK rp_code=%s", rpc.c_str());
                    break;
                }
            }
        } catch (...) {}
        beast::get_lowest_layer(*ws).expires_never();
    }

    // ── Send RequestCancelOrder for each basket ───────────────────────────────
    int sent = 0;
    for (const auto& s : stops) {
        rti::RequestCancelOrder req;
        req.set_template_id(316);
        req.set_basket_id(s.basket_id);
        req.set_account_id(account_id);
        req.set_fcm_id(fcm_id);
        req.set_ib_id(ib_id);
        try {
            ws_write(proto_frame(req));
            LOG("[CANCEL_STOPS] RequestCancelOrder sent: basket=%s sym=%s dir=%s",
                s.basket_id.c_str(), s.instrument.c_str(),
                s.was_buy ? "BUY-stop(SHORT)" : "SELL-stop(LONG)");
            ++sent;
        } catch (std::exception& e) {
            LOG("[CANCEL_STOPS] ERROR sending cancel basket=%s: %s",
                s.basket_id.c_str(), e.what());
        }
    }
    LOG("[CANCEL_STOPS] Sent %d/%zu cancel(s)", sent, stops.size());

    // ── Brief drain — collect ACKs (best-effort, 5s) ─────────────────────────
    LOG("[CANCEL_STOPS] Waiting 5s for cancel ACKs...");
    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(5));
    int acked = 0;
    try {
        for (;;) {
            auto pl = ws_read();
            rti::Base b; if (!b.ParseFromString(pl)) continue;
            int tid = b.template_id();
            if (tid == 351) {
                rti::RithmicOrderNotification n; n.ParseFromString(pl);
                LOG("[CANCEL_STOPS] tid=351 notify_type=%d basket=%s user_tag=%s status=%s",
                    (int)n.notify_type(), n.basket_id().c_str(),
                    n.user_tag().c_str(), n.status().c_str());
                if ((int)n.notify_type() == 3) ++acked;
            } else if (tid == 352) {
                rti::ExchangeOrderNotification n; n.ParseFromString(pl);
                LOG("[CANCEL_STOPS] tid=352 notify_type=%d basket=%s user_tag=%s",
                    (int)n.notify_type(), n.basket_id().c_str(), n.user_tag().c_str());
                if ((int)n.notify_type() == 3) ++acked;
            }
        }
    } catch (...) {}  // timeout = done

    LOG("[CANCEL_STOPS] Done — %d cancel ACK(s) received. "
        "Verify positions in RTrader.", acked);

    try { ws->close(websocket::close_code::normal); } catch (...) {}
    return 0;
}
