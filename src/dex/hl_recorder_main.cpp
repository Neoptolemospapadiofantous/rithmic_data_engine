// hl_recorder — archives Hyperliquid order books and trades for market-making research
// (founder 2026-10-25). Research data only: no orders, no keys, no Postgres.
//
//   hl_recorder --out DIR --coins A,B,C [--duration SECONDS] [--verbose] [--name NAME] [--status FILE] [--frame-seconds N]
//
// Since 2026-10-26: a health file (default data/recorder_status/<name>.json, name "hyperliquid"; recorder_status.hpp)
// for scripts/recorder_watch.sh, and day files end their zstd frame every --frame-seconds (default 60, 0 = one frame
// per run as before) so a kill loses at most ~2 s and damage at most one frame (zstd_daily.hpp).
//
// One websocket (wss://api.hyperliquid.xyz/ws, public, no auth), l2Book + trades subscriptions per
// coin. Every message is archived raw to DIR/<COIN>/<UTC date>.jsonl.zst as
//   {"r":<recv µs>,"snap":<bool>,"m":<message>}
// (format and the meaning of "snap" in src/dex/hl_mm.hpp). Kept forever, like the Bitstamp archive.
//
// DIR must already exist: it is meant to sit on the second drive, and refusing a missing DIR is what
// stops an unmounted drive from silently filling the root disk.
// Logs go to stdout (the systemd unit appends them to data/logs/hl_recorder.log).

#include "dex/hl_mm.hpp"
#include "dex/recorder_status.hpp"
#include "zstd_daily.hpp"

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace asio      = boost::asio;
namespace ssl       = asio::ssl;
namespace beast     = boost::beast;
namespace websocket = beast::websocket;
using tcp           = asio::ip::tcp;
using asio::use_awaitable;
using json          = nlohmann::json;

static void logf(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::time_t t = std::time(nullptr);
    char ts[32]; std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::localtime(&t));
    std::printf("%s [hl] %s\n", ts, msg);
    std::fflush(stdout);
}

static int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Opts {
    std::filesystem::path out;
    std::vector<std::string> coins;
    int duration_s = 0;
    bool verbose = false;
    std::string name = "hyperliquid";
    std::filesystem::path status;
    int frame_s = 60;
};
static Opts g_opt;
static std::map<std::string, std::unique_ptr<ZstdDaily>> g_files;
static std::atomic<int64_t> g_books{0}, g_trade_msgs{0}, g_snap_msgs{0}, g_other{0}, g_reconnects{0}, g_bytes{0};
static std::unique_ptr<rec::Status> g_status;

using WsStream = websocket::stream<beast::ssl_stream<beast::tcp_stream>>;

static asio::awaitable<std::shared_ptr<WsStream>> ws_connect(ssl::context& ctx) {
    const std::string host = "api.hyperliquid.xyz";
    auto ex = co_await asio::this_coro::executor;
    tcp::resolver res(ex);
    auto eps = co_await res.async_resolve(host, "443", use_awaitable);
    auto ws = std::make_shared<WsStream>(ex, ctx);
    beast::get_lowest_layer(*ws).expires_after(std::chrono::seconds(15));
    co_await beast::get_lowest_layer(*ws).async_connect(eps, use_awaitable);
    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str()))
        throw std::runtime_error("SNI failed");
    ws->next_layer().set_verify_callback(ssl::host_name_verification(host));
    co_await ws->next_layer().async_handshake(ssl::stream_base::client, use_awaitable);
    beast::get_lowest_layer(*ws).expires_never();
    websocket::stream_base::timeout to{};
    to.handshake_timeout = std::chrono::seconds(15);
    to.idle_timeout      = std::chrono::seconds(60);
    to.keep_alive_pings  = true;
    ws->set_option(to);
    ws->read_message_max(64 << 20);
    co_await ws->async_handshake(host, "/ws", use_awaitable);
    co_return ws;
}

static asio::awaitable<void> sleep_s(int s) {
    asio::steady_timer t(co_await asio::this_coro::executor);
    t.expires_after(std::chrono::seconds(s));
    co_await t.async_wait(use_awaitable);
}

