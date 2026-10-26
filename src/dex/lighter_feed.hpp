#pragma once
// lighter_feed.hpp — Lighter (zkLighter perps) order-book and trade messages → hl::Book / hl::Trade.
// Pure: no network, no files; unit-tested by tests/dex/test_lighter.cpp. Used by lighter_recorder,
// mm_paper and hl_events.hpp (so mm_sim / mm_opt / mm_robust replay Lighter recordings too).
//
// Wire format (wss://mainnet.zklighter.elliot.ai/stream, measured 2026-10-25):
//   subscribe {"type":"subscribe","channel":"order_book/<market_id>"} and "trade/<market_id>".
//   "subscribed/order_book" = the FULL book; every "update/order_book" after it carries only changed levels,
//   size "0.000" removing one. order_book.begin_nonce of an update must equal the previous nonce — anything
//   else is a gap and the book is not trustworthy until the next full snapshot (the caller reconnects).
//   Top-level "timestamp" is milliseconds.
//   "subscribed/trade" replays RECENT trades (before we were listening — never fill a paper order with them);
//   "update/trade" carries new ones in "trades" and "liquidation_trades". is_maker_ask true = the resting
//   order was the ask, i.e. the taker BOUGHT. Types "deleverage" and "market-settlement" are not market
//   trades and are skipped.

#include "dex/hl_mm.hpp"

#include <map>
#include <string>
#include <vector>

namespace lighter {

using json = nlohmann::json;

// "order_book:27" / "trade:27" → 27; -1 when the channel is not one of ours.
inline int market_of(const json& m, const char* prefix) {
    if (!m.is_object()) return -1;
    const std::string ch = m.value("channel", "");
    const std::string p = std::string(prefix) + ":";
    if (ch.rfind(p, 0) != 0) return -1;
    return std::atoi(ch.c_str() + p.size());
}

class Book {
public:
    enum class Apply { Snapshot, Delta, Gap, Ignored };

    Apply apply(const json& m) {
        const std::string type = m.value("type", "");
        if (!m.contains("order_book") || !m["order_book"].is_object()) return Apply::Ignored;
        const json& ob = m["order_book"];
        if (type == "subscribed/order_book") {
            bids_.clear(); asks_.clear();
            levels(ob, true);
            nonce_ = ob.value("nonce", int64_t{0});
            t_ms_ = m.value("timestamp", int64_t{0});
            valid_ = true;
            return Apply::Snapshot;
        }
        if (type != "update/order_book") return Apply::Ignored;
        if (!valid_) return Apply::Gap;
        // the recorder's day-roll snapshot is written AFTER applying the update that crossed midnight, and that update is then archived again right after it — an exact repeat is already in the book, not a gap (until 2026-10-26 every replay lost hours after 00:00 UTC)
        if (ob.value("nonce", int64_t{-1}) == nonce_) return Apply::Ignored;
        if (ob.value("begin_nonce", int64_t{-1}) != nonce_) { valid_ = false; return Apply::Gap; }
        levels(ob, false);
        nonce_ = ob.value("nonce", nonce_);
        t_ms_ = m.value("timestamp", t_ms_);
        return Apply::Delta;
    }

    bool valid() const { return valid_ && !bids_.empty() && !asks_.empty() && bids_.begin()->first < asks_.begin()->first; }
    int64_t nonce() const { return nonce_; }
    int64_t t_ms() const { return t_ms_; }
    size_t depth(bool bid_side) const { return bid_side ? bids_.size() : asks_.size(); }

    // Top `n` levels per side in hl::Book form (best first), stamped with the last message time.
    hl::Book top(size_t n) const {
        hl::Book b;
        b.t_ms = t_ms_;
        for (auto it = bids_.begin(); it != bids_.end() && b.bids.size() < n; ++it) b.bids.push_back({it->first, it->second});
        for (auto it = asks_.begin(); it != asks_.end() && b.asks.size() < n; ++it) b.asks.push_back({it->first, it->second});
        return b;
    }

