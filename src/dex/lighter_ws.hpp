#pragma once
// lighter_ws.hpp — Lighter's public market data over the network (no keys, no orders), shared by
// lighter_recorder and mm_paper. Message handling lives in lighter_feed.hpp.
//
//   markets:   GET https://mainnet.zklighter.elliot.ai/api/v1/orderBooks → symbol → market_id (active perps)
//   stream:    wss://mainnet.zklighter.elliot.ai/stream, "order_book/<id>" + "trade/<id>" per market
//   keepalive: the server drops a connection that sends nothing for 2 minutes → {"type":"ping"} every 60 s

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace lighter {

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using asio::use_awaitable;

inline const char* HOST = "mainnet.zklighter.elliot.ai";
using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

inline int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// One HTTPS request to any host (GET when body is empty, else POST with a JSON body).
inline asio::awaitable<std::string> https_request(ssl::context& ctx, const std::string& host, const std::string& target,
                                                   const std::string& body = "") {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(host, "443", use_awaitable);
    beast::ssl_stream<beast::tcp_stream> s(ex, ctx);
    beast::get_lowest_layer(s).expires_after(std::chrono::seconds(20));
    co_await beast::get_lowest_layer(s).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(s.native_handle(), host.c_str())) throw std::runtime_error("SNI failed");
    s.set_verify_callback(ssl::host_name_verification(host));
    co_await s.async_handshake(ssl::stream_base::client, use_awaitable);
    http::request<http::string_body> req{body.empty() ? http::verb::get : http::verb::post, target, 11};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, "rithmic_engine-lighter/1.0");
    if (!body.empty()) { req.set(http::field::content_type, "application/json"); req.body() = body; req.prepare_payload(); }
    co_await http::async_write(s, req, use_awaitable);
    beast::flat_buffer buf;
    http::response_parser<http::string_body> p;
    p.body_limit(32u << 20);
    co_await http::async_read(s, buf, p, use_awaitable);
    if (p.get().result() != http::status::ok) throw std::runtime_error("HTTP " + std::to_string(p.get().result_int()) + " for " + target);
    beast::error_code ec;
    beast::get_lowest_layer(s).expires_after(std::chrono::seconds(3));
    co_await s.async_shutdown(asio::redirect_error(use_awaitable, ec));
    co_return p.release().body();
}

inline asio::awaitable<std::string> https_get(ssl::context& ctx, const std::string& target) { co_return co_await https_request(ctx, HOST, target); }

// size_decimals / min_base / min_quote: Lighter's order-size rules (supported_size_decimals, min_base_amount,
// min_quote_amount; size_decimals −1 = not listed) — mm_paper rounds its paper quotes to them (hl::lot_qty).
struct MarketInfo { int id = -1; int price_decimals = 0; int size_decimals = -1; double min_base = 0, min_quote = 0; };

// symbol → {market_id, supported_price_decimals, size rules} for the requested symbols Lighter lists as active perps
// (missing ones are left out). 10^-price_decimals is the venue's real tick (hl::Params::venue_tick).
inline asio::awaitable<std::map<std::string, MarketInfo>> resolve_markets(ssl::context& ctx, const std::vector<std::string>& symbols) {
    const auto body = co_await https_get(ctx, "/api/v1/orderBooks");
    auto j = nlohmann::json::parse(body, nullptr, false);
    std::map<std::string, MarketInfo> all, out;
    auto dec = [](const nlohmann::json& ob, const char* k) {
        const auto it = ob.find(k);
        if (it == ob.end()) return 0.0;
        if (it->is_string()) return std::strtod(it->get_ref<const std::string&>().c_str(), nullptr);
        return it->is_number() ? it->get<double>() : 0.0;
    };
    if (j.is_object() && j.contains("order_books"))
        for (const auto& ob : j["order_books"])
            if (ob.value("market_type", "") == "perp" && ob.value("status", "") == "active")
                all[ob.value("symbol", "")] = {ob.value("market_id", -1), ob.value("supported_price_decimals", 0),
                                               ob.value("supported_size_decimals", -1), dec(ob, "min_base_amount"), dec(ob, "min_quote_amount")};
    for (const auto& s : symbols) if (auto it = all.find(s); it != all.end()) out[s] = it->second;
    co_return out;
}

