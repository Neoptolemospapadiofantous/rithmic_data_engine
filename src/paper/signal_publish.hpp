#pragma once
// signal_publish.hpp — live trend_confirm state + manual-trading signals for the dashboard (2026-10-07).
//
// Founder: "a dedicated live strategy page that shows the progress of a pullback and a signal for me to
// take; if the strategy doesn't fully complete, don't create a signal". For every paper-fleet strategy
// flagged `"publish_signals": true` the paper engine:
//   * upserts live_signal_state (one row per strategy) at most every 5 s, and at once on every tf-bar
//     close, entry and exit — never per tick (CLAUDE.md FEED-DROP TRAP);
//   * inserts ONE live_signals row only when the paper broker ACTUALLY OPENS the position — i.e. the
//     strategy's own confirmations passed AND every host gate (book imbalance_min, liquidity
//     liq_vol_min / liq_impact_max, regime, rvol) let the entry through. A blocked or incomplete setup
//     never produces a row. The row is closed with the broker's exit price/reason when the position ends.
// Replay / audit runs (PaperDb trades_only) write nothing: the DB methods are no-ops there and the host
// does not build publishers while warming up or replaying.
//
// The JSON shape is a contract with the dashboard (~/Desktop/bot, /live-signals page).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "paper_broker.hpp"
#include "paper_db.hpp"
#include "paper_quote.hpp"
#include "../execution/trend_strategy.hpp"

