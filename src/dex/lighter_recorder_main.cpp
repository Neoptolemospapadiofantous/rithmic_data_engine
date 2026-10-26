// lighter_recorder — archives Lighter's order books and trades for market-making research (founder
// 2026-10-25: prove the edge on Lighter's own books, not Hyperliquid's). Public data only: no keys, no orders.
//
//   lighter_recorder --out DIR --symbols A,B,C [--duration SECONDS]
//
// Every message goes raw to DIR/<SYMBOL>/<UTC date>.jsonl.zst as {"r":<recv µs>,"m":<message>}. The first line
// of each new day's file is a full "subscribed/order_book" snapshot rebuilt from the live book ("synthetic":
// true), so any one day file replays alone (hl_events.hpp). A sequence gap (lighter_feed.hpp) reconnects —
// a fresh connection brings fresh snapshots. DIR must exist (it lives on the second drive; a missing DIR
// means an unmounted drive and the recorder refuses rather than fill the root disk).

#include "dex/lighter_feed.hpp"
#include "dex/lighter_ws.hpp"
#include "zstd_daily.hpp"

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace asio = boost::asio;
using json = nlohmann::json;

static void logf(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::time_t t = std::time(nullptr);
    char ts[32]; std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::localtime(&t));
    std::printf("%s [lighter] %s\n", ts, msg);
    std::fflush(stdout);
}

struct Market {
    std::string symbol;
    std::unique_ptr<ZstdDaily> file;
    lighter::Book book;
    std::string day;
};

int main(int argc, char** argv) {
    std::filesystem::path out;
    std::vector<std::string> symbols;
    int duration_s = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--symbols" && i + 1 < argc) { std::stringstream ss(argv[++i]); for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) symbols.push_back(s); }
        else if (a == "--duration" && i + 1 < argc) duration_s = std::atoi(argv[++i]);
        else { std::fprintf(stderr, "usage: lighter_recorder --out DIR --symbols A,B,C [--duration S]\n"); return 2; }
    }
    if (out.empty() || symbols.empty()) { std::fprintf(stderr, "--out and --symbols are required\n"); return 2; }
    std::error_code ec;
    if (!std::filesystem::is_directory(out, ec)) { logf("output dir %s does not exist — refusing", out.c_str()); return 1; }

    asio::io_context ioc(1);
    lighter::ssl::context ctx(lighter::ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(lighter::ssl::verify_peer);

    std::map<int, Market> mk;                 // market_id → state
    int64_t books = 0, trades = 0, gaps = 0, reconnects = 0;
    auto handle = [&](const json& m, const std::string& raw, int64_t recv) {
        int id = lighter::market_of(m, "order_book");
        const bool is_book = id >= 0;
        if (!is_book) id = lighter::market_of(m, "trade");
        auto it = mk.find(id);
        if (it == mk.end()) return;
        Market& x = it->second;
        if (is_book) {
            const auto r = x.book.apply(m);
            if (r == lighter::Book::Apply::Gap) { ++gaps; throw std::runtime_error("sequence gap on " + x.symbol + " — reconnecting for fresh snapshots"); }
            ++books;
        } else {
            ++trades;
        }
        const std::string day = ZstdDaily::utc_date(recv);
        if (day != x.day) {
            // A new day file: open it with the full book so it replays on its own (skip when this very
            // message is the real snapshot, which already is one).
            if (!x.day.empty() && x.book.valid() && !(is_book && m.value("type", "") == "subscribed/order_book"))
                x.file->write_line("{\"r\":" + std::to_string(recv) + ",\"m\":" + x.book.snapshot_message(id).dump() + "}", recv);
            x.day = day;
        }
        x.file->write_line("{\"r\":" + std::to_string(recv) + ",\"m\":" + raw + "}", recv);
    };

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        std::map<std::string, lighter::MarketInfo> ids;
        while (ids.empty()) {
            bool failed = false;
            try { ids = co_await lighter::resolve_markets(ctx, symbols); }
            catch (const std::exception& e) { logf("market lookup failed (%s) — retrying in 30 s", e.what()); failed = true; }
            if (failed || ids.empty()) co_await lighter::sleep_s(30);   // (no co_await inside a handler)
        }
        for (const auto& s : symbols) if (!ids.count(s)) logf("not an active Lighter perp, skipped: %s", s.c_str());
        std::vector<int> id_list;
        for (const auto& [s, info] : ids) {
            mk[info.id] = Market{s, std::make_unique<ZstdDaily>(out / s, [](const std::string& t) { logf("%s", t.c_str()); }, 3), {}, {}};
            id_list.push_back(info.id);
        }
        logf("recording %zu Lighter markets → %s", ids.size(), out.c_str());
        int backoff = 1;
        while (true) {
            const auto t0 = std::chrono::steady_clock::now();
            std::string why;
            for (auto& [id, x] : mk) x.book = lighter::Book{};
            try { co_await lighter::session(ctx, id_list, handle); why = "closed"; }
            catch (const std::exception& e) { why = e.what(); }
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
            ++reconnects;
            logf("disconnected (%s) — reconnecting in %d s", why.c_str(), backoff);
            co_await lighter::sleep_s(backoff);
            backoff = std::min(backoff * 2, 60);
        }
    }, [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); } });

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::steady_timer t(co_await asio::this_coro::executor);
        for (int n = 1;; ++n) {
            t.expires_after(std::chrono::seconds(2));
            co_await t.async_wait(asio::use_awaitable);
            for (auto& [id, x] : mk) x.file->flush();
            if (n % 30 == 0) logf("summary: books=%lld trades=%lld gaps=%lld reconnects=%lld", (long long)books, (long long)trades, (long long)gaps, (long long)reconnects);
        }
    }, asio::detached);

    asio::signal_set sigs(ioc, SIGINT, SIGTERM);
    sigs.async_wait([&](const boost::system::error_code&, int sig) { logf("signal %d — stopping", sig); ioc.stop(); });
    asio::steady_timer limit(ioc);
    if (duration_s > 0) {
        limit.expires_after(std::chrono::seconds(duration_s));
        limit.async_wait([&](const boost::system::error_code& e) { if (!e) { logf("--duration %d s reached — stopping", duration_s); ioc.stop(); } });
    }
    ioc.run();
    for (auto& [id, x] : mk) x.file->close();
    logf("final: books=%lld trades=%lld gaps=%lld reconnects=%lld", (long long)books, (long long)trades, (long long)gaps, (long long)reconnects);
    return 0;
}
