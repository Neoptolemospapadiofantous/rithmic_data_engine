#pragma once
// mm_paper.hpp — the 24/7 paper market maker's portfolio and risk controls (founder 2026-10-25: "a system of
// all angles where we can run it and daily overview it"). Pure: no network, DB or files; unit-tested by
// tests/dex/test_lighter.cpp. mm_paper_main.cpp feeds it Lighter's live books and trades and persists what
// it reports. No orders exist anywhere: fills come from hl::Simulator's queue model.
//
// One hl::Simulator per market, all on the frozen setting (mm_robust's candidate). Controls, in order:
//   * day roll at 00:00 UTC — every market flattened (taker), the day's row reported, fresh simulators;
//     the same flat-to-flat day the backtests measured, so the paper days compare with them directly;
//   * feed down — no message at all on the connection (mm_paper_main: 15 s, or disconnected) halts every
//     market until the feed is back; a single market with no book change for `stale_ms` is pulled alone;
//   * daily loss stop — portfolio equity for the day at or below −`daily_loss_usd` flattens and halts
//     everything until the next day;
//   * kill switch — while set (mm_paper_main: the file data/mm_paper/KILL), flatten and halt everything.
//   * after a loss-stop or kill flatten, an order whose cancel is still in flight (cancel latency) can still fill:
//     once that latency has passed in exchange time, any inventory left is flattened again ("reflatten").
//
// Clocks (audit 2026-10-26): everything the simulator sees — fills, cancels, the halt's requote — runs on the
// EXCHANGE clock (each market's last book time), never the wall clock; only feed health (feed down, a stale
// market) is judged on the wall clock against the time messages were RECEIVED.
//
// Event order (audit 2026-10-26): the live feed is re-ordered exactly like the backtest loader
// (hl_events.hpp sort_events: exchange time, trades before books at equal times, then arrival) through a short
// receive-time buffer (Sequencer, Config::reorder_ms). Applying a book that already shows a trade's effect before
// the trade itself counts the traded size twice and produces fills the backtest would not have.

#include "dex/hl_mm.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <ctime>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mmpaper {

struct Config {
    hl::Params p;                                   // the setting every market runs …
    std::map<std::string, hl::Params> per_market;   // … unless this names the market (venue tick, HL size decimals)
    double  daily_loss_usd = 150;   // ≈ 3× the worst holdout day at $100 quotes, 30 coins
    int64_t stale_ms = 120'000;     // a market with no book change this long is pulled. Lighter only sends a
                                    // book when it changes, so quiet markets are silent for tens of seconds;
                                    // a DEAD connection is caught by tick(feed_ok=false) instead
    int64_t reorder_ms = 250;       // live events are held this long (receive time) and released in backtest order
};

// The frozen setting (mm_robust.cpp candidate, Lighter Standard): width 5, spread ≥ 4 ticks, imbalance
// pull 0.6, 2 s pause after a 2-tick jump, keep queue place for a 1-tick change; 0 fees; 300 ms delay (200 ms
// imposed by Lighter's Standard account + network; measured exchange→box lag here is 142 ms); $100 quotes,
// cap 3. Ticks are HYPERLIQUID ticks (mm_paper_main sets strat_tick_rule + hl_sz_decimals + venue_tick per
// market): that is the unit the setting was validated in — Lighter's own grid is up to 100× finer.
inline hl::Params frozen_params() {
    hl::Params p;
    p.order_usd = 100; p.max_inv_units = 3;
    p.maker_fee_bps = 0; p.taker_fee_bps = 0; p.latency_ms = 300;   // 300 ms = the delay every backtest used
    p.cancels = hl::CancelModel::Pessimistic;
    p.half_width_ticks = 5; p.min_spread_ticks = 4; p.imb_pull = 0.6;
    p.pause_move_ticks = 2; p.pause_ms = 2000; p.requote_ticks = 1;
    p.track_markouts = true;
    return p;
}

struct DayRow {
    std::string day, market;
    double pnl = 0, volume = 0, capture_bps = 0, mk10_bps = 0, max_dd = 0;
    int64_t fills = 0, flattens = 0;
};

