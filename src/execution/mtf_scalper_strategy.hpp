#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    mtf_scalper_strategy.hpp — C++ port of Pine v6 "Momentum Scalper — MTF Flag
    AutoPilot v5" (source: newstrategy, 1774 lines; port spec: agent-0d6paywo).

    Pure signal generator in the OrbStrategy idiom — no I/O, no DB, no sockets.
    Fed by on_tick(), builds 1m bars internally plus 12m (HTF) / D (Globex
    18:00 ET rollover) / W aggregators and optional 5m aggregators (FVG
    structure-TF and confirmation-TF modules). The full signal/gate/exit
    DECISION pipeline runs once per completed 1m bar (Pine
    calc_on_every_tick=false + process_orders_on_close=true, spec §6.4); the
    host manages the resting OCO bracket intrabar via cur_stop()/cur_tp().

    Pine lookahead semantics: every request.security call in the source uses
    lookahead_on with [1]/[2] offsets = completed higher-TF bars only. This
    port reads ONLY completed HTF bars — the forming HTF bar is never consulted
    (spec §1.2, §6.1).

    Host contract:
      - Wire set_signal_callback(); BUY/SELL = market entry at ~bar close
        (reverse signal while positioned = flip). FLATTEN_EOD = market flatten;
        the Pine exit reason is preserved in the reason string
        ("Osc reversal", "Time stop", "Trend reversal",
        "Session End / Out of Range").
      - After a BUY/SELL, place/maintain the OCO bracket from cur_stop() /
        cur_tp() (cur_tp() == NaN means trailing mode: cancel the limit leg)
        sized by qty_calc(equity).
      - Call notify_trade_filled(dir, exit_reason, pnl) when the position
        closes (bracket fill or flatten) — drives cooldown / daily-loss
        guardrails. On a reversal call it for the closed leg BEFORE the new
        leg's notify_entry_filled.
      - notify_entry_filled(dir, avg_price) is optional; if omitted the
        strategy assumes a fill at the signal bar's close (Pine
        process_orders_on_close semantics).
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_strategy.hpp"        // OrbTick, OrbSignal, MinuteBar, us_et_offset (via orb_config.hpp)
#include "mtf_scalper_config.hpp"
#include "log.hpp"
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <vector>

namespace mtf {

inline constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
inline bool is_na(double v) { return std::isnan(v); }

// Pine ta.ema: alpha = 2/(len+1), seeded with the first value.
inline double ema_next(double prev, double x, int len) {
    if (is_na(prev)) return x;
    const double a = 2.0 / (len + 1.0);
    return a * x + (1.0 - a) * prev;
}
// Pine ta.rma (Wilder): alpha = 1/len, seeded with the first value.
inline double rma_next(double prev, double x, int len) {
    if (is_na(prev)) return x;
    return (prev * (len - 1) + x) / len;
}
// Pine ta.percentrank(src, len): 100 × count(src[i] <= src[0], i=1..len) / len.
// hist holds oldest→newest INCLUDING the current value at back().
inline double percentrank(const std::deque<double>& hist, int len) {
    if ((int)hist.size() < len + 1) return NaN;
    const double cur = hist.back();
    int cnt = 0;
    for (int i = (int)hist.size() - 1 - len; i <= (int)hist.size() - 2; ++i)
        if (hist[i] <= cur) ++cnt;
    return 100.0 * cnt / len;
}
// Pine ta.sma over the last n values; NaN if any value in the window is na.
inline double sma_window(const std::deque<double>& hist, int n) {
    if ((int)hist.size() < n) return NaN;
    double s = 0.0;
    for (int i = (int)hist.size() - n; i < (int)hist.size(); ++i) {
        if (is_na(hist[i])) return NaN;
        s += hist[i];
    }
    return s / n;
}

} // namespace mtf

// ─── Bracket spec exposed to the host (spec §2) ─────────────────────────────
struct BracketSpec {
    bool        valid  = false;
    OrbSignal   dir    = OrbSignal::NONE;
    double      stop   = mtf::NaN;   // ratcheting stop (BE latch = exactly avg)
    double      tp     = mtf::NaN;   // NaN = trailing armed, fixed TP retired
    int         qty    = 0;
    double      entry_price = mtf::NaN;
    double      entry_atr   = mtf::NaN;
    double      r_unit      = 0.0;   // 1.2 × entryATR (ATR mode)
    bool        be_on       = false;
    bool        trail_on    = false;
    std::string reason;              // entry trigger that opened the bracket
};

// ─── Blocker-funnel counters (Pine :1166-1212, spec §7) ─────────────────────
struct MtfFunnel {
    long trig_l = 0, trig_s = 0, sig_l = 0, sig_s = 0;
    long bk_sess = 0, bk_atr = 0, bk_regime = 0, bk_guard = 0,
         bk_htf = 0, bk_trend = 0, bk_conf = 0;
};

class MtfScalperStrategy {
public:
    using SignalCallback = std::function<void(OrbSignal, double price, const std::string& reason)>;

    explicit MtfScalperStrategy(const MtfScalperConfig& cfg)
        : cfg_(cfg), equity_(cfg.starting_balance) {
        parse_session_window();
        if (cfg_.trigger_mode == "auto") {
            if (cfg_.auto_mode_flag_fix)
                LOG("[MTF] auto_mode_flag_fix=true — Auto mode uses INTENDED semantics "
                    "(flagTrig = bullBreak or retest; stoch analog live)");
            else
                LOG("[MTF] auto_mode_flag_fix=false — reproducing Pine bug: flag/stoch "
                    "triggers are dead in Auto mode");
        }
        if (cfg_.use_smt_entry && cfg_.reference_symbol.empty())
            LOG("[MTF] SMT disabled — no reference feed (use_smt_entry=true but "
                "reference_symbol empty; SMT triggers will never fire)");
        if (cfg_.use_im_filter && cfg_.reference_symbol.empty())
            LOG("[MTF] WARNING: use_im_filter=true with no reference feed — "
                "correlation gate will BLOCK all entries");
    }

    void set_signal_callback(SignalCallback cb) { signal_cb_ = std::move(cb); }

    // ── Session reset: clears position/guardrail/session state but KEEPS ────
    // ── indicator warmup (bar history, EMAs, ATR) — unlike OrbStrategy. ─────
    void reset_session() {
        clear_position();
        trades_today_   = 0;
        day_closed_pnl_ = 0.0;
        funnel_         = MtfFunnel{};
        eod_flat_emitted_ = false;
        LOG("[MTF] Session reset — warmup kept (%ld bars), window %s ET",
            bar_index_ + 1, cfg_.session_window.c_str());
    }

    // ── Tick ingestion: build 1m bars; pipeline runs on completed bars ──────
    void on_tick(const OrbTick& tick) {
        if (risk_halted_) return;

        const int cur_min = static_cast<int>(tick.ts_micros / 1'000'000 / 60);
        if (cur_bar_.minute_utc < 0) cur_bar_.minute_utc = cur_min;

        if (cur_min != cur_bar_.minute_utc) {
            handle_completed_bar(cur_bar_);
            cur_bar_ = MinuteBar{};
            cur_bar_.minute_utc = cur_min;
        }
        cur_bar_.update(tick.price, tick.size);
        last_price_ = tick.price;
    }

    // ── Trade-close notification from the host (drives guardrails §4) ───────
    // dir = direction of the CLOSED position (BUY=was long, SELL=was short).
    void notify_trade_filled(OrbSignal dir, const std::string& exit_reason = "",
                             double pnl = 0.0) {
        // Guardrail accounting (Pine :1060-1070 — every closed trade).
        day_closed_pnl_ += pnl;
        equity_         += pnl;
        if (pnl < 0.0) {
            ++loss_streak_;
            if (loss_streak_ >= cfg_.cooldown_after_losses) {
                last_loss_bar_ = bar_index_;
                LOG("[MTF] Loss streak %d — entries blocked for %d bars",
                    loss_streak_, cfg_.cooldown_bars);
            }
        } else {
            loss_streak_ = 0;
        }
        // Clear position only if this notification matches the open leg —
        // on a reversal the host reports the closed leg AFTER the strategy
        // already tracks the new one.
        const int closed_dir = (dir == OrbSignal::BUY) ? +1 : (dir == OrbSignal::SELL) ? -1 : 0;
        if (pos_dir_ == 0 || closed_dir == pos_dir_)
            clear_position();
        LOG("[MTF] Trade closed (%s) pnl=%.2f streak=%d trades_today=%d/%d",
            exit_reason.empty() ? "?" : exit_reason.c_str(), pnl,
            loss_streak_, trades_today_, cfg_.max_daily_trades);
    }

    // ── Optional: refine the assumed entry price with the actual fill ───────
    void notify_entry_filled(OrbSignal dir, double avg_price) {
        const int d = (dir == OrbSignal::BUY) ? +1 : (dir == OrbSignal::SELL) ? -1 : 0;
        if (d == 0 || d != pos_dir_) return;
        entry_price_ = avg_price;
        init_bracket(entry_reason_);
        LOG("[MTF] Entry fill refined: %s @ %.2f (stop=%.2f tp=%.2f)",
            d > 0 ? "LONG" : "SHORT", avg_price, cur_stop_, cur_tp_);
    }

    // ── Reference-symbol 1m bars (SMT / correlation, spec §1.8) ─────────────
    // Never called in the current engine (no DXY subscription) → SMT inert.
    void on_reference_bar(double high, double low, double close) {
        if (!ref_wired_) {
            ref_wired_ = true;
            LOG("[MTF] Reference feed wired (%s) — SMT machinery live",
                cfg_.reference_symbol.empty() ? "?" : cfg_.reference_symbol.c_str());
        }
        ref_hist_.push_back({0, 0.0, high, low, close, 1, true});
        if (ref_hist_.size() > 512) ref_hist_.pop_front();
        ref_close_hist_.push_back(close);
        if (ref_close_hist_.size() > 512) ref_close_hist_.pop_front();
        // Reference pivots, confirmed smt_len bars late (Pine :1008-1029).
        const int L = cfg_.smt_pivot_len;
        const int n = (int)ref_hist_.size();
        const int c = n - 1 - L;
        if (c - L >= 0) {
            bool ph = true, pl = true;
            for (int i = c - L; i <= c + L; ++i) {
                if (i == c) continue;
                if (ref_hist_[i].high >= ref_hist_[c].high) ph = false;
                if (ref_hist_[i].low  <= ref_hist_[c].low)  pl = false;
            }
            if (ph) { rf_hi2_ = rf_hi1_; rf_hi1_ = ref_hist_[c].high; }
            if (pl) { rf_lo2_ = rf_lo1_; rf_lo1_ = ref_hist_[c].low; }
        }
    }

