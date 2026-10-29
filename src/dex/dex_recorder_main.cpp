// dex_recorder — archives the order books and trades of Extended, Aster or Paradex for market-making research
// (founder 2026-10-25: after the venue survey, "lets set everything" — record every zero-maker-fee venue the way
// Lighter is recorded, so each gets the same G1 re-test). Public data only: no keys, no orders.
//
//   dex_recorder --venue extended|aster|paradex --out DIR --symbols AAVE,BNB,… [--duration S] [--paradex-feed deltas|snapshot]
//                [--name NAME] [--status FILE] [--frame-seconds N]
//
// Since 2026-10-26: a health file (default data/recorder_status/<name>.json, name = the venue; recorder_status.hpp)
// for scripts/recorder_watch.sh, and day files end their zstd frame every --frame-seconds (default 60, 0 = one frame
// per run as before) so a kill loses at most ~2 s and damage at most one frame (zstd_daily.hpp).
//
// --symbols are Hyperliquid coin names; the venue's market name is derived (AAVE-USD / AAVEUSDT / AAVE-USD-PERP)
// and checked against the venue's market list at start (unknown ones are skipped with a log line). Every message
// goes to DIR/<COIN>/<UTC date>.jsonl.zst as {"r":<recv µs>,"v":"<venue>","m":<message>} (venue_feeds.hpp has the
// shapes; Paradex frames are decoded from SBE first). The first line of a new day's file is a full snapshot rebuilt
// from the live book ("synthetic": true) for the venues that send deltas, so any one day file replays alone. A
// sequence gap reconnects — a fresh connection brings fresh snapshots. DIR/markets.json records each coin's venue
// market and price tick (scripts/mm_venues.sh reads it for mm_sim --params). DIR must exist: it lives on the
// second drive, and refusing a missing DIR is what keeps an unmounted drive from filling the root disk.

#include "dex/dex_ws.hpp"
#include "dex/extended_ws.hpp"
#include "dex/recorder_status.hpp"
#include "dex/venue_feeds.hpp"
#include "zstd_daily.hpp"

#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace asio = boost::asio;
using json = nlohmann::json;

static std::string g_venue;
static void logf(const char* fmt, ...) {
    char msg[2048];
    va_list ap; va_start(ap, fmt); std::vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    std::time_t t = std::time(nullptr);
    char ts[32]; std::strftime(ts, sizeof ts, "[%Y-%m-%d %H:%M:%S]", std::localtime(&t));
    std::printf("%s [%s] %s\n", ts, g_venue.c_str(), msg);
    std::fflush(stdout);
}

struct Market {
    std::string coin, market;
    double tick = 0;
    std::unique_ptr<ZstdDaily> file;
    std::string day;
    venue::extended::Book ext;
    venue::paradex::Book pdx;
};

static std::map<std::string, Market> g_mk;       // venue market name → state
static int64_t g_books = 0, g_trades = 0, g_gaps = 0, g_reconnects = 0, g_other = 0;
static std::unique_ptr<rec::Status> g_status;    // data/recorder_status/<name>.json (recorder_status.hpp)

// One archived line; the venues that send deltas open a new day file with a synthetic snapshot of the book.
// `extra` adds fields to the line ("snap":true marks a venue's replay of trades from before we listened — hl_events
// drops those, as it does Hyperliquid's; filling a paper order with a trade that happened an hour ago is a lie).
static void archive(Market& x, const std::string& m_json, int64_t recv, bool is_real_snapshot, bool is_trade, const std::string& extra = "") {
    const std::string day = ZstdDaily::utc_date(recv);
    if (day != x.day) {
        if (!x.day.empty() && !is_real_snapshot) {
            if (g_venue == "extended" && x.ext.valid())
                x.file->write_line("{\"r\":" + std::to_string(recv) + ",\"v\":\"extended\",\"m\":" + x.ext.snapshot_message().dump() + "}", recv);
            if (g_venue == "paradex" && x.pdx.valid())
                x.file->write_line("{\"r\":" + std::to_string(recv) + ",\"v\":\"paradex\",\"m\":" + x.pdx.snapshot_message().dump() + "}", recv);
        }
        x.day = day;
    }
    x.file->write_line("{\"r\":" + std::to_string(recv) + ",\"v\":\"" + g_venue + "\"" + extra + ",\"m\":" + m_json + "}", recv);
    g_status->set_connected(true);
    if (extra.find("\"snap\":true") == std::string::npos)
        g_status->on_msg(x.coin, is_trade ? rec::Status::Kind::Trade : rec::Status::Kind::Book, recv);
}

