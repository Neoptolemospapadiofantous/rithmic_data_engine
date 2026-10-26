// lighter_recorder — archives Lighter's order books and trades for market-making research (founder
// 2026-10-25: prove the edge on Lighter's own books, not Hyperliquid's). Public data only: no keys, no orders.
//
//   lighter_recorder --out DIR --symbols A,B,C [--duration SECONDS] [--name NAME] [--status FILE]
//                    [--frame-seconds N] [--no-stats] [--no-meta]
//
// Every message goes raw to DIR/<SYMBOL>/<UTC date>.jsonl.zst as {"r":<recv µs>,"m":<message>}. The first line
// of each new day's file is a full "subscribed/order_book" snapshot rebuilt from the live book ("synthetic":
// true), so any one day file replays alone (hl_events.hpp). A sequence gap (lighter_feed.hpp) reconnects —
// a fresh connection brings fresh snapshots. DIR must exist (it lives on the second drive; a missing DIR
// means an unmounted drive and the recorder refuses rather than fill the root disk). A STALLED book also
// reconnects: a trade on a market whose book has sent nothing for STALL_US means Lighter stopped that one
// order_book channel while the socket stayed up (2026-10-26 06:49-07:15 UTC: ONDO + GMX, 26 min, 18 ONDO trades
// printed into a frozen book). A trade proves the book should have changed; a quiet market (no trades) never trips it.
//
// Since 2026-10-26 (recording for live P&L, not just research):
//   * market_stats/<id> is subscribed too and archived in the same day file (mark / index / last trade price,
//     funding rate + next funding time, open interest). hl_events.hpp skips those lines for book/trade replay
//     and hands them out separately (lighter::parse_market_stats). --no-stats turns it off.
//   * DIR/_meta/<UTC date>.json = GET /api/v1/orderBookDetails at start and every day at 00:05 UTC; any change in
//     a recorded market's decimals, minimums or status is logged as a "meta change:" line (lighter::meta_diff).
//   * a status file (default data/recorder_status/<name>.json, recorder_status.hpp) for scripts/recorder_watch.sh.
//   * day files end their zstd frame every --frame-seconds (default 60; 0 = one frame per run as before), so a
//     kill loses at most the last ~2 s and damage at most one frame (zstd_daily.hpp).
//   * DIR/.lighter_recorder.lock (flock): a second recorder on the same DIR refuses to start — two writers on one
//     day file would interleave frames. A backup recorder uses its own DIR and --name (docs/market_making_system.md).

#include "dex/lighter_feed.hpp"
#include "dex/lighter_meta.hpp"
#include "dex/lighter_ws.hpp"
#include "dex/recorder_status.hpp"
#include "zstd_daily.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace asio = boost::asio;
using json = nlohmann::json;
namespace fs = std::filesystem;

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
    int64_t last_book_us = 0;   // recv time of the last order_book message on this connection (0 = none yet)
};

static constexpr int64_t STALL_US = 300'000'000;   // 5 min of book silence with trades printing

// Seconds from `now_us` to the next 00:05 UTC.
static int seconds_to_next_meta(int64_t now_us) {
    const int64_t s = now_us / 1'000'000;
    const int64_t day = 86400, at = 5 * 60;
    int64_t next = (s / day) * day + at;
    if (next <= s) next += day;
    return static_cast<int>(next - s);
}