    // ── Periodic time-based flatten (host may call once/sec) ────────────────
    // Covers the case where no ticks arrive after the session window closes.
    void check_time_flatten(int et_hour, int et_min) {
        if (!cfg_.use_session || !cfg_.flat_at_session_end) return;
        if (pos_dir_ == 0 || exit_pending_ || eod_flat_emitted_) return;
        const int t = et_hour * 60 + et_min;
        const bool past_end = sess_end_min_ > sess_start_min_
            ? (t >= sess_end_min_ || t < sess_start_min_)
            : (t >= sess_end_min_ && t < sess_start_min_);
        if (past_end) {
            eod_flat_emitted_ = true;
            exit_pending_ = true;
            LOG("[MTF] Session-end time flatten at ET %02d:%02d", et_hour, et_min);
            if (signal_cb_) signal_cb_(OrbSignal::FLATTEN_EOD, last_price_,
                                       "Session End / Out of Range");
        }
    }

    void halt_trading(const std::string& reason) {
        risk_halted_ = true;
        LOG("[MTF] Trading halted: %s", reason.c_str());
    }
    void unhalt_trading(const std::string& reason) {
        if (!risk_halted_) return;
        risk_halted_ = false;
        LOG("[MTF] Trading unhalted: %s", reason.c_str());
    }

    // Host seeding on restart (paper fleet): restores guardrail accounting so
    // a midday restart doesn't reset the max-trades counter, the closed-PnL
    // daily-loss base, or size positions off a cold-start equity. Positions,
    // indicators, and loss streaks are unaffected (streak rebuilds naturally).
    void seed_state(int trades_today, double day_closed_pnl, double total_pnl) {
        trades_today_   = trades_today;
        day_closed_pnl_ = day_closed_pnl;
        equity_         = cfg_.starting_balance + total_pnl;
        LOG("[MTF] Seeded state: trades_today=%d day_closed_pnl=%.2f equity=%.2f",
            trades_today_, day_closed_pnl_, equity_);
    }

    // ── Bracket / sizing accessors (host maintains the OCO) ─────────────────
    double cur_stop() const { return cur_stop_; }
    double cur_tp()   const { return cur_tp_; }   // NaN = trailing, no limit leg
    bool   warmed_up()  const { return bar_index_ + 1 >= cfg_.warmup_bars; }
    bool   in_position() const { return pos_dir_ != 0; }
    int    pos_dir()     const { return pos_dir_; }
    double entry_price() const { return entry_price_; }
    bool   exit_pending() const { return exit_pending_; }
    int    trades_today() const { return trades_today_; }
    double equity() const { return equity_; }
    const MtfFunnel& funnel() const { return funnel_; }

    // Position sizing (Pine :1270-1274): risk_pct% of equity / (slDist × pv),
    // floored to whole contracts, clamped to [min_qty, qty_max]. A floor clamp
    // can oversize risk beyond risk_pct — we log it at entry time.
    int qty_calc(double equity) const {
        const bool fixed = (cfg_.exit_sizing == "fixed");
        const double sl_dist = fixed ? cfg_.sl_points : cfg_.sl_atr_mult * atr_;
        if (!(sl_dist > 0.0) || equity <= 0.0) return 0;
        const double raw = equity * (cfg_.risk_pct / 100.0) / (sl_dist * cfg_.point_value);
        int q = (int)std::floor(raw);
        if (q < cfg_.min_qty) q = cfg_.min_qty;
        if (cfg_.qty_max > 0 && q > cfg_.qty_max) q = cfg_.qty_max;
        return q;
    }

    BracketSpec current_bracket() const {
        BracketSpec b;
        b.valid       = pos_dir_ != 0;
        b.dir         = pos_dir_ > 0 ? OrbSignal::BUY : pos_dir_ < 0 ? OrbSignal::SELL : OrbSignal::NONE;
        b.stop        = cur_stop_;
        b.tp          = cur_tp_;
        b.qty         = pos_dir_ != 0 ? qty_calc(equity_) : 0;
        b.entry_price = entry_price_;
        b.entry_atr   = entry_atr_;
        b.r_unit      = r_unit_;
        b.be_on       = be_on_;
        b.trail_on    = trail_on_;
        b.reason      = entry_reason_;
        return b;
    }

    // ── Indicator / state accessors (dashboards + tests) ────────────────────
    double ema_fast()  const { return ema_fast_; }
    double ema_slow()  const { return ema_slow_; }
    double macd_line() const { return macd_line_; }
    double macd_signal() const { return macd_sig_; }
    double macd_hist() const { return macd_hist_; }
    double rsi()  const { return rsi_; }
    double stoch_k() const { return stoch_k_; }
    double stoch_d() const { return stoch_d_; }
    double atr()  const { return atr_; }
    double vwap() const { return vwap_; }
    double di_plus()  const { return di_plus_; }
    double di_minus() const { return di_minus_; }
    double adx()  const { return adx_; }
    double atr_rank() const { return atr_rank_; }
    double adx_rank() const { return adx_rank_; }
    bool   regime_active() const { return regime_active_; }
    bool   htf_up() const { return htf_up_; }
    bool   htf_dn() const { return htf_dn_; }
    bool   mtf_up() const { return mtf_up_; }
    bool   in_session() const { return in_session_; }
    long   bars_completed() const { return bar_index_ + 1; }
    double pdh() const { return pdh_; }
    double pdl() const { return pdl_; }
    double pdc() const { return pdc_; }
    double pwh() const { return pwh_; }
    double pwl() const { return pwl_; }
    size_t live_fvg_bulls() const { return fvg_bull_.size(); }
    size_t live_fvg_bears() const { return fvg_bear_.size(); }
    double bo_lvl_l() const { return bo_lvl_l_; }
    double bo_lvl_s() const { return bo_lvl_s_; }
    bool   bull_flag() const { return bull_flag_; }
    bool   bear_flag() const { return bear_flag_; }
    bool   bull_break() const { return bull_break_; }
    bool   bear_break() const { return bear_break_; }
    bool   onset_armed_l() const { return onset_bar_l_ >= 0; }
    bool   onset_armed_s() const { return onset_bar_s_ >= 0; }

private:
    // ── Higher-TF bar aggregator (completed bars only — spec §6.1) ──────────
    struct HtfAgg {
        MinuteBar cur;
        long   bucket  = -1;
        double ema_f   = mtf::NaN, ema_s = mtf::NaN, ema_f_prev = mtf::NaN;
        double cls     = mtf::NaN, slope = mtf::NaN;
        bool   has_completed = false;
        // FVG struct mode / conf module read the raw completed bars too
        std::deque<MinuteBar> done;   // last few completed bars
        double atr_rma = mtf::NaN;    // RMA of TR over completed bars (struct FVG)
    };
    struct Gap { double top, bot; long bar; bool used; };

    MtfScalperConfig cfg_;
    SignalCallback   signal_cb_;
    MinuteBar        cur_bar_;
    std::deque<MinuteBar> hist_;       // completed 1m bars, oldest→newest
    long    bar_index_ = -1;
    bool    risk_halted_ = false;
    double  last_price_ = 0.0;
    double  equity_;

    // time/session state
    int     sess_start_min_ = 540, sess_end_min_ = 720;
    long    day_key_ = -1, week_key_ = -1;
    bool    in_session_ = false, prev_in_session_ = false;
    bool    in_date_range_ = true;
    bool    eod_flat_emitted_ = false;

    // 1m indicators (Pine :439-449)
    double ema_fast_ = mtf::NaN, ema_slow_ = mtf::NaN;
    double ema_fast_prev_ = mtf::NaN, ema_slow_prev_ = mtf::NaN;
    double macd_ef_ = mtf::NaN, macd_es_ = mtf::NaN;
    double macd_line_ = mtf::NaN, macd_sig_ = mtf::NaN, macd_hist_ = mtf::NaN;
    double macd_line_prev_ = mtf::NaN, macd_sig_prev_ = mtf::NaN, macd_hist_prev_ = mtf::NaN;
    double rma_up_ = mtf::NaN, rma_dn_ = mtf::NaN, rsi_ = mtf::NaN;
    std::deque<double> kraw_hist_, k_hist_;
    double stoch_k_ = mtf::NaN, stoch_d_ = mtf::NaN;
    double stoch_k_prev_ = mtf::NaN, stoch_d_prev_ = mtf::NaN;
    double atr_ = mtf::NaN, atr_prev_ = mtf::NaN, prev_close_ = mtf::NaN;
    std::deque<double> atr_hist_, adx_hist_;
    double atr_rank_ = 50.0, adx_rank_ = 50.0;   // nz(percentrank, 50)
    // DMI (Pine ta.dmi, :527)
    double dmi_tr_ = mtf::NaN, dmi_pdm_ = mtf::NaN, dmi_mdm_ = mtf::NaN, adx_rma_ = mtf::NaN;
    double di_plus_ = mtf::NaN, di_minus_ = mtf::NaN, adx_ = mtf::NaN;
    double prev_high_ = mtf::NaN, prev_low_ = mtf::NaN;
    // VWAP (ta.vwap(hlc3), :447) — anchor per cfg.vwap_anchor
    double vwap_pv_ = 0.0, vwap_v_ = 0.0, vwap_ = mtf::NaN;
    long   vwap_key_ = -1;

    // HTF alignment (:458-466)
    HtfAgg htf_, htf2_;
    bool   htf_up_ = false, htf_dn_ = false, mtf_up_ = false, mtf_dn_ = false;

    // Day/week levels (:730-731) — Globex 18:00 ET rollover, Monday weeks
    MinuteBar day_bar_, week_bar_;
    long   day_bar_key_ = -1, week_bar_key_ = -1;
    double pdh_ = mtf::NaN, pdl_ = mtf::NaN, pdc_ = mtf::NaN;
    double pwh_ = mtf::NaN, pwl_ = mtf::NaN;
    bool   pdh_swept_ = false, pdl_swept_ = false, pwh_swept_ = false, pwl_swept_ = false;
    // session H/L (:734-746)
    double sess_h_ = mtf::NaN, sess_l_ = mtf::NaN, psess_h_ = mtf::NaN, psess_l_ = mtf::NaN;
    bool   psh_swept_ = false, psl_swept_ = false;
    // sweeps (:848-881)
    bool   sweep_l_ = false, sweep_s_ = false;
    double sweep_lvl_l_ = mtf::NaN, sweep_lvl_s_ = mtf::NaN;

    // flag engine (:469-498)
    bool   bull_flag_ = false, bear_flag_ = false;
    bool   bull_break_ = false, bear_break_ = false;
    double flag_hi_ = mtf::NaN, flag_lo_ = mtf::NaN;
    // break-retest state machine (:1081-1108)
    double bo_lvl_l_ = mtf::NaN, bo_lvl_s_ = mtf::NaN;
    long   bo_bar_l_ = -1, bo_bar_s_ = -1;
    bool   retest_l_ = false, retest_s_ = false;

