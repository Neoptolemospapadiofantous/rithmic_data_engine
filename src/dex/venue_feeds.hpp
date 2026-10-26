#pragma once
// venue_feeds.hpp — public market data of the three zero-maker-fee venues after Lighter, parsed into hl::Book /
// hl::Trade (founder 2026-10-25: "lets set everything" — record every venue the fee filter leaves, same archive
// shape, same simulator). Pure: no network, no files; unit-tested by tests/dex/test_venues.cpp. Used by
// dex_recorder and by hl_events.hpp (so mm_sim / mm_opt / mm_robust replay these recordings like Lighter's).
//
// Archive line (dex_recorder): {"r":<recv µs>,"v":"extended"|"aster"|"paradex","m":<message>} — "m" is the venue's
// own JSON for Extended and Aster, and for Paradex the decoded SBE frame in the shape paradex::sbe::decode writes
// (Paradex has required SBE binary on public channels since 2026-09-21). Directories are named by the Hyperliquid
// coin (AAVE), not the venue's market name (AAVE-USD / AAVEUSDT / AAVE-USD-PERP), so one per-coin parameter file
// serves every venue.
//
// Wire formats, all measured live on 2026-10-25:
//   Extended  wss://api.starknet.extended.exchange/stream.extended.exchange/v1/orderbooks/<MARKET>  (User-Agent required)
//             {"type":"SNAPSHOT"|"DELTA","data":{"m":"BTC-USD","b":[{"p","q","c"}],"a":[...]},"ts":<ms>,"seq":N}
//             first message a snapshot, then deltas; a full snapshot about every minute; seq +1 per message, any
//             other step = reconnect. In a DELTA "q" is the CHANGE and "c" the absolute size (0 removes the level);
//             in a SNAPSHOT "q" is the absolute size. Trades: .../v1/publicTrades/<MARKET>
//             {"ts","data":[{"m","S":"BUY"|"SELL" (taker side),"tT":"TRADE"|"LIQUIDATION"|"DELEVERAGE","T":<ms>,"p","q","i"}],"seq"}
//   Aster     wss://fstream.asterdex.com/stream?streams=<sym>@depth20@100ms/<sym>@aggTrade  (Binance futures shape)
//             {"stream":"btcusdt@depth20@100ms","data":{"e":"depthUpdate","E":<event ms>,"T":<transaction ms>,"s":"BTCUSDT",
//              "U","u","pu","b":[["px","sz"],...20],"a":[...]}} — every message is a complete 20-level snapshot.
//             {"stream":"btcusdt@aggTrade","data":{"e":"aggTrade","E","s","a","p","q","f","l","T":<ms>,"m":<buyer is maker>}}
//   Paradex   wss://ws.api.prod.paradex.trade/v1?sbeSchemaId=1&sbeSchemaVersion=1 (plain /v1 answers HTTP 400 since
//             the SBE requirement); JSON-RPC subscribe {"jsonrpc":"2.0","method":"subscribe","params":{"channel":
//             "order_book.BTC-USD-PERP.deltas@15@100ms"},"id":1} and "trades.BTC-USD-PERP"; control messages stay
//             JSON text, channel data arrives as BINARY frames: SBE header (4 × uint16 LE: blockLength, templateId,
//             schemaId, version) + root block + repeating groups + var strings, schema paradex_1_0.xml (tradeparadex/
//             paradex-py). Prices and sizes are int64 mantissas × 1e-8, INT64_MIN = null. Timestamps are µs.

#include "dex/hl_mm.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace venue {

using json = nlohmann::json;

inline std::string upper(std::string s) { for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
inline std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }

// Prices/sizes as the venues print them: no exponent, no trailing zeros.
inline std::string fmt(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10f", v);
    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    return s.empty() ? "0" : s;
}