namespace paper {

// Where published state goes (PaperDb in production, a fake in tests).
struct SignalSink {
    virtual ~SignalSink() = default;
    virtual void    upsert_state(const std::string& id, int64_t ts_us, const std::string& json) = 0;
    virtual int64_t insert_signal(const std::string& id, int64_t ts_us, int dir, double entry, double stop,
                                  double risk, double t1, double t2, const std::string& checks_json) = 0;
    virtual void    close_signal(int64_t signal_id, int64_t ts_us, double px, const std::string& reason) = 0;
    virtual void    update_stop(int64_t /*signal_id*/, double /*stop*/) {}
    virtual void    add_event(int64_t /*signal_id*/, const std::string& /*event_json*/) {}
    virtual void    notify(const std::string&) {}
};

struct PaperDbSignalSink : SignalSink {
    PaperDb* db;
    explicit PaperDbSignalSink(PaperDb* d) : db(d) {}
    void upsert_state(const std::string& id, int64_t ts, const std::string& j) override { if (db) db->upsert_signal_state(id, ts, j); }
    int64_t insert_signal(const std::string& id, int64_t ts, int dir, double e, double s, double r, double a, double b,
                          const std::string& c) override { return db ? db->insert_live_signal(id, ts, dir, e, s, r, a, b, c) : 0; }
    void close_signal(int64_t sid, int64_t ts, double px, const std::string& why) override { if (db) db->close_live_signal(sid, ts, px, why); }
    void update_stop(int64_t sid, double stop) override { if (db) db->update_live_signal_stop(sid, stop); }
    void add_event(int64_t sid, const std::string& j) override { if (db) db->append_live_signal_event(sid, j); }
    void notify(const std::string& text) override {          // fail-open, same pattern as executor_main
        std::string q; for (char ch : text) { if (ch == '"' || ch == '\\' || ch == '$' || ch == '`') q += '\\'; q += ch; }
        const int rc = std::system(("grid-notify \"" + q + "\" >/dev/null 2>&1 &").c_str());
        (void)rc;
    }
};

inline std::string sp_num(double v, int prec = 2) {
    if (!std::isfinite(v)) return "null";
    char b[48]; std::snprintf(b, sizeof b, "%.*f", prec, v); return b;
}
inline const char* sp_bool(bool v) { return v ? "true" : "false"; }
inline std::string sp_iso(int64_t us) {
    time_t t = (time_t)(us / 1'000'000LL); struct tm g; gmtime_r(&t, &g);
    char b[32]; std::strftime(b, sizeof b, "%Y-%m-%dT%H:%M:%SZ", &g); return b;
}

// Host-side confirmations the strategy cannot see (they are applied by the broker's gates).
struct HostChecks {
    double liq_vol = NAN, liq_impact = NAN; bool liq_ok = true;
    double liq_vol_min = 0, liq_impact_max = 0;
    double book_imb = NAN; bool book_ok = true; double book_min = 0;
};
inline HostChecks host_checks(const OrbConfig& cfg, const RegimeState* reg, const QuoteState& qs, int64_t now_us, int dir) {
    HostChecks h;
    h.liq_vol_min = cfg.liq_vol_min; h.liq_impact_max = cfg.liq_impact_max; h.book_min = cfg.imbalance_min;
    double vpm = 0, imp = 0;
    const bool have = reg && reg->liquidity(cfg.liq_bars, vpm, imp);
    if (have) { h.liq_vol = vpm; h.liq_impact = imp; }
    if (cfg.liq_vol_min > 0 || cfg.liq_impact_max > 0)
        h.liq_ok = have && !(cfg.liq_vol_min > 0 && vpm < cfg.liq_vol_min) && !(cfg.liq_impact_max > 0 && imp > cfg.liq_impact_max);
    const bool fresh = qs.fresh(now_us);
    if (fresh) h.book_imb = qs.q.imbalance();
    if (cfg.imbalance_min > 0)
        h.book_ok = fresh && (dir > 0 ? h.book_imb >= cfg.imbalance_min : h.book_imb <= 1.0 - cfg.imbalance_min);
    return h;
}

inline std::string checks_json(const TrendStrategy::TcfStatus& s, const HostChecks& h) {
    std::string j = "{";
    j += "\"ema_stack\":"; j += sp_bool(s.ema_stack);
    j += ",\"ema_mid_rising\":"; j += sp_bool(s.ema_mid_rising);
    j += ",\"er\":" + sp_num(s.er, 3) + ",\"er_min\":" + sp_num(s.er_min, 3) + ",\"er_ok\":" + sp_bool(s.er_ok);
    j += ",\"slope\":" + sp_num(s.slope, 3) + ",\"slope_min\":" + sp_num(s.slope_min, 3) + ",\"slope_ok\":" + sp_bool(s.slope_ok);
    j += ",\"vwap_side_ok\":"; j += sp_bool(s.vwap_side_ok);
    j += ",\"flow\":" + sp_num(s.flow, 3) + ",\"flow_min\":" + sp_num(s.flow_min, 3) + ",\"flow_ok\":" + sp_bool(s.flow_ok);
    j += ",\"vol_ratio\":" + sp_num(s.vol_ratio, 2) + ",\"vol_mult\":" + sp_num(s.vol_mult, 2) + ",\"vol_ok\":" + sp_bool(s.vol_ok);
    j += ",\"atr\":" + sp_num(s.atr, 2) + ",\"atr_min\":" + sp_num(s.atr_min, 2) + ",\"atr_max\":" + sp_num(s.atr_max, 2) + ",\"atr_ok\":" + sp_bool(s.atr_ok);
    j += ",\"liq_vol\":" + sp_num(h.liq_vol, 1) + ",\"liq_vol_min\":" + sp_num(h.liq_vol_min, 1);
    j += ",\"liq_impact\":" + sp_num(h.liq_impact, 1) + ",\"liq_impact_max\":" + sp_num(h.liq_impact_max, 1) + ",\"liq_ok\":" + sp_bool(h.liq_ok);
    j += ",\"book_imb\":" + sp_num(h.book_imb, 3) + ",\"book_min\":" + sp_num(h.book_min, 3) + ",\"book_ok\":" + sp_bool(h.book_ok);
    j += "}";
    return j;
}

// Stage after the host gates: a setup whose liquidity / book gate would block the entry is not
// "trigger_ready" (the trigger is not the only thing missing) — it is shown one step back.
// Uses stage_flat: whether a position is open is the BROKER's call (in_trade is set by the caller), not the
// strategy's flag — a blocked or still-pending entry leaves the strategy "in position" with nothing filled.
inline std::string host_stage(const TrendStrategy::TcfStatus& s, const HostChecks& h, bool breakout) {
    if (s.stage_flat == "trigger_ready" && !(h.liq_ok && h.book_ok)) return breakout ? "trend" : "pullback";
    return s.stage_flat;
}

inline std::string state_json(const TrendStrategy::TcfStatus& s, const HostChecks& h, const std::string& stage, int64_t now_us,
                              int pos_dir, double entry, double stop, double best) {
    char win[16]; std::snprintf(win, sizeof win, "%04d-%04d", s.win_start, s.win_end);
    std::string j = "{\"mode\":\"trend_confirm\",\"tf_min\":" + std::to_string(s.tf_min) + ",\"window\":\"" + win +
                    "\",\"session\":\"" + s.session + "\",\"in_window\":" + sp_bool(s.in_window) + ",\"ts\":\"" + sp_iso(now_us) +
                    "\",\"price\":" + sp_num(s.price) + ",\"bias\":" + std::to_string(s.bias) + ",\"stage\":\"" + stage + "\"";
    j += ",\"checks\":" + checks_json(s, h);
    j += ",\"pullback\":{\"touched\":"; j += sp_bool(s.touched);
    j += ",\"bars_since_touch\":" + (s.bars_since_touch >= 0 ? std::to_string(s.bars_since_touch) : std::string("null"));
    j += ",\"pb_bars\":" + std::to_string(s.pb_bars) + ",\"touch_px\":" + (s.touched ? sp_num(s.touch_px) : "null");
    j += ",\"trigger_level\":" + (s.trigger_level != 0 ? sp_num(s.trigger_level) : "null");
    j += ",\"trigger_dist\":" + (s.trigger_level != 0 ? sp_num(s.trigger_dist) : "null") + "}";
    j += ",\"levels\":{\"ema_fast\":" + sp_num(s.ema_fast) + ",\"ema_mid\":" + sp_num(s.ema_mid) + ",\"ema_slow\":" + sp_num(s.ema_slow) +
         ",\"vwap\":" + (s.vwap > 0 ? sp_num(s.vwap) : "null") + "}";
    if (pos_dir == 0) j += ",\"position\":null";
    else {
        const double risk = (entry - stop) * pos_dir, open_pts = (s.price - entry) * pos_dir;
        j += ",\"position\":{\"dir\":" + std::to_string(pos_dir) + ",\"entry\":" + sp_num(entry) + ",\"stop\":" + sp_num(stop) +
             ",\"best\":" + sp_num(best) + ",\"open_pts\":" + sp_num(open_pts) + ",\"r\":" + (risk > 0 ? sp_num(open_pts / risk) : "null") + "}";
    }
    j += "}";
    return j;
}

// `"publish_signals": true` in a fleet strategy's params (flat JSON, same reader style as the configs).
inline bool wants_publish(const std::string& params) {
    const size_t k = params.find("\"publish_signals\""); if (k == std::string::npos) return false;
    size_t p = params.find(':', k); if (p == std::string::npos) return false;
    ++p; while (p < params.size() && (params[p] == ' ' || params[p] == '\t')) ++p;
    return params.compare(p, 4, "true") == 0 || params.compare(p, 1, "1") == 0;
}

// The manual trader's stop: the broker's working stop (initial stop, break-even, trail ratchet) and,
// for trend_confirm, the strategy's own trail — whichever is tighter.
inline double effective_stop(const PaperBroker& b, const TrendStrategy::TcfStatus& st, int dir, double fallback) {
    double e = b.placed_stop() > 0 ? b.placed_stop() : fallback;
    if (st.in_position && st.stop != 0) e = dir > 0 ? std::max(e, st.stop) : std::min(e, st.stop);
    return e;
}

// Telegram text for a closed signal. A stop / break-even / trail exit is a level the trader's own stop
// order already covers; anything else (strategy exit, target, end of window) means "close it now".
inline std::string exit_message(const std::string& id, int dir, double entry, double exit_px, double risk, const std::string& reason,
                                const OrbConfig& cfg) {
    const bool resting = reason == "stop" || reason == "breakeven" || reason == "trail";
    const double pts = (exit_px - entry) * dir;
    char m[360];
    std::snprintf(m, sizeof m, "%s %s %s — %s · exit %.2f · %+.2f pts = %s$%.0f on 1 %s%s · %s%s",
                  resting ? "⚪ STOPPED OUT" : "🟡 EXIT NOW — CLOSE", dir > 0 ? "LONG (SELL to close)" : "SHORT (BUY to close)",
                  cfg.symbol.c_str(), id.c_str(), exit_px, pts, pts < 0 ? "-" : "+", std::fabs(pts) * cfg.point_value, cfg.symbol.c_str(),
                  risk > 0 ? (" (" + sp_num(pts / risk, 2) + "R)").c_str() : "",
                  reason.c_str(), resting ? " — your stop order should have filled" : "");
    return m;
}

// One timeline entry of a signal: {"t": ISO, "k": entry|breakeven|trail|stop|exit, "px": price,
// "stop": the stop after the event, "pts": result so far in points, "note": reason}.
inline std::string event_json(int64_t ts_us, const char* kind, double px, double stop, double pts, const std::string& note = "") {
    std::string j = "{\"t\":\"" + sp_iso(ts_us) + "\",\"k\":\"" + kind + "\",\"px\":" + sp_num(px) +
                    ",\"stop\":" + sp_num(stop) + ",\"pts\":" + sp_num(pts);
    if (!note.empty()) { j += ",\"note\":\""; for (char c : note) if (c != '"' && c != '\\') j += c; j += "\""; }
    return j + "}";
}

// Entry text. The paper engine's break-even fires within seconds (median trade 36 s on the low-drawdown
// picks), faster than anyone can move a stop by hand — so the alert carries the BRACKET to enter with
// (platform ATM / auto-breakeven + auto-trail), which reproduces the paper management exactly.
inline std::string entry_message(const std::string& id, int dir, double entry, double stop, double risk, double t1, double t2,
                                 const OrbConfig& cfg) {
    char tp[48] = "";
    if (cfg.take_profit_pts() > 0) std::snprintf(tp, sizeof tp, " · target +%.2f pts", cfg.take_profit_pts());
    char m[480];
    if (t1 == t2 && std::fabs(t1 - entry) > 1e-9) {             // one strategy target (flag_take's measured move), no BE / trail
        std::snprintf(m, sizeof m,
            "%s %s — %s · 1 contract · entry %.2f · stop %.2f (1R %.2f pts = $%.0f on 1 %s) · TARGET %.2f (+%.2f pts, %.1fR)\n"
            "BRACKET: stop %.2f pts · target %.2f pts · no break-even, no trail · flat 15:55 ET",
            dir > 0 ? "🟢 LONG / BUY" : "🔴 SHORT / SELL", cfg.symbol.c_str(), id.c_str(), entry, stop, risk, risk * cfg.point_value,
            cfg.symbol.c_str(), t1, dir * (t1 - entry), risk > 0 ? dir * (t1 - entry) / risk : 0.0, risk, dir * (t1 - entry));
        return m;
    }
    std::snprintf(m, sizeof m,
        "%s %s — %s · 1 contract · entry %.2f · stop %.2f (1R %.2f pts = $%.0f on 1 %s) · T1 %.2f · T2 %.2f\n"
        "BRACKET: stop %.2f pts · auto-breakeven at +%.2f → entry %+.2f · trail %.2f pts%s%s",
        dir > 0 ? "🟢 LONG / BUY" : "🔴 SHORT / SELL", cfg.symbol.c_str(), id.c_str(), entry, stop, risk, risk * cfg.point_value,
        cfg.symbol.c_str(), t1, t2,
        risk, cfg.trail_be_trigger, cfg.trail_be_offset, cfg.trail_step,
        cfg.trail_delay_secs > 0 ? (" (trail starts after " + std::to_string(cfg.trail_delay_secs / 60) + " min)").c_str() : "", tp);
    return m;
}

// One per flagged strategy. Call on_tick() after the broker and the strategy have seen the tick.
// Telegram (via the sink): the entry, every material stop move (break-even, then each trail step of
// at least max(2 pts, ¼R)), and the exit — so a trader running 1 contract by hand can follow it.
class SignalPublisher {
public:
    SignalPublisher(std::string id, SignalSink* sink, int64_t min_interval_us = 5'000'000LL, bool trend_confirm = true)
        : id_(std::move(id)), sink_(sink), min_us_(min_interval_us), tcf_(trend_confirm) {}

