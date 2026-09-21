#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    trend_strategy.hpp — configurable trend-following signal generator.

    One engine ("trend"), eleven entry modes, one shared exit manager (the
    paper broker's stop / break-even / trail from the OrbConfig risk knobs,
    plus optional flip / time-stop / session-end flattens emitted here).

    Modes (all decide on completed `tf_min`-minute bars, fill on the next tick):
      donchian        close above the highest high (below the lowest low) of the
                      last N completed bars
      ema_pullback    fast EMA > slow EMA and rising; a bar touched the fast EMA
                      within the last K bars; enter on a close above that bar's high
      vwap_trend      price above a rising session VWAP; enter on a bar that
                      touched VWAP and closed back above it
      opening_drive   direction of the first D minutes after 09:30 (net move ≥
                      k·ATR); enter on the first pullback bar in that direction
                      inside an entry window
      gap_go          open gap vs prior RTH close ≥ G pts; after W minutes, if the
                      gap is less than half filled, go with the gap
      pdhl_breakout   close through the prior-day (or overnight) high/low with
                      bar volume ≥ m × average
      squeeze         Bollinger inside Keltner for S bars, then a close outside
                      the Bollinger band
      supertrend      ATR-band trend flip (exit on the opposite flip if enabled)
      nr7             narrowest range of the last N bars, then a break of that bar
      rs_continuation NQ outperforms the ES reference over L minutes by ≥ R bp
                      while its own trend is up (mirror for shorts)
      tod_momentum    at a clock time, continuation of the prior L-minute move
                      when it is ≥ k·ATR

    Pure signal generator: no I/O beyond LOG. Mirrors OrbStrategy's host contract
    (set_signal_callback / on_tick / check_eod / reset_session / halt / notify).
    Prior-day levels come from bars the engine itself has seen, so modes that need
    yesterday stay silent on their first day — by design, never fabricated.
    ═══════════════════════════════════════════════════════════════════════════ */

#include "orb_config.hpp"
#include "orb_strategy.hpp"   // OrbTick, OrbSignal, us_et_offset
#include "log.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <functional>
#include <limits>
#include <string>
#include <vector>

struct TrendConfig {
    std::string mode = "donchian";
    int  tf_min = 5;
    int  win_start = 930, win_end = 1555;   // ET HHMM: entries allowed [start, end); flatten at end
    bool allow_longs = true, allow_shorts = true;
    bool exit_on_flip = true;               // supertrend / ema modes: opposite signal closes the trade
    int  time_stop_min = 0;                 // 0 = none
    int  atr_len = 14;
    // donchian
    int donchian_n = 20;
    // ema_pullback
    int ema_fast = 9, ema_slow = 21; double pullback_tol_atr = 0.5; int pullback_lookback = 3;
    // vwap_trend
    int vwap_slope_bars = 5; double vwap_tol_atr = 0.3;
    // opening_drive
    int drive_minutes = 30; double drive_min_atr = 1.0; int drive_entry_from = 1000, drive_entry_to = 1130;
    // gap_go
    double gap_min_pts = 20.0; int gap_wait_min = 15; double gap_fill_frac = 0.5;
    // pdhl_breakout
    std::string level = "prior_day";        // prior_day | overnight
    double vol_mult = 1.5; int vol_avg_bars = 20;
    // squeeze
    int bb_len = 20; double bb_mult = 2.0; double kc_mult = 1.5; int squeeze_bars = 6;
    // supertrend
    int st_len = 10; double st_mult = 3.0;
    // nr7
    int nr_n = 7;
    // rs_continuation
    int rs_lookback_min = 15; double rs_min_bp = 5.0;
    // tod_momentum
    int tod_hhmm = 1000; int tod_lookback_min = 15; double tod_min_atr = 0.8; int tod_window_min = 30;

