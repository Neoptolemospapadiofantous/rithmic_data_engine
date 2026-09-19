#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    mtf_scalper_config.hpp — "Momentum Scalper — MTF Flag AutoPilot v5" config

    Port of the Pine v6 strategy inputs (see newstrategy + port spec §5/§8).
    Flat snake_case JSON in the style of config/tradeify_config.json.
    JSON extraction helpers are copied from orb_config.hpp (they are private
    there) — same find_top_key semantics: top-level keys only, string-safe.
    ═══════════════════════════════════════════════════════════════════════════ */
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace fs_mtf = std::filesystem;

struct MtfScalperConfig {
    // ── Port-level switches ──────────────────────────────────────────
    // §0: true = intended Auto-mode semantics (flagTrig = bullBreak or retest,
    // stoch analog); false = reproduce the Pine bug (dead flag/stoch triggers
    // in Auto mode). Default true.
    bool    auto_mode_flag_fix = true;
    bool    allow_longs        = true;
    bool    allow_shorts       = true;
    int64_t date_start_epoch   = 0;   // 0 = unbounded (live default)
    int64_t date_end_epoch     = 0;   // 0 = unbounded

    // ── Entry-TF trend / momentum / oscillators (§1.1) ──────────────
    int    ema_fast_len              = 9;
    int    ema_slow_len              = 21;
    bool   require_price_beyond_fast = true;
    std::string setup_mode           = "relaxed";   // "relaxed" | "full"
    bool   use_macd                  = true;
    int    macd_fast                 = 12;
    int    macd_slow                 = 26;
    int    macd_signal               = 9;
    bool   macd_hist_expanding       = true;
    int    rsi_len                   = 9;
    double rsi_mid                   = 50.0;
    int    stoch_k_len               = 8;
    int    stoch_k_smooth            = 3;
    int    stoch_d_len               = 3;
    double stoch_ob                  = 80.0;
    double stoch_os                  = 20.0;
    bool   use_vwap                  = false;
    std::string vwap_anchor          = "globex";    // "globex" | "rth" | "session_window"

    // ── MTF alignment (§1.2) ─────────────────────────────────────────
    bool   use_mtf        = true;
    std::string htf_mode  = "auto";   // "auto" (chart × mult) | "manual"
    int    htf_mult       = 12;       // 1m chart → 12m HTF
    int    htf_manual_min = 60;
    int    htf_fast_len   = 21;
    int    htf_slow_len   = 50;
    bool   htf_need_price = false;
    bool   htf_slope_req  = false;
    bool   use_htf2       = false;

    // ── Trigger mode (§1.9) ──────────────────────────────────────────
    // "auto" | "flag_any" | "all" | "flag_retest" | "flag_stop" |
    // "flag_close" | "stoch" | "onset" | "fvg" | "sweep" | "smt"
    std::string trigger_mode = "auto";

    // ── Flag pattern (§1.4) ──────────────────────────────────────────
    double pole_min_atr      = 0.8;
    int    flag_len          = 5;
    int    pole_recency_bars = 7;
    double flag_max_rng_atr  = 2.0;
    double max_retr_pct      = 75.0;
    double flag_drift_atr    = 0.3;
    bool   adaptive_pole     = true;
    int    pole_len_min      = 6;
    int    pole_len_max      = 20;
    int    pole_len_fixed    = 12;
    double retest_tol_atr    = 0.5;
    int    retest_max_bars   = 12;
    double retest_inval_atr  = 0.5;
    bool   retest_conf_close = false;
    bool   retest_strict_trend = false;

    // ── Trend onset (§1.5) ───────────────────────────────────────────
    bool   use_onset           = true;
    int    onset_window_bars   = 10;
    bool   onset_need_macd     = true;
    double onset_max_ext_atr   = 0.25;
    int    onset_swing_len     = 4;
    bool   onset_neutral_htf_ok = true;   // relaxed HTF for onset-sourced signals

