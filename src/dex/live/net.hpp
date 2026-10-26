#pragma once
/*  net.hpp — the REAL transports (Boost.Beast over TLS) behind transport.hpp's interfaces.

    Host guard (endpoint_guard): the only remote endpoint either client will ever connect to is
    mainnet.zklighter.elliot.ai:443 (system CA store, hostname verified). Any host naming "testnet" is refused by name.
    The single exception is loopback_test = true, which allows exactly 127.0.0.1 with a caller-supplied CA (the
    tests' local fake HTTPS/WS server); no config file can set it — only code can.

    BeastHttps: one TLS connection per request, everything async on a private io_context driven by run_for(timeout),
    so a stalled DNS/connect/read can never block past the deadline. `sent` is set immediately before the request is
    written: a failure before that is a definite "not sent"; any failure after it (no answer, cut connection,
    timeout) leaves status 0 with sent = true → the caller treats the outcome as UNKNOWN.

    BeastWs: one connection on its own io thread; received messages go to a mutex-protected inbox (poll), sends are
    posted to the io thread and written in order. Beast's keep-alive pings detect a dead peer; Lighter's
    application-level {"type":"ping"} is the engine's job (account_feed.hpp / engine.hpp).                    */

#include "dex/live/gateway_config.hpp"
#include "dex/live/transport.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace lighter::live {

namespace net_asio = boost::asio;
namespace net_ssl = boost::asio::ssl;
namespace net_beast = boost::beast;
namespace net_http = boost::beast::http;
namespace net_ws = boost::beast::websocket;

struct Endpoint {
    std::string host = kMainnetHost;
    std::string port = "443";
    std::string tls_name;        // name the certificate must carry (default: host)
    std::string ca_pem;          // loopback tests only: trust exactly this CA (empty = the system store)
    bool loopback_test = false;  // code-only switch for the local test servers
};

inline void endpoint_guard(const Endpoint& e) {
    if (e.host.find("testnet") != std::string::npos || e.tls_name.find("testnet") != std::string::npos)
        throw ConfigError("testnet endpoint '" + e.host + "' refused (mainnet only, never testnet)");
    if (e.loopback_test) {
        if (e.host != "127.0.0.1") throw ConfigError("loopback test endpoint must be 127.0.0.1");
        if (e.ca_pem.empty()) throw ConfigError("loopback test endpoint needs its own CA");
        return;
    }
    if (e.host != kMainnetHost || e.port != "443" || !(e.tls_name.empty() || e.tls_name == kMainnetHost))
        throw ConfigError("endpoint '" + e.host + ":" + e.port + "' refused (only " + std::string(kMainnetHost) + ":443)");
}

inline std::shared_ptr<net_ssl::context> make_tls_context(const Endpoint& e) {
    auto ctx = std::make_shared<net_ssl::context>(net_ssl::context::tls_client);
    if (e.ca_pem.empty()) ctx->set_default_verify_paths();
    else ctx->add_certificate_authority(net_asio::buffer(e.ca_pem.data(), e.ca_pem.size()));
    ctx->set_verify_mode(net_ssl::verify_peer);
    return ctx;
}

class BeastHttps : public HttpClient {
public:
    explicit BeastHttps(Endpoint e) : ep_(std::move(e)) {
        endpoint_guard(ep_);
        if (ep_.tls_name.empty()) ep_.tls_name = ep_.host;
        ctx_ = make_tls_context(ep_);
    }

    HttpResponse request(const HttpRequest& r) override {
        HttpResponse out;
        out.sent = false;
        bool got = false;
        {
            net_asio::io_context ioc;
            net_asio::co_spawn(ioc, [&]() -> net_asio::awaitable<void> {
                using net_asio::use_awaitable;
                net_asio::ip::tcp::resolver res(ioc);
                auto eps = co_await res.async_resolve(ep_.host, ep_.port, use_awaitable);
                net_beast::ssl_stream<net_beast::tcp_stream> s(ioc, *ctx_);
                co_await net_beast::get_lowest_layer(s).async_connect(eps, use_awaitable);
                if (!SSL_set_tlsext_host_name(s.native_handle(), ep_.tls_name.c_str())) throw std::runtime_error("SNI failed");
                s.set_verify_callback(net_ssl::host_name_verification(ep_.tls_name));
                co_await s.async_handshake(net_ssl::stream_base::client, use_awaitable);
                net_http::request<net_http::string_body> req{r.method == "POST" ? net_http::verb::post : net_http::verb::get, r.target, 11};
                req.set(net_http::field::host, ep_.tls_name);
                req.set(net_http::field::user_agent, "rithmic_engine-mm_live/1.0");
                for (const auto& [k, v] : r.headers) req.set(k, v);
                if (r.method == "POST") {
                    req.set(net_http::field::content_type, r.content_type.empty() ? "application/x-www-form-urlencoded" : r.content_type);
                    req.body() = r.body;
                    req.prepare_payload();
                }
                out.sent = true;   // from here on, a missing answer is UNKNOWN, never "not sent"
                co_await net_http::async_write(s, req, use_awaitable);
                net_beast::flat_buffer buf;
                net_http::response_parser<net_http::string_body> p;
                p.body_limit(8u << 20);
                co_await net_http::async_read(s, buf, p, use_awaitable);
                out.status = int(p.get().result_int());
                out.body = p.get().body();
                got = true;
                net_beast::error_code ec;
                co_await s.async_shutdown(net_asio::redirect_error(use_awaitable, ec));
            }, [&](std::exception_ptr e) {
                if (!e) return;
                try { std::rethrow_exception(e); } catch (const std::exception& x) { out.error = x.what(); }
            });
            ioc.run_for(std::chrono::milliseconds(r.timeout_ms));
        }
        if (!got) {
            out.status = 0;
            if (out.error.empty()) out.error = "timeout after " + std::to_string(r.timeout_ms) + " ms";
        }
        return out;
    }

private:
    Endpoint ep_;
    std::shared_ptr<net_ssl::context> ctx_;
};

