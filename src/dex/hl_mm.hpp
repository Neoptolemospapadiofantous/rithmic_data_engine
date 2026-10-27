#pragma once
// hl_mm.hpp — Hyperliquid book/trade parsing + a queue-aware market-making simulator.
//
// Research only (founder 2026-10-25, "build run and test", after the algorithmic market-making
// video). No network, no DB, no files: everything here is unit-tested by tests/dex/test_hl_mm.cpp.
// hl_recorder_main.cpp archives the raw feed; mm_sim_main.cpp replays it through Simulator.
//
// Archive line format (one JSON object per line, written by hl_recorder):
//   {"r":<recv µs>,"snap":<bool>,"m":<raw Hyperliquid ws message>}
// "snap" marks the first trades message after a (re)subscribe: Hyperliquid replays recent trades
// there, so they happened BEFORE we were listening and must never fill a simulated order.
//
// The simulator is the video's market maker: one post-only order each side of OUR fair price (mid or
// microprice), at a width we choose, leaning against inventory (Params), a fixed $ size, an inventory
// cap, a maker fee per fill. Defaults (half width 0.5 tick, no skew, mid) = join the best bid and ask. What the video leaves
// out — and what decides the result — is modelled explicitly:
//   * queue position: an order joins at the BACK of its price level (Hyperliquid's public book is
//     aggregated by level, so the order count ahead is unknown — only the size ahead is). Trades at
//     our price eat the queue ahead first; we fill only with what is left. A trade THROUGH our price
//     fills us completely.
//   * cancellations ahead of us are invisible on an aggregated book, so the queue is bounded two ways:
//     Pessimistic = every cancel came from behind us (queue ahead only shrinks by trades),
//     Optimistic  = cancels came from the whole level in proportion (queue ahead shrinks with it).
//     The truth lies between; a result that is only positive under Optimistic is not a result.
//   * latency: a new order and a cancel take effect `latency_ms` after the decision (Lighter's
//     standard accounts are deliberately ~200-300 ms; Hyperliquid blocks ~0.1-0.5 s).
//   * adverse selection: every fill records the mid 1 s / 10 s / 60 s later (markout). Spread
//     captured at the fill minus the 10 s markout loss is what the spread is really worth.

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace hl {

using json = nlohmann::json;

struct Level { double px = 0, sz = 0; };
struct Book {
    std::string coin;
    int64_t t_ms = 0;
    std::vector<Level> bids, asks;          // bids best (highest) first, asks best (lowest) first
    bool valid() const { return !bids.empty() && !asks.empty() && bids[0].px < asks[0].px; }
    double mid() const { return (bids[0].px + asks[0].px) / 2; }
    // Size-weighted fair price: leans toward the side with LESS resting size (that side is about to go).
    double microprice() const {
        const double bs = bids[0].sz, as = asks[0].sz;
        return bs + as > 0 ? (bids[0].px * as + asks[0].px * bs) / (bs + as) : mid();
    }
    // Price increment: the smallest gap between adjacent levels in this snapshot (Hyperliquid's tick
    // depends on the price's magnitude, so it is read from the book rather than configured).
    double tick() const {
        double t = 0;
        for (const auto* side : {&bids, &asks})
            for (size_t i = 1; i < side->size(); ++i) {
                const double d = std::abs((*side)[i].px - (*side)[i - 1].px);
                if (d > 0 && (t == 0 || d < t)) t = d;
            }
        const double spread = asks[0].px - bids[0].px;
        if (t == 0 || spread < t) t = spread;
        return t;
    }
    // True when `px` lies within the levels this snapshot shows (Tardis history keeps 5 per side, the
    // live feed 20): beyond them an absent level means "not shown", not "empty".
    bool visible(bool bid_side, double px) const {
        const auto& v = bid_side ? bids : asks;
        return !v.empty() && (bid_side ? px >= v.back().px - px * 1e-12 : px <= v.back().px + px * 1e-12);
    }
    // Size resting at `px` on one side (0 if the level is absent).
    double size_at(bool bid_side, double px) const {
        for (const auto& l : bid_side ? bids : asks)
            if (std::abs(l.px - px) <= px * 1e-12) return l.sz;
        return 0;
    }
};
struct Trade { std::string coin; int64_t t_ms = 0; double px = 0, sz = 0; bool buy_aggr = false; };

inline double num(const json& v) {
    if (v.is_number()) return v.get<double>();
    if (v.is_string()) return std::strtod(v.get_ref<const std::string&>().c_str(), nullptr);
    return 0;
}

// {"channel":"l2Book","data":{"coin":..,"time":ms,"levels":[[bids..],[asks..]]}}
inline std::optional<Book> parse_book(const json& m) {
    if (!m.is_object() || m.value("channel", "") != "l2Book") return std::nullopt;
    const auto& d = m["data"];
    if (!d.is_object() || !d.contains("levels") || !d["levels"].is_array() || d["levels"].size() != 2)
        return std::nullopt;
    Book b;
    b.coin = d.value("coin", "");
    b.t_ms = d.value("time", int64_t{0});
    for (int side = 0; side < 2; ++side)
        for (const auto& l : d["levels"][side]) {
            Level lv{num(l["px"]), num(l["sz"])};
            if (lv.px > 0 && lv.sz > 0) (side == 0 ? b.bids : b.asks).push_back(lv);
        }
    return b;
}

// {"channel":"trades","data":[{"coin","side":"B"|"A","px","sz","time",...}]} — side B = buyer was
// the aggressor (lifted an ask), A = seller was the aggressor (hit a bid).
inline std::vector<Trade> parse_trades(const json& m) {
    std::vector<Trade> out;
    if (!m.is_object() || m.value("channel", "") != "trades" || !m["data"].is_array()) return out;
    for (const auto& t : m["data"]) {
        Trade tr{t.value("coin", ""), t.value("time", int64_t{0}), num(t["px"]), num(t["sz"]),
                 t.value("side", "") == "B"};
        if (tr.px > 0 && tr.sz > 0) out.push_back(tr);
    }
    return out;
}

// Coin named by a raw message (l2Book data.coin, or the first trade's coin); "" if none.
inline std::string coin_of(const json& m) {
    if (!m.is_object() || !m.contains("data")) return "";
    const auto& d = m["data"];
    if (d.is_object()) return d.value("coin", "");
    if (d.is_array() && !d.empty() && d[0].is_object()) return d[0].value("coin", "");
    return "";
}

// ── simulator ───────────────────────────────────────────────────────────────
enum class CancelModel { Pessimistic, Optimistic };
enum class Fair { Mid, Micro };
// The unit the strategy's distances are measured in. Every backtest ran on Hyperliquid, whose tick is
// max(10^(floor(log10 price) − 4), 10^−(6 − szDecimals)) (5 significant figures, decimals capped by the
// coin's size precision; reproduced on live HL data for 12/12 coins, 2026-10-25). Lighter's grid is up to
// 100× finer (BNB) or 10× coarser (2Z) for the same coin, so "5 ticks" there is not what was validated:
// with Hyperliquid, distances stay in HL ticks and only the ROUNDING uses the venue's grid.
enum class TickRule { Book, Hyperliquid };

// Fill model (audit 2026-10-26, founder "fix the simulator flaws and rerun everything"). Every backtest before that
// date ran Legacy, which an independent review found optimistic in four ways; Full is the default since.
//   Legacy  — fills only on printed trades; a trade through our price fills the whole order; an order is checked for
//             post-only crossing against the FIRST book after it arrived; the end-of-day close is priced at mid.
//   NoCross — Legacy fixed except for crossed-book fills:
//             * arrival: an order that arrived before a book update is checked against the book IN FORCE when it
//               arrived (the previous one), so an order the market then runs over rests and is filled, not rejected;
//             * a trade through our price fills at most the trade's size: the aggressor reached us first, and only
//               the printed size is known to have reached our price (at the back of a visible level the prints at
//               our price have already used up the queue ahead);
//             * finish().flat_pnl closes the open position at the best bid / ask (crossing), not at the mid.
//   Full    — STRESS TEST ONLY: NoCross plus crossed-book fills — a book whose best ask is at or below our resting
//             bid (or best bid at or above our ask) fills us at our price up to the size shown. A crossed recorded
//             book mostly means our order was NOT there (a post-only seller would have been rejected, a limit seller
//             would have printed a trade the data cannot contain), so Full is a bound no real fill exceeds, not an
//             estimate (independent review 2026-10-26: NoCross −2.7 bps vs Full −3.6 on the same 32 coin-days).
// Default NoCross. Env MM_FILLS=legacy|nocross|full (read once; anything else = nocross) lets every research program
// reproduce the old numbers or run the stress test without a flag of its own.
enum class FillModel { Legacy, NoCross, Full };
inline FillModel default_fill_model() {
    static const FillModel m = [] {
        const char* e = std::getenv("MM_FILLS");
        const std::string v = e ? e : "";
        return v == "legacy" ? FillModel::Legacy : v == "full" ? FillModel::Full : FillModel::NoCross;
    }();
    return m;
}

struct Params {
    double order_usd     = 100;     // size per quote (Hyperliquid minimum order is $10)
    int    max_inv_units = 3;       // inventory cap, in quote sizes, each direction
    double maker_fee_bps = 1.5;     // Hyperliquid base perps maker fee; 0 = Lighter standard
    double taker_fee_bps = 4.5;     // paid once to flatten the final inventory
    int64_t latency_ms   = 200;
    // Split delays (−1 = latency_ms for both, the behaviour every backtest used). Lighter's Standard account puts no
    // delay on post-only placements and 300 ms on cancels/modifies (docs.lighter.xyz trading fees, 2026-10-25), so a
    // live replay sets place = network, cancel = network + 300.
    int64_t place_latency_ms  = -1;
    int64_t cancel_latency_ms = -1;
    // Requote by MODIFY instead of cancel + create (2026-10-27, research; Lighter signs SignModifyOrder). One
    // transaction instead of two through a send gate; the order keeps resting at its old price until the modify lands
    // (cancel latency — Lighter delays modifies like cancels), then rejoins the BACK of the queue at the new price
    // (post-only: a modify that would cross is rejected and the order is gone). A pull (not allowed) still cancels.
    bool modify = false;
    CancelModel cancels  = CancelModel::Pessimistic;
    // The video's strategy: quote around our own fair price, at a width we choose, leaning against inventory.
    //   bid = fair − skew·inventory − half_width   (rounded down to the tick grid, never crossing the ask)
    //   ask = fair − skew·inventory + half_width   (rounded up)
    // half_width 0.5 tick on a one-tick market = join the best bid and ask; wider = further back (fewer,
    // safer fills); on a wide market a small width quotes INSIDE the spread and is first in its queue.
    double half_width_ticks = 0.5;
    double skew_ticks       = 0;     // fair price moves this many ticks per order-size of inventory held
    Fair   fair             = Fair::Mid;
    bool   track_markouts   = true;  // off in the optimizer's grid: saves the mid timeline's memory
    // Defences against being picked off (round 3, 2026-10-25). All off by default.
    double min_spread_ticks = 0;     // quote only while the market's spread is at least this many ticks
    double imb_pull         = 0;     // > 0: when the top-of-book size imbalance (bid−ask)/(bid+ask) reaches this,
                                     // pull the side about to be run over (heavy bids → no ask; heavy asks → no bid)
    double pause_move_ticks = 0;     // > 0: a mid jump of at least this many ticks between two books …
    int64_t pause_ms        = 0;     // … pulls both quotes for this long (moves come in bursts)
    double requote_ticks    = 0;     // keep a resting order (and its queue place) until the target moves MORE
                                     // than this many ticks; 0 = follow the target exactly
    // Tick definitions. venue_tick: the exchange's real price increment (0 = infer it from the book's level
    // spacing). strat_tick_rule / hl_sz_decimals: what one "tick" means in the parameters above.
    double   venue_tick      = 0;
    TickRule strat_tick_rule = TickRule::Book;
    int      hl_sz_decimals  = 0;
    FillModel fills          = default_fill_model();
    // Venue order-size rules (mm_paper since 2026-10-26; all 0 = continuous size order_usd / mid, the behaviour every
    // backtest used). size_step: the size grid in coin (Lighter 10^−supported_size_decimals); min_base: the smallest
    // size in coin (min_base_amount); min_quote_usd: the smallest notional (Lighter min_quote_amount × the gateway's
    // 1.05 margin). The quote size is then rounded UP the way src/dex/live/market_spec.hpp size_quote does — see lot_qty.
    double size_step     = 0;
    double min_base      = 0;
    double min_quote_usd = 0;
};

// Size of one quote in coin: order_usd at `mid`, rounded UP to the size grid and raised until it clears both minimums
// (src/dex/live/market_spec.hpp smallest_valid / size_quote, in doubles: tests/dex/test_lighter.cpp checks they agree).
// No cap here: the paper engine reports what a venue-legal order would have done; the live gateway refuses a market
// whose smallest legal order exceeds its cap.
inline double lot_qty(const Params& p, double mid) {
    if (mid <= 0) return 0;
    double q = p.order_usd / mid;
    if (p.size_step <= 0) return std::max({q, p.min_base, p.min_quote_usd > 0 ? p.min_quote_usd / mid : 0.0});
    auto up = [&](double x) { return std::ceil(x / p.size_step - 1e-9) * p.size_step; };
    q = up(q);
    if (p.min_base > 0 && q < p.min_base - p.size_step * 1e-9) q = up(p.min_base);
    if (p.min_quote_usd > 0 && q * mid < p.min_quote_usd) q = up(p.min_quote_usd / mid);
    return q;
}

struct Fill { int64_t t_ms; double px, qty; int side; double mid; bool taker = false; };   // side +1 bought, -1 sold; taker = a flatten

// Why a side is (not) quoting after the last requote decision (observability, 2026-10-26 — reported only, never
// changes what the simulator does). First matching rule wins, in this order:
//   NoBook        no valid book yet (or no target price could be formed)
//   Halted        set_halted(true): risk control, kill switch, feed down / stale market
//   SpreadNarrow  the market's spread is below min_spread_ticks
//   JumpPause     inside pause_ms after a mid jump of ≥ pause_move_ticks
//   ImbalancePull top-of-book imbalance reached imb_pull against this side
//   InvCap        one more fill on this side would exceed the inventory cap
//   Kept          allowed; a resting order is kept at its old price (requote hysteresis keeps its queue place)
//   Quoting       allowed; an order is at (or is being sent to) the target price
enum QuoteReason { Quoting, NoBook, Halted, SpreadNarrow, JumpPause, ImbalancePull, InvCap, Kept };
constexpr int N_QUOTE_REASON = 8;
// lower_snake name of a reason — the text written to mm_paper_quotes / status.json.
inline const char* reason_name(QuoteReason r) {
    switch (r) {
        case Quoting: return "quoting";
        case NoBook: return "no_book";
        case Halted: return "halted";
        case SpreadNarrow: return "spread_narrow";
        case JumpPause: return "jump_pause";
        case ImbalancePull: return "imbalance_pull";
        case InvCap: return "inv_cap";
        case Kept: return "kept";
    }
    return "no_book";
}

constexpr int64_t MARKOUT_S[] = {1, 5, 10, 30, 60};   // markout horizons, seconds
constexpr int N_MARKOUT = 5;

struct Result {
    int64_t fills = 0, quotes_placed = 0, rejected_cross = 0, flattens = 0;   // flattens: taker exits (not in fills)
    int64_t quotes_modified = 0;                                                // Params::modify requotes (one tx each)
    double volume_usd = 0, fees = 0, cash = 0, inv = 0;
    double capture_bps_sum = 0;                  // Σ side·(mid−px)/px, $-weighted
    double mk1_bps_sum = 0, mk10_bps_sum = 0, mk60_bps_sum = 0;   // Σ side·(mid_later−px)/px, $-weighted
    double mk_weight = 0;                        // $ of fills that have a 60 s markout
    double mk_curve_sum[N_MARKOUT] = {};         // Σ side·(mid_later−px)/px per MARKOUT_S horizon, $-weighted
    std::vector<std::pair<int64_t, double>> equity;   // (t_ms, cash + inv·mid) once a minute
    double mtm_pnl = 0, flat_pnl = 0;            // at the last mid / after a taker flatten
    double max_dd = 0;
    int64_t first_ms = 0, last_ms = 0;
    double days() const { return last_ms > first_ms ? (last_ms - first_ms) / 86'400'000.0 : 0; }
};

class Simulator {
public:
    explicit Simulator(Params p) : p_(p) {}

    void on_book(const Book& b) {
        if (!b.valid()) return;
        if (!r_.first_ms) r_.first_ms = b.t_ms;
        r_.last_ms = std::max(r_.last_ms, b.t_ms);
        if (p_.track_markouts) mids_.push_back({b.t_ms, b.mid()});
        // Orders that arrived before this update meet the book that was in force when they arrived.
        if (p_.fills != FillModel::Legacy && have_book_) activate_and_expire(b.t_ms - 1, nullptr);
        activate_and_expire(b.t_ms, &b);
        if (p_.fills == FillModel::Full)
            for (int s = 0; s < 2; ++s) cross_fill(s == 0 ? bid_ : ask_, s == 0, b);
        for (int s = 0; s < 2; ++s) update_queue(s == 0 ? bid_ : ask_, s == 0, b);
        if (have_book_ && p_.pause_move_ticks > 0 &&
            std::abs(b.mid() - book_.mid()) >= p_.pause_move_ticks * strat_tick(b) - 1e-12)
            paused_until_ = b.t_ms + p_.pause_ms;
        book_ = b;
        have_book_ = true;
        requote(b.t_ms);
        mark(b.mid(), b.t_ms);
    }

    void on_trade(const Trade& t) {
        if (!have_book_) return;
        activate_and_expire(t.t_ms, nullptr);
        // A seller-aggressor trade can only hit bids; a buyer-aggressor trade only asks.
        Order& o = t.buy_aggr ? ask_ : bid_;
        if (!o.live || o.active_at > t.t_ms) return;
        const bool bid_side = !t.buy_aggr;
        const bool through = bid_side ? t.px < o.px - eps(o.px) : t.px > o.px + eps(o.px);
        const bool at = std::abs(t.px - o.px) <= eps(o.px);
        double fill = 0;
        if (through) {
            // The aggressor reached us first, but only the printed size is known to have reached our price.
            fill = p_.fills != FillModel::Legacy ? std::min(o.remaining, t.sz) : o.remaining;
        } else if (at) {
            double q = t.sz;
            const double take = std::min(q, o.queue_ahead);
            o.queue_ahead -= take;
            q -= take;
            if (q <= t.sz * 1e-9) q = 0;          // floating-point residue of "trade = queue ahead" is not a fill (dust, 2026-10-27)
            o.traded_at_px += t.sz;
            fill = std::min(q, o.remaining);
        }
        if (fill > 0) execute(o, bid_side, fill, t.t_ms);
    }

    // Close the books: markouts for every fill, final mark and a taker flatten.
    Result finish() {
        for (const auto& f : fills_) {
            if (f.taker) continue;                // markouts judge our resting fills, not our exits
            const double w = f.px * f.qty;
            const auto m1 = mid_at(f.t_ms + 1'000), m10 = mid_at(f.t_ms + 10'000), m60 = mid_at(f.t_ms + 60'000);
            if (!m60) continue;                   // too close to the end of the data to judge
            for (int k = 0; k < N_MARKOUT; ++k)
                r_.mk_curve_sum[k] += w * f.side * (*mid_at(f.t_ms + MARKOUT_S[k] * 1000) - f.px) / f.px * 1e4;
            r_.mk1_bps_sum  += w * f.side * (*m1 - f.px) / f.px * 1e4;
            r_.mk10_bps_sum += w * f.side * (*m10 - f.px) / f.px * 1e4;
            r_.mk60_bps_sum += w * f.side * (*m60 - f.px) / f.px * 1e4;
            r_.mk_weight    += w;
        }
        if (have_book_) {
            const double mid = book_.mid();
            r_.equity.push_back({book_.t_ms, r_.cash + r_.inv * mid});
            r_.mtm_pnl  = r_.cash + r_.inv * mid;
            r_.flat_pnl = r_.mtm_pnl - std::abs(r_.inv) * mid * p_.taker_fee_bps / 1e4;
            if (p_.fills != FillModel::Legacy && std::abs(r_.inv) > 1e-12)   // a close crosses the spread
                r_.flat_pnl -= std::abs(r_.inv) * std::abs(mid - (r_.inv > 0 ? book_.bids[0].px : book_.asks[0].px));
        }
        return r_;
    }
    // A day boundary WITHOUT a flatten (mm_paper since 2026-10-26): returns the closing day's record exactly as finish()
    // would (markouts for fills whose 60 s window the data covers; mtm / flat P&L there are cumulative since this
    // simulator started — a caller wanting the day's P&L takes the difference of equity()), then starts a new day's
    // record: fills, counters, volume, fees, markout sums, equity samples and reason counts restart, the drawdown peak
    // is re-anchored at the current equity. The position, cash, resting orders, book, jump pause, halt and overlay all
    // carry over, so equity() keeps running from the simulator's start. fills() is emptied (the caller drains first).
    Result new_day() {
        Result out = finish();
        Result n;
        n.cash = r_.cash; n.inv = r_.inv;
        r_ = n;
        fills_.clear();
        if (mids_.size() > 1) mids_.erase(mids_.begin(), mids_.end() - 1);   // the last mid stays for the next markouts
        peak_ = equity();
        reason_n_[0] = {}; reason_n_[1] = {};
        return out;
    }
    const std::vector<Fill>& fills() const { return fills_; }
    // Risk controls (mm_paper): a halted simulator cancels its quotes and places none until released.
    void set_halted(bool h, int64_t now) { halted_ = h; if (have_book_) requote(now); }
    bool halted() const { return halted_; }
    // Close the whole position NOW by crossing the spread (sell at the best bid / buy at the best ask) and
    // paying the taker fee — a daily roll, the loss stop or the kill switch. Returns the $ it cost against
    // the mid. Assumes the best level absorbs the size (true at $100-$300 positions on these books).
    double flatten_now(int64_t t) {
        if (!have_book_ || std::abs(r_.inv) < 1e-12) return 0;
        const bool sell = r_.inv > 0;
        const double px = sell ? book_.bids[0].px : book_.asks[0].px, qty = std::abs(r_.inv), mid = book_.mid();
        const double usd = qty * px, fee = usd * p_.taker_fee_bps / 1e4;
        r_.cash += sell ? usd : -usd;
        r_.cash -= fee;
        r_.fees += fee;
        r_.inv = 0;
        fills_.push_back({t, px, qty, sell ? -1 : +1, mid, true});
        ++r_.flattens;
        mark(mid, t);
        return qty * std::abs(mid - px) + fee;
    }
    double equity() const { return have_book_ ? r_.cash + r_.inv * book_.mid() : r_.cash; }
    double inventory_usd() const { return have_book_ ? r_.inv * book_.mid() : 0; }
    int64_t last_book_ms() const { return have_book_ ? book_.t_ms : 0; }
    // Price of the live order on one side (0 = none), e.g. to inspect where the strategy is quoting.
    double quote_px(bool bid_side) const { const Order& o = bid_side ? bid_ : ask_; return o.live ? o.px : 0; }
    // Observability (reported only): the reason behind the last requote decision on one side, and how many requote
    // decisions ended in each reason since this simulator was created (index = QuoteReason).
    QuoteReason reason(bool bid_side) const { return reason_[bid_side ? 0 : 1]; }
    const std::array<int64_t, N_QUOTE_REASON>& reason_counts(bool bid_side) const { return reason_n_[bid_side ? 0 : 1]; }
    bool has_book() const { return have_book_; }
    const Book& last_book() const { return book_; }   // meaningful only while has_book()
    // Quoter surface (src/dex/live, mm_live): the strategy's decision for an inventory the CALLER supplies (live: the
    // exchange position), on the last book given to on_book — exactly the code requote() runs (gates_for /
    // target_price_for), so live quoting and the backtest share one decision path. Valid only once has_book().
    double quote_target(bool bid_side, double inv) const { return target_price_for(bid_side, inv); }
    bool quote_allowed(bool bid_side, double inv, int64_t now, bool halted) const {
        const Gates g = gates_for(bid_side, inv, now);
        return !halted && g.spread_ok && !g.paused && !g.toxic && g.inv_ok;
    }
    double strategy_tick() const { return strat_tick(book_); }
    double requote_band() const { return p_.requote_ticks * strat_tick(book_); }
    const Result& running() const { return r_; }
    // Optional send gate (research: exchange rate limits). Called once per create or cancel the strategy decides at
    // `now`; returns when that transaction actually leaves (≥ now), e.g. the next free request slot of a shared,
    // batching rate limiter. Unset = sent immediately (every existing caller).
    void set_send_gate(std::function<int64_t(int64_t)> g) { gate_ = std::move(g); }
    // Research overlay (mm_lab, 2026-10-26): per-side pulls and shifts decided OUTSIDE the simulator from causal features.
    // The default Overlay changes nothing (every term is +0 / false), so a simulator that is never given one — the live
    // quoter, mm_paper, every backtest — behaves exactly as before. set_overlay only stores it; the next requote (next
    // book, or requote_now) applies it.
    //   pull_bid / pull_ask  the side is not allowed to quote (a resting order is cancelled; noted as reason Halted)
    //   fair_off             added to the fair price, in price units (e.g. a Hyperliquid-anchored fair minus our mid)
    //   shift_ticks          both quotes move this many strategy ticks (positive = up)
    //   widen_ticks          added to half_width_ticks on both sides (the sum is floored at 0)
    struct Overlay { bool pull_bid = false, pull_ask = false; double fair_off = 0, shift_ticks = 0, widen_ticks = 0; };
    void set_overlay(const Overlay& o) { ov_ = o; }
    const Overlay& overlay() const { return ov_; }
    void requote_now(int64_t now) { if (have_book_) requote(now); }

private:
    struct Order {
        bool   live = false;
        double px = 0, qty = 0, remaining = 0;
        double queue_ahead = 0;
        double level_seen = 0;       // level size at our price in the last book we saw
        double traded_at_px = 0;     // trade size at our price since that book
        int64_t active_at = 0;       // order reaches the book (decision + latency)
        int64_t cancel_at = 0;       // 0 = no cancel pending; else leaves the book at this time
        bool    placed_in_book = false;
        int64_t mod_at = 0;          // Params::modify: 0 = none pending; else the new price applies at this time
        double  mod_px = 0;
    };

    static double eps(double px) { return px * 1e-9; }
    double venue_tick(const Book& b) const { return p_.venue_tick > 0 ? p_.venue_tick : b.tick(); }
    double strat_tick(const Book& b) const {
        if (p_.strat_tick_rule == TickRule::Book) return venue_tick(b);
        const double mid = b.mid();
        return std::max(std::pow(10.0, std::floor(std::log10(mid)) - 4), std::pow(10.0, -(6 - p_.hl_sz_decimals)));
    }

    // Orders whose latency has elapsed join the queue; pending cancels take effect.
    void activate_and_expire(int64_t now, const Book* b) {
        for (int s = 0; s < 2; ++s) {
            Order& o = s == 0 ? bid_ : ask_;
            if (!o.live) continue;
            if (o.cancel_at && now >= o.cancel_at) { o = Order{}; continue; }
            if (o.mod_at && now >= o.mod_at) {
                o.px = o.mod_px; o.mod_at = 0; o.mod_px = 0;
                if (o.placed_in_book) { o.placed_in_book = false; o.active_at = now; }   // re-enters below: crossing check + back of the queue
            }
            if (!o.placed_in_book && now >= o.active_at) {
                const Book& ref = b ? *b : book_;
                const bool bid_side = s == 0;
                // Post-only (ALO): an order that would cross the book on arrival is rejected.
                const bool crosses = bid_side ? o.px >= ref.asks[0].px - eps(o.px)
                                              : o.px <= ref.bids[0].px + eps(o.px);
                if (crosses) { ++r_.rejected_cross; o = Order{}; continue; }
                // Back of the queue. Deeper than the book shows: assume the deepest visible level's size
                // (conservative — an unseen level is rarely empty) and let only trades shrink it.
                o.queue_ahead = ref.visible(bid_side, o.px) ? ref.size_at(bid_side, o.px)
                                                            : (bid_side ? ref.bids.back().sz : ref.asks.back().sz);
                o.level_seen = o.queue_ahead;
                o.traded_at_px = 0;
                o.placed_in_book = true;
            }
        }
    }

    // Full fill model: the new book shows the other side resting at or through our resting price.
    void cross_fill(Order& o, bool bid_side, const Book& b) {
        if (!o.live || !o.placed_in_book) return;
        double avail = 0;
        if (bid_side) { for (const auto& l : b.asks) { if (l.px > o.px + eps(o.px)) break; avail += l.sz; } }
        else          { for (const auto& l : b.bids) { if (l.px < o.px - eps(o.px)) break; avail += l.sz; } }
        if (avail > 0) execute(o, bid_side, std::min(o.remaining, avail), b.t_ms);
    }

    void update_queue(Order& o, bool bid_side, const Book& b) {
        if (!o.live || !o.placed_in_book) return;
        if (!b.visible(bid_side, o.px)) { o.traded_at_px = 0; return; }   // level not shown: nothing to learn
        const double now_sz = b.size_at(bid_side, o.px);
        const double after_trades = std::max(0.0, o.level_seen - o.traded_at_px);
        if (now_sz < after_trades) {                     // more left the level than trades explain
            if (p_.cancels == CancelModel::Optimistic && after_trades > 0)
                o.queue_ahead *= now_sz / after_trades;
        }
        o.queue_ahead = std::min(o.queue_ahead, now_sz);  // can never exceed what is resting there
        o.level_seen = now_sz;
        o.traded_at_px = 0;
    }

    // Where the strategy wants its two quotes now (see Params). Post-only: never at or through the other side.
    double target_price(bool bid_side) const { return target_price_for(bid_side, r_.inv); }
    double target_price_for(bool bid_side, double inv) const {
        const double mid = book_.mid(), tick = venue_tick(book_), st = strat_tick(book_);
        const double bb = book_.bids[0].px, ba = book_.asks[0].px;
        const double fair = (p_.fair == Fair::Micro ? book_.microprice() : mid) + ov_.fair_off;
        const double inv_units = inv * mid / p_.order_usd;
        const double centre = fair - p_.skew_ticks * inv_units * st + ov_.shift_ticks * st;   // distances in strategy ticks …
        const double hw = std::max(0.0, p_.half_width_ticks + ov_.widen_ticks);
        if (bid_side) {
            const double k = std::floor((centre - hw * st - bb) / tick + 1e-6);   // … rounded to the venue grid
            return std::min(bb + k * tick, ba - tick);
        }
        const double k = std::ceil((centre + hw * st - ba) / tick - 1e-6);
        return std::max(ba + k * tick, bb + tick);
    }

    // The strategy's gates for one side, for inventory `inv` (coin units): spread wide enough, not paused after a
    // jump, the side not about to be run over, one more unit inside the cap. requote() and the live quoter
    // (quote_allowed) both use exactly this.
    struct Gates { bool spread_ok, paused, toxic, inv_ok; };
    Gates gates_for(bool bid_side, double inv, int64_t now) const {
        const double mid = book_.mid();
        const double unit = lot_qty(p_, mid);
        // Cap in $ at the current mid, counted in LOTS: with venue size rules the lot rounds UP (Lighter $11 → $11.00–11.11,
        // BNB ~$14.8), so a cap of max_inv_units × order_usd would never admit the last lot. Without size rules (every
        // backtest, the live quoter) this is exactly max_inv_units × order_usd, as before.
        const bool sized = p_.size_step > 0 || p_.min_base > 0 || p_.min_quote_usd > 0;
        const double lot_usd = sized ? std::max(p_.order_usd, unit * mid) : p_.order_usd;
        const double cap_usd = p_.max_inv_units * lot_usd * (1 + 1e-9);
        const double st = strat_tick(book_);
        const bool spread_ok = (book_.asks[0].px - book_.bids[0].px) / st >= p_.min_spread_ticks - 1e-9;
        const bool paused = now < paused_until_;
        const double bs = book_.bids[0].sz, as = book_.asks[0].sz;
        const double imb = bs + as > 0 ? (bs - as) / (bs + as) : 0;
        const bool toxic = p_.imb_pull > 0 && (bid_side ? imb <= -p_.imb_pull : imb >= p_.imb_pull);
        const bool inv_ok = bid_side ? (inv + unit) * mid <= cap_usd : (inv - unit) * mid >= -cap_usd;
        return {spread_ok, paused, toxic, inv_ok};
    }

    // Keep one order on each side at the strategy's price (subject to the inventory cap).
    void requote(int64_t now) {
        const double mid = book_.mid();
        const double unit = lot_qty(p_, mid);
        const double st = strat_tick(book_);
        for (int s = 0; s < 2; ++s) {
            const bool bid_side = s == 0;
            Order& o = bid_side ? bid_ : ask_;
            const double target = target_price(bid_side);
            if (target <= 0) { note_reason(s, NoBook); continue; }
            const Gates g = gates_for(bid_side, r_.inv, now);
            const bool spread_ok = g.spread_ok, paused = g.paused, toxic = g.toxic, inv_ok = g.inv_ok;
            const bool pulled = bid_side ? ov_.pull_bid : ov_.pull_ask;      // research overlay, after the strategy's gates
            const bool allowed = !halted_ && !pulled && spread_ok && !paused && !toxic && inv_ok;
            note_reason(s, halted_ || pulled ? Halted : !spread_ok ? SpreadNarrow : paused ? JumpPause : toxic ? ImbalancePull
                         : !inv_ok ? InvCap
                         : (o.live && !o.cancel_at && std::abs(o.px - target) > eps(target) &&
                            std::abs(o.px - target) <= p_.requote_ticks * st + eps(target)) ? Kept : Quoting);
            const bool moved = std::abs(o.px - target) > p_.requote_ticks * st + eps(target);
            const int64_t cancel_ms = p_.cancel_latency_ms >= 0 ? p_.cancel_latency_ms : p_.latency_ms;
            if (o.live && !o.cancel_at && (!allowed || (moved && !p_.modify)))
                o.cancel_at = send_at(now) + cancel_ms;   // still fillable until the cancel lands
            else if (p_.modify && o.live && !o.cancel_at && !o.mod_at && moved) {
                o.mod_px = target; o.mod_at = send_at(now) + cancel_ms;   // still fillable at the old price until then
                ++r_.quotes_modified;
            }
            if (!o.live && allowed) {
                o = Order{};
                o.live = true;
                o.px = target;
                o.qty = o.remaining = unit;
                o.active_at = send_at(now) + (p_.place_latency_ms >= 0 ? p_.place_latency_ms : p_.latency_ms);
                ++r_.quotes_placed;
            }
        }
    }

    void execute(Order& o, bool bid_side, double qty, int64_t t) {
        const int side = bid_side ? +1 : -1;
        const double usd = qty * o.px;
        const double mid = book_.mid();
        r_.inv  += side * qty;
        r_.cash -= side * usd;
        const double fee = usd * p_.maker_fee_bps / 1e4;
        r_.cash -= fee;
        r_.fees += fee;
        r_.volume_usd += usd;
        r_.capture_bps_sum += usd * side * (mid - o.px) / o.px * 1e4;
        ++r_.fills;
        fills_.push_back({t, o.px, qty, side, mid});
        o.remaining -= qty;
        if (o.remaining <= o.qty * 1e-9) o = Order{};
        mark(mid, t);
    }

    void mark(double mid, int64_t t) {
        const double eq = r_.cash + r_.inv * mid;
        peak_ = std::max(peak_, eq);
        r_.max_dd = std::max(r_.max_dd, peak_ - eq);
        if (t >= next_sample_) {
            r_.equity.push_back({t, eq});
            next_sample_ = (t / 60'000 + 1) * 60'000;
        }
    }

    std::optional<double> mid_at(int64_t t) const {      // last mid at or before t, if t is covered
        if (mids_.empty() || t > mids_.back().first) return std::nullopt;
        auto it = std::upper_bound(mids_.begin(), mids_.end(), t,
                                   [](int64_t v, const std::pair<int64_t, double>& m) { return v < m.first; });
        if (it == mids_.begin()) return std::nullopt;
        return std::prev(it)->second;
    }

    Params p_;
    Result r_;
    Book book_;
    bool have_book_ = false;
    Order bid_, ask_;
    std::vector<Fill> fills_;
    std::vector<std::pair<int64_t, double>> mids_;
    double peak_ = 0;
    int64_t next_sample_ = 0;
    int64_t paused_until_ = 0;
    bool halted_ = false;
    QuoteReason reason_[2] = {NoBook, NoBook};                  // observability only (see QuoteReason)
    std::array<int64_t, N_QUOTE_REASON> reason_n_[2] = {};
    void note_reason(int side, QuoteReason r) { reason_[side] = r; ++reason_n_[side][r]; }
    std::function<int64_t(int64_t)> gate_;
    Overlay ov_;
    int64_t send_at(int64_t now) { return gate_ ? std::max(now, gate_(now)) : now; }
};

// An exchange request limiter for set_send_gate, shared by every market of one account (2026-10-27; moved here
// from mm_lab so the judge and the paper engine run the same code). Lighter Standard: 60 requests per rolling
// minute, up to 15 transactions per request — transactions decided while a request is still open (same or earlier
// send time, < `batch` in it) ride in that request; otherwise a new request takes the next free slot of the window.
// Returns when the transaction actually leaves (≥ now). Calls must come in non-decreasing `now` (one event clock).
struct RequestGate {
    int limit = 60, batch = 15; int64_t window = 60'000;
    int64_t linger_ms = 0;          // research: a new request waits this long for more transactions (0 = send at once, as live)
    std::deque<int64_t> sent;
    int64_t open_t = -1; int open_n = 0;
    int64_t txs = 0, requests = 0, delayed = 0, max_wait_ms = 0;   // observability only
    int64_t operator()(int64_t now) {
        ++txs;
        if (open_t >= now && open_n < batch) { ++open_n; note(now, open_t); return open_t; }
        while (!sent.empty() && sent.front() <= now - window) sent.pop_front();
        int64_t t = now + linger_ms;
        if (static_cast<int>(sent.size()) >= limit) t = std::max(t, sent[sent.size() - limit] + window);
        sent.push_back(t);
        ++requests;
        open_t = t; open_n = 1;
        note(now, t);
        return t;
    }
private:
    void note(int64_t now, int64_t t) { if (t > now) { ++delayed; max_wait_ms = std::max(max_wait_ms, t - now); } }
};

}  // namespace hl