    // ── Fair Value Gaps (§1.6) ───────────────────────────────────────
    bool   use_fvg            = true;
    double fvg_min_atr        = 0.25;
    int    fvg_max_bars       = 30;
    int    fvg_max_keep       = 8;
    bool   fvg_mid_invalidate = false;
    bool   use_fvg_entry      = true;
    bool   use_fvg_filter     = false;
    bool   fvg_struct_mode    = false;    // zones from completed struct-TF bars
    int    fvg_struct_tf_min  = 5;

    // ── Liquidity levels & sweeps (§1.7) ─────────────────────────────
    bool   use_levels      = true;
    bool   lv_pd           = true;
    bool   lv_pw           = true;
    bool   lv_sess         = true;
    bool   lv_pdc          = true;
    int    lv_keep         = 2;
    double sweep_pen_atr   = 0.0;
    bool   use_sweep_entry = true;
    bool   use_lv_filter   = false;
    double lv_near_atr     = 0.5;

    // ── Intermarket / SMT (§1.8) — no reference feed in engine ──────
    bool   use_smt_entry  = false;        // spec §1.8: default OFF (no DXY feed)
    int    smt_pivot_len  = 5;
    std::string reference_symbol = "";    // empty = no reference feed wired
    int    im_corr_len    = 50;
    std::string im_expected_corr = "inverse";  // "inverse" | "positive" | "measured"
    double im_min_corr    = 0.3;
    bool   use_im_filter  = false;

    // ── Confirmation TF (§1.9, default off) ──────────────────────────
    bool   use_conf_tf    = false;
    int    conf_tf_min    = 5;
    bool   conf_need_bull = true;
    std::string conf_refine = "level_fvg_ce";  // "market" | "level_fvg_ce" | "candle_50"
    int    conf_max_wait  = 6;

    // ── Exits (§2) ───────────────────────────────────────────────────
    int    atr_len          = 14;
    std::string exit_sizing = "atr";      // "atr" | "fixed"
    double sl_atr_mult      = 1.2;
    double rr_ratio         = 1.5;
    double sl_points        = 15.0;       // fixed mode only
    double tp_points        = 35.0;       // fixed mode only
    bool   use_osc_exit     = true;
    bool   use_time_stop    = true;
    int    max_hold_bars    = 20;
    bool   use_breakeven    = true;
    double be_trigger_pct_tp = 0.5;
    bool   use_trailing     = false;
    double trail_atr_mult   = 1.0;
    double trail_start_r    = 1.0;
    bool   use_trend_exit   = true;
    bool   use_htf_exit     = false;
    // ICT profit targets: "fixed_rr" | "opposing_fallback_rr" | "opposing_cap_rr"
    std::string tp_target_mode  = "fixed_rr";
    double tp_buffer_atr    = 0.1;
    double tp_min_dist_atr  = 0.5;
    bool   tp_fvg           = true;
    bool   tp_fvg_ce        = true;

    // ── Volatility floor + regime filter (§1.9) ──────────────────────
    bool   use_atr_floor = true;
    int    min_atr_ticks = 10;            // fixed regime mode only
    double atr_rank_min  = 10.0;
    int    rank_lookback = 200;
    bool   use_regime     = true;
    bool   regime_dir_only = true;
    std::string regime_mode = "relative"; // "relative" | "fixed"
    int    regime_components_required = 2;
    int    regime_bars    = 1;
    bool   reg_adx_on     = true;
    int    adx_len        = 14;
    double adx_min        = 20.0;         // fixed mode
    double adx_rank_min   = 30.0;
    bool   adx_rising_pass = true;
    bool   reg_atr_on     = true;
    int    atr_avg_len    = 50;           // fixed mode
    double atr_pct_of_avg = 80.0;         // fixed mode
    double atr_rank_min_regime = 20.0;
    bool   atr_expansion_pass  = true;
    bool   reg_spread_on  = true;
    double ema_spread_min_atr  = 0.25;

