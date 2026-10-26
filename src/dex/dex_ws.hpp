#pragma once
// dex_ws.hpp — one TLS websocket to any host, for the venue recorders that are not Lighter (lighter_ws.hpp keeps
// Lighter's own connect + subscribe + ping loop). Text and binary frames are both handed to the caller; Beast
// answers the server's ping frames by itself. REST lookups reuse lighter::https_request (any host).

#include "dex/lighter_ws.hpp"

namespace dexws {

namespace asio = boost::asio;
namespace ssl = asio::ssl;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = asio::ip::tcp;
using asio::use_awaitable;
using WsStream = lighter::WsStream;

inline asio::awaitable<std::shared_ptr<WsStream>> connect(ssl::context& ctx, const std::string& host, const std::string& target,
                                                          const std::string& user_agent = "rithmic_engine-dex/1.0") {
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(host, "443", use_awaitable);
    auto ws = std::make_shared<WsStream>(ex, ctx);
    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(15));
    co_await beast::get_lowest_layer(*ws).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) throw std::runtime_error("SNI failed");
    ws->next_layer().set_verify_callback(ssl::host_name_verification(host));
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);
    beast::get_lowest_layer(*ws).expires_never();
    websocket::stream_base::timeout to{};
    to.handshake_timeout = std::chrono::seconds(15);
    to.idle_timeout = std::chrono::seconds(90);        // a venue that goes silent for 90 s is reconnected
    to.keep_alive_pings = true;
    ws->set_option(to);
    ws->set_option(websocket::stream_base::decorator([user_agent](websocket::request_type& req) {
        req.set(beast::http::field::user_agent, user_agent);   // Extended refuses a handshake without one
    }));
    ws->read_message_max(64 << 20);
    co_await ws->async_handshake(host, target, use_awaitable);
    co_return ws;
}

// payload, binary?, receive time µs. Returns/throws when the connection ends; the callback may throw to force
// a reconnect (sequence gap → fresh snapshots).
using MsgFn = std::function<void(const std::string&, bool, int64_t)>;

inline asio::awaitable<void> read_loop(std::shared_ptr<WsStream> ws, MsgFn on_msg) {
    beast::flat_buffer buf;
    while (true) {
        buf.clear();
        co_await ws->async_read(buf, use_awaitable);
        const int64_t recv = lighter::now_us();
        on_msg(beast::buffers_to_string(buf.data()), ws->got_binary(), recv);
    }
}

inline asio::awaitable<void> send_text(std::shared_ptr<WsStream> ws, const std::string& text) {
    ws->text(true);
    co_await ws->async_write(asio::buffer(text), use_awaitable);
}

}  // namespace dexws