    static TrendConfig from_json_string(const std::string& t) {
        TrendConfig c;
        c.mode = jstr(t, "mode", c.mode);
        c.tf_min = jint(t, "tf_min", c.tf_min);
        c.win_start = jint(t, "win_start", c.win_start); c.win_end = jint(t, "win_end", c.win_end);
        c.allow_longs = jbool(t, "allow_longs", c.allow_longs); c.allow_shorts = jbool(t, "allow_shorts", c.allow_shorts);
        c.exit_on_flip = jbool(t, "exit_on_flip", c.exit_on_flip);
        c.time_stop_min = jint(t, "time_stop_min", c.time_stop_min);
        c.atr_len = jint(t, "atr_len", c.atr_len);
        c.donchian_n = jint(t, "donchian_n", c.donchian_n);
        c.ema_fast = jint(t, "ema_fast", c.ema_fast); c.ema_slow = jint(t, "ema_slow", c.ema_slow);
        c.pullback_tol_atr = jdbl(t, "pullback_tol_atr", c.pullback_tol_atr);
        c.pullback_lookback = jint(t, "pullback_lookback", c.pullback_lookback);
        c.vwap_slope_bars = jint(t, "vwap_slope_bars", c.vwap_slope_bars); c.vwap_tol_atr = jdbl(t, "vwap_tol_atr", c.vwap_tol_atr);
        c.drive_minutes = jint(t, "drive_minutes", c.drive_minutes); c.drive_min_atr = jdbl(t, "drive_min_atr", c.drive_min_atr);
        c.drive_entry_from = jint(t, "drive_entry_from", c.drive_entry_from); c.drive_entry_to = jint(t, "drive_entry_to", c.drive_entry_to);
        c.gap_min_pts = jdbl(t, "gap_min_pts", c.gap_min_pts); c.gap_wait_min = jint(t, "gap_wait_min", c.gap_wait_min);
        c.gap_fill_frac = jdbl(t, "gap_fill_frac", c.gap_fill_frac);
        c.level = jstr(t, "level", c.level); c.vol_mult = jdbl(t, "vol_mult", c.vol_mult); c.vol_avg_bars = jint(t, "vol_avg_bars", c.vol_avg_bars);
        c.bb_len = jint(t, "bb_len", c.bb_len); c.bb_mult = jdbl(t, "bb_mult", c.bb_mult); c.kc_mult = jdbl(t, "kc_mult", c.kc_mult);
        c.squeeze_bars = jint(t, "squeeze_bars", c.squeeze_bars);
        c.st_len = jint(t, "st_len", c.st_len); c.st_mult = jdbl(t, "st_mult", c.st_mult);
        c.nr_n = jint(t, "nr_n", c.nr_n);
        c.rs_lookback_min = jint(t, "rs_lookback_min", c.rs_lookback_min); c.rs_min_bp = jdbl(t, "rs_min_bp", c.rs_min_bp);
        c.tod_hhmm = jint(t, "tod_hhmm", c.tod_hhmm); c.tod_lookback_min = jint(t, "tod_lookback_min", c.tod_lookback_min);
        c.tod_min_atr = jdbl(t, "tod_min_atr", c.tod_min_atr); c.tod_window_min = jint(t, "tod_window_min", c.tod_window_min);
        return c;
    }

private:
    // Minimal flat-JSON readers (same shape as the other configs; no nesting).
    static std::string raw(const std::string& s, const std::string& key) {
        std::string k = "\"" + key + "\"";
        size_t p = s.find(k); if (p == std::string::npos) return "";
        p = s.find(':', p + k.size()); if (p == std::string::npos) return "";
        ++p; while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
        size_t e = p;
        if (p < s.size() && s[p] == '"') { e = s.find('"', p + 1); return e == std::string::npos ? "" : s.substr(p + 1, e - p - 1); }
        while (e < s.size() && s[e] != ',' && s[e] != '}' && !std::isspace((unsigned char)s[e])) ++e;
        return s.substr(p, e - p);
    }
    static int jint(const std::string& s, const std::string& k, int d) { auto v = raw(s, k); return v.empty() ? d : std::atoi(v.c_str()); }
    static double jdbl(const std::string& s, const std::string& k, double d) { auto v = raw(s, k); return v.empty() ? d : std::atof(v.c_str()); }
    static bool jbool(const std::string& s, const std::string& k, bool d) { auto v = raw(s, k); return v.empty() ? d : (v == "true" || v == "1"); }
    static std::string jstr(const std::string& s, const std::string& k, const std::string& d) { auto v = raw(s, k); return v.empty() ? d : v; }
};