int main(int argc, char** argv) {
    fs::path out, status_file;
    std::vector<std::string> symbols;
    std::string name = "lighter";
    int duration_s = 0, frame_s = 60;
    bool want_stats = true, want_meta = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--symbols" && i + 1 < argc) { std::stringstream ss(argv[++i]); for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) symbols.push_back(s); }
        else if (a == "--duration" && i + 1 < argc) duration_s = std::atoi(argv[++i]);
        else if (a == "--name" && i + 1 < argc) name = argv[++i];
        else if (a == "--status" && i + 1 < argc) status_file = argv[++i];
        else if (a == "--frame-seconds" && i + 1 < argc) frame_s = std::atoi(argv[++i]);
        else if (a == "--no-stats") want_stats = false;
        else if (a == "--no-meta") want_meta = false;
        else {
            std::fprintf(stderr, "usage: lighter_recorder --out DIR --symbols A,B,C [--duration S] [--name NAME] [--status FILE]\n"
                                 "                        [--frame-seconds N] [--no-stats] [--no-meta]\n");
            return 2;
        }
    }
    if (out.empty() || symbols.empty()) { std::fprintf(stderr, "--out and --symbols are required\n"); return 2; }
    if (status_file.empty()) status_file = fs::path("data/recorder_status") / (name + ".json");
    std::error_code ec;
    if (!fs::is_directory(out, ec)) { logf("output dir %s does not exist — refusing", out.c_str()); return 1; }

    const fs::path lock_path = out / ".lighter_recorder.lock";
    const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd < 0 || ::flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        logf("another lighter_recorder already writes %s (lock %s) — refusing; a backup recorder needs its own --out",
             out.c_str(), lock_path.c_str());
        return 1;
    }

    asio::io_context ioc(1);
    lighter::ssl::context ctx(lighter::ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(lighter::ssl::verify_peer);

    rec::Status status(name, lighter::now_us());
    status.extra()["venue"] = "lighter";
    status.extra()["out"] = out.string();
    status.extra()["frame_seconds"] = frame_s;
    status.extra()["meta_last_us"] = 0;
    status.extra()["meta_errors"] = 0;
    status.extra()["meta_changes"] = 0;

    std::map<int, Market> mk;                 // market_id → state
    int64_t books = 0, trades = 0, stats = 0, gaps = 0, stalls = 0, reconnects = 0;
    auto handle = [&](const json& m, const std::string& raw, int64_t recv) {
        int id = lighter::market_of(m, "order_book");
        const bool is_book = id >= 0;
        bool is_stats = false;
        if (!is_book) id = lighter::market_of(m, "trade");
        if (id < 0) { id = lighter::market_of(m, "market_stats"); is_stats = id >= 0; }
        auto it = mk.find(id);
        if (it == mk.end()) return;
        Market& x = it->second;
        status.set_connected(true);
        if (is_book) {
            const auto r = x.book.apply(m);
            if (r == lighter::Book::Apply::Gap) {
                ++gaps; status.on_gap(x.symbol);
                throw std::runtime_error("sequence gap on " + x.symbol + " — reconnecting for fresh snapshots");
            }
            ++books;
            x.last_book_us = recv;
            status.on_msg(x.symbol, rec::Status::Kind::Book, recv);
        } else if (is_stats) {
            ++stats;
            status.on_msg(x.symbol, rec::Status::Kind::Stats, recv);
        } else {
            ++trades;
            status.on_msg(x.symbol, rec::Status::Kind::Trade, recv);
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
        if (!is_book && !is_stats && x.last_book_us > 0 && recv - x.last_book_us > STALL_US) {
            ++stalls;
            throw std::runtime_error("book stalled on " + x.symbol + " (" + std::to_string((recv - x.last_book_us) / 1'000'000) +
                                     " s without a book update, trades printing) — reconnecting for fresh snapshots");
        }
    };

    std::set<int> recorded_ids;
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
            auto f = std::make_unique<ZstdDaily>(out / s, [](const std::string& t) { logf("%s", t.c_str()); }, 3);
            f->set_frame_seconds(frame_s);
            f->set_sync(frame_s > 0);
            mk[info.id] = Market{s, std::move(f), {}, {}};
            id_list.push_back(info.id);
            recorded_ids.insert(info.id);
            status.add_market(s);
        }
        logf("recording %zu Lighter markets → %s (market_stats %s, frames every %d s, status %s)", ids.size(), out.c_str(),
             want_stats ? "on" : "off", frame_s, status_file.c_str());
        std::vector<std::string> extra;
        if (want_stats) extra.push_back("market_stats/");
        int backoff = 1;
        while (true) {
            const auto t0 = std::chrono::steady_clock::now();
            std::string why;
            for (auto& [id, x] : mk) { x.book = lighter::Book{}; x.last_book_us = 0; }
            try { co_await lighter::session(ctx, id_list, handle, extra); why = "closed"; }
            catch (const std::exception& e) { why = e.what(); }
            if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
            ++reconnects;
            status.on_reconnect();
            logf("disconnected (%s) — reconnecting in %d s", why.c_str(), backoff);
            co_await lighter::sleep_s(backoff);
            backoff = std::min(backoff * 2, 60);
        }
    }, [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); } });

    // Daily metadata snapshot: at start, then every 00:05 UTC (a failed fetch retries every 5 minutes).
    if (want_meta)
        asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
            const fs::path dir = out / "_meta";
            while (true) {
                bool ok = false;
                std::string body, err;
                try { body = co_await lighter::order_book_details(ctx); }
                catch (const std::exception& e) { err = e.what(); }
                if (err.empty()) {
                    const auto now = lighter::now_us();
                    const auto res = lighter::write_meta_snapshot(dir, ZstdDaily::utc_date(now), body, recorded_ids);
                    if (res.ok) {
                        ok = true;
                        status.extra()["meta_last_us"] = now;
                        status.extra()["meta_file"] = res.file.string();
                        status.extra()["meta_changes"] = status.extra()["meta_changes"].get<int64_t>() + static_cast<int64_t>(res.changes.size());
                        logf("meta: %s (%d markets%s%s)", res.file.c_str(), res.markets,
                             res.previous.empty() ? "" : ", compared with ", res.previous.empty() ? "" : res.previous.filename().c_str());
                        for (const auto& c : res.changes) logf("meta change: %s", c.c_str());
                    } else err = res.error;
                }
                if (!ok) {
                    status.extra()["meta_errors"] = status.extra()["meta_errors"].get<int64_t>() + 1;
                    logf("meta snapshot failed (%s) — retrying in 5 min", err.c_str());
                }
                co_await lighter::sleep_s(ok ? seconds_to_next_meta(lighter::now_us()) : 300);
            }
        }, [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("meta task ended: %s", x.what()); } });

    auto write_status = [&] {
        int64_t we = 0, dl = 0, fr = 0;
        for (auto& [id, x] : mk) { we += x.file->write_errors(); dl += x.file->dropped_lines(); fr += x.file->frames(); }
        status.set_writer_totals(we, dl, fr);
        if (!status.write(status_file, lighter::now_us())) {
            static time_t last = 0;
            if (std::time(nullptr) - last >= 300) { last = std::time(nullptr); logf("cannot write status file %s", status_file.c_str()); }
        }
    };

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::steady_timer t(co_await asio::this_coro::executor);
        for (int n = 1;; ++n) {
            t.expires_after(std::chrono::seconds(2));
            co_await t.async_wait(asio::use_awaitable);
            const auto now = lighter::now_us();
            for (auto& [id, x] : mk) x.file->flush(now);
            if (n % 2 == 0) write_status();
            if (n % 30 == 0) logf("summary: books=%lld trades=%lld stats=%lld gaps=%lld stalls=%lld reconnects=%lld", (long long)books, (long long)trades, (long long)stats, (long long)gaps, (long long)stalls, (long long)reconnects);
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
    status.set_connected(false);
    write_status();
    logf("final: books=%lld trades=%lld stats=%lld gaps=%lld stalls=%lld reconnects=%lld", (long long)books, (long long)trades, (long long)stats, (long long)gaps, (long long)stalls, (long long)reconnects);
    ::close(lock_fd);
    return 0;
}
