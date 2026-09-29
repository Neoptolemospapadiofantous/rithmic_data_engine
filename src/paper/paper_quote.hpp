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
#include <cmath>
#include <cstdint>
#include <deque>
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

// ─── Regime gate ───────────────────────────────────────────────────────────────
// The session's shape so far, from the RTH open (09:30 ET) of the current ET trade
// date: open, high, low, last. Plus the prior day's ATR14 (session_stats), which the
// host sets per date. From those, three intraday readings a strategy can gate on —
// all known at entry time, nothing from later in the day:
//   range_atr  (high − low) / ATR              how much the day has expanded
//   eff        |last − open| / (high − low)     directional efficiency, 0..1
//   move_atr   (last − open) / ATR              signed distance from the open
// OrbConfig knobs (all off by default, so every existing strategy is untouched):
//   regime_min_range_atr / regime_max_range_atr
//   regime_min_eff / regime_max_eff
//   regime_min_move_atr / regime_max_move_atr   (absolute distance from the open)
//   regime_with_move = 1  entries only in the direction of the session move
//   regime_with_move = -1 entries only AGAINST it (fade)
//   regime_min_minutes    readings need this many minutes after 09:30 (default 15)
// Why here and not in the strategies: the same gate has to apply to ORB, the trend
// modes and the scalper, live and paper alike, without touching strategy logic.
struct RegimeState {
    std::string date;              // ET trade date the readings belong to
    double atr_pts = 0.0;          // prior-day ATR14 for `date` (0 = unknown)
    double open = 0.0, high = 0.0, low = 0.0, last = 0.0;
    int64_t open_us = 0;           // first RTH tick
    int    minutes = 0;            // minutes since the RTH open, from the last tick
    int64_t last_us = 0;
    // Tick volume per closed minute (any hour), newest last — the relative-volume gate's tape.
    std::deque<double> vol_hist;
    double vol_cur = 0.0; int vol_cur_min = -1;

    void set_atr(const std::string& d, double atr) { if (d != date) reset(d); atr_pts = atr; }
    void reset(const std::string& d) { date = d; open = high = low = last = 0.0; open_us = 0; minutes = 0; last_us = 0; }

    // Feed every tick. `d` = ET trade date, `et_hour/et_min` = the tick's ET wall time.
    void on_tick(const std::string& d, int et_hour, int et_min, int64_t ts_us, double price, double size = 0.0) {
        if (d != date) { const double a = atr_pts; reset(d); atr_pts = 0.0; (void)a; }
        // minute volume: close the previous minute when the clock moves on (volume history is
        // NOT reset by the date roll — relative volume compares against the recent tape)
        const int key = et_hour * 60 + et_min;
        if (key != vol_cur_min) {
            if (vol_cur_min >= 0) { vol_hist.push_back(vol_cur); if (vol_hist.size() > 120) vol_hist.pop_front(); }
            vol_cur = 0.0; vol_cur_min = key;
        }
        vol_cur += size;
        const int hhmm = et_hour * 100 + et_min;
        if (hhmm < 930 || hhmm >= 1600) return;         // RTH only
        if (open_us == 0) { open = high = low = price; open_us = ts_us; }
        if (price > high) high = price;
        if (price < low)  low  = price;
        last = price; last_us = ts_us;
        minutes = (int)((ts_us - open_us) / 60'000'000LL);
    }
    bool has_open() const { return open_us != 0; }
    double range_pts() const { return has_open() ? high - low : 0.0; }
    double range_atr() const { return atr_pts > 0.0 && has_open() ? (high - low) / atr_pts : 0.0; }
    double eff() const { const double r = range_pts(); return r > 0.0 ? std::fabs(last - open) / r : 0.0; }
    double move_atr() const { return atr_pts > 0.0 && has_open() ? (last - open) / atr_pts : 0.0; }