    // onset engine (:551-591)
    long   onset_bar_l_ = -1, onset_bar_s_ = -1;
    double last_ph_ = mtf::NaN, last_pl_ = mtf::NaN;
    bool   onset_pull_l_ = false, onset_pull_s_ = false;
    bool   onset_struct_l_ = false, onset_struct_s_ = false;
    bool   onset_trig_l_ = false, onset_trig_s_ = false;

    // FVG engine (:605-721)
    std::deque<Gap> fvg_bull_, fvg_bear_;
    bool   fvg_retest_l_ = false, fvg_retest_s_ = false;
    double fvg_ret_lvl_l_ = mtf::NaN, fvg_ret_lvl_s_ = mtf::NaN;
    HtfAgg struct5_;   // fvg_struct_mode aggregator
    bool   struct_rolled_ = false;

    // SMT / intermarket (:994-1040)
    bool   ref_wired_ = false;
    std::deque<MinuteBar> ref_hist_;
    std::deque<double>    ref_close_hist_;
    double ch_hi1_ = mtf::NaN, ch_hi2_ = mtf::NaN, ch_lo1_ = mtf::NaN, ch_lo2_ = mtf::NaN;
    double rf_hi1_ = mtf::NaN, rf_hi2_ = mtf::NaN, rf_lo1_ = mtf::NaN, rf_lo2_ = mtf::NaN;
    bool   smt_trig_l_ = false, smt_trig_s_ = false;

    // derived booleans / regime (:503-542)
    bool   trend_up_ = false, trend_dn_ = false, trend_up_loose_ = false, trend_dn_loose_ = false;
    bool   mom_up_ = false, mom_dn_ = false;
    bool   regime_active_ = false;
    int    regime_count_ = 0;

    // signal gate (:1129-1162) — kept as members for reason strings
    bool   flag_trig_l_ = false, flag_trig_s_ = false;
    bool   stoch_trig_l_ = false, stoch_trig_s_ = false;
    bool   onset_fired_l_ = false, onset_fired_s_ = false;
    bool   long_signal_ = false, short_signal_ = false;

    // confirmation TF (:1221-1265)
    HtfAgg conf5_;
    long   pend_bar_l_ = -1, pend_bar_s_ = -1;
    double pend_ref_l_ = mtf::NaN, pend_ref_s_ = mtf::NaN;
    bool   conf_go_l_ = false, conf_go_s_ = false;

    // position / bracket (:1270-1316, :1422-1484)
    int    pos_dir_ = 0;
    double entry_price_ = mtf::NaN, entry_atr_ = mtf::NaN;
    long   entry_bar_ = -1;
    double r_unit_ = 0.0;
    bool   be_on_ = false, trail_on_ = false, exit_pending_ = false;
    double cur_stop_ = mtf::NaN, cur_tp_ = mtf::NaN;
    double lv_tp_ = mtf::NaN;
    std::string lv_tp_name_, entry_reason_;

    // guardrails (:1045-1076)
    int    trades_today_ = 0;
    double day_start_equity_ = mtf::NaN, day_closed_pnl_ = 0.0;
    int    loss_streak_ = 0;
    long   last_loss_bar_ = -100000;

    MtfFunnel funnel_;

    // ═══════════════════════════════════════════════════════════════════════
    void parse_session_window() {
        // "0900-1200" → minutes since ET midnight
        const std::string& w = cfg_.session_window;
        if (w.size() >= 9 && w[4] == '-') {
            sess_start_min_ = std::stoi(w.substr(0, 2)) * 60 + std::stoi(w.substr(2, 2));
            sess_end_min_   = std::stoi(w.substr(5, 2)) * 60 + std::stoi(w.substr(7, 2));
        }
    }

    static void bar_et(int minute_utc, int& et_min_of_day, long& et_day, long& globex_day,
                       int rollover_hour) {
        const int64_t utc_sec = (int64_t)minute_utc * 60;
        time_t tt = (time_t)utc_sec;
        struct tm tm_utc;
        gmtime_r(&tt, &tm_utc);
        const int64_t et_sec = utc_sec - us_et_offset(tm_utc) * 3600LL;
        et_day = et_sec / 86400LL;
        const int rem = (int)(et_sec % 86400LL);
        et_min_of_day = rem / 60;
        // Globex trading day: from rollover_hour ET belongs to the next day
        globex_day = (rem / 3600 >= rollover_hour) ? et_day + 1 : et_day;
    }

    static long week_key_of(long globex_day) {
        // Monday-start weeks; day 4 (1970-01-05) was a Monday. The Sunday
        // 18:00 ET Globex open already maps to Monday's day key.
        long d = globex_day - 4;
        return d >= 0 ? d / 7 : -((-d + 6) / 7);
    }

    void update_htf(HtfAgg& a, int minutes, const MinuteBar& bar, int len_f, int len_s) {
        const long bucket = bar.minute_utc / minutes;
        if (a.bucket < 0) a.bucket = bucket;
        if (bucket != a.bucket) {
            if (a.cur.volume > 0) {
                a.done.push_back(a.cur);
                if (a.done.size() > 8) a.done.pop_front();
                a.ema_f_prev = a.ema_f;
                a.ema_f = mtf::ema_next(a.ema_f, a.cur.close, len_f);
                a.ema_s = mtf::ema_next(a.ema_s, a.cur.close, len_s);
                a.cls   = a.cur.close;
                a.slope = (!mtf::is_na(a.ema_f) && !mtf::is_na(a.ema_f_prev))
                          ? a.ema_f - a.ema_f_prev : mtf::NaN;
                a.has_completed = true;
                // struct-mode ATR over completed HTF bars (Pine ta.atr on 5m)
                const size_t n = a.done.size();
                const double pc = n >= 2 ? a.done[n - 2].close : mtf::NaN;
                const double tr = mtf::is_na(pc)
                    ? a.cur.high - a.cur.low
                    : std::max({a.cur.high - a.cur.low,
                                std::fabs(a.cur.high - pc), std::fabs(a.cur.low - pc)});
                a.atr_rma = mtf::rma_next(a.atr_rma, tr, cfg_.atr_len);
            }
            a.cur = MinuteBar{};
            a.cur.minute_utc = bar.minute_utc;
            a.bucket = bucket;
        }
        if (a.cur.volume == 0) {
            a.cur.open = bar.open; a.cur.high = bar.high; a.cur.low = bar.low;
        }
        if (bar.high > a.cur.high) a.cur.high = bar.high;
        if (bar.low  < a.cur.low)  a.cur.low  = bar.low;
        a.cur.close = bar.close;
        a.cur.volume += bar.volume;
    }

