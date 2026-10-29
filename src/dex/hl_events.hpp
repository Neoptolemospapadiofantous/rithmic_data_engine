#pragma once
// hl_events.hpp — loads one coin-day of Hyperliquid books and trades into time order, from either
//   * the live archive written by hl_recorder (<dir>/<COIN>/<YYYY-MM-DD>.jsonl.zst), or
//   * Tardis.dev history (<dir>/raw/<COIN>/<YYYY-MM-DD>.{book,trades}.csv.gz, scripts/hl_history_download.sh;
//     book_snapshot_25 keeps 5 levels per side for Hyperliquid, so hl_mm.hpp's Book::visible matters).
// Order: exchange time, and at equal times trades BEFORE the book. A Hyperliquid block's trades and its
// book snapshot share one timestamp and the snapshot already shows the level after those trades; book
// first would count the traded size twice (once as a cancel, once as a trade) and understate our fills.

#include "dex/hl_mm.hpp"
#include "dex/lighter_feed.hpp"
#include "dex/venue_feeds.hpp"
#include "zstd_daily.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hl {

struct Event {
    int64_t t_ms = 0;
    int64_t seq = 0;          // arrival order, the last tie-break
    int64_t r_ms = 0;         // when WE received it (archive "r" / Tardis local_timestamp), ms; 0 = unknown. Cross-venue
                              // features align on it (mm_lab); the simulator never reads it.
    bool is_book = false;
    Book book;
    Trade trade;
};

inline void sort_events(std::vector<Event>& ev) {
    std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) {
        if (a.t_ms != b.t_ms) return a.t_ms < b.t_ms;
        if (a.is_book != b.is_book) return !a.is_book;
        return a.seq < b.seq;
    });
}

inline void run_events(const std::vector<Event>& ev, Simulator& s) {
    for (const auto& e : ev) {
        if (e.is_book) s.on_book(e.book);
        else s.on_trade(e.trade);
    }
}

// Live archive, any venue: Hyperliquid (full l2Book messages; "snap" trades = its replay of recent trades on
// subscribe, dropped), Lighter (lighter_feed.hpp: a book rebuilt from snapshot + changes, one event per message
// while the book is consistent; its own trade replay is dropped by lighter::parse_trades), or a dex_recorder
// archive tagged "v": extended / aster / paradex (venue_feeds.hpp; Extended and Paradex books are rebuilt from
// snapshot + deltas like Lighter's, every Aster depth20 message is a complete book).
//
// Lighter market_stats lines (recorded since 2026-10-26: mark/index price, funding, open interest) are never book or
// trade events — the replay is byte-identical with or without them. Pass `stats` to collect them (file order, each
// with its archive receive time); old archives simply yield none.
inline std::vector<Event> load_archive(const std::filesystem::path& file, int64_t* bad_lines = nullptr,
                                       std::vector<lighter::MarketStats>* stats = nullptr) {
    std::vector<Event> ev;
    ZstdLines in(file);
    std::string line;
    int64_t seq = 0;
    std::map<int, lighter::Book> lbooks;
    venue::extended::Book ebook;
    venue::paradex::Book pbook;
    int64_t r_ms = 0;
    auto push_book = [&](hl::Book b) { Event e; e.t_ms = b.t_ms; e.seq = seq++; e.r_ms = r_ms; e.is_book = true; e.book = std::move(b); ev.push_back(std::move(e)); };
    auto push_trades = [&](std::vector<hl::Trade> ts) { for (auto& t : ts) { t.coin.clear(); Event e; e.t_ms = t.t_ms; e.seq = seq++; e.r_ms = r_ms; e.trade = t; ev.push_back(std::move(e)); } };
    while (in.next(line)) {
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("m")) { if (bad_lines) ++*bad_lines; continue; }
        r_ms = j.contains("r") && j["r"].is_number() ? j["r"].get<int64_t>() / 1000 : 0;
        const json& m = j["m"];
        if (const std::string v = j.value("v", ""); !v.empty()) {
            if (j.value("snap", false)) continue;             // a venue's replay of trades from before the recorder listened
            if (v == "extended") {
                if (venue::extended::is_book(m)) {
                    if (auto b = venue::extended::book_event(ebook, m)) push_book(std::move(*b));   // the paper engine's own call
                } else if (venue::extended::is_trades(m)) push_trades(venue::extended::parse_trades(m));
                else if (bad_lines) ++*bad_lines;
            } else if (v == "aster") {
                if (auto b = venue::aster::parse_depth(m)) { b->coin.clear(); if (b->valid()) push_book(std::move(*b)); }
                else if (auto ts = venue::aster::parse_trades(m); !ts.empty()) push_trades(std::move(ts));
            } else if (v == "binance") {
                if (auto b = venue::binance::parse_book_ticker(m)) { b->coin.clear(); if (b->valid()) push_book(std::move(*b)); }
                else if (auto ts = venue::binance::parse_trades(m); !ts.empty()) push_trades(std::move(ts));
            } else if (v == "paradex") {
                if (m.value("c", "") == "order_book") {
                    const auto r = pbook.apply(m);
                    if ((r == venue::Apply::Snapshot || r == venue::Apply::Delta) && pbook.valid()) push_book(pbook.top(20));
                } else push_trades(venue::paradex::parse_trades(m));
            } else if (bad_lines) ++*bad_lines;
            continue;
        }
        if (lighter::is_market_stats(m)) {
            if (stats)
                for (auto& s : lighter::parse_market_stats(m)) { s.recv_us = j.value("r", int64_t{0}); stats->push_back(s); }
            continue;
        }
        if (const int mk = lighter::market_of(m, "order_book"); mk >= 0) {
            auto& lb = lbooks[mk];
            const auto r = lb.apply(m);
            if ((r == lighter::Book::Apply::Snapshot || r == lighter::Book::Apply::Delta) && lb.valid()) {
                Event e; e.t_ms = lb.t_ms(); e.seq = seq++; e.r_ms = r_ms; e.is_book = true; e.book = lb.top(20);
                ev.push_back(std::move(e));
            }
            continue;
        }
        if (lighter::market_of(m, "trade") >= 0) {
            for (auto& t : lighter::parse_trades(m)) { Event e; e.t_ms = t.t_ms; e.seq = seq++; e.r_ms = r_ms; e.trade = t; ev.push_back(std::move(e)); }
            continue;
        }
        if (auto b = parse_book(m)) {
            b->coin.clear();
            Event e; e.t_ms = b->t_ms; e.seq = seq++; e.r_ms = r_ms; e.is_book = true; e.book = std::move(*b);
            ev.push_back(std::move(e));
        } else if (!j.value("snap", false)) {
            for (auto& t : parse_trades(m)) {
                t.coin.clear();
                Event e; e.t_ms = t.t_ms; e.seq = seq++; e.r_ms = r_ms; e.trade = t;
                ev.push_back(std::move(e));
            }
        }
    }
    sort_events(ev);
    return ev;
}

