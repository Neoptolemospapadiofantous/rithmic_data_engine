#pragma once
// extended_ws.hpp — Extended's public websocket streams, shared by dex_recorder (archives them) and mm_paper --venue
// extended (paper-trades on them). One connection per stream per market, as Extended serves them
// (venue_feeds.hpp has the URLs and message shapes): run_stream connects, hands every frame to on_msg, and on any end
// — a close, an error, or on_msg throwing (a sequence gap) — reports it and reconnects after 1, 2, 4 … 60 s (back to 1 s
// once a connection had lived 60 s). It never returns. Public data only: no keys.

#include "dex/dex_ws.hpp"
#include "dex/venue_feeds.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace venue::extended {

struct StreamHooks {
    std::function<void()> on_connect;                                            // before the first frame of each connection
    std::function<void(const std::string& raw, bool binary, int64_t recv_us)> on_msg;   // may throw to force a reconnect
    std::function<void(const std::string& why, int backoff_s)> on_disconnect;    // after the end, before the backoff sleep
};

inline boost::asio::awaitable<void> run_stream(lighter::ssl::context& ctx, const std::string& market, bool book, StreamHooks h) {
    int backoff = 1;
    while (true) {
        const auto t0 = std::chrono::steady_clock::now();
        std::string why;
        try {
            auto ws = co_await dexws::connect(ctx, HOST, book ? book_path(market) : trades_path(market));
            if (h.on_connect) h.on_connect();
            co_await dexws::read_loop(ws, h.on_msg);
            why = "closed";
        } catch (const std::exception& e) { why = e.what(); }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(60)) backoff = 1;
        if (h.on_disconnect) h.on_disconnect(why, backoff);
        co_await lighter::sleep_s(backoff);
        backoff = std::min(backoff * 2, 60);
    }
}

// coin → its market's rules, for the requested coins Extended lists as ACTIVE (REST, one call).
inline boost::asio::awaitable<std::map<std::string, MarketRule>> resolve(lighter::ssl::context& ctx, const std::vector<std::string>& coins) {
    const auto live = parse_markets(json::parse(co_await lighter::https_request(ctx, HOST, MARKETS_PATH), nullptr, false));
    std::map<std::string, MarketRule> out;
    for (const auto& c : coins) if (auto it = live.find(market_for(c)); it != live.end()) out[c] = it->second;
    co_return out;
}

}  // namespace venue::extended