    // ── Session (§4) ─────────────────────────────────────────────────
    bool   use_session       = true;
    std::string session_window = "0900-1200";   // "HHMM-HHMM" ET
    std::string session_tz     = "America/New_York";  // informational; ET assumed
    bool   flat_at_session_end = true;
    int    day_rollover_hour_et = 18;     // Globex trading-day rollover

    // ── Guardrails (§4) ──────────────────────────────────────────────
    bool   use_max_trades    = true;
    int    max_daily_trades  = 20;
    bool   use_daily_loss    = true;
    double daily_loss_pct    = 2.0;
    bool   daily_loss_closed_only = true;
    bool   use_cooldown      = true;
    int    cooldown_after_losses = 2;
    int    cooldown_bars     = 5;

    // ── Pending-order machinery (host-side; §1.10) ──────────────────
    int    pb_valid_bars     = 5;
    bool   convert_to_market = true;
    int    convert_bars      = 2;

    // ── Sizing / instrument (§3) ─────────────────────────────────────
    double risk_pct         = 1.0;
    int    min_qty          = 1;
    int    qty_max          = 0;          // 0 = uncapped (Pine has no cap)
    double point_value      = 2.0;        // MNQ = $2.00/pt
    double tick_size        = 0.25;
    double starting_balance = 25000.0;

    // ── Warmup (§6.7) ────────────────────────────────────────────────
    int    warmup_bars = 1500;

    // ── Derived ──────────────────────────────────────────────────────
    int htf_minutes() const {
        return htf_mode == "manual" ? htf_manual_min : htf_mult;  // 1m chart
    }
    int htf2_minutes() const { return htf_minutes() * htf_mult; }

