#pragma once
// crypto_feed.hpp — pure parsing + order-book logic for crypto_collector.
//
// No network, no DB, no files: everything here is unit-tested by
// tests/crypto/test_crypto_collector.cpp. The binary (crypto_collector_main.cpp)
// wires it to websockets, Postgres and the zstd raw archive.
//
// Row conventions match the NQ collector (src/db.cpp write_ticks/write_bbo) so the
// paper engine replays crypto exactly like NQ:
//   ticks.size     BIGINT  — quantity in exchange lot steps of 0.001 (llround(qty*1000))
//   ticks.side     'B' when the aggressor bought, else 'A' (db.cpp convention, not 'S')
//   ticks.is_buy   aggressor bought  (= NOT Binance's "m" buyer-is-maker flag)
//   ticks.seq      Binance aggregate trade id % 2,000,000,000 — trades sharing a
//                  millisecond keep exchange order (replay orders by ts_event, seq, price, size)
//   bbo sizes      INTEGER, same 0.001 lots; orders NULL (Binance gives no order counts)
// The parent session's historical backfill (source 'binance_hist') uses the same rules.

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crypto {

constexpr double LOT = 0.001;                       // 1 size unit = 0.001 BTC/ETH
inline int64_t to_lots(double qty) { return std::llround(qty / LOT); }

// ── Rows destined for the existing ticks / bbo tables ──────────────────────
struct TickRow {
    int64_t ts_us = 0;
    std::string symbol, exchange, source;
    double price = 0;
    int64_t size = 0;
    int32_t seq = 0;
    bool is_buy = false;
    char side() const { return is_buy ? 'B' : 'A'; }
};

struct BboRow {
    int64_t ts_us = 0;
    std::string symbol, exchange, source;
    double bid_price = 0, ask_price = 0;
    int64_t bid_size = 0, ask_size = 0;               // 0.001 lots
};

// ── numeric helpers (Binance / Bitstamp send numbers as strings) ───────────
inline double num(const nlohmann::json& v) {
    if (v.is_string()) return std::strtod(v.get_ref<const std::string&>().c_str(), nullptr);
    if (v.is_number()) return v.get<double>();
    return 0.0;
}
inline int64_t inum(const nlohmann::json& v) {
    if (v.is_string()) return std::strtoll(v.get_ref<const std::string&>().c_str(), nullptr, 10);
    if (v.is_number_integer()) return v.get<int64_t>();
    if (v.is_number()) return static_cast<int64_t>(v.get<double>());
    return 0;
}
inline uint64_t unum(const nlohmann::json& v) {
    if (v.is_string()) return std::strtoull(v.get_ref<const std::string&>().c_str(), nullptr, 10);
    if (v.is_number_unsigned()) return v.get<uint64_t>();
    if (v.is_number_integer()) return static_cast<uint64_t>(v.get<int64_t>());
    return 0;
}

// Missing-key-safe field access (const json::operator[] on a missing key is UB).
inline const nlohmann::json& at(const nlohmann::json& d, const char* k) {
    static const nlohmann::json null_json;
    auto it = d.find(k);
    return it == d.end() ? null_json : *it;
}

// Binance combined-stream envelope {"stream":"...","data":{...}} or a raw payload.
inline const nlohmann::json& binance_payload(const nlohmann::json& j) {
    auto it = j.find("data");
    return (it != j.end() && it->is_object()) ? *it : j;
}

// aggTrade: {"e":"aggTrade","E":..,"s":"BTCUSDT","a":agg_id,"p":"..","q":"..",
//            "f":..,"l":..,"T":trade_ms,"m":buyer_is_maker}
inline std::optional<TickRow> parse_agg_trade(const nlohmann::json& j,
                                              const std::string& source = "binance_fut") {
    const auto& d = binance_payload(j);
    if (!d.is_object() || d.value("e", "") != "aggTrade") return std::nullopt;
    TickRow r;
    r.symbol   = d.value("s", "");
    r.exchange = "BINANCE";
    r.source   = source;
    r.price    = num(at(d, "p"));
    r.size     = to_lots(num(at(d, "q")));
    r.ts_us    = inum(at(d, "T")) * 1000;
    r.seq      = static_cast<int32_t>(inum(at(d, "a")) % 2000000000LL);
    r.is_buy   = !d.value("m", false);
    if (r.symbol.empty() || r.price <= 0 || r.size <= 0 || r.ts_us <= 0) return std::nullopt;
    return r;
}

// bookTicker: {"e":"bookTicker","u":..,"E":..,"T":tx_ms,"s":"BTCUSDT",
//              "b":bid,"B":bid_qty,"a":ask,"A":ask_qty}
inline std::optional<BboRow> parse_book_ticker(const nlohmann::json& j,
                                               const std::string& source = "binance_fut") {
    const auto& d = binance_payload(j);
    if (!d.is_object() || d.value("e", "") != "bookTicker") return std::nullopt;
    BboRow r;
    r.symbol    = d.value("s", "");
    r.exchange  = "BINANCE";
    r.source    = source;
    r.bid_price = num(at(d, "b"));
    r.bid_size  = to_lots(num(at(d, "B")));
    r.ask_price = num(at(d, "a"));
    r.ask_size  = to_lots(num(at(d, "A")));
    int64_t t   = inum(d.contains("T") ? at(d, "T") : at(d, "E"));
    r.ts_us     = t * 1000;
    if (r.symbol.empty() || r.bid_price <= 0 || r.ask_price <= 0 || r.ts_us <= 0) return std::nullopt;
    return r;
}

// ── bookTicker throttle: keep only the LAST quote per symbol per 100 ms bucket ──
// A quote is held until a quote from a later bucket arrives (then the held one is
// emitted) or flush_due() finds it older than one bucket of wall/feed time.
class QuoteThrottle {
public:
    explicit QuoteThrottle(int64_t bucket_us = 100'000) : bucket_us_(bucket_us) {}

    // Returns the row to emit (the previous bucket's last quote), if any.
    std::optional<BboRow> push(const BboRow& q) {
        auto& slot = held_[q.symbol];
        std::optional<BboRow> out;
        if (slot && slot->ts_us / bucket_us_ != q.ts_us / bucket_us_) out = slot;
        slot = q;
        return out;
    }
    // Emit held quotes whose bucket has ended by `now_us` (feed clock or wall clock).
    std::vector<BboRow> flush_due(int64_t now_us) {
        std::vector<BboRow> out;
        for (auto& [sym, slot] : held_)
            if (slot && now_us / bucket_us_ > slot->ts_us / bucket_us_) { out.push_back(*slot); slot.reset(); }
        return out;
    }
private:
    int64_t bucket_us_;
    std::map<std::string, std::optional<BboRow>> held_;
};

// ── Bitstamp market-by-order ────────────────────────────────────────────────
// Bitstamp live_orders_<pair> publishes order_created / order_changed / order_deleted.
// data: {id, id_str, order_type (0 buy, 1 sell), price, amount, datetime, microtimestamp,
//        and on newer feeds amount_traded / amount_at_create}.
// THERE ARE NO SEQUENCE NUMBERS on this channel: a gap cannot be detected from the
// stream itself. (Live messages also carry opaque "event_id" / "pre_event_id" strings —
// undocumented; they are kept in the raw archive and could support gap checks later.)
// A filled order arrives as order_deleted with amount 0 and amount_traded == amount_at_create
// (observed live 2026-10-10); a cancel keeps its remaining amount. The resync rule is therefore time-based — after every (re)connect we
// take a REST snapshot (/api/v2/order_book/{pair}/?group=2, which carries full order ids)
// and skip every event whose microtimestamp <= the snapshot's microtimestamp.
enum class MboKind { Created, Changed, Deleted, Other };

struct MboEvent {
    MboKind kind = MboKind::Other;
    uint64_t id = 0;
    bool is_bid = false;
    double price = 0, amount = 0;
    std::optional<double> amount_traded;          // present on newer feeds only
    int64_t micro_ts = 0;
    std::string channel;
};

inline std::optional<MboEvent> parse_bitstamp_event(const nlohmann::json& j) {
    if (!j.is_object()) return std::nullopt;
    const std::string ev = j.value("event", "");
    MboEvent e;
    if (ev == "order_created") e.kind = MboKind::Created;
    else if (ev == "order_changed") e.kind = MboKind::Changed;
    else if (ev == "order_deleted") e.kind = MboKind::Deleted;
    else return std::nullopt;
    auto it = j.find("data");
    if (it == j.end() || !it->is_object()) return std::nullopt;
    const auto& d = *it;
    e.channel  = j.value("channel", "");
    e.id       = d.contains("id_str") ? unum(at(d, "id_str")) : unum(at(d, "id"));
    e.is_bid   = inum(at(d, "order_type")) == 0;
    e.price    = d.contains("price_str") ? num(at(d, "price_str")) : num(at(d, "price"));
    e.amount   = d.contains("amount_str") ? num(at(d, "amount_str")) : num(at(d, "amount"));
    if (d.contains("amount_traded")) e.amount_traded = num(at(d, "amount_traded"));
    e.micro_ts = inum(at(d, "microtimestamp"));
    if (e.id == 0 || e.micro_ts <= 0) return std::nullopt;
    return e;
}

struct BookOrder { bool is_bid; int64_t px; double amount; };

struct Snapshot {
    int64_t micro_ts = 0;
    std::vector<std::pair<uint64_t, BookOrder>> orders;
};

constexpr double PX_SCALE = 1e8;
inline int64_t px_key(double p) { return std::llround(p * PX_SCALE); }
inline double px_val(int64_t k) { return static_cast<double>(k) / PX_SCALE; }

// /api/v2/order_book/{pair}/?group=2 → {"timestamp","microtimestamp",
//   "bids":[[price,amount,order_id],...],"asks":[...]}
inline std::optional<Snapshot> parse_bitstamp_snapshot(const nlohmann::json& j) {
    if (!j.is_object() || !j.contains("microtimestamp")) return std::nullopt;
    Snapshot s;
    s.micro_ts = inum(at(j, "microtimestamp"));
    for (const char* side : {"bids", "asks"}) {
        auto it = j.find(side);
        if (it == j.end() || !it->is_array()) return std::nullopt;
        const bool bid = side[0] == 'b';
        for (const auto& lvl : *it) {
            if (!lvl.is_array() || lvl.size() < 3) return std::nullopt;   // group=2 carries ids
            s.orders.push_back({unum(lvl[2]), BookOrder{bid, px_key(num(lvl[0])), num(lvl[1])}});
        }
    }
    if (s.micro_ts <= 0) return std::nullopt;
    return s;
}

// One mbo_1m row (see migrations/20261010_mbo_1m.sql for the column meanings).
struct Mbo1m {
    std::string venue, symbol;
    int64_t minute_us = 0;                       // start of the minute, UTC
    int64_t adds = 0, changes = 0, deletes = 0, executed_deletes = 0;
    double add_qty = 0, delete_qty = 0;
    int64_t bid_orders = 0, ask_orders = 0;
    double bid_qty_10bps = 0, ask_qty_10bps = 0;
    double best_bid = 0, best_ask = 0;           // 0 = side empty
    double order_count_imbalance = 0, qty_imbalance = 0;
    std::optional<double> cancel_to_add;         // NULL when no adds
    std::optional<double> avg_order_qty;         // NULL when no adds
    bool complete = true;                        // book synced for the whole minute
};

// In-memory per-pair order book + per-minute counters. Driven by event microtimestamps
// (exchange clock), so it is deterministic under test and on replay of the raw archive.
class MboBook {
public:
    MboBook(std::string venue, std::string symbol) : venue_(std::move(venue)), symbol_(std::move(symbol)) {}

    // Load a REST snapshot: replaces the book. A first sync / resync after desync() leaves
    // the current minute marked incomplete (desync() already did); a periodic re-snapshot
    // of a book that stayed synced keeps the minute complete.
    void load_snapshot(const Snapshot& s) {
        orders_.clear(); bids_.clear(); asks_.clear();
        for (const auto& [id, o] : s.orders) insert(id, o);
        snap_ts_ = s.micro_ts;
        synced_  = true;
    }
    // Forget the book (connection lost). Events are ignored until the next snapshot.
    void desync() { synced_ = false; minute_complete_ = false; }
    bool synced() const { return synced_; }
    int64_t snapshot_ts() const { return snap_ts_; }

    // Apply one event. Returns finished minute rows (0 or 1) that the event's time closed.
    // Events at or before the snapshot microtimestamp are already in the snapshot → skipped.
    std::vector<Mbo1m> apply(const MboEvent& e, bool* skipped = nullptr) {
        std::vector<Mbo1m> out;
        if (skipped) *skipped = false;
        if (!synced_ || e.micro_ts <= snap_ts_) { if (skipped) *skipped = true; return out; }
        roll_to(e.micro_ts, out);
        const int64_t px = px_key(e.price);
        switch (e.kind) {
        case MboKind::Created:
            ++adds_; add_qty_ += e.amount;
            erase(e.id);
            insert(e.id, BookOrder{e.is_bid, px, e.amount});
            break;
        case MboKind::Changed: {
            ++changes_;
            erase(e.id);
            if (e.amount > 0) insert(e.id, BookOrder{e.is_bid, px, e.amount});
            break;
        }
        case MboKind::Deleted: {
            ++deletes_;
            auto it = orders_.find(e.id);
            double remaining = it != orders_.end() ? it->second.amount : e.amount;
            delete_qty_ += remaining;
            // Executed (filled away) vs cancelled: a delete whose remaining amount is 0
            // was consumed by trades; Bitstamp also sends amount_traded on newer feeds.
            if (e.amount <= 0 || (e.amount_traded && *e.amount_traded > 0 && e.amount <= 1e-12))
                ++executed_deletes_;
            erase(e.id);
            break;
        }
        default: break;
        }
        return out;
    }

    // Close the current minute if `now_us` is past it (quiet market / timer path).
    std::vector<Mbo1m> tick(int64_t now_us) {
        std::vector<Mbo1m> out;
        if (cur_minute_ >= 0 && minute_of(now_us) > cur_minute_) roll_to(now_us, out);
        return out;
    }

    // Book state accessors (tests + summary).
    size_t order_count() const { return orders_.size(); }
    double best_bid() const { return bids_.empty() ? 0.0 : px_val(bids_.rbegin()->first); }
    double best_ask() const { return asks_.empty() ? 0.0 : px_val(asks_.begin()->first); }
    double qty_at(bool bid, double price) const {
        const auto& m = bid ? bids_ : asks_;
        auto it = m.find(px_key(price));
        return it == m.end() ? 0.0 : it->second.qty;
    }

    // Snapshot the current state + counters as a row for `minute_us` (no reset).
    Mbo1m summarize(int64_t minute_us) const {
        Mbo1m r;
        r.venue = venue_; r.symbol = symbol_; r.minute_us = minute_us;
        r.adds = adds_; r.changes = changes_; r.deletes = deletes_; r.executed_deletes = executed_deletes_;
        r.add_qty = add_qty_; r.delete_qty = delete_qty_;
        for (const auto& [px, lv] : bids_) r.bid_orders += lv.count;
        for (const auto& [px, lv] : asks_) r.ask_orders += lv.count;
        r.best_bid = best_bid(); r.best_ask = best_ask();
        if (r.best_bid > 0 && r.best_ask > 0) {
            const double mid = 0.5 * (r.best_bid + r.best_ask);
            const int64_t lo = px_key(mid * (1.0 - 0.001)), hi = px_key(mid * (1.0 + 0.001));
            for (auto it = bids_.lower_bound(lo); it != bids_.end(); ++it) r.bid_qty_10bps += it->second.qty;
            for (auto it = asks_.begin(); it != asks_.end() && it->first <= hi; ++it) r.ask_qty_10bps += it->second.qty;
        }
        const double oc = static_cast<double>(r.bid_orders + r.ask_orders);
        r.order_count_imbalance = oc > 0 ? (r.bid_orders - r.ask_orders) / oc : 0.0;
        const double q = r.bid_qty_10bps + r.ask_qty_10bps;
        r.qty_imbalance = q > 0 ? (r.bid_qty_10bps - r.ask_qty_10bps) / q : 0.0;
        if (adds_ > 0) {
            r.cancel_to_add = static_cast<double>(deletes_ - executed_deletes_) / adds_;
            r.avg_order_qty = add_qty_ / adds_;
        }
        r.complete = minute_complete_;
        return r;
    }

    static int64_t minute_of(int64_t us) { return us / 60'000'000; }

private:
    struct Level { double qty = 0; int64_t count = 0; };

    void insert(uint64_t id, const BookOrder& o) {
        orders_[id] = o;
        auto& lv = (o.is_bid ? bids_ : asks_)[o.px];
        lv.qty += o.amount; ++lv.count;
    }
    void erase(uint64_t id) {
        auto it = orders_.find(id);
        if (it == orders_.end()) return;
        auto& side = it->second.is_bid ? bids_ : asks_;
        auto lv = side.find(it->second.px);
        if (lv != side.end()) {
            lv->second.qty -= it->second.amount;
            if (--lv->second.count <= 0) side.erase(lv);
            else if (lv->second.qty < 1e-12) lv->second.qty = 0;
        }
        orders_.erase(it);
    }
    void roll_to(int64_t ts_us, std::vector<Mbo1m>& out) {
        const int64_t m = minute_of(ts_us);
        if (cur_minute_ < 0) { cur_minute_ = m; return; }
        if (m <= cur_minute_) return;
        out.push_back(summarize(cur_minute_ * 60'000'000));
        adds_ = changes_ = deletes_ = executed_deletes_ = 0;
        add_qty_ = delete_qty_ = 0;
        minute_complete_ = synced_;
        cur_minute_ = m;
    }

    std::string venue_, symbol_;
    std::unordered_map<uint64_t, BookOrder> orders_;
    std::map<int64_t, Level> bids_, asks_;
    int64_t snap_ts_ = 0;
    bool synced_ = false;
    bool minute_complete_ = false;
    int64_t cur_minute_ = -1;
    int64_t adds_ = 0, changes_ = 0, deletes_ = 0, executed_deletes_ = 0;
    double add_qty_ = 0, delete_qty_ = 0;
};

}  // namespace crypto
