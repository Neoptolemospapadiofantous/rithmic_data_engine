/*  test_trend_strategy.cpp — TrendStrategy is a pure signal generator; these
    tests drive it with synthetic ticks (2025-04-30, EDT = UTC-4) and check that
    each mode fires exactly where its rule says, and stays silent where it must.

    Build: part of the CMake test set (test_trend_strategy); run by scripts/hermes.sh.
*/
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/execution/trend_strategy.hpp"
#include "../../src/paper/paper_quote.hpp"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } else std::printf("  ok:   %s\n", msg); } while (0)

// 2025-04-30 09:30 ET = 13:30 UTC → epoch 1746019800
static constexpr int64_t T0930 = 1746019800LL;
static int64_t at(int et_hour, int et_min, int sec = 0) {
    return (T0930 + ((et_hour - 9) * 60 + (et_min - 30)) * 60LL + sec) * 1'000'000LL;
}

struct Rec { OrbSignal sig; double px; std::string why; };

static OrbConfig risk_cfg() {
    OrbConfig c; c.max_daily_trades = 3; c.sl_points = 15; c.trail_step = 10; return c;
}

// feed one tick per second from a price path starting at `start_et_min` minutes after 09:30
static void feed_path(TrendStrategy& s, const std::vector<double>& px_per_min, int start_h, int start_m,
                      int ticks_per_min = 6) {
    for (size_t i = 0; i < px_per_min.size(); ++i) {
        int total = start_h * 60 + start_m + (int)i;
        for (int k = 0; k < ticks_per_min; ++k) {
            int64_t ts = at(total / 60, total % 60, k * (60 / ticks_per_min));
            double p = px_per_min[i] + (k % 2 ? 0.25 : -0.25);
            s.on_tick(OrbTick{ts, p, 1, k % 2 == 0});
        }
    }
}