// Hyperliquid drops a connection that sends nothing for 60 s: an application ping every 30 s.
static asio::awaitable<void> pinger(std::shared_ptr<WsStream> ws, std::shared_ptr<std::atomic<bool>> alive) {
    try {
        while (*alive) {
            co_await sleep_s(30);
            if (!*alive) break;
            co_await ws->async_write(asio::buffer(std::string(R"({"method":"ping"})")), use_awaitable);
        }
    } catch (const std::exception&) {}   // the read loop sees the same failure and reconnects
}

static asio::awaitable<void> session(ssl::context& ctx) {
    auto ws = co_await ws_connect(ctx);
    ws->text(true);
    for (const auto& c : g_opt.coins)
        for (const char* type : {"l2Book", "trades"}) {
            json sub = {{"method", "subscribe"}, {"subscription", {{"type", type}, {"coin", c}}}};
            // Without "fast" the book arrives every ~5 s (measured 2026-10-25); fast = every ~0.55 s.
            if (std::string(type) == "l2Book") sub["subscription"]["fast"] = true;
            co_await ws->async_write(asio::buffer(sub.dump()), use_awaitable);
        }
    logf("connected, subscribed l2Book+trades for %zu coins", g_opt.coins.size());
    auto alive = std::make_shared<std::atomic<bool>>(true);
    asio::co_spawn(co_await asio::this_coro::executor, pinger(ws, alive), asio::detached);
    // Hyperliquid opens each trades subscription with a replay of recent trades. The first trades
    // message per coin is "snap" when every trade in it predates this session (a quiet coin's first
    // message can be a real, live trade — that one is kept).
    const int64_t session_ms = now_us() / 1000;
    std::set<std::string> trades_seen;
    beast::flat_buffer buf;
    int shown = 0;
    try {
        while (true) {
            buf.clear();
            co_await ws->async_read(buf, use_awaitable);
            std::string msg = beast::buffers_to_string(buf.data());
            const int64_t recv = now_us();
            g_bytes += static_cast<int64_t>(msg.size());
            json j = json::parse(msg, nullptr, false);
            if (j.is_discarded() || !j.is_object()) { ++g_other; continue; }
            const std::string ch = j.value("channel", "");
            if (ch != "l2Book" && ch != "trades") { ++g_other; continue; }
            const std::string coin = hl::coin_of(j);
            auto f = g_files.find(coin);
            if (f == g_files.end()) { ++g_other; continue; }
            bool snap = false;
            if (ch == "trades") {
                if (trades_seen.insert(coin).second) {
                    int64_t newest = 0;
                    for (const auto& t : j["data"]) if (t.is_object()) newest = std::max(newest, t.value("time", int64_t{0}));
                    snap = newest < session_ms;
                }
                ++(snap ? g_snap_msgs : g_trade_msgs);
            } else {
                ++g_books;
            }
            if (g_opt.verbose && shown < 3) { ++shown; logf("sample: %.300s", msg.c_str()); }
            std::string line;
            line.reserve(msg.size() + 48);
            line += "{\"r\":"; line += std::to_string(recv);
            line += snap ? ",\"snap\":true,\"m\":" : ",\"snap\":false,\"m\":";
            line += msg; line += '}';
            f->second->write_line(line, recv);
            g_status->set_connected(true);
            if (!snap) g_status->on_msg(coin, ch == "trades" ? rec::Status::Kind::Trade : rec::Status::Kind::Book, recv);
        }
    } catch (...) {
        *alive = false;
        throw;
    }
}

static asio::awaitable<void> run_forever(ssl::context& ctx) {
    int backoff = 1;
    while (true) {
        auto t0 = std::chrono::steady_clock::now();
        std::string why;
        try { co_await session(ctx); why = "closed"; }
        catch (const std::exception& e) { why = e.what(); }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
        ++g_reconnects;
        g_status->on_reconnect();
        logf("disconnected (%s) — reconnecting in %d s", why.c_str(), backoff);
        co_await sleep_s(backoff);
        backoff = std::min(backoff * 2, 60);
    }
}