    // Trend engine: the strategy reports its own status (and, for trend_confirm, its trail stop).
    void on_tick(const PaperBroker& b, const TrendStrategy& s, const OrbConfig& cfg, const RegimeState* reg, int64_t now_us) {
        TrendStrategy::TcfStatus st = s.tcf_status(now_us);
        if (!tcf_) st.in_position = false;                        // only trend_confirm reports its own trail stop
        step(b, cfg, reg, now_us, st, tcf_ ? s.tcf_entry_snapshot() : nullptr, s.last_price());
    }

    // ORB engine (2026-10-08): no setup status — the window is the session open → last entry time.
    void on_tick_orb(const PaperBroker& b, const OrbConfig& cfg, const RegimeState* reg, int64_t now_us, double last_px) {
        TrendStrategy::TcfStatus st;
        st.price = last_px; st.session = "rth"; st.stage_flat = st.stage = "watching";
        st.win_start = cfg.session_open_hour * 100 + cfg.session_open_min;
        st.win_end = cfg.last_entry_hour * 100 + cfg.last_entry_min;
        const time_t tt = (time_t)(now_us / 1'000'000LL); struct tm g; gmtime_r(&tt, &g);
        const time_t et = tt - us_et_offset(g) * 3600; struct tm e; gmtime_r(&et, &e);
        const int hhmm = e.tm_hour * 100 + e.tm_min;
        st.in_window = e.tm_wday >= 1 && e.tm_wday <= 5 && hhmm >= st.win_start && hhmm < st.win_end;
        step(b, cfg, reg, now_us, st, nullptr, last_px);
    }

private:
    void step(const PaperBroker& b, const OrbConfig& cfg, const RegimeState* reg, int64_t now_us,
              const TrendStrategy::TcfStatus& st, const TrendStrategy::TcfStatus* snap, double last_px) {
        if (!sink_) return;
        const int dir = b.direction();
        bool force = false;
        const int cdir = dir != 0 ? dir : (st.bias != 0 ? st.bias : 1);
        const HostChecks h = host_checks(cfg, reg, b.quotes(), now_us, cdir);
        if (dir != 0 && last_dir_ == 0) {                         // the broker OPENED a position: a real signal
            entry_ = b.entry_price();
            stop_ = effective_stop(b, st, dir, entry_ - dir * cfg.sl_points);
            double risk = (entry_ - stop_) * dir;
            if (risk <= 0) { risk = cfg.sl_points; stop_ = entry_ - dir * risk; }
            risk_ = risk; notified_stop_ = stop_;
            best_ = entry_;
            // a strategy target (flag_take's measured move) replaces the generic 1R / 2R levels
            const double stt = b.strategy_target();
            const double t1 = stt > 0.0 ? stt : entry_ + dir * risk, t2 = stt > 0.0 ? stt : entry_ + 2 * dir * risk;
            signal_id_ = sink_->insert_signal(id_, b.entry_time_us() > 0 ? b.entry_time_us() : now_us, dir, entry_, stop_, risk, t1, t2,
                                              checks_json(snap ? *snap : st, h));
            sink_->notify(entry_message(id_, dir, entry_, stop_, risk, t1, t2, cfg));
            sink_->add_event(signal_id_, event_json(b.entry_time_us() > 0 ? b.entry_time_us() : now_us, "entry", entry_, stop_, 0.0));
            ++signals_;
            force = true;
        } else if (dir == 0 && last_dir_ != 0) {                  // position closed
            sink_->add_event(signal_id_, event_json(b.last_exit_us() > 0 ? b.last_exit_us() : now_us, "exit", b.last_exit_px(), stop_,
                                                    (b.last_exit_px() - entry_) * last_dir_, b.last_exit_reason()));
            sink_->close_signal(signal_id_, b.last_exit_us() > 0 ? b.last_exit_us() : now_us, b.last_exit_px(), b.last_exit_reason());
            sink_->notify(exit_message(id_, last_dir_, entry_, b.last_exit_px(), risk_, b.last_exit_reason(), cfg));
            ++exits_;
            signal_id_ = 0; force = true;
        }
        if (dir != 0) {
            const double eff = effective_stop(b, st, dir, stop_);
            if ((eff - stop_) * dir > 1e-9) stop_ = eff;          // the trail only ever tightens
            const double step = std::max(2.0, 0.25 * risk_);
            if ((stop_ - notified_stop_) * dir >= step - 1e-9) {
                const double locked = (stop_ - entry_) * dir;
                char msg[320];
                std::snprintf(msg, sizeof msg, "🔒 MOVE STOP — %s %s · %s · new stop %.2f (%s %+.2f pts%s) · price %.2f",
                              dir > 0 ? "LONG" : "SHORT", cfg.symbol.c_str(), id_.c_str(), stop_, locked >= -1e-9 ? "locks" : "risk now",
                              locked, risk_ > 0 ? (" / " + sp_num(locked / risk_, 2) + "R").c_str() : "", last_px);
                sink_->notify(msg);
                sink_->update_stop(signal_id_, stop_);
                const bool first_lock = (notified_stop_ - entry_) * dir < -1e-9 && locked >= -1e-9;
                sink_->add_event(signal_id_, event_json(now_us, locked < -1e-9 ? "stop" : first_lock ? "breakeven" : "trail",
                                                        last_px, stop_, (last_px - entry_) * dir));
                notified_stop_ = stop_; ++stop_moves_; force = true;
            }
            best_ = dir > 0 ? std::max(best_, last_px) : std::min(best_, last_px);
        }
        last_dir_ = dir;
        if (st.bars != last_bars_) { last_bars_ = st.bars; force = true; }   // tf-bar close
        if (!force && now_us - last_pub_us_ < min_us_) return;
        last_pub_us_ = now_us;
        std::string stage = dir != 0 ? std::string("in_trade") : (tcf_ ? host_stage(st, h, st.breakout) : std::string("watching"));
        std::string sj = state_json(st, h, stage, now_us, dir, entry_, stop_, best_);
        if (!tcf_) { const std::string m = "\"mode\":\"trend_confirm\""; const size_t k = sj.find(m);
                     if (k != std::string::npos) sj.replace(k, m.size(), "\"mode\":\"other\""); }
        sink_->upsert_state(id_, now_us, sj);
        ++writes_;
    }
public:
    int signals() const { return signals_; }
    int exits() const { return exits_; }
    int stop_moves() const { return stop_moves_; }
    int writes() const { return writes_; }

private:
    std::string id_; SignalSink* sink_; int64_t min_us_; bool tcf_;
    int last_dir_ = 0; int64_t last_pub_us_ = 0, last_bars_ = -1, signal_id_ = 0;
    double entry_ = 0, stop_ = 0, best_ = 0, risk_ = 0, notified_stop_ = 0;
    int signals_ = 0, exits_ = 0, stop_moves_ = 0, writes_ = 0;
};

} // namespace paper