    // The whole book as a "subscribed/order_book" message — the recorder writes one at the top of every day
    // file so each file can be replayed alone.
    json snapshot_message(int market) const {
        json asks = json::array(), bids = json::array();
        char buf[64];
        auto fmt = [&](double v) { std::snprintf(buf, sizeof buf, "%.10g", v); return std::string(buf); };
        for (const auto& [p, s] : asks_) asks.push_back({{"price", fmt(p)}, {"size", fmt(s)}});
        for (const auto& [p, s] : bids_) bids.push_back({{"price", fmt(p)}, {"size", fmt(s)}});
        return {{"channel", "order_book:" + std::to_string(market)}, {"type", "subscribed/order_book"},
                {"timestamp", t_ms_}, {"synthetic", true},
                {"order_book", {{"asks", asks}, {"bids", bids}, {"nonce", nonce_}, {"code", 0}}}};
    }

private:
    void levels(const json& ob, bool snapshot) {
        for (int side = 0; side < 2; ++side) {
            const char* key = side == 0 ? "bids" : "asks";
            if (!ob.contains(key) || !ob[key].is_array()) continue;
            for (const auto& l : ob[key]) {
                const double px = hl::num(l["price"]), sz = hl::num(l["size"]);
                if (px <= 0) continue;
                if (side == 0) { if (sz > 0) bids_[px] = sz; else if (!snapshot) bids_.erase(px); }
                else           { if (sz > 0) asks_[px] = sz; else if (!snapshot) asks_.erase(px); }
            }
        }
    }
    std::map<double, double, std::greater<double>> bids_;
    std::map<double, double> asks_;
    int64_t nonce_ = 0, t_ms_ = 0;
    bool valid_ = false;
};

// New market trades in an "update/trade" message (the "subscribed/trade" replay returns nothing).
inline std::vector<hl::Trade> parse_trades(const json& m) {
    std::vector<hl::Trade> out;
    if (!m.is_object() || m.value("type", "") != "update/trade") return out;
    for (const char* key : {"trades", "liquidation_trades"}) {
        if (!m.contains(key) || !m[key].is_array()) continue;
        for (const auto& t : m[key]) {
            const std::string type = t.value("type", "trade");
            if (type != "trade" && type != "liquidation") continue;
            hl::Trade tr{"", t.value("timestamp", int64_t{0}), hl::num(t["price"]), hl::num(t["size"]), t.value("is_maker_ask", false)};
            if (tr.px > 0 && tr.sz > 0) out.push_back(tr);
        }
    }
    return out;
}

// ── market_stats/<id> (recorded since 2026-10-26; verified on the live mainnet stream that day) ─────────────────
// {"channel":"market_stats:25","type":"subscribed/market_stats" | "update/market_stats","timestamp":<ms>,
//  "market_stats":{"symbol":"BNB","market_id":25,"index_price":"742.0016","mark_price":"741.7927",
//    "mid_price":"741.7800","best_ask_price":"741.8413","best_bid_price":"741.7186","open_interest":"4234301.090140",
//    "open_interest_usd":"8468602.180280","last_trade_price":"741.7186","current_funding_rate":"0.0012",
//    "funding_rate":"0.0012","funding_timestamp":1793012400000,"premium":"-0.0217", daily volume/range fields…}}
// About one update per second per market. Prices are decimal strings; funding_timestamp (ms) is on the hour —
// Lighter funds hourly. The rates are as Lighter publishes them (percent per funding period, per their docs —
// the unit is not converted here). "market_stats/all" (not used) nests one such object per market id; both
// shapes are accepted.
struct MarketStats {
    int market_id = -1;
    int64_t t_ms = 0;               // envelope "timestamp"
    int64_t recv_us = 0;            // archive "r" (filled by the loader; 0 when parsed from a bare message)
    double mark_price = 0, index_price = 0, mid_price = 0, last_trade_price = 0;
    double best_bid = 0, best_ask = 0;
    double funding_rate = 0, current_funding_rate = 0, premium = 0;
    double open_interest = 0, open_interest_usd = 0;
    int64_t funding_timestamp_ms = 0;
    bool snapshot = false;          // "subscribed/market_stats"
};

inline bool is_market_stats(const json& m) { return market_of(m, "market_stats") >= 0; }

inline std::vector<MarketStats> parse_market_stats(const json& m) {
    std::vector<MarketStats> out;
    if (!m.is_object() || !m.contains("market_stats") || !m["market_stats"].is_object()) return out;
    const std::string type = m.value("type", "");
    if (type != "subscribed/market_stats" && type != "update/market_stats") return out;
    const int64_t t = m.value("timestamp", int64_t{0});
    auto one = [&](const json& s) {
        if (!s.is_object() || !s.contains("market_id")) return;
        MarketStats x;
        x.market_id = s.value("market_id", -1);
        x.t_ms = t;
        x.snapshot = type == "subscribed/market_stats";
        auto d = [&](const char* k) { return s.contains(k) ? hl::num(s[k]) : 0.0; };
        x.mark_price = d("mark_price"); x.index_price = d("index_price"); x.mid_price = d("mid_price");
        x.last_trade_price = d("last_trade_price"); x.best_bid = d("best_bid_price"); x.best_ask = d("best_ask_price");
        x.funding_rate = d("funding_rate"); x.current_funding_rate = d("current_funding_rate"); x.premium = d("premium");
        x.open_interest = d("open_interest"); x.open_interest_usd = d("open_interest_usd");
        x.funding_timestamp_ms = s.value("funding_timestamp", int64_t{0});
        out.push_back(x);
    };
    const json& ms = m["market_stats"];
    if (ms.contains("market_id")) one(ms);
    else for (const auto& [k, v] : ms.items()) one(v);
    return out;
}

}  // namespace lighter