static asio::awaitable<void> housekeeping() {
    asio::steady_timer t(co_await asio::this_coro::executor);
    int ticks = 0;
    while (true) {
        t.expires_after(std::chrono::seconds(2));
        co_await t.async_wait(use_awaitable);
        const int64_t now = now_us();
        int64_t we = 0, dl = 0, fr = 0;
        for (auto& [c, f] : g_files) { f->flush(now); we += f->write_errors(); dl += f->dropped_lines(); fr += f->frames(); }
        g_status->set_writer_totals(we, dl, fr);
        if (ticks % 2 == 0) g_status->write(g_opt.status, now);
        if (++ticks % (g_opt.duration_s > 0 && g_opt.duration_s < 300 ? 5 : 30) == 0)
            logf("summary: books=%lld trades=%lld replayed=%lld other=%lld bytes_in=%lld reconnects=%lld",
                 (long long)g_books, (long long)g_trade_msgs, (long long)g_snap_msgs, (long long)g_other,
                 (long long)g_bytes, (long long)g_reconnects);
    }
}

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) g_opt.out = argv[++i];
        else if (a == "--coins" && i + 1 < argc) {
            std::stringstream ss(argv[++i]);
            for (std::string c; std::getline(ss, c, ',');) if (!c.empty()) g_opt.coins.push_back(c);
        }
        else if (a == "--duration" && i + 1 < argc) g_opt.duration_s = std::atoi(argv[++i]);
        else if (a == "--verbose") g_opt.verbose = true;
        else if (a == "--name" && i + 1 < argc) g_opt.name = argv[++i];
        else if (a == "--status" && i + 1 < argc) g_opt.status = argv[++i];
        else if (a == "--frame-seconds" && i + 1 < argc) g_opt.frame_s = std::atoi(argv[++i]);
        else {
            std::fprintf(stderr, "usage: hl_recorder --out DIR --coins A,B,C [--duration S] [--verbose] [--name NAME] [--status FILE] [--frame-seconds N]\n"
                                 "  DIR must exist (an unmounted drive must never fill the root disk)\n");
            return 2;
        }
    }
    if (g_opt.out.empty() || g_opt.coins.empty()) { std::fprintf(stderr, "--out and --coins are required\n"); return 2; }
    std::error_code ec;
    if (!std::filesystem::is_directory(g_opt.out, ec)) { logf("output dir %s does not exist — refusing", g_opt.out.c_str()); return 1; }
    if (g_opt.status.empty()) g_opt.status = std::filesystem::path("data/recorder_status") / (g_opt.name + ".json");
    g_status = std::make_unique<rec::Status>(g_opt.name, now_us());
    g_status->extra()["venue"] = "hyperliquid";
    g_status->extra()["out"] = g_opt.out.string();
    g_status->extra()["frame_seconds"] = g_opt.frame_s;
    for (const auto& c : g_opt.coins) {
        g_files[c] = std::make_unique<ZstdDaily>(g_opt.out / c, [](const std::string& s) { logf("%s", s.c_str()); },
                                                3);   // 30 streams: level 9 peaked at 326 MB, level 3 fits the unit's cap
        g_files[c]->set_frame_seconds(g_opt.frame_s);
        g_files[c]->set_sync(g_opt.frame_s > 0);
        g_status->add_market(c);
    }
    logf("hl_recorder starting: %zu coins → %s", g_opt.coins.size(), g_opt.out.c_str());

    asio::io_context ioc(1);
    ssl::context ctx(ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(ssl::verify_peer);
    auto on_done = [](std::exception_ptr e) {
        if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); }
    };
    asio::co_spawn(ioc, run_forever(ctx), on_done);
    asio::co_spawn(ioc, housekeeping(), on_done);
    asio::signal_set sigs(ioc, SIGINT, SIGTERM);
    sigs.async_wait([&](const boost::system::error_code&, int sig) { logf("signal %d — stopping", sig); ioc.stop(); });
    asio::steady_timer limit(ioc);
    if (g_opt.duration_s > 0) {
        limit.expires_after(std::chrono::seconds(g_opt.duration_s));
        limit.async_wait([&](const boost::system::error_code& e) {
            if (!e) { logf("--duration %d s reached — stopping", g_opt.duration_s); ioc.stop(); } });
    }
    ioc.run();
    for (auto& [c, f] : g_files) f->close();
    g_status->set_connected(false);
    g_status->write(g_opt.status, now_us());
    logf("final: books=%lld trades=%lld replayed=%lld other=%lld bytes_in=%lld reconnects=%lld",
         (long long)g_books, (long long)g_trade_msgs, (long long)g_snap_msgs, (long long)g_other,
         (long long)g_bytes, (long long)g_reconnects);
    return 0;
}
