/*  paper_quote.hpp — top-of-book state shared by the paper brokers.

    The collector records BestBidOffer (bid/ask + sizes) in `bbo`; the paper
    runner streams those rows to every broker and trend strategy alongside the
    ticks. From one quote we derive:
      spread_ticks   (ask − bid) / tick
      imbalance      bid_size / (bid_size + ask_size)   0.5 = balanced
      microprice     size-weighted price — leans toward the heavier side
    and three ENTRY GATES an OrbConfig can switch on (all off by default, so
    every existing strategy is untouched):
      spread_gate_ticks   block when spread > N ticks
      spread_gate_rel     block when spread > k × its own rolling mean
      imbalance_min       longs need bid share ≥ x, shorts need ≤ 1 − x
      microprice_lead     longs need microprice ≥ mid, shorts ≤ mid
    plus the FILL MODEL:
      fill_model "last_slip" (default) — last price ± slippage_ticks
      fill_model "bbo"                 — buys at the ask, sells at the bid
    Whatever the fill model, every trade also carries the bid/ask SHADOW fill,
    so the cost of the book is measured on the same trade, not on a sibling.
*/
#pragma once
#include "orb_config.hpp"
#include <algorithm>
#include <cstdint>
#include <string>

namespace paper {

struct Quote {
    int64_t ts_us = 0;
    double  bid = 0.0, ask = 0.0;
    int     bid_sz = 0, ask_sz = 0;
    bool   valid() const { return bid > 0.0 && ask >= bid; }
    double mid() const { return (bid + ask) / 2.0; }
    double spread_ticks(double tick) const { return tick > 0.0 ? (ask - bid) / tick : 0.0; }
    double imbalance() const { const int n = bid_sz + ask_sz; return n > 0 ? (double)bid_sz / n : 0.5; }
    double microprice() const {
        const int n = bid_sz + ask_sz;
        return n > 0 ? (bid * ask_sz + ask * bid_sz) / (double)n : mid();
    }
};

struct QuoteState {
    Quote  q;
    double spread_ema = 0.0;                     // ticks, slow EMA (≈ last 50 quotes)
    static constexpr int64_t kFreshUs = 5'000'000;   // a quote older than 5 s is not a book

    void on_quote(const Quote& n, double tick) {
        q = n;
        const double s = n.spread_ticks(tick);
        spread_ema = spread_ema <= 0.0 ? s : spread_ema + 0.02 * (s - spread_ema);
    }
    bool fresh(int64_t ts_us) const {
        return q.valid() && ts_us - q.ts_us <= kFreshUs && q.ts_us - ts_us <= kFreshUs;
    }
    bool any_gate(const OrbConfig& c) const {
        return c.spread_gate_ticks > 0.0 || c.spread_gate_rel > 0.0 || c.imbalance_min > 0.0 || c.microprice_lead || c.imbalance_max > 0.0;
    }
    // Does the book lean the way of a position/entry in direction dir? (bid-heavy for longs)
    bool agrees(int dir) const { const double im = q.imbalance(); return dir > 0 ? im >= 0.55 : im <= 0.45; }
    // Fill timing: ready when the spread is one tick or the microprice already leans our way.
    bool fill_ready(int dir, int64_t ts_us, double tick) const {
        if (!fresh(ts_us)) return true;                       // no book → don't wait forever
        if (q.spread_ticks(tick) <= 1.0 + 1e-9) return true;
        const double d = q.microprice() - q.mid();
        return dir > 0 ? d > 0.0 : d < 0.0;
    }
    // In-position book exits: "" = keep, else the exit reason.
    std::string book_exit(const OrbConfig& c, int pos_dir, int64_t ts_us, double pnl_pts) const {
        if (!fresh(ts_us)) return "";
        const double im = q.imbalance();
        if (c.book_exit_flip > 0.0 && ((pos_dir > 0 && im <= c.book_exit_flip) || (pos_dir < 0 && im >= 1.0 - c.book_exit_flip)))
            return "book_flip";
        if (c.book_tp_imbalance > 0.0 && pnl_pts >= c.book_tp_min_pts &&
            ((pos_dir > 0 && im >= c.book_tp_imbalance) || (pos_dir < 0 && im <= 1.0 - c.book_tp_imbalance)))
            return "book_tp";
        return "";
    }
    bool flipped_against(int pos_dir, int64_t ts_us) const {
        if (!fresh(ts_us)) return false;
        const double im = q.imbalance();
        return pos_dir > 0 ? im <= 0.45 : im >= 0.55;
    }
    // "" = entry allowed; otherwise the blocking reason.
    std::string gate(const OrbConfig& c, int dir, int64_t ts_us, double tick) const {
        if (!any_gate(c)) return "";
        if (!fresh(ts_us)) return "no_quote";
        const double sp = q.spread_ticks(tick);
        if (c.spread_gate_ticks > 0.0 && sp > c.spread_gate_ticks) return "spread_ticks";
        if (c.spread_gate_rel > 0.0 && spread_ema > 0.0 && sp > c.spread_gate_rel * spread_ema) return "spread_rel";
        if (c.imbalance_min > 0.0) {
            const double im = q.imbalance();
            if (dir > 0 && im < c.imbalance_min) return "imbalance";
            if (dir < 0 && im > 1.0 - c.imbalance_min) return "imbalance";
        }
        if (c.microprice_lead) {
            const double d = q.microprice() - q.mid();
            if (dir > 0 && d < 0.0) return "microprice";
            if (dir < 0 && d > 0.0) return "microprice";
        }
        if (c.imbalance_max > 0.0) {                          // inverted: only when the book leans AGAINST the entry
            const double im = q.imbalance();
            if (dir > 0 && im > c.imbalance_max) return "imbalance_inv";
            if (dir < 0 && im < 1.0 - c.imbalance_max) return "imbalance_inv";
        }
        return "";
    }
    // Honest fill for a market order in direction dir (buy at ask / sell at bid),
    // or `fallback` when there is no fresh book. `adverse_vs` (a stop-based fill)
    // is folded in: a stop-market never fills better than the touch.
    double market_fill(int dir, int64_t ts_us, double fallback, double adverse_vs = 0.0) const {
        if (!fresh(ts_us)) return fallback;
        double f = dir > 0 ? q.ask : q.bid;
        if (adverse_vs > 0.0) f = dir > 0 ? std::max(f, adverse_vs) : std::min(f, adverse_vs);
        return f;
    }
};

}  // namespace paper