struct TrendSession {
    int    trades_today = 0;
    bool   in_position  = false;
    bool   risk_halted  = false;
    std::string halt_reason;
    // exposed for the position row / logs (no opening range here)
    double orb_high = 0.0, orb_low = 0.0; bool orb_set = false;
};

class TrendStrategy {
public:
    using SignalCallback = std::function<void(OrbSignal, double, const std::string&)>;
    struct Bar { int mod = -1; int64_t ts = 0; double o = 0, h = 0, l = 0, c = 0; double v = 0; };

    TrendStrategy(const TrendConfig& tc, const OrbConfig& risk) : tc_(tc), risk_(risk) {
        if (tc_.tf_min < 1) tc_.tf_min = 1;
    }
    void set_signal_callback(SignalCallback cb) { cb_ = std::move(cb); }

    // ── host contract (mirrors OrbStrategy) ──────────────────────────────────
    void reset_session() {
        // 18:00 ET rollover: yesterday's RTH becomes "prior day"; overnight tracking restarts.
        if (rth_seen_) { prev_hi_ = day_hi_; prev_lo_ = day_lo_; prev_close_ = rth_close_; have_prev_ = true; }
        day_hi_ = std::numeric_limits<double>::lowest(); day_lo_ = std::numeric_limits<double>::max();
        on_hi_ = std::numeric_limits<double>::lowest(); on_lo_ = std::numeric_limits<double>::max();
        rth_seen_ = false; in_rth_ = false; vwap_pv_ = vwap_v_ = 0.0; vwap_hist_.clear();
        drive_dir_ = 0; drive_done_ = false; gap_ = 0.0; gap_done_ = false; tod_done_ = false; reenter_dir_ = 0;
        eod_emitted_ = false; entry_ts_ = 0; pos_dir_ = 0;
        sess_.trades_today = 0; sess_.in_position = false; sess_.risk_halted = false; sess_.halt_reason.clear();
        LOG("[TREND %s] Session reset (tf=%dm window %04d-%04d)%s", tc_.mode.c_str(), tc_.tf_min,
            tc_.win_start, tc_.win_end, have_prev_ ? "" : " — no prior day yet");
    }
    void halt_trading(const std::string& why) { sess_.risk_halted = true; sess_.halt_reason = why; LOG("[TREND %s] halted: %s", tc_.mode.c_str(), why.c_str()); }
    void unhalt_trading(const std::string& why) { if (!sess_.risk_halted) return; sess_.risk_halted = false; sess_.halt_reason.clear(); LOG("[TREND %s] unhalted: %s", tc_.mode.c_str(), why.c_str()); }
    void seed_trades_today(int n) { sess_.trades_today = n; }
    // Restart recovery: the host resumed an open paper position from the DB —
    // mirror it so the entry gate, flip-exit and time-stop see the leg.
    void seed_open_position(OrbSignal dir, int64_t entry_ts_us) {
        sess_.in_position = true; pos_dir_ = (dir == OrbSignal::BUY) ? 1 : -1; entry_ts_ = entry_ts_us;
    }
    void notify_trade_filled(OrbSignal, const std::string& reason = "") {
        sess_.in_position = false; pos_dir_ = 0; entry_ts_ = 0;
        LOG("[TREND %s] trade closed (%s) trades_today=%d", tc_.mode.c_str(), reason.c_str(), sess_.trades_today);
    }
    const TrendSession& session() const { return sess_; }
    double last_price() const { return last_px_; }
    double orb_high() const { return 0.0; }
    double orb_low()  const { return 0.0; }
    bool   orb_set()  const { return false; }

    // ES (reference) completed 1-minute bar — only rs_continuation uses it.
    void on_reference_bar(int64_t /*ts_us*/, double close) {
        ref_closes_.push_back(close);
        if (ref_closes_.size() > 400) ref_closes_.pop_front();
    }