inline std::string utc_day(int64_t ms) {
    std::time_t s = static_cast<std::time_t>(ms / 1000);
    std::tm t{};
    gmtime_r(&s, &t);
    char b[16];
    std::strftime(b, sizeof b, "%Y-%m-%d", &t);
    return b;
}


// ── observability (2026-10-26): what each market is quoting and why ──────────────────────────────────
// One market's quote decision as written to mm_paper_quotes and status.json. Prices 0 = none (NULL in Postgres).
struct QuoteState {
    double mid = 0, best_bid = 0, best_ask = 0;
    double bid_px = 0, ask_px = 0;
    hl::QuoteReason bid_reason = hl::NoBook, ask_reason = hl::NoBook;
    double inv = 0, inv_usd = 0, equity = 0;
    // The row-worthy part: a new row is due when any of these four changes.
    bool same_quote(const QuoteState& o) const {
        return bid_px == o.bid_px && ask_px == o.ask_px && bid_reason == o.bid_reason && ask_reason == o.ask_reason;
    }
};

inline QuoteState quote_state_of(const hl::Simulator& s) {
    QuoteState q;
    if (s.has_book()) {
        const hl::Book& b = s.last_book();
        q.mid = b.mid(); q.best_bid = b.bids[0].px; q.best_ask = b.asks[0].px;
    }
    q.bid_px = s.quote_px(true); q.ask_px = s.quote_px(false);
    q.bid_reason = s.reason(true); q.ask_reason = s.reason(false);
    q.inv = s.running().inv; q.inv_usd = s.inventory_usd(); q.equity = s.equity();
    return q;
}

// Decides when a quote row is written: on a CHANGE of (bid_px, ask_px, bid_reason, ask_reason), at most one row per
// second per market (a change inside the second is written by the next observe() after it — mm_paper calls observe on
// every applied event and on its 1 s tick, so a change waits at most ~1 s; a flip that reverts inside the second is
// not written), plus a keepalive row every 60 s while nothing changes. Row times strictly increase per market (the
// primary key is (label, market, ts)).
class QuoteTracker {
public:
    static constexpr int64_t MIN_GAP_MS = 1000, KEEPALIVE_MS = 60'000;
    struct Row { int64_t ts_ms = 0; std::string market; QuoteState q; bool keepalive = false; };
    // A market with no book (mid 0) yields no row (mid is NOT NULL; no_book shows in status.json). The two callers' clocks
    // differ (event rows: exchange time; 1 s tick rows: wall − re-order hold), so a call may arrive "before" the last row:
    // its time is moved to last row + 1 ms rather than skipped, and the 1 s limit is judged on that time.
    std::optional<Row> observe(const std::string& m, int64_t t_ms, const QuoteState& q) {
        if (q.mid <= 0) return std::nullopt;
        St& s = st_[m];
        if (s.written && t_ms <= s.last_ts) t_ms = s.last_ts + 1;
        const bool changed = !s.written || !q.same_quote(s.last);
        const int64_t since = s.written ? t_ms - s.last_ts : INT64_MAX;
        bool keep = false;
        if (changed) { if (since < MIN_GAP_MS) { ++deferred_; return std::nullopt; } }
        else if (since >= KEEPALIVE_MS) keep = true;
        else return std::nullopt;
        s.written = true; s.last_ts = t_ms; s.last = q;
        ++rows_;
        if (keep) ++keepalives_;
        return Row{t_ms, m, q, keep};
    }
    int64_t rows() const { return rows_; }
    int64_t keepalives() const { return keepalives_; }
    int64_t deferred() const { return deferred_; }       // observe() calls that saw a change inside the 1 s limit

private:
    struct St { bool written = false; int64_t last_ts = 0; QuoteState last; };
    std::map<std::string, St> st_;
    int64_t rows_ = 0, keepalives_ = 0, deferred_ = 0;
};