int main() {
    std::printf("test_trend_strategy\n");

    // ── donchian: 20 flat bars on 1m then a close above the range ──────────
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 20;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(26, 20000.0);
        for (int i = 22; i < 26; ++i) path[i] = 20010.0;      // breakout bars
        feed_path(s, path, 9, 30);
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY, "donchian: BUY on close above 20-bar high");
        CHECK(out.size() == 1, "donchian: exactly one entry while in position");
        CHECK(s.session().trades_today == 1 && s.session().in_position, "donchian: session bookkeeping");
        // flat again → a break below fires a SELL
        s.notify_trade_filled(OrbSignal::BUY, "test");
        std::vector<double> down(4, 19980.0);
        feed_path(s, down, 9, 56);
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::SELL, "donchian: SELL on close below 20-bar low after flat");
    }
    // ── window: same setup outside the entry window must stay silent ────────
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 5; tc.win_start = 1000; tc.win_end = 1100;
        TrendStrategy s(tc, risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; });
        s.reset_session();
        std::vector<double> path(8, 20000.0); for (int i = 6; i < 8; ++i) path[i] = 20020.0;
        feed_path(s, path, 9, 30);                             // 09:30–09:37 — before the window
        CHECK(n == 0, "window: no entries before win_start");
        // also: no entries when shorts disallowed
        TrendConfig tl = tc; tl.win_start = 930; tl.allow_shorts = false;
        TrendStrategy s2(tl, risk_cfg()); int n2 = 0; OrbSignal last{};
        s2.set_signal_callback([&](OrbSignal g, double, const std::string&) { ++n2; last = g; });
        s2.reset_session();
        std::vector<double> dn(8, 20000.0); for (int i = 6; i < 8; ++i) dn[i] = 19980.0;
        feed_path(s2, dn, 9, 30);
        CHECK(n2 == 0, "direction: short break ignored when allow_shorts=false");
    }
    // ── supertrend: trend flip produces an entry, opposite flip flattens ────
    {
        TrendConfig tc; tc.mode = "supertrend"; tc.tf_min = 1; tc.st_len = 5; tc.st_mult = 1.0; tc.exit_on_flip = true;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path;
        for (int i = 0; i < 12; ++i) path.push_back(20000.0 - i * 2);     // down-trend → first signal SELL
        feed_path(s, path, 9, 30);
        bool got_sell = false; for (auto& r : out) if (r.sig == OrbSignal::SELL) got_sell = true;
        CHECK(got_sell, "supertrend: SELL once the down-trend is established");
        std::vector<double> up; for (int i = 0; i < 12; ++i) up.push_back(19978.0 + i * 3);   // sharp reversal
        feed_path(s, up, 9, 42);
        bool got_flat = false; for (auto& r : out) if (r.sig == OrbSignal::FLATTEN_EOD && r.why == "supertrend_flip") got_flat = true;
        CHECK(got_flat, "supertrend: flatten on opposite flip while short");
        s.notify_trade_filled(OrbSignal::SELL, "signal_flatten");           // host reports the flatten fill
        std::vector<double> more; for (int i = 0; i < 4; ++i) more.push_back(20014.0 + i * 2);
        feed_path(s, more, 9, 54);
        bool got_buy = false; for (auto& r : out) if (r.sig == OrbSignal::BUY) got_buy = true;
        CHECK(got_buy, "supertrend: re-enters long on the next bar once flat (stop-and-reverse)");
    }
    // ── nr7 + session end flatten ───────────────────────────────────────────
    {
        TrendConfig tc; tc.mode = "nr7"; tc.tf_min = 1; tc.nr_n = 7; tc.win_end = 1000;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        // 8 wide bars (±0.25 ticks give range 0.5), then a narrow bar, then a break of its high
        std::vector<double> path(10, 20000.0);
        feed_path(s, path, 9, 30, 6);                          // ranges all 0.5 → not strictly narrowest
        // make bar 9 narrow by feeding a single flat tick, then a break bar
        s.on_tick(OrbTick{at(9, 40), 20000.0, 1, true});       // bar 09:40: range 0
        s.on_tick(OrbTick{at(9, 41), 20003.0, 1, true});       // closes 09:40 (narrowest) → bar 09:41 above its high
        s.on_tick(OrbTick{at(9, 42), 20003.0, 1, true});       // closes 09:41 → NR break → BUY
        bool got = false; for (auto& r : out) if (r.sig == OrbSignal::BUY) got = true;
        CHECK(got, "nr7: BUY on break of the narrowest bar's high");
        s.check_eod(10, 0);
        bool flat = false; for (auto& r : out) if (r.sig == OrbSignal::FLATTEN_EOD && r.why == "session_end") flat = true;
        CHECK(flat, "session end: flatten at win_end while in position");
    }
    // ── gap_go: needs a prior day; first day stays silent, second day fires ──
    {
        TrendConfig tc; tc.mode = "gap_go"; tc.gap_min_pts = 20; tc.gap_wait_min = 3;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> day1(6, 20000.0);
        feed_path(s, day1, 9, 30);
        CHECK(out.empty(), "gap_go: silent without a prior-day close");
        // roll the day: prior close = 20000; open next day +40 and hold
        s.reset_session();
        std::vector<double> day2(6, 20040.0);
        // next calendar day: shift by 24h via ticks at same clock (test helper uses fixed date; simulate by direct ticks)
        for (int i = 0; i < 6; ++i)
            for (int k = 0; k < 6; ++k)
                s.on_tick(OrbTick{at(9, 30 + i, k * 10) + 86400LL * 1'000'000LL, day2[i] + (k % 2 ? 0.25 : -0.25), 1, true});
        bool got = false; for (auto& r : out) if (r.sig == OrbSignal::BUY && r.why == "gap_and_go_long") got = true;
        CHECK(got, "gap_go: BUY with an unfilled +40 gap after the wait");
    }
    // ── failed_breakout: a channel break that closes back inside is faded ───
    {
        TrendConfig tc; tc.mode = "failed_breakout"; tc.tf_min = 1; tc.donchian_n = 5; tc.fb_bars = 3;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(8, 20000.0); path.push_back(20020.0); path.push_back(19995.0); path.push_back(19995.0);
        feed_path(s, path, 9, 30);
        CHECK(!out.empty() && out[0].sig == OrbSignal::SELL && out[0].why == "failed_break_high", "failed_breakout: SELL when the high break closes back inside");
    }
    // ── keltner_ride: break out of the channel, flatten on the mid-line cross ─
    {
        TrendConfig tc; tc.mode = "keltner_ride"; tc.tf_min = 1; tc.kc_mult = 1.0; tc.exit_on_flip = true;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(25, 20000.0); path.push_back(20010.0); path.push_back(20011.0); path.push_back(19990.0); path.push_back(19990.0);
        feed_path(s, path, 9, 30);
        bool buy = false, flat = false; for (auto& r : out) { if (r.sig == OrbSignal::BUY) buy = true; if (r.why == "keltner_mid_cross") flat = true; }
        CHECK(buy, "keltner_ride: BUY on a close above the upper channel");
        CHECK(flat, "keltner_ride: flatten when the close falls back through the 20-EMA");
    }
    // ── vwap_fade: stretched from VWAP then turning back → fade, target VWAP ─
    {
        TrendConfig tc; tc.mode = "vwap_fade"; tc.tf_min = 1; tc.mr_dev_atr = 2.0; tc.mr_target_atr = 1.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(20, 20000.0); path.push_back(20010.0); path.push_back(20005.0);
        path.push_back(20001.0); path.push_back(20000.5);
        feed_path(s, path, 9, 30);
        bool sell = false, tgt = false; for (auto& r : out) { if (r.sig == OrbSignal::SELL && r.why == "vwap_fade_short") sell = true; if (r.why == "vwap_target") tgt = true; }
        CHECK(sell, "vwap_fade: SELL after a stretched bar turns back toward VWAP");
        CHECK(tgt, "vwap_fade: flatten once price is back at VWAP");
    }
    // ── band_fade: close outside the band then back inside → fade to the mid ─
    {
        TrendConfig tc; tc.mode = "band_fade"; tc.tf_min = 1; tc.bb_len = 5; tc.bb_mult = 1.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(6, 20000.0); path.push_back(20010.0); path.push_back(20004.0); path.push_back(20000.0); path.push_back(20000.0);
        feed_path(s, path, 9, 30);
        bool sell = false, tgt = false; for (auto& r : out) { if (r.why == "band_fade_short") sell = true; if (r.why == "band_mid_target") tgt = true; }
        CHECK(sell, "band_fade: SELL on the close back inside the upper band");
        CHECK(tgt, "band_fade: flatten at the band mid");
    }
    // ── rsi2_pullback: oversold dip inside an uptrend, exit when RSI recovers ─
    {
        TrendConfig tc; tc.mode = "rsi2_pullback"; tc.tf_min = 1; tc.ema_slow = 5; tc.rsi_len = 2; tc.rsi_buy = 10; tc.rsi_exit = 50;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path; for (int i = 0; i <= 10; ++i) path.push_back(20000.0 + 3 * i);   // 20000..20030
        path.push_back(20028.0); path.push_back(20027.0);                                         // two down closes → RSI(2)=0
        path.push_back(20031.0); path.push_back(20034.0);                                         // recovery → RSI(2)=100
        feed_path(s, path, 9, 30);
        bool buy = false, ex = false; for (auto& r : out) { if (r.why == "rsi2_oversold_in_uptrend") buy = true; if (r.why == "rsi_exit") ex = true; }
        CHECK(buy, "rsi2_pullback: BUY the oversold dip above the slow EMA");
        CHECK(ex, "rsi2_pullback: flatten when RSI crosses back above the exit level");
    }
    // ── trend_day: recognise an up trend day, buy the VWAP pullback ─────────
    {
        TrendConfig tc; tc.mode = "trend_day"; tc.tf_min = 1; tc.td_check_min = 10; tc.td_min_atr = 1.0; tc.td_pullback = "vwap"; tc.vwap_tol_atr = 0.5;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path; for (int i = 0; i < 13; ++i) path.push_back(20000.0 + 2 * i);   // steady climb, closes above VWAP
        feed_path(s, path, 9, 30);
        CHECK(out.empty(), "trend_day: no entry before a pullback");
        // pullback bar: dips well below VWAP then closes above it and above its open
        s.on_tick(OrbTick{at(9, 43, 0), 20004.0, 1, true});
        for (int k = 1; k < 6; ++k) s.on_tick(OrbTick{at(9, 43, k * 10), 20014.0, 1, true});
        s.on_tick(OrbTick{at(9, 44, 0), 20014.0, 1, true});                                       // closes the 09:43 bar
        bool buy = false; for (auto& r : out) if (r.why == "trend_day_pullback_long") buy = true;
        CHECK(buy, "trend_day: BUY the pullback that closes back above VWAP");
    }
    // ── delta_trend: price high confirmed by a cumulative-delta high ─────────
    {
        TrendConfig tc; tc.mode = "delta_trend"; tc.tf_min = 1; tc.dt_bars = 3;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(6, 20000.0);
        feed_path(s, path, 9, 30);                                   // balanced buys/sells → delta stays 0
        for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, 36, k * 10), 20020.0, 1, true});   // all aggressor buys
        s.on_tick(OrbTick{at(9, 37, 0), 20020.0, 1, true});
        bool buy = false; for (auto& r : out) if (r.why == "price_delta_high") buy = true;
        CHECK(buy, "delta_trend: BUY when price and cumulative delta both break out");
        // divergence: price high with balanced flow (delta not a new high) must not fire
        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; });
        s2.reset_session();
        std::vector<double> p2(6, 20000.0); p2.push_back(20020.0); p2.push_back(20020.0);
        feed_path(s2, p2, 9, 30);
        CHECK(n2 == 0, "delta_trend: price high WITHOUT a delta high is blocked (divergence)");
    }
    // ── fib_pullback: retrace into the zone, resume through the prior high ──
    {
        TrendConfig tc; tc.mode = "fib_pullback"; tc.tf_min = 1; tc.fib_swing_bars = 6; tc.ema_fast = 3; tc.ema_slow = 6;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path; for (int i = 0; i < 8; ++i) path.push_back(20000.0 + 2 * i);   // impulse to 20014
        path.push_back(20010.0);                                                                    // ~50% retrace of the 6-bar window (low 20006)
        path.push_back(20012.0); path.push_back(20012.0);                                           // resumes above the prior high (+1 bar to close it)
        feed_path(s, path, 9, 30);
        bool buy = false; for (auto& r : out) if (r.why == "fib_retrace_long") buy = true;
        CHECK(buy, "fib_pullback: BUY the resumption out of the retracement zone");
    }
    // ── ema_ribbon: 3-EMA stack pullback, then unstack flattens ──────────────
    {
        TrendConfig tc; tc.mode = "ema_ribbon"; tc.tf_min = 1;
        tc.ribbon_fast = 3; tc.ribbon_mid = 5; tc.ribbon_slow = 8;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path; for (int i = 0; i < 18; ++i) path.push_back(20000.0 + 3 * i);   // steady climb, stacks the ribbon
        path.push_back(20040.0);                                                                    // pullback bar: dips to the mid EMA
        path.push_back(20055.0);                                                                    // resumes above the pullback bar's high
        for (int i = 1; i <= 4; ++i) path.push_back(20055.0 - 15 * i);                              // sharp reversal → unstacks
        feed_path(s, path, 9, 30);
        bool buy = false, flat = false;
        for (auto& r : out) { if (r.why == "ribbon_pullback_long") buy = true; if (r.why == "ribbon_unstack") flat = true; }
        CHECK(buy, "ema_ribbon: BUY the pullback to the mid EMA while fast>mid>slow");
        CHECK(flat, "ema_ribbon: flatten once the stack unstacks");
    }
    // ── thrust_fade: a parabolic run that fails to extend gets faded ────────
    {
        TrendConfig tc; tc.mode = "thrust_fade"; tc.tf_min = 1;
        tc.thrust_bars = 3; tc.thrust_min_atr = 2.0; tc.thrust_retrace_frac = 0.5; tc.thrust_target_atr = 1.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(6, 20000.0);                                    // baseline (small ATR)
        path.push_back(20020.0); path.push_back(20040.0); path.push_back(20060.0); // 3-bar thrust, ~40pt run
        path.push_back(20055.0);                                                   // fails to extend, retraces past the mid — fade short
        path.push_back(20030.0); path.push_back(20030.0);                          // continues down to the fade target (+1 bar to close it)
        feed_path(s, path, 9, 30);
        bool sell = false, tgt = false;
        for (auto& r : out) { if (r.why == "thrust_exhaustion_fade_short") sell = true; if (r.why == "thrust_target") tgt = true; }
        CHECK(sell, "thrust_fade: SELL when the up-thrust fails to extend and retraces");
        CHECK(tgt, "thrust_fade: flatten once the fade reaches its ATR target");
    }
    // ── tape modes: bars with controlled volume / aggressor side ────────────
    // feed one 1m bar at (h, m): 4 prints o→l→h→c; buy volume = vol×share on the o/h
    // prints, sell volume on the l/c prints (sizes are integers, vol ≥ 4).
    auto feed_bar = [](TrendStrategy& s, int h, int m, double o, double hi, double lo, double c, int vol, double share) {
        int bv = (int)std::lround(vol * share), sv = vol - bv;
        int b1 = bv / 2, b2 = bv - b1, s1 = sv / 2, s2 = sv - s1;
        if (b1 > 0) s.on_tick(OrbTick{at(h, m, 0),  o,  (int64_t)b1, true});
        if (s1 > 0) s.on_tick(OrbTick{at(h, m, 15), lo, (int64_t)s1, false});
        if (b2 > 0) s.on_tick(OrbTick{at(h, m, 30), hi, (int64_t)b2, true});
        if (s2 > 0) s.on_tick(OrbTick{at(h, m, 45), c,  (int64_t)s2, false});
        else        s.on_tick(OrbTick{at(h, m, 45), c,  1, true});
    };
    auto minute = [](int i, int& h, int& m) { int t = 9 * 60 + 30 + i; h = t / 60; m = t % 60; };
    // absorption_reversal: 25 quiet bars, a 5-bar +15 run, then a huge-volume bar with no
    // range (buyers absorbed), then a close back below its midpoint → SELL.
    {
        TrendConfig tc; tc.mode = "absorption_reversal"; tc.tf_min = 1; tc.vol_avg_bars = 10;
        tc.abs_vol_mult = 2.0; tc.abs_max_range_atr = 0.5; tc.abs_min_move_atr = 1.5; tc.abs_move_bars = 5; tc.abs_delta_min = 0.6;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int i = 0, h, m; double px = 20000.0;
        for (; i < 25; ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 1.0, px - 1.0, px, 60, 0.5); }
        for (int k = 0; k < 5; ++k, ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 3.2, px - 0.2, px + 3.0, 60, 0.5); px += 3.0; }
        minute(i++, h, m); feed_bar(s, h, m, px, px + 0.3, px - 0.2, px + 0.1, 240, 0.7);   // absorption: 4× volume, no range, buyers
        CHECK(out.empty(), "absorption: arms on the absorption bar, no entry yet");
        minute(i++, h, m); feed_bar(s, h, m, px + 0.1, px + 0.2, px - 1.5, px - 1.2, 60, 0.4);  // closes back through the midpoint
        minute(i++, h, m); feed_bar(s, h, m, px - 1.2, px - 1.0, px - 1.6, px - 1.4, 60, 0.5);  // completes that bar
        bool sell = false; for (auto& r : out) if (r.why == "absorption_fade_short") sell = true;
        CHECK(sell, "absorption: SELL when the next bar closes back through the absorption bar's midpoint");
    }
    // delta_divergence (dd_bars=5): a new 5-bar high with sellers on the breakout bar and a
    // 5-bar delta sum below its recent max → SELL.
    {
        TrendConfig tc; tc.mode = "delta_divergence"; tc.tf_min = 1; tc.dd_bars = 5;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int i = 0, h, m; double px = 20000.0;
        for (; i < 12; ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 1.0, px - 1.0, px, 60, 0.5); }
        for (int k = 0; k < 5; ++k, ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 2.2, px - 0.2, px + 2.0, 60, 0.8); px += 2.0; }  // strong buying
        CHECK(out.empty(), "delta_divergence: confirmed buying makes no signal");
        minute(i++, h, m); feed_bar(s, h, m, px, px + 2.5, px - 0.5, px + 1.0, 60, 0.2);   // new high, sellers dominant
        minute(i++, h, m); feed_bar(s, h, m, px + 1.0, px + 1.2, px + 0.5, px + 0.8, 60, 0.5);
        bool sell = false; for (auto& r : out) if (r.why == "delta_divergence_short") sell = true;
        CHECK(sell, "delta_divergence: SELL a new high the delta does not confirm");
    }
    // volume_burst: a 4× volume bar closing at its high with 80% buy share in an uptrend → BUY.
    {
        TrendConfig tc; tc.mode = "volume_burst"; tc.tf_min = 1; tc.vol_avg_bars = 10; tc.vb_vol_mult = 2.5; tc.vb_trend_agree = true;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int i = 0, h, m; double px = 20000.0;
        for (; i < 12; ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 1.0, px - 1.0, px, 60, 0.5); }
        for (int k = 0; k < 3; ++k, ++i) { minute(i, h, m); feed_bar(s, h, m, px, px + 1.2, px - 0.2, px + 1.0, 60, 0.55); px += 1.0; }  // mild uptrend
        minute(i++, h, m); feed_bar(s, h, m, px, px + 4.0, px - 0.2, px + 4.0, 240, 0.8);   // the burst, closes at its high
        minute(i++, h, m); feed_bar(s, h, m, px + 4.0, px + 4.2, px + 3.6, px + 3.9, 60, 0.5);
        bool buy = false; for (auto& r : out) if (r.why == "volume_burst_long") buy = true;
        CHECK(buy, "volume_burst: BUY a high-volume bar closing at its high with the aggressors long");
    }
    // book_imbalance with bi_invert: a bid-stacked book held for the hold time is FADED (SELL).
    {
        TrendConfig tc; tc.mode = "book_imbalance"; tc.bi_invert = true; tc.bi_min = 0.70; tc.bi_hold_secs = 2;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        s.on_tick(OrbTick{at(10, 0, 0), 20000.0, 1, true});
        s.on_quote(at(10, 0, 1), 19999.75, 90, 20000.0, 10);   // bid-stacked → side arms
        s.on_quote(at(10, 0, 4), 19999.75, 90, 20000.0, 10);   // held 3 s ≥ hold
        CHECK(!out.empty() && out[0].sig == OrbSignal::SELL && out[0].why == "book_fade_bid",
              "book_fade: SELL into a bid-stacked book (inverted imbalance)");
        s.on_quote(at(10, 0, 9), 19999.75, 50, 20000.0, 50);   // normalised → flat
        CHECK(out.size() == 2 && out[1].why == "imbalance_normalised", "book_fade: flattens when the book normalises");
    }
    // ── roc_momentum: accelerating move fires; flat tape does not ───────────
    {
        TrendConfig tc; tc.mode = "roc_momentum"; tc.tf_min = 1; tc.roc_bars = 3; tc.roc_min_atr = 1.0; tc.roc_hi_bars = 3;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(8, 20000.0); for (int i = 1; i <= 4; ++i) path.push_back(20000.0 + 6 * i);
        feed_path(s, path, 9, 30);
        bool buy = false; for (auto& r : out) if (r.why == "roc_new_high") buy = true;
        CHECK(buy, "roc_momentum: BUY when ROC is large and a fresh high");
    }
    // ── htf gate: a valid 1m breakout is blocked while the 5m close is not above its EMA ─
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 5; tc.htf_tf_min = 5; tc.htf_ema = 2;
        TrendStrategy s(tc, risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; });
        s.reset_session();
        std::vector<double> flat(12, 20000.0); flat.push_back(20020.0); flat.push_back(20020.0);
        feed_path(s, flat, 9, 30);
        CHECK(n == 0, "htf gate: flat 5-minute closes (close == EMA) block the long");
        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; });
        s2.reset_session();
        std::vector<double> rise; for (int i = 0; i < 14; ++i) rise.push_back(20000.0 + 2 * i);
        feed_path(s2, rise, 9, 30);
        CHECK(n2 >= 1, "htf gate: rising 5-minute closes above their EMA allow the long");
    }
    // ── chandelier exit: flatten when price gives back k×ATR from the best ──
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 5; tc.chandelier_mult = 2.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(8, 20000.0); path.push_back(20020.0); path.push_back(20021.0); path.push_back(20015.0); path.push_back(20015.0);
        feed_path(s, path, 9, 30);
        bool flat = false; for (auto& r : out) if (r.why == "chandelier") flat = true;
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY && flat, "chandelier: BUY on the break, flatten after the give-back");
    }
    // ── wrapped window (Asia 20:00–02:30): entries allowed after midnight, flat at 02:30 ─
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 5; tc.win_start = 2000; tc.win_end = 230; tc.session = "window";
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        // 00:10 ET next day = at(24, 10) via the helper's hour arithmetic
        std::vector<double> path(8, 20000.0); path.push_back(20020.0); path.push_back(20020.0);
        feed_path(s, path, 24, 10);
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY, "wrapped window: breakout at 00:18 ET is inside 20:00–02:30");
        s.check_eod(2, 30);
        bool flat = false; for (auto& r : out) if (r.why == "session_end") flat = true;
        CHECK(flat, "wrapped window: flatten at 02:30");
        // 12:00 ET (outside) must be silent
        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; });
        s2.reset_session(); feed_path(s2, path, 12, 0);
        CHECK(n2 == 0, "wrapped window: no entries at noon");
    }
    // ── session anchor: VWAP fade works at the London open when session=window ─
    {
        TrendConfig tc; tc.mode = "vwap_fade"; tc.tf_min = 1; tc.mr_dev_atr = 2.0; tc.mr_target_atr = 1.0;
        tc.win_start = 300; tc.win_end = 700; tc.session = "window";
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(20, 20000.0); path.push_back(20010.0); path.push_back(20005.0); path.push_back(20001.0); path.push_back(20000.5);
        feed_path(s, path, 3, 0);
        bool sell = false; for (auto& r : out) if (r.why == "vwap_fade_short") sell = true;
        CHECK(sell, "session=window: VWAP anchored at 03:00 lets the London fade fire");
    }
    // ── book gates + honest fills (paper_quote.hpp) ─────────────────────────
    {
        paper::QuoteState qs; OrbConfig c; const double tick = 0.25;
        paper::Quote q{1'000'000, 20000.00, 20000.50, 6, 2};       // 2-tick spread, bid-heavy (0.75)
        qs.on_quote(q, tick);
        CHECK(qs.gate(c, +1, 1'000'000, tick).empty(), "gate: nothing configured → allowed");
        c.spread_gate_ticks = 1.0;
        CHECK(qs.gate(c, +1, 1'000'000, tick) == "spread_ticks", "gate: spread 2 > 1 tick blocks");
        c.spread_gate_ticks = 0; c.imbalance_min = 0.6;
        CHECK(qs.gate(c, +1, 1'000'000, tick).empty() && qs.gate(c, -1, 1'000'000, tick) == "imbalance", "gate: imbalance 0.75 allows longs, blocks shorts");
        c.imbalance_min = 0; c.microprice_lead = true;
        CHECK(qs.gate(c, +1, 1'000'000, tick).empty() && qs.gate(c, -1, 1'000'000, tick) == "microprice", "gate: microprice leans to the ask → longs only");
        CHECK(qs.gate(c, +1, 20'000'000, tick) == "no_quote", "gate: a 19-second-old quote is no book");
        CHECK(qs.market_fill(+1, 1'000'000, 19999.0) == 20000.50 && qs.market_fill(-1, 1'000'000, 20001.0) == 20000.00, "fill: buy at ask, sell at bid");
        CHECK(qs.market_fill(-1, 1'000'000, 19999.75, 19999.75) == 19999.75, "fill: a stop exit never fills better than the touch");
        CHECK(qs.market_fill(+1, 1'000'000, 20003.0, 20002.5) == 20002.5, "fill: a buy never fills better than the print that triggered it (stale quote)");
        CHECK(qs.market_fill(+1, 30'000'000, 19999.0) == 19999.0, "fill: stale book → fallback fill");
        OrbConfig c2; c2.imbalance_max = 0.45;                       // inverted gate: only fade a stacked book
        CHECK(qs.gate(c2, +1, 1'000'000, tick) == "imbalance_inv" && qs.gate(c2, -1, 1'000'000, tick).empty(), "inverted gate: bid-heavy book blocks longs, allows shorts");
        OrbConfig c3; c3.book_exit_flip = 0.35; c3.book_tp_imbalance = 0.75;
        CHECK(qs.book_exit(c3, -1, 1'000'000, 1.0).empty() == false && qs.book_exit(c3, -1, 1'000'000, 1.0) == "book_flip", "book exit: a short with the book at 0.75 bid share flips out");
        CHECK(qs.book_exit(c3, +1, 1'000'000, 2.0) == "book_tp" && qs.book_exit(c3, +1, 1'000'000, -1.0).empty(), "book exit: a profitable long takes profit into a 0.75 stack, a losing one does not");
        CHECK(qs.agrees(+1) && !qs.agrees(-1) && qs.flipped_against(-1, 1'000'000) && !qs.flipped_against(+1, 1'000'000), "book agreement / flip helpers");
        CHECK(!qs.fill_ready(-1, 1'000'000, tick) && qs.fill_ready(+1, 1'000'000, tick) && qs.fill_ready(-1, 40'000'000, tick), "fill timing: 2-tick spread leaning to the ask → shorts wait, longs go, stale book never waits");
    }
    // ── book_imbalance mode: sustained bid-heavy book → long, normalising → flat ─
    {
        TrendConfig tc; tc.mode = "book_imbalance"; tc.bi_min = 0.7; tc.bi_hold_secs = 3; tc.bi_max_spread_ticks = 2;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        for (int k = 0; k < 6; ++k) s.on_quote(at(10, 0, k), 20000.0, 8, 20000.25, 2);      // 0.8 for 5 s
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY && out[0].why == "book_imbalance_bid", "book_imbalance: BUY after 3 s of 0.8 bid share");
        s.on_quote(at(10, 0, 8), 20000.0, 4, 20000.25, 4);                                     // 0.5 → normalised
        bool flat = false; for (auto& r : out) if (r.why == "imbalance_normalised") flat = true;
        CHECK(flat, "book_imbalance: flatten once the book normalises");
    }
    // ── config parsing ──────────────────────────────────────────────────────
    {
        auto c = TrendConfig::from_json_string(R"({"mode":"squeeze","tf_min":15,"win_start":935,"allow_shorts":false,"bb_mult":2.5})");
        CHECK(c.mode == "squeeze" && c.tf_min == 15 && c.win_start == 935 && !c.allow_shorts && std::fabs(c.bb_mult - 2.5) < 1e-9,
              "config: json fields parsed");
    }

    // ── live executor host: trading date + engine config ────────────────────
    {
        // the trading day rolls at 18:00 ET (Tradeify/CME), not at midnight
        CHECK(trading_date_str(1790200740) == "2026-09-23", "trading date: 17:59 ET belongs to the same day");
        CHECK(trading_date_str(1790200800) == "2026-09-24", "trading date: 18:00 ET rolls to the next day");
        CHECK(trading_date_str(1790222340) == "2026-09-24", "trading date: 23:59 ET is still the next day");
        CHECK(trading_date_str(1790224200) == "2026-09-24", "trading date: 00:30 ET keeps the calendar date");
        CHECK(trading_date_str(1790256600) == "2026-09-24", "trading date: 09:30 ET RTH keeps the calendar date");
        CHECK(trading_date_str(1796165940) == "2026-12-01" && trading_date_str(1796166000) == "2026-12-02",
              "trading date: EST (UTC-5) rolls at 23:00 UTC");

        OrbConfig c; c.account_label = "tradeify";
        bool threw = false;
        c.engine = "trend"; c.strategy = "ORB";
        try { c.validate(); } catch (const std::exception&) { threw = true; }
        CHECK(threw, "config: trend engine refuses the ORB strategy tag");
        threw = false; c.strategy = "TREND_ST";
        try { c.validate(); } catch (const std::exception&) { threw = true; }
        CHECK(!threw, "config: trend engine with its own tag validates");
        threw = false; c.engine = "scalp";
        try { c.validate(); } catch (const std::exception&) { threw = true; }
        CHECK(threw, "config: unknown engine refused");
    }
    // ── host release: a signal the order manager never executed ────────────
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 20;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(26, 20000.0);
        for (int i = 22; i < 26; ++i) path[i] = 20010.0;
        feed_path(s, path, 9, 30);
        CHECK(s.session().in_position, "release: engine books the entry it emitted");
        s.notify_trade_filled(OrbSignal::FLATTEN_EOD, "not_executed:risk");
        CHECK(!s.session().in_position && s.session().trades_today == 1,
              "release: host notify frees the engine, the attempt still counts toward max_daily_trades");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