static asio::awaitable<void> backoff_sleep(int& backoff, const std::chrono::steady_clock::time_point& t0, const std::string& who, const std::string& why) {
    if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
    ++g_reconnects;
    g_status->on_reconnect();
    logf("%s disconnected (%s) — reconnecting in %d s", who.c_str(), why.c_str(), backoff);
    co_await lighter::sleep_s(backoff);
    backoff = std::min(backoff * 2, 60);
}

// ── market lists (REST, once at start; retried until it answers) ────────────────────────────────────────
// coin → (venue market, price tick) for the requested coins the venue lists as a live perp.
static asio::awaitable<std::map<std::string, std::pair<std::string, double>>> resolve(lighter::ssl::context& ctx, const std::vector<std::string>& coins) {
    std::map<std::string, std::pair<std::string, double>> out;
    if (g_venue == "extended") {
        for (const auto& [c, r] : co_await venue::extended::resolve(ctx, coins)) out[c] = {r.market, r.tick};
    } else if (g_venue == "aster" || g_venue == "binance") {      // Binance futures: the same exchangeInfo shape
        const bool bn = g_venue == "binance";
        auto j = json::parse(co_await lighter::https_request(ctx, bn ? venue::binance::API_HOST : venue::aster::API_HOST, "/fapi/v1/exchangeInfo"), nullptr, false);
        std::map<std::string, double> live;
        if (j.is_object() && j.contains("symbols"))
            for (const auto& s : j["symbols"]) {
                if (s.value("status", "") != "TRADING" || s.value("contractType", "") != "PERPETUAL") continue;
                double tick = 0;
                if (s.contains("filters")) for (const auto& f : s["filters"]) if (f.value("filterType", "") == "PRICE_FILTER") tick = hl::num(f.value("tickSize", json()));
                live[s.value("symbol", "")] = tick;
            }
        for (const auto& c : coins) if (auto it = live.find(venue::aster::market_for(c)); it != live.end()) out[c] = {it->first, it->second};   // same COINUSDT naming
    } else {
        auto j = json::parse(co_await lighter::https_request(ctx, venue::paradex::API_HOST, "/v1/markets"), nullptr, false);
        std::map<std::string, double> live;
        if (j.is_object() && j.contains("results"))
            for (const auto& m : j["results"])
                if (m.value("asset_kind", "") == "PERP") live[m.value("symbol", "")] = hl::num(m.value("price_tick_size", json()));
        for (const auto& c : coins) if (auto it = live.find(venue::paradex::market_for(c)); it != live.end()) out[c] = {it->first, it->second};
    }
    co_return out;
}

// ── Extended: one connection per stream per market ──────────────────────────────────────────────────────
static asio::awaitable<void> extended_stream(lighter::ssl::context& ctx, Market& x, bool book) {
    const std::string who = x.market + (book ? " book" : " trades");
    auto first_trades = std::make_shared<bool>(true);   // publicTrades opens with the market's RECENT trades (hours old on a quiet market)
    venue::extended::StreamHooks h;
    h.on_connect = [&x, book, first_trades] { if (book) x.ext = venue::extended::Book{}; *first_trades = true; };
    h.on_msg = [&x, book, first_trades](const std::string& raw, bool binary, int64_t recv) {
        if (binary) { ++g_other; return; }
        auto m = json::parse(raw, nullptr, false);
        if (m.is_discarded() || !m.is_object()) { ++g_other; return; }
        if (book) {
            const auto r = x.ext.apply(m);
            if (r == venue::Apply::Gap) { ++g_gaps; throw std::runtime_error("sequence gap"); }
            if (r == venue::Apply::Ignored) { ++g_other; return; }
            ++g_books;
            archive(x, raw, recv, r == venue::Apply::Snapshot, false);
        } else {
            if (!venue::extended::is_trades(m)) { ++g_other; return; }
            ++g_trades;
            archive(x, raw, recv, false, true, *first_trades ? ",\"snap\":true" : "");
            *first_trades = false;
        }
    };
    h.on_disconnect = [who](const std::string& why, int backoff) {
        ++g_reconnects;
        g_status->on_reconnect();
        logf("%s disconnected (%s) — reconnecting in %d s", who.c_str(), why.c_str(), backoff);
    };
    co_await venue::extended::run_stream(ctx, x.market, book, std::move(h));
}