    void check_eod(int h, int m) {
        int hhmm = h * 100 + m;
        if (hhmm >= tc_.win_end && sess_.in_position && !eod_emitted_) {
            eod_emitted_ = true;
            emit(OrbSignal::FLATTEN_EOD, last_px_, "session_end");
        }
    }

    // ── ticks → 1m bars → tf bars → decisions ────────────────────────────────
    void on_tick(const OrbTick& t) {
        int h, m; to_et(t.ts_micros, h, m);
        const int mod = h * 60 + m;
        last_px_ = t.price; last_ts_ = t.ts_micros;
        // session phases
        const bool rth = mod >= 9 * 60 + 30 && mod < 16 * 60;
        if (rth && !in_rth_) {            // RTH start: VWAP + day range + drive/gap state
            in_rth_ = true; rth_seen_ = true;
            vwap_pv_ = vwap_v_ = 0.0; vwap_hist_.clear();
            day_hi_ = std::numeric_limits<double>::lowest(); day_lo_ = std::numeric_limits<double>::max();
            rth_open_ = t.price; rth_open_mod_ = mod;
            gap_ = have_prev_ ? (t.price - prev_close_) : 0.0;
        }
        if (!rth && in_rth_) in_rth_ = false;
        if (rth) {
            vwap_pv_ += t.price * (double)t.size; vwap_v_ += (double)t.size; rth_close_ = t.price;
            day_hi_ = std::max(day_hi_, t.price); day_lo_ = std::min(day_lo_, t.price);
        } else if (mod >= 18 * 60 || mod < 9 * 60 + 30) {
            on_hi_ = std::max(on_hi_, t.price); on_lo_ = std::min(on_lo_, t.price);
        }
        // 1m bar
        if (m1_.mod != mod) {
            if (m1_.mod >= 0) on_m1_close(m1_, h, m);
            m1_ = Bar{mod, t.ts_micros, t.price, t.price, t.price, t.price, (double)t.size};
        } else {
            m1_.h = std::max(m1_.h, t.price); m1_.l = std::min(m1_.l, t.price); m1_.c = t.price; m1_.v += (double)t.size;
        }
    }

private:
    // ── helpers ──────────────────────────────────────────────────────────────
    static void to_et(int64_t us, int& h, int& m) {
        time_t tt = (time_t)(us / 1'000'000LL); struct tm g; gmtime_r(&tt, &g);
        int64_t et = (int64_t)tt - us_et_offset(g) * 3600LL;
        h = (int)((et / 3600) % 24); if (h < 0) h += 24; m = (int)((et % 3600) / 60);
    }
    double vwap() const { return vwap_v_ > 0 ? vwap_pv_ / vwap_v_ : 0.0; }
    bool in_window(int hhmm) const { return hhmm >= tc_.win_start && hhmm < tc_.win_end; }
    double atr() const { return atr_; }
    void emit(OrbSignal s, double px, const std::string& why) {
        if (cb_) cb_(s, px, why);
        if (s == OrbSignal::BUY || s == OrbSignal::SELL) {
            sess_.in_position = true; ++sess_.trades_today; pos_dir_ = (s == OrbSignal::BUY) ? 1 : -1; entry_ts_ = last_ts_;
            LOG("[TREND %s] %s signal @%.2f (%s) trades_today=%d", tc_.mode.c_str(), s == OrbSignal::BUY ? "LONG" : "SHORT", px, why.c_str(), sess_.trades_today);
        }
    }
    bool can_enter(int hhmm, int dir) const {
        if (sess_.in_position || sess_.risk_halted) return false;
        if (!in_window(hhmm)) return false;
        if (sess_.trades_today >= risk_.max_daily_trades) return false;
        if (dir > 0 && !tc_.allow_longs) return false;
        if (dir < 0 && !tc_.allow_shorts) return false;
        return true;
    }