    // ═══════════════════════════════════════════════════════════════════════
    // Main pipeline — one call per completed 1m bar (Pine calc order).
    // ═══════════════════════════════════════════════════════════════════════
    void handle_completed_bar(const MinuteBar& bar) {
        if (bar.volume == 0) return;
        ++bar_index_;
        hist_.push_back(bar);
        if (hist_.size() > 4096) hist_.pop_front();
        const long bi = bar_index_;

        int et_mod; long et_day, gday;
        bar_et(bar.minute_utc, et_mod, et_day, gday, cfg_.day_rollover_hour_et);
        const long wkey = week_key_of(gday);
        const bool new_day  = (gday != day_key_);
        const bool new_week = (wkey != week_key_);
        day_key_ = gday; week_key_ = wkey;

        // ── "D"/"W" security: completed prior day/week (Pine :730-731) ─────
        if (new_day) {
            if (day_bar_key_ >= 0 && day_bar_.volume > 0) {
                pdh_ = day_bar_.high; pdl_ = day_bar_.low; pdc_ = day_bar_.close;
            }
            day_bar_ = MinuteBar{}; day_bar_key_ = gday;
            pdh_swept_ = pdl_swept_ = false;
            trades_today_ = 0;
            day_closed_pnl_ = 0.0;
            day_start_equity_ = equity_;
            eod_flat_emitted_ = false;
        }
        if (new_week) {
            if (week_bar_key_ >= 0 && week_bar_.volume > 0) {
                pwh_ = week_bar_.high; pwl_ = week_bar_.low;
            }
            week_bar_ = MinuteBar{}; week_bar_key_ = wkey;
            pwh_swept_ = pwl_swept_ = false;
        }
        auto agg_update = [](MinuteBar& a, const MinuteBar& b) {
            if (a.volume == 0) { a.open = b.open; a.high = b.high; a.low = b.low; }
            if (b.high > a.high) a.high = b.high;
            if (b.low  < a.low)  a.low  = b.low;
            a.close = b.close; a.volume += b.volume;
        };
        agg_update(day_bar_, bar);
        agg_update(week_bar_, bar);

        // ── Session window (Pine :520) — bar-open time in ET ────────────────
        in_session_ = !cfg_.use_session ||
            (sess_end_min_ > sess_start_min_
                ? (et_mod >= sess_start_min_ && et_mod < sess_end_min_)
                : (et_mod >= sess_start_min_ || et_mod < sess_end_min_));
        const int64_t bar_epoch = (int64_t)bar.minute_utc * 60;
        in_date_range_ = (cfg_.date_start_epoch == 0 || bar_epoch >= cfg_.date_start_epoch) &&
                         (cfg_.date_end_epoch   == 0 || bar_epoch <= cfg_.date_end_epoch);
        const bool sess_start = cfg_.use_session && in_session_ && !prev_in_session_;

        // ── Session H/L + sweep-flag resets (Pine :734-765) ─────────────────
        if (sess_start) {
            psess_h_ = sess_h_; psess_l_ = sess_l_;
            sess_h_ = bar.high; sess_l_ = bar.low;
            psh_swept_ = psl_swept_ = false;
        }
        if (cfg_.use_session && in_session_) {
            sess_h_ = mtf::is_na(sess_h_) ? bar.high : std::max(sess_h_, bar.high);
            sess_l_ = mtf::is_na(sess_l_) ? bar.low  : std::min(sess_l_, bar.low);
        }

        // ── VWAP (ta.vwap(hlc3), :447) with configurable anchor (§6.5) ─────
        {
            long key;
            if (cfg_.vwap_anchor == "rth")
                key = (et_mod >= 570) ? et_day : et_day - 1;   // RTH day = 9:30 ET
            else if (cfg_.vwap_anchor == "session_window")
                key = sess_start ? bar_index_ : vwap_key_;     // reset at window open
            else
                key = gday;                                     // "globex" default
            if (key != vwap_key_) { vwap_key_ = key; vwap_pv_ = 0.0; vwap_v_ = 0.0; }
            const double tp = (bar.high + bar.low + bar.close) / 3.0;
            if (bar.volume > 0) {
                vwap_pv_ += tp * (double)bar.volume;
                vwap_v_  += (double)bar.volume;
                vwap_ = vwap_pv_ / vwap_v_;
            }
        }

        // ── HTF aggregation: 12m (+144m if use_htf2) — completed only ──────
        update_htf(htf_, cfg_.htf_minutes(), bar, cfg_.htf_fast_len, cfg_.htf_slow_len);
        if (cfg_.use_htf2)
            update_htf(htf2_, cfg_.htf2_minutes(), bar, cfg_.htf_fast_len, cfg_.htf_slow_len);
        if (cfg_.fvg_struct_mode) {
            const size_t done_before = struct5_.done.size();
            update_htf(struct5_, cfg_.fvg_struct_tf_min, bar, 2, 3);  // emas unused
            struct_rolled_ = struct5_.done.size() != done_before;
        } else {
            struct_rolled_ = false;
        }
        // conf5_ rollover detected BEFORE the update so the conf module below
        // reads the just-completed confirm-TF bar from done.back().
        const bool conf_rolled = cfg_.use_conf_tf &&
            conf5_.bucket >= 0 && conf5_.bucket != bar.minute_utc / cfg_.conf_tf_min;
        if (cfg_.use_conf_tf) update_htf(conf5_, cfg_.conf_tf_min, bar, 2, 3);

        // ── 1m indicators (Pine :439-449) ───────────────────────────────────
        ema_fast_prev_ = ema_fast_; ema_slow_prev_ = ema_slow_;
        ema_fast_ = mtf::ema_next(ema_fast_, bar.close, cfg_.ema_fast_len);
        ema_slow_ = mtf::ema_next(ema_slow_, bar.close, cfg_.ema_slow_len);

        macd_line_prev_ = macd_line_; macd_sig_prev_ = macd_sig_; macd_hist_prev_ = macd_hist_;
        macd_ef_ = mtf::ema_next(macd_ef_, bar.close, cfg_.macd_fast);
        macd_es_ = mtf::ema_next(macd_es_, bar.close, cfg_.macd_slow);
        macd_line_ = macd_ef_ - macd_es_;
        macd_sig_  = mtf::ema_next(macd_sig_, macd_line_, cfg_.macd_signal);
        macd_hist_ = macd_line_ - macd_sig_;

        if (!mtf::is_na(prev_close_)) {
            const double chg = bar.close - prev_close_;
            rma_up_ = mtf::rma_next(rma_up_, std::max(chg, 0.0), cfg_.rsi_len);
            rma_dn_ = mtf::rma_next(rma_dn_, std::max(-chg, 0.0), cfg_.rsi_len);
            rsi_ = (rma_dn_ == 0.0) ? 100.0
                 : (rma_up_ == 0.0) ? 0.0
                 : 100.0 - 100.0 / (1.0 + rma_up_ / rma_dn_);
        }
        prev_close_ = bar.close;

        // Stochastic (:443-445)
        stoch_k_prev_ = stoch_k_; stoch_d_prev_ = stoch_d_;
        if ((int)hist_.size() >= cfg_.stoch_k_len) {
            double hh = -1e300, ll = 1e300;
            for (size_t i = hist_.size() - cfg_.stoch_k_len; i < hist_.size(); ++i) {
                hh = std::max(hh, hist_[i].high);
                ll = std::min(ll, hist_[i].low);
            }
            kraw_hist_.push_back(hh > ll ? 100.0 * (bar.close - ll) / (hh - ll) : mtf::NaN);
        } else {
            kraw_hist_.push_back(mtf::NaN);
        }
        if (kraw_hist_.size() > 64) kraw_hist_.pop_front();
        stoch_k_ = mtf::sma_window(kraw_hist_, cfg_.stoch_k_smooth);
        k_hist_.push_back(stoch_k_);
        if (k_hist_.size() > 64) k_hist_.pop_front();
        stoch_d_ = mtf::sma_window(k_hist_, cfg_.stoch_d_len);

        // ATR (ta.atr = RMA of TR, :446)
        atr_prev_ = atr_;
        const double tr = mtf::is_na(prev_high_)
            ? bar.high - bar.low
            : std::max({bar.high - bar.low,
                        std::fabs(bar.high - hist_[hist_.size() - 2].close),
                        std::fabs(bar.low - hist_[hist_.size() - 2].close)});
        atr_ = mtf::rma_next(atr_, tr, cfg_.atr_len);
        atr_hist_.push_back(atr_);
        if ((int)atr_hist_.size() > cfg_.rank_lookback + 2) atr_hist_.pop_front();

        // DMI/ADX (ta.dmi(regAdxLen, regAdxLen), :527) — RMA form per Pine impl
        if (!mtf::is_na(prev_high_)) {
            const double up = bar.high - prev_high_;
            const double dn = prev_low_ - bar.low;
            const double pdm = (up > dn && up > 0.0) ? up : 0.0;
            const double mdm = (dn > up && dn > 0.0) ? dn : 0.0;
            dmi_tr_  = mtf::rma_next(dmi_tr_, tr, cfg_.adx_len);
            dmi_pdm_ = mtf::rma_next(dmi_pdm_, pdm, cfg_.adx_len);
            dmi_mdm_ = mtf::rma_next(dmi_mdm_, mdm, cfg_.adx_len);
            if (dmi_tr_ > 0.0) {
                di_plus_  = 100.0 * dmi_pdm_ / dmi_tr_;
                di_minus_ = 100.0 * dmi_mdm_ / dmi_tr_;
                const double sum = di_plus_ + di_minus_;
                const double dx  = 100.0 * std::fabs(di_plus_ - di_minus_) / (sum == 0.0 ? 1.0 : sum);
                adx_rma_ = mtf::rma_next(adx_rma_, dx, cfg_.adx_len);
                adx_ = adx_rma_;
            }
        }
        prev_high_ = bar.high; prev_low_ = bar.low;
        adx_hist_.push_back(adx_);
        if ((int)adx_hist_.size() > cfg_.rank_lookback + 2) adx_hist_.pop_front();

        // Percentile ranks (:449, :529) — nz(...,50) during warmup
        {
            const double ar = mtf::percentrank(atr_hist_, cfg_.rank_lookback);
            atr_rank_ = mtf::is_na(ar) ? 50.0 : ar;
            const double xr = mtf::percentrank(adx_hist_, cfg_.rank_lookback);
            adx_rank_ = mtf::is_na(xr) ? 50.0 : xr;
        }

        // ── HTF booleans (:461-466) ─────────────────────────────────────────
        if (htf_.has_completed) {
            htf_up_ = htf_.ema_f > htf_.ema_s &&
                      (!cfg_.htf_need_price || htf_.cls > htf_.ema_f) &&
                      (!cfg_.htf_slope_req || (!mtf::is_na(htf_.slope) && htf_.slope > 0.0));
            htf_dn_ = htf_.ema_f < htf_.ema_s &&
                      (!cfg_.htf_need_price || htf_.cls < htf_.ema_f) &&
                      (!cfg_.htf_slope_req || (!mtf::is_na(htf_.slope) && htf_.slope < 0.0));
        } else { htf_up_ = htf_dn_ = false; }
        const bool htf2_up = !cfg_.use_htf2 || (htf2_.has_completed && htf2_.ema_f > htf2_.ema_s);
        const bool htf2_dn = !cfg_.use_htf2 || (htf2_.has_completed && htf2_.ema_f < htf2_.ema_s);
        mtf_up_ = !cfg_.use_mtf || (htf_up_ && htf2_up);
        mtf_dn_ = !cfg_.use_mtf || (htf_dn_ && htf2_dn);

        // ── Derived booleans (:503-518) ─────────────────────────────────────
        trend_up_loose_ = ema_fast_ > ema_slow_;
        trend_dn_loose_ = ema_fast_ < ema_slow_;
        trend_up_ = trend_up_loose_ && (!cfg_.require_price_beyond_fast || bar.close > ema_fast_);
        trend_dn_ = trend_dn_loose_ && (!cfg_.require_price_beyond_fast || bar.close < ema_fast_);
        mom_up_ = !cfg_.use_macd || (macd_hist_ > 0.0 &&
                  (!cfg_.macd_hist_expanding || macd_hist_ > macd_hist_prev_));
        mom_dn_ = !cfg_.use_macd || (macd_hist_ < 0.0 &&
                  (!cfg_.macd_hist_expanding || macd_hist_ < macd_hist_prev_));
        const bool rsi_bull = rsi_ > cfg_.rsi_mid, rsi_bear = rsi_ < cfg_.rsi_mid;
        const bool stoch_xup = !mtf::is_na(stoch_k_) && !mtf::is_na(stoch_d_) &&
            !mtf::is_na(stoch_k_prev_) && !mtf::is_na(stoch_d_prev_) &&
            stoch_k_ > stoch_d_ && stoch_k_prev_ <= stoch_d_prev_ && stoch_k_ < cfg_.stoch_ob;
        const bool stoch_xdn = !mtf::is_na(stoch_k_) && !mtf::is_na(stoch_d_) &&
            !mtf::is_na(stoch_k_prev_) && !mtf::is_na(stoch_d_prev_) &&
            stoch_k_ < stoch_d_ && stoch_k_prev_ >= stoch_d_prev_ && stoch_k_ > cfg_.stoch_os;
        const bool vwap_bull = !cfg_.use_vwap || (!mtf::is_na(vwap_) && bar.close > vwap_);
        const bool vwap_bear = !cfg_.use_vwap || (!mtf::is_na(vwap_) && bar.close < vwap_);

        // ATR floor (:522-524)
        const bool rel_mode = (cfg_.regime_mode != "fixed");
        const bool atr_ok = !cfg_.use_atr_floor ||
            (rel_mode ? atr_rank_ >= cfg_.atr_rank_min
                      : atr_ / cfg_.tick_size >= cfg_.min_atr_ticks);

        // ── Regime filter (:526-542) — N of M components, relative default ──
        {
            double atr_avg50 = mtf::sma_window(atr_hist_, std::min(cfg_.atr_avg_len, (int)atr_hist_.size()));
            double atr_sma20 = mtf::sma_window(atr_hist_, std::min(20, (int)atr_hist_.size()));
            const double adx_prev = adx_hist_.size() >= 2 ? adx_hist_[adx_hist_.size() - 2] : mtf::NaN;
            const bool adx_pass_raw = rel_mode ? adx_rank_ >= cfg_.adx_rank_min
                                               : !mtf::is_na(adx_) && adx_ >= cfg_.adx_min;
            const bool adx_pass = adx_pass_raw ||
                (cfg_.adx_rising_pass && !mtf::is_na(adx_) && !mtf::is_na(adx_prev) &&
                 adx_ > adx_prev && adx_ > 15.0);
            const bool atr_pass_raw = rel_mode ? atr_rank_ >= cfg_.atr_rank_min_regime
                                               : !mtf::is_na(atr_avg50) && atr_ >= atr_avg50 * (cfg_.atr_pct_of_avg / 100.0);
            const bool atr_exp_now = !mtf::is_na(atr_prev_) && !mtf::is_na(atr_sma20) &&
                                     atr_ > atr_prev_ && atr_ > atr_sma20;
            const bool atr_pass = atr_pass_raw || (cfg_.atr_expansion_pass && atr_exp_now);
            const bool spr_pass = std::fabs(ema_fast_ - ema_slow_) >= cfg_.ema_spread_min_atr * atr_;
            const int enabled = (cfg_.reg_adx_on ? 1 : 0) + (cfg_.reg_atr_on ? 1 : 0) +
                                (cfg_.reg_spread_on ? 1 : 0);
            const int passed = (cfg_.reg_adx_on && adx_pass ? 1 : 0) +
                               (cfg_.reg_atr_on && atr_pass ? 1 : 0) +
                               (cfg_.reg_spread_on && spr_pass ? 1 : 0);
            const bool raw = !cfg_.use_regime || enabled == 0 ||
                             passed >= std::min(cfg_.regime_components_required, enabled);
            regime_count_ = raw ? regime_count_ + 1 : 0;
            regime_active_ = regime_count_ >= cfg_.regime_bars;
        }

        // ── Flag pattern engine (:469-498) ──────────────────────────────────
        bull_flag_ = bear_flag_ = bull_break_ = bear_break_ = false;
        flag_hi_ = flag_lo_ = mtf::NaN;
        {
            const int pole_eff = cfg_.adaptive_pole
                ? std::max(2, (int)std::lround(cfg_.pole_len_max -
                        (atr_rank_ / 100.0) * (cfg_.pole_len_max - cfg_.pole_len_min)))
                : cfg_.pole_len_fixed;
            const int win = pole_eff + cfg_.flag_len + 2;
            const int n = (int)hist_.size();
            if (n >= win + 1 && !mtf::is_na(atr_)) {
                // window = high[1..win] → indices n-1-win .. n-2
                double hi_win = -1e300, lo_win = 1e300;
                int hi_idx = -1, lo_idx = -1;
                for (int i = n - 1 - win; i <= n - 2; ++i) {
                    if (hist_[i].high > hi_win) { hi_win = hist_[i].high; hi_idx = i; }
                    if (hist_[i].low  < lo_win) { lo_win = hist_[i].low;  lo_idx = i; }
                }
                const int hi_off = hi_idx - (n - 1);   // ta.highestbars: negative
                const int lo_off = lo_idx - (n - 1);
                double fhi = -1e300, flo = 1e300;
                for (int i = n - 1 - cfg_.flag_len; i <= n - 2; ++i) {
                    fhi = std::max(fhi, hist_[i].high);
                    flo = std::min(flo, hist_[i].low);
                }
                flag_hi_ = fhi; flag_lo_ = flo;
                // flagSlope = linreg(close[1],len,0) - linreg(close[1],len,1) = LS slope
                double sx = 0, sy = 0, sxy = 0, sxx = 0;
                for (int k = 0; k < cfg_.flag_len; ++k) {
                    const double y = hist_[n - 1 - cfg_.flag_len + k].close; // wait: close[1..len]
                    sx += k; sy += y; sxy += k * y; sxx += (double)k * k;
                }
                const double den = cfg_.flag_len * sxx - sx * sx;
                const double slope = den != 0.0 ? (cfg_.flag_len * sxy - sx * sy) / den : 0.0;
                const double pole_rise = hi_win - lo_win;
                const bool pole_up = pole_rise >= cfg_.pole_min_atr * atr_ &&
                                     lo_off < hi_off && (-hi_off) <= cfg_.pole_recency_bars;
                const bool pole_dn = pole_rise >= cfg_.pole_min_atr * atr_ &&
                                     hi_off < lo_off && (-lo_off) <= cfg_.pole_recency_bars;
                const bool rng_ok = (fhi - flo) <= cfg_.flag_max_rng_atr * atr_;
                const bool retr_bull = (hi_win - flo) <= (cfg_.max_retr_pct / 100.0) * pole_rise;
                const bool retr_bear = (fhi - lo_win) <= (cfg_.max_retr_pct / 100.0) * pole_rise;
                const bool drift_bull = slope <= 0.25 * atr_ && slope >= -cfg_.flag_drift_atr * atr_;
                const bool drift_bear = slope >= -0.25 * atr_ && slope <= cfg_.flag_drift_atr * atr_;
                bull_flag_ = pole_up && rng_ok && retr_bull && drift_bull;
                bear_flag_ = pole_dn && rng_ok && retr_bear && drift_bear;
                bull_break_ = bull_flag_ && bar.close > fhi;
                bear_break_ = bear_flag_ && bar.close < flo;
            }
        }

        // ── Trend-onset engine (:551-591) ───────────────────────────────────
        {
            const bool flip_l = !mtf::is_na(ema_fast_prev_) &&
                ema_fast_ > ema_slow_ && ema_fast_prev_ <= ema_slow_prev_;
            const bool flip_s = !mtf::is_na(ema_fast_prev_) &&
                ema_fast_ < ema_slow_ && ema_fast_prev_ >= ema_slow_prev_;
            const bool macd_xup = !mtf::is_na(macd_line_prev_) &&
                macd_line_ > macd_sig_ && macd_line_prev_ <= macd_sig_prev_;
            const bool macd_xdn = !mtf::is_na(macd_line_prev_) &&
                macd_line_ < macd_sig_ && macd_line_prev_ >= macd_sig_prev_;
            const bool oflip_l = flip_l && (!cfg_.onset_need_macd || macd_hist_ > 0.0 || macd_xup);
            const bool oflip_s = flip_s && (!cfg_.onset_need_macd || macd_hist_ < 0.0 || macd_xdn);
            if (oflip_l && onset_bar_l_ < 0 && pos_dir_ <= 0) onset_bar_l_ = bi;
            if (oflip_s && onset_bar_s_ < 0 && pos_dir_ >= 0) onset_bar_s_ = bi;
            const bool live_l = onset_bar_l_ >= 0 &&
                bi - onset_bar_l_ <= cfg_.onset_window_bars && trend_up_loose_;
            const bool live_s = onset_bar_s_ >= 0 &&
                bi - onset_bar_s_ <= cfg_.onset_window_bars && trend_dn_loose_;

            onset_pull_l_ = live_l && bar.low <= ema_fast_ && bar.close > ema_fast_ &&
                bar.close <= ema_fast_ + cfg_.onset_max_ext_atr * atr_ && mom_up_;
            onset_pull_s_ = live_s && bar.high >= ema_fast_ && bar.close < ema_fast_ &&
                bar.close >= ema_fast_ - cfg_.onset_max_ext_atr * atr_ && mom_dn_;

            // Pivots confirmed swing_len bars late (:573-579)
            const int L = cfg_.onset_swing_len;
            const int n = (int)hist_.size();
            const int c = n - 1 - L;
            if (c - L >= 0) {
                bool ph = true, pl = true;
                for (int i = c - L; i <= c + L; ++i) {
                    if (i == c) continue;
                    if (hist_[i].high >= hist_[c].high) ph = false;
                    if (hist_[i].low  <= hist_[c].low)  pl = false;
                }
                if (ph) last_ph_ = hist_[c].high;
                if (pl) last_pl_ = hist_[c].low;
            }
            onset_struct_l_ = live_l && !mtf::is_na(last_ph_) &&
                bar.close > last_ph_ && bar.close <= last_ph_ + 0.5 * atr_;
            onset_struct_s_ = live_s && !mtf::is_na(last_pl_) &&
                bar.close < last_pl_ && bar.close >= last_pl_ - 0.5 * atr_;
            onset_trig_l_ = cfg_.use_onset && (onset_pull_l_ || onset_struct_l_);
            onset_trig_s_ = cfg_.use_onset && (onset_pull_s_ || onset_struct_s_);
            if (onset_trig_l_ || !trend_up_loose_) onset_bar_l_ = -1;
            if (onset_trig_s_ || !trend_dn_loose_) onset_bar_s_ = -1;
        }

        // ── FVG engine (:605-721) ───────────────────────────────────────────
        fvg_retest_l_ = fvg_retest_s_ = false;
        fvg_ret_lvl_l_ = fvg_ret_lvl_s_ = mtf::NaN;
        if (cfg_.use_fvg) {
            const int n = (int)hist_.size();
            if (!cfg_.fvg_struct_mode) {
                if (n >= 3 && bar.low > hist_[n - 3].high &&
                    bar.low - hist_[n - 3].high >= cfg_.fvg_min_atr * atr_)
                    fvg_bull_.push_back({bar.low, hist_[n - 3].high, bi, false});
                if (n >= 3 && bar.high < hist_[n - 3].low &&
                    hist_[n - 3].low - bar.high >= cfg_.fvg_min_atr * atr_)
                    fvg_bear_.push_back({hist_[n - 3].low, bar.high, bi, false});
            } else if (struct_rolled_) {
                // Zones from COMPLETED struct-TF bars (Pine :605-638):
                // s1 = last completed, s3 = 3rd-last completed struct bar.
                const size_t nd = struct5_.done.size();
                if (nd >= 3 && !mtf::is_na(struct5_.atr_rma)) {
                    const MinuteBar& s1 = struct5_.done[nd - 1];
                    const MinuteBar& s3 = struct5_.done[nd - 3];
                    if (s1.low > s3.high &&
                        s1.low - s3.high >= cfg_.fvg_min_atr * struct5_.atr_rma)
                        fvg_bull_.push_back({s1.low, s3.high, bi, false});
                    if (s1.high < s3.low &&
                        s3.low - s1.high >= cfg_.fvg_min_atr * struct5_.atr_rma)
                        fvg_bear_.push_back({s3.low, s1.high, bi, false});
                }
            }
            fvg_maintain(fvg_bull_, true, bar, bi);
            fvg_maintain(fvg_bear_, false, bar, bi);
            while ((int)fvg_bull_.size() > cfg_.fvg_max_keep) fvg_bull_.pop_front();
            while ((int)fvg_bear_.size() > cfg_.fvg_max_keep) fvg_bear_.pop_front();
        }

        // ── Liquidity sweeps (:848-881) — one signal per level per period ───
        sweep_l_ = sweep_s_ = false;
        sweep_lvl_l_ = sweep_lvl_s_ = mtf::NaN;
        if (cfg_.use_levels) {
            const double pen = cfg_.sweep_pen_atr * atr_;
            if (cfg_.lv_pd && !mtf::is_na(pdl_) && !pdl_swept_ &&
                bar.low < pdl_ - pen && bar.close > pdl_) {
                pdl_swept_ = true; sweep_l_ = true; sweep_lvl_l_ = pdl_;
            }
            if (cfg_.lv_pd && !mtf::is_na(pdh_) && !pdh_swept_ &&
                bar.high > pdh_ + pen && bar.close < pdh_) {
                pdh_swept_ = true; sweep_s_ = true; sweep_lvl_s_ = pdh_;
            }
            if (cfg_.lv_pw && !mtf::is_na(pwl_) && !pwl_swept_ &&
                bar.low < pwl_ - pen && bar.close > pwl_) {
                pwl_swept_ = true; sweep_l_ = true; sweep_lvl_l_ = pwl_;
            }
            if (cfg_.lv_pw && !mtf::is_na(pwh_) && !pwh_swept_ &&
                bar.high > pwh_ + pen && bar.close < pwh_) {
                pwh_swept_ = true; sweep_s_ = true; sweep_lvl_s_ = pwh_;
            }
            if (cfg_.lv_sess && !mtf::is_na(psess_l_) && !psl_swept_ &&
                bar.low < psess_l_ - pen && bar.close > psess_l_) {
                psl_swept_ = true; sweep_l_ = true; sweep_lvl_l_ = psess_l_;
            }
            if (cfg_.lv_sess && !mtf::is_na(psess_h_) && !psh_swept_ &&
                bar.high > psess_h_ + pen && bar.close < psess_h_) {
                psh_swept_ = true; sweep_s_ = true; sweep_lvl_s_ = psess_h_;
            }
        }
        // Level confluence (:884-899)
        double lv_near = 1e10;
        auto near_lvl = [&](double lv) {
            if (!mtf::is_na(lv)) lv_near = std::min(lv_near, std::fabs(bar.close - lv));
        };
        if (cfg_.use_levels) {
            if (cfg_.lv_pd)  { near_lvl(pdh_); near_lvl(pdl_); }
            if (cfg_.lv_pdc) near_lvl(pdc_);
            if (cfg_.lv_pw)  { near_lvl(pwh_); near_lvl(pwl_); }
            if (cfg_.lv_sess){ near_lvl(psess_h_); near_lvl(psess_l_); }
        }
        const bool lv_conf = !cfg_.use_lv_filter || lv_near <= cfg_.lv_near_atr * atr_;

        // ── SMT divergence (:1005-1040) — fires only with a wired ref feed ──
        smt_trig_l_ = smt_trig_s_ = false;
        if (cfg_.use_smt_entry && ref_wired_) {
            const int L = cfg_.smt_pivot_len;
            const int n = (int)hist_.size();
            const int c = n - 1 - L;
            if (c - L >= 0) {
                bool ph = true, pl = true;
                for (int i = c - L; i <= c + L; ++i) {
                    if (i == c) continue;
                    if (hist_[i].high >= hist_[c].high) ph = false;
                    if (hist_[i].low  <= hist_[c].low)  pl = false;
                }
                if (pl) {
                    // Pine shifts chLo2:=chLo1 before evaluating, so the
                    // comparison is against the IMMEDIATELY PREVIOUS chart
                    // pivot (our pre-shift ch_lo1_), not two generations back.
                    if (!mtf::is_na(ch_lo1_) && !mtf::is_na(rf_lo2_) &&
                        hist_[c].low < ch_lo1_ && rf_lo1_ > rf_lo2_)
                        smt_trig_l_ = true;
                    ch_lo2_ = ch_lo1_; ch_lo1_ = hist_[c].low;
                }
                if (ph) {
                    if (!mtf::is_na(ch_hi1_) && !mtf::is_na(rf_hi2_) &&
                        hist_[c].high > ch_hi1_ && rf_hi1_ < rf_hi2_)
                        smt_trig_s_ = true;
                    ch_hi2_ = ch_hi1_; ch_hi1_ = hist_[c].high;
                }
            }
        }
        // Correlation gate (:995-1003) — blocks when filter on and no feed
        bool im_ok_l = true, im_ok_s = true;
        if (cfg_.use_im_filter) {
            bool ok_l = false, ok_s = false;
            if (ref_wired_ && (int)ref_close_hist_.size() >= cfg_.im_corr_len &&
                (int)hist_.size() >= cfg_.im_corr_len) {
                const double corr = correlation(cfg_.im_corr_len);
                if (!mtf::is_na(corr) && std::fabs(corr) >= cfg_.im_min_corr) {
                    double rf_f = mtf::NaN, rf_s = mtf::NaN;
                    for (double v : ref_close_hist_) {
                        rf_f = mtf::ema_next(rf_f, v, cfg_.ema_fast_len);
                        rf_s = mtf::ema_next(rf_s, v, cfg_.ema_slow_len);
                    }
                    const bool im_up = rf_f > rf_s, im_dn = rf_f < rf_s;
                    const bool exp_neg = cfg_.im_expected_corr == "inverse" ||
                        (cfg_.im_expected_corr == "measured" && corr < 0.0);
                    ok_l = exp_neg ? im_dn : im_up;
                    ok_s = exp_neg ? im_up : im_dn;
                }
            }
            im_ok_l = ok_l; im_ok_s = ok_s;
        }

        // ── Break-retest state machine (:1081-1108) ─────────────────────────
        if (bull_break_ && mtf::is_na(bo_lvl_l_) && pos_dir_ <= 0) {
            bo_lvl_l_ = flag_hi_; bo_bar_l_ = bi;
        }
        if (bear_break_ && mtf::is_na(bo_lvl_s_) && pos_dir_ >= 0) {
            bo_lvl_s_ = flag_lo_; bo_bar_s_ = bi;
        }
        const bool watch_l = !mtf::is_na(bo_lvl_l_) && bi > bo_bar_l_;
        const bool watch_s = !mtf::is_na(bo_lvl_s_) && bi > bo_bar_s_;
        retest_l_ = watch_l && bar.low <= bo_lvl_l_ + cfg_.retest_tol_atr * atr_ &&
            bar.close >= bo_lvl_l_ && (!cfg_.retest_conf_close || bar.close > bar.open);
        retest_s_ = watch_s && bar.high >= bo_lvl_s_ - cfg_.retest_tol_atr * atr_ &&
            bar.close <= bo_lvl_s_ && (!cfg_.retest_conf_close || bar.close < bar.open);
        const bool rt_trd_l = cfg_.retest_strict_trend ? trend_up_ : trend_up_loose_;
        const bool rt_trd_s = cfg_.retest_strict_trend ? trend_dn_ : trend_dn_loose_;
        if (watch_l && (bar.close < bo_lvl_l_ - cfg_.retest_inval_atr * atr_ ||
                        bi - bo_bar_l_ > cfg_.retest_max_bars || !rt_trd_l)) {
            bo_lvl_l_ = mtf::NaN; bo_bar_l_ = -1;
        }
        if (watch_s && (bar.close > bo_lvl_s_ + cfg_.retest_inval_atr * atr_ ||
                        bi - bo_bar_s_ > cfg_.retest_max_bars || !rt_trd_s)) {
            bo_lvl_s_ = mtf::NaN; bo_bar_s_ = -1;
        }

        // ── Signal gates (:1113-1162) ───────────────────────────────────────
        const std::string& tm = cfg_.trigger_mode;
        const bool is_auto = tm == "auto",    is_flag_any = tm == "flag_any",
                   is_all  = tm == "all",     is_flag_rt  = tm == "flag_retest",
                   is_fstop= tm == "flag_stop", is_fclose = tm == "flag_close",
                   is_onset= tm == "onset",   is_fvg      = tm == "fvg",
                   is_sweep= tm == "sweep",   is_smt      = tm == "smt",
                   is_stoch= tm == "stoch";
        // §0: with auto_mode_flag_fix, Auto mode gets the intended triggers;
        // without it, the Pine ternary chain leaves them false (bug repro).
        if (is_auto && !cfg_.auto_mode_flag_fix) {
            flag_trig_l_ = flag_trig_s_ = false;
            stoch_trig_l_ = stoch_trig_s_ = false;
        } else {
            flag_trig_l_ = (is_auto || is_flag_any || is_all) ? (bull_break_ || retest_l_)
                         : is_flag_rt ? retest_l_
                         : is_fstop  ? bull_flag_
                         : is_fclose ? bull_break_ : false;
            flag_trig_s_ = (is_auto || is_flag_any || is_all) ? (bear_break_ || retest_s_)
                         : is_flag_rt ? retest_s_
                         : is_fstop  ? bear_flag_
                         : is_fclose ? bear_break_ : false;
            stoch_trig_l_ = (is_auto || is_all || is_stoch) ? stoch_xup : false;
            stoch_trig_s_ = (is_auto || is_all || is_stoch) ? stoch_xdn : false;
        }
        onset_fired_l_ = onset_trig_l_ && !flag_trig_l_ && !stoch_trig_l_;
        onset_fired_s_ = onset_trig_s_ && !flag_trig_s_ && !stoch_trig_s_;
        const bool fvg_trig_l = cfg_.use_fvg && fvg_retest_l_;
        const bool fvg_trig_s = cfg_.use_fvg && fvg_retest_s_;
        const bool sweep_trig_l = cfg_.use_levels && sweep_l_;
        const bool sweep_trig_s = cfg_.use_levels && sweep_s_;

        const bool trig_long = is_onset ? onset_trig_l_
            : is_fvg ? fvg_trig_l : is_sweep ? sweep_trig_l : is_smt ? smt_trig_l_
            : is_auto ? (flag_trig_l_ || stoch_trig_l_ || onset_fired_l_ ||
                         (cfg_.use_fvg_entry && fvg_trig_l) ||
                         (cfg_.use_sweep_entry && sweep_trig_l) ||
                         (cfg_.use_smt_entry && smt_trig_l_))
            : (flag_trig_l_ || stoch_trig_l_);
        const bool trig_short = is_onset ? onset_trig_s_
            : is_fvg ? fvg_trig_s : is_sweep ? sweep_trig_s : is_smt ? smt_trig_s_
            : is_auto ? (flag_trig_s_ || stoch_trig_s_ || onset_fired_s_ ||
                         (cfg_.use_fvg_entry && fvg_trig_s) ||
                         (cfg_.use_sweep_entry && sweep_trig_s) ||
                         (cfg_.use_smt_entry && smt_trig_s_))
            : (flag_trig_s_ || stoch_trig_s_);

        const bool full_conf = (cfg_.setup_mode == "full");
        const bool relaxed_l = is_onset || (cfg_.onset_neutral_htf_ok && onset_fired_l_);
        const bool relaxed_s = is_onset || (cfg_.onset_neutral_htf_ok && onset_fired_s_);
        const bool htf_allow_l = relaxed_l ? (!cfg_.use_mtf || !htf_dn_) : mtf_up_;
        const bool htf_allow_s = relaxed_s ? (!cfg_.use_mtf || !htf_up_) : mtf_dn_;
        const bool long_setup  = trend_up_ && htf_allow_l &&
            (!full_conf || (mom_up_ && rsi_bull && vwap_bull));
        const bool short_setup = trend_dn_ && htf_allow_s &&
            (!full_conf || (mom_dn_ && rsi_bear && vwap_bear));
        const bool regime_dir_l = !cfg_.regime_dir_only || !regime_active_ ||
            (relaxed_l ? !htf_dn_ : htf_up_);
        const bool regime_dir_s = !cfg_.regime_dir_only || !regime_active_ ||
            (relaxed_s ? !htf_up_ : htf_dn_);
        const bool fvg_conf_l = !cfg_.use_fvg_filter || !fvg_bull_.empty();
        const bool fvg_conf_s = !cfg_.use_fvg_filter || !fvg_bear_.empty();

        // Guardrails (:1045-1076)
        const bool cooldown_ok = !cfg_.use_cooldown ||
            (bi - last_loss_bar_) > cfg_.cooldown_bars;
        const bool trades_ok = !cfg_.use_max_trades || trades_today_ < cfg_.max_daily_trades;
        const double dse = mtf::is_na(day_start_equity_) ? equity_ : day_start_equity_;
        double daily_pnl = day_closed_pnl_;   // closed-only (default)
        if (!cfg_.daily_loss_closed_only) {
            // equity_ already includes day_closed_pnl_ (notify_trade_filled
            // adds every close to both), so realized = equity_ - dse, counted
            // ONCE; Pine's strategy.equity path also includes open PnL.
            double unreal = 0.0;
            if (pos_dir_ != 0 && !mtf::is_na(entry_price_))
                unreal = (last_price_ - entry_price_) * pos_dir_ *
                         cfg_.point_value * qty_calc(equity_);
            daily_pnl = (equity_ - dse) + unreal;
        }
        const bool daily_loss_ok = !cfg_.use_daily_loss ||
            daily_pnl > -(cfg_.daily_loss_pct / 100.0) * dse;
        const bool guards_ok = cooldown_ok && trades_ok && daily_loss_ok;

        long_signal_ = cfg_.allow_longs && in_date_range_ && in_session_ && atr_ok &&
            guards_ok && regime_active_ && regime_dir_l && fvg_conf_l && lv_conf &&
            im_ok_l && long_setup && trig_long;
        short_signal_ = cfg_.allow_shorts && in_date_range_ && in_session_ && atr_ok &&
            guards_ok && regime_active_ && regime_dir_s && fvg_conf_s && lv_conf &&
            im_ok_s && short_setup && trig_short;

        // ── Blocker funnel (:1166-1212) ─────────────────────────────────────
        if (cfg_.allow_longs && trig_long) ++funnel_.trig_l;
        if (cfg_.allow_shorts && trig_short) ++funnel_.trig_s;
        if (long_signal_) ++funnel_.sig_l;
        if (short_signal_) ++funnel_.sig_s;
        if ((cfg_.allow_longs && trig_long) || (cfg_.allow_shorts && trig_short)) {
            if (!long_signal_ && !short_signal_) {
                const bool dl = cfg_.allow_longs && trig_long;
                if (!in_date_range_ || !in_session_) ++funnel_.bk_sess;
                else if (!atr_ok) ++funnel_.bk_atr;
                else if (!regime_active_) ++funnel_.bk_regime;
                else if (!guards_ok) ++funnel_.bk_guard;
                else if (!(dl ? htf_allow_l : htf_allow_s) ||
                         !(dl ? regime_dir_l : regime_dir_s)) ++funnel_.bk_htf;
                else if (!(dl ? trend_up_ : trend_dn_)) ++funnel_.bk_trend;
                else ++funnel_.bk_conf;
            }
        }

        if (!warmed_up()) { prev_in_session_ = in_session_; return; }

        // ── Confirmation TF module (:1221-1265, default OFF) ────────────────
        conf_go_l_ = conf_go_s_ = false;
        if (cfg_.use_conf_tf) {
            if (long_signal_ && pos_dir_ <= 0 && pend_bar_l_ < 0) {
                pend_bar_l_ = bi;
                pend_ref_l_ = fvg_retest_l_ ? fvg_ret_lvl_l_
                            : sweep_l_ ? sweep_lvl_l_ : mtf::NaN;
            }
            if (short_signal_ && pos_dir_ >= 0 && pend_bar_s_ < 0) {
                pend_bar_s_ = bi;
                pend_ref_s_ = fvg_retest_s_ ? fvg_ret_lvl_s_
                            : sweep_s_ ? sweep_lvl_s_ : mtf::NaN;
            }
            if (conf_rolled && conf5_.done.size() >= 1) {
                const MinuteBar& cb = conf5_.done.back();
                const double mid = (cb.high + cb.low) / 2.0;
                const bool strong_l = cb.close > mid && (!cfg_.conf_need_bull || cb.close > cb.open);
                const bool strong_s = cb.close < mid && (!cfg_.conf_need_bull || cb.close < cb.open);
                if (pend_bar_l_ >= 0) {
                    if (strong_l && in_session_ && in_date_range_) conf_go_l_ = true;
                    pend_bar_l_ = -1; pend_ref_l_ = mtf::NaN;
                }
                if (pend_bar_s_ >= 0) {
                    if (strong_s && in_session_ && in_date_range_) conf_go_s_ = true;
                    pend_bar_s_ = -1; pend_ref_s_ = mtf::NaN;
                }
            }
            if (pend_bar_l_ >= 0 &&
                (!trend_up_loose_ || bi - pend_bar_l_ > cfg_.conf_max_wait || pos_dir_ > 0)) {
                pend_bar_l_ = -1; pend_ref_l_ = mtf::NaN;
            }
            if (pend_bar_s_ >= 0 &&
                (!trend_dn_loose_ || bi - pend_bar_s_ > cfg_.conf_max_wait || pos_dir_ < 0)) {
                pend_bar_s_ = -1; pend_ref_s_ = mtf::NaN;
            }
        }

        // ── Entries (:1336-1384) — Auto/market path; stop/limit modes are ───
        // ── host-side pending-order machinery (cfg pb_valid_bars etc.) ──────
        const bool enter_l = (long_signal_ && !cfg_.use_conf_tf) || conf_go_l_;
        const bool enter_s = (short_signal_ && !cfg_.use_conf_tf) || conf_go_s_;
        if (enter_l && pos_dir_ <= 0 && !exit_pending_ && qty_calc(equity_) > 0) {
            open_position(+1, bar.close, entry_reason(true));
            if (retest_l_) { bo_lvl_l_ = mtf::NaN; bo_bar_l_ = -1; }
        } else if (enter_s && pos_dir_ >= 0 && !exit_pending_ && qty_calc(equity_) > 0) {
            open_position(-1, bar.close, entry_reason(false));
            if (retest_s_) { bo_lvl_s_ = mtf::NaN; bo_bar_s_ = -1; }
        }

        // ── Exits (:1422-1484) — bracket math + market exits on bar close ───
        if (pos_dir_ != 0 && !exit_pending_ && bi > entry_bar_)
            update_exits(bar);

        // ── Session end / out of range (:1477-1484) ─────────────────────────
        const bool session_ending = cfg_.use_session && cfg_.flat_at_session_end &&
            prev_in_session_ && !in_session_;
        if ((session_ending || !in_date_range_) && pos_dir_ != 0 && !exit_pending_) {
            exit_pending_ = true;
            LOG("[MTF] Session end flatten (bar ET min %d)", et_mod);
            if (signal_cb_) signal_cb_(OrbSignal::FLATTEN_EOD, bar.close,
                                       "Session End / Out of Range");
        }

        prev_in_session_ = in_session_;
    }