    // Exit-side regime: the trail step / take-profit in force for a config right now.
    // Off (regime_exit_min_eff = 0) or no session yet → the plain knobs.
    static bool exit_trend(const OrbConfig& c, const RegimeState* r) {
        return c.regime_exit_min_eff > 0.0 && r && r->has_open() && r->eff() >= c.regime_exit_min_eff;
    }
    static double trail_step_for(const OrbConfig& c, const RegimeState* r) {
        if (c.regime_exit_min_eff > 0.0 && r && r->has_open()) {
            const double v = exit_trend(c, r) ? c.trail_step_trend : c.trail_step_range;
            if (v > 0.0) return v;
        }
        return c.trail_step;
    }
    static double take_profit_for(const OrbConfig& c, const RegimeState* r) {
        if (c.regime_exit_min_eff > 0.0 && r && r->has_open()) {
            if (exit_trend(c, r)) return 0.0;                              // trend: let it run
            if (c.tp_r_range > 0.0) return c.tp_r_range * c.sl_points;     // range: take the target
        }
        return c.take_profit_pts();
    }
    // Relative volume: last closed minute / mean of the `bars` closed minutes before it.
    // Negative when fewer than bars+1 closed minutes exist.
    double rvol(int bars) const {
        const int n = (int)vol_hist.size();
        if (bars <= 0 || n < bars + 1) return -1.0;
        double sum = 0.0; for (int i = n - 1 - bars; i < n - 1; ++i) sum += vol_hist[i];
        const double avg = sum / bars;
        return avg > 0.0 ? vol_hist[n - 1] / avg : -1.0;
    }
    // Volatility-targeted size for a config: vt_risk_usd / (prior-day ATR14 × point value),
    // clamped to [1, vt_qty_max]; the plain `qty` when off or the ATR is unknown.
    static int qty_for(const OrbConfig& c, const RegimeState* r) {
        if (c.vt_risk_usd <= 0.0 || !r || r->atr_pts <= 0.0 || c.point_value <= 0.0) return c.qty;
        const int q = (int)std::lround(c.vt_risk_usd / (r->atr_pts * c.point_value));
        return std::max(1, std::min(std::max(1, c.vt_qty_max), q));
    }
    static bool any_gate(const OrbConfig& c) {
        return c.regime_min_range_atr > 0.0 || c.regime_max_range_atr > 0.0 ||
               c.regime_min_eff > 0.0 || c.regime_max_eff > 0.0 ||
               c.regime_min_move_atr > 0.0 || c.regime_max_move_atr > 0.0 || c.regime_with_move != 0 ||
               c.rvol_min > 0.0 || c.rvol_max > 0.0;
    }
    // "" = entry allowed; otherwise the blocking reason. Fail-CLOSED: a variant that asks
    // for an ATR reading and has none (no session_stats row yet) does not trade — an
    // ungated fallback would silently turn the variant into its base.
    std::string gate(const OrbConfig& c, int dir) const {
        if (!any_gate(c)) return "";
        // relative volume first: it needs no session open and no ATR (any hour of the tape)
        if (c.rvol_min > 0.0 || c.rvol_max > 0.0) {
            const double rv = rvol(c.rvol_bars);
            if (rv < 0.0) return "rvol_warmup";
            if (c.rvol_min > 0.0 && rv < c.rvol_min) return "rvol_low";
            if (c.rvol_max > 0.0 && rv > c.rvol_max) return "rvol_high";
        }
        const bool session_gate = c.regime_min_range_atr > 0.0 || c.regime_max_range_atr > 0.0 ||
                                  c.regime_min_eff > 0.0 || c.regime_max_eff > 0.0 ||
                                  c.regime_min_move_atr > 0.0 || c.regime_max_move_atr > 0.0 || c.regime_with_move != 0;
        if (!session_gate) return "";
        if (!has_open()) return "regime_no_open";
        if (minutes < c.regime_min_minutes) return "regime_warmup";
        const bool needs_atr = c.regime_min_range_atr > 0.0 || c.regime_max_range_atr > 0.0 ||
                               c.regime_min_move_atr > 0.0 || c.regime_max_move_atr > 0.0;
        if (needs_atr && atr_pts <= 0.0) return "regime_no_atr";
        const double ra = range_atr(), e = eff(), mv = move_atr();
        if (c.regime_min_range_atr > 0.0 && ra < c.regime_min_range_atr) return "regime_range_low";
        if (c.regime_max_range_atr > 0.0 && ra > c.regime_max_range_atr) return "regime_range_high";
        if (c.regime_min_eff > 0.0 && e < c.regime_min_eff) return "regime_eff_low";
        if (c.regime_max_eff > 0.0 && e > c.regime_max_eff) return "regime_eff_high";
        if (c.regime_min_move_atr > 0.0 && std::fabs(mv) < c.regime_min_move_atr) return "regime_move_small";
        if (c.regime_max_move_atr > 0.0 && std::fabs(mv) > c.regime_max_move_atr) return "regime_move_extended";
        if (c.regime_with_move != 0) {
            const int side = last > open ? 1 : last < open ? -1 : 0;
            if (side == 0) return "regime_no_move";
            if (c.regime_with_move > 0 && side != dir) return "regime_against_move";
            if (c.regime_with_move < 0 && side == dir) return "regime_with_move";
        }
        return "";
    }
};

}  // namespace paper
