#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    mbo_feed.hpp — Databento MBO files → trades + reconstructed top of book

    Turns market-by-order history (GLBX.MDP3, NQ.c.0) into the same two streams
    the paper engine reads from Postgres:
      ticks   one per MBO trade record (action 'T'): price, size in contracts,
              is_buy = the AGGRESSOR bought (Databento: side 'B' on a trade = a
              buy aggressor, verified against the book: those prints sit at the ask)
      quotes  top of book (bid/ask price + size) rebuilt from adds / cancels /
              modifies / clears per instrument_id, emitted at event boundaries
              (F_LAST) only — a quote is never a half-applied event.

    Quote emission (keeps the stream close to the collector's throttled BBO and
    the paper engine's per-quote cost bounded — the raw top of book changes ~14×
    per trade on NQ, 105 M times in 2026-02): a changed top of book is held and
    emitted right before the next trade (so a gate deciding on that trade sees
    the exact book) or at once when no quote went out for ≥ 1 s (keeps the book
    "fresh" through quiet stretches). ≈ 1 quote per trade.
    Timestamps are one monotonic microsecond clock across both streams, and a
    quote produced by the same event as a trade is stamped ≥ trade + 1 µs, so the
    post-trade book is never visible to the trade that caused it.

    Continuous-contract roll: the instrument_id changes (NQ.c.0 maps to the next
    contract). Each instrument keeps its own book; the active one is the
    instrument of the latest record. Databento re-sends a clear ('R') + snapshot
    at every session start, so a new contract's book is complete from its first
    session.

    Optional per-minute aggregation (MinuteRow) for the chart's order-flow
    history: order-event counts, resting orders, depth near the mid, top-of-book
    stats and the minute's trade volume / aggressor-buy volume.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "history/dbn.hpp"

#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace hist {

struct Tick  { int64_t ts_us; double price; int64_t size; bool is_buy; };
struct Quote { int64_t ts_us; double bid, ask; int bid_sz, ask_sz; };

// A file-backed replay source for paper_engine (MBO here, Binance aggTrades in binance_feed.hpp).
class TickFeed {
public:
    virtual ~TickFeed() = default;
    // Appends up to max_ticks ticks (+ the quotes produced on the way); false when exhausted.
    virtual bool next_batch(std::vector<Tick>& ticks, std::deque<Quote>& quotes, size_t max_ticks) = 0;
    virtual std::string summary() const = 0;
    void set_log(std::function<void(const std::string&)> f) { log_ = std::move(f); }
protected:
    std::function<void(const std::string&)> log_;
};

// ── Order book for one instrument ────────────────────────────────────────────
class MboBook {
public:
    struct Level { uint64_t qty = 0; uint32_t orders = 0; };
    struct Counters { uint64_t adds = 0, changes = 0, deletes = 0, executed_deletes = 0, add_qty = 0, delete_qty = 0,
                      unknown_cancel = 0, unknown_modify = 0, clears = 0; };

    void clear() { orders_.clear(); bids_.clear(); asks_.clear(); bid_orders_ = ask_orders_ = 0; }

    // Applies one MBO record (T/F/N leave the book unchanged; F marks the order as filled).
    void apply(const dbn::MboMsg& m, Counters& c) {
        switch (m.action) {
        case 'R': clear(); ++c.clears; break;
        case 'A': add(m.order_id, m.price, m.size, m.side); ++c.adds; c.add_qty += m.size; break;
        case 'C': {
            auto it = orders_.find(m.order_id);
            if (it == orders_.end()) { ++c.unknown_cancel; break; }
            Order& o = it->second;
            const uint32_t k = m.size < o.size ? m.size : o.size;
            level(o.side, o.price).qty -= k;
            o.size -= k;
            ++c.deletes; c.delete_qty += k;
            if (o.size == 0) {
                if (o.filled) ++c.executed_deletes;
                remove_order(it);
            }
            break;
        }
        case 'M': {
            ++c.changes;
            auto it = orders_.find(m.order_id);
            if (it == orders_.end()) { ++c.unknown_modify; add(m.order_id, m.price, m.size, m.side); break; }
            Order& o = it->second;
            if (o.price != m.price || o.side != m.side) {
                const bool filled = o.filled;
                remove_order(it);
                if (m.size > 0) { add(m.order_id, m.price, m.size, m.side); orders_[m.order_id].filled = filled; }
            } else {
                Level& lv = level(o.side, o.price);
                lv.qty = lv.qty - o.size + m.size;
                o.size = m.size;
                if (o.size == 0) remove_order(it);
            }
            break;
        }
        case 'F': { auto it = orders_.find(m.order_id); if (it != orders_.end()) it->second.filled = true; break; }
        default: break;   // 'T' trade, 'N' none — no book change
        }
    }

