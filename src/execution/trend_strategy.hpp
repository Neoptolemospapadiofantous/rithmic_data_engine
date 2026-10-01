#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    trend_strategy.hpp — configurable trend-following signal generator.

    One engine ("trend"), two dozen entry modes, one shared exit manager (the
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
      ema_ribbon      three stacked EMAs (fast>mid>slow, or the mirror) confirm a
                      strong trend; enter on a pullback to the mid EMA that closes
                      back through the pullback bar's extreme; flips out if the
                      stack unstacks
      thrust_fade     a run of consecutive same-direction bars covering ≥ k·ATR
                      (a parabolic thrust) followed by a bar that fails to extend
                      and retraces back through it → fade the exhaustion, target
                      k·ATR back toward the entry

    Added 2026-10-01 (fleet expansion, founder: "new strategy types"):
      level_fade      a bar that trades through the prior-day (or overnight) high/low
                      and closes back inside fades the failed break
      vwap_reclaim    after N tf closes on one side of the session VWAP, a close
                      back across it enters in the reclaim direction
      range_break     the high/low of any clock window (e.g. the Asia 20:00-02:00
                      range), broken inside a later entry window (London, US open)

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
#include <map>
#include <string>
#include <vector>

struct TrendConfig {
    std::string mode = "donchian";
    int  tf_min = 5;
    int  win_start = 930, win_end = 1555;   // ET HHMM: entries allowed [start, end); flatten at end. start > end = wraps midnight (e.g. 2000-0230)
    // Which stretch of the day VWAP / cumulative delta / the "session open" (drive, gap,
    // trend_day) anchor to: "rth" 09:30-16:00, "globex" 18:00→17:00, "window" = win_start..win_end.
    std::string session = "rth";
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
    // ── families added 2026-09-21 ──
    // trend_day: recognise a trend day after td_check_min, then buy/sell every pullback to VWAP (or the fast EMA)
    int td_check_min = 60; double td_min_atr = 1.5; double td_vwap_frac = 0.8; std::string td_pullback = "vwap";  // vwap | ema
    // failed_breakout (turtle soup): a donchian_n break that closes back inside within fb_bars → fade it
    int fb_bars = 3;
    // keltner_ride: close outside ema20 ± kc_mult×ATR enters; close back through ema20 exits (exit_on_flip)
    // ichimoku: tenkan/kijun/senkou spans on the tf frame; kijun cross with close beyond the cloud
    int ichi_tenkan = 9, ichi_kijun = 26, ichi_senkou = 52;
    // roc_momentum: ROC over roc_bars ≥ roc_min_atr×ATR and a fresh roc_hi_bars high of that ROC
    int roc_bars = 12; double roc_min_atr = 2.0; int roc_hi_bars = 20;
    // delta_trend: price AND session cumulative delta both make a dt_bars high (divergence blocks)
    int dt_bars = 10;
    // ── families added 2026-09-28 (founder: "do all you said") ──
    // orb_retest: after a close beyond the first ort_minutes range, wait for the first pullback that touches
    // the broken edge (within ort_tol_atr×ATR) and closes back on the breakout side; one retest per break,
    // disarmed by a close back inside or after ort_max_bars.
    int ort_minutes = 5; double ort_tol_atr = 0.15; int ort_max_bars = 30;
    // gap_fade: |open − prior close| ≥ gf_min_pts; after gf_wait_min a bar closing back toward the prior close
    // fades the gap; the engine flattens ("gap_filled") once gf_fill_frac of the gap has closed.
    double gf_min_pts = 20.0; int gf_wait_min = 5; double gf_fill_frac = 0.8;
    // news_break: the nb_range_min-minute range from nb_hhmm (08:30 / 14:00 ET); first close beyond it
    // within nb_window_min enters; nb_event_only limits it to calendar days (host calls set_event_day).
    int nb_hhmm = 830; int nb_range_min = 5; int nb_window_min = 45; bool nb_event_only = false;
    // ib_break: first ib_minutes (initial balance) high/low; a close beyond enters; the engine flattens
    // ("ib_target") at ib_ext × the balance beyond the broken edge; ib_min_atr floors the balance size.
    int ib_minutes = 60; double ib_ext = 1.0; double ib_min_atr = 0.0;
    // hold (beta exposure, 2026-09-29): enter at hold_entry_hhmm in hold_dir, hold until the window end
    // (win_end = the exit time; the window may wrap midnight, so overnight holds work). Optional day
    // filters: hold_tom (turn of month: day ≥ 28 or ≤ 3), hold_pre_event (the day BEFORE a calendar
    // fomc/nfp — host calls set_event_next_day), hold_up_day (only when the session is up at entry).
    // The position survives the 18:00 ET session reset so the exit at win_end still fires.
    int hold_entry_hhmm = 1555; int hold_dir = 1; bool hold_tom = false, hold_pre_event = false, hold_up_day = false;
    // atr_break (2026-09-29): close beyond anchor ± ab_k × ATR enters, one per side per session.
    // ab_anchor = open (session open) | prev_close; ab_day_atr = use the prior-day ATR14 the host
    // provides (set_day_atr) instead of the tf-bar ATR.
    double ab_k = 1.0; std::string ab_anchor = "open"; bool ab_day_atr = true;
    // vprofile (2026-09-29): the PRIOR RTH session's volume profile (1-pt bins) → POC + value area
    // (vp_va_frac of volume around the POC). vp_style = break (close through VAH/VAL, one per side)
    // | fade (touch VAH/VAL within vp_tol_atr×ATR and close back inside → fade toward the POC;
    // the engine flattens at the POC, "vp_target").
    std::string vp_style = "break"; double vp_va_frac = 0.70; double vp_tol_atr = 0.15;
    // fib_pullback: retrace of the last fib_swing_bars impulse into [fib_lo, fib_hi], resume through the prior bar
    int fib_swing_bars = 20; double fib_lo = 0.382, fib_hi = 0.618;
    // vwap_fade (mean reversion): close ≥ mr_dev_atr×ATR from VWAP then a bar closing back toward it; target VWAP
    double mr_dev_atr = 2.0; double mr_target_atr = 0.2;
    // band_fade (mean reversion): close outside Bollinger(bb_len, bb_mult) then back inside; target the band mid
    // rsi2_pullback (Connors): RSI(rsi_len) ≤ rsi_buy (≥ rsi_sell) with close on the trend side of the slow EMA
    int rsi_len = 2; double rsi_buy = 10.0, rsi_sell = 90.0, rsi_exit = 50.0;
    // book_imbalance (needs the collector's bbo stream): bid share ≥ bi_min held for bi_hold_secs with
    // spread ≤ bi_max_spread_ticks → long (mirror ≤ 1−bi_min → short); flat when it normalises to bi_exit
    double bi_min = 0.70, bi_exit = 0.55, bi_max_spread_ticks = 2.0; int bi_hold_secs = 5;
    // ema_ribbon: three stacked EMAs confirm the trend (stricter than ema_pullback's
    // 2-line cross); entry is a pullback to the MID line, reusing pullback_tol_atr /
    // pullback_lookback. Flips flat (exit_on_flip) the moment the stack unstacks.
    int ribbon_fast = 8, ribbon_mid = 21, ribbon_slow = 50;
    // thrust_fade: thrust_bars consecutive same-direction closes covering
    // ≥ thrust_min_atr×ATR mark a parabolic run; a bar that fails to make a new
    // extreme and closes back thrust_retrace_frac of the way through the last
    // thrust bar's range fades it; target thrust_target_atr×ATR from the fade entry.
    int thrust_bars = 4; double thrust_min_atr = 2.5, thrust_retrace_frac = 0.5, thrust_target_atr = 1.0;
    // ── tape (order-flow) modes: every tick carries the aggressor side, so each tf bar has
    // volume v and aggressor-buy volume bv (bar delta = 2·bv − v, buy share = bv / v).
    // absorption_reversal: a bar with volume ≥ abs_vol_mult × the avg of the prior vol_avg_bars
    // bars but range ≤ abs_max_range_atr×ATR, ending a ≥ abs_min_move_atr×ATR move over
    // abs_move_bars bars (heavy volume, no progress = the passive side absorbing) arms a fade;
    // within abs_arm_bars, a close back through the absorption bar's midpoint enters against the
    // move. abs_delta_min > 0 also requires the aggressors to sit on the absorbed side (buy
    // share ≥ x for an up-move).
    double abs_vol_mult = 2.0, abs_max_range_atr = 0.5, abs_min_move_atr = 1.5, abs_delta_min = 0.0;
    int abs_move_bars = 5, abs_arm_bars = 2;
    // delta_divergence: price makes a new dd_bars-bar high while the rolling dd_bars-bar delta
    // sum does not (below its own max over the prior dd_bars windows) and the breakout bar's
    // delta ≤ 0 → sellers into a new high, fade it short (mirror long).
    int dd_bars = 20;
    // volume_burst: bar volume ≥ vb_vol_mult × avg, close in the top vb_close_loc of its range
    // with buy share ≥ vb_delta_min → continuation long (mirror short); vb_trend_agree also
    // requires ema_fast > ema_slow (mirror).
    double vb_vol_mult = 2.5, vb_close_loc = 0.7, vb_delta_min = 0.6; bool vb_trend_agree = true;
    // book_imbalance: bi_invert FADES the stacked side instead of following it — the paper fleet's
    // __inv overlay (imbalance_max) was the only book overlay with an edge (PF 2.19 vs 0.92 base,
    // 2026-09-25); this makes it a first-class trigger with its own hold/threshold variants.
    bool bi_invert = false;
    // ── families added 2026-10-01 (fleet expansion) ──
    // level_fade: a bar whose high (low) trades through the `level` high (low) by at most
    // lf_max_poke_atr×ATR (0 = any depth) and closes back inside fades the failed break; one per side
    // per session. level = overnight is only read once the overnight range is complete (09:30-17:00 ET).
    double lf_max_poke_atr = 1.0;
    // vwap_reclaim: ≥ vr_min_bars consecutive tf closes on one side of the session VWAP (anchor =
    // `session`), then a close back across it enters in the reclaim direction; one per side per session.
    int vr_min_bars = 10;
    // range_break: the high/low of the rb_from..rb_to clock window (ET HHMM, may wrap midnight; built
    // from 1-minute bars and kept until the 18:00 ET rollover, so an Asia range survives the 09:30
    // session start). Once the window has closed, a tf close beyond high/low ± rb_buffer_atr×ATR inside
    // win_start..win_end enters; one per side per day.
    int rb_from = 2000, rb_to = 200; double rb_buffer_atr = 0.0;
    // generic gates / exits usable by ANY mode
    int htf_tf_min = 0; int htf_ema = 21;        // >0: longs only when the htf close is above its EMA (mirror for shorts)
    double chandelier_mult = 0.0;                // >0: flatten when close falls chandelier_mult×ATR from the best price since entry

    static TrendConfig from_json_string(const std::string& t) {
        TrendConfig c;
        c.mode = jstr(t, "mode", c.mode);
        c.tf_min = jint(t, "tf_min", c.tf_min);
        c.win_start = jint(t, "win_start", c.win_start); c.win_end = jint(t, "win_end", c.win_end);
        c.session = jstr(t, "session", c.session);
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
        c.ab_k = jdbl(t, "ab_k", c.ab_k); c.ab_anchor = jstr(t, "ab_anchor", c.ab_anchor); c.ab_day_atr = jbool(t, "ab_day_atr", c.ab_day_atr);
        c.vp_style = jstr(t, "vp_style", c.vp_style); c.vp_va_frac = jdbl(t, "vp_va_frac", c.vp_va_frac); c.vp_tol_atr = jdbl(t, "vp_tol_atr", c.vp_tol_atr);
        c.hold_entry_hhmm = jint(t, "hold_entry_hhmm", c.hold_entry_hhmm); c.hold_dir = jint(t, "hold_dir", c.hold_dir);
        c.hold_tom = jbool(t, "hold_tom", c.hold_tom); c.hold_pre_event = jbool(t, "hold_pre_event", c.hold_pre_event); c.hold_up_day = jbool(t, "hold_up_day", c.hold_up_day);
        c.ort_minutes = jint(t, "ort_minutes", c.ort_minutes); c.ort_tol_atr = jdbl(t, "ort_tol_atr", c.ort_tol_atr); c.ort_max_bars = jint(t, "ort_max_bars", c.ort_max_bars);
        c.gf_min_pts = jdbl(t, "gf_min_pts", c.gf_min_pts); c.gf_wait_min = jint(t, "gf_wait_min", c.gf_wait_min); c.gf_fill_frac = jdbl(t, "gf_fill_frac", c.gf_fill_frac);
        c.nb_hhmm = jint(t, "nb_hhmm", c.nb_hhmm); c.nb_range_min = jint(t, "nb_range_min", c.nb_range_min); c.nb_window_min = jint(t, "nb_window_min", c.nb_window_min);
        c.nb_event_only = jbool(t, "nb_event_only", c.nb_event_only);
        c.ib_minutes = jint(t, "ib_minutes", c.ib_minutes); c.ib_ext = jdbl(t, "ib_ext", c.ib_ext); c.ib_min_atr = jdbl(t, "ib_min_atr", c.ib_min_atr);
        c.level = jstr(t, "level", c.level); c.vol_mult = jdbl(t, "vol_mult", c.vol_mult); c.vol_avg_bars = jint(t, "vol_avg_bars", c.vol_avg_bars);
        c.bb_len = jint(t, "bb_len", c.bb_len); c.bb_mult = jdbl(t, "bb_mult", c.bb_mult); c.kc_mult = jdbl(t, "kc_mult", c.kc_mult);
        c.squeeze_bars = jint(t, "squeeze_bars", c.squeeze_bars);
        c.st_len = jint(t, "st_len", c.st_len); c.st_mult = jdbl(t, "st_mult", c.st_mult);
        c.nr_n = jint(t, "nr_n", c.nr_n);
        c.rs_lookback_min = jint(t, "rs_lookback_min", c.rs_lookback_min); c.rs_min_bp = jdbl(t, "rs_min_bp", c.rs_min_bp);
        c.tod_hhmm = jint(t, "tod_hhmm", c.tod_hhmm); c.tod_lookback_min = jint(t, "tod_lookback_min", c.tod_lookback_min);
        c.tod_min_atr = jdbl(t, "tod_min_atr", c.tod_min_atr); c.tod_window_min = jint(t, "tod_window_min", c.tod_window_min);
        c.td_check_min = jint(t, "td_check_min", c.td_check_min); c.td_min_atr = jdbl(t, "td_min_atr", c.td_min_atr);
        c.td_vwap_frac = jdbl(t, "td_vwap_frac", c.td_vwap_frac); c.td_pullback = jstr(t, "td_pullback", c.td_pullback);
        c.fb_bars = jint(t, "fb_bars", c.fb_bars);
        c.ichi_tenkan = jint(t, "ichi_tenkan", c.ichi_tenkan); c.ichi_kijun = jint(t, "ichi_kijun", c.ichi_kijun); c.ichi_senkou = jint(t, "ichi_senkou", c.ichi_senkou);
        c.roc_bars = jint(t, "roc_bars", c.roc_bars); c.roc_min_atr = jdbl(t, "roc_min_atr", c.roc_min_atr); c.roc_hi_bars = jint(t, "roc_hi_bars", c.roc_hi_bars);
        c.dt_bars = jint(t, "dt_bars", c.dt_bars);
        c.fib_swing_bars = jint(t, "fib_swing_bars", c.fib_swing_bars); c.fib_lo = jdbl(t, "fib_lo", c.fib_lo); c.fib_hi = jdbl(t, "fib_hi", c.fib_hi);
        c.mr_dev_atr = jdbl(t, "mr_dev_atr", c.mr_dev_atr); c.mr_target_atr = jdbl(t, "mr_target_atr", c.mr_target_atr);
        c.rsi_len = jint(t, "rsi_len", c.rsi_len); c.rsi_buy = jdbl(t, "rsi_buy", c.rsi_buy); c.rsi_sell = jdbl(t, "rsi_sell", c.rsi_sell); c.rsi_exit = jdbl(t, "rsi_exit", c.rsi_exit);
        c.htf_tf_min = jint(t, "htf_tf_min", c.htf_tf_min); c.htf_ema = jint(t, "htf_ema", c.htf_ema);
        c.bi_min = jdbl(t, "bi_min", c.bi_min); c.bi_exit = jdbl(t, "bi_exit", c.bi_exit);
        c.bi_max_spread_ticks = jdbl(t, "bi_max_spread_ticks", c.bi_max_spread_ticks); c.bi_hold_secs = jint(t, "bi_hold_secs", c.bi_hold_secs);
        c.chandelier_mult = jdbl(t, "chandelier_mult", c.chandelier_mult);
        c.ribbon_fast = jint(t, "ribbon_fast", c.ribbon_fast); c.ribbon_mid = jint(t, "ribbon_mid", c.ribbon_mid);
        c.ribbon_slow = jint(t, "ribbon_slow", c.ribbon_slow);
        c.thrust_bars = jint(t, "thrust_bars", c.thrust_bars); c.thrust_min_atr = jdbl(t, "thrust_min_atr", c.thrust_min_atr);
        c.thrust_retrace_frac = jdbl(t, "thrust_retrace_frac", c.thrust_retrace_frac);
        c.thrust_target_atr = jdbl(t, "thrust_target_atr", c.thrust_target_atr);
        c.abs_vol_mult = jdbl(t, "abs_vol_mult", c.abs_vol_mult); c.abs_max_range_atr = jdbl(t, "abs_max_range_atr", c.abs_max_range_atr);
        c.abs_min_move_atr = jdbl(t, "abs_min_move_atr", c.abs_min_move_atr); c.abs_delta_min = jdbl(t, "abs_delta_min", c.abs_delta_min);
        c.abs_move_bars = jint(t, "abs_move_bars", c.abs_move_bars); c.abs_arm_bars = jint(t, "abs_arm_bars", c.abs_arm_bars);
        c.dd_bars = jint(t, "dd_bars", c.dd_bars);
        c.vb_vol_mult = jdbl(t, "vb_vol_mult", c.vb_vol_mult); c.vb_close_loc = jdbl(t, "vb_close_loc", c.vb_close_loc);
        c.vb_delta_min = jdbl(t, "vb_delta_min", c.vb_delta_min); c.vb_trend_agree = jbool(t, "vb_trend_agree", c.vb_trend_agree);
        c.bi_invert = jbool(t, "bi_invert", c.bi_invert);
        c.lf_max_poke_atr = jdbl(t, "lf_max_poke_atr", c.lf_max_poke_atr);
        c.vr_min_bars = jint(t, "vr_min_bars", c.vr_min_bars);
        c.rb_from = jint(t, "rb_from", c.rb_from); c.rb_to = jint(t, "rb_to", c.rb_to); c.rb_buffer_atr = jdbl(t, "rb_buffer_atr", c.rb_buffer_atr);
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
    struct Bar { int mod = -1; int64_t ts = 0; double o = 0, h = 0, l = 0, c = 0; double v = 0; double bv = 0; };   // bv = aggressor-buy volume

    TrendStrategy(const TrendConfig& tc, const OrbConfig& risk) : tc_(tc), risk_(risk) {
        if (tc_.tf_min < 1) tc_.tf_min = 1;
    }
    void set_signal_callback(SignalCallback cb) { cb_ = std::move(cb); }

    // ── host contract (mirrors OrbStrategy) ──────────────────────────────────
    // Host: is the current trade date a scheduled-release day (calendar: fomc / nfp)? Read by
    // news_break when nb_event_only is set; paper_main / executor_main set it per trade date.
    void set_event_day(bool v) { event_day_ = v; }
    bool event_day() const { return event_day_; }
    void set_event_next_day(bool v) { event_next_day_ = v; }   // hold_pre_event: tomorrow is fomc/nfp
    void set_day_atr(double pts) { day_atr_ = pts; }            // prior-day ATR14 (session_stats), for atr_break
    // prior-session volume profile (read by tests / dashboards)
    double vp_poc() const { return vp_poc_; } double vp_vah() const { return vp_vah_; } double vp_val() const { return vp_val_; } bool vp_ready() const { return vp_ready_; }

    void reset_session() {
        // hold mode: an overnight position is still open at the 18:00 ET rollover — keep the
        // engine's view of it so check_eod() can flatten it at the window end next morning.
        const bool keep_pos = tc_.mode == "hold" && sess_.in_position;
        const int keep_dir = pos_dir_; const int64_t keep_ts = entry_ts_;
        // 18:00 ET rollover: yesterday's RTH becomes "prior day"; overnight tracking restarts.
        if (rth_seen_) { prev_hi_ = day_hi_; prev_lo_ = day_lo_; prev_close_ = rth_close_; have_prev_ = true; }
        day_hi_ = std::numeric_limits<double>::lowest(); day_lo_ = std::numeric_limits<double>::max();
        on_hi_ = std::numeric_limits<double>::lowest(); on_lo_ = std::numeric_limits<double>::max();
        rth_seen_ = false; in_rth_ = false; vwap_pv_ = vwap_v_ = 0.0; vwap_hist_.clear();
        drive_dir_ = 0; drive_done_ = false; gap_ = 0.0; gap_done_ = false; tod_done_ = false; reenter_dir_ = 0;
        eod_emitted_ = false; entry_ts_ = 0; pos_dir_ = 0;
        td_dir_ = 0; td_done_ = false; vwap_side_ok_ = vwap_side_n_ = 0; fb_dir_ = 0; fb_age_ = 0; cum_delta_ = 0.0; delta_hist_.clear();
        imb_dir_ = 0; imb_since_ = 0;
        best_px_ = 0.0; thrust_entry_ref_ = 0.0; abs_dir_ = 0; abs_age_ = 0; abs_mid_ = 0.0;
        reset_2809_modes();
        rb_hi_ = std::numeric_limits<double>::lowest(); rb_lo_ = std::numeric_limits<double>::max();
        rb_built_ = rb_done_ = false; rb_used_long_ = rb_used_short_ = false;
        sess_.trades_today = 0; sess_.in_position = false; sess_.risk_halted = false; sess_.halt_reason.clear();
        if (keep_pos) { sess_.in_position = true; pos_dir_ = keep_dir; entry_ts_ = keep_ts; }
        LOG("[TREND %s] Session reset (tf=%dm window %04d-%04d anchor=%s)%s", tc_.mode.c_str(), tc_.tf_min,
            tc_.win_start, tc_.win_end, tc_.session.c_str(), have_prev_ ? "" : " — no prior day yet");
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

    // Top of book (bid, bid_size, ask, ask_size). Only book_imbalance decides on it;
    // every mode keeps the latest quote for logging/analysis.
    void on_quote(int64_t ts_us, double bid, int bid_sz, double ask, int ask_sz, double tick = 0.25) {
        q_bid_ = bid; q_ask_ = ask; q_bsz_ = bid_sz; q_asz_ = ask_sz; q_ts_ = ts_us;
        if (tc_.mode != "book_imbalance" || bid <= 0 || ask < bid) return;
        const int n = bid_sz + ask_sz; if (n <= 0) return;
        const double raw = (double)bid_sz / n, spread = (ask - bid) / tick, mid = (bid + ask) / 2.0;
        // bi_invert: a bid-stacked book reads as the SHORT side — fade the displayed size
        // instead of following it. Entry side, hold and the normalised-exit all use `imb`.
        const double imb = tc_.bi_invert ? 1.0 - raw : raw;
        int h, m; to_et(ts_us, h, m); const int hhmm = h * 100 + m;
        const int side = imb >= tc_.bi_min ? 1 : imb <= 1.0 - tc_.bi_min ? -1 : 0;
        if (side != imb_dir_) { imb_dir_ = side; imb_since_ = ts_us; }
        last_px_ = mid; last_ts_ = ts_us;
        if (sess_.in_position) {
            if ((pos_dir_ > 0 && imb <= tc_.bi_exit) || (pos_dir_ < 0 && imb >= 1.0 - tc_.bi_exit))
                emit(OrbSignal::FLATTEN_EOD, mid, "imbalance_normalised");
            return;
        }
        if (side == 0 || ts_us - imb_since_ < (int64_t)tc_.bi_hold_secs * 1'000'000LL) return;
        if (spread > tc_.bi_max_spread_ticks) return;
        if (can_enter(hhmm, side)) emit(side > 0 ? OrbSignal::BUY : OrbSignal::SELL, mid,
                                        tc_.bi_invert ? (side > 0 ? "book_fade_ask" : "book_fade_bid")
                                                      : (side > 0 ? "book_imbalance_bid" : "book_imbalance_ask"));
    }

    void check_eod(int h, int m) {
        int hhmm = h * 100 + m;
        const bool past_end = tc_.win_start <= tc_.win_end ? hhmm >= tc_.win_end
                                                           : (hhmm >= tc_.win_end && hhmm < tc_.win_start);
        if (past_end && sess_.in_position && !eod_emitted_) {
            eod_emitted_ = true;
            emit(OrbSignal::FLATTEN_EOD, last_px_, "session_end");
        }
    }

    // ── ticks → 1m bars → tf bars → decisions ────────────────────────────────
    void on_tick(const OrbTick& t) {
        int h, m; to_et(t.ts_micros, h, m);
        const int mod = h * 60 + m;
        last_px_ = t.price; last_ts_ = t.ts_micros;
        // session phases (in_rth_ = "inside the anchor session", see TrendConfig::session)
        const bool rth = in_session(mod);
        if (rth && !in_rth_) {            // session start: VWAP + day range + drive/gap state
            in_rth_ = true; rth_seen_ = true; cum_delta_ = 0.0; delta_hist_.clear(); td_dir_ = 0; td_done_ = false; vwap_side_ok_ = vwap_side_n_ = 0;
            vwap_pv_ = vwap_v_ = 0.0; vwap_hist_.clear();
            day_hi_ = std::numeric_limits<double>::lowest(); day_lo_ = std::numeric_limits<double>::max();
            rth_open_ = t.price; rth_open_mod_ = mod;
            gap_ = have_prev_ ? (t.price - prev_close_) : 0.0;
            reset_2809_modes();
        }
        if (!rth && in_rth_) { in_rth_ = false; finalize_profile(); }
        if (in_window((mod / 60) * 100 + mod % 60) != was_in_window_) { was_in_window_ = !was_in_window_; if (was_in_window_) eod_emitted_ = false; }
        if (rth) {
            vwap_pv_ += t.price * (double)t.size; vwap_v_ += (double)t.size; rth_close_ = t.price;
            day_hi_ = std::max(day_hi_, t.price); day_lo_ = std::min(day_lo_, t.price);
        } else if (mod >= 18 * 60 || mod < 9 * 60 + 30) {
            on_hi_ = std::max(on_hi_, t.price); on_lo_ = std::min(on_lo_, t.price);
        }
        // 1m bar
        if (m1_.mod != mod) {
            if (m1_.mod >= 0) on_m1_close(m1_, h, m);
            m1_ = Bar{mod, t.ts_micros, t.price, t.price, t.price, t.price, (double)t.size, t.is_buy ? (double)t.size : 0.0};
        } else {
            m1_.h = std::max(m1_.h, t.price); m1_.l = std::min(m1_.l, t.price); m1_.c = t.price; m1_.v += (double)t.size;
            if (t.is_buy) m1_.bv += (double)t.size;
        }
        if (rth) { cum_delta_ += t.is_buy ? (double)t.size : -(double)t.size; }
    }

private:
    // ── helpers ──────────────────────────────────────────────────────────────
    static void to_et(int64_t us, int& h, int& m) {
        time_t tt = (time_t)(us / 1'000'000LL); struct tm g; gmtime_r(&tt, &g);
        int64_t et = (int64_t)tt - us_et_offset(g) * 3600LL;
        h = (int)((et / 3600) % 24); if (h < 0) h += 24; m = (int)((et % 3600) / 60);
    }
    double vwap() const { return vwap_v_ > 0 ? vwap_pv_ / vwap_v_ : 0.0; }
    bool in_window(int hhmm) const {
        return tc_.win_start <= tc_.win_end ? (hhmm >= tc_.win_start && hhmm < tc_.win_end)
                                            : (hhmm >= tc_.win_start || hhmm < tc_.win_end);   // wraps midnight
    }
    // session membership for the VWAP / delta / session-open anchor
    bool in_session(int mod) const {
        if (tc_.session == "globex") return !(mod >= 17 * 60 && mod < 18 * 60);
        if (tc_.session == "window") return in_window((mod / 60) * 100 + mod % 60);
        return mod >= 9 * 60 + 30 && mod < 16 * 60;                                            // rth
    }
    double atr() const { return atr_; }
    void emit(OrbSignal s, double px, const std::string& why) {
        if (cb_) cb_(s, px, why);
        if (s == OrbSignal::BUY || s == OrbSignal::SELL) {
            sess_.in_position = true; ++sess_.trades_today; pos_dir_ = (s == OrbSignal::BUY) ? 1 : -1; entry_ts_ = last_ts_; best_px_ = px;
            LOG("[TREND %s] %s signal @%.2f (%s) trades_today=%d", tc_.mode.c_str(), s == OrbSignal::BUY ? "LONG" : "SHORT", px, why.c_str(), sess_.trades_today);
        }
    }
    bool can_enter(int hhmm, int dir) const {
        if (sess_.in_position || sess_.risk_halted) return false;
        if (!in_window(hhmm)) return false;
        if (sess_.trades_today >= risk_.max_daily_trades) return false;
        if (dir > 0 && !tc_.allow_longs) return false;
        if (dir < 0 && !tc_.allow_shorts) return false;
        if (tc_.htf_tf_min > 0) {                 // higher-timeframe alignment gate
            if (htf_ema_ <= 0 || htf_n_ < tc_.htf_ema) return false;
            if (dir > 0 && htf_close_ <= htf_ema_) return false;
            if (dir < 0 && htf_close_ >= htf_ema_) return false;
        }
        return true;
    }

    // 1-minute bar completed
    void on_m1_close(const Bar& b, int h, int m) {
        const int hhmm = h * 100 + m;
        m1_hist_.push_back(b); if (m1_hist_.size() > 600) m1_hist_.pop_front();
        if (in_rth_ && b.v > 0 && b.h >= b.l) {                     // volume profile of the session in progress
            const int lo = (int)std::floor(b.l), hi = (int)std::floor(b.h);
            const double per = b.v / (double)(hi - lo + 1);
            for (int px = lo; px <= hi; ++px) vp_cur_[px] += per;
        }
        // time stop / flip exits evaluated every minute
        if (sess_.in_position && tc_.time_stop_min > 0 && entry_ts_ > 0 &&
            (b.ts - entry_ts_) >= (int64_t)tc_.time_stop_min * 60'000'000LL)
            emit(OrbSignal::FLATTEN_EOD, b.c, "time_stop");
        if (tc_.mode == "range_break") {                    // build the clock-window range from 1m bars
            const int bh = (b.mod / 60) * 100 + b.mod % 60;
            if (in_hhmm(bh, tc_.rb_from, tc_.rb_to)) { rb_hi_ = std::max(rb_hi_, b.h); rb_lo_ = std::min(rb_lo_, b.l); rb_built_ = true; }
            else if (rb_built_ && !rb_done_) {
                rb_done_ = true;
                LOG("[TREND range_break] range %04d-%04d complete: %.2f-%.2f", tc_.rb_from, tc_.rb_to, rb_lo_, rb_hi_);
            }
        }
        // tf aggregation — a tf bar closes as soon as its LAST minute completes
        // (a 09:30–09:34 five-minute bar closes on the first 09:35 tick).
        const int key = b.mod / tc_.tf_min;
        if (tf_cur_.mod < 0 || tf_cur_.mod / tc_.tf_min != key) {
            if (tf_cur_.mod >= 0) on_tf_close(tf_cur_, hhmm);   // safety: a gap skipped the last minute
            tf_cur_ = b; tf_cur_.mod = key * tc_.tf_min;
        } else {
            tf_cur_.h = std::max(tf_cur_.h, b.h); tf_cur_.l = std::min(tf_cur_.l, b.l); tf_cur_.c = b.c; tf_cur_.v += b.v; tf_cur_.bv += b.bv;
        }
        if ((b.mod + 1) / tc_.tf_min != key) {                  // last minute of the bucket
            on_tf_close(tf_cur_, hhmm);
            tf_cur_.mod = -1;
        }
        if (in_rth_) {
            vwap_hist_.push_back(vwap());
            delta_hist_.push_back(cum_delta_); if (delta_hist_.size() > 600) delta_hist_.pop_front();
            const double vw = vwap();
            if (vw > 0) { ++vwap_side_n_; if ((b.c > vw && td_dir_ >= 0) || (b.c < vw && td_dir_ <= 0)) ++vwap_side_ok_; }
        }
        // higher-timeframe EMA for the alignment gate (closes sampled at htf bucket ends)
        if (tc_.htf_tf_min > 0 && (b.mod + 1) % tc_.htf_tf_min == 0) {
            htf_close_ = b.c; ++htf_n_;
            htf_ema_ = htf_ema_ <= 0 ? b.c : htf_ema_ + (2.0 / (tc_.htf_ema + 1)) * (b.c - htf_ema_);
        }
        if (tc_.mode == "trend_day") recognise_trend_day(b, hhmm);
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
        ema_rf_ = ema_rf_ <= 0 ? b.c : ema_rf_ + (2.0 / (tc_.ribbon_fast + 1)) * (b.c - ema_rf_);
        ema_rm_ = ema_rm_ <= 0 ? b.c : ema_rm_ + (2.0 / (tc_.ribbon_mid + 1)) * (b.c - ema_rm_);
        ema_rs_ = ema_rs_ <= 0 ? b.c : ema_rs_ + (2.0 / (tc_.ribbon_slow + 1)) * (b.c - ema_rs_);
        vol_hist_.push_back(b.v); if (vol_hist_.size() > 200) vol_hist_.pop_front();
        bar_delta_hist_.push_back(2.0 * b.bv - b.v); if (bar_delta_hist_.size() > 400) bar_delta_hist_.pop_front();
        if (atr_ <= 0) return;
        // generic chandelier exit (any mode): best price since entry minus k×ATR
        if (sess_.in_position && tc_.chandelier_mult > 0) {
            best_px_ = pos_dir_ > 0 ? std::max(best_px_, b.h) : std::min(best_px_, b.l);
            const double stop = best_px_ - pos_dir_ * tc_.chandelier_mult * atr_;
            if ((pos_dir_ > 0 && b.c < stop) || (pos_dir_ < 0 && b.c > stop)) { emit(OrbSignal::FLATTEN_EOD, b.c, "chandelier"); return; }
        }
        if (tc_.mode == "donchian")       mode_donchian(b, hhmm);
        else if (tc_.mode == "trend_day")     mode_trend_day(b, hhmm);
        else if (tc_.mode == "failed_breakout") mode_failed_breakout(b, hhmm);
        else if (tc_.mode == "keltner_ride")  mode_keltner(b, hhmm);
        else if (tc_.mode == "ichimoku")      mode_ichimoku(b, hhmm);
        else if (tc_.mode == "roc_momentum")  mode_roc(b, hhmm);
        else if (tc_.mode == "delta_trend")   mode_delta(b, hhmm);
        else if (tc_.mode == "fib_pullback")  mode_fib(b, hhmm);
        else if (tc_.mode == "vwap_fade")     mode_vwap_fade(b, hhmm);
        else if (tc_.mode == "band_fade")     mode_band_fade(b, hhmm);
        else if (tc_.mode == "rsi2_pullback") mode_rsi2(b, hhmm);
        else if (tc_.mode == "ema_pullback") mode_ema_pullback(b, hhmm);
        else if (tc_.mode == "vwap_trend")   mode_vwap(b, hhmm);
        else if (tc_.mode == "opening_drive") mode_drive(b, hhmm);
        else if (tc_.mode == "pdhl_breakout") mode_pdhl(b, hhmm);
        else if (tc_.mode == "squeeze")      mode_squeeze(b, hhmm);
        else if (tc_.mode == "supertrend")   mode_supertrend(b, hhmm);
        else if (tc_.mode == "nr7")          mode_nr7(b, hhmm);
        else if (tc_.mode == "ema_ribbon")   mode_ema_ribbon(b, hhmm);
        else if (tc_.mode == "thrust_fade")  mode_thrust_fade(b, hhmm);
        else if (tc_.mode == "absorption_reversal") mode_absorption(b, hhmm);
        else if (tc_.mode == "delta_divergence")    mode_delta_div(b, hhmm);
        else if (tc_.mode == "volume_burst")        mode_volume_burst(b, hhmm);
        else if (tc_.mode == "orb_retest")          mode_orb_retest(b, hhmm);
        else if (tc_.mode == "gap_fade")            mode_gap_fade(b, hhmm);
        else if (tc_.mode == "news_break")          mode_news_break(b, hhmm);
        else if (tc_.mode == "ib_break")            mode_ib_break(b, hhmm);
        else if (tc_.mode == "hold")                mode_hold(b, hhmm);
        else if (tc_.mode == "atr_break")           mode_atr_break(b, hhmm);
        else if (tc_.mode == "vprofile")            mode_vprofile(b, hhmm);
        else if (tc_.mode == "level_fade")          mode_level_fade(b, hhmm);
        else if (tc_.mode == "vwap_reclaim")        mode_vwap_reclaim(b, hhmm);
        else if (tc_.mode == "range_break")         mode_range_break(b, hhmm);
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
    // ── 2026-09-28 modes ────────────────────────────────────────────────────
    static int mod_of(int hhmm) { return (hhmm / 100) * 60 + hhmm % 100; }
    int mins_since_open(const Bar& b) const { return in_rth_ ? b.mod - rth_open_mod_ : -1; }   // b.mod = bar START minute
    void reset_2809_modes() {
        ort_hi_ = std::numeric_limits<double>::lowest(); ort_lo_ = std::numeric_limits<double>::max(); ort_set_ = false;
        ort_arm_ = 0; ort_age_ = 0; ort_used_long_ = ort_used_short_ = false;
        gf_done_ = false; gf_dir_ = 0;
        nb_hi_ = std::numeric_limits<double>::lowest(); nb_lo_ = std::numeric_limits<double>::max(); nb_set_ = false; nb_done_ = false;
        ib_hi_ = std::numeric_limits<double>::lowest(); ib_lo_ = std::numeric_limits<double>::max(); ib_set_ = false; ib_dir_ = 0; ib_target_ = 0.0;
        hold_done_ = false;
        ab_used_long_ = ab_used_short_ = false; vp_used_long_ = vp_used_short_ = false; vp_dir_ = 0;
        lf_used_long_ = lf_used_short_ = false;
        vr_above_ = vr_below_ = 0; vr_used_long_ = vr_used_short_ = false;
    }

    // Session end: the day's histogram becomes the prior-session profile (POC, value area).
    void finalize_profile() {
        if (vp_cur_.empty()) return;
        double total = 0.0; int poc = vp_cur_.begin()->first; double pv = -1.0;
        for (const auto& [px, v] : vp_cur_) { total += v; if (v > pv) { pv = v; poc = px; } }
        // value area: expand from the POC toward the heavier side until vp_va_frac of the volume is inside
        int lo = poc, hi = poc; double inside = pv;
        while (inside < tc_.vp_va_frac * total) {
            auto up = vp_cur_.find(hi + 1), dn = vp_cur_.find(lo - 1);
            const double uv = up != vp_cur_.end() ? up->second : -1.0, dv = dn != vp_cur_.end() ? dn->second : -1.0;
            if (uv < 0 && dv < 0) break;
            if (uv >= dv) { ++hi; inside += uv; } else { --lo; inside += dv; }
        }
        vp_poc_ = poc + 0.5; vp_vah_ = hi + 1.0; vp_val_ = (double)lo; vp_ready_ = true;
        LOG("[TREND %s] Volume profile: POC=%.1f VAH=%.1f VAL=%.1f (%.0f%% of %.0f)", tc_.mode.c_str(), vp_poc_, vp_vah_, vp_val_, 100.0 * inside / total, total);
        vp_cur_.clear(); vp_used_long_ = vp_used_short_ = false; vp_dir_ = 0;
    }
    // ── 2026-10-01 modes ────────────────────────────────────────────────────
    static bool in_hhmm(int x, int from, int to) {
        return from <= to ? (x >= from && x < to) : (x >= from || x < to);   // wraps midnight
    }
    // level_fade — a failed break of the prior-day / overnight high or low.
    void mode_level_fade(const Bar& b, int hhmm) {
        double hi, lo;
        if (tc_.level == "overnight") {
            if (hhmm < 930 || hhmm >= 1700) return;                    // the overnight range is still forming
            hi = on_hi_; lo = on_lo_; if (hi <= lo) return;
        } else { if (!have_prev_) return; hi = prev_hi_; lo = prev_lo_; }
        const double max_poke = tc_.lf_max_poke_atr > 0 ? tc_.lf_max_poke_atr * atr_ : std::numeric_limits<double>::max();
        if (!lf_used_short_ && b.h > hi && b.h - hi <= max_poke && b.c < hi && can_enter(hhmm, -1)) {
            lf_used_short_ = true; emit(OrbSignal::SELL, b.c, tc_.level + "_high_fade");
        } else if (!lf_used_long_ && b.l < lo && lo - b.l <= max_poke && b.c > lo && can_enter(hhmm, +1)) {
            lf_used_long_ = true; emit(OrbSignal::BUY, b.c, tc_.level + "_low_fade");
        }
    }
    // vwap_reclaim — a long stretch on one side of VWAP, then a close back across it.
    void mode_vwap_reclaim(const Bar& b, int hhmm) {
        if (!in_rth_ || vwap_v_ <= 0) return;
        const double vw = vwap();
        if (b.c > vw) {
            if (!vr_used_long_ && vr_below_ >= tc_.vr_min_bars && can_enter(hhmm, +1)) { vr_used_long_ = true; emit(OrbSignal::BUY, b.c, "vwap_reclaim_long"); }
            ++vr_above_; vr_below_ = 0;
        } else if (b.c < vw) {
            if (!vr_used_short_ && vr_above_ >= tc_.vr_min_bars && can_enter(hhmm, -1)) { vr_used_short_ = true; emit(OrbSignal::SELL, b.c, "vwap_reclaim_short"); }
            ++vr_below_; vr_above_ = 0;
        }
    }
    // range_break — a fresh close beyond a completed clock-window range.
    void mode_range_break(const Bar& b, int hhmm) {
        if (!rb_done_ || rb_hi_ <= rb_lo_) return;
        const double buf = tc_.rb_buffer_atr * atr_;
        const size_t n = tf_.size(); if (n < 2) return; const Bar& p = tf_[n - 2];
        const double up = rb_hi_ + buf, dn = rb_lo_ - buf;
        if (!rb_used_long_ && p.c <= up && b.c > up && can_enter(hhmm, +1)) { rb_used_long_ = true; emit(OrbSignal::BUY, b.c, "range_break_long"); }
        else if (!rb_used_short_ && p.c >= dn && b.c < dn && can_enter(hhmm, -1)) { rb_used_short_ = true; emit(OrbSignal::SELL, b.c, "range_break_short"); }
    }
    // atr_break — expansion beyond the anchor by k × ATR.
    void mode_atr_break(const Bar& b, int hhmm) {
        if (!in_rth_) return;
        const double atr = tc_.ab_day_atr ? day_atr_ : atr_; if (atr <= 0) return;
        const double anchor = tc_.ab_anchor == "prev_close" ? (have_prev_ ? prev_close_ : 0.0) : rth_open_;
        if (anchor <= 0) return;
        const double up = anchor + tc_.ab_k * atr, dn = anchor - tc_.ab_k * atr;
        const size_t n = tf_.size(); if (n < 2) return; const Bar& p = tf_[n - 2];
        if (!ab_used_long_ && p.c <= up && b.c > up && can_enter(hhmm, +1)) { ab_used_long_ = true; emit(OrbSignal::BUY, b.c, "atr_break_long"); }
        else if (!ab_used_short_ && p.c >= dn && b.c < dn && can_enter(hhmm, -1)) { ab_used_short_ = true; emit(OrbSignal::SELL, b.c, "atr_break_short"); }
    }
    // vprofile — prior-session value area: break it, or fade it toward the POC.
    void mode_vprofile(const Bar& b, int hhmm) {
        if (!vp_ready_ || !in_rth_) return;
        if (vp_dir_ != 0 && sess_.in_position) {                    // fade target: the POC
            if ((vp_dir_ > 0 && b.h >= vp_poc_) || (vp_dir_ < 0 && b.l <= vp_poc_)) { vp_dir_ = 0; emit(OrbSignal::FLATTEN_EOD, b.c, "vp_target"); }
            return;
        }
        const size_t n = tf_.size(); if (n < 2) return; const Bar& p = tf_[n - 2];
        if (tc_.vp_style == "fade") {
            const double tol = tc_.vp_tol_atr * atr_;
            if (!vp_used_short_ && b.h >= vp_vah_ - tol && b.c < vp_vah_ && can_enter(hhmm, -1)) { vp_used_short_ = true; vp_dir_ = -1; emit(OrbSignal::SELL, b.c, "vp_fade_vah"); }
            else if (!vp_used_long_ && b.l <= vp_val_ + tol && b.c > vp_val_ && can_enter(hhmm, +1)) { vp_used_long_ = true; vp_dir_ = +1; emit(OrbSignal::BUY, b.c, "vp_fade_val"); }
        } else {
            if (!vp_used_long_ && p.c <= vp_vah_ && b.c > vp_vah_ && can_enter(hhmm, +1)) { vp_used_long_ = true; emit(OrbSignal::BUY, b.c, "vp_break_vah"); }
            else if (!vp_used_short_ && p.c >= vp_val_ && b.c < vp_val_ && can_enter(hhmm, -1)) { vp_used_short_ = true; emit(OrbSignal::SELL, b.c, "vp_break_val"); }
        }
    }
    // hold — beta exposure: one entry at a clock time, exit at the window end (check_eod), day filters.
    static int et_day_of_month(int64_t ts_us) {
        time_t tt = static_cast<time_t>(ts_us / 1'000'000LL); struct tm g; gmtime_r(&tt, &g);
        time_t et = tt - (time_t)us_et_offset(g) * 3600; struct tm e; gmtime_r(&et, &e); return e.tm_mday;
    }
    void mode_hold(const Bar& b, int hhmm) {
        if (sess_.in_position || hold_done_) return;
        // the first completed bar at/after the entry time (within 5 minutes of it)
        const int diff = mod_of(hhmm) - mod_of(tc_.hold_entry_hhmm);
        if (diff < 0 || diff > 5) return;
        hold_done_ = true;                                   // one attempt per session
        if (tc_.hold_tom) { const int d = et_day_of_month(b.ts); if (!(d >= 28 || d <= 3)) return; }
        if (tc_.hold_pre_event && !event_next_day_) return;
        if (tc_.hold_up_day && in_rth_ && !(last_px_ > rth_open_)) return;
        const int dir = tc_.hold_dir < 0 ? -1 : 1;
        if (can_enter(hhmm, dir)) emit(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, b.c, dir > 0 ? "hold_long" : "hold_short");
        else LOG("[TREND hold] entry skipped at %04d: in_position=%d halted=%d trades_today=%d/%d in_window=%d htf_gate=%d",
                 hhmm, (int)sess_.in_position, (int)sess_.risk_halted, sess_.trades_today, risk_.max_daily_trades,
                 (int)in_window(hhmm), (int)(tc_.htf_tf_min > 0));
    }
    // orb_retest — the fib lesson applied to ORB: enter on the pullback, not the break.
    void mode_orb_retest(const Bar& b, int hhmm) {
        const int m = mins_since_open(b); if (m < 0) return;
        if (m < tc_.ort_minutes) { ort_hi_ = std::max(ort_hi_, b.h); ort_lo_ = std::min(ort_lo_, b.l); return; }
        if (!ort_set_) { ort_set_ = ort_hi_ > ort_lo_; if (!ort_set_) return; }
        if (ort_arm_ == 0) {                                   // wait for the break
            if (b.c > ort_hi_ && !ort_used_long_)  { ort_arm_ = +1; ort_age_ = 0; }
            else if (b.c < ort_lo_ && !ort_used_short_) { ort_arm_ = -1; ort_age_ = 0; }
            return;
        }
        ++ort_age_;
        const double tol = tc_.ort_tol_atr * atr_;
        if (ort_arm_ > 0) {
            if (b.c < ort_hi_ || ort_age_ > tc_.ort_max_bars) { ort_arm_ = 0; return; }       // failed break / too old
            const bool touched = b.l <= ort_hi_ + tol;                                          // pulled back to the edge
            if (touched && b.c > ort_hi_ && can_enter(hhmm, +1)) { ort_used_long_ = true; ort_arm_ = 0; emit(OrbSignal::BUY, b.c, "orb_retest_long"); }
        } else {
            if (b.c > ort_lo_ || ort_age_ > tc_.ort_max_bars) { ort_arm_ = 0; return; }
            const bool touched = b.h >= ort_lo_ - tol;
            if (touched && b.c < ort_lo_ && can_enter(hhmm, -1)) { ort_used_short_ = true; ort_arm_ = 0; emit(OrbSignal::SELL, b.c, "orb_retest_short"); }
        }
    }
    // gap_fade — the mirror of gap_go.
    void mode_gap_fade(const Bar& b, int hhmm) {
        if (!have_prev_) return;
        const int m = mins_since_open(b); if (m < 0) return;
        if (gf_dir_ != 0 && sess_.in_position) {               // strategy-owned target: the gap is (mostly) filled
            const double target = prev_close_ + (1.0 - tc_.gf_fill_frac) * gap_;
            if ((gf_dir_ < 0 && b.l <= target) || (gf_dir_ > 0 && b.h >= target)) { gf_dir_ = 0; emit(OrbSignal::FLATTEN_EOD, b.c, "gap_filled"); }
            return;
        }
        if (gf_done_ || std::fabs(gap_) < tc_.gf_min_pts || m < tc_.gf_wait_min) return;
        if (gap_ > 0 && b.c < b.o && b.c < rth_open_ && can_enter(hhmm, -1)) { gf_done_ = true; gf_dir_ = -1; emit(OrbSignal::SELL, b.c, "gap_fade_short"); }
        else if (gap_ < 0 && b.c > b.o && b.c > rth_open_ && can_enter(hhmm, +1)) { gf_done_ = true; gf_dir_ = +1; emit(OrbSignal::BUY, b.c, "gap_fade_long"); }
    }
    // news_break — the release's own range, then its break.
    void mode_news_break(const Bar& b, int hhmm) {
        if (tc_.nb_event_only && !event_day_) return;
        const int start = mod_of(tc_.nb_hhmm), bm = b.mod;
        if (bm < start) return;
        if (bm < start + tc_.nb_range_min) { nb_hi_ = std::max(nb_hi_, b.h); nb_lo_ = std::min(nb_lo_, b.l); return; }
        if (!nb_set_) { nb_set_ = nb_hi_ > nb_lo_; if (!nb_set_) return; }
        if (nb_done_ || bm >= start + tc_.nb_window_min) return;
        if (b.c > nb_hi_ && can_enter(hhmm, +1)) { nb_done_ = true; emit(OrbSignal::BUY, b.c, "news_break_long"); }
        else if (b.c < nb_lo_ && can_enter(hhmm, -1)) { nb_done_ = true; emit(OrbSignal::SELL, b.c, "news_break_short"); }
    }
    // ib_break — initial balance with an extension target owned by the strategy.
    void mode_ib_break(const Bar& b, int hhmm) {
        const int m = mins_since_open(b); if (m < 0) return;
        if (m < tc_.ib_minutes) { ib_hi_ = std::max(ib_hi_, b.h); ib_lo_ = std::min(ib_lo_, b.l); return; }
        if (!ib_set_) { ib_set_ = ib_hi_ > ib_lo_ && (ib_hi_ - ib_lo_) >= tc_.ib_min_atr * atr_; if (!ib_set_) return; }
        const double rng = ib_hi_ - ib_lo_;
        if (ib_dir_ != 0 && sess_.in_position) {
            if ((ib_dir_ > 0 && b.h >= ib_target_) || (ib_dir_ < 0 && b.l <= ib_target_)) { ib_dir_ = 0; emit(OrbSignal::FLATTEN_EOD, b.c, "ib_target"); }
            return;
        }
        if (b.c > ib_hi_ && can_enter(hhmm, +1)) { ib_dir_ = +1; ib_target_ = ib_hi_ + tc_.ib_ext * rng; emit(OrbSignal::BUY, b.c, "ib_break_long"); }
        else if (b.c < ib_lo_ && can_enter(hhmm, -1)) { ib_dir_ = -1; ib_target_ = ib_lo_ - tc_.ib_ext * rng; emit(OrbSignal::SELL, b.c, "ib_break_short"); }
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

    // ── families added 2026-09-21 ───────────────────────────────────────────
    // trend_day: after td_check_min minutes of RTH decide once per day whether this
    // is a trend day — net move ≥ td_min_atr×ATR from the open AND ≥ td_vwap_frac of
    // minute closes on one side of VWAP. Then every pullback to VWAP / the fast EMA
    // that closes back with the trend is an entry (bounded by max_daily_trades).
    void recognise_trend_day(const Bar& b, int /*hhmm*/) {
        if (!in_rth_ || td_done_ || atr_ <= 0) return;
        if (b.mod < rth_open_mod_ + tc_.td_check_min) return;
        td_done_ = true;
        const double mv = b.c - rth_open_;
        const int dir = std::fabs(mv) >= tc_.td_min_atr * atr_ ? (mv > 0 ? 1 : -1) : 0;
        // side-of-VWAP consistency measured against the candidate direction
        int ok = 0, n = 0;
        for (size_t i = 0; i < vwap_hist_.size() && i < m1_hist_.size(); ++i) {
            const size_t k = m1_hist_.size() - vwap_hist_.size() + i; if (k >= m1_hist_.size()) continue;
            const double vw = vwap_hist_[i]; if (vw <= 0) continue; ++n;
            if ((dir > 0 && m1_hist_[k].c > vw) || (dir < 0 && m1_hist_[k].c < vw)) ++ok;
        }
        const double frac = n ? (double)ok / n : 0.0;
        td_dir_ = (dir != 0 && frac >= tc_.td_vwap_frac) ? dir : 0;
        LOG("[TREND trend_day] after %dm: move %+.2f (atr %.2f) vwap-side %.0f%% → %s", tc_.td_check_min, mv, atr_,
            100 * frac, td_dir_ > 0 ? "UP trend day" : td_dir_ < 0 ? "DOWN trend day" : "not a trend day");
    }
    void mode_trend_day(const Bar& b, int hhmm) {
        if (td_dir_ == 0 || !in_rth_) return;
        const double ref = tc_.td_pullback == "ema" ? ema_f_ : vwap(); if (ref <= 0) return;
        const double tol = tc_.vwap_tol_atr * atr_;
        if (td_dir_ > 0 && b.l <= ref + tol && b.c > ref && b.c > b.o && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "trend_day_pullback_long");
        else if (td_dir_ < 0 && b.h >= ref - tol && b.c < ref && b.c < b.o && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "trend_day_pullback_short");
    }
    // failed_breakout (turtle soup): a close outside the donchian_n channel arms a
    // fade; if within fb_bars the close comes back inside, enter against the break.
    void mode_failed_breakout(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); const int N = tc_.donchian_n;
        if ((int)n < N + 2) return;
        double hh = std::numeric_limits<double>::lowest(), ll = std::numeric_limits<double>::max();
        for (size_t i = n - 1 - N; i < n - 1; ++i) { hh = std::max(hh, tf_[i].h); ll = std::min(ll, tf_[i].l); }
        if (fb_dir_ != 0) {
            ++fb_age_;
            if (fb_age_ > tc_.fb_bars) { fb_dir_ = 0; fb_age_ = 0; }
            else if (fb_dir_ > 0 && b.c < fb_level_) { fb_dir_ = 0; if (can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "failed_break_high"); return; }
            else if (fb_dir_ < 0 && b.c > fb_level_) { fb_dir_ = 0; if (can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "failed_break_low"); return; }
        }
        if (fb_dir_ == 0) {
            if (b.c > hh) { fb_dir_ = 1; fb_level_ = hh; fb_age_ = 0; }
            else if (b.c < ll) { fb_dir_ = -1; fb_level_ = ll; fb_age_ = 0; }
        }
    }
    // keltner_ride: enter on a close outside the Keltner channel, ride while closes
    // hold beyond the mid-line; a close back through ema20 flattens (exit_on_flip).
    void mode_keltner(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if (n < 22) return;
        const double up = ema20_ + tc_.kc_mult * atr_, dn = ema20_ - tc_.kc_mult * atr_;
        if (sess_.in_position && tc_.exit_on_flip && ((pos_dir_ > 0 && b.c < ema20_) || (pos_dir_ < 0 && b.c > ema20_))) {
            emit(OrbSignal::FLATTEN_EOD, b.c, "keltner_mid_cross"); return;
        }
        const Bar& p = tf_[n - 2];
        if (p.c <= up && b.c > up && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "keltner_break_up");
        else if (p.c >= dn && b.c < dn && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "keltner_break_down");
    }
    // ichimoku: tenkan/kijun midpoints; the cloud that applies to this bar was
    // drawn ichi_kijun bars ago. Entry = close crosses kijun with close beyond the cloud.
    double mid_range(size_t end_excl, int len) const {
        double hh = std::numeric_limits<double>::lowest(), ll = std::numeric_limits<double>::max();
        for (size_t i = end_excl - len; i < end_excl; ++i) { hh = std::max(hh, tf_[i].h); ll = std::min(ll, tf_[i].l); }
        return (hh + ll) / 2.0;
    }
    void mode_ichimoku(const Bar& b, int hhmm) {
        const size_t n = tf_.size();
        if ((int)n < tc_.ichi_senkou + tc_.ichi_kijun + 2) return;
        const double kijun = mid_range(n, tc_.ichi_kijun), kijun_prev = mid_range(n - 1, tc_.ichi_kijun);
        const size_t back = n - tc_.ichi_kijun;                                   // cloud for this bar
        const double span_a = (mid_range(back, tc_.ichi_tenkan) + mid_range(back, tc_.ichi_kijun)) / 2.0;
        const double span_b = mid_range(back, tc_.ichi_senkou);
        const double cloud_top = std::max(span_a, span_b), cloud_bot = std::min(span_a, span_b);
        if (sess_.in_position && tc_.exit_on_flip && ((pos_dir_ > 0 && b.c < kijun) || (pos_dir_ < 0 && b.c > kijun))) {
            emit(OrbSignal::FLATTEN_EOD, b.c, "kijun_cross"); return;
        }
        const Bar& p = tf_[n - 2];
        if (p.c <= kijun_prev && b.c > kijun && b.c > cloud_top && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "ichimoku_kijun_up");
        else if (p.c >= kijun_prev && b.c < kijun && b.c < cloud_bot && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "ichimoku_kijun_down");
    }
    // roc_momentum: rate of change over roc_bars in ATR units; enter when it is
    // both large and a fresh roc_hi_bars extreme (momentum accelerating, not fading).
    void mode_roc(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.roc_bars + tc_.roc_hi_bars + 1) return;
        auto roc_at = [&](size_t i) { return (tf_[i].c - tf_[i - tc_.roc_bars].c) / atr_; };
        const double r = roc_at(n - 1);
        double hi = std::numeric_limits<double>::lowest(), lo = std::numeric_limits<double>::max();
        for (size_t i = n - 1 - tc_.roc_hi_bars; i < n - 1; ++i) { const double x = roc_at(i); hi = std::max(hi, x); lo = std::min(lo, x); }
        if (r >= tc_.roc_min_atr && r > hi && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "roc_new_high");
        else if (r <= -tc_.roc_min_atr && r < lo && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "roc_new_low");
    }
    // delta_trend: price makes a dt_bars high AND the session's cumulative delta
    // (aggressor buys − sells) makes a dt_bars high on the same bar. Price high
    // without delta high is a divergence and blocks the entry.
    void mode_delta(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.dt_bars + 2 || !in_rth_) return;
        const size_t need = (size_t)tc_.dt_bars * tc_.tf_min;
        if (delta_hist_.size() < need) return;
        double ph = std::numeric_limits<double>::lowest(), pl = std::numeric_limits<double>::max();
        for (size_t i = n - 1 - tc_.dt_bars; i < n - 1; ++i) { ph = std::max(ph, tf_[i].h); pl = std::min(pl, tf_[i].l); }
        // delta_hist_ holds the cumulative delta at each PRIOR minute close (this
        // bar's minute is appended after the tf close); cum_delta_ is live.
        double dh = std::numeric_limits<double>::lowest(), dl = std::numeric_limits<double>::max();
        for (size_t i = delta_hist_.size() - need; i < delta_hist_.size(); ++i) { dh = std::max(dh, delta_hist_[i]); dl = std::min(dl, delta_hist_[i]); }
        const double d = cum_delta_;
        if (b.c > ph && d > dh && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "price_delta_high");
        else if (b.c < pl && d < dl && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "price_delta_low");
    }
    // fib_pullback: last impulse = extreme-to-extreme over fib_swing_bars in the
    // direction of the EMA trend; a bar that dips into the fib_lo..fib_hi retrace
    // zone and closes above the prior bar's high resumes the impulse.
    void mode_fib(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.fib_swing_bars + 2 || ema_f_ <= 0 || ema_s_ <= 0) return;
        const bool up = ema_f_ > ema_s_, dn = ema_f_ < ema_s_;
        size_t ilo = n - 1 - tc_.fib_swing_bars, ihi = ilo;
        for (size_t i = n - 1 - tc_.fib_swing_bars; i < n - 1; ++i) { if (tf_[i].l < tf_[ilo].l) ilo = i; if (tf_[i].h > tf_[ihi].h) ihi = i; }
        const Bar& p = tf_[n - 2];
        if (up && ihi > ilo) {                                    // impulse low → high
            const double rng = tf_[ihi].h - tf_[ilo].l; if (rng < atr_) return;
            const double z_hi = tf_[ihi].h - tc_.fib_lo * rng, z_lo = tf_[ihi].h - tc_.fib_hi * rng;
            if (p.l <= z_hi && p.l >= z_lo && b.c > p.h && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "fib_retrace_long");
        } else if (dn && ilo > ihi) {                             // impulse high → low
            const double rng = tf_[ihi].h - tf_[ilo].l; if (rng < atr_) return;
            const double z_lo = tf_[ilo].l + tc_.fib_lo * rng, z_hi = tf_[ilo].l + tc_.fib_hi * rng;
            if (p.h >= z_lo && p.h <= z_hi && b.c < p.l && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "fib_retrace_short");
        }
    }
    // vwap_fade (mean reversion): the previous close stretched ≥ mr_dev_atr×ATR
    // from VWAP and this bar closes back toward it → fade. Target: VWAP ± mr_target_atr.
    void mode_vwap_fade(const Bar& b, int hhmm) {
        if (!in_rth_ || vwap_v_ <= 0) return;
        const double vw = vwap(); const size_t n = tf_.size(); if (n < 3) return;
        if (sess_.in_position && std::fabs(b.c - vw) <= tc_.mr_target_atr * atr_) { emit(OrbSignal::FLATTEN_EOD, b.c, "vwap_target"); return; }
        const Bar& p = tf_[n - 2];
        const double dev = tc_.mr_dev_atr * atr_;
        if (p.c >= vw + dev && b.c < p.c && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "vwap_fade_short");
        else if (p.c <= vw - dev && b.c > p.c && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "vwap_fade_long");
    }
    // band_fade (mean reversion): close outside the Bollinger band, then a close
    // back inside → fade toward the band mid; a close through the mid is the target.
    void mode_band_fade(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.bb_len + 2) return;
        double mean = 0; for (size_t i = n - tc_.bb_len; i < n; ++i) mean += tf_[i].c; mean /= tc_.bb_len;
        double var = 0; for (size_t i = n - tc_.bb_len; i < n; ++i) var += (tf_[i].c - mean) * (tf_[i].c - mean); var /= tc_.bb_len;
        const double sd = std::sqrt(var); const double up = mean + tc_.bb_mult * sd, dn = mean - tc_.bb_mult * sd;
        if (sess_.in_position && ((pos_dir_ > 0 && b.c >= mean) || (pos_dir_ < 0 && b.c <= mean))) { emit(OrbSignal::FLATTEN_EOD, b.c, "band_mid_target"); return; }
        const Bar& p = tf_[n - 2];
        if (p.c > up && b.c < up && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "band_fade_short");
        else if (p.c < dn && b.c > dn && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "band_fade_long");
    }
    // rsi2_pullback (Connors): RSI(rsi_len) oversold with the close above the slow
    // EMA → long (mirror); exit when RSI crosses back through rsi_exit.
    double rsi(int len) const {
        const size_t n = tf_.size(); if ((int)n < len + 1) return 50.0;
        double g = 0, l = 0;
        for (size_t i = n - len; i < n; ++i) { const double d = tf_[i].c - tf_[i - 1].c; if (d > 0) g += d; else l -= d; }
        if (g + l <= 0) return 50.0;
        return 100.0 * g / (g + l);
    }
    void mode_rsi2(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); if ((int)n < tc_.ema_slow + 2 || ema_s_ <= 0) return;
        const double r = rsi(tc_.rsi_len);
        if (sess_.in_position && ((pos_dir_ > 0 && r >= tc_.rsi_exit) || (pos_dir_ < 0 && r <= tc_.rsi_exit))) { emit(OrbSignal::FLATTEN_EOD, b.c, "rsi_exit"); return; }
        if (r <= tc_.rsi_buy && b.c > ema_s_ && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "rsi2_oversold_in_uptrend");
        else if (r >= tc_.rsi_sell && b.c < ema_s_ && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "rsi2_overbought_in_downtrend");
    }
    // ema_ribbon: fast/mid/slow EMAs fully stacked (fast>mid>slow, or the mirror)
    // — a stricter trend filter than ema_pullback's single fast/slow cross. Entry
    // is a pullback to the MID line that closes back through the touching bar's
    // extreme, same shape as ema_pullback. Unstacking flattens (exit_on_flip).
    void mode_ema_ribbon(const Bar& b, int hhmm) {
        const size_t n = tf_.size();
        if ((int)n < tc_.ribbon_slow + 6) return;
        const bool up = ema_rf_ > ema_rm_ && ema_rm_ > ema_rs_;
        const bool dn = ema_rf_ < ema_rm_ && ema_rm_ < ema_rs_;
        if (sess_.in_position && tc_.exit_on_flip && ((pos_dir_ > 0 && !up) || (pos_dir_ < 0 && !dn))) {
            emit(OrbSignal::FLATTEN_EOD, b.c, "ribbon_unstack"); return;
        }
        if (!up && !dn) return;
        const double tol = tc_.pullback_tol_atr * atr_;
        bool touched = false; double trig_hi = 0, trig_lo = 0;
        for (int k = 2; k <= tc_.pullback_lookback + 1 && (int)n - k >= 0; ++k) {
            const Bar& x = tf_[n - k];
            if (up && x.l <= ema_rm_ + tol) { touched = true; trig_hi = std::max(trig_hi, x.h); }
            if (dn && x.h >= ema_rm_ - tol) { touched = true; trig_lo = (trig_lo == 0) ? x.l : std::min(trig_lo, x.l); }
        }
        if (!touched) return;
        if (up && b.c > trig_hi && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "ribbon_pullback_long");
        else if (dn && trig_lo > 0 && b.c < trig_lo && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "ribbon_pullback_short");
    }
    // thrust_fade (climax exhaustion): thrust_bars consecutive same-direction
    // closes covering ≥ thrust_min_atr×ATR mark a parabolic run in the LAST
    // completed bar of the window; a bar that fails to extend the run's extreme
    // and closes back thrust_retrace_frac of the way through that bar's range
    // fades it. Pure price action — no band, VWAP or oscillator involved, which
    // is what makes it distinct from vwap_fade / band_fade / rsi2_pullback.
    void mode_thrust_fade(const Bar& b, int hhmm) {
        const size_t n = tf_.size(); const int T = tc_.thrust_bars;
        if ((int)n < T + 2 || atr_ <= 0) return;
        if (sess_.in_position) {
            const double tgt = tc_.thrust_target_atr * atr_;
            if ((pos_dir_ > 0 && b.c >= thrust_entry_ref_ + tgt) || (pos_dir_ < 0 && b.c <= thrust_entry_ref_ - tgt)) {
                emit(OrbSignal::FLATTEN_EOD, b.c, "thrust_target"); return;
            }
        }
        bool up_run = true, dn_run = true;
        for (size_t i = n - 1 - T; i < n - 1; ++i) {
            if (tf_[i].c <= tf_[i].o) up_run = false;
            if (tf_[i].c >= tf_[i].o) dn_run = false;
        }
        const Bar& last = tf_[n - 2];                       // last completed thrust-candidate bar
        const Bar& first = tf_[n - 1 - T];
        const double run = last.c - first.o;
        const bool thrust_up = up_run && run >= tc_.thrust_min_atr * atr_;
        const bool thrust_dn = dn_run && -run >= tc_.thrust_min_atr * atr_;
        if (!thrust_up && !thrust_dn) return;
        const double rng = last.h - last.l; if (rng <= 0) return;
        if (thrust_up && b.h <= last.h && b.c < last.h - tc_.thrust_retrace_frac * rng && can_enter(hhmm, -1)) {
            thrust_entry_ref_ = b.c; emit(OrbSignal::SELL, b.c, "thrust_exhaustion_fade_short");
        } else if (thrust_dn && b.l >= last.l && b.c > last.l + tc_.thrust_retrace_frac * rng && can_enter(hhmm, +1)) {
            thrust_entry_ref_ = b.c; emit(OrbSignal::BUY, b.c, "thrust_exhaustion_fade_long");
        }
    }

    // ── tape (order-flow) modes ──────────────────────────────────────────────
    static double buy_share(const Bar& b) { return b.v > 0 ? b.bv / b.v : 0.5; }
    // average volume of the n completed bars BEFORE the current one (vol_hist_ already holds it)
    double avg_vol_prior(int n) const {
        if (n <= 0 || (int)vol_hist_.size() < n + 1) return 0.0;
        double s = 0; for (size_t i = vol_hist_.size() - 1 - n; i < vol_hist_.size() - 1; ++i) s += vol_hist_[i];
        return s / n;
    }
    // absorption_reversal: heavy volume, no progress at the end of a move → the passive side is
    // absorbing; fade when the next bar(s) close back through the absorption bar's midpoint.
    void mode_absorption(const Bar& b, int hhmm) {
        if (abs_dir_ != 0) {
            const int d = abs_dir_;
            if (++abs_age_ > tc_.abs_arm_bars) { abs_dir_ = 0; }
            else if (d > 0 && b.c < abs_mid_ && can_enter(hhmm, -1)) { abs_dir_ = 0; emit(OrbSignal::SELL, b.c, "absorption_fade_short"); return; }
            else if (d < 0 && b.c > abs_mid_ && can_enter(hhmm, +1)) { abs_dir_ = 0; emit(OrbSignal::BUY, b.c, "absorption_fade_long"); return; }
            if (abs_dir_ != 0) return;
        }
        const size_t n = tf_.size(); const int L = tc_.abs_move_bars;
        if ((int)n < L + 2) return;
        const double avg = avg_vol_prior(tc_.vol_avg_bars); if (avg <= 0) return;
        if (b.v < tc_.abs_vol_mult * avg || (b.h - b.l) > tc_.abs_max_range_atr * atr_) return;
        const double move = b.c - tf_[n - 1 - L].c, share = buy_share(b);
        if (move >= tc_.abs_min_move_atr * atr_ && (tc_.abs_delta_min <= 0 || share >= tc_.abs_delta_min)) {
            abs_dir_ = +1; abs_mid_ = (b.h + b.l) / 2.0; abs_age_ = 0;
        } else if (-move >= tc_.abs_min_move_atr * atr_ && (tc_.abs_delta_min <= 0 || share <= 1.0 - tc_.abs_delta_min)) {
            abs_dir_ = -1; abs_mid_ = (b.h + b.l) / 2.0; abs_age_ = 0;
        }
    }
    // delta_divergence: a new N-bar price high the N-bar delta sum does not confirm, with sellers
    // on the breakout bar itself → fade short (mirror long).
    void mode_delta_div(const Bar& b, int hhmm) {
        const size_t n = tf_.size(), m = bar_delta_hist_.size(); const int N = tc_.dd_bars;
        if (N < 2 || (int)n < 2 * N + 1 || (int)m < 2 * N + 1) return;
        double hh = std::numeric_limits<double>::lowest(), ll = std::numeric_limits<double>::max();
        for (size_t i = n - 1 - N; i < n - 1; ++i) { hh = std::max(hh, tf_[i].h); ll = std::min(ll, tf_[i].l); }
        auto wsum = [&](size_t end_excl) { double s = 0; for (size_t i = end_excl - N; i < end_excl; ++i) s += bar_delta_hist_[i]; return s; };
        const double cur = wsum(m);
        double wmax = std::numeric_limits<double>::lowest(), wmin = std::numeric_limits<double>::max();
        for (size_t e = m - N; e < m; ++e) { const double w = wsum(e); wmax = std::max(wmax, w); wmin = std::min(wmin, w); }
        const double bd = bar_delta_hist_.back();
        if (b.h > hh && cur < wmax && bd <= 0 && can_enter(hhmm, -1)) emit(OrbSignal::SELL, b.c, "delta_divergence_short");
        else if (b.l < ll && cur > wmin && bd >= 0 && can_enter(hhmm, +1)) emit(OrbSignal::BUY, b.c, "delta_divergence_long");
    }
    // volume_burst: a high-volume bar closing near its extreme with the aggressors on that side →
    // continuation in the burst's direction (optionally only with the EMA trend).
    void mode_volume_burst(const Bar& b, int hhmm) {
        const double avg = avg_vol_prior(tc_.vol_avg_bars);
        if (avg <= 0 || b.v < tc_.vb_vol_mult * avg) return;
        const double rng = b.h - b.l; if (rng <= 0) return;
        const double loc = (b.c - b.l) / rng, share = buy_share(b);
        const bool up_ok = !tc_.vb_trend_agree || ema_f_ > ema_s_, dn_ok = !tc_.vb_trend_agree || ema_f_ < ema_s_;
        if (loc >= tc_.vb_close_loc && share >= tc_.vb_delta_min && up_ok && can_enter(hhmm, +1))
            emit(OrbSignal::BUY, b.c, "volume_burst_long");
        else if (loc <= 1.0 - tc_.vb_close_loc && share <= 1.0 - tc_.vb_delta_min && dn_ok && can_enter(hhmm, -1))
            emit(OrbSignal::SELL, b.c, "volume_burst_short");
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
    // 2026-09-28 modes
    double ort_hi_ = 0, ort_lo_ = 0; bool ort_set_ = false; int ort_arm_ = 0, ort_age_ = 0; bool ort_used_long_ = false, ort_used_short_ = false;
    bool gf_done_ = false; int gf_dir_ = 0;
    double nb_hi_ = 0, nb_lo_ = 0; bool nb_set_ = false, nb_done_ = false; bool event_day_ = false;
    double ib_hi_ = 0, ib_lo_ = 0; bool ib_set_ = false; int ib_dir_ = 0; double ib_target_ = 0;
    bool hold_done_ = false; bool event_next_day_ = false;
    double day_atr_ = 0.0; bool ab_used_long_ = false, ab_used_short_ = false;
    bool lf_used_long_ = false, lf_used_short_ = false;                              // level_fade
    int vr_above_ = 0, vr_below_ = 0; bool vr_used_long_ = false, vr_used_short_ = false;   // vwap_reclaim
    double rb_hi_ = std::numeric_limits<double>::lowest(), rb_lo_ = std::numeric_limits<double>::max();   // range_break
    bool rb_built_ = false, rb_done_ = false, rb_used_long_ = false, rb_used_short_ = false;
    std::map<int, double> vp_cur_; double vp_poc_ = 0, vp_vah_ = 0, vp_val_ = 0; bool vp_ready_ = false; bool vp_used_long_ = false, vp_used_short_ = false; int vp_dir_ = 0;
    int squeeze_count_ = 0; int st_dir_ = 0; double st_up_ = 0, st_dn_ = 0; int reenter_dir_ = 0;
    std::deque<double> ref_closes_;
    // families added 2026-09-21
    int td_dir_ = 0; bool td_done_ = false; int vwap_side_ok_ = 0, vwap_side_n_ = 0;
    int fb_dir_ = 0, fb_age_ = 0; double fb_level_ = 0;
    double cum_delta_ = 0; std::deque<double> delta_hist_;
    double htf_ema_ = 0, htf_close_ = 0; int htf_n_ = 0;
    double best_px_ = 0;
    bool was_in_window_ = false;
    double q_bid_ = 0, q_ask_ = 0; int q_bsz_ = 0, q_asz_ = 0; int64_t q_ts_ = 0;   // latest quote
    int imb_dir_ = 0; int64_t imb_since_ = 0;                                          // book_imbalance state
    double ema_rf_ = 0, ema_rm_ = 0, ema_rs_ = 0;                                       // ema_ribbon
    double thrust_entry_ref_ = 0;                                                      // thrust_fade
    int abs_dir_ = 0, abs_age_ = 0; double abs_mid_ = 0.0;                              // absorption_reversal (armed fade)
    std::deque<double> bar_delta_hist_;                                                // per tf bar: 2·bv − v
};