    // ── JSON loading ─────────────────────────────────────────────────
    static MtfScalperConfig from_json_string(const std::string& text) {
        MtfScalperConfig c;
        c.auto_mode_flag_fix = json_bool(text, "auto_mode_flag_fix", c.auto_mode_flag_fix);
        c.allow_longs   = json_bool(text, "allow_longs",  c.allow_longs);
        c.allow_shorts  = json_bool(text, "allow_shorts", c.allow_shorts);
        c.date_start_epoch = (int64_t)json_dbl(text, "date_start_epoch", (double)c.date_start_epoch);
        c.date_end_epoch   = (int64_t)json_dbl(text, "date_end_epoch",   (double)c.date_end_epoch);

        c.ema_fast_len  = json_int(text, "ema_fast_len",  c.ema_fast_len);
        c.ema_slow_len  = json_int(text, "ema_slow_len",  c.ema_slow_len);
        c.require_price_beyond_fast = json_bool(text, "require_price_beyond_fast", c.require_price_beyond_fast);
        c.setup_mode    = json_str(text, "setup_mode", c.setup_mode);
        c.use_macd      = json_bool(text, "use_macd", c.use_macd);
        c.macd_fast     = json_int(text, "macd_fast",   c.macd_fast);
        c.macd_slow     = json_int(text, "macd_slow",   c.macd_slow);
        c.macd_signal   = json_int(text, "macd_signal", c.macd_signal);
        c.macd_hist_expanding = json_bool(text, "macd_hist_expanding", c.macd_hist_expanding);
        c.rsi_len       = json_int(text, "rsi_len", c.rsi_len);
        c.rsi_mid       = json_dbl(text, "rsi_mid", c.rsi_mid);
        c.stoch_k_len   = json_int(text, "stoch_k_len",    c.stoch_k_len);
        c.stoch_k_smooth= json_int(text, "stoch_k_smooth", c.stoch_k_smooth);
        c.stoch_d_len   = json_int(text, "stoch_d_len",    c.stoch_d_len);
        c.stoch_ob      = json_dbl(text, "stoch_ob", c.stoch_ob);
        c.stoch_os      = json_dbl(text, "stoch_os", c.stoch_os);
        c.use_vwap      = json_bool(text, "use_vwap", c.use_vwap);
        c.vwap_anchor   = json_str(text, "vwap_anchor", c.vwap_anchor);

        c.use_mtf       = json_bool(text, "use_mtf", c.use_mtf);
        c.htf_mode      = json_str(text, "htf_mode", c.htf_mode);
        c.htf_mult      = json_int(text, "htf_mult", c.htf_mult);
        c.htf_manual_min= json_int(text, "htf_manual_min", c.htf_manual_min);
        c.htf_fast_len  = json_int(text, "htf_fast_len", c.htf_fast_len);
        c.htf_slow_len  = json_int(text, "htf_slow_len", c.htf_slow_len);
        c.htf_need_price= json_bool(text, "htf_need_price", c.htf_need_price);
        c.htf_slope_req = json_bool(text, "htf_slope_req", c.htf_slope_req);
        c.use_htf2      = json_bool(text, "use_htf2", c.use_htf2);

        c.trigger_mode  = json_str(text, "trigger_mode", c.trigger_mode);

        c.pole_min_atr     = json_dbl(text, "pole_min_atr", c.pole_min_atr);
        c.flag_len         = json_int(text, "flag_len", c.flag_len);
        c.pole_recency_bars= json_int(text, "pole_recency_bars", c.pole_recency_bars);
        c.flag_max_rng_atr = json_dbl(text, "flag_max_rng_atr", c.flag_max_rng_atr);
        c.max_retr_pct     = json_dbl(text, "max_retr_pct", c.max_retr_pct);
        c.flag_drift_atr   = json_dbl(text, "flag_drift_atr", c.flag_drift_atr);
        c.adaptive_pole    = json_bool(text, "adaptive_pole", c.adaptive_pole);
        c.pole_len_min     = json_int(text, "pole_len_min", c.pole_len_min);
        c.pole_len_max     = json_int(text, "pole_len_max", c.pole_len_max);
        c.pole_len_fixed   = json_int(text, "pole_len_fixed", c.pole_len_fixed);
        c.retest_tol_atr   = json_dbl(text, "retest_tol_atr", c.retest_tol_atr);
        c.retest_max_bars  = json_int(text, "retest_max_bars", c.retest_max_bars);
        c.retest_inval_atr = json_dbl(text, "retest_inval_atr", c.retest_inval_atr);
        c.retest_conf_close= json_bool(text, "retest_conf_close", c.retest_conf_close);
        c.retest_strict_trend = json_bool(text, "retest_strict_trend", c.retest_strict_trend);

        c.use_onset         = json_bool(text, "use_onset", c.use_onset);
        c.onset_window_bars = json_int(text, "onset_window_bars", c.onset_window_bars);
        c.onset_need_macd   = json_bool(text, "onset_need_macd", c.onset_need_macd);
        c.onset_max_ext_atr = json_dbl(text, "onset_max_ext_atr", c.onset_max_ext_atr);
        c.onset_swing_len   = json_int(text, "onset_swing_len", c.onset_swing_len);
        c.onset_neutral_htf_ok = json_bool(text, "onset_neutral_htf_ok", c.onset_neutral_htf_ok);

        c.use_fvg           = json_bool(text, "use_fvg", c.use_fvg);
        c.fvg_min_atr       = json_dbl(text, "fvg_min_atr", c.fvg_min_atr);
        c.fvg_max_bars      = json_int(text, "fvg_max_bars", c.fvg_max_bars);
        c.fvg_max_keep      = json_int(text, "fvg_max_keep", c.fvg_max_keep);
        c.fvg_mid_invalidate= json_bool(text, "fvg_mid_invalidate", c.fvg_mid_invalidate);
        c.use_fvg_entry     = json_bool(text, "use_fvg_entry", c.use_fvg_entry);
        c.use_fvg_filter    = json_bool(text, "use_fvg_filter", c.use_fvg_filter);
        c.fvg_struct_mode   = json_bool(text, "fvg_struct_mode", c.fvg_struct_mode);
        c.fvg_struct_tf_min = json_int(text, "fvg_struct_tf_min", c.fvg_struct_tf_min);

        c.use_levels      = json_bool(text, "use_levels", c.use_levels);
        c.lv_pd           = json_bool(text, "lv_pd", c.lv_pd);
        c.lv_pw           = json_bool(text, "lv_pw", c.lv_pw);
        c.lv_sess         = json_bool(text, "lv_sess", c.lv_sess);
        c.lv_pdc          = json_bool(text, "lv_pdc", c.lv_pdc);
        c.lv_keep         = json_int(text, "lv_keep", c.lv_keep);
        c.sweep_pen_atr   = json_dbl(text, "sweep_pen_atr", c.sweep_pen_atr);
        c.use_sweep_entry = json_bool(text, "use_sweep_entry", c.use_sweep_entry);
        c.use_lv_filter   = json_bool(text, "use_lv_filter", c.use_lv_filter);
        c.lv_near_atr     = json_dbl(text, "lv_near_atr", c.lv_near_atr);

        c.use_smt_entry   = json_bool(text, "use_smt_entry", c.use_smt_entry);
        c.smt_pivot_len   = json_int(text, "smt_pivot_len", c.smt_pivot_len);
        c.reference_symbol= json_str(text, "reference_symbol", c.reference_symbol);
        c.im_corr_len     = json_int(text, "im_corr_len", c.im_corr_len);
        c.im_expected_corr= json_str(text, "im_expected_corr", c.im_expected_corr);
        c.im_min_corr     = json_dbl(text, "im_min_corr", c.im_min_corr);
        c.use_im_filter   = json_bool(text, "use_im_filter", c.use_im_filter);

        c.use_conf_tf     = json_bool(text, "use_conf_tf", c.use_conf_tf);
        c.conf_tf_min     = json_int(text, "conf_tf_min", c.conf_tf_min);
        c.conf_need_bull  = json_bool(text, "conf_need_bull", c.conf_need_bull);
        c.conf_refine     = json_str(text, "conf_refine", c.conf_refine);
        c.conf_max_wait   = json_int(text, "conf_max_wait", c.conf_max_wait);

        c.atr_len         = json_int(text, "atr_len", c.atr_len);
        c.exit_sizing     = json_str(text, "exit_sizing", c.exit_sizing);
        c.sl_atr_mult     = json_dbl(text, "sl_atr_mult", c.sl_atr_mult);
        c.rr_ratio        = json_dbl(text, "rr_ratio", c.rr_ratio);
        c.sl_points       = json_dbl(text, "sl_points", c.sl_points);
        c.tp_points       = json_dbl(text, "tp_points", c.tp_points);
        c.use_osc_exit    = json_bool(text, "use_osc_exit", c.use_osc_exit);
        c.use_time_stop   = json_bool(text, "use_time_stop", c.use_time_stop);
        c.max_hold_bars   = json_int(text, "max_hold_bars", c.max_hold_bars);
        c.use_breakeven   = json_bool(text, "use_breakeven", c.use_breakeven);
        c.be_trigger_pct_tp = json_dbl(text, "be_trigger_pct_tp", c.be_trigger_pct_tp);
        c.use_trailing    = json_bool(text, "use_trailing", c.use_trailing);
        c.trail_atr_mult  = json_dbl(text, "trail_atr_mult", c.trail_atr_mult);
        c.trail_start_r   = json_dbl(text, "trail_start_r", c.trail_start_r);
        c.use_trend_exit  = json_bool(text, "use_trend_exit", c.use_trend_exit);
        c.use_htf_exit    = json_bool(text, "use_htf_exit", c.use_htf_exit);
        c.tp_target_mode  = json_str(text, "tp_target_mode", c.tp_target_mode);
        c.tp_buffer_atr   = json_dbl(text, "tp_buffer_atr", c.tp_buffer_atr);
        c.tp_min_dist_atr = json_dbl(text, "tp_min_dist_atr", c.tp_min_dist_atr);
        c.tp_fvg          = json_bool(text, "tp_fvg", c.tp_fvg);
        c.tp_fvg_ce       = json_bool(text, "tp_fvg_ce", c.tp_fvg_ce);

        c.use_atr_floor   = json_bool(text, "use_atr_floor", c.use_atr_floor);
        c.min_atr_ticks   = json_int(text, "min_atr_ticks", c.min_atr_ticks);
        c.atr_rank_min    = json_dbl(text, "atr_rank_min", c.atr_rank_min);
        c.rank_lookback   = json_int(text, "rank_lookback", c.rank_lookback);
        c.use_regime      = json_bool(text, "use_regime", c.use_regime);
        c.regime_dir_only = json_bool(text, "regime_dir_only", c.regime_dir_only);
        c.regime_mode     = json_str(text, "regime_mode", c.regime_mode);
        c.regime_components_required = json_int(text, "regime_components_required", c.regime_components_required);
        c.regime_bars     = json_int(text, "regime_bars", c.regime_bars);
        c.reg_adx_on      = json_bool(text, "reg_adx_on", c.reg_adx_on);
        c.adx_len         = json_int(text, "adx_len", c.adx_len);
        c.adx_min         = json_dbl(text, "adx_min", c.adx_min);
        c.adx_rank_min    = json_dbl(text, "adx_rank_min", c.adx_rank_min);
        c.adx_rising_pass = json_bool(text, "adx_rising_pass", c.adx_rising_pass);
        c.reg_atr_on      = json_bool(text, "reg_atr_on", c.reg_atr_on);
        c.atr_avg_len     = json_int(text, "atr_avg_len", c.atr_avg_len);
        c.atr_pct_of_avg  = json_dbl(text, "atr_pct_of_avg", c.atr_pct_of_avg);
        c.atr_rank_min_regime = json_dbl(text, "atr_rank_min_regime", c.atr_rank_min_regime);
        c.atr_expansion_pass  = json_bool(text, "atr_expansion_pass", c.atr_expansion_pass);
        c.reg_spread_on   = json_bool(text, "reg_spread_on", c.reg_spread_on);
        c.ema_spread_min_atr  = json_dbl(text, "ema_spread_min_atr", c.ema_spread_min_atr);

        c.use_session       = json_bool(text, "use_session", c.use_session);
        c.session_window    = json_str(text, "session_window", c.session_window);
        c.session_tz        = json_str(text, "session_tz", c.session_tz);
        c.flat_at_session_end = json_bool(text, "flat_at_session_end", c.flat_at_session_end);
        c.day_rollover_hour_et = json_int(text, "day_rollover_hour_et", c.day_rollover_hour_et);

        c.use_max_trades    = json_bool(text, "use_max_trades", c.use_max_trades);
        c.max_daily_trades  = json_int(text, "max_daily_trades", c.max_daily_trades);
        c.use_daily_loss    = json_bool(text, "use_daily_loss", c.use_daily_loss);
        c.daily_loss_pct    = json_dbl(text, "daily_loss_pct", c.daily_loss_pct);
        c.daily_loss_closed_only = json_bool(text, "daily_loss_closed_only", c.daily_loss_closed_only);
        c.use_cooldown      = json_bool(text, "use_cooldown", c.use_cooldown);
        c.cooldown_after_losses = json_int(text, "cooldown_after_losses", c.cooldown_after_losses);
        c.cooldown_bars     = json_int(text, "cooldown_bars", c.cooldown_bars);

        c.pb_valid_bars     = json_int(text, "pb_valid_bars", c.pb_valid_bars);
        c.convert_to_market = json_bool(text, "convert_to_market", c.convert_to_market);
        c.convert_bars      = json_int(text, "convert_bars", c.convert_bars);

        c.risk_pct          = json_dbl(text, "risk_pct", c.risk_pct);
        c.min_qty           = json_int(text, "min_qty", c.min_qty);
        c.qty_max           = json_int(text, "qty_max", c.qty_max);
        c.point_value       = json_dbl(text, "point_value", c.point_value);
        c.tick_size         = json_dbl(text, "tick_size", c.tick_size);
        c.starting_balance  = json_dbl(text, "starting_balance", c.starting_balance);

        c.warmup_bars       = json_int(text, "warmup_bars", c.warmup_bars);

        c.validate();
        return c;
    }