// Every hl::Params field, as status.json publishes it per market ("params") — what scripts/mm_debug.sh replay feeds mm_sim,
// so a replay runs the engine's exact setting instead of a copy of it. Enum fields as text.
inline nlohmann::json params_json(const hl::Params& p) {
    return {{"order_usd", p.order_usd}, {"max_inv_units", p.max_inv_units}, {"maker_fee_bps", p.maker_fee_bps}, {"taker_fee_bps", p.taker_fee_bps},
            {"latency_ms", p.latency_ms}, {"place_latency_ms", p.place_latency_ms}, {"cancel_latency_ms", p.cancel_latency_ms},
            {"cancels", p.cancels == hl::CancelModel::Optimistic ? "optimistic" : "pessimistic"},
            {"half_width_ticks", p.half_width_ticks}, {"skew_ticks", p.skew_ticks}, {"fair", p.fair == hl::Fair::Micro ? "micro" : "mid"},
            {"min_spread_ticks", p.min_spread_ticks}, {"imb_pull", p.imb_pull}, {"pause_move_ticks", p.pause_move_ticks}, {"pause_ms", p.pause_ms},
            {"requote_ticks", p.requote_ticks}, {"venue_tick", p.venue_tick},
            {"tick_rule", p.strat_tick_rule == hl::TickRule::Hyperliquid ? "hyperliquid" : "book"}, {"hl_sz_decimals", p.hl_sz_decimals},
            {"fills", p.fills == hl::FillModel::Legacy ? "legacy" : p.fills == hl::FillModel::Full ? "full" : "nocross"}};
}

// {"quoting":n, "no_book":n, …} — every reason present, 0 included.
inline nlohmann::json reasons_json(const std::array<int64_t, hl::N_QUOTE_REASON>& c) {
    nlohmann::json j = nlohmann::json::object();
    for (int r = 0; r < hl::N_QUOTE_REASON; ++r) j[hl::reason_name(static_cast<hl::QuoteReason>(r))] = c[r];
    return j;
}

// Inputs for status.json's `components` block; each component gets an `ok` health verdict.
struct Health {
    int64_t now_ms = 0, started_ms = 0;
    // feed
    bool connected = false;
    int64_t last_msg_ms = 0, books = 0, trades = 0, gaps = 0, reconnects = 0;
    int64_t feed_timeout_ms = 15'000;
    // reorder
    int64_t reorder_pending = 0, late_trades = 0;
    // sim
    int64_t markets = 0, halted_markets = 0;
    // db
    bool db_enabled = false;
    int64_t db_queue = 0, db_retries = 0, db_errors = 0, db_dropped = 0, db_dropped_today = 0, db_last_commit_ms = 0;
    // quote rows (own queue + ledger, never part of `ok` — the G2 gate does not read them)
    int64_t quote_queue = 0, quote_dropped = 0, quote_dropped_today = 0, quote_written = 0, quote_conflicts = 0, quote_errors = 0;
    // risk
    double daily_loss_limit = 0, worst_market_equity = 0, equity = 0;
    bool kill = false, loss_stop = false;
};
// ok rules: feed — connected and a message within feed_timeout_ms; reorder — fewer than 10,000 events held;
// sim — markets running, no kill / loss stop, fewer than half the markets halted (the G2 outage rule); db — disabled,
// or no drop today, queue < 10,000 and (queue empty or a commit within 120 s); risk — no kill switch, no loss stop.
// The quote_* counters (mm_paper_quotes rows: own queue + own ledger) are reported but never decide db.ok.
inline nlohmann::json components_json(const Health& h) {
    using nlohmann::json;
    const bool feed_ok = h.connected && h.now_ms - h.last_msg_ms < h.feed_timeout_ms;
    const bool reorder_ok = h.reorder_pending < 10'000;
    const bool sim_ok = h.markets > 0 && !h.kill && !h.loss_stop && h.halted_markets * 2 < h.markets;
    const int64_t commit_ref = h.db_last_commit_ms ? h.db_last_commit_ms : h.started_ms;
    const bool db_ok = !h.db_enabled || (h.db_dropped_today == 0 && h.db_queue < 10'000 && (h.db_queue == 0 || h.now_ms - commit_ref < 120'000));
    const bool risk_ok = !h.kill && !h.loss_stop;
    return {
        {"feed", {{"ok", feed_ok}, {"last_ms", h.last_msg_ms}, {"books", h.books}, {"trades", h.trades}, {"gaps", h.gaps}, {"reconnects", h.reconnects}, {"connected", h.connected}}},
        {"reorder", {{"ok", reorder_ok}, {"pending", h.reorder_pending}, {"late_trades", h.late_trades}}},
        {"sim", {{"ok", sim_ok}, {"markets", h.markets}, {"halted", h.halted_markets}}},
        {"db", {{"ok", db_ok}, {"enabled", h.db_enabled}, {"queue", h.db_queue}, {"retries", h.db_retries}, {"errors", h.db_errors},
                {"dropped", h.db_dropped}, {"dropped_today", h.db_dropped_today}, {"last_commit_ms", h.db_last_commit_ms},
                {"quote_queue", h.quote_queue}, {"quote_dropped", h.quote_dropped}, {"quote_dropped_today", h.quote_dropped_today},
                {"quote_written", h.quote_written}, {"quote_conflicts", h.quote_conflicts}, {"quote_errors", h.quote_errors}}},
        {"risk", {{"ok", risk_ok}, {"daily_loss_limit", h.daily_loss_limit}, {"worst_market_equity", h.worst_market_equity},
                  {"equity", h.equity}, {"kill", h.kill}, {"loss_stop", h.loss_stop}}},
    };
}