// ── Aster / Binance: one connection, combined streams (Binance: bookTicker instead of depth20) ─────────────
static asio::awaitable<void> aster_session(lighter::ssl::context& ctx) {
    const bool bn = g_venue == "binance";
    int backoff = 1;
    std::vector<std::string> markets;
    for (const auto& [m, x] : g_mk) markets.push_back(m);
    while (true) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string why;
        try {
            auto ws = co_await dexws::connect(ctx, bn ? venue::binance::WS_HOST : venue::aster::WS_HOST,
                                              bn ? venue::binance::streams_target(markets) : venue::aster::streams_target(markets));
            co_await dexws::read_loop(ws, [&](const std::string& raw, bool binary, int64_t recv) {
                if (binary) { ++g_other; return; }
                auto m = json::parse(raw, nullptr, false);
                if (m.is_discarded() || !m.is_object()) { ++g_other; return; }
                auto it = g_mk.find(venue::aster::market_of_stream(venue::aster::stream_of(m)));
                if (it == g_mk.end()) { ++g_other; return; }
                const std::string e = m.contains("data") && m["data"].is_object() ? m["data"].value("e", "") : "";
                if (e == "depthUpdate" || e == "bookTicker") ++g_books; else if (e == "aggTrade" || e == "trade") ++g_trades; else { ++g_other; return; }
                archive(it->second, raw, recv, true, e == "aggTrade" || e == "trade");          // every depth20 / bookTicker message is a complete snapshot
            });
            why = "closed";
        } catch (const std::exception& e) { why = e.what(); }
        co_await backoff_sleep(backoff, t0, g_venue.c_str(), why);
    }
}

// ── Paradex: one connection, JSON-RPC subscriptions, SBE binary frames ──────────────────────────────────
static asio::awaitable<void> paradex_session(lighter::ssl::context& ctx, const std::string& feed) {
    int backoff = 1;
    while (true) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string why;
        for (auto& [m, x] : g_mk) x.pdx = venue::paradex::Book{};
        try {
            auto ws = co_await dexws::connect(ctx, venue::paradex::WS_HOST, venue::paradex::WS_TARGET);
            int id = 1;
            for (const auto& [m, x] : g_mk) {
                co_await dexws::send_text(ws, venue::paradex::subscribe_message(venue::paradex::book_channel(m, feed), id++));
                co_await dexws::send_text(ws, venue::paradex::subscribe_message(venue::paradex::trades_channel(m), id++));
            }
            co_await dexws::read_loop(ws, [&](const std::string& raw, bool binary, int64_t recv) {
                if (!binary) {
                    auto j = json::parse(raw, nullptr, false);
                    if (j.is_object() && j.contains("error")) logf("paradex error: %s", j["error"].dump().c_str());
                    ++g_other;
                    return;
                }
                json m = venue::paradex::sbe::decode(raw);
                if (m.is_null()) { ++g_other; return; }
                auto it = g_mk.find(m.value("m", ""));
                if (it == g_mk.end()) { ++g_other; return; }
                Market& x = it->second;
                const std::string c = m.value("c", "");
                if (c == "order_book") {
                    const auto r = x.pdx.apply(m);
                    if (r == venue::Apply::Gap) { ++g_gaps; throw std::runtime_error("sequence gap on " + x.market); }
                    if (r == venue::Apply::Ignored) { ++g_other; return; }
                    ++g_books;
                    archive(x, m.dump(), recv, r == venue::Apply::Snapshot, false);
                } else if (c == "trades") {
                    ++g_trades;
                    archive(x, m.dump(), recv, false, true);
                } else ++g_other;
            });
            why = "closed";
        } catch (const std::exception& e) { why = e.what(); }
        co_await backoff_sleep(backoff, t0, "paradex", why);
    }
}