    bool has_bid() const { return !bids_.empty(); }
    bool has_ask() const { return !asks_.empty(); }
    int64_t best_bid() const { return bids_.empty() ? dbn::kUndefPrice : bids_.rbegin()->first; }
    int64_t best_ask() const { return asks_.empty() ? dbn::kUndefPrice : asks_.begin()->first; }
    uint64_t bid_size() const { return bids_.empty() ? 0 : bids_.rbegin()->second.qty; }
    uint64_t ask_size() const { return asks_.empty() ? 0 : asks_.begin()->second.qty; }
    uint64_t bid_orders() const { return bid_orders_; }
    uint64_t ask_orders() const { return ask_orders_; }
    size_t   order_count() const { return orders_.size(); }
    // Resting quantity priced within `frac` of the mid (e.g. 0.001 = 10 bps), per side.
    void depth_near_mid(double frac, uint64_t& bid_q, uint64_t& ask_q) const {
        bid_q = ask_q = 0;
        if (bids_.empty() || asks_.empty()) return;
        const double mid = 0.5 * ((double)best_bid() + (double)best_ask());
        const double lo = mid * (1.0 - frac), hi = mid * (1.0 + frac);
        for (auto it = bids_.rbegin(); it != bids_.rend() && (double)it->first >= lo; ++it) bid_q += it->second.qty;
        for (auto it = asks_.begin(); it != asks_.end() && (double)it->first <= hi; ++it) ask_q += it->second.qty;
    }

private:
    struct Order { int64_t price; uint32_t size; char side; bool filled; };
    Level& level(char side, int64_t price) { return side == 'B' ? bids_[price] : asks_[price]; }
    void add(uint64_t id, int64_t price, uint32_t size, char side) {
        if (side != 'B' && side != 'A') return;
        if (size == 0 || price == dbn::kUndefPrice) return;
        auto [it, fresh] = orders_.try_emplace(id, Order{price, size, side, false});
        if (!fresh) {                       // duplicate id: treat as replace
            remove_order(it);
            it = orders_.try_emplace(id, Order{price, size, side, false}).first;
        }
        Level& lv = level(side, price); lv.qty += size; ++lv.orders;
        (side == 'B' ? bid_orders_ : ask_orders_)++;
    }
    void remove_order(std::unordered_map<uint64_t, Order>::iterator it) {
        const Order& o = it->second;
        auto& side_map = o.side == 'B' ? bids_ : asks_;
        auto lit = side_map.find(o.price);
        if (lit != side_map.end()) {
            lit->second.qty -= std::min<uint64_t>(lit->second.qty, o.size);
            if (lit->second.orders) --lit->second.orders;
            if (lit->second.orders == 0) side_map.erase(lit);
        }
        (o.side == 'B' ? bid_orders_ : ask_orders_)--;
        orders_.erase(it);
    }

    std::unordered_map<uint64_t, Order> orders_;
    std::map<int64_t, Level> bids_, asks_;
    uint64_t bid_orders_ = 0, ask_orders_ = 0;
};