// ── live re-ordering (see the header) ────────────────────────────────────────────────────────────────
// Every event waits up to `hold_ms` of RECEIVE time, then leaves in hl::sort_events order: exchange time, trades
// before books at equal times, arrival order last. Releasing an event also releases everything that sorts before
// it, so order is never broken to honour the hold. A trade whose exchange time is older than a book of the same
// market that has ALREADY been released (it arrived more than hold_ms late) is still applied — before any later
// book, since every later book sorts after it — and counted in late_trades(): dropping it would hide a real print,
// and the simulator meets it with the book in force, exactly as it would any trade.
class Sequencer {
public:
    struct Ev {
        int64_t recv_ms = 0, t_ms = 0, seq = 0;
        bool is_book = false;
        std::string market;
        hl::Book book;
        hl::Trade trade;
    };
    explicit Sequencer(int64_t hold_ms = 250) : hold_(hold_ms) {}
    void push_book(const std::string& m, hl::Book b, int64_t recv_ms) {
        Ev e; e.recv_ms = recv_ms; e.t_ms = b.t_ms; e.is_book = true; e.market = m; e.book = std::move(b);
        insert(std::move(e));
    }
    void push_trade(const std::string& m, hl::Trade t, int64_t recv_ms) {
        Ev e; e.recv_ms = recv_ms; e.t_ms = t.t_ms; e.is_book = false; e.market = m; e.trade = std::move(t);
        insert(std::move(e));
    }
    // Releases every event held at least hold_ms by `now_recv_ms` (all of them with all=true), plus everything
    // that sorts before the last such event, in order, through apply(const Ev&). Returns how many left.
    template <class F> size_t release(int64_t now_recv_ms, F&& apply, bool all = false) {
        size_t n = 0;
        if (all) n = q_.size();
        else for (size_t i = q_.size(); i-- > 0;) if (q_[i].recv_ms + hold_ <= now_recv_ms) { n = i + 1; break; }
        for (size_t i = 0; i < n; ++i) {
            Ev e = std::move(q_.front());
            q_.pop_front();
            int64_t& last_book = last_book_t_[e.market];
            if (e.is_book) last_book = std::max(last_book, e.t_ms);
            else if (e.t_ms < last_book) ++late_;
            apply(e);
        }
        return n;
    }
    size_t pending() const { return q_.size(); }
    int64_t late_trades() const { return late_; }
    int64_t hold_ms() const { return hold_; }
    static bool before(const Ev& a, const Ev& b) {      // == hl::sort_events
        if (a.t_ms != b.t_ms) return a.t_ms < b.t_ms;
        if (a.is_book != b.is_book) return !a.is_book;
        return a.seq < b.seq;
    }

private:
    void insert(Ev e) {
        e.seq = next_seq_++;
        auto pos = std::upper_bound(q_.begin(), q_.end(), e, before);
        q_.insert(pos, std::move(e));
    }
    int64_t hold_;
    int64_t next_seq_ = 0, late_ = 0;
    std::deque<Ev> q_;
    std::map<std::string, int64_t> last_book_t_;
};