    static MtfScalperConfig from_file(const fs_mtf::path& path) {
        if (!fs_mtf::exists(path))
            throw std::runtime_error("Config file not found: " + path.string());
        std::ifstream f(path);
        if (!f) throw std::runtime_error("Cannot open config: " + path.string());
        std::string text((std::istreambuf_iterator<char>(f)),
                          std::istreambuf_iterator<char>());
        return from_json_string(text);
    }

    void validate() const {
        auto need_positive = [](const char* key, double v) {
            if (v <= 0.0)
                throw std::runtime_error(std::string("FATAL: invalid config key '") + key +
                    "' — must be > 0 (got " + std::to_string(v) + ")");
        };
        need_positive("point_value", point_value);
        need_positive("tick_size", tick_size);
        need_positive("min_qty", (double)min_qty);
        need_positive("atr_len", (double)atr_len);
        need_positive("ema_fast_len", (double)ema_fast_len);
        need_positive("ema_slow_len", (double)ema_slow_len);
        need_positive("htf_mult", (double)htf_mult);
        if (warmup_bars < 0)
            throw std::runtime_error("FATAL: invalid config key 'warmup_bars' — must be >= 0");
    }

private:
    // ── JSON helpers (copied from orb_config.hpp — private there) ────
    static size_t find_top_key(const std::string& s, const std::string& key) {
        const std::string needle = "\"" + key + "\"";
        int  depth  = 0;
        bool in_str = false;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (in_str) {
                if (c == '\\') { ++i; continue; }
                if (c == '"') in_str = false;
                continue;
            }
            if (c == '"') {
                if (depth == 1 && s.compare(i, needle.size(), needle) == 0) {
                    size_t j = i + needle.size();
                    while (j < s.size() &&
                           (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) ++j;
                    if (j < s.size() && s[j] == ':') return i;
                }
                in_str = true;
                continue;
            }
            if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') --depth;
        }
        return std::string::npos;
    }

    static int json_int(const std::string& s, const std::string& key, int def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        try { return std::stoi(s.substr(vp)); }
        catch (...) { return def; }
    }

    static double json_dbl(const std::string& s, const std::string& key, double def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        try { return std::stod(s.substr(vp)); }
        catch (...) { return def; }
    }

    static bool json_bool(const std::string& s, const std::string& key, bool def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos) return def;
        if (s.compare(vp, 4, "true")  == 0) return true;
        if (s.compare(vp, 5, "false") == 0) return false;
        return def;
    }

    static std::string json_str(const std::string& s,
                                const std::string& key,
                                const std::string& def) {
        auto pos = find_top_key(s, key);
        if (pos == std::string::npos) return def;
        auto colon = s.find(':', pos);
        if (colon == std::string::npos) return def;
        auto vp = s.find_first_not_of(" \t\r\n", colon + 1);
        if (vp == std::string::npos || s[vp] != '"') return def;
        std::string out;
        for (size_t i = vp + 1; i < s.size(); ++i) {
            char c = s[i];
            if (c == '\\' && i + 1 < s.size()) { out += s[i + 1]; ++i; continue; }
            if (c == '"') return out;
            out += c;
        }
        return def;
    }
};