class BeastWs : public WsConn {
public:
    explicit BeastWs(Endpoint e, std::string path = "/stream", int open_timeout_ms = 5'000)
        : ep_(std::move(e)), path_(std::move(path)), open_timeout_ms_(open_timeout_ms) {
        endpoint_guard(ep_);
        if (ep_.tls_name.empty()) ep_.tls_name = ep_.host;
        ctx_ = make_tls_context(ep_);
    }
    ~BeastWs() override { close(); }

    bool open() override {
        close();
        {
            std::lock_guard lk(mu_);
            inbox_.clear();
            error_.clear();
            state_ = State::Connecting;
        }
        ioc_ = std::make_unique<net_asio::io_context>();
        auto guard = std::make_shared<net_asio::executor_work_guard<net_asio::io_context::executor_type>>(ioc_->get_executor());
        net_asio::co_spawn(*ioc_, session(), [this](std::exception_ptr e) {
            std::string why = "closed";
            if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { why = x.what(); }
            std::lock_guard lk(mu_);
            if (error_.empty()) error_ = why;
            state_ = State::Closed;
            cv_.notify_all();
        });
        thread_ = std::thread([this, guard]() mutable {
            try { ioc_->run(); } catch (...) {}
        });
        std::unique_lock lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(open_timeout_ms_), [&] { return state_ != State::Connecting; });
        if (state_ == State::Open) return true;
        if (error_.empty()) error_ = "open timeout";
        lk.unlock();
        close();
        return false;
    }

    bool send(const std::string& text) override {
        if (!is_open()) return false;
        net_asio::post(*ioc_, [this, text] {
            outq_.push_back(text);
            if (!writing_) write_next();
        });
        return true;
    }

    std::optional<std::string> poll() override {
        std::lock_guard lk(mu_);
        if (inbox_.empty()) return std::nullopt;
        std::string m = std::move(inbox_.front());
        inbox_.pop_front();
        return m;
    }

    bool is_open() const override { std::lock_guard lk(mu_); return state_ == State::Open; }
    std::string last_error() const override { std::lock_guard lk(mu_); return error_; }

    void close() override {
        if (ioc_) ioc_->stop();
        if (thread_.joinable()) thread_.join();
        ws_.reset();     // the io thread is gone: closing the socket by destruction is race-free
        ioc_.reset();
        outq_.clear();
        writing_ = false;
        std::lock_guard lk(mu_);
        state_ = State::Closed;
    }

private:
    using Stream = net_ws::stream<net_beast::ssl_stream<net_beast::tcp_stream>>;
    enum class State { Closed, Connecting, Open };

    net_asio::awaitable<void> session() {
        using net_asio::use_awaitable;
        net_asio::ip::tcp::resolver res(*ioc_);
        auto eps = co_await res.async_resolve(ep_.host, ep_.port, use_awaitable);
        auto ws = std::make_shared<Stream>(*ioc_, *ctx_);
        ws_ = ws;
        net_beast::get_lowest_layer(*ws).expires_after(std::chrono::milliseconds(open_timeout_ms_));
        co_await net_beast::get_lowest_layer(*ws).async_connect(eps, use_awaitable);
        if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), ep_.tls_name.c_str())) throw std::runtime_error("SNI failed");
        ws->next_layer().set_verify_callback(net_ssl::host_name_verification(ep_.tls_name));
        co_await ws->next_layer().async_handshake(net_ssl::stream_base::client, use_awaitable);
        net_beast::get_lowest_layer(*ws).expires_never();
        net_ws::stream_base::timeout to{};
        to.handshake_timeout = std::chrono::milliseconds(open_timeout_ms_);
        to.idle_timeout = std::chrono::seconds(90);
        to.keep_alive_pings = true;
        ws->set_option(to);
        ws->read_message_max(64 << 20);
        co_await ws->async_handshake(ep_.tls_name, path_, use_awaitable);
        ws->text(true);
        {
            std::lock_guard lk(mu_);
            state_ = State::Open;
            cv_.notify_all();
        }
        net_beast::flat_buffer buf;
        while (true) {
            buf.clear();
            co_await ws->async_read(buf, use_awaitable);
            std::string m = net_beast::buffers_to_string(buf.data());
            std::lock_guard lk(mu_);
            inbox_.push_back(std::move(m));
        }
    }

    void write_next() {
        auto ws = ws_;
        if (!ws || outq_.empty()) { writing_ = false; return; }
        writing_ = true;
        ws->async_write(net_asio::buffer(outq_.front()), [this, ws](net_beast::error_code ec, std::size_t) {
            if (ec) {
                std::lock_guard lk(mu_);
                if (error_.empty()) error_ = "write: " + ec.message();
                state_ = State::Closed;
                writing_ = false;
                return;
            }
            outq_.pop_front();
            write_next();
        });
    }

    Endpoint ep_;
    std::string path_;
    int open_timeout_ms_;
    std::shared_ptr<net_ssl::context> ctx_;
    std::unique_ptr<net_asio::io_context> ioc_;
    std::thread thread_;
    std::shared_ptr<Stream> ws_;
    std::deque<std::string> outq_;   // io thread only
    bool writing_ = false;           // io thread only
    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> inbox_;
    State state_ = State::Closed;
    std::string error_;
};

}  // namespace lighter::live