class Portfolio {
public:
    static constexpr int64_t ROLL_SLACK_MS = 2000;
    using FillFn = std::function<void(const std::string& market, const hl::Fill&, double inv_after)>;
    using EventFn = std::function<void(int64_t t_ms, const std::string& kind, const std::string& market, const std::string& detail)>;
    using DayFn = std::function<void(const DayRow&)>;

    explicit Portfolio(Config c) : c_(std::move(c)), seq_(c_.reorder_ms) {}
    void on_fill(FillFn f) { fill_fn_ = std::move(f); }
    void on_event(EventFn f) { event_fn_ = std::move(f); }
    void on_day(DayFn f) { day_fn_ = std::move(f); }

    // Apply one event NOW (tests, replays). recv_ms = when the book was received (wall clock, for the stale
    // check); -1 = its exchange time.
    void book(const std::string& m, const hl::Book& b, int64_t recv_ms = -1) {
        if (!b.valid()) return;
        roll_if_new_day(b.t_ms);
        Mkt& k = mkt(m);
        k.sim->on_book(b);
        k.last_book_recv_ms = recv_ms >= 0 ? recv_ms : b.t_ms;
        drain(m, k);
        recheck_flat(m, k, b.t_ms);
        if (applied_fn_) applied_fn_(m, b.t_ms);
    }
    void trade(const std::string& m, const hl::Trade& t) {
        roll_if_new_day(t.t_ms);
        Mkt& k = mkt(m);
        k.sim->on_trade(t);
        drain(m, k);
        recheck_flat(m, k, t.t_ms);
        if (applied_fn_) applied_fn_(m, t.t_ms);
    }
    // Observability: called after every applied book / trade with (market, exchange time) — mm_paper reads
    // quote_state(m) there and feeds its QuoteTracker.
    using AppliedFn = std::function<void(const std::string& market, int64_t t_ms)>;
    void on_applied(AppliedFn f) { applied_fn_ = std::move(f); }
    QuoteState quote_state(const std::string& m) const {
        auto it = mkts_.find(m);
        return it == mkts_.end() ? QuoteState{} : quote_state_of(*it->second.sim);
    }
    std::vector<std::string> markets() const { std::vector<std::string> v; for (const auto& [m, k] : mkts_) v.push_back(m); return v; }
    const hl::Params& params(const std::string& m) const { return params_for(m); }
    bool killed() const { return killed_; }
    bool loss_stopped() const { return loss_stop_; }
    // status.json "markets": the original fields (market, equity, inv, fills, halted) + the quote, its reasons and the
    // reason counts since 00:00 UTC (or since the engine started, if later), + what a replay needs (tick settings).
    nlohmann::json market_status() const {
        nlohmann::json a = nlohmann::json::array();
        for (const auto& [m, k] : mkts_) {
            const auto& s = *k.sim;
            const QuoteState q = quote_state_of(s);
            const hl::Params& p = params_for(m);
            auto px = [](double v) { return v > 0 ? nlohmann::json(v) : nlohmann::json(nullptr); };
            a.push_back({{"market", m}, {"equity", s.equity()}, {"inv", s.running().inv}, {"fills", s.running().fills}, {"halted", s.halted()},
                         {"stale", k.stale}, {"mid", px(q.mid)}, {"best_bid", px(q.best_bid)}, {"best_ask", px(q.best_ask)},
                         {"bid_px", px(q.bid_px)}, {"ask_px", px(q.ask_px)},
                         {"bid_reason", hl::reason_name(q.bid_reason)}, {"ask_reason", hl::reason_name(q.ask_reason)},
                         {"reasons_bid", reasons_json(s.reason_counts(true))}, {"reasons_ask", reasons_json(s.reason_counts(false))},
                         {"inv_usd", q.inv_usd}, {"last_book_ms", s.last_book_ms()},
                         {"venue_tick", p.venue_tick}, {"tick_rule", p.strat_tick_rule == hl::TickRule::Hyperliquid ? "hyperliquid" : "book"},
                         {"hl_sz_decimals", p.hl_sz_decimals}, {"params", params_json(p)}});
        }
        return a;
    }
    // The live path: events go through the re-order buffer; pump() applies whatever is due (call it on every
    // message and on a short timer).
    void push_book(const std::string& m, const hl::Book& b, int64_t recv_ms) { if (b.valid()) seq_.push_book(m, b, recv_ms); }
    void push_trade(const std::string& m, const hl::Trade& t, int64_t recv_ms) { seq_.push_trade(m, t, recv_ms); }
    size_t pump(int64_t now_recv_ms, bool all = false) {
        return seq_.release(now_recv_ms, [&](const Sequencer::Ev& e) {
            if (e.is_book) book(e.market, e.book, e.recv_ms);
            else trade(e.market, e.trade);
        }, all);
    }
    const Sequencer& sequencer() const { return seq_; }