// A price-keyed two-sided book shared by the venues that send deltas (Extended, Paradex).
class LevelBook {
public:
    void clear() { bids_.clear(); asks_.clear(); }
    void set(bool bid, double px, double sz) {
        if (px <= 0) return;
        if (bid) { if (sz > 0) bids_[px] = sz; else bids_.erase(px); }
        else     { if (sz > 0) asks_[px] = sz; else asks_.erase(px); }
    }
    bool crossed_or_empty() const { return bids_.empty() || asks_.empty() || !(bids_.begin()->first < asks_.begin()->first); }
    hl::Book top(size_t n, int64_t t_ms) const {
        hl::Book b;
        b.t_ms = t_ms;
        for (auto it = bids_.begin(); it != bids_.end() && b.bids.size() < n; ++it) b.bids.push_back({it->first, it->second});
        for (auto it = asks_.begin(); it != asks_.end() && b.asks.size() < n; ++it) b.asks.push_back({it->first, it->second});
        return b;
    }
    const std::map<double, double, std::greater<double>>& bids() const { return bids_; }
    const std::map<double, double>& asks() const { return asks_; }
private:
    std::map<double, double, std::greater<double>> bids_;
    std::map<double, double> asks_;
};

enum class Apply { Snapshot, Delta, Gap, Ignored };

// ── Extended ──────────────────────────────────────────────────────────────────────────────────────────────
namespace extended {

inline const char* HOST = "api.starknet.extended.exchange";
inline std::string market_for(const std::string& coin) { return coin + "-USD"; }
inline std::string coin_of_market(const std::string& m) { const auto p = m.find("-USD"); return p == std::string::npos ? m : m.substr(0, p); }
inline std::string book_path(const std::string& market) { return "/stream.extended.exchange/v1/orderbooks/" + market; }
inline std::string trades_path(const std::string& market) { return "/stream.extended.exchange/v1/publicTrades/" + market; }

class Book {
public:
    Apply apply(const json& m) {
        if (!m.is_object() || !m.contains("data") || !m["data"].is_object()) return Apply::Ignored;
        const std::string type = m.value("type", "");
        const json& d = m["data"];
        const int64_t seq = m.value("seq", int64_t{-1});
        if (type == "SNAPSHOT") {
            lv_.clear();
            levels(d, true);
            seq_ = seq; t_ms_ = m.value("ts", int64_t{0}); market_ = d.value("m", market_); valid_ = true;
            return Apply::Snapshot;
        }
        if (type != "DELTA") return Apply::Ignored;
        if (!valid_) return Apply::Gap;
        if (seq == seq_) return Apply::Ignored;   // the recorder's day-roll snapshot is written AFTER applying the update that crossed midnight, and that update is then archived again right after it — an exact repeat is already in the book, not a gap (until 2026-10-26 every replay lost hours after 00:00 UTC)
        if (seq != seq_ + 1) { valid_ = false; return Apply::Gap; }
        levels(d, false);
        seq_ = seq; t_ms_ = m.value("ts", t_ms_);
        return Apply::Delta;
    }
    bool valid() const { return valid_ && !lv_.crossed_or_empty(); }
    int64_t seq() const { return seq_; }
    int64_t t_ms() const { return t_ms_; }
    const std::string& market() const { return market_; }
    hl::Book top(size_t n) const { return lv_.top(n, t_ms_); }
    // The whole book as a SNAPSHOT message (written at the top of every day file so each replays alone).
    json snapshot_message() const {
        json b = json::array(), a = json::array();
        for (const auto& [p, s] : lv_.bids()) b.push_back({{"p", fmt(p)}, {"q", fmt(s)}});
        for (const auto& [p, s] : lv_.asks()) a.push_back({{"p", fmt(p)}, {"q", fmt(s)}});
        return {{"type", "SNAPSHOT"}, {"data", {{"t", "SNAPSHOT"}, {"m", market_}, {"b", b}, {"a", a}}}, {"ts", t_ms_}, {"seq", seq_}, {"synthetic", true}};
    }
private:
    void levels(const json& d, bool snapshot) {
        for (int side = 0; side < 2; ++side) {
            const char* key = side == 0 ? "b" : "a";
            if (!d.contains(key) || !d[key].is_array()) continue;
            for (const auto& l : d[key]) {
                const double px = hl::num(l.value("p", json()));
                // snapshot: q is the size; delta: c is the size after the change (q is the change itself)
                const double sz = snapshot ? hl::num(l.value("q", json())) : (l.contains("c") ? hl::num(l["c"]) : -1);
                if (sz < 0) continue;
                lv_.set(side == 0, px, sz);
            }
        }
    }
    LevelBook lv_;
    int64_t seq_ = -1, t_ms_ = 0;
    std::string market_;
    bool valid_ = false;
};

// Trades of a publicTrades message; DELEVERAGE is a position transfer, not a market trade.
inline std::vector<hl::Trade> parse_trades(const json& m) {
    std::vector<hl::Trade> out;
    if (!m.is_object() || !m.contains("data") || !m["data"].is_array()) return out;
    for (const auto& t : m["data"]) {
        const std::string tt = t.value("tT", "TRADE");
        if (tt != "TRADE" && tt != "LIQUIDATION") continue;
        hl::Trade tr{coin_of_market(t.value("m", "")), t.value("T", int64_t{0}), hl::num(t.value("p", json())), hl::num(t.value("q", json())), t.value("S", "") == "BUY"};
        if (tr.px > 0 && tr.sz > 0) out.push_back(tr);
    }
    return out;
}
inline bool is_trades(const json& m) { return m.is_object() && m.contains("data") && m["data"].is_array(); }
inline bool is_book(const json& m) { return m.is_object() && m.contains("data") && m["data"].is_object() && m["data"].contains("b"); }

}  // namespace extended