int main(int argc, char** argv) {
    std::filesystem::path out;
    std::vector<std::string> coins;
    int duration_s = 0;
    std::string feed = "deltas", name;
    std::filesystem::path status_file;
    int frame_s = 60;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--venue" && i + 1 < argc) g_venue = argv[++i];
        else if (a == "--name" && i + 1 < argc) name = argv[++i];
        else if (a == "--status" && i + 1 < argc) status_file = argv[++i];
        else if (a == "--frame-seconds" && i + 1 < argc) frame_s = std::atoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--symbols" && i + 1 < argc) { std::stringstream ss(argv[++i]); for (std::string s; std::getline(ss, s, ',');) if (!s.empty()) coins.push_back(s); }
        else if (a == "--duration" && i + 1 < argc) duration_s = std::atoi(argv[++i]);
        else if (a == "--paradex-feed" && i + 1 < argc) feed = argv[++i];
        else { std::fprintf(stderr, "usage: dex_recorder --venue extended|aster|paradex|binance --out DIR --symbols A,B,C [--duration S] [--paradex-feed deltas|snapshot]\n"
                                    "                    [--name NAME] [--status FILE] [--frame-seconds N]\n"); return 2; }
    }
    if (g_venue != "extended" && g_venue != "aster" && g_venue != "paradex" && g_venue != "binance") { std::fprintf(stderr, "--venue must be extended, aster, paradex or binance\n"); return 2; }
    if (out.empty() || coins.empty()) { std::fprintf(stderr, "--out and --symbols are required\n"); return 2; }
    if (feed != "deltas" && feed != "snapshot") { std::fprintf(stderr, "--paradex-feed must be deltas or snapshot\n"); return 2; }
    std::error_code ec;
    if (!std::filesystem::is_directory(out, ec)) { logf("output dir %s does not exist — refusing", out.c_str()); return 1; }
    if (name.empty()) name = g_venue;
    if (status_file.empty()) status_file = std::filesystem::path("data/recorder_status") / (name + ".json");
    g_status = std::make_unique<rec::Status>(name, lighter::now_us());
    g_status->extra()["venue"] = g_venue;
    g_status->extra()["out"] = out.string();
    g_status->extra()["frame_seconds"] = frame_s;

    asio::io_context ioc(1);
    lighter::ssl::context ctx(lighter::ssl::context::tls_client);
    ctx.set_default_verify_paths();
    ctx.set_verify_mode(lighter::ssl::verify_peer);

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        std::map<std::string, std::pair<std::string, double>> live;
        while (live.empty()) {
            bool failed = false;
            try { live = co_await resolve(ctx, coins); }
            catch (const std::exception& e) { logf("market lookup failed (%s) — retrying in 30 s", e.what()); failed = true; }
            if (failed || live.empty()) co_await lighter::sleep_s(30);   // (no co_await inside a handler)
        }
        json meta = json::object();
        for (const auto& c : coins) {
            auto it = live.find(c);
            if (it == live.end()) { logf("not a live %s perp, skipped: %s", g_venue.c_str(), c.c_str()); continue; }
            Market x;
            x.coin = c; x.market = it->second.first; x.tick = it->second.second;
            x.file = std::make_unique<ZstdDaily>(out / c, [](const std::string& t) { logf("%s", t.c_str()); }, 3);
            x.file->set_frame_seconds(frame_s);
            x.file->set_sync(frame_s > 0);
            g_status->add_market(c);
            meta[c] = {{"market", x.market}, {"tick", x.tick}};
            g_mk[x.market] = std::move(x);
        }
        { std::ofstream f(out / "markets.json"); f << json{{"venue", g_venue}, {"written", lighter::now_us() / 1000}, {"markets", meta}}.dump(1) << "\n"; }
        logf("recording %zu %s markets → %s", g_mk.size(), g_venue.c_str(), out.c_str());
        auto ex = co_await asio::this_coro::executor;
        auto on_end = [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); } };
        if (g_venue == "extended") {
            for (auto& [m, x] : g_mk) {
                asio::co_spawn(ex, extended_stream(ctx, x, true), on_end);
                asio::co_spawn(ex, extended_stream(ctx, x, false), on_end);
            }
        } else if (g_venue == "aster" || g_venue == "binance") {
            asio::co_spawn(ex, aster_session(ctx), on_end);
        } else {
            asio::co_spawn(ex, paradex_session(ctx, feed), on_end);
        }
    }, [](std::exception_ptr e) { if (e) try { std::rethrow_exception(e); } catch (const std::exception& x) { logf("task ended: %s", x.what()); } });

    asio::co_spawn(ioc, [&]() -> asio::awaitable<void> {
        asio::steady_timer t(co_await asio::this_coro::executor);
        for (int n = 1;; ++n) {
            t.expires_after(std::chrono::seconds(2));
            co_await t.async_wait(asio::use_awaitable);
            const int64_t now = lighter::now_us();
            int64_t we = 0, dl = 0, fr = 0;
            for (auto& [m, x] : g_mk) { x.file->flush(now); we += x.file->write_errors(); dl += x.file->dropped_lines(); fr += x.file->frames(); }
            g_status->set_writer_totals(we, dl, fr);
            if (n % 2 == 0) g_status->write(status_file, now);
            if (n % 30 == 0) logf("summary: books=%lld trades=%lld gaps=%lld reconnects=%lld other=%lld", (long long)g_books, (long long)g_trades, (long long)g_gaps, (long long)g_reconnects, (long long)g_other);
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
    for (auto& [m, x] : g_mk) x.file->close();
    g_status->set_connected(false);
    g_status->write(status_file, lighter::now_us());
    logf("final: books=%lld trades=%lld gaps=%lld reconnects=%lld other=%lld", (long long)g_books, (long long)g_trades, (long long)g_gaps, (long long)g_reconnects, (long long)g_other);
    return 0;
}