// ── Tardis CSV ──────────────────────────────────────────────────────────────
class GzLines {
public:
    explicit GzLines(const std::filesystem::path& p) : f_(gzopen(p.c_str(), "rb")) { if (f_) gzbuffer(f_, 1 << 18); }
    ~GzLines() { if (f_) gzclose(f_); }
    GzLines(const GzLines&) = delete;
    GzLines& operator=(const GzLines&) = delete;
    bool ok() const { return f_ != nullptr; }
    bool next(std::string& line) {
        line.clear();
        if (!f_) return false;
        char buf[8192];
        while (gzgets(f_, buf, sizeof buf)) {
            line += buf;
            if (!line.empty() && line.back() == '\n') { line.pop_back(); if (!line.empty() && line.back() == '\r') line.pop_back(); return true; }
        }
        return !line.empty();
    }
private:
    gzFile f_ = nullptr;
};

inline std::vector<std::string_view> split_csv(const std::string& s) {   // Tardis CSV has no quoted fields
    std::vector<std::string_view> out;
    size_t a = 0;
    for (size_t i = 0; i <= s.size(); ++i)
        if (i == s.size() || s[i] == ',') { out.emplace_back(s.data() + a, i - a); a = i + 1; }
    return out;
}
inline double to_d(std::string_view v) { return v.empty() ? 0 : std::strtod(std::string(v).c_str(), nullptr); }