    // 1-minute bar completed
    void on_m1_close(const Bar& b, int h, int m) {
        const int hhmm = h * 100 + m;
        m1_hist_.push_back(b); if (m1_hist_.size() > 600) m1_hist_.pop_front();
        // time stop / flip exits evaluated every minute
        if (sess_.in_position && tc_.time_stop_min > 0 && entry_ts_ > 0 &&
            (b.ts - entry_ts_) >= (int64_t)tc_.time_stop_min * 60'000'000LL)
            emit(OrbSignal::FLATTEN_EOD, b.c, "time_stop");
        // tf aggregation — a tf bar closes as soon as its LAST minute completes
        // (a 09:30–09:34 five-minute bar closes on the first 09:35 tick).
        const int key = b.mod / tc_.tf_min;
        if (tf_cur_.mod < 0 || tf_cur_.mod / tc_.tf_min != key) {
            if (tf_cur_.mod >= 0) on_tf_close(tf_cur_, hhmm);   // safety: a gap skipped the last minute
            tf_cur_ = b; tf_cur_.mod = key * tc_.tf_min;
        } else {
            tf_cur_.h = std::max(tf_cur_.h, b.h); tf_cur_.l = std::min(tf_cur_.l, b.l); tf_cur_.c = b.c; tf_cur_.v += b.v;
        }
        if ((b.mod + 1) / tc_.tf_min != key) {                  // last minute of the bucket
            on_tf_close(tf_cur_, hhmm);
            tf_cur_.mod = -1;
        }
        if (in_rth_) vwap_hist_.push_back(vwap());
        // minute-driven modes
        if (tc_.mode == "gap_go") mode_gap(b, hhmm);
        else if (tc_.mode == "tod_momentum") mode_tod(b, hhmm);
        else if (tc_.mode == "rs_continuation") mode_rs(b, hhmm);
    }

    // tf-minute bar completed: indicators then the bar-close modes
    void on_tf_close(const Bar& b, int hhmm) {
        tf_.push_back(b); if (tf_.size() > 400) tf_.erase(tf_.begin());
        const size_t n = tf_.size();
        // ATR (Wilder)
        if (n >= 2) {
            const Bar& p = tf_[n - 2];
            double tr = std::max({b.h - b.l, std::fabs(b.h - p.c), std::fabs(b.l - p.c)});
            atr_ = (atr_ <= 0) ? tr : (atr_ * (tc_.atr_len - 1) + tr) / tc_.atr_len;
        }
        // EMAs
        ema_f_ = ema_f_ <= 0 ? b.c : ema_f_ + (2.0 / (tc_.ema_fast + 1)) * (b.c - ema_f_);
        ema_s_prev5_.push_back(ema_s_); if (ema_s_prev5_.size() > 6) ema_s_prev5_.pop_front();
        ema_s_ = ema_s_ <= 0 ? b.c : ema_s_ + (2.0 / (tc_.ema_slow + 1)) * (b.c - ema_s_);
        ema20_ = ema20_ <= 0 ? b.c : ema20_ + (2.0 / 21.0) * (b.c - ema20_);
        vol_hist_.push_back(b.v); if (vol_hist_.size() > 200) vol_hist_.pop_front();
        if (atr_ <= 0) return;
        if (tc_.mode == "donchian")       mode_donchian(b, hhmm);
        else if (tc_.mode == "ema_pullback") mode_ema_pullback(b, hhmm);
        else if (tc_.mode == "vwap_trend")   mode_vwap(b, hhmm);
        else if (tc_.mode == "opening_drive") mode_drive(b, hhmm);
        else if (tc_.mode == "pdhl_breakout") mode_pdhl(b, hhmm);
        else if (tc_.mode == "squeeze")      mode_squeeze(b, hhmm);
        else if (tc_.mode == "supertrend")   mode_supertrend(b, hhmm);
        else if (tc_.mode == "nr7")          mode_nr7(b, hhmm);
    }

