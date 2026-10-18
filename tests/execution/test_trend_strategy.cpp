/*  test_trend_strategy.cpp — TrendStrategy is a pure signal generator; these
    tests drive it with synthetic ticks (2025-04-30, EDT = UTC-4) and check that
    each mode fires exactly where its rule says, and stays silent where it must.

    Build: part of the CMake test set (test_trend_strategy); run by scripts/hermes.sh.
*/
#include "../../src/paper/vix_regime.hpp"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
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

// one 1-minute bar o -> h -> l -> c, `vol` contracts split into 8 ticks, the first buy_share of them aggressor-buys
static void az_bar(TrendStrategy& s, int h, int m, double o, double hi, double lo, double c, int vol, double buy_share) {
    const double path[4] = {o, hi, lo, c};
    const int n = 8, per = vol / n, buys = (int)(n * buy_share + 0.5);
    for (int k = 0; k < n; ++k) s.on_tick(OrbTick{at(h, m, k * 7), path[std::min(3, k * 4 / n)], per, k < buys});
}
static TrendConfig az_balance_cfg() {
    TrendConfig tc; tc.mode = "absorption_zone"; tc.tf_min = 1; tc.az_daytype = false; tc.az_setups = "balance";
    tc.az_ema200_bias = false; tc.az_require_slope = false; tc.az_from = 930; tc.az_to = 1600;
    tc.win_start = 930; tc.win_end = 1555; return tc;
}
// 40 quiet bars around 20000 (VWAP ~20000), then the absorption bar and the trigger bar
static int az_feed_balance(TrendStrategy& s, double abs_lo, double abs_c, int abs_vol, double trig_c) {
    int hh = 9, mm = 30; auto next = [&]() { if (++mm == 60) { mm = 0; ++hh; } };
    for (int i = 0; i < 40; ++i) { az_bar(s, hh, mm, 20000, 20010, 19990, i % 2 ? 20002 : 19998, 100, 0.5); next(); }
    az_bar(s, hh, mm, 19992, 19993, abs_lo, abs_c, abs_vol, 0.2); next();          // heavy volume, net selling, held
    az_bar(s, hh, mm, abs_c, trig_c + 1, abs_c - 1, trig_c, 150, 0.8); next();     // close back above the zone, buyers
    s.on_tick(OrbTick{at(hh, mm, 0), trig_c, 1, true});                             // first tick of the next minute closes it
    return hh * 60 + mm;
}
static TrendConfig tcf_cfg() {
    TrendConfig tc; tc.mode = "trend_confirm"; tc.tf_min = 1; tc.tcf_ema_fast = 3; tc.tcf_ema_mid = 5; tc.tcf_ema_slow = 8;
    tc.tcf_er_bars = 10; tc.tcf_er_min = 0.3; tc.tcf_flow_bars = 3; tc.tcf_pb_bars = 3; tc.tcf_stop_atr = 3; tc.tcf_trail_atr = 1.0;
    tc.tcf_exit_ema = false; tc.tcf_bo_bars = 10; tc.tcf_vol_mult = 1.2; tc.win_start = 930; tc.win_end = 1555; return tc;
}
// 30-bar climb (2 pts/bar, buy share bs), two pullback bars, a 130-lot trigger bar closing above the prior high;
// sign -1 mirrors every price around 20000 and every buy share. Returns the next free minute (since midnight).
static int tcf_feed(TrendStrategy& s, int sign, double bs, double pb_bs, bool chop) {
    int t = 9 * 60 + 30; double px = 20000;
    auto B = [&](double o, double h, double l, double c, int v, double share) {
        if (sign < 0) az_bar(s, t / 60, t % 60, 40000 - o, 40000 - l, 40000 - h, 40000 - c, v, 1.0 - share);
        else          az_bar(s, t / 60, t % 60, o, h, l, c, v, share);
        ++t;
    };
    for (int i = 0; i < 30; ++i) {
        if (chop) { const double o = 20000 + (i % 2 ? 4 : -4); B(o, o + 3, o - 3, o + (i % 2 ? -3 : 3), 100, 0.5); }
        else { B(px, px + 3, px - 1, px + 2, 100, bs); px += 2; }
    }
    const double top = px;
    B(top, top + 1, top - 4, top - 2, 100, chop ? 0.5 : pb_bs);
    B(top - 2, top - 1, top - 6, top - 4, 100, chop ? 0.5 : pb_bs);
    B(top - 4, top + 2, top - 4, top + 1.5, 130, 0.8);                        // trigger: close above the prior high, buyers
    s.on_tick(OrbTick{at(t / 60, t % 60, 0), sign > 0 ? top + 1.5 : 40000 - (top + 1.5), 1, sign > 0});   // closes it
    return t;
}
int main() {
    std::printf("test_trend_strategy\n");

    // ── atr_break (2026-09-29): close beyond open ± k × day ATR, one per side ──
    {
        TrendConfig tc; tc.mode = "atr_break"; tc.tf_min = 1; tc.ab_k = 0.5; tc.ab_anchor = "open"; tc.ab_day_atr = true;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); s.set_day_atr(100.0);                         // threshold = open ± 50
        std::vector<double> path = {20000, 20010, 20030, 20060, 20070, 20070};   // 09:33 closes above 20050
        feed_path(s, path, 9, 30);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "atr_break_long", "atr_break: BUY through open + 0.5 ATR");
        s.notify_trade_filled(OrbSignal::BUY, "test");
        feed_path(s, {20080, 20090}, 9, 36);
        CHECK(out.size() == 1, "atr_break: long side used once per session");
        TrendConfig t2 = tc; t2.ab_day_atr = true; TrendStrategy s2(t2, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; }); s2.reset_session();   // no day ATR set
        feed_path(s2, path, 9, 30);
        CHECK(n2 == 0, "atr_break: silent without a day ATR (fail-closed)");
    }
    // ── opening_drive + drive_day_atr + drive_market (2026-10-13): decide at 09:30 + D on k × day ATR, enter at market ──
    {
        TrendConfig tc; tc.mode = "opening_drive"; tc.tf_min = 1; tc.drive_minutes = 4; tc.drive_min_atr = 0.2;
        tc.drive_day_atr = true; tc.drive_market = true; tc.drive_entry_from = 930; tc.drive_entry_to = 1000;
        auto run = [&](const std::vector<double>& path, double day_atr, bool set_atr) {
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); if (set_atr) s.set_day_atr(day_atr);
            feed_path(s, path, 9, 30);
            return out;
        };
        // threshold 0.2 × 100 = 20 pts; bar 09:34 decides (closes ~20030 vs open ~20000)
        auto up = run({20000, 20010, 20020, 20025, 20030, 20000, 19990, 20040, 20050}, 100.0, true);
        CHECK(up.size() == 1 && up[0].sig == OrbSignal::BUY && up[0].why == "drive_market_long", "drive_market: BUY once at the decision bar");
        auto dn = run({20000, 19990, 19980, 19975, 19970, 20000, 20010}, 100.0, true);
        CHECK(dn.size() == 1 && dn[0].sig == OrbSignal::SELL, "drive_market: SELL on a strong down open");
        auto quiet = run({20000, 20005, 20010, 20008, 20010, 20040, 20060}, 100.0, true);
        CHECK(quiet.empty(), "drive_market: no trade when the drive is under k × day ATR (later moves ignored)");
        auto noatr = run({20000, 20010, 20020, 20025, 20030, 20040}, 0.0, false);
        CHECK(noatr.empty(), "drive_market: silent without a day ATR (fail-closed)");
    }
    // ── E1 strong open (2026-10-14): drive_score_min / drive_score_units / drive_stop_atr ──
    {
        TrendConfig tc; tc.mode = "opening_drive"; tc.tf_min = 1; tc.drive_minutes = 4; tc.drive_min_atr = 0.15;
        tc.drive_day_atr = true; tc.drive_market = true; tc.drive_entry_from = 930; tc.drive_entry_to = 1000;
        tc.drive_score_big = 0.35; tc.drive_score_units = true; tc.drive_stop_atr = 0.35;
        const int64_t DAY = 86'400'000'000LL;
        // prior RTH day closing at 20000, 18:00 overnight open `on`, then today's drive path (one price per minute)
        auto run = [&](int score_min, double on, const std::vector<double>& path, int& units, double& sd) {
            tc.drive_score_min = score_min;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, 30 + k) - DAY, 20000.0, 1, true});   // yesterday's RTH
            s.on_tick(OrbTick{at(15, 59) - DAY, 20000.0, 1, true});
            s.reset_session();                                                                      // 18:00 rollover
            s.on_tick(OrbTick{at(18, 0) - DAY, on, 1, true});
            s.on_tick(OrbTick{at(18, 1) - DAY, on, 1, true});
            s.set_day_atr(100.0);
            feed_path(s, path, 9, 30);
            units = s.last_units(); sd = s.take_stop_dist();
            return out;
        };
        int u = 0; double sd = 0.0;
        // down drive −39.5 (> .35 × 100): gap down, open < 18:00 open 19990, closes at the low → score 4
        auto a4 = run(2, 19990.0, {19980, 19960, 19950, 19940, 19930, 19920}, u, sd);
        CHECK(a4.size() == 1 && a4[0].sig == OrbSignal::SELL, "E1: score-4 down drive → SELL");
        CHECK(u == 3, "E1: score 4 → 3 units");
        CHECK(std::fabs(sd - 35.0) < 1e-9, "E1: stop distance = .35 × day ATR");
        // same drive but the 18:00 open was below today's open → overnight point missing → score 3
        auto a3 = run(2, 19970.0, {19980, 19960, 19950, 19940, 19930}, u, sd);
        CHECK(a3.size() == 1 && u == 2, "E1: score 3 → 2 units");
        auto none = run(4, 19970.0, {19980, 19960, 19950, 19940, 19930}, u, sd);
        CHECK(none.empty(), "E1: score 3 < min 4 → no trade");
        // up drive +20 (> .15 ATR, < .35): gap down, open < 18:00 open, closes mid-range → score 0 → no trade
        auto weak = run(2, 19990.0, {19980, 20010, 19990, 20000, 20010}, u, sd);
        CHECK(weak.empty(), "E1: weak up drive against gap/overnight → no trade");
        // default (drive_score_min 0) still trades the plain drive and asks for no units / stop distance
        tc.drive_score_units = false; tc.drive_stop_atr = 0.0;
        auto plain = run(0, 19990.0, {19980, 20010, 19990, 20000, 20010}, u, sd);
        CHECK(plain.size() == 1 && plain[0].sig == OrbSignal::BUY && u == 0 && sd == 0.0, "E1 off: plain drive_market unchanged");
    }
    // ── thesis exits on the drive (2026-10-16): drive_exit_vwap / drive_exit_stall_min ──
    {
        TrendConfig tc; tc.mode = "opening_drive"; tc.tf_min = 1; tc.drive_minutes = 4; tc.drive_min_atr = 0.2;
        tc.drive_day_atr = true; tc.drive_market = true; tc.drive_entry_from = 930; tc.drive_entry_to = 1000;
        tc.drive_stop_atr = 0.35;
        auto run = [&](TrendConfig t, const std::vector<double>& path) {
            TrendStrategy s(t, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            feed_path(s, path, 9, 30);
            return out;
        };
        // BUY at the 09:34 close (~20030, entry minute 09:35); the first 5-minute close checked is 09:39's
        const std::vector<double> drop = {20000, 20010, 20020, 20025, 20030, 20040, 20040, 20040, 20040, 19980, 19970};
        TrendConfig v = tc; v.drive_exit_vwap = true;
        auto a = run(v, drop);
        CHECK(a.size() == 2 && a[0].sig == OrbSignal::BUY && a[1].sig == OrbSignal::FLATTEN_EOD && a[1].why == "drive_vwap_exit"
              && std::fabs(a[1].px - 19980.25) < 1e-9, "drive_exit_vwap: the 09:39 5-minute close below VWAP closes the long");
        // a dip below VWAP inside the bucket (09:36) is not a 5-minute close → no exit while 09:39 closes above
        auto b = run(v, {20000, 20010, 20020, 20025, 20030, 20040, 19990, 20045, 20050, 20055, 20060});
        CHECK(b.size() == 1 && b[0].sig == OrbSignal::BUY, "drive_exit_vwap: only completed 5-minute closes count");
        auto off = run(tc, drop);
        CHECK(off.size() == 1, "thesis exits off by default: no exit signal");
        // stall: 10 min after the entry the best excursion must reach 0.5 × the 35-pt stop (17.5 pts)
        TrendConfig st = tc; st.drive_exit_stall_min = 10; st.drive_exit_stall_r = 0.5;
        std::vector<double> flat = {20000, 20010, 20020, 20025, 20030};
        for (int k = 0; k < 14; ++k) flat.push_back(20035);
        auto c = run(st, flat);
        CHECK(c.size() == 2 && c[1].why == "drive_stall_exit" && std::fabs(c[1].px - 20035.25) < 1e-9, "drive_exit_stall_min: no progress after 10 min → exit");
        std::vector<double> go = {20000, 20010, 20020, 20025, 20030};
        for (int k = 0; k < 14; ++k) go.push_back(20035 + 3 * k);
        auto d = run(st, go);
        CHECK(d.size() == 1, "drive_exit_stall_min: a trade that went 0.5R in favour is kept");
    }
    // ── asia_sweep / round_break (2026-10-16): liquidity sweep / break with order flow on 5-minute bars ──
    {
        const int64_t DAY = 86'400'000'000LL;
        // one 1-minute bar at h:m (day_off = −1 → the evening before), 8 ticks o → hi → lo → c, buy_share aggressor buys
        auto bar1 = [&](TrendStrategy& s, int day_off, int h, int m, double o, double hi, double lo, double c, double buy_share) {
            const double path[4] = {o, hi, lo, c};
            const int buys = (int)(8 * buy_share + 0.5);
            for (int k = 0; k < 8; ++k) s.on_tick(OrbTick{at(h, m, k * 7) + day_off * DAY, path[std::min(3, k / 2)], 10, k < buys});
        };
        auto run_asia = [&](double sweep_buy_share, double flat_buy_share, double sweep_hi, double sweep_c, double& sd) {
            TrendConfig tc; tc.mode = "asia_sweep"; tc.tf_min = 5; tc.win_start = 300; tc.win_end = 1200;
            tc.lf_delta_min = 0.05; tc.lf_stop_atr = 0.25;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            for (int mm = 18 * 60; mm < 24 * 60; ++mm) {                          // evening: Asia high 20010 at 21:00
                const double hi = mm == 21 * 60 ? 20010 : 20000.5;
                bar1(s, -1, mm / 60, mm % 60, 20000, hi, 19999.5, 20000, flat_buy_share);
            }
            for (int mm = 0; mm < 3 * 60; ++mm) {                                 // after midnight: Asia low 19990 at 01:00
                const double lo = mm == 60 ? 19990 : 19999.5;
                bar1(s, 0, mm / 60, mm % 60, 20000, 20000.5, lo, 20000, flat_buy_share);
            }
            for (int mm = 0; mm < 5; ++mm)                                        // 03:00–03:04: the sweep bar
                bar1(s, 0, 3, mm, 20000, mm == 2 ? sweep_hi : 20000.5, 19999.5, mm == 4 ? sweep_c : 20000, sweep_buy_share);
            bar1(s, 0, 3, 5, 20000, 20000.5, 19999.5, 20000, flat_buy_share);     // closes the 5-minute bar
            bar1(s, 0, 3, 6, 20000, 20000.5, 19999.5, 20000, flat_buy_share);
            sd = s.take_stop_dist();
            return out;
        };
        double sd = 0;
        auto trapped = run_asia(0.8, 0.5, 20015, 20005, sd);
        CHECK(trapped.size() == 1 && trapped[0].sig == OrbSignal::SELL && trapped[0].why == "asia_sweep_short" && std::fabs(sd - 25.0) < 1e-9,
              "asia_sweep: buyers push above the Asia high and the bar closes back inside → SHORT, stop 0.25 × day ATR");
        auto sellers = run_asia(0.2, 0.5, 20015, 20005, sd);
        CHECK(sellers.empty(), "asia_sweep: the same poke with SELLING aggression is not a trapped-buyer sweep → no trade");
        auto held = run_asia(0.8, 0.5, 20015, 20012, sd);
        CHECK(held.empty(), "asia_sweep: a bar that closes above the Asia high (break, not sweep) → no trade");
        auto noflow = run_asia(0.0, 0.0, 20015, 20005, sd);
        CHECK(noflow.empty(), "asia_sweep: no aggressor data that day → no trade (fail closed)");

        // round_break: open 20030 → band 20000–20100; 5-minute closes 20090, then 20110 with buying → LONG
        auto run_round = [&](double buy_share, double c2) {
            TrendConfig tc; tc.mode = "round_break"; tc.tf_min = 5; tc.win_start = 930; tc.win_end = 1205;
            tc.lf_delta_min = 0.05; tc.lf_stop_atr = 0.25;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            for (int mm = 9 * 60 + 20; mm < 9 * 60 + 30; ++mm) bar1(s, 0, mm / 60, mm % 60, 20030, 20031, 20029, 20030, 0.5);
            for (int mm = 0; mm < 5; ++mm) bar1(s, 0, 9, 30 + mm, 20030, 20091, 20029, mm == 4 ? 20090 : 20060, 0.5);
            for (int mm = 0; mm < 5; ++mm) bar1(s, 0, 9, 35 + mm, 20090, c2 + 1, 20089, mm == 4 ? c2 : 20095, buy_share);
            bar1(s, 0, 9, 40, c2, c2 + 1, c2 - 1, c2, 0.5);
            bar1(s, 0, 9, 41, c2, c2 + 1, c2 - 1, c2, 0.5);
            return out;
        };
        auto up = run_round(0.8, 20110);
        CHECK(up.size() == 1 && up[0].sig == OrbSignal::BUY && up[0].why == "round_break_long", "round_break: first close above the 100-pt band with buying → LONG");
        CHECK(run_round(0.2, 20110).empty(), "round_break: the same close with SELLING aggression → no trade");
        CHECK(run_round(0.8, 20095).empty(), "round_break: a close still inside the band → no trade");

        // gap_or_retest (2026-10-17): yesterday closed 20000; today opens 20030 (gap +0.3 ATR at ATR 100 → long side);
        // opening range 09:30–09:44 = 20025–20040; the 09:45 bar dips to 20038 and closes 20045 → LONG, stop 0.35 × ATR
        auto run_gr = [&](double open, double bar_lo, double bar_c, double& sd, bool flow_agree = false, double rth_buy = 0.5) {
            TrendConfig tc; tc.mode = "gap_or_retest"; tc.tf_min = 5; tc.win_start = 930; tc.win_end = 1555;
            tc.lf_stop_atr = 0.35; tc.gr_flow_agree = flow_agree;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session();
            for (int mm = 9 * 60 + 30; mm < 16 * 60; ++mm) bar1(s, -1, mm / 60, mm % 60, 20000, 20001, 19999, 20000, 0.5);
            s.reset_session(); s.set_day_atr(100.0);                              // 18:00: yesterday's close is 20000
            for (int mm = 0; mm < 15; ++mm)                                       // opening range 09:30–09:44
                bar1(s, 0, 9, 30 + mm, open, mm == 7 ? open + 10 : open + 1, mm == 3 ? open - 5 : open - 1, open, rth_buy);
            for (int mm = 0; mm < 5; ++mm)                                        // the 09:45 bar
                bar1(s, 0, 9, 45 + mm, open + 11, open + 16, mm == 2 ? bar_lo : open + 11, mm == 4 ? bar_c : open + 12, 0.5);
            bar1(s, 0, 9, 50, bar_c, bar_c + 1, bar_c - 1, bar_c, 0.5);
            bar1(s, 0, 9, 51, bar_c, bar_c + 1, bar_c - 1, bar_c, 0.5);
            sd = s.take_stop_dist();
            return out;
        };
        auto gl = run_gr(20030, 20038, 20045, sd);
        CHECK(gl.size() == 1 && gl[0].sig == OrbSignal::BUY && gl[0].why == "gap_or_retest_long" && std::fabs(sd - 35.0) < 1e-9,
              "gap_or_retest: gap up, the 09:45 bar touches the opening-range high and closes above → LONG, stop 0.35 × ATR");
        CHECK(run_gr(20005, 20013, 20020, sd).empty(), "gap_or_retest: a gap of 0.05 ATR (< 0.1) sets no side → no trade");
        CHECK(run_gr(20030, 20042, 20045, sd).empty(), "gap_or_retest: a bar that never touches the opening-range high → no trade");
        CHECK(run_gr(19970, 19978, 19985, sd).empty(), "gap_or_retest: a gap DOWN never takes the long retest");
        // gr_flow_agree (2026-10-17): the same long needs buyers ahead in the RTH cumulative delta
        CHECK(run_gr(20030, 20038, 20045, sd, true, 0.2).empty(), "gap_or_retest + flow: sellers ahead (RTH delta < 0) → no long");
        auto gf = run_gr(20030, 20038, 20045, sd, true, 0.8);
        CHECK(gf.size() == 1 && gf[0].sig == OrbSignal::BUY, "gap_or_retest + flow: buyers ahead (RTH delta > 0) → LONG");

        // box_go (2026-10-17): overnight high 20050, yesterday's close 20000, open 20040 (gap +0.4 ATR at ATR 100 → long side)
        //   09:30 bar breaks the overnight high (o 20040 c 20060, range 24); 09:35 box stays above it (20052–20065, range 13 ≤ 0.75 × 24);
        //   09:40 bar closes 20068 above the box → LONG, stop = 20068 − 20052 + 0.25 = 16.25
        auto run_bg = [&](double open, double box_lo, double go_c, double& sd) {
            TrendConfig tc; tc.mode = "box_go"; tc.tf_min = 5; tc.win_start = 930; tc.win_end = 1555;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session();
            for (int mm = 9 * 60 + 30; mm < 16 * 60; ++mm) bar1(s, -1, mm / 60, mm % 60, 20000, 20001, 19999, 20000, 0.5);
            s.reset_session(); s.set_day_atr(100.0);
            for (int mm = 8 * 60; mm < 9 * 60 + 30; ++mm)                        // overnight: high 20050 at 08:30
                bar1(s, 0, mm / 60, mm % 60, open, mm == 8 * 60 + 30 ? 20050 : open + 1, open - 1, open, 0.5);
            for (int mm = 0; mm < 5; ++mm) bar1(s, 0, 9, 30 + mm, mm ? 20050 : open, 20062, 20038, mm == 4 ? 20060 : 20055, 0.5);   // break bar
            for (int mm = 0; mm < 5; ++mm) bar1(s, 0, 9, 35 + mm, 20060, 20065, mm == 2 ? box_lo : 20058, 20060, 0.5);               // box bar
            for (int mm = 0; mm < 5; ++mm) bar1(s, 0, 9, 40 + mm, 20060, go_c + 1, 20059, mm == 4 ? go_c : 20062, 0.5);              // go bar
            bar1(s, 0, 9, 45, go_c, go_c + 1, go_c - 1, go_c, 0.5);
            bar1(s, 0, 9, 46, go_c, go_c + 1, go_c - 1, go_c, 0.5);
            sd = s.take_stop_dist();
            return out;
        };
        auto bg = run_bg(20040, 20052, 20068, sd);
        CHECK(bg.size() == 1 && bg[0].sig == OrbSignal::BUY && bg[0].why == "box_go_long" && std::fabs(sd - 16.25) < 1e-9,
              "box_go: break of the overnight high, a small box that holds, a close above the box → LONG, stop below the box");
        CHECK(run_bg(20040, 20048, 20068, sd).empty(), "box_go: a box that dips back below the overnight high → no trade");
        CHECK(run_bg(20040, 20052, 20063, sd).empty(), "box_go: the go bar does not close above the box → no trade");
        CHECK(run_bg(19990, 20052, 20068, sd).empty(), "box_go: a gap DOWN never takes the long break");

        // mom_dir (2026-10-17): ATR 100, flat 20000 until 09:30, then a path from the 09:30 open; signals only at 5-minute closes
        //   from 09:45, when the last 30 one-minute moves went one way with efficiency ≥ 0.3 and price is on that side of VWAP and the open
        auto run_mom = [&](const std::vector<double>& rth, bool ema_entry, int notify_after, double notify_pnl, double& sd) {
            TrendConfig tc; tc.mode = "mom_dir"; tc.tf_min = 1; tc.win_start = 930; tc.win_end = 1555; tc.mom_entry_ema = ema_entry;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            for (int mm = 9 * 60; mm < 9 * 60 + 30; ++mm) bar1(s, 0, mm / 60, mm % 60, 20000, 20001, 19999, 20000, 0.5);
            double prev = 20000; sd = 0;
            for (size_t i = 0; i < rth.size(); ++i) { const int mm = 9 * 60 + 30 + (int)i; const double c = rth[i];
                bar1(s, 0, mm / 60, mm % 60, prev, std::max(prev, c) + 0.25, std::min(prev, c) - 0.25, c, 0.5); prev = c;
                if ((int)out.size() == notify_after && sd == 0) { sd = s.take_stop_dist(); s.notify_trade_filled(out.back().sig, "stop", notify_pnl); } }
            return out;
        };
        std::vector<double> rise; for (int i = 0; i < 40; ++i) rise.push_back(20000 + 1.5 * (i + 1));            // straight up 1.5 pts/min
        auto mr = run_mom(rise, false, 99, 0, sd); const double sd_first = [&] { double x; auto o = run_mom(rise, false, 1, 50, x); (void)o; return x; }();
        CHECK(!mr.empty() && mr[0].sig == OrbSignal::BUY && mr[0].why == "mom_dir_long" && std::fabs(sd_first - 25.0) < 1e-9,
              "mom_dir: a straight 30-minute rise above VWAP and the open → LONG at the first 5-minute close from 09:45, stop 0.25 ATR");
        std::vector<double> chop; for (int i = 0; i < 40; ++i) chop.push_back(20000 + ((i % 2) ? 6.0 : -6.0) + 0.1 * i);   // ±6 every minute
        CHECK(run_mom(chop, false, 99, 0, sd).empty(), "mom_dir: a choppy last 30 minutes (efficiency < 0.3) → no trade");
        std::vector<double> below; for (int i = 0; i < 40; ++i) below.push_back(i < 5 ? 20000 - 12.0 * (i + 1) : 19940 + 1.0 * (i - 4));
        CHECK(run_mom(below, false, 99, 0, sd).empty() || run_mom(below, false, 99, 0, sd)[0].sig != OrbSignal::BUY,
              "mom_dir: a straight rise that is still below the 09:30 open → no long");

        // path_dir (2026-10-18, research/preregistered/2026-10-18_path_direction_map.md): ATR 100; yesterday closed 20000; today
        // opens at `open` and follows `rth` minute closes from 09:30; one look at the 1-minute close ending 10:00
        auto run_pd = [&](const std::string& src, double open, const std::vector<double>& rth, double& sd) {
            TrendConfig tc; tc.mode = "path_dir"; tc.tf_min = 1; tc.win_start = 930; tc.win_end = 1555; tc.pd_source = src;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session();
            for (int mm = 9 * 60 + 30; mm < 16 * 60; ++mm) bar1(s, -1, mm / 60, mm % 60, 20000, 20000.25, 19999.75, 20000, 0.5);
            s.reset_session(); s.set_day_atr(100.0);
            for (int mm = 8 * 60 + 30; mm < 9 * 60 + 30; ++mm) bar1(s, 0, mm / 60, mm % 60, open, open + 0.25, open - 0.25, open, 0.5);
            double prev = open; sd = 0;
            for (size_t i = 0; i < rth.size(); ++i) { const int mm = 9 * 60 + 30 + (int)i; const double c = rth[i];
                bar1(s, 0, mm / 60, mm % 60, prev, std::max(prev, c) + 0.25, std::min(prev, c) - 0.25, c, 0.5); prev = c;
                if (!out.empty() && sd == 0) sd = s.take_stop_dist(); }
            return out;
        };
        std::vector<double> pd_up; for (int i = 0; i < 60; ++i) pd_up.push_back(20030 + 1.0 * (i + 1));                 // straight up 1 pt/min
        auto pd = run_pd("gap", 20030, pd_up, sd);
        CHECK(pd.size() == 1 && pd[0].sig == OrbSignal::BUY && pd[0].why == "path_dir_long" && std::fabs(pd[0].px - 20060) < 1e-9 && std::fabs(sd - 25.0) < 1e-9,
              "path_dir gap: +0.3 ATR gap, a straight path on the gap side → LONG once, at the 10:00 close, stop 0.25 ATR");
        std::vector<double> zz; for (int i = 0; i < 60; ++i) zz.push_back(20030 + ((i % 2) ? 20.0 : -20.0));   // wide chop: 10:00 close 20 above the open on a ~40-pt range (efficiency ~0.49)
        CHECK(run_pd("gap", 20030, zz, sd).empty(), "path_dir gap: a choppy path (efficiency < 0.55) → no trade");
        std::vector<double> dn; for (int i = 0; i < 60; ++i) dn.push_back(20030 - 1.0 * (i + 1));
        CHECK(run_pd("gap", 20030, dn, sd).empty(), "path_dir gap: price below the open against a gap up → no trade");
        std::vector<double> up_small; for (int i = 0; i < 60; ++i) up_small.push_back(20005 + 1.0 * (i + 1));
        CHECK(run_pd("gap", 20005, up_small, sd).empty(), "path_dir gap: a gap under 0.1 ATR sets no side → no trade");
        auto pm = run_pd("mom", 20005, up_small, sd);
        CHECK(pm.size() == 1 && pm[0].sig == OrbSignal::BUY && pm[0].why == "path_dir_long",
              "path_dir mom: no gap, but a straight 45-minute rise above VWAP and the open on a straight path → LONG once");
        auto lost = run_mom(rise, false, 1, -50.0, sd);
        CHECK(lost.size() == 1, "mom_dir: after a losing trade (notify_trade_filled with P&L < 0) no more entries that day");
        auto won = run_mom(rise, false, 1, 50.0, sd);
        CHECK(won.size() >= 2 && won[1].sig == OrbSignal::BUY, "mom_dir: after a winning trade the next 5-minute close in the direction may enter again");
        // mom_entry vwap / dip (2026-10-17): 2 pts/min straight up from 09:30, then the 09:45 bar dips to 20020 (below VWAP, ≥ 0.1 ATR off the
        // 30-min high) and closes 20095 (above VWAP and above the 09:44 close) → both enter; on the plain rise neither does (any-bar does)
        auto run_entry = [&](const std::string& entry, bool dip_bar, bool need_plan = false, double atr = 100.0) {
            TrendConfig tc; tc.mode = "mom_dir"; tc.tf_min = 1; tc.win_start = 930; tc.win_end = 1555; tc.mom_entry = entry; tc.mom_need_plan = need_plan;
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(atr);
            for (int mm = 9 * 60; mm < 9 * 60 + 30; ++mm) bar1(s, 0, mm / 60, mm % 60, 20000, 20001, 19999, 20000, 0.5);
            double prev = 20000;
            for (int i = 0; i < 15; ++i) { const double c = 20000 + 2.0 * (i + 1); bar1(s, 0, 9, 30 + i, prev, c + 0.25, prev - 0.25, c, 0.5); prev = c; }
            for (int i = 0; i < 5; ++i) { const int mm = 45 + i; const double c = dip_bar ? (i == 4 ? 20095.0 : 20031.0 + 12 * i) : 20030.0 + 2.0 * (i + 1);
                const double lo = dip_bar && i == 0 ? 20020.0 : std::min(prev, c) - 0.25; bar1(s, 0, 9, mm, prev, std::max(prev, c) + 0.25, lo, c, 0.5); prev = c; }
            bar1(s, 0, 9, 50, prev, prev + 0.25, prev - 0.25, prev, 0.5);
            return out;
        };
        CHECK(run_entry("vwap", true).size() == 1 && run_entry("vwap", true)[0].sig == OrbSignal::BUY, "mom_dir vwap: a 5-min bar that dips through VWAP and closes back above it → LONG");
        CHECK(run_entry("vwap", false).empty(), "mom_dir vwap: no pullback to VWAP → no trade");
        CHECK(run_entry("dip", true).size() == 1, "mom_dir dip: a ≥ 0.1 ATR pullback from the 30-min high that closes above the previous 5-min close → LONG");
        CHECK(run_entry("dip", false).empty(), "mom_dir dip: no pullback → no trade");
        CHECK(run_entry("any", false).size() == 1, "mom_dir any: the plain straight rise enters at the 09:45 close");
        // mom_need_plan (2026-10-17): the 09:50 cascade check (+95 ≥ 0.25 × ATR 100) sets the plan LONG in the same bar → the VWAP entry
        // is taken; with ATR 400 the move is < 0.25 ATR, no plan direction exists yet → no entry (without the flag it would enter)
        CHECK(run_entry("vwap", true, true, 100.0).size() == 1, "mom_dir + need_plan: plan direction LONG from the 09:50 check → the VWAP entry is taken");
        CHECK(run_entry("vwap", true, true, 400.0).empty(), "mom_dir + need_plan: no plan direction yet → no entry");
        CHECK(run_entry("vwap", true, false, 400.0).size() == 1, "mom_dir without need_plan: the same bar enters");
    }
    // ── late strong open (2026-10-15): re-check later only on days the main decision gave no direction ──
    {
        TrendConfig tc; tc.mode = "opening_drive"; tc.tf_min = 1; tc.drive_minutes = 4; tc.drive_min_atr = 0.2;
        tc.drive_day_atr = true; tc.drive_market = true; tc.drive_entry_from = 930; tc.drive_entry_to = 935;
        tc.drive_stop_atr = 0.25;
        tc.drive_late_count = 2; tc.drive_late_minutes = 8; tc.drive_late_step = 2; tc.drive_late_min_atr = 0.3;   // checks 09:38, 09:40 at ±30
        auto run = [&](bool late_only, const std::vector<double>& path, double& sd) {
            TrendConfig t = tc; t.drive_late_only = late_only;
            TrendStrategy s(t, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            feed_path(s, path, 9, 30);
            sd = s.take_stop_dist();
            return out;
        };
        double sd = 0.0;
        // quiet at 09:34 (+5 < 20), +25 at 09:38 (< 30), +35 at 09:40 → late BUY
        const std::vector<double> late_up = {20000, 20002, 20003, 20004, 20005, 20010, 20015, 20020, 20025, 20030, 20035, 20036, 20037, 20038};
        auto a = run(false, late_up, sd);
        CHECK(a.size() == 1 && a[0].sig == OrbSignal::BUY && a[0].why == "drive_late_long", "late SO: BUY at the first late check past 0.3 ATR");
        CHECK(std::fabs(sd - 25.0) < 1e-9, "late SO: stop distance = drive_stop_atr × day ATR");
        auto dn = run(false, {20000, 19998, 19997, 19996, 19995, 19990, 19980, 19972, 19965, 19960, 19955, 19950}, sd);
        CHECK(dn.size() == 1 && dn[0].sig == OrbSignal::SELL && dn[0].why == "drive_late_short", "late SO: SELL on a late down move (09:38 −35)");
        // main decision fires (+30 at 09:34) → no late trade even though the move keeps going
        auto main_day = run(false, {20000, 20010, 20020, 20025, 20030, 20040, 20050, 20060, 20070, 20080, 20090, 20100}, sd);
        CHECK(main_day.size() == 1 && main_day[0].why == "drive_market_long", "late SO: main-decision days take only the main trade");
        // late moves never reach 0.3 ATR → nothing
        auto quiet = run(false, {20000, 20002, 20003, 20004, 20005, 20010, 20012, 20015, 20020, 20022, 20025, 20026, 20080}, sd);
        CHECK(quiet.empty(), "late SO: no trade when no late check passes (moves after the last check ignored)");
        // drive_flow_agree (2026-10-17): the late BUY needs buyers ahead in the RTH cumulative delta
        {
            TrendConfig t = tc; t.drive_flow_agree = true;
            auto run_flow = [&](bool buyers) {
                TrendStrategy s(t, risk_cfg()); std::vector<Rec> out;
                s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
                s.reset_session(); s.set_day_atr(100.0);
                for (size_t i = 0; i < late_up.size(); ++i) {
                    const int total = 9 * 60 + 30 + (int)i;
                    for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(total / 60, total % 60, k * 10), late_up[i] + (k % 2 ? 0.25 : -0.25), 1, buyers ? k < 5 : k < 1});
                }
                return out;
            };
            auto fb = run_flow(true);
            CHECK(fb.size() == 1 && fb[0].sig == OrbSignal::BUY, "cascade + flow: buyers ahead → the late BUY is taken");
            CHECK(run_flow(false).empty(), "cascade + flow: sellers ahead while price rises → no BUY");
        }
        // late_only: the main decision is a skip test — never traded
        auto lo_main = run(true, {20000, 20010, 20020, 20025, 20030, 20040, 20050, 20060, 20070, 20080, 20090, 20100}, sd);
        CHECK(lo_main.empty(), "late_only: main-decision day → no trade at all");
        auto lo_late = run(true, late_up, sd);
        CHECK(lo_late.size() == 1 && lo_late[0].why == "drive_late_long", "late_only: quiet main → late trade");
        // off by default: the plain drive_market behaviour is unchanged on a quiet main day
        TrendConfig off = tc; off.drive_late_count = 0; TrendStrategy s0(off, risk_cfg()); int n0 = 0;
        s0.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n0; }); s0.reset_session(); s0.set_day_atr(100.0);
        feed_path(s0, late_up, 9, 30);
        CHECK(n0 == 0, "late SO off: quiet main day → no trade (unchanged)");
        // every-day cascade: drive_late_last_min_atr 0 → the LAST late check follows any move; earlier checks keep 0.3
        auto cascade = [&](const std::vector<double>& path, std::vector<Rec>& out) {
            TrendConfig t = tc; t.drive_late_last_min_atr = 0.0;
            TrendStrategy s(t, risk_cfg());
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            feed_path(s, path, 9, 30);
            return s.take_stop_dist();
        };
        const std::vector<double> quiet_up = {20000, 20002, 20003, 20004, 20005, 20010, 20012, 20015, 20020, 20022, 20025, 20026};
        std::vector<Rec> c1; const double csd = cascade(quiet_up, c1);
        CHECK(c1.size() == 1 && c1[0].sig == OrbSignal::BUY && c1[0].why == "drive_late_long" && std::fabs(c1[0].px - 20022.25) < 1e-9,
              "cascade: last check (09:40, +22.5 < 0.3 ATR) follows any move; the 09:38 check (+15.5) still needs 0.3 ATR");
        CHECK(std::fabs(csd - 25.0) < 1e-9, "cascade: last-check trade keeps drive_stop_atr × day ATR");
        std::vector<Rec> c2; cascade({20000, 19999, 19998, 19997, 19996, 19994, 19992, 19990, 19988, 19987, 19986, 19985}, c2);
        CHECK(c2.size() == 1 && c2[0].sig == OrbSignal::SELL, "cascade: small down move at the last check → SELL");
        std::vector<Rec> c3; cascade({20000, 20001, 19999, 20002, 20001, 20003, 20001, 20002, 20001, 19999.5, 20000, 20004}, c3);   // 09:40 check: close 19999.75 = open
        CHECK(c3.empty(), "cascade: price exactly at the open at the last check → no trade");
        std::vector<Rec> c4; cascade({20000, 20010, 20020, 20025, 20030, 20040, 20050, 20060, 20070, 20080, 20090, 20100}, c4);
        CHECK(c4.size() == 1 && c4[0].why == "drive_market_long", "cascade: main-decision days still take only the main trade");
        // cascade context filters (2026-10-16, paper only): drive_gap_agree / drive_prev_extreme. Day 1 sets the prior
        // RTH day, day 2 runs the cascade; checks at 09:34 (main), 09:38, 09:40 (last, any move).
        auto ctx_run = [&](double gap_agree, double prev_ext, const std::vector<double>& day1, const std::vector<double>& day2) {
            TrendConfig t = tc; t.drive_late_last_min_atr = 0.0; t.drive_gap_agree = gap_agree; t.drive_prev_extreme = prev_ext;
            TrendStrategy s(t, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            if (!day1.empty()) { feed_path(s, day1, 9, 30); s.reset_session(); }
            const int64_t day = 86400LL * 1'000'000LL;
            for (size_t i = 0; i < day2.size(); ++i) {
                const int tot = 9 * 60 + 30 + (int)i;
                for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(tot / 60, tot % 60, k * 10) + day, day2[i] + (k % 2 ? 0.25 : -0.25), 1, k % 2 == 0});
            }
            return out;
        };
        const std::vector<double> d2_up = {20000, 20010, 20020, 20025, 20030, 20040, 20050, 20060, 20070, 20080, 20090, 20100};
        const std::vector<double> d1_low_close = {19950, 20050, 19980};     // prior close 19980.25 → day 2 gaps up ≈ +19.5
        const std::vector<double> d1_high_close = {19950, 19980, 20050};    // prior close at its high (IBS 1.0) → gaps down
        auto g1 = ctx_run(0.1, 0.0, d1_low_close, d2_up);
        CHECK(g1.size() == 1 && g1[0].why == "drive_market_long", "gap_agree: gap up ≥ 0.1 ATR + up drive → main BUY");
        CHECK(ctx_run(0.1, 0.0, d1_high_close, d2_up).empty(), "gap_agree: gap down against an up drive → no main and no late trade");
        CHECK(ctx_run(0.1, 0.0, {}, d2_up).empty(), "gap_agree: no prior day → no trade (fails closed)");
        CHECK(ctx_run(0.0, 0.0, d1_high_close, d2_up).size() == 1, "gap_agree off: the same day trades (unchanged)");
        // late checks stay armed: 09:38 long (+40) is against the gap, the 09:40 last check (down) agrees → SELL
        auto g2 = ctx_run(0.1, 0.0, d1_high_close, {20000, 20002, 20003, 20004, 20005, 20010, 20015, 20040, 20030, 19980, 19975, 19970});
        CHECK(g2.size() == 1 && g2[0].sig == OrbSignal::SELL && g2[0].why == "drive_late_short",
              "gap_agree: a blocked late check keeps the later checks armed");
        CHECK(ctx_run(0.0, 0.7, d1_high_close, d2_up).empty(), "prev_extreme: yesterday closed at its high → no long");
        auto p1 = ctx_run(0.0, 0.7, d1_high_close, {20000, 19999, 19998, 19997, 19996, 19994, 19992, 19990, 19988, 19987, 19986, 19985});
        CHECK(p1.size() == 1 && p1[0].sig == OrbSignal::SELL, "prev_extreme: a short after a high close still trades");
        CHECK(ctx_run(0.0, 0.7, d1_low_close, d2_up).size() == 1, "prev_extreme: a long after a low close (IBS 0.3) trades");
        // skip_fomc_day (2026-10-16): no entry on an FOMC decision day; other event days and normal days trade
        auto fomc_run = [&](bool skip, bool event, bool fomc) {
            TrendConfig t = tc; t.drive_late_last_min_atr = 0.0; t.skip_fomc_day = skip;
            TrendStrategy s(t, risk_cfg()); int n = 0;
            s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; });
            s.reset_session(); s.set_day_atr(100.0); s.set_event_day(event, fomc);
            feed_path(s, quiet_up, 9, 30);
            return n;
        };
        CHECK(fomc_run(true, true, true) == 0, "skip_fomc_day: FOMC decision day → no entry");
        CHECK(fomc_run(true, true, false) == 1, "skip_fomc_day: a non-FOMC event day still trades");
        CHECK(fomc_run(true, false, false) == 1, "skip_fomc_day: a normal day trades");
        CHECK(fomc_run(false, true, true) == 1, "skip_fomc_day off: FOMC day trades (default unchanged)");
    }
    // ── vprofile (2026-09-29): yesterday's value area, fade the touch toward the POC ──
    {
        TrendConfig tc; tc.mode = "vprofile"; tc.tf_min = 1; tc.vp_style = "fade"; tc.vp_tol_atr = 0.5; tc.vp_va_frac = 0.7;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        // day 1: a bell of volume over 19995..20005 (peak at 20000), light tails at 20030 / 19970
        auto bar = [&](int h, int m, double px, int64_t vol, int64_t day = 0) {
            for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(h, m, k * 15) + day, px + (k % 2 ? 0.25 : -0.25), vol / 4, k % 2 == 0});
        };
        for (int m = 0; m < 20; ++m) { const int off = (m % 11) - 5; bar(9, 30 + m, 20000.0 + off, 400 + (5 - std::abs(off)) * 200); }
        for (int m = 0; m < 5; ++m)  bar(9, 50 + m, 20030.0, 20);
        for (int m = 0; m < 5; ++m)  bar(9, 55 + m, 19970.0, 20);
        s.on_tick(OrbTick{at(16, 1), 20000.0, 1, true});                 // session end → profile finalised
        CHECK(s.vp_ready() && std::fabs(s.vp_poc() - 20000.0) < 1.6, "vprofile: POC at the heavy price");   // 1-pt bins: 19999.5 or 20000.5
        CHECK(s.vp_vah() > 20001.0 && s.vp_vah() < 20030.0 && s.vp_val() < 19999.0 && s.vp_val() > 19970.0, "vprofile: value area spans the bell and excludes the light tails");
        const double vah = s.vp_vah(), poc = s.vp_poc();
        s.reset_session();                                              // 18:00 rollover keeps the profile
        const int64_t day2 = 86400LL * 1'000'000LL;
        bar(9, 30, 20000.0, 100, day2); bar(9, 31, 20000.0, 100, day2);   // warm the ATR
        for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(9, 32, k * 15) + day2, k < 2 ? vah + 1.0 : vah - 2.0, 25, true});   // touches VAH, closes below
        bar(9, 33, vah - 2.0, 100, day2);                                // closes the 09:32 bar → SELL
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::SELL && out[0].why == "vp_fade_vah", "vprofile: fade the VAH touch");
        for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(9, 34, k * 15) + day2, poc - 1.0, 25, true});   // reaches the POC
        bar(9, 35, poc - 1.0, 100, day2);
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "vp_target", "vprofile: flattens at the POC");
    }
    // ── hold (beta, 2026-09-29): in at 15:55, survives the 18:00 reset, out at 09:30 next day ──
    {
        TrendConfig tc; tc.mode = "hold"; tc.tf_min = 1; tc.hold_entry_hhmm = 1555; tc.win_start = 1555; tc.win_end = 930;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        feed_path(s, std::vector<double>(3, 20000.0), 15, 53);            // 15:53–15:55 → the 15:55 bar closes at 15:56
        feed_path(s, std::vector<double>(2, 20001.0), 15, 56);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "hold_long", "hold: one BUY at the entry time");
        s.check_eod(20, 0);                                                // overnight: not the window end
        CHECK(out.size() == 1, "hold: no flatten during the night");
        s.reset_session();                                                 // 18:00 rollover keeps the position
        CHECK(s.session().in_position, "hold: position survives the session reset");
        s.check_eod(9, 30);
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "session_end", "hold: flattened at the window end next morning");
        // filters: turn-of-month blocks mid-month; pre-event needs the host flag; up-day needs the session up
        TrendConfig tm = tc; tm.hold_tom = true; TrendStrategy s2(tm, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; }); s2.reset_session();
        feed_path(s2, std::vector<double>(5, 20000.0), 15, 53);          // 2025-04-30 in the test clock: day 30 → allowed
        CHECK(n2 == 1, "hold_tom: enters on the 30th");
        for (int dw : {3, 1}) {                                            // 2025-04-30 is a Wednesday (3)
            TrendConfig tw = tc; tw.hold_dow = dw; TrendStrategy sw(tw, risk_cfg()); int nw = 0;
            sw.set_signal_callback([&](OrbSignal, double, const std::string&) { ++nw; }); sw.reset_session();
            feed_path(sw, std::vector<double>(5, 20000.0), 15, 53);
            CHECK(nw == (dw == 3 ? 1 : 0), dw == 3 ? "hold_dow 3: enters on a Wednesday" : "hold_dow 1: silent on a Wednesday");
        }
        TrendConfig pe = tc; pe.hold_pre_event = true; TrendStrategy s3(pe, risk_cfg()); int n3 = 0;
        s3.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n3; }); s3.reset_session(); s3.set_event_next_day(false);
        feed_path(s3, std::vector<double>(5, 20000.0), 15, 53);
        CHECK(n3 == 0, "hold_pre_event: silent when tomorrow is not an event day");
        s3.reset_session(); s3.set_event_next_day(true);
        feed_path(s3, std::vector<double>(5, 20000.0), 15, 53);
        CHECK(n3 == 1, "hold_pre_event: enters the day before an event");
    }
    // ── hold_trend_fade + noise_momentum (2026-10-15, book research BK1) ──
    {
        constexpr int64_t DAY = 86400LL * 1'000'000LL;
        auto day_path = [](TrendStrategy& s, int64_t off, double open, double close) {   // 09:30–16:00, open → close
            for (int m = 0; m <= 390; ++m) {
                const int tm = 9 * 60 + 30 + m; const double p = open + (close - open) * m / 390.0;
                for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, p, 1, k % 2 == 0});
            }
        };
        auto path_at = [](TrendStrategy& s, int64_t off, const std::vector<double>& per_min) {    // from 09:30, one price per minute
            for (size_t m = 0; m < per_min.size(); ++m) {
                const int tm = 9 * 60 + 30 + (int)m;
                for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, per_min[m], 1, k % 2 == 0});
            }
        };
        TrendConfig tc; tc.mode = "hold"; tc.tf_min = 1; tc.hold_entry_hhmm = 930; tc.win_start = 930; tc.win_end = 1555;
        tc.hold_trend_fade = true;
        auto fade_run = [&](double open, double close) {
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0);
            day_path(s, 0, open, close); s.check_eod(15, 55); s.reset_session(); out.clear();
            path_at(s, DAY, std::vector<double>(6, close));
            return out;
        };
        auto up = fade_run(20000.0, 20200.0);                  // range 2 ATR, body 2 ATR, closes at the high
        CHECK(up.size() == 1 && up[0].sig == OrbSignal::SELL && up[0].why == "trend_fade_short", "hold_trend_fade: sells the open after an up trend day");
        auto dn = fade_run(20000.0, 19800.0);
        CHECK(dn.size() == 1 && dn[0].sig == OrbSignal::BUY && dn[0].why == "trend_fade_long", "hold_trend_fade: buys the open after a down trend day");
        auto small = fade_run(20000.0, 20120.0);               // 1.2 ATR range < 1.8
        CHECK(small.empty(), "hold_trend_fade: a 1.2 ATR day is not a trend day");
        TrendStrategy cold(tc, risk_cfg()); int nc = 0;
        cold.set_signal_callback([&](OrbSignal, double, const std::string&) { ++nc; });
        cold.reset_session(); cold.set_day_atr(100.0); path_at(cold, 0, std::vector<double>(6, 20000.0));
        CHECK(nc == 0 && cold.prev_trend_day_dir() == 0, "hold_trend_fade: no prior session → no entry");

        TrendConfig nm; nm.mode = "noise_momentum"; nm.tf_min = 1; nm.nm_days = 3; nm.nm_check_min = 30;
        nm.win_start = 930; nm.win_end = 1555;
        TrendStrategy s(nm, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); s.set_day_atr(100.0);
        std::vector<double> burst(31, 20010.0); for (int m = 5; m < 31; ++m) burst[m] = 20100.0;   // +90 by 09:35, held
        int64_t off = 0;
        for (int d = 0; d < 3; ++d, off += DAY) {               // quiet days: |close/open − 1| ≤ 0.0005
            day_path(s, off, 20000.0, 20010.0); s.check_eod(15, 55); s.reset_session(); out.clear();
        }
        {   // only two sessions of history → the same burst stays silent
            TrendStrategy y(nm, risk_cfg()); int ny = 0;
            y.set_signal_callback([&](OrbSignal, double, const std::string&) { ++ny; });
            y.reset_session(); y.set_day_atr(100.0);
            for (int d = 0; d < 2; ++d) { day_path(y, d * DAY, 20000.0, 20010.0); y.check_eod(15, 55); y.reset_session(); }
            path_at(y, 2 * DAY, burst);
            CHECK(ny == 0 && y.nm_history_days() == 2, "noise_momentum: silent while history < nm_days");
        }
        CHECK(s.nm_history_days() == 3, "noise_momentum: three sessions of history stored");
        CHECK(s.nm_sigma(29) > 0.0 && s.nm_sigma(29) < 0.001, "noise_momentum: sigma at 10:00 is the quiet days' move");
        std::vector<double> today = burst;                     // 10:00 check closes at 20100 > upper band ≈ 20011
        for (int m = 31; m < 61; ++m) today.push_back(19950.0); // back under the open → the 10:30 check exits
        today.push_back(19950.0);
        path_at(s, off, today);
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY && out[0].why == "noise_long", "noise_momentum: long when the 10:00 close is above the upper band");
        CHECK(out.size() >= 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "noise_exit_long", "noise_momentum: exits at the 10:30 check below max(band, VWAP)");
    }
    // ── hold fear-day filters + dip entry (2026-10-11): prior RTH body / range in prior-day ATR units ──
    {
        constexpr int64_t DAY = 86400LL * 1'000'000LL;
        auto day_path = [](TrendStrategy& s, int64_t off, double open, double close) {   // 09:30–16:00, open → close
            for (int m = 0; m <= 390; ++m) {
                const int tm = 9 * 60 + 30 + m; const double p = open + (close - open) * m / 390.0;
                for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, p, 1, k % 2 == 0});
            }
        };
        TrendConfig tc; tc.mode = "hold"; tc.tf_min = 1; tc.hold_entry_hhmm = 930; tc.win_start = 930; tc.win_end = 1555;
        tc.hold_prev_body_max = -1.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); s.set_day_atr(100.0);
        feed_path(s, std::vector<double>(6, 20000.0), 9, 30);
        CHECK(out.empty(), "hold_prev_body_max: no prior day yet → no entry");
        s.check_eod(15, 55); s.reset_session(); out.clear();
        day_path(s, 0, 20000.0, 19950.0); s.check_eod(15, 55); s.reset_session(); out.clear();          // −0.5 ATR day
        feed_path(s, std::vector<double>(6, 19950.0), 9, 30);
        CHECK(out.empty(), "hold_prev_body_max: a −0.5 ATR prior day is not a fear day");
        s.check_eod(15, 55); s.reset_session(); out.clear();
        TrendStrategy s2(tc, risk_cfg()); std::vector<Rec> o2;
        s2.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { o2.push_back({g, p, w}); });
        s2.reset_session(); s2.set_day_atr(100.0);
        day_path(s2, 0, 20000.0, 19880.0); s2.check_eod(15, 55); s2.reset_session(); o2.clear();       // −1.2 ATR day
        feed_path(s2, std::vector<double>(6, 19880.0), 9, 30);
        CHECK(o2.size() == 1 && o2[0].sig == OrbSignal::BUY && o2[0].why == "hold_long", "hold_prev_body_max: buys the open after a −1.2 ATR day");
        // range filter: a 1.2 ATR down day fails range ≥ 1.5
        TrendConfig tr = tc; tr.hold_prev_body_max = 0.0; tr.hold_prev_range_min = 1.5; tr.hold_prev_down = true;
        TrendStrategy s3(tr, risk_cfg()); int n3 = 0;
        s3.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n3; });
        s3.reset_session(); s3.set_day_atr(100.0);
        day_path(s3, 0, 20000.0, 19880.0); s3.check_eod(15, 55); s3.reset_session();
        feed_path(s3, std::vector<double>(6, 19880.0), 9, 30);
        CHECK(n3 == 0, "hold_prev_range_min: range 1.2 ATR < 1.5 → no entry");
        // dip entry: after a fear day, waits for open − 0.5 ATR, then buys once
        TrendConfig td = tc; td.hold_dip_atr = 0.5; td.hold_dip_until = 1200;
        TrendStrategy s4(td, risk_cfg()); std::vector<Rec> o4;
        s4.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { o4.push_back({g, p, w}); });
        s4.reset_session(); s4.set_day_atr(100.0);
        day_path(s4, 0, 20000.0, 19880.0); s4.check_eod(15, 55); s4.reset_session(); o4.clear();
        feed_path(s4, {19880, 19870, 19860, 19850, 19845}, 9, 30);           // −35 from the open: not yet
        CHECK(o4.empty(), "hold_dip_atr: no entry before the dip level");
        feed_path(s4, {19828, 19820, 19825, 19815}, 9, 35);                  // −52 → buy on that bar's close
        CHECK(o4.size() == 1 && o4[0].sig == OrbSignal::BUY && o4[0].why == "hold_dip_long", "hold_dip_atr: buys once at open − 0.5 ATR");
        // hold_combo3: one unit per daily signal on (weak close & VIX ≥ 20, body ≤ −1 ATR, range ≥ 1.5 ATR & down)
        auto combo = [&](double open, double close, double vix, bool vix_known, std::vector<Rec>& o, bool stack = true) {
            TrendConfig c3; c3.mode = "hold"; c3.tf_min = 1; c3.hold_entry_hhmm = 930; c3.win_start = 930; c3.win_end = 1555;
            c3.hold_combo3 = true; c3.combo_stack = stack;
            auto s = std::make_unique<TrendStrategy>(c3, risk_cfg());
            s->set_signal_callback([&](OrbSignal g, double p, const std::string& w) { o.push_back({g, p, w}); });
            s->reset_session(); s->set_day_atr(100.0);
            day_path(*s, 0, open, close); s->check_eod(15, 55); s->reset_session(); o.clear();   // linear day: IBS 0 when down
            s->set_vix(vix, 0.0, 0.0, vix_known);
            feed_path(*s, std::vector<double>(6, close), 9, 30);
            return s->last_units();
        };
        { std::vector<Rec> o; const int u = combo(20000.0, 19880.0, 0.0, false, o);
          CHECK(o.size() == 1 && o[0].why == "hold_combo3" && u == 1, "hold_combo3: −1.2 ATR day, no VIX → 1 unit (big down)"); }
        { std::vector<Rec> o; const int u = combo(20000.0, 19880.0, 25.0, true, o);
          CHECK(o.size() == 1 && u == 2, "hold_combo3: −1.2 ATR day closing on its low, VIX 25 → 2 units"); }
        { std::vector<Rec> o; combo(20000.0, 19950.0, 15.0, true, o);
          CHECK(o.empty(), "hold_combo3: −0.5 ATR day, VIX 15 → no signal, no entry"); }
        { std::vector<Rec> o; const int u = combo(20000.0, 19880.0, 25.0, true, o, false);
          CHECK(o.size() == 1 && u == 0, "hold_combo3 combo_stack=false: 2 signals → entry at the config qty (units 0)"); }
        { std::vector<Rec> o; combo(20000.0, 19950.0, 15.0, true, o, false);
          CHECK(o.empty(), "hold_combo3 combo_stack=false: no signal → still no entry"); }
        // CME holiday / early close (calendar): a 15:55 flatten must not open on a day the market shuts earlier
        CHECK(market_close_et_min("holiday", "") == 0, "calendar: holiday → closed all day");
        CHECK(market_close_et_min("early_close", "12:15") == 13 * 60 + 15, "calendar: early close 12:15 CT → 13:15 ET");
        CHECK(market_close_et_min("early_close", "") == 0, "calendar: early close without a time → treated as closed");
        CHECK(market_close_et_min("fomc", "") == -1, "calendar: other kinds → normal day");
        CHECK(market_closes_before_flatten(13 * 60 + 15, 15 * 60 + 55), "calendar: 13:15 close blocks a 15:55 flatten");
        CHECK(market_closes_before_flatten(0, 15 * 60 + 55), "calendar: holiday blocks entries");
        CHECK(!market_closes_before_flatten(13 * 60 + 15, 10 * 60 + 30), "calendar: 13:15 close does not block a 10:30 flatten");
        CHECK(!market_closes_before_flatten(-1, 15 * 60 + 55), "calendar: normal day does not block");
        { std::vector<Rec> o; const int u = combo(20000.0, 19950.0, 22.0, true, o);
          CHECK(o.size() == 1 && u == 1, "hold_combo3: weak close with VIX 22 → 1 unit"); }
        { std::vector<Rec> o; combo(20000.0, 20050.0, 30.0, true, o);
          CHECK(o.empty(), "hold_combo3: up day closing on its high → no entry even with VIX 30"); }
        (void)DAY;
    }
    // ── hold daily-context filters + multi-day exits (2026-10-11 sweep): IBS, streak, RSI(2), Donchian, hold_days ──
    {
        constexpr int64_t DAY = 86400LL * 1'000'000LL;
        // one RTH session open → close with the given high/low touched mid-day, then the 15:55 check + the 18:00 reset
        auto session = [](TrendStrategy& s, int64_t off, double open, double hi, double lo, double close) {
            auto tick = [&](int tm, double p) { for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, p, 1, k % 2 == 0}); };
            for (int m = 0; m <= 390; ++m) {
                const int tm = 9 * 60 + 30 + m;
                double p = open + (close - open) * m / 390.0;
                if (m == 100) p = hi; else if (m == 200) p = lo;
                tick(tm, p);
            }
        };
        auto make = [](TrendConfig tc, std::vector<Rec>& out) {
            auto s = std::make_unique<TrendStrategy>(tc, risk_cfg());
            s->set_signal_callback([&out](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s->reset_session(); s->set_day_atr(100.0); return s;
        };
        TrendConfig base; base.mode = "hold"; base.tf_min = 1; base.hold_entry_hhmm = 930; base.win_start = 930; base.win_end = 1555;
        // 1. hold_prev_ibs_max: a close in the bottom 20 % of the range enters; a mid-range close does not
        {
            TrendConfig tc = base; tc.hold_prev_ibs_max = 0.2; std::vector<Rec> out; auto s = make(tc, out);
            session(*s, 0, 20000, 20100, 19900, 19920); s->check_eod(15, 55); s->reset_session(); out.clear();   // ibs 0.10
            feed_path(*s, std::vector<double>(6, 19920.0), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_prev_ibs_max: buys after a close at 10 % of the range");
            s->check_eod(15, 55); s->reset_session(); out.clear();
            session(*s, DAY, 19920, 20100, 19900, 20000); s->check_eod(15, 55); s->reset_session(); out.clear(); // ibs 0.50
            feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.empty(), "hold_prev_ibs_max: a mid-range close does not enter");
        }
        // 2. hold_down_streak: two lower closes in a row enter; an up close breaks the streak
        {
            TrendConfig tc = base; tc.hold_down_streak = 2; std::vector<Rec> out; auto s = make(tc, out);
            session(*s, 0, 20000, 20050, 19950, 20000); s->check_eod(15, 55); s->reset_session();
            session(*s, DAY, 20000, 20050, 19900, 19950); s->check_eod(15, 55); s->reset_session(); out.clear();   // 1 lower close
            feed_path(*s, std::vector<double>(6, 19950.0), 9, 30);
            CHECK(out.empty(), "hold_down_streak: one lower close is not a streak of two");
            std::vector<Rec> o2; auto s2 = make(tc, o2);                                                       // a fresh instance: 2 lower closes
            session(*s2, 0, 20000, 20050, 19950, 20000); s2->check_eod(15, 55); s2->reset_session();
            session(*s2, DAY, 20000, 20050, 19900, 19950); s2->check_eod(15, 55); s2->reset_session();
            session(*s2, 2 * DAY, 19950, 20000, 19850, 19900); s2->check_eod(15, 55); s2->reset_session(); o2.clear();
            feed_path(*s2, std::vector<double>(6, 19900.0), 9, 30);
            CHECK(o2.size() == 1 && o2[0].sig == OrbSignal::BUY, "hold_down_streak: buys after two consecutive lower closes");
        }
        // 3. hold_rsi2_max: three down closes → RSI(2) = 0 → entry; an up close lifts it above 20
        {
            TrendConfig tc = base; tc.hold_rsi2_max = 20.0; std::vector<Rec> out; auto s = make(tc, out);
            double c = 20000; for (int d = 0; d < 6; ++d) { session(*s, d * DAY, c, c + 20, c - 60, c - 50); s->check_eod(15, 55); s->reset_session(); c -= 50; }
            out.clear(); feed_path(*s, std::vector<double>(6, c), 9, 30);
            CHECK(out.empty(), "hold_rsi2_max: fewer than 10 closes of history never enters");
            s->check_eod(15, 55); s->reset_session();                                            // (that session counts as a close too)
            for (int d = 7; d < 10; ++d) { session(*s, d * DAY, c, c + 20, c - 60, c - 50); s->check_eod(15, 55); s->reset_session(); c -= 50; }
            out.clear(); feed_path(*s, std::vector<double>(6, c), 9, 30);                        // the 10th close is in: RSI(2) = 0
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_rsi2_max: buys when RSI(2) of the closes is 0");
            s->check_eod(15, 55); s->notify_trade_filled(OrbSignal::FLATTEN_EOD, "session_end"); s->reset_session();
            session(*s, 11 * DAY, c, c + 120, c - 10, c + 100); s->check_eod(15, 55); s->reset_session(); out.clear();   // big up close
            feed_path(*s, std::vector<double>(6, c + 100), 9, 30);
            CHECK(out.empty(), "hold_rsi2_max: an up close lifts RSI(2) above the cap");
        }
        // 4. hold_donchian: a close above the highest of the previous 3 closes enters
        {
            TrendConfig tc = base; tc.hold_donchian = 3; std::vector<Rec> out; auto s = make(tc, out);
            const double cl[4] = {20000, 20050, 20020, 20100};
            for (int d = 0; d < 3; ++d) { session(*s, d * DAY, cl[d], cl[d] + 30, cl[d] - 30, cl[d]); s->check_eod(15, 55); s->reset_session(); }
            out.clear(); feed_path(*s, std::vector<double>(6, 20020.0), 9, 30);
            CHECK(out.empty(), "hold_donchian: 20020 is not above the prior 3 closes");
            s->check_eod(15, 55); s->reset_session();
            session(*s, 3 * DAY, 20020, 20120, 20000, cl[3]); s->check_eod(15, 55); s->reset_session(); out.clear();
            feed_path(*s, std::vector<double>(6, cl[3]), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_donchian: buys after a close above the prior 3 closes");
        }
        // 5. hold_days = 2: the first window end keeps the position, the second flattens it
        {
            TrendConfig tc = base; tc.hold_days = 2; std::vector<Rec> out; auto s = make(tc, out);
            feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_days: enters at the clock time");
            out.clear(); s->check_eod(15, 55); s->check_eod(16, 30);
            CHECK(out.empty(), "hold_days=2: no FLATTEN_EOD at the first window end");
            s->reset_session();
            CHECK(s->session().in_position, "hold_days=2: the position survives the session reset");
            session(*s, DAY, 20000, 20050, 19950, 20010); s->check_eod(15, 55);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::FLATTEN_EOD, "hold_days=2: FLATTEN_EOD at the second window end");
        }
        // 6. hold_exit_ibs_min: a strong close ends the hold early; a weak one runs to hold_days
        {
            TrendConfig tc = base; tc.hold_days = 5; tc.hold_exit_ibs_min = 0.6; std::vector<Rec> out; auto s = make(tc, out);
            session(*s, 0, 20000, 20100, 19900, 19920); s->check_eod(15, 55);                  // entry at 09:30, weak close (ibs 0.10)
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_exit_ibs_min: weak session keeps the hold");
            s->reset_session(); out.clear();
            session(*s, DAY, 19920, 20100, 19900, 20080); s->check_eod(15, 55);                // ibs 0.90
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::FLATTEN_EOD, "hold_exit_ibs_min: a strong close flattens at that window end");
        }
        // 7. hold_exit_up_close: a close above the prior session's close ends the hold
        {
            TrendConfig tc = base; tc.hold_days = 5; tc.hold_exit_up_close = true; std::vector<Rec> out; auto s = make(tc, out);
            session(*s, 0, 20000, 20050, 19950, 20000); s->check_eod(15, 55); s->reset_session(); out.clear();   // prior close 20000
            session(*s, DAY, 20000, 20020, 19900, 19950); s->check_eod(15, 55);                 // entry 09:30; close 19950 < 20000 → keep
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_exit_up_close: a lower close keeps the hold");
            s->reset_session(); out.clear();
            session(*s, 2 * DAY, 19950, 20100, 19940, 20060); s->check_eod(15, 55);            // 20060 > 19950 → exit
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::FLATTEN_EOD, "hold_exit_up_close: an up close flattens");
        }
        (void)DAY;
    }
    // ── hold VIX gates + hold_weak_any (2026-10-11): regime from set_vix, weakness filters under OR ──
    {
        constexpr int64_t DAY = 86400LL * 1'000'000LL;
        auto session = [](TrendStrategy& s, int64_t off, double open, double hi, double lo, double close) {
            auto tick = [&](int tm, double p) { for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, p, 1, k % 2 == 0}); };
            for (int m = 0; m <= 390; ++m) { const int tm = 9 * 60 + 30 + m; double p = open + (close - open) * m / 390.0; if (m == 100) p = hi; else if (m == 200) p = lo; tick(tm, p); }
        };
        auto make = [](TrendConfig tc, std::vector<Rec>& out) {
            auto s = std::make_unique<TrendStrategy>(tc, risk_cfg());
            s->set_signal_callback([&out](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s->reset_session(); s->set_day_atr(100.0); return s;
        };
        TrendConfig base; base.mode = "hold"; base.tf_min = 1; base.hold_entry_hhmm = 930; base.win_start = 930; base.win_end = 1555;
        // 1. VIX gate: no regime → no entry; VIX 15 → no entry; VIX 25 → entry
        {
            TrendConfig tc = base; tc.hold_vix_min = 20.0; std::vector<Rec> out; auto s = make(tc, out);
            feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.empty(), "hold_vix_min: unknown regime never enters");
            s->check_eod(15, 55); s->reset_session(); s->set_vix(15.0, 0.0, 0.3); out.clear();
            session(*s, DAY, 20000, 20050, 19950, 20000); s->check_eod(15, 55); s->reset_session(); out.clear();
            s->set_vix(15.0, 0.0, 0.3); feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.empty(), "hold_vix_min 20: VIX 15 blocks the entry");
            s->check_eod(15, 55); s->reset_session(); out.clear(); s->set_vix(25.0, 0.0, 0.8);
            feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_vix_min 20: VIX 25 enters");
        }
        // 2. hold_vix_any: level 15 fails, but a +12 % rise passes under OR; under AND it does not
        {
            TrendConfig tc = base; tc.hold_vix_min = 20.0; tc.hold_vix_rise_min = 0.10; tc.hold_vix_any = true; std::vector<Rec> out; auto s = make(tc, out);
            s->set_vix(15.0, 0.12, 0.3); feed_path(*s, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(out.size() == 1, "hold_vix_any: a rising VIX passes even below the level");
            TrendConfig ta = tc; ta.hold_vix_any = false; std::vector<Rec> o2; auto s2 = make(ta, o2);
            s2->set_vix(15.0, 0.12, 0.3); feed_path(*s2, std::vector<double>(6, 20000.0), 9, 30);
            CHECK(o2.empty(), "VIX gates under AND: level 15 blocks");
        }
        // 3. hold_weak_any: IBS fails (mid-range close) but RSI(2) is 0 after ten lower closes → enters; under AND it does not
        {
            TrendConfig tc = base; tc.hold_prev_ibs_max = 0.2; tc.hold_rsi2_max = 20.0; tc.hold_weak_any = true; std::vector<Rec> out; auto s = make(tc, out);
            double c = 20000; for (int d = 0; d < 10; ++d) { session(*s, d * DAY, c, c + 60, c - 100, c - 50); s->check_eod(15, 55); s->reset_session(); c -= 50; }   // closes at 31 % of the range (not weak by IBS), each lower
            out.clear(); feed_path(*s, std::vector<double>(6, c), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_weak_any: RSI(2)=0 enters although IBS is mid-range");
            TrendConfig ta = tc; ta.hold_weak_any = false; std::vector<Rec> o2; auto s2 = make(ta, o2);
            c = 20000; for (int d = 0; d < 10; ++d) { session(*s2, d * DAY, c, c + 60, c - 100, c - 50); s2->check_eod(15, 55); s2->reset_session(); c -= 50; }
            o2.clear(); feed_path(*s2, std::vector<double>(6, c), 9, 30);
            CHECK(o2.empty(), "weakness filters under AND: the mid-range IBS blocks");
        }
        // 4. hold_weak_any with the fear-body filter: a −1.2 ATR day enters even with IBS mid-range
        {
            TrendConfig tc = base; tc.hold_prev_ibs_max = 0.2; tc.hold_prev_body_max = -1.0; tc.hold_weak_any = true; std::vector<Rec> out; auto s = make(tc, out);
            session(*s, 0, 20000, 20010, 19800, 19880); s->check_eod(15, 55); s->reset_session(); out.clear();   // body −1.2 ATR, ibs 0.38
            feed_path(*s, std::vector<double>(6, 19880.0), 9, 30);
            CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY, "hold_weak_any: a fear day enters through the OR");
        }
        (void)DAY;
    }
    // ── generic VIX entry gate (2026-10-11): any mode, through can_enter ──
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 20; tc.vix_min = 20.0;
        TrendStrategy s(tc, risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; });
        s.reset_session();
        std::vector<double> path(26, 20000.0); for (int i = 22; i < 26; ++i) path[i] = 20010.0;   // the donchian test's own breakout path
        feed_path(s, path, 9, 30);
        CHECK(n == 0, "vix_min on donchian: no regime → the breakout is not taken");
        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; });
        s2.reset_session(); s2.set_vix(24.0, 0.05, 0.7); feed_path(s2, path, 9, 30);
        CHECK(n2 >= 1, "vix_min on donchian: VIX 24 lets the breakout through");
    }
    // ── conviction stack (2026-10-11): units from the prior close and the VIX fear state ──
    {
        auto session = [](TrendStrategy& s, int64_t off, double open, double hi, double lo, double close) {
            auto tick = [&](int tm, double p) { for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(tm / 60, tm % 60, k * 15) + off, p, 1, k % 2 == 0}); };
            for (int m = 0; m <= 390; ++m) { const int tm = 9 * 60 + 30 + m; double p = open + (close - open) * m / 390.0; if (m == 100) p = hi; else if (m == 200) p = lo; tick(tm, p); }
        };
        TrendConfig tc; tc.mode = "hold"; tc.tf_min = 1; tc.hold_entry_hhmm = 930; tc.win_start = 930; tc.win_end = 1555;
        tc.hold_prev_ibs_max = 0.5; tc.stack_wc_units = 1; tc.stack_vix_units = 1; tc.stack_both_units = 1; tc.stack_wc_ibs = 0.3; tc.stack_vix_min = 18.0;
        auto run = [&](double close_ibs_lo, double vix) {
            TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
            s.reset_session(); s.set_day_atr(100.0); s.set_vix(vix, 0.0, 0.5);
            session(s, 0, 20000, 20100, 19900, close_ibs_lo); s.check_eod(15, 55); s.reset_session(); s.set_vix(vix, 0.0, 0.5); out.clear();
            feed_path(s, std::vector<double>(6, close_ibs_lo), 9, 30);
            return out.empty() ? -1 : s.last_units();
        };
        CHECK(run(19920.0, 25.0) == 4, "stack: weak close (ibs 0.10) + VIX 25 → 4 units");
        CHECK(run(19920.0, 14.0) == 2, "stack: weak close, calm VIX → 2 units");
        CHECK(run(19980.0, 25.0) == 2, "stack: mid close (ibs 0.40) + VIX 25 → 2 units");
        CHECK(run(19980.0, 14.0) == 1, "stack: mid close, calm → 1 unit");
        TrendConfig t0 = tc; t0.stack_wc_units = t0.stack_vix_units = t0.stack_both_units = 0;
        TrendStrategy s0(t0, risk_cfg()); s0.reset_session();
        CHECK(s0.last_units() == 0, "stack off → 0 (executor uses its config qty)");
    }
    // ── orb_retest (2026-09-28): break, then the pullback to the edge, enter on resumption ──
    {
        TrendConfig tc; tc.mode = "orb_retest"; tc.tf_min = 1; tc.ort_minutes = 5; tc.ort_tol_atr = 4.0; tc.ort_max_bars = 30;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        // 09:30–09:34 range at 20000 (±0.25), 09:35 closes 10 pts above (break — no entry),
        // 09:36 dips back near the edge and closes above it (retest → BUY), 09:37 closes the bar
        std::vector<double> path = {20000, 20000, 20000, 20000, 20000, 20010, 20003, 20003};
        feed_path(s, path, 9, 30);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "orb_retest_long", "orb_retest: BUY on the retest, not the break");
        CHECK(out[0].px < 20005.0, "orb_retest: entry is the retest bar's close, not the breakout bar's");
        // one retest per break: a second identical pullback stays silent while flat again
        s.notify_trade_filled(OrbSignal::BUY, "test");
        feed_path(s, {20010, 20003, 20003}, 9, 38);
        CHECK(out.size() == 1, "orb_retest: long side used once per session");
    }
    // ── ib_break: first 10 minutes = balance, close beyond enters, extension target flattens ──
    {
        TrendConfig tc; tc.mode = "ib_break"; tc.tf_min = 1; tc.ib_minutes = 10; tc.ib_ext = 1.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(10, 20000.0);            // balance 19999.75–20000.25 (0.5 wide)
        path.push_back(20005.0);                           // 09:40 closes above → BUY, target 20000.25 + 0.5
        path.push_back(20005.0);                           // 09:41: high 20005.25 ≥ target → flatten when it closes
        path.push_back(20005.0);                           // 09:42 tick closes the 09:41 bar
        feed_path(s, path, 9, 30);
        CHECK(out.size() >= 1 && out[0].sig == OrbSignal::BUY && out[0].why == "ib_break_long", "ib_break: BUY on the balance break");
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "ib_target", "ib_break: flattens at the extension target");
    }
    // ── news_break: range from 08:30 for 5 min, break within the window; event-only gate ──
    {
        TrendConfig tc; tc.mode = "news_break"; tc.tf_min = 1; tc.nb_hhmm = 830; tc.nb_range_min = 5; tc.nb_window_min = 30;
        tc.win_start = 800; tc.win_end = 1000;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path = {20000, 20000, 20000, 20000, 20000, 19990, 19990};
        feed_path(s, path, 8, 30);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::SELL && out[0].why == "news_break_short", "news_break: SELL on the post-release range break");
        TrendConfig te = tc; te.nb_event_only = true;
        TrendStrategy s2(te, risk_cfg()); int n = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; });
        s2.reset_session(); s2.set_event_day(false);
        feed_path(s2, path, 8, 30);
        CHECK(n == 0, "news_break: event-only variant stays silent on a non-event day");
        s2.reset_session(); s2.set_event_day(true);
        feed_path(s2, path, 8, 30);
        CHECK(n == 1, "news_break: event-only variant trades on an event day");
    }
    // ── gap_fade: day 1 sets the prior close; day 2 opens 30 pts higher, fades, flattens at 80 % filled ──
    {
        TrendConfig tc; tc.mode = "gap_fade"; tc.tf_min = 1; tc.gf_min_pts = 20; tc.gf_wait_min = 3; tc.gf_fill_frac = 0.8;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        feed_path(s, std::vector<double>(5, 20000.0), 9, 30);       // day 1: RTH close ≈ 20000
        s.reset_session();                                          // 18:00 rollover: prior close = 20000
        const int64_t day = 86400LL * 1'000'000LL;
        auto tick2 = [&](int h, int m, int k, double p) { s.on_tick(OrbTick{at(h, m, k * 10) + day, p, 1, k % 2 == 0}); };
        for (int m = 0; m < 3; ++m) for (int k = 0; k < 6; ++k) tick2(9, 30 + m, k, 20030.0 + (k % 2 ? 0.25 : -0.25));   // gap +30, holds
        for (int k = 0; k < 6; ++k) tick2(9, 33, k, 20032.0 - k * 1.0);      // 09:33 opens 20032, closes 20027 (< open, < rth_open)
        for (int k = 0; k < 6; ++k) tick2(9, 34, k, 20026.0);                 // closes the 09:33 bar → SELL
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::SELL && out[0].why == "gap_fade_short", "gap_fade: SELL when the gap starts closing");
        for (int k = 0; k < 6; ++k) tick2(9, 35, k, 20004.0);                 // 80 % of 30 = 24 → target 20006; low 20004 reaches it
        for (int k = 0; k < 6; ++k) tick2(9, 36, k, 20004.0);                 // closes the 09:35 bar → flatten
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "gap_filled", "gap_fade: flattens once the gap is 80 % filled");
    }
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
        CHECK(!s.session().in_position && s.session().trades_today == 0,
              "release: host notify frees the engine and gives the unexecuted trade back (2026-10-09)");
        s.notify_trade_filled(OrbSignal::FLATTEN_EOD, "not_executed:risk");
        CHECK(s.session().trades_today == 0, "release: a second release while flat does not undercount");
    }
    // ── a gate-refused entry does not use up the cap (2026-10-09: refused 02:24 ET short → 02:36 long blocked at 2/2) ──
    {
        TrendConfig tc; tc.mode = "donchian"; tc.tf_min = 1; tc.donchian_n = 20;
        OrbConfig rc = risk_cfg(); rc.max_daily_trades = 1;
        TrendStrategy s(tc, rc); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> path(26, 20000.0);
        for (int i = 22; i < 26; ++i) path[i] = 20010.0;
        feed_path(s, path, 9, 30);
        CHECK(out.size() == 1 && s.session().trades_today == 1, "cap: first entry signal counts 1/1");
        s.notify_trade_filled(OrbSignal::FLATTEN_EOD, "not_executed:book_gate:liq_vol_low");
        CHECK(s.session().trades_today == 0, "cap: refused entry gives the trade back");
        std::vector<double> p2(26, 20010.0);
        for (int i = 22; i < 26; ++i) p2[i] = 20030.0;
        feed_path(s, p2, 9, 56);
        CHECK(out.size() == 2 && s.session().trades_today == 1, "cap: the next signal is allowed at the cap");
        s.notify_trade_filled(OrbSignal::BUY, "exchange_stop");
        CHECK(s.session().trades_today == 1, "cap: an executed trade still counts");
    }

    // ── level_fade (2026-10-01): failed break of the prior-day high, fade it once ──
    {
        TrendConfig tc; tc.mode = "level_fade"; tc.tf_min = 1; tc.level = "prior_day"; tc.lf_max_poke_atr = 0.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        feed_path(s, {20000, 20020, 20050, 20010, 19980}, 9, 30);       // day 1 RTH: high ≈ 20050.25
        s.on_tick(OrbTick{at(16, 1), 20000.0, 1, true});
        s.reset_session();                                              // 18:00 rollover → prior day = 20050.25 / 19979.75
        const int64_t day2 = 86400LL * 1'000'000LL;
        auto tick2 = [&](int h, int m, int sec, double px) { s.on_tick(OrbTick{at(h, m, sec) + day2, px, 1, true}); };
        for (int m = 0; m < 3; ++m) for (int k = 0; k < 6; ++k) tick2(9, 30 + m, k * 10, 20030.0 + (k % 2));   // warm the ATR inside the range
        tick2(9, 33, 0, 20040.0); tick2(9, 33, 20, 20060.0); tick2(9, 33, 40, 20045.0);                         // pokes above, closes back inside
        tick2(9, 34, 0, 20044.0);                                                                                // closes the 09:33 bar
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::SELL && out[0].why == "prior_day_high_fade", "level_fade: SELL the failed break of the prior-day high");
        s.notify_trade_filled(OrbSignal::SELL, "test");
        tick2(9, 35, 0, 20065.0); tick2(9, 35, 30, 20045.0); tick2(9, 36, 0, 20045.0);
        CHECK(out.size() == 1, "level_fade: the high side fades once per session");

        TrendConfig t2 = tc; t2.lf_max_poke_atr = 0.5;                  // a deep break is not a "poke"
        TrendStrategy s2(t2, risk_cfg()); std::vector<Rec> o2;
        s2.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { o2.push_back({g, p, w}); });
        s2.reset_session();
        feed_path(s2, {20000, 20020, 20050, 20010, 19980}, 9, 30);
        s2.on_tick(OrbTick{at(16, 1), 20000.0, 1, true}); s2.reset_session();
        auto t2k = [&](int h, int m, int sec, double px) { s2.on_tick(OrbTick{at(h, m, sec) + day2, px, 1, true}); };
        for (int m = 0; m < 3; ++m) for (int k = 0; k < 6; ++k) t2k(9, 30 + m, k * 10, 20030.0 + (k % 2));
        t2k(9, 33, 0, 20040.0); t2k(9, 33, 20, 20150.0); t2k(9, 33, 40, 20045.0); t2k(9, 34, 0, 20044.0);
        CHECK(o2.empty(), "level_fade: a break deeper than lf_max_poke_atr x ATR is not faded");
    }
    // ── vwap_reclaim (2026-10-01): N closes below VWAP, then a close back above → long ──
    {
        TrendConfig tc; tc.mode = "vwap_reclaim"; tc.tf_min = 1; tc.vr_min_bars = 4;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        auto bar = [&](int h, int m, double px, int vol) {
            for (int k = 0; k < 4; ++k) s.on_tick(OrbTick{at(h, m, k * 15), px, vol, k % 2 == 0});
        };
        bar(9, 30, 20000.0, 5000);                                      // heavy volume pins VWAP near 20000
        for (int m = 31; m <= 35; ++m) bar(9, m, 19990.0, 10);          // five closes below VWAP
        bar(9, 36, 20010.0, 10);                                        // closes back above
        bar(9, 37, 20010.0, 10);                                        // closes the 09:36 bar
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "vwap_reclaim_long", "vwap_reclaim: BUY the reclaim after a stretch below");

        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; }); s2.reset_session();
        auto bar2 = [&](int h, int m, double px, int vol) { for (int k = 0; k < 4; ++k) s2.on_tick(OrbTick{at(h, m, k * 15), px, vol, k % 2 == 0}); };
        bar2(9, 30, 20000.0, 5000);
        for (int m = 31; m <= 32; ++m) bar2(9, m, 19990.0, 10);         // only two closes below
        bar2(9, 33, 20010.0, 10); bar2(9, 34, 20010.0, 10);
        CHECK(n2 == 0, "vwap_reclaim: silent when the stretch is shorter than vr_min_bars");
    }
    // ── range_break (2026-10-01): a 02:00-03:00 range, broken in the 03:00-05:00 window ──
    {
        TrendConfig tc; tc.mode = "range_break"; tc.tf_min = 1; tc.rb_from = 200; tc.rb_to = 300;
        tc.win_start = 300; tc.win_end = 500; tc.session = "globex";
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        std::vector<double> rng; for (int i = 0; i < 60; ++i) rng.push_back(20000.0 + (i % 5) * 2.0);   // 20000..20008
        feed_path(s, rng, 2, 0);
        CHECK(out.empty(), "range_break: no entry while the range is forming");
        feed_path(s, {20005, 20006, 20007, 20020, 20025}, 3, 0);       // 03:03 closes above 20008.25
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "range_break_long", "range_break: BUY the first close above the completed range");
        s.notify_trade_filled(OrbSignal::BUY, "test");
        feed_path(s, {20005, 20030}, 3, 5);
        CHECK(out.size() == 1, "range_break: the long side fires once per day");
        feed_path(s, {19990, 19980}, 3, 7);
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::SELL && out[1].why == "range_break_short", "range_break: the short side still fires");

        TrendStrategy s2(tc, risk_cfg()); int n2 = 0;                   // break outside the entry window
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; }); s2.reset_session();
        feed_path(s2, rng, 2, 0);
        feed_path(s2, std::vector<double>(120, 20004.0), 3, 0);         // quiet until 05:00
        feed_path(s2, {20004, 20030, 20030}, 5, 0);
        CHECK(n2 == 0, "range_break: no entry outside win_start..win_end");
    }
    // ── order-flow ORB (2026-10-14): NY 09:30-09:35 range, rb_of_filter +1, first break only, range-side stop ──
    {
        TrendConfig tc; tc.mode = "range_break"; tc.tf_min = 1; tc.rb_from = 930; tc.rb_to = 935;
        tc.win_start = 935; tc.win_end = 1555; tc.rb_last_entry = 1525; tc.rb_of_filter = 1; tc.rb_first_only = true; tc.rb_stop_range = true;
        // minutes of 6 ticks each; buys = how many of the 6 are aggressor buys
        auto run = [&](int window_buys, const std::vector<double>& after, std::vector<Rec>& out, double& hint) {
            TrendStrategy s(tc, risk_cfg());
            s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); if (g == OrbSignal::BUY || g == OrbSignal::SELL) hint = s.take_stop_hint(); });
            s.reset_session();
            for (int m = 30; m < 35; ++m)                                  // range 19995..20005
                for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, m, k * 10), 20000.0 + (k % 2 ? 5.0 : -5.0), 1, k < window_buys});
            for (size_t i = 0; i < after.size(); ++i)
                for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, 35 + (int)i, k * 10), after[i], 1, k % 2 == 0});
        };
        std::vector<Rec> o1; double h1 = 0; run(5, {20001, 20012, 20013, 20014}, o1, h1);   // buyers, break up
        CHECK(o1.size() == 1 && o1[0].sig == OrbSignal::BUY, "of_orb: buyers in the range + break up = BUY");
        CHECK(h1 == 19995.0, "of_orb: stop hint = the other side of the range");
        std::vector<Rec> o2; double h2 = 0; run(1, {20001, 20012, 20013, 19980, 19970, 19960}, o2, h2);  // sellers, break up then down
        CHECK(o2.empty(), "of_orb: sellers + break up = no trade, and the first break ends the day");
        std::vector<Rec> o3; double h3 = 0; run(1, {20001, 19985, 19980, 19979}, o3, h3);   // sellers, break down
        CHECK(o3.size() == 1 && o3[0].sig == OrbSignal::SELL && h3 == 20005.0, "of_orb: sellers + break down = SELL, stop at the range high");
        std::vector<Rec> o4; double h4 = 0; run(3, {20001, 20012, 20013}, o4, h4);          // balanced flow
        CHECK(o4.empty(), "of_orb: zero net aggressor = no trade");
        tc.rb_last_entry = 936;                                            // cutoff before the 09:36 break
        std::vector<Rec> o5; double h5 = 0; run(5, {20001, 20012, 20013, 20014}, o5, h5);
        CHECK(o5.empty(), "of_orb: no break entry at/after rb_last_entry");
        tc.rb_last_entry = 1525; tc.vix_chg1_max = -0.03;                // VIX-fall copy (2x size on those days)
        auto run_v = [&](bool known, double chg1) {
            TrendStrategy s(tc, risk_cfg()); int n = 0;
            s.set_signal_callback([&](OrbSignal g, double, const std::string&) { if (g == OrbSignal::BUY) ++n; });
            s.reset_session(); s.set_vix(18.0, 0.0, 0.5, known, chg1);
            for (int m = 30; m < 35; ++m) for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, m, k * 10), 20000.0 + (k % 2 ? 5.0 : -5.0), 1, k < 5});
            std::vector<double> after = {20001, 20012, 20013, 20014};
            for (size_t i = 0; i < after.size(); ++i) for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, 35 + (int)i, k * 10), after[i], 1, k % 2 == 0});
            return n;
        };
        CHECK(run_v(true, -0.05) == 1, "vix_chg1_max: enters after VIX fell 5%");
        CHECK(run_v(true, -0.01) == 0, "vix_chg1_max: no entry after VIX fell only 1%");
        CHECK(run_v(false, -0.05) == 0, "vix_chg1_max: no entry without a VIX regime");
        tc.vix_chg1_max = 0.0; tc.rb_min_range_atr = 0.21;                // wide-range copy: range 10 pts
        auto run_w = [&](double day_atr) {
            TrendStrategy s(tc, risk_cfg()); int n = 0;
            s.set_signal_callback([&](OrbSignal g, double, const std::string&) { if (g == OrbSignal::BUY) ++n; });
            s.reset_session(); s.set_day_atr(day_atr);
            for (int m = 30; m < 35; ++m) for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, m, k * 10), 20000.0 + (k % 2 ? 5.0 : -5.0), 1, k < 5});
            std::vector<double> after = {20001, 20012, 20013, 20014};
            for (size_t i = 0; i < after.size(); ++i) for (int k = 0; k < 6; ++k) s.on_tick(OrbTick{at(9, 35 + (int)i, k * 10), after[i], 1, k % 2 == 0});
            return n;
        };
        CHECK(run_w(40.0) == 1, "rb_min_range_atr: 10-pt range >= .21 x 40 ATR -> trade");
        CHECK(run_w(60.0) == 0, "rb_min_range_atr: 10-pt range < .21 x 60 ATR -> no trade");
        CHECK(run_w(0.0) == 0, "rb_min_range_atr: unknown ATR -> no trade");
        tc.rb_min_range_atr = 0.0;
        {   // VixRegime: chg1 maths, weekend lookup, staleness
            char dir[] = "/tmp/vixtestXXXXXX"; CHECK(mkdtemp(dir) != nullptr, "vix regime: temp dir");
            FILE* f = std::fopen((std::string(dir) + "/VIX_History.csv").c_str(), "w");
            std::fputs("DATE,OPEN,HIGH,LOW,CLOSE\n10/08/2026,1,1,1,20.0\n10/09/2026,1,1,1,19.0\n", f); std::fclose(f);
            paper::VixRegime v; CHECK(v.reload_if_changed(dir), "vix regime: loads");
            auto mon = v.before("2026-10-12");
            CHECK(mon.known && std::fabs(mon.chg1 + 0.05) < 1e-9 && mon.level == 19.0, "vix regime: Monday reads Friday's close, chg1 = -5%");
            CHECK(!v.before("2026-10-16").known, "vix regime: a close 7 days old is stale -> unknown");
            CHECK(!v.reload_if_changed(dir), "vix regime: unchanged file is not reloaded");
        }
    }


    // ── absorption_zone (2026-10-07, founder spec) ──
    {   // balance fade at VWAP - 2 SD: absorption then trigger → LONG; VWAP target exits it
        TrendStrategy s(az_balance_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int t = az_feed_balance(s, 19986, 19987, 300, 19989.5);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "az_balance_fade",
              "absorption_zone: balance fade fires LONG after absorption + trigger");
        az_bar(s, t / 60, t % 60, 19990, 20001, 19990, 20000, 100, 0.6);                // rallies through VWAP
        CHECK(out.size() == 2 && out[1].sig == OrbSignal::FLATTEN_EOD && out[1].why == "az_target1",
              "absorption_zone: exits at target 1 (VWAP)");
    }
    {   // near-miss on volume: 110 < 1.2 x average → no setup
        TrendStrategy s(az_balance_cfg(), risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; }); s.reset_session();
        az_feed_balance(s, 19986, 19987, 110, 19989.5);
        CHECK(n == 0, "absorption_zone: volume near-miss does not fire");
    }
    {   // the absorption bar closes BELOW the zone (selling won) → no setup
        TrendStrategy s(az_balance_cfg(), risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; }); s.reset_session();
        az_feed_balance(s, 19974, 19974.5, 300, 19989.5);
        CHECK(n == 0, "absorption_zone: a close below the zone is not absorption");
    }
    {   // stop: entry then a drop through absorption low - 2 ticks
        TrendStrategy s(az_balance_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int t = az_feed_balance(s, 19986, 19987, 300, 19989.5);
        az_bar(s, t / 60, t % 60, 19989, 19989, 19984, 19985, 100, 0.3);
        CHECK(out.size() == 2 && out[1].why == "az_stop", "absorption_zone: structural stop exits");
    }
    {   // t2 exit style: target 1 moves the stop to entry; a return to entry exits at break-even
        TrendConfig tc = az_balance_cfg(); tc.az_exit = "t2";
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int t = az_feed_balance(s, 19986, 19987, 300, 19989.5);
        az_bar(s, t / 60, t % 60, 19990, 20001, 19990, 20000, 100, 0.6); ++t;         // t1 reached, no exit
        CHECK(out.size() == 1, "absorption_zone: t2 style holds through target 1");
        az_bar(s, t / 60, t % 60, 20000, 20000, 19988, 19988, 100, 0.3);
        CHECK(out.size() == 2 && out[1].why == "az_breakeven", "absorption_zone: t2 style exits at break-even after t1");
    }
    {   // day type: a steady one-way climb after the opening range → TREND up at +30 min
        TrendConfig tc; tc.mode = "absorption_zone"; tc.tf_min = 1; tc.az_or_bars = 3; tc.win_start = 930; tc.win_end = 1555;
        TrendStrategy s(tc, risk_cfg()); s.set_signal_callback([](OrbSignal, double, const std::string&) {}); s.reset_session();
        for (int i = 0; i < 3; ++i) az_bar(s, 9, 30 + i, 20000, 20005, 19995, 20000, 100, 0.5);
        for (int i = 0; i < 27; ++i) { const double o = 20006 + 3.0 * i; az_bar(s, 9, 33 + i, o, o + 4, o - 1, o + 3, 100, 0.75); }
        s.on_tick(OrbTick{at(10, 0, 0), 20090, 1, true});                               // closes the bar ending at +30 min
        CHECK(s.az_daytype() == 1 && s.az_daytype_dir() == 1, "absorption_zone: one-way climb classifies TREND up");
    }
    {   // day type: chop across the opening range and VWAP → BALANCE
        TrendConfig tc; tc.mode = "absorption_zone"; tc.tf_min = 1; tc.az_or_bars = 3; tc.win_start = 930; tc.win_end = 1555;
        TrendStrategy s(tc, risk_cfg()); s.set_signal_callback([](OrbSignal, double, const std::string&) {}); s.reset_session();
        for (int i = 0; i < 3; ++i) az_bar(s, 9, 30 + i, 20000, 20005, 19995, 20000, 100, 0.5);
        for (int i = 0; i < 27; ++i) {
            const bool up = (i / 2) % 2 == 0;
            az_bar(s, 9, 33 + i, 20000, up ? 20008 : 20002, up ? 19998 : 19992, up ? 20003 : 19997, 100, up ? 0.8 : 0.2);
        }
        s.on_tick(OrbTick{at(10, 0, 0), 20000, 1, true});                               // closes the bar ending at +30 min
        CHECK(s.az_daytype() == 2, "absorption_zone: two-sided chop classifies BALANCE");
    }


    // ── trend_confirm (2026-10-07): trend/momentum entries only when every confirmation agrees ──
    {   // clean uptrend + pullback to ema_fast + trigger → LONG at the trigger close
        TrendStrategy s(tcf_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); tcf_feed(s, 1, 0.7, 0.55, false);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::BUY && out[0].why == "tcf_pullback_long" && std::fabs(out[0].px - 20061.5) < 1e-9,
              "trend_confirm: uptrend pullback + trigger → LONG at the trigger close");
        // the signal row must record what the DECISION saw (2026-10-08: the fill-time read of a fresh,
        // empty forming bar reported vol_ok=false / vol_ratio 0 for an entry that passed at 1.44x)
        const TrendStrategy::TcfStatus* snap = s.tcf_entry_snapshot();
        CHECK(snap && snap->vol_ok && snap->vol_ratio > 0 && snap->er_ok && snap->flow_ok && snap->stage == "signal" && snap->bias == 1,
              "trend_confirm: entry snapshot keeps the trigger bar's confirmations");
    }
    {   // flat chop: no EMA stack, low efficiency → nothing
        TrendStrategy s(tcf_cfg(), risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n; }); s.reset_session();
        tcf_feed(s, 1, 0.7, 0.55, true);
        CHECK(n == 0, "trend_confirm: chop does not enter");
    }
    {   // uptrend in price but the aggressors are selling (buy share 0.3) → no long
        TrendStrategy s(tcf_cfg(), risk_cfg()); int n = 0;
        s.set_signal_callback([&](OrbSignal g, double, const std::string&) { if (g == OrbSignal::BUY) ++n; }); s.reset_session();
        tcf_feed(s, 1, 0.3, 0.3, false);
        CHECK(n == 0, "trend_confirm: selling flow blocks the long");
    }
    {   // breakout entry: a new 10-bar high with volume inside a confirmed uptrend
        TrendConfig tc = tcf_cfg(); tc.tcf_entry = "breakout"; tc.tcf_vol_mult = 0.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); tcf_feed(s, 1, 0.7, 0.55, false);
        CHECK(!out.empty() && out[0].sig == OrbSignal::BUY && out[0].why == "tcf_breakout_long", "trend_confirm: breakout entry fires LONG");
    }
    {   // structural stop: entry at 20061.50, then a drop through the pullback low - 0.25 ATR
        TrendStrategy s(tcf_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); int t = tcf_feed(s, 1, 0.7, 0.55, false);
        az_bar(s, t / 60, t % 60, 20061, 20061, 20040, 20041, 100, 0.2);
        CHECK(out.size() == 2 && out[1].why == "tcf_stop", "trend_confirm: structural stop exits");
    }
    {   // trail: +1R and more, then a pullback of one ATR from the best price
        TrendStrategy s(tcf_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); int t = tcf_feed(s, 1, 0.7, 0.55, false);
        az_bar(s, t / 60, t % 60, 20062, 20085, 20062, 20084, 100, 0.8); ++t;         // +23 pts (> 1R)
        az_bar(s, t / 60, t % 60, 20084, 20084, 20060, 20062, 100, 0.2);               // gives it back
        CHECK(out.size() == 2 && out[1].why == "tcf_trail", "trend_confirm: trail exits after +1R");
    }
    {   // volatility confirmation: tf ATR in points must sit inside [tcf_min_atr, tcf_max_atr]
        TrendConfig lo = tcf_cfg(); lo.tcf_min_atr = 50.0;                              // fixture ATR is ~4-5 pts
        TrendStrategy s1(lo, risk_cfg()); int n1 = 0;
        s1.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n1; }); s1.reset_session();
        tcf_feed(s1, 1, 0.7, 0.55, false);
        TrendConfig hi = tcf_cfg(); hi.tcf_max_atr = 1.0;
        TrendStrategy s2(hi, risk_cfg()); int n2 = 0;
        s2.set_signal_callback([&](OrbSignal, double, const std::string&) { ++n2; }); s2.reset_session();
        tcf_feed(s2, 1, 0.7, 0.55, false);
        CHECK(n1 == 0 && n2 == 0, "trend_confirm: ATR below tcf_min_atr / above tcf_max_atr blocks the entry");
    }
    {   // mirror: downtrend + pullback up to ema_fast + trigger → SHORT
        TrendStrategy s(tcf_cfg(), risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session(); tcf_feed(s, -1, 0.7, 0.55, false);
        CHECK(out.size() == 1 && out[0].sig == OrbSignal::SELL && out[0].why == "tcf_pullback_short", "trend_confirm: downtrend fires SHORT");
    }

    // ── trend_confirm live status (manual-signal dashboard, 2026-10-07): stage progression ──
    {
        TrendConfig tc = tcf_cfg(); tc.tcf_ema_fast = 2; tc.tcf_pb_tol_atr = 0.0; tc.tcf_vol_mult = 0.0;
        TrendStrategy s(tc, risk_cfg()); std::vector<Rec> out;
        s.set_signal_callback([&](OrbSignal g, double p, const std::string& w) { out.push_back({g, p, w}); });
        s.reset_session();
        int t = 9 * 60 + 30; double px = 20000;
        auto B = [&](double o, double h, double l, double c, double share) { az_bar(s, t / 60, t % 60, o, h, l, c, 100, share); ++t; };
        auto close_now = [&](double p) { s.on_tick(OrbTick{at(t / 60, t % 60, 0), p, 1, true}); };
        for (int i = 0; i < 3; ++i) { B(px, px + 1, px, px + 1, 0.8); px += 6; }
        close_now(px);
        CHECK(s.tcf_status(at(t / 60, t % 60, 1)).stage == "no_trend", "tcf_status: too few bars → no_trend");
        // narrow gap-up bars: price stays ABOVE EMA-fast (no touch) → trend
        for (int i = 0; i < 22; ++i) { B(px, px + 1, px, px + 1, 0.8); px += 6; }
        close_now(px);
        auto st = s.tcf_status(at(t / 60, t % 60, 1));
        CHECK(st.stage == "trend" && st.bias == 1 && st.ema_stack && st.er_ok && !st.touched, "tcf_status: clean climb, no touch → trend");
        // pullback into EMA-fast on SELLING flow → pullback (waiting: flow not back yet)
        const double top = px - 5;
        B(top, top, top - 4, top - 3, 0.1);                                 // shallow dip into EMA-fast
        B(top - 3, top - 2, top - 4, top - 3, 0.1);
        close_now(top - 3);
        st = s.tcf_status(at(t / 60, t % 60, 1));
        CHECK(st.stage == "pullback" && st.touched && !st.flow_ok, "tcf_status: EMA touch with sellers → pullback");
        // buyers return but no close above the prior high yet → trigger_ready (only the trigger close missing)
        B(top - 3, top - 2.5, top - 3.5, top - 2.75, 0.95);
        B(top - 2.75, top - 2.5, top - 3.25, top - 3, 0.95);                 // closes below the prior high: no trigger
        close_now(top - 3);
        st = s.tcf_status(at(t / 60, t % 60, 1));
        CHECK(st.stage == "trigger_ready" && st.flow_ok && st.trigger_dist > 0, "tcf_status: flow back, trigger pending → trigger_ready");
        CHECK(out.empty(), "tcf_status: no signal while a stage is incomplete");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