    // ── FVG maintenance (:640-692) ───────────────────────────────────────────
    void fvg_maintain(std::deque<Gap>& gaps, bool bull, const MinuteBar& bar, long bi) {
        for (int i = (int)gaps.size() - 1; i >= 0; --i) {
            Gap& g = gaps[i];
            const double mid = (g.top + g.bot) / 2.0;
            const bool filled = bull
                ? (cfg_.fvg_mid_invalidate ? bar.close < mid : bar.low <= g.bot)
                : (cfg_.fvg_mid_invalidate ? bar.close > mid : bar.high >= g.top);
            const bool expired = bi - g.bar > cfg_.fvg_max_bars;
            if (filled || expired) {
                gaps.erase(gaps.begin() + i);
            } else if (!g.used && bi > g.bar) {
                if (bull && bar.low <= g.top && bar.low > g.bot && bar.close >= mid) {
                    g.used = true; fvg_retest_l_ = true; fvg_ret_lvl_l_ = mid;
                } else if (!bull && bar.high >= g.bot && bar.high < g.top && bar.close <= mid) {
                    g.used = true; fvg_retest_s_ = true; fvg_ret_lvl_s_ = mid;
                }
            }
        }
    }

    // ── Rolling correlation vs reference closes (:995) ───────────────────────
    double correlation(int len) const {
        const int nh = (int)hist_.size(), nr = (int)ref_close_hist_.size();
        if (nh < len || nr < len) return mtf::NaN;
        double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
        for (int i = 0; i < len; ++i) {
            const double x = hist_[nh - len + i].close;
            const double y = ref_close_hist_[nr - len + i];
            sx += x; sy += y; sxy += x * y; sxx += x * x; syy += y * y;
        }
        const double num = len * sxy - sx * sy;
        const double den = std::sqrt((len * sxx - sx * sx) * (len * syy - sy * sy));
        return den != 0.0 ? num / den : mtf::NaN;
    }