// book_snapshot_25: exchange,symbol,timestamp(µs),local_timestamp, then per level i:
//   asks[i].price, asks[i].amount, bids[i].price, bids[i].amount  (empty once the book runs out)
inline bool parse_tardis_book(const std::string& line, Book& b, int64_t* r_ms = nullptr) {
    auto f = split_csv(line);
    if (f.size() < 8 || f[2] == "timestamp") return false;
    b = Book{};
    b.t_ms = std::strtoll(std::string(f[2]).c_str(), nullptr, 10) / 1000;
    if (r_ms) *r_ms = std::strtoll(std::string(f[3]).c_str(), nullptr, 10) / 1000;
    for (size_t i = 4; i + 3 < f.size(); i += 4) {
        const double ap = to_d(f[i]), as = to_d(f[i + 1]), bp = to_d(f[i + 2]), bs = to_d(f[i + 3]);
        if (ap > 0 && as > 0) b.asks.push_back({ap, as});
        if (bp > 0 && bs > 0) b.bids.push_back({bp, bs});
    }
    return true;
}
// trades: exchange,symbol,timestamp(µs),local_timestamp,id,side(buy|sell = the aggressor),price,amount
inline bool parse_tardis_trade(const std::string& line, Trade& t, int64_t* r_ms = nullptr) {
    auto f = split_csv(line);
    if (f.size() < 8 || f[2] == "timestamp") return false;
    t = Trade{};
    t.t_ms = std::strtoll(std::string(f[2]).c_str(), nullptr, 10) / 1000;
    if (r_ms) *r_ms = std::strtoll(std::string(f[3]).c_str(), nullptr, 10) / 1000;
    t.buy_aggr = f[5] == "buy";
    t.px = to_d(f[6]);
    t.sz = to_d(f[7]);
    return t.px > 0 && t.sz > 0;
}

inline std::vector<Event> load_tardis(const std::filesystem::path& book_gz, const std::filesystem::path& trades_gz) {
    std::vector<Event> ev;
    std::string line;
    int64_t seq = 0;
    {
        GzLines in(book_gz);
        Book b;
        int64_t r = 0;
        while (in.next(line))
            if (parse_tardis_book(line, b, &r) && b.valid()) {
                Event e; e.t_ms = b.t_ms; e.seq = seq++; e.r_ms = r; e.is_book = true; e.book = std::move(b);
                ev.push_back(std::move(e));
            }
    }
    {
        GzLines in(trades_gz);
        Trade t;
        int64_t r = 0;
        while (in.next(line))
            if (parse_tardis_trade(line, t, &r)) { Event e; e.t_ms = t.t_ms; e.seq = seq++; e.r_ms = r; e.trade = t; ev.push_back(std::move(e)); }
    }
    sort_events(ev);
    return ev;
}

// The coin-days a directory holds, either layout: (coin, YYYY-MM-DD, loader input paths).
struct DayFile { std::string coin, day; std::filesystem::path a, b; bool tardis = false; };

inline std::vector<DayFile> list_days(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::vector<DayFile> out;
    std::error_code ec;
    if (fs::is_directory(dir / "raw", ec)) {
        for (const auto& c : fs::directory_iterator(dir / "raw"))
            for (const auto& f : fs::directory_iterator(c.path())) {
                const std::string n = f.path().filename().string();       // YYYY-MM-DD.book.csv.gz
                if (n.size() > 10 && n.substr(10) == ".book.csv.gz") {
                    fs::path tr = c.path() / (n.substr(0, 10) + ".trades.csv.gz");
                    if (fs::exists(tr)) out.push_back({c.path().filename().string(), n.substr(0, 10), f.path(), tr, true});
                }
            }
    } else {
        for (const auto& c : fs::directory_iterator(dir)) {
            if (!c.is_directory()) continue;
            for (const auto& f : fs::directory_iterator(c.path())) {
                const std::string n = f.path().filename().string();       // YYYY-MM-DD.jsonl.zst
                if (n.size() == 20 && n.substr(10) == ".jsonl.zst") out.push_back({c.path().filename().string(), n.substr(0, 10), f.path(), {}, false});
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const DayFile& x, const DayFile& y) { return x.coin != y.coin ? x.coin < y.coin : x.day < y.day; });
    return out;
}

inline std::vector<Event> load_day(const DayFile& d, int64_t* bad_lines = nullptr,
                                   std::vector<lighter::MarketStats>* stats = nullptr) {
    return d.tardis ? load_tardis(d.a, d.b) : load_archive(d.a, bad_lines, stats);
}

// Only the Lighter market_stats of one archive day file (funding / mark-price features), without building the book:
// a line is JSON-parsed only when it names a market_stats channel.
inline std::vector<lighter::MarketStats> load_market_stats(const std::filesystem::path& file) {
    std::vector<lighter::MarketStats> out;
    ZstdLines in(file);
    std::string line;
    while (in.next(line)) {
        if (line.find("\"market_stats:") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object() || !j.contains("m") || !lighter::is_market_stats(j["m"])) continue;
        for (auto& s : lighter::parse_market_stats(j["m"])) { s.recv_us = j.value("r", int64_t{0}); out.push_back(s); }
    }
    return out;
}

}  // namespace hl