    // Called about once a second with the wall clock: feed health, stale markets, loss stop, kill switch, day roll.
    void tick(int64_t now_ms, bool kill, bool feed_ok = true) {
        // The wall clock only rolls the day once no buffered event of the old day can still be released: events wait
        // up to reorder_ms, plus 2 s of slack for receive-vs-exchange skew. Normally the first released event of the
        // new day rolls it (on exchange time) before this does.
        roll_if_new_day(now_ms - c_.reorder_ms - ROLL_SLACK_MS);
        if (feed_ok == feed_down_) {
            feed_down_ = !feed_ok;
            emit(now_ms, feed_down_ ? "feed_down" : "feed_up", "", feed_down_ ? "no market data: every quote pulled" : "market data flowing again");
        }
        if (kill != killed_) {
            killed_ = kill;
            if (kill) { flatten_all(); emit(now_ms, "kill", "", "kill switch set: flattened and halted"); }
            else {
                emit(now_ms, "resume", "", "kill switch cleared");
                if (!halted()) for (auto& [m, k] : mkts_) k.recheck_ms = 0;
            }
        }
        if (!loss_stop_ && equity() <= -c_.daily_loss_usd) {
            loss_stop_ = true;
            flatten_all();
            emit(now_ms, "loss_stop", "", "day equity " + fmt(equity()) + " ≤ −" + fmt(c_.daily_loss_usd) + ": halted until 00:00 UTC");
        }
        for (auto& [m, k] : mkts_) {
            // Feed health on the wall clock against RECEIVE time; everything the simulator does on exchange time.
            const bool stale = now_ms - k.last_book_recv_ms > c_.stale_ms;
            if (stale != k.stale) {
                k.stale = stale;
                emit(now_ms, stale ? "stale" : "fresh", m, stale ? "no book for " + std::to_string(c_.stale_ms / 1000) + " s: quotes pulled" : "book flowing again");
            }
            k.sim->set_halted(halted() || feed_down_ || k.stale, k.sim->last_book_ms());
            drain(m, k);
            recheck_flat(m, k, k.sim->last_book_ms());
        }
    }

    double equity() const { double e = 0; for (const auto& [m, k] : mkts_) e += k.sim->equity(); return e; }
    double gross_usd() const { double g = 0; for (const auto& [m, k] : mkts_) g += std::abs(k.sim->inventory_usd()); return g; }
    bool halted() const { return killed_ || loss_stop_; }
    const std::string& day() const { return day_; }
    // Per-market (equity, inventory in coin, fills today) for the minute equity rows and the status file.
    struct Snap { std::string market; double equity, inv; int64_t fills; bool halted; };
    double inventory(const std::string& m) const { auto it = mkts_.find(m); return it == mkts_.end() ? 0 : it->second.sim->running().inv; }
    std::vector<Snap> snapshot() const {
        std::vector<Snap> v;
        for (const auto& [m, k] : mkts_) v.push_back({m, k.sim->equity(), k.sim->running().inv, k.sim->running().fills, k.sim->halted()});
        return v;
    }

private:
    struct Mkt {
        std::unique_ptr<hl::Simulator> sim;
        size_t drained = 0;
        bool stale = false;
        int64_t last_book_recv_ms = 0;   // wall clock, receive time of the last book (stale check only)
        int64_t recheck_ms = 0;          // exchange time when in-flight cancels after a halt flatten have landed; 0 = none
    };
    int64_t cancel_latency(const std::string& m) const {
        const auto& p = params_for(m);
        return p.cancel_latency_ms >= 0 ? p.cancel_latency_ms : p.latency_ms;
    }
    // After a halt flatten, an order whose cancel was still travelling may have filled: once the cancel latency has
    // passed (exchange time) and the portfolio is still halted, flatten whatever inventory is left (paper only).
    // Checked on every event of that market and on every tick (a market that goes silent has no new fills either).
    void recheck_flat(const std::string& m, Mkt& k, int64_t t) {
        if (!k.recheck_ms || t < k.recheck_ms || !halted()) return;
        k.recheck_ms = 0;
        const double inv = k.sim->running().inv;
        if (std::abs(inv) < 1e-12) return;
        const double cost = k.sim->flatten_now(t);
        drain(m, k);
        char d[160];
        std::snprintf(d, sizeof d, "an order still in flight filled after the halt: %.8g flattened (cost %s)", inv, fmt(cost).c_str());
        emit(t, "reflatten", m, d);
    }