    // ── Entry trigger reason strings ─────────────────────────────────────────
    std::string entry_reason(bool long_side) const {
        const char* side = long_side ? "long" : "short";
        if (long_side ? conf_go_l_ : conf_go_s_)
            return std::string("conf_") + side;
        if (long_side ? flag_trig_l_ : flag_trig_s_) {
            const bool rt = long_side ? retest_l_ : retest_s_;
            return std::string(rt ? "flag_retest_" : "flag_break_") + side;
        }
        if (long_side ? stoch_trig_l_ : stoch_trig_s_)
            return std::string("stoch_") + side;
        if (long_side ? onset_fired_l_ : onset_fired_s_) {
            const bool pull = long_side ? onset_pull_l_ : onset_pull_s_;
            return std::string(pull ? "onset_pullback_" : "onset_struct_") + side;
        }
        if (cfg_.use_fvg_entry && (long_side ? fvg_retest_l_ : fvg_retest_s_))
            return std::string("fvg_retest_") + side;
        if (cfg_.use_sweep_entry && (long_side ? sweep_l_ : sweep_s_))
            return std::string("sweep_") + side;
        if (cfg_.use_smt_entry && (long_side ? smt_trig_l_ : smt_trig_s_))
            return std::string("smt_") + side;
        return std::string("signal_") + side;
    }