// ── Aster ─────────────────────────────────────────────────────────────────────────────────────────────────
namespace aster {

inline const char* WS_HOST = "fstream.asterdex.com";
inline const char* API_HOST = "fapi.asterdex.com";
inline std::string market_for(const std::string& coin) { return upper(coin) + "USDT"; }
inline std::string stream_of(const json& m) { return m.is_object() ? m.value("stream", "") : ""; }
// "btcusdt@depth20@100ms" → "BTCUSDT"
inline std::string market_of_stream(const std::string& s) { const auto p = s.find('@'); return upper(p == std::string::npos ? s : s.substr(0, p)); }
inline std::string streams_target(const std::vector<std::string>& markets) {
    std::string t = "/stream?streams=";
    bool first = true;
    for (const auto& m : markets) {
        const std::string l = lower(m);
        t += (first ? "" : "/") + l + "@depth20@100ms/" + l + "@aggTrade";
        first = false;
    }
    return t;
}

// A depth20 message is a complete book: 20 levels per side, stamped with the matching engine's time.
inline std::optional<hl::Book> parse_depth(const json& m) {
    if (!m.is_object() || !m.contains("data") || !m["data"].is_object()) return std::nullopt;
    const json& d = m["data"];
    if (d.value("e", "") != "depthUpdate" || !d.contains("b") || !d.contains("a")) return std::nullopt;
    hl::Book b;
    b.coin = d.value("s", "");
    b.t_ms = d.value("T", d.value("E", int64_t{0}));
    for (int side = 0; side < 2; ++side)
        for (const auto& l : d[side == 0 ? "b" : "a"]) {
            if (!l.is_array() || l.size() < 2) continue;
            hl::Level lv{hl::num(l[0]), hl::num(l[1])};
            if (lv.px > 0 && lv.sz > 0) (side == 0 ? b.bids : b.asks).push_back(lv);
        }
    std::sort(b.bids.begin(), b.bids.end(), [](const hl::Level& x, const hl::Level& y) { return x.px > y.px; });
    std::sort(b.asks.begin(), b.asks.end(), [](const hl::Level& x, const hl::Level& y) { return x.px < y.px; });
    return b;
}

// aggTrade: m = "buyer is the maker" → the taker SOLD.
inline std::vector<hl::Trade> parse_trades(const json& m) {
    std::vector<hl::Trade> out;
    if (!m.is_object() || !m.contains("data") || !m["data"].is_object()) return out;
    const json& d = m["data"];
    if (d.value("e", "") != "aggTrade") return out;
    hl::Trade tr{d.value("s", ""), d.value("T", int64_t{0}), hl::num(d.value("p", json())), hl::num(d.value("q", json())), !d.value("m", false)};
    if (tr.px > 0 && tr.sz > 0) out.push_back(tr);
    return out;
}

}  // namespace aster