// One UTC minute of order flow (see migrations/20261010_mbo_1m.sql + scripts/sql/book_1m.sql).
struct MinuteRow {
    int64_t  minute_us = 0;                 // minute start, UTC
    // order events (mbo_1m)
    uint64_t adds = 0, changes = 0, deletes = 0, executed_deletes = 0, add_qty = 0, delete_qty = 0;
    uint64_t bid_orders = 0, ask_orders = 0, bid_qty_near = 0, ask_qty_near = 0;
    double   best_bid = 0.0, best_ask = 0.0;   // end of minute, 0 = side empty
    bool     complete = true;
    // trades
    uint64_t trades = 0, volume = 0, buy_volume = 0;
    // emitted quotes (book_1m semantics)
    uint64_t quotes = 0; double spread_sum = 0, spread_max = 0, imb_sum = 0, bid_heavy = 0, ask_heavy = 0,
             bid_sz_sum = 0, ask_sz_sum = 0, mid_open = 0, mid_close = 0;
};

// ── Streaming feed over one or more MBO files ────────────────────────────────
class MboFeed : public TickFeed {
public:
    struct Stats {
        uint64_t records = 0, trades = 0, trade_volume = 0, side_n_trades = 0, quotes = 0, instrument_switches = 0,
                 snapshots = 0, clamped_ts = 0, crossed_books = 0, unsynced_records = 0;
        MboBook::Counters book;
    };

    // Ticks/quotes with ts in (from_us, to_us] are emitted; the book is built from every
    // record of every file (the weekly snapshot sits at the start of a file).
    MboFeed(std::vector<std::string> files, int64_t from_us, int64_t to_us, double tick_size = 0.25)
        : files_(std::move(files)), from_us_(from_us), to_us_(to_us), tick_(tick_size) {}

    void set_minute_sink(std::function<void(const MinuteRow&)> f, double near_frac = 0.001) {
        minute_sink_ = std::move(f); near_frac_ = near_frac;
    }
    // Appends up to `max_ticks` ticks; quotes produced on the way are appended to `quotes`
    // (time order, interleaved with the ticks by timestamp). Returns false when the stream
    // is exhausted (or past to_us) and nothing was added.
    bool next_batch(std::vector<Tick>& ticks, std::deque<Quote>& quotes, size_t max_ticks) override {
        const size_t t0 = ticks.size(), q0 = quotes.size();
        while (!done_ && ticks.size() - t0 < max_ticks) {
            const dbn::RecordHeader* h = next_record();
            if (!h) { done_ = true; break; }
            const dbn::MboMsg* m = dbn::Reader::as_mbo(h);
            if (!m) continue;
            step(*m, ticks, quotes);
        }
        if (done_) finish_minute();
        return ticks.size() > t0 || quotes.size() > q0;
    }
    bool done() const { return done_; }
    const Stats& stats() const { return st_; }
    std::string summary() const override {
        char b[512];
        std::snprintf(b, sizeof b, "MBO feed: %llu records, %llu trades (%llu contracts), %llu quotes, %llu side-N trades, "
                      "%llu clamped timestamps, %llu crossed-book events skipped, %llu instrument switch(es), "
                      "%llu records before a book snapshot (no quotes)",
                      (unsigned long long)st_.records, (unsigned long long)st_.trades, (unsigned long long)st_.trade_volume,
                      (unsigned long long)st_.quotes, (unsigned long long)st_.side_n_trades, (unsigned long long)st_.clamped_ts,
                      (unsigned long long)st_.crossed_books, (unsigned long long)st_.instrument_switches,
                      (unsigned long long)st_.unsynced_records);
        return b;
    }
    // Drain the whole stream (minute stats only, nothing kept). Used by dbn_tool mbo-1m.
    void run_all() {
        std::vector<Tick> t; std::deque<Quote> q;
        while (next_batch(t, q, 100000)) { t.clear(); q.clear(); }
    }

private:
    const dbn::RecordHeader* next_record() {
        for (;;) {
            if (!rd_) {
                if (fi_ >= files_.size()) return nullptr;
                rd_ = std::make_unique<dbn::Reader>(files_[fi_++]);
                if (log_) log_("[MBO] reading " + rd_->path());
                // Every file is its own download: books start empty and stay "unsynced" (no quotes)
                // until a clear ('R', followed by Databento's snapshot). A monthly file starts with one;
                // a partial download (2026-03-partial-update) starts mid-session and waits for the next.
                books_.clear(); book_ = nullptr; active_ = 0; synced_.clear(); pending_ = false;
            }
            const dbn::RecordHeader* h = rd_->next();
            if (h) { ++st_.records; return h; }
            rd_.reset();
        }
    }
    static int64_t minute_of(int64_t ts_us) { return ts_us - (((ts_us % 60'000'000LL) + 60'000'000LL) % 60'000'000LL); }
    bool in_window(int64_t ts_us) const { return ts_us > from_us_ && ts_us <= to_us_; }