    // ── modes ────────────────────────────────────────────────────────────────
    void mode_donchian(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); const int N = tc_.donchian_n;
        if ((int)n < N + 1) return;
        double hh = std::numeric_limits<double>::lowest(), ll = std::numeric_limits<double>::max();
        for (size_t i = n - 1 - N; i < n - 1; ++i) { hh = std::max(hh, tf_[i].h); ll = std::min(ll, tf_[i].l); }
        if (b.c > hh && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "donchian_high_" + std::to_string(N));
        else if (b.c < ll && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "donchian_low_" + std::to_string(N));
    }
    void mode_ema_pullback(const Bar& b, int hhmm) {
        const size_t n = tf_.size();
        if ((int)n < tc_.ema_slow + 6 || ema_s_prev5_.size() < 6) return;
        const bool up = ema_f_ > ema_s_ && ema_s_ > ema_s_prev5_.front();
        const bool dn = ema_f_ < ema_s_ && ema_s_ < ema_s_prev5_.front();
        // flip exit
        if (sess_.in_position && tc_.exit_on_flip && ((pos_dir_ > 0 && ema_f_ < ema_s_) || (pos_dir_ < 0 && ema_f_ > ema_s_))) {
            emit(OrbSignal::FLATTEN_EOD, b.c, "ema_flip"); return;
        }
        const double tol = tc_.pullback_tol_atr * atr_;
        // a bar within the last K (excluding this one) that touched the fast EMA
        bool touched = false; double trig_hi = 0, trig_lo = 0;
        for (int k = 2; k <= tc_.pullback_lookback + 1 && (int)n - k >= 0; ++k) {
            const Bar& x = tf_[n - k];
            if (up && x.l <= ema_f_ + tol) { touched = true; trig_hi = std::max(trig_hi, x.h); }
            if (dn && x.h >= ema_f_ - tol) { touched = true; trig_lo = (trig_lo == 0) ? x.l : std::min(trig_lo, x.l); }
        }
        if (!touched) return;
        if (up && b.c > trig_hi && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "ema_pullback_long");
        else if (dn && trig_lo > 0 && b.c < trig_lo && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "ema_pullback_short");
    }
    void mode_vwap(const Bar& b, int hhmm) {
        if (!in_rth_ || vwap_v_ <= 0 || (int)vwap_hist_.size() < tc_.vwap_slope_bars * tc_.tf_min + 1) return;
        const double vw = vwap(); const double vw_prev = vwap_hist_[vwap_hist_.size() - 1 - tc_.vwap_slope_bars * tc_.tf_min];
        const double tol = tc_.vwap_tol_atr * atr_;
        const bool rising = vw > vw_prev, falling = vw < vw_prev;
        if (rising && b.l <= vw + tol && b.c > vw && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "vwap_hold_long");
        else if (falling && b.h >= vw - tol && b.c < vw && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "vwap_hold_short");
    }
    void mode_drive(const Bar& b, int hhmm) {
        if (!in_rth_) return;
        const int mod = (hhmm / 100) * 60 + hhmm % 100;
        if (!drive_done_ && mod >= rth_open_mod_ + tc_.drive_minutes) {
            drive_done_ = true;
            const double mv = b.c - rth_open_;
            drive_dir_ = std::fabs(mv) >= tc_.drive_min_atr * atr_ ? (mv > 0 ? 1 : -1) : 0;
            LOG("[TREND opening_drive] drive after %dm = %+.2f (atr %.2f) → dir %d", tc_.drive_minutes, mv, atr_, drive_dir_);
        }
        if (drive_dir_ == 0 || hhmm < tc_.drive_entry_from || hhmm >= tc_.drive_entry_to) return;
        const size_t n = tf_.size(); if (n < 3) return;
        const Bar& p = tf_[n - 2];
        // pullback bar (against the drive) followed by a bar closing back with the drive
        if (drive_dir_ > 0 && p.c < p.o && b.c > p.h && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "drive_pullback_long");
        else if (drive_dir_ < 0 && p.c > p.o && b.c < p.l && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "drive_pullback_short");
    }
    void mode_gap(const Bar& b, int hhmm) {
        if (!in_rth_ || !have_prev_ || gap_done_) return;
        const int mod = (hhmm / 100) * 60 + hhmm % 100;
        if (mod < rth_open_mod_ + tc_.gap_wait_min) return;
        gap_done_ = true;
        if (std::fabs(gap_) < tc_.gap_min_pts) { LOG("[TREND gap_go] gap %.2f < %.2f — no trade", gap_, tc_.gap_min_pts); return; }
        const double filled = (gap_ > 0) ? (rth_open_ - day_lo_) : (day_hi_ - rth_open_);
        if (filled >= tc_.gap_fill_frac * std::fabs(gap_)) { LOG("[TREND gap_go] gap %.2f already %.0f%% filled — no trade", gap_, 100 * filled / std::fabs(gap_)); return; }
        if (gap_ > 0 && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "gap_and_go_long");
        else if (gap_ < 0 && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "gap_and_go_short");
    }
    void mode_pdhl(const Bar& b, int hhmm) {
        double hi, lo;
        if (tc_.level == "overnight") { hi = on_hi_; lo = on_lo_; if (hi <= lo) return; }
        else { if (!have_prev_) return; hi = prev_hi_; lo = prev_lo_; }
        if ((int)vol_hist_.size() < tc_.vol_avg_bars + 1) return;
        double avg = 0; for (size_t i = vol_hist_.size() - 1 - tc_.vol_avg_bars; i < vol_hist_.size() - 1; ++i) avg += vol_hist_[i];
        avg /= tc_.vol_avg_bars;
        if (b.v < tc_.vol_mult * avg) return;
        const size_t n = tf_.size(); if (n < 2) return; const Bar& p = tf_[n - 2];
        if (p.c <= hi && b.c > hi && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, tc_.level + "_high_break");
        else if (p.c >= lo && b.c < lo && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, tc_.level + "_low_break");
    }
    void mode_squeeze(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.bb_len + 1) return;
        double mean = 0; for (size_t i = n - tc_.bb_len; i < n; ++i) mean += tf_[i].c; mean /= tc_.bb_len;
        double var = 0; for (size_t i = n - tc_.bb_len; i < n; ++i) var += (tf_[i].c - mean) * (tf_[i].c - mean); var /= tc_.bb_len;
        const double sd = std::sqrt(var);
        const double bb_up = mean + tc_.bb_mult * sd, bb_dn = mean - tc_.bb_mult * sd;
        const double kc_up = ema20_ + tc_.kc_mult * atr_, kc_dn = ema20_ - tc_.kc_mult * atr_;
        const bool inside = bb_up < kc_up && bb_dn > kc_dn;
        if (inside) { ++squeeze_count_; return; }
        const bool was_squeezed = squeeze_count_ >= tc_.squeeze_bars;
        squeeze_count_ = 0;
        if (!was_squeezed) return;
        if (b.c > bb_up && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "squeeze_break_long");
        else if (b.c < bb_dn && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "squeeze_break_short");
    }
    void mode_supertrend(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.st_len + 1) return;
        const double mid = (b.h + b.l) / 2.0;
        double up = mid - tc_.st_mult * atr_, dn = mid + tc_.st_mult * atr_;
        const Bar& p = tf_[n - 2];
        if (st_dir_ != 0) {
            if (p.c > st_up_) up = std::max(up, st_up_);
            if (p.c < st_dn_) dn = std::min(dn, st_dn_);
        }
        int dir = st_dir_;
        if (st_dir_ >= 0 && b.c < up) dir = -1;
        else if (st_dir_ <= 0 && b.c > dn) dir = 1;
        else if (st_dir_ == 0) dir = b.c > mid ? 1 : -1;
        const bool flipped = (dir != st_dir_) && st_dir_ != 0;
        st_up_ = up; st_dn_ = dn; st_dir_ = dir;
        if (flipped && sess_.in_position && tc_.exit_on_flip && pos_dir_ != dir) {
            reenter_dir_ = dir;                                   // stop-and-reverse once the host reports flat
            emit(OrbSignal::FLATTEN_EOD, b.c, "supertrend_flip"); return;
        }
        if (!flipped && !(reenter_dir_ != 0 && reenter_dir_ == dir)) return;
        if (dir > 0 && can_enter(hhmm, +1)) { emit(OrbSignal::BUY, b.c, flipped ? "supertrend_up" : "supertrend_reenter_up"); reenter_dir_ = 0; }
        else if (dir < 0 && can_enter(hhmm, -1)) { emit(OrbSignal::SELL, b.c, flipped ? "supertrend_down" : "supertrend_reenter_down"); reenter_dir_ = 0; }
    }
    void mode_nr7(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.nr_n + 2) return;
        // was the PREVIOUS bar the narrowest of the last nr_n bars ending at it?
        const Bar& p = tf_[n - 2];
        const double pr = p.h - p.l; bool narrowest = true;
        for (size_t i = n - 1 - tc_.nr_n; i < n - 2; ++i) if (tf_[i].h - tf_[i].l <= pr) { narrowest = false; break; }
        if (!narrowest) return;
        if (b.c > p.h && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "nr" + std::to_string(tc_.nr_n) + "_break_up");
        else if (b.c < p.l && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "nr" + std::to_string(tc_.nr_n) + "_break_down");
    }
    void mode_rs(const Bar& b, int hhmm) {
        const int L = tc_.rs_lookback_min;
        if ((int)m1_hist_.size() < L + 1 || (int)ref_closes_.size() < L + 1 || ema_f_ <= 0) return;
        const double nq_r = 1e4 * (b.c / m1_hist_[m1_hist_.size() - 1 - L].c - 1.0);
        const double es_r = 1e4 * (ref_closes_.back() / ref_closes_[ref_closes_.size() - 1 - L] - 1.0);
        const double spread = nq_r - es_r;
        if (spread >= tc_.rs_min_bp && nq_r > 0 && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "nq_leads_es_up");
        else if (spread <= -tc_.rs_min_bp && nq_r < 0 && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "nq_leads_es_down");
    }
    void mode_tod(const Bar& b, int hhmm) {
        if (tod_done_ || hhmm < tc_.tod_hhmm) return;
        const int end = tc_.tod_hhmm + tc_.tod_window_min;   // window compare in HHMM space is fine for < 60-minute windows
        if (hhmm >= end) { tod_done_ = true; return; }
        const int L = tc_.tod_lookback_min;
        if ((int)m1_hist_.size() < L + 1 || atr_ <= 0) return;
        const double mv = b.c - m1_hist_[m1_hist_.size() - 1 - L].c;
        if (std::fabs(mv) < tc_.tod_min_atr * atr_) return;
        tod_done_ = true;
        if (mv > 0 && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "tod_continuation_long");
        else if (mv < 0 && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "tod_continuation_short");
    }

    // ── state ────────────────────────────────────────────────────────────────
    TrendConfig tc_; OrbConfig risk_; SignalCallback cb_;
    TrendSession sess_;
    double last_px_ = 0; int64_t last_ts_ = 0, entry_ts_ = 0; int pos_dir_ = 0; bool eod_emitted_ = false;
    Bar m1_{}; Bar tf_cur_{}; std::vector<Bar> tf_; std::deque<Bar> m1_hist_;
    double atr_ = 0, ema_f_ = 0, ema_s_ = 0, ema20_ = 0; std::deque<double> ema_s_prev5_; std::deque<double> vol_hist_;
    // session context
    bool in_rth_ = false, rth_seen_ = false, have_prev_ = false;
    double rth_open_ = 0, rth_close_ = 0; int rth_open_mod_ = 0;
    double day_hi_ = std::numeric_limits<double>::lowest(), day_lo_ = std::numeric_limits<double>::max();
    double on_hi_ = std::numeric_limits<double>::lowest(), on_lo_ = std::numeric_limits<double>::max();
    double prev_hi_ = 0, prev_lo_ = 0, prev_close_ = 0;
    double vwap_pv_ = 0, vwap_v_ = 0; std::vector<double> vwap_hist_;
    int drive_dir_ = 0; bool drive_done_ = false; double gap_ = 0; bool gap_done_ = false; bool tod_done_ = false;
    int squeeze_count_ = 0; int st_dir_ = 0; double st_up_ = 0, st_dn_ = 0; int reenter_dir_ = 0;
    std::deque<double> ref_closes_;
};