    // ── Open position at (assumed) bar-close fill (:1336-1384) ──────────────
    void open_position(int dir, double price, const std::string& reason) {
        pos_dir_     = dir;
        entry_price_ = price;
        entry_atr_   = atr_;
        entry_bar_   = bar_index_;
        entry_reason_ = reason;
        be_on_ = trail_on_ = false;
        exit_pending_ = false;
        lv_tp_ = mtf::NaN; lv_tp_name_.clear();
        ++trades_today_;

        const bool fixed = (cfg_.exit_sizing == "fixed");
        r_unit_ = fixed ? cfg_.sl_points : cfg_.sl_atr_mult * entry_atr_;
        const double tp_dist = fixed ? cfg_.tp_points : r_unit_ * cfg_.rr_ratio;

        // ICT opposing-liquidity target (:1299-1316), locked at fill
        if (cfg_.tp_target_mode != "fixed_rr") {
            double lv; std::string nm;
            if (dir > 0) nearest_above(price, lv, nm); else nearest_below(price, lv, nm);
            if (!mtf::is_na(lv) &&
                std::fabs(lv - price) >= cfg_.tp_min_dist_atr * entry_atr_) {
                lv_tp_ = dir > 0 ? lv - cfg_.tp_buffer_atr * entry_atr_
                                 : lv + cfg_.tp_buffer_atr * entry_atr_;
                lv_tp_name_ = nm;
            }
        }
        cur_stop_ = dir > 0 ? price - r_unit_ : price + r_unit_;
        cur_tp_   = !mtf::is_na(lv_tp_)
            ? (cfg_.tp_target_mode == "opposing_cap_rr"
                ? (dir > 0 ? std::min(lv_tp_, price + tp_dist)
                           : std::max(lv_tp_, price - tp_dist))
                : lv_tp_)
            : (dir > 0 ? price + tp_dist : price - tp_dist);

        // min-qty clamp warning (Pine tooltip, spec §3)
        const double sl_dist = fixed ? cfg_.sl_points : cfg_.sl_atr_mult * entry_atr_;
        const double raw = equity_ * (cfg_.risk_pct / 100.0) / (sl_dist * cfg_.point_value);
        if (raw < cfg_.min_qty)
            LOG("[MTF] WARNING: qtyRaw=%.2f clamped to min_qty=%d — actual risk "
                "$%.2f exceeds risk_pct target", raw, cfg_.min_qty,
                cfg_.min_qty * sl_dist * cfg_.point_value);

        LOG("[MTF] %s signal @ %.2f (%s) stop=%.2f tp=%.2f atr=%.2f",
            dir > 0 ? "LONG" : "SHORT", price, reason.c_str(), cur_stop_, cur_tp_, entry_atr_);
        if (signal_cb_) signal_cb_(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, price, reason);
    }