    const hl::Params& params_for(const std::string& m) const {
        auto it = c_.per_market.find(m);
        return it == c_.per_market.end() ? c_.p : it->second;
    }
    Mkt& mkt(const std::string& m) {
        auto it = mkts_.find(m);
        if (it == mkts_.end()) {
            Mkt k; k.sim = std::make_unique<hl::Simulator>(params_for(m));
            it = mkts_.emplace(m, std::move(k)).first;
            if (halted()) it->second.sim->set_halted(true, 0);
        }
        return it->second;
    }

    void drain(const std::string& m, Mkt& k) {
        const auto& f = k.sim->fills();
        for (; k.drained < f.size(); ++k.drained)
            if (fill_fn_) fill_fn_(m, f[k.drained], k.sim->running().inv);
    }

    // Halt flatten on each market's own exchange clock: quotes pulled (their cancels land after the cancel latency),
    // position closed now, and a re-check scheduled for when those cancels have landed.
    void flatten_all() {
        for (auto& [m, k] : mkts_) {
            const int64_t t = k.sim->last_book_ms();
            k.sim->set_halted(true, t);
            k.sim->flatten_now(t);
            drain(m, k);
            if (t > 0) k.recheck_ms = t + cancel_latency(m);
        }
    }

    void roll_if_new_day(int64_t t_ms) {
        if (t_ms <= 0) return;
        const std::string d = utc_day(t_ms);
        if (day_.empty()) { day_ = d; return; }
        if (d <= day_) return;
        for (auto& [m, k] : mkts_) {
            const int64_t last = k.sim->last_book_ms();
            k.sim->flatten_now(last > 0 ? std::min(last, t_ms) : t_ms);   // priced at the last book: stamped with its time
            drain(m, k);
            const auto r = k.sim->finish();
            DayRow row;
            row.day = day_; row.market = m; row.pnl = r.flat_pnl; row.volume = r.volume_usd; row.fills = r.fills;
            row.flattens = r.flattens; row.max_dd = r.max_dd;
            row.capture_bps = r.volume_usd > 0 ? r.capture_bps_sum / r.volume_usd : 0;
            row.mk10_bps = r.mk_weight > 0 ? r.mk10_bps_sum / r.mk_weight : 0;
            if (day_fn_) day_fn_(row);
            const int64_t recv = k.last_book_recv_ms;
            k = Mkt{};
            k.sim = std::make_unique<hl::Simulator>(params_for(m));
            k.last_book_recv_ms = recv;
        }
        emit(t_ms, "day_roll", "", day_ + " closed; " + d + " opened");
        day_ = d;
        loss_stop_ = false;
        if (killed_) for (auto& [m, k] : mkts_) k.sim->set_halted(true, t_ms);
    }

    void emit(int64_t t, const std::string& kind, const std::string& m, const std::string& d) { if (event_fn_) event_fn_(t, kind, m, d); }
    static std::string fmt(double v) { char b[32]; std::snprintf(b, sizeof b, "$%.2f", v); return b; }

    Config c_;
    Sequencer seq_;
    std::map<std::string, Mkt> mkts_;
    std::string day_;
    bool killed_ = false, loss_stop_ = false, feed_down_ = false;
    FillFn fill_fn_;
    EventFn event_fn_;
    DayFn day_fn_;
    AppliedFn applied_fn_;
};

}  // namespace mmpaper