    void step(const dbn::MboMsg& m, std::vector<Tick>& ticks, std::deque<Quote>& quotes) {
        const int64_t ts_us = (int64_t)(m.hd.ts_event / 1000);
        if (ts_us > to_us_) { done_ = true; return; }
        const uint32_t iid = m.hd.instrument_id;
        if (iid != active_) {
            if (active_ != 0) {
                ++st_.instrument_switches;
                if (log_) log_("[MBO] instrument " + std::to_string(active_) + " -> " + std::to_string(iid) + " at " +
                               std::to_string(ts_us) + " us (continuous roll)");
                pending_ = false; cur_.complete = false;
            }
            active_ = iid;
            book_ = &books_[iid];
        }
        if (m.action == 'R') synced_[iid] = true;
        const bool synced = synced_[iid];
        if (!synced) { ++st_.unsynced_records; cur_.complete = false; }
        if (minute_sink_) roll_minute(ts_us);
        if (m.flags & dbn::kFlagSnapshot) { ++st_.snapshots; cur_.complete = false; }
        if (m.action == 'R') cur_.complete = false;

        MboBook::Counters before = st_.book;
        book_->apply(m, st_.book);
        if (minute_sink_) {
            cur_.adds += st_.book.adds - before.adds;
            cur_.changes += st_.book.changes - before.changes;
            cur_.deletes += st_.book.deletes - before.deletes;
            cur_.executed_deletes += st_.book.executed_deletes - before.executed_deletes;
            cur_.add_qty += st_.book.add_qty - before.add_qty;
            cur_.delete_qty += st_.book.delete_qty - before.delete_qty;
        }

        if (m.action == 'T' && m.size > 0 && m.price != dbn::kUndefPrice) {
            bool is_buy;
            if (m.side == 'B') is_buy = true;
            else if (m.side == 'A') is_buy = false;
            else {                                   // no aggressor side: infer from the book
                ++st_.side_n_trades;
                is_buy = book_->has_ask() && m.price >= book_->best_ask();
            }
            ++st_.trades; st_.trade_volume += m.size;
            if (minute_sink_) { ++cur_.trades; cur_.volume += m.size; if (is_buy) cur_.buy_volume += m.size; }
            if (in_window(ts_us)) {
                if (pending_) emit_quote(pending_q_, quotes, false);
                int64_t t = ts_us;
                if (t < clk_) { t = clk_; ++st_.clamped_ts; }
                clk_ = t; last_was_tick_ = true;
                ticks.push_back(Tick{t, dbn::px(m.price), (int64_t)m.size, is_buy});
            }
        }

        if (!(m.flags & dbn::kFlagLast)) return;      // mid-event: the book is not consistent yet
        if (!synced) return;                          // no snapshot yet: the book is incomplete
        if (!book_->has_bid() || !book_->has_ask()) return;
        const int64_t bp = book_->best_bid(), ap = book_->best_ask();
        if (bp > ap) { ++st_.crossed_books; return; }
        Quote q{ts_us, dbn::px(bp), dbn::px(ap), (int)book_->bid_size(), (int)book_->ask_size()};
        const bool px_change = bp != last_bp_ || ap != last_ap_;
        const bool sz_change = q.bid_sz != last_q_.bid_sz || q.ask_sz != last_q_.ask_sz;
        if (!px_change && !sz_change) return;
        if (!in_window(ts_us)) { last_bp_ = bp; last_ap_ = ap; last_q_ = q; return; }
        last_bp_ = bp; last_ap_ = ap;
        if (ts_us - last_emit_us_ >= 1'000'000LL) emit_quote(q, quotes, true);
        else { pending_ = true; pending_q_ = q; last_q_ = q; }
    }