    // Alias kept for notify_entry_filled — recomputes the base bracket.
    void init_bracket(const std::string& reason) {
        const int dir = pos_dir_;
        const double px = entry_price_;
        pos_dir_ = dir;
        const bool fixed = (cfg_.exit_sizing == "fixed");
        r_unit_ = fixed ? cfg_.sl_points : cfg_.sl_atr_mult * entry_atr_;
        const double tp_dist = fixed ? cfg_.tp_points : r_unit_ * cfg_.rr_ratio;
        double new_stop = dir > 0 ? px - r_unit_ : px + r_unit_;
        // Never-retreat: if update_exits already ratcheted/BE-latched the stop
        // before this fill notification landed, keep the tighter stop.
        if (!mtf::is_na(cur_stop_) && cur_stop_ > 0.0)
            new_stop = dir > 0 ? std::max(new_stop, cur_stop_)
                               : std::min(new_stop, cur_stop_);
        cur_stop_ = new_stop;
        cur_tp_   = !mtf::is_na(lv_tp_) ? lv_tp_
                  : (dir > 0 ? px + tp_dist : px - tp_dist);
        (void)reason;
    }

    void clear_position() {
        pos_dir_ = 0;
        entry_price_ = entry_atr_ = mtf::NaN;
        entry_bar_ = -1;
        be_on_ = trail_on_ = exit_pending_ = false;
        cur_stop_ = cur_tp_ = lv_tp_ = mtf::NaN;
        lv_tp_name_.clear();
        entry_reason_.clear();
    }

    // ── Per-bar exit evaluation (:1422-1467) ─────────────────────────────────
    void update_exits(const MinuteBar& bar) {
        const bool fixed = (cfg_.exit_sizing == "fixed");
        const double avg = entry_price_;
        const double ea  = mtf::is_na(entry_atr_) ? atr_ : entry_atr_;
        r_unit_ = fixed ? cfg_.sl_points : cfg_.sl_atr_mult * ea;
        const double tp_dist = fixed ? cfg_.tp_points : r_unit_ * cfg_.rr_ratio;
        const double trl_dst = fixed ? cfg_.sl_points : cfg_.trail_atr_mult * atr_;

        if (pos_dir_ > 0) {
            double ns = avg - r_unit_;
            if (cfg_.use_breakeven && (be_on_ || bar.close > avg + tp_dist * cfg_.be_trigger_pct_tp))
                be_on_ = true;
            if (be_on_) ns = std::max(ns, avg);                    // BE latch: exactly avg
            if (cfg_.use_trailing && (trail_on_ || bar.close > avg + r_unit_ * cfg_.trail_start_r))
                trail_on_ = true;
            if (trail_on_) ns = std::max(ns, bar.close - trl_dst);
            cur_stop_ = std::max(ns, mtf::is_na(cur_stop_) ? ns : cur_stop_);
            const double tgt = !mtf::is_na(lv_tp_)
                ? (cfg_.tp_target_mode == "opposing_cap_rr" ? std::min(lv_tp_, avg + tp_dist) : lv_tp_)
                : avg + tp_dist;
            cur_tp_ = trail_on_ ? mtf::NaN : tgt;                  // trailing retires TP
            if (cfg_.use_osc_exit &&
                !mtf::is_na(stoch_k_) && !mtf::is_na(stoch_d_) &&
                !mtf::is_na(stoch_k_prev_) && !mtf::is_na(stoch_d_prev_) &&
                stoch_k_ < stoch_d_ && stoch_k_prev_ >= stoch_d_prev_ && stoch_k_ > cfg_.stoch_ob)
                return emit_flatten(bar.close, "Osc reversal");
            if (cfg_.use_time_stop && bar_index_ - entry_bar_ >= cfg_.max_hold_bars)
                return emit_flatten(bar.close, "Time stop");
            if (cfg_.use_trend_exit &&
                (!trend_up_ || (cfg_.use_mtf && cfg_.use_htf_exit && htf_dn_)))
                return emit_flatten(bar.close, "Trend reversal");
        } else {
            double ns = avg + r_unit_;
            if (cfg_.use_breakeven && (be_on_ || bar.close < avg - tp_dist * cfg_.be_trigger_pct_tp))
                be_on_ = true;
            if (be_on_) ns = std::min(ns, avg);
            if (cfg_.use_trailing && (trail_on_ || bar.close < avg - r_unit_ * cfg_.trail_start_r))
                trail_on_ = true;
            if (trail_on_) ns = std::min(ns, bar.close + trl_dst);
            cur_stop_ = std::min(ns, mtf::is_na(cur_stop_) ? ns : cur_stop_);
            const double tgt = !mtf::is_na(lv_tp_)
                ? (cfg_.tp_target_mode == "opposing_cap_rr" ? std::max(lv_tp_, avg - tp_dist) : lv_tp_)
                : avg - tp_dist;
            cur_tp_ = trail_on_ ? mtf::NaN : tgt;
            if (cfg_.use_osc_exit &&
                !mtf::is_na(stoch_k_) && !mtf::is_na(stoch_d_) &&
                !mtf::is_na(stoch_k_prev_) && !mtf::is_na(stoch_d_prev_) &&
                stoch_k_ > stoch_d_ && stoch_k_prev_ <= stoch_d_prev_ && stoch_k_ < cfg_.stoch_os)
                return emit_flatten(bar.close, "Osc reversal");
            if (cfg_.use_time_stop && bar_index_ - entry_bar_ >= cfg_.max_hold_bars)
                return emit_flatten(bar.close, "Time stop");
            if (cfg_.use_trend_exit &&
                (!trend_dn_ || (cfg_.use_mtf && cfg_.use_htf_exit && htf_up_)))
                return emit_flatten(bar.close, "Trend reversal");
        }
    }

    void emit_flatten(double price, const char* reason) {
        exit_pending_ = true;
        LOG("[MTF] Market exit: %s @ %.2f", reason, price);
        if (signal_cb_) signal_cb_(OrbSignal::FLATTEN_EOD, price, reason);
    }

    // ── ICT nearest opposing level (:902-984) ────────────────────────────────
    void nearest_above(double px, double& out, std::string& nm) const {
        out = mtf::NaN;
        auto consider = [&](double lv, const char* name) {
            if (!mtf::is_na(lv) && lv > px && (mtf::is_na(out) || lv < out)) {
                out = lv; nm = name;
            }
        };
        if (cfg_.use_levels && cfg_.lv_pd)  { consider(pdh_, "PDH"); consider(pdl_, "PDL"); }
        if (cfg_.use_levels && cfg_.lv_pw)  { consider(pwh_, "PWH"); consider(pwl_, "PWL"); }
        if (cfg_.use_levels && cfg_.lv_sess){ consider(psess_h_, "sessH"); consider(psess_l_, "sessL"); }
        if (cfg_.use_levels && cfg_.lv_pdc) consider(pdc_, "PDC");
        if (cfg_.tp_fvg) {
            for (const Gap& g : fvg_bull_) {
                const double t = cfg_.tp_fvg_ce ? (g.top + g.bot) / 2.0 : g.top;
                consider(t, "FVG");
            }
            for (const Gap& g : fvg_bear_) {
                const double t = cfg_.tp_fvg_ce ? (g.top + g.bot) / 2.0 : g.top;
                consider(t, "FVG");
            }
        }
    }
    void nearest_below(double px, double& out, std::string& nm) const {
        out = mtf::NaN;
        auto consider = [&](double lv, const char* name) {
            if (!mtf::is_na(lv) && lv < px && (mtf::is_na(out) || lv > out)) {
                out = lv; nm = name;
            }
        };
        if (cfg_.use_levels && cfg_.lv_pd)  { consider(pdh_, "PDH"); consider(pdl_, "PDL"); }
        if (cfg_.use_levels && cfg_.lv_pw)  { consider(pwh_, "PWH"); consider(pwl_, "PWL"); }
        if (cfg_.use_levels && cfg_.lv_sess){ consider(psess_h_, "sessH"); consider(psess_l_, "sessL"); }
        if (cfg_.use_levels && cfg_.lv_pdc) consider(pdc_, "PDC");
        if (cfg_.tp_fvg) {
            for (const Gap& g : fvg_bull_) {
                const double t = cfg_.tp_fvg_ce ? (g.top + g.bot) / 2.0 : g.bot;
                consider(t, "FVG");
            }
            for (const Gap& g : fvg_bear_) {
                const double t = cfg_.tp_fvg_ce ? (g.top + g.bot) / 2.0 : g.bot;
                consider(t, "FVG");
            }
        }
    }
};