// ── Paradex ───────────────────────────────────────────────────────────────────────────────────────────────
namespace paradex {

inline const char* WS_HOST = "ws.api.prod.paradex.trade";
inline const char* API_HOST = "api.prod.paradex.trade";
inline const char* WS_TARGET = "/v1?sbeSchemaId=1&sbeSchemaVersion=1";
inline std::string market_for(const std::string& coin) { return coin + "-USD-PERP"; }
// "deltas" takes no depth/rate parameters (the server answers "no parameters expected for deltas topic"); the
// snapshot feed is order_book.<M>.snapshot@15@100ms.
inline std::string book_channel(const std::string& market, const std::string& feed = "deltas") {
    return "order_book." + market + "." + (feed == "deltas" ? std::string("deltas") : std::string("snapshot@15@100ms"));
}
inline std::string trades_channel(const std::string& market) { return "trades." + market; }
inline std::string subscribe_message(const std::string& channel, int id) {
    return R"({"jsonrpc":"2.0","method":"subscribe","params":{"channel":")" + channel + R"("},"id":)" + std::to_string(id) + "}";
}

namespace sbe {

constexpr int64_t NULL_I64 = INT64_MIN;
constexpr uint16_t TEMPLATE_TRADE = 1, TEMPLATE_BBO = 2, TEMPLATE_BOOK = 3;

struct Cursor {
    const uint8_t* p; size_t n, i = 0;
    bool has(size_t k) const { return i + k <= n; }
    uint8_t u8() { uint8_t v = p[i]; i += 1; return v; }
    uint16_t u16() { uint16_t v; std::memcpy(&v, p + i, 2); i += 2; return v; }
    int64_t i64() { int64_t v; std::memcpy(&v, p + i, 8); i += 8; return v; }
};
inline double scaled(int64_t mantissa) { return mantissa == NULL_I64 ? 0.0 : static_cast<double>(mantissa) * 1e-8; }

// One binary frame → our JSON. Returns a null json for frames we do not decode (BBO, summaries, truncated data):
//   order_book: {"c":"order_book","m":<market>,"ts":<µs>,"seq":N,"pkg":"s"|"d","bids":[["px","sz"],…],"asks":[…]}
//   trades:     {"c":"trades","m":<market>,"ts":<µs>,"seq":N,"side":"BUY"|"SELL","price":"..","size":"..","created_at":<µs>,"type":"FILL"|…}
inline json decode(const uint8_t* data, size_t n) {
    Cursor c{data, n};
    if (!c.has(8)) return json();
    const uint16_t block = c.u16(), tmpl = c.u16(), schema = c.u16(), version = c.u16();
    (void)schema; (void)version;
    const size_t root_end = 8 + block;
    if (root_end > n) return json();
    auto var_string = [&]() -> std::string {
        if (!c.has(1)) return "";
        const uint8_t len = c.u8();
        if (!c.has(len)) return "";
        std::string s(reinterpret_cast<const char*>(c.p + c.i), len);
        c.i += len;
        return s;
    };
    if (tmpl == TEMPLATE_BOOK) {
        if (block < 17) return json();
        const int64_t ts = c.i64(), seq = c.i64();
        const uint8_t pkg = c.u8();
        c.i = root_end;                                  // skip the best-price fields whatever this version carries
        json out = {{"c", "order_book"}, {"ts", ts}, {"seq", seq}, {"pkg", pkg == 0 ? "s" : "d"}};
        for (const char* side : {"bids", "asks"}) {
            if (!c.has(4)) return json();
            const uint16_t gblock = c.u16(), count = c.u16();
            if (gblock < 16 || !c.has(static_cast<size_t>(gblock) * count)) return json();
            json levels = json::array();
            for (uint16_t k = 0; k < count; ++k) {
                const size_t at = c.i;
                const int64_t px = c.i64(), sz = c.i64();
                c.i = at + gblock;
                levels.push_back({fmt(scaled(px)), fmt(scaled(sz))});
            }
            out[side] = std::move(levels);
        }
        out["m"] = var_string();
        return out;
    }
    if (tmpl == TEMPLATE_TRADE) {
        if (block < 50) return json();
        const int64_t ts = c.i64(), seq = c.i64(), trade_id = c.i64();
        const uint8_t side = c.u8();
        const int64_t px = c.i64(), sz = c.i64(), created = c.i64();
        const uint8_t type = c.u8();
        (void)trade_id;
        c.i = root_end;
        static const char* kTypes[] = {"UNSPECIFIED", "FILL", "LIQUIDATION", "UNWIND_TRANSFER", "SETTLE_MARKET", "RPI", "BLOCK_TRADE"};
        json out = {{"c", "trades"}, {"ts", ts}, {"seq", seq}, {"side", side == 1 ? "BUY" : side == 2 ? "SELL" : "?"},
                    {"price", fmt(scaled(px))}, {"size", fmt(scaled(sz))}, {"created_at", created},
                    {"type", type < 7 ? kTypes[type] : "OTHER"}};
        out["m"] = var_string();
        return out;
    }
    return json();
}
inline json decode(const std::string& frame) { return decode(reinterpret_cast<const uint8_t*>(frame.data()), frame.size()); }

}  // namespace sbe

class Book {
public:
    Apply apply(const json& m) {
        if (!m.is_object() || m.value("c", "") != "order_book") return Apply::Ignored;
        const int64_t seq = m.value("seq", int64_t{-1});
        const std::string pkg = m.value("pkg", "");
        if (pkg == "s") {
            lv_.clear();
            levels(m);
            seq_ = seq; ts_us_ = m.value("ts", int64_t{0}); market_ = m.value("m", market_); valid_ = true;
            return Apply::Snapshot;
        }
        if (pkg != "d") return Apply::Ignored;
        if (!valid_) return Apply::Gap;
        if (seq == seq_) return Apply::Ignored;   // the day-roll repeat (see extended::Book::apply)
        if (seq <= seq_) { valid_ = false; return Apply::Gap; }   // deltas must apply in order; a repeat or a step back is a gap
        levels(m);
        seq_ = seq; ts_us_ = m.value("ts", ts_us_);
        return Apply::Delta;
    }
    bool valid() const { return valid_ && !lv_.crossed_or_empty(); }
    int64_t seq() const { return seq_; }
    int64_t t_ms() const { return ts_us_ / 1000; }
    const std::string& market() const { return market_; }
    hl::Book top(size_t n) const { return lv_.top(n, ts_us_ / 1000); }
    json snapshot_message() const {
        json b = json::array(), a = json::array();
        for (const auto& [p, s] : lv_.bids()) b.push_back({fmt(p), fmt(s)});
        for (const auto& [p, s] : lv_.asks()) a.push_back({fmt(p), fmt(s)});
        return {{"c", "order_book"}, {"m", market_}, {"ts", ts_us_}, {"seq", seq_}, {"pkg", "s"}, {"bids", b}, {"asks", a}, {"synthetic", true}};
    }
private:
    void levels(const json& m) {
        for (int side = 0; side < 2; ++side) {
            const char* key = side == 0 ? "bids" : "asks";
            if (!m.contains(key) || !m[key].is_array()) continue;
            for (const auto& l : m[key]) if (l.is_array() && l.size() >= 2) lv_.set(side == 0, hl::num(l[0]), hl::num(l[1]));
        }
    }
    LevelBook lv_;
    int64_t seq_ = -1, ts_us_ = 0;
    std::string market_;
    bool valid_ = false;
};

// FILL, LIQUIDATION and RPI are matched trades; transfers and settlements are not.
inline std::vector<hl::Trade> parse_trades(const json& m) {
    std::vector<hl::Trade> out;
    if (!m.is_object() || m.value("c", "") != "trades") return out;
    const std::string type = m.value("type", "FILL");
    if (type != "FILL" && type != "LIQUIDATION" && type != "RPI") return out;
    hl::Trade tr{m.value("m", ""), m.value("created_at", m.value("ts", int64_t{0})) / 1000, hl::num(m.value("price", json())), hl::num(m.value("size", json())), m.value("side", "") == "BUY"};
    if (tr.px > 0 && tr.sz > 0) out.push_back(tr);
    return out;
}

}  // namespace paradex

}  // namespace venue