// Hyperliquid's size precision per coin (POST https://api.hyperliquid.xyz/info {"type":"meta"}): with the
// price it defines HL's tick, the unit every backtest measured the strategy in (hl::TickRule::Hyperliquid).
inline asio::awaitable<std::map<std::string, int>> hyperliquid_sz_decimals(ssl::context& ctx) {
    const auto body = co_await https_request(ctx, "api.hyperliquid.xyz", "/info", R"({"type":"meta"})");
    auto j = nlohmann::json::parse(body, nullptr, false);
    std::map<std::string, int> out;
    if (j.is_object() && j.contains("universe"))
        for (const auto& a : j["universe"]) if (!a.value("isDelisted", false)) out[a.value("name", "")] = a.value("szDecimals", 0);
    co_return out;
}

inline asio::awaitable<std::shared_ptr<WsStream>> ws_connect(ssl::context& ctx) {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(HOST, "443", use_awaitable);
    auto ws = std::make_shared<WsStream>(ex, ctx);
    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(15));
    co_await beast::get_lowest_layer(*ws).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), HOST)) throw std::runtime_error("SNI failed");
    ws->next_layer().set_verify_callback(ssl::host_name_verification(HOST));
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);
    beast::get_lowest_layer(*ws).expires_never();
    websocket::stream_base::timeout to{};
    to.handshake_timeout = std::chrono::seconds(15);
    to.idle_timeout = std::chrono::seconds(60);
    to.keep_alive_pings = true;
    ws->set_option(to);
    ws->read_message_max(64 << 20);
    co_await ws->async_handshake(HOST, "/stream", use_awaitable);
    ws->text(true);
    co_return ws;
}

inline asio::awaitable<void> sleep_s(int s) {
    asio::steady_timer t(co_await asio::this_coro::executor);
    t.expires_after(std::chrono::seconds(s));
    co_await t.async_wait(use_awaitable);
}

// One connection: subscribe every market's book and trades, then hand each message to `on_msg` (parsed,
// raw text, receive time µs). Returns/throws when the connection ends; on_msg may throw to force a
// reconnect (e.g. on a nonce gap — a fresh connection brings fresh snapshots).
using MsgFn = std::function<void(const nlohmann::json&, const std::string&, int64_t)>;

inline asio::awaitable<void> session(ssl::context& ctx, const std::vector<int>& market_ids, MsgFn on_msg) {
    auto ws = co_await ws_connect(ctx);
    for (int id : market_ids)
        for (const char* ch : {"order_book/", "trade/"}) {
            const std::string sub = std::string(R"({"type":"subscribe","channel":")") + ch + std::to_string(id) + "\"}";
            co_await ws->async_write(asio::buffer(sub), use_awaitable);
        }
    auto alive = std::make_shared<std::atomic<bool>>(true);
    asio::co_spawn(co_await asio::this_coro::executor, [ws, alive]() -> asio::awaitable<void> {
        try {
            while (*alive) {
                co_await sleep_s(60);
                if (*alive) co_await ws->async_write(asio::buffer(std::string(R"({"type":"ping"})")), use_awaitable);
            }
        } catch (const std::exception&) {}
    }, asio::detached);
    beast::flat_buffer buf;
    try {
        while (true) {
            buf.clear();
            co_await ws->async_read(buf, use_awaitable);
            const int64_t recv = now_us();
            std::string msg = beast::buffers_to_string(buf.data());
            auto j = nlohmann::json::parse(msg, nullptr, false);
            if (j.is_discarded() || !j.is_object()) continue;
            on_msg(j, msg, recv);
        }
    } catch (...) {
        *alive = false;
        throw;
    }
}

}  // namespace lighter