    void emit_quote(Quote q, std::deque<Quote>& quotes, bool fresh) {
        int64_t t = q.ts_us;
        const int64_t floor = last_was_tick_ ? clk_ + 1 : clk_;
        if (t < floor) t = floor;
        q.ts_us = t; clk_ = t; last_was_tick_ = false;
        quotes.push_back(q);
        pending_ = false; last_emit_us_ = t; last_q_ = q;
        (void)fresh;
        ++st_.quotes;
        if (minute_sink_) {
            const double sp = q.ask - q.bid, mid = 0.5 * (q.bid + q.ask);
            const int n = q.bid_sz + q.ask_sz;
            const double imb = n > 0 ? (double)q.bid_sz / n : 0.5;
            if (cur_.quotes == 0) cur_.mid_open = mid;
            cur_.mid_close = mid;
            ++cur_.quotes; cur_.spread_sum += sp; if (sp > cur_.spread_max) cur_.spread_max = sp;
            cur_.imb_sum += imb; if (imb >= 0.65) cur_.bid_heavy += 1; if (imb <= 0.35) cur_.ask_heavy += 1;
            cur_.bid_sz_sum += q.bid_sz; cur_.ask_sz_sum += q.ask_sz;
        }
    }

    void roll_minute(int64_t ts_us) {
        const int64_t mn = minute_of(ts_us);
        if (!min_started_) { min_started_ = true; cur_ = MinuteRow{}; cur_.minute_us = mn; cur_.complete = false; cur_open_ = true; return; }  // first minute is partial
        if (cur_open_ && mn <= cur_.minute_us) return;
        finish_minute();
        cur_ = MinuteRow{}; cur_.minute_us = mn; cur_open_ = true;
    }
    void finish_minute() {
        if (!minute_sink_ || !cur_open_ || !book_) return;
        cur_open_ = false;
        if (cur_.minute_us + 60'000'000LL <= from_us_ || cur_.minute_us > to_us_) return;
        cur_.bid_orders = book_->bid_orders(); cur_.ask_orders = book_->ask_orders();
        book_->depth_near_mid(near_frac_, cur_.bid_qty_near, cur_.ask_qty_near);
        cur_.best_bid = book_->has_bid() ? dbn::px(book_->best_bid()) : 0.0;
        cur_.best_ask = book_->has_ask() ? dbn::px(book_->best_ask()) : 0.0;
        if (cur_.adds || cur_.changes || cur_.deletes || cur_.trades) minute_sink_(cur_);
    }

    std::vector<std::string> files_;
    size_t fi_ = 0;
    std::unique_ptr<dbn::Reader> rd_;
    int64_t from_us_, to_us_;
    double  tick_;
    bool    done_ = false;
    std::unordered_map<uint32_t, MboBook> books_;
    std::unordered_map<uint32_t, bool> synced_;
    uint32_t active_ = 0;
    MboBook* book_ = nullptr;
    Stats st_;
    int64_t clk_ = 0; bool last_was_tick_ = false;
    int64_t last_bp_ = 0, last_ap_ = 0, last_emit_us_ = 0;
    Quote   last_q_{0, 0, 0, 0, 0};
    bool    pending_ = false; Quote pending_q_{0, 0, 0, 0, 0};
    std::function<void(const MinuteRow&)> minute_sink_;
    double near_frac_ = 0.001;
    MinuteRow cur_;
    bool min_started_ = false, cur_open_ = false;
};

} // namespace hist
