/*  ═══════════════════════════════════════════════════════════════════════════
    test_mtf_scalper.cpp — Unit tests for MtfScalperStrategy

    Pure signal generator — all tests in-process, no PG, no network.
    Standalone build (from repo root):
        g++ -std=c++20 -I src -I src/execution \
            tests/execution/test_mtf_scalper.cpp src/execution/orb_strategy.cpp \
            -o /tmp/test_mtf_scalper && /tmp/test_mtf_scalper

    Time reference: 2025-04-23 .. 2025-05-01, all EDT (UTC-4).
        kBaseUTC = 2025-04-30 00:00:00 UTC = 1746057600
        ET minute m on day_off d → UTC micros = (kBaseUTC + d*86400 + (m+240)*60) * 1e6
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "../../src/execution/mtf_scalper_strategy.hpp"
#include "../../src/execution/mtf_scalper_config.hpp"

// ─── Minimal test harness (same idiom as test_orb_strategy.cpp) ─────────────
static int tests_run = 0, tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("Assert failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a)-(b)) > (eps)) throw std::runtime_error( \
    "ASSERT_NEAR failed: " #a " vs " #b); } while(0)

static constexpr double QNaN = std::numeric_limits<double>::quiet_NaN();

// ─── Time / tick helpers ─────────────────────────────────────────────────────
static constexpr int64_t kBaseUTC = 1746057600LL;   // 2025-04-30 00:00 UTC (Wed, EDT)
// day_off 0 = Apr 30; -1 = Apr 29 (Tue); -2 = Apr 28 (Mon); -7 = Apr 23 (Wed)

static int64_t bar_ts_us(int day_off, int abs_et_min, int sec = 0) {
    return (kBaseUTC + day_off * 86400LL + (int64_t)(abs_et_min + 240) * 60LL + sec)
           * 1'000'000LL;
}

// One 1m bar = 4 ticks (open @:00, low @:10, high @:20, close @:30), size 1 each.
// The PREVIOUS bar completes (and the pipeline runs) when this bar's first tick
// arrives. Requires h >= max(o,c), l <= min(o,c).
static void feed_bar(MtfScalperStrategy& s, int day, int m,
                     double o, double h, double l, double c) {
    s.on_tick({bar_ts_us(day, m, 0),  o, 1, true});
    s.on_tick({bar_ts_us(day, m, 10), l, 1, true});
    s.on_tick({bar_ts_us(day, m, 20), h, 1, true});
    s.on_tick({bar_ts_us(day, m, 30), c, 1, true});
}
static void feed_flat(MtfScalperStrategy& s, int day, int m, double c, double rng = 0.4) {
    feed_bar(s, day, m, c, c + rng / 2, c - rng / 2, c);
}
// Complete the pending bar without starting a meaningful new one.
static void flush(MtfScalperStrategy& s, int day, int next_m, double price) {
    s.on_tick({bar_ts_us(day, next_m, 0), price, 1, true});
}

struct CapturedSignal {
    OrbSignal   signal;
    double      price;
    std::string reason;
};

static MtfScalperStrategy make_strategy(const MtfScalperConfig& cfg,
                                        std::vector<CapturedSignal>& out) {
    MtfScalperStrategy s(cfg);
    s.set_signal_callback([&out](OrbSignal sig, double price, const std::string& reason) {
        out.push_back({sig, price, reason});
    });
    return s;
}

// ─── Config builders ─────────────────────────────────────────────────────────
// Minimal-gate config: no MTF/session/regime/ATR-floor, fast EMAs, compact flag
// params, all exits off — each test enables exactly what it exercises.
static MtfScalperConfig base_cfg() {
    MtfScalperConfig c;
    c.warmup_bars = 5;
    c.use_mtf = false; c.use_session = false; c.use_regime = false; c.use_atr_floor = false;
    c.use_macd = false;
    c.ema_fast_len = 2; c.ema_slow_len = 3;
    c.adaptive_pole = false; c.pole_len_fixed = 4; c.flag_len = 3; c.pole_recency_bars = 5;
    c.use_onset = false;
    c.use_fvg = false; c.use_levels = false;
    c.use_cooldown = false; c.use_daily_loss = false; c.use_max_trades = false;
    c.regime_dir_only = false;   // isolate: directional gate needs HTF bars
    c.use_osc_exit = false; c.use_time_stop = false; c.use_trend_exit = false;
    c.use_breakeven = false; c.use_trailing = false;
    // Flag triggers only: in "auto" the live stoch cross would fire extra
    // entries mid-scenario (faithful to Pine, but noise for these tests).
    c.trigger_mode = "flag_any";
    return c;
}

// ─── Flag pattern builders (spec §1.4) ──────────────────────────────────────
// 4 base + 4 pole + 3 flag + 1 break = 12 bars starting at m0 around `base`.
// Bull: pole +10 over 4 bars, flag consolidates 9.5..10.05, break closes 10.5.
static void feed_flag_long(MtfScalperStrategy& s, int day, int m0, double base) {
    for (int i = 0; i < 4; ++i)
        feed_bar(s, day, m0 + i, base, base + 0.4, base - 0.2, base + 0.1);
    const double steps[4] = {2.5, 5.0, 7.5, 10.0};
    double prev = base + 0.1;
    for (int i = 0; i < 4; ++i) {
        const double c = base + steps[i];
        feed_bar(s, day, m0 + 4 + i, prev, c + 0.2, prev - 0.1, c);
        prev = c;
    }
    feed_bar(s, day, m0 + 8,  base + 10.0, base + 10.05, base + 9.5,  base + 9.7);
    feed_bar(s, day, m0 + 9,  base + 9.7,  base + 10.05, base + 9.5,  base + 9.8);
    feed_bar(s, day, m0 + 10, base + 9.8,  base + 10.05, base + 9.55, base + 9.9);
    feed_bar(s, day, m0 + 11, base + 9.9,  base + 10.6,  base + 9.8,  base + 10.5);
}
static void feed_flag_bear(MtfScalperStrategy& s, int day, int m0, double base) {
    for (int i = 0; i < 4; ++i)
        feed_bar(s, day, m0 + i, base, base + 0.2, base - 0.4, base - 0.1);
    const double steps[4] = {2.5, 5.0, 7.5, 10.0};
    double prev = base - 0.1;
    for (int i = 0; i < 4; ++i) {
        const double c = base - steps[i];
        feed_bar(s, day, m0 + 4 + i, prev, prev + 0.1, c - 0.2, c);
        prev = c;
    }
    feed_bar(s, day, m0 + 8,  base - 10.0, base - 9.5,  base - 10.05, base - 9.7);
    feed_bar(s, day, m0 + 9,  base - 9.7,  base - 9.5,  base - 10.05, base - 9.8);
    feed_bar(s, day, m0 + 10, base - 9.8,  base - 9.55, base - 10.05, base - 9.9);
    feed_bar(s, day, m0 + 11, base - 9.9,  base - 9.8,  base - 10.6,  base - 10.5);
}

// ═════════════════════════════════════════════════════════════════════════════
// 1. Indicator fidelity vs independent reference implementations (Pine ta.*
//    semantics: EMA seed=first value, RMA Wilder, percentrank, DMI RMA form).
// ═════════════════════════════════════════════════════════════════════════════
namespace ref {
static double ema(double prev, double x, int len) {
    if (std::isnan(prev)) return x;
    const double a = 2.0 / (len + 1.0);
    return a * x + (1.0 - a) * prev;
}
static double rma(double prev, double x, int len) {
    if (std::isnan(prev)) return x;
    return (prev * (len - 1) + x) / len;
}
struct Ind {
    double ef = QNaN, es = QNaN;                 // ema 9 / 21
    double mf = QNaN, ms = QNaN, msig = QNaN;    // macd 12/26/9
    double rup = QNaN, rdn = QNaN, pc = QNaN;    // rsi 9
    double atr = QNaN;                           // atr 14
    double ph = QNaN, pl = QNaN;                 // dmi 14
    double dtr = QNaN, dp = QNaN, dm = QNaN, adxr = QNaN;
    double di_p = QNaN, di_m = QNaN, adx = QNaN;
    double pv = 0.0, vv = 0.0, vw = QNaN;
    std::deque<double> closes, highs, lows, kraw, ks;
    double k = QNaN, d = QNaN;

    static double sma(const std::deque<double>& v, int n) {
        if ((int)v.size() < n) return QNaN;
        double s = 0;
        for (int i = (int)v.size() - n; i < (int)v.size(); ++i)
            if (std::isnan(v[i])) return QNaN; else s += v[i];
        return s / n;
    }
    void update(double o, double h, double l, double c, int64_t vol) {
        (void)o;
        ef = ema(ef, c, 9);  es = ema(es, c, 21);
        mf = ema(mf, c, 12); ms = ema(ms, c, 26);
        msig = ema(msig, mf - ms, 9);
        if (!std::isnan(pc)) {
            const double chg = c - pc;
            rup = rma(rup, std::max(chg, 0.0), 9);
            rdn = rma(rdn, std::max(-chg, 0.0), 9);
        }
        pc = c;
        const double tr = std::isnan(ph) ? h - l
            : std::max({h - l, std::fabs(h - pc), std::fabs(l - pc)});
        atr = rma(atr, tr, 14);
        closes.push_back(c); highs.push_back(h); lows.push_back(l);
        if (closes.size() > 64) { closes.pop_front(); highs.pop_front(); lows.pop_front(); }
        if (closes.size() >= 8) {
            double hh = -1e300, ll = 1e300;
            for (size_t i = closes.size() - 8; i < closes.size(); ++i) {
                hh = std::max(hh, highs[i]); ll = std::min(ll, lows[i]);
            }
            kraw.push_back(hh > ll ? 100.0 * (c - ll) / (hh - ll) : QNaN);
        } else kraw.push_back(QNaN);
        if (kraw.size() > 64) kraw.pop_front();
        k = sma(kraw, 3);
        ks.push_back(k); if (ks.size() > 64) ks.pop_front();
        d = sma(ks, 3);
        if (!std::isnan(ph)) {
            const double up = h - ph, dn = pl - l;
            dp = rma(dp, (up > dn && up > 0) ? up : 0.0, 14);
            dm = rma(dm, (dn > up && dn > 0) ? dn : 0.0, 14);
            dtr = rma(dtr, tr, 14);
            if (dtr > 0) {
                di_p = 100.0 * dp / dtr; di_m = 100.0 * dm / dtr;
                const double sum = di_p + di_m;
                adxr = rma(adxr, 100.0 * std::fabs(di_p - di_m) / (sum == 0 ? 1 : sum), 14);
                adx = adxr;
            }
        }
        ph = h; pl = l;
        if (vol > 0) {
            pv += (h + l + c) / 3.0 * (double)vol;
            vv += (double)vol;
            vw = pv / vv;
        }
    }
    double rsi() const {
        if (std::isnan(rup)) return QNaN;
        if (rdn == 0) return 100.0;
        if (rup == 0) return 0.0;
        return 100.0 - 100.0 / (1.0 + rup / rdn);
    }
};
} // namespace ref

TEST(indicators_match_reference_250_bars) {
    MtfScalperConfig cfg;   // defaults: ema 9/21, macd 12/26/9, rsi 9, stoch 8/3/3, atr/adx 14
    cfg.warmup_bars = 100000;   // inert — no signals, we only read indicators
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    ref::Ind r;
    uint32_t rng = 12345;
    double px = 20000.0;
    for (int i = 0; i < 250; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const double mv = ((int)(rng >> 24) - 128) / 32.0;   // ±4 pts
        const double o = px, c = px + mv;
        const double h = std::max(o, c) + 0.3, l = std::min(o, c) - 0.3;
        feed_bar(s, 0, 570 + i, o, h, l, c);
        r.update(o, h, l, c, 4);
        px = c;
    }
    flush(s, 0, 570 + 250, px);

    ASSERT(sigs.empty());
    ASSERT_NEAR(s.ema_fast(), r.ef, 1e-6);
    ASSERT_NEAR(s.ema_slow(), r.es, 1e-6);
    ASSERT_NEAR(s.macd_line(), r.mf - r.ms, 1e-6);
    ASSERT_NEAR(s.macd_signal(), r.msig, 1e-6);
    ASSERT_NEAR(s.macd_hist(), (r.mf - r.ms) - r.msig, 1e-6);
    ASSERT_NEAR(s.rsi(), r.rsi(), 1e-6);
    ASSERT_NEAR(s.stoch_k(), r.k, 1e-6);
    ASSERT_NEAR(s.stoch_d(), r.d, 1e-6);
    ASSERT_NEAR(s.atr(), r.atr, 1e-6);
    ASSERT_NEAR(s.vwap(), r.vw, 1e-6);
    ASSERT_NEAR(s.di_plus(), r.di_p, 1e-6);
    ASSERT_NEAR(s.di_minus(), r.di_m, 1e-6);
    ASSERT_NEAR(s.adx(), r.adx, 1e-6);
}

TEST(indicators_constant_series) {
    MtfScalperConfig cfg;
    cfg.warmup_bars = 100000;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    for (int i = 0; i < 40; ++i) feed_bar(s, 0, 570 + i, 100.0, 100.0, 100.0, 100.0);
    flush(s, 0, 610, 100.0);

    ASSERT_NEAR(s.ema_fast(), 100.0, 1e-9);
    ASSERT_NEAR(s.ema_slow(), 100.0, 1e-9);
    ASSERT_NEAR(s.macd_hist(), 0.0, 1e-9);
    ASSERT_NEAR(s.rsi(), 100.0, 1e-9);      // no down moves → 100
    ASSERT_NEAR(s.atr(), 0.0, 1e-9);
    ASSERT_NEAR(s.vwap(), 100.0, 1e-9);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Bull flag detection + break signal; bear mirror (spec §1.4)
// ═════════════════════════════════════════════════════════════════════════════
TEST(bull_flag_break_fires_long) {
    MtfScalperConfig cfg = base_cfg();   // flag_any mode, fix on
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    ASSERT(sigs.empty());            // nothing until the break bar completes
    flush(s, 0, 612, 110.5);

    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_NEAR(sigs[0].price, 110.5, 1e-9);
    ASSERT_EQ(sigs[0].reason, std::string("flag_break_long"));
    ASSERT(s.bull_break());
}

TEST(bear_flag_break_fires_short) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_bear(s, 0, 600, 100.0);
    flush(s, 0, 612, 89.5);

    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::SELL);
    ASSERT_NEAR(sigs[0].price, 89.5, 1e-9);
    ASSERT_EQ(sigs[0].reason, std::string("flag_break_short"));
    ASSERT(s.bear_break());
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Break-retest state machine (spec §1.4, Pine :1081-1108)
// ═════════════════════════════════════════════════════════════════════════════
TEST(retest_arms_then_fires) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "flag_retest";   // entries on retest only
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.4);                // tick == next bar's open (no pollution)
    ASSERT(sigs.empty());                       // break bar arms, no entry
    ASSERT_NEAR(s.bo_lvl_l(), 110.05, 1e-9);    // armed at flagHi

    // Retest: wick into boLvl + 0.5*ATR, close back >= boLvl AND above the
    // fast EMA (require_price_beyond_fast trend gate)
    feed_bar(s, 0, 612, 110.4, 110.7, 110.0, 110.6);
    flush(s, 0, 613, 110.6);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("flag_retest_long"));
}

TEST(retest_invalidated_on_close_beyond_inval) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "flag_retest";
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(!std::isnan(s.bo_lvl_l()));

    // close < boLvl(110.05) - 0.5*ATR(~1.0) ≈ 109.55 → invalidate
    feed_bar(s, 0, 612, 110.0, 110.1, 109.3, 109.4);
    flush(s, 0, 613, 109.4);
    ASSERT(std::isnan(s.bo_lvl_l()));

    feed_bar(s, 0, 613, 109.4, 110.3, 109.4, 110.2);   // retest shape — dead
    flush(s, 0, 614, 110.2);
    ASSERT(sigs.empty());
}

TEST(retest_expires_after_max_bars) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "flag_retest";
    cfg.retest_max_bars = 3;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.6);             // tick == next bar's open (no pollution)
    ASSERT(!std::isnan(s.bo_lvl_l()));

    // 4 bars that never dip into the retest zone (low > boLvl + 0.5*ATR)
    for (int i = 0; i < 4; ++i)
        feed_bar(s, 0, 612 + i, 110.6, 110.9, 110.6, 110.7);
    flush(s, 0, 616, 110.7);
    ASSERT(std::isnan(s.bo_lvl_l()));           // expired (bar - boBar > 3)

    feed_bar(s, 0, 616, 110.2, 110.4, 110.0, 110.2);   // late retest — dead
    flush(s, 0, 617, 110.2);
    ASSERT(sigs.empty());
}

TEST(retest_invalidated_on_trend_lost) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "flag_retest";
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(!std::isnan(s.bo_lvl_l()));

    // Drop hard: emaFast(2) < emaSlow(3) → loose trend lost → invalidate
    feed_bar(s, 0, 612, 110.4, 110.5, 109.3, 109.5);
    flush(s, 0, 613, 109.5);
    ASSERT(std::isnan(s.bo_lvl_l()));

    feed_bar(s, 0, 613, 109.5, 110.3, 109.5, 110.2);
    flush(s, 0, 614, 110.2);
    ASSERT(sigs.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Onset engine (spec §1.5): flip arm, pullback A, structural B, expiry
// ═════════════════════════════════════════════════════════════════════════════
static MtfScalperConfig onset_cfg(int window = 10) {
    MtfScalperConfig c = base_cfg();
    c.trigger_mode = "onset";
    c.use_onset = true;
    c.onset_need_macd = false;   // use_macd=false → MACD confirm moot
    c.onset_swing_len = 2;
    c.onset_window_bars = window;
    return c;
}

TEST(onset_flip_arm_and_pullback_trigger) {
    MtfScalperConfig cfg = onset_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Decline (emaFast < emaSlow), range 1.0 bars so ATR ≈ 1
    double c = 100.0;
    for (int i = 0; i < 6; ++i) {
        c -= 1.0;
        feed_bar(s, 0, 600 + i, c + 1.0, c + 1.5, c - 0.5 + 1.0 - 1.0, c);
    }
    // Rise until the EMA flip arms the onset
    int m = 606;
    while (!s.onset_armed_l() && m < 615) {
        c += 1.5;
        feed_bar(s, 0, m, c - 1.5, c + 0.5, c - 1.5 - 0.5, c);
        flush(s, 0, m + 1, c);
        ++m;
    }
    ASSERT(s.onset_armed_l());
    ASSERT(sigs.empty());   // flip bar itself is too extended for trigger A

    // Pullback bar crafted against the live fast EMA: dip below, close back
    // above within 0.25*ATR (trigger A, Pine :568)
    const double ef = s.ema_fast();
    feed_bar(s, 0, m, ef, ef + 0.3, ef - 0.5, ef + 0.1);
    flush(s, 0, m + 1, ef + 0.1);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("onset_pullback_long"));
    ASSERT(!s.onset_armed_l());   // consumed on fire
}

TEST(onset_structural_break_trigger) {
    MtfScalperConfig cfg = onset_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Pivot high 102 at bar 602 (len 2, confirmed at 604)
    feed_bar(s, 0, 600, 100.0, 100.5, 99.8, 100.2);
    feed_bar(s, 0, 601, 100.2, 101.5, 100.0, 101.2);
    feed_bar(s, 0, 602, 101.2, 102.0, 101.0, 101.8);
    feed_bar(s, 0, 603, 101.8, 101.5, 100.6, 100.8);
    feed_bar(s, 0, 604, 100.8, 100.5, 99.6, 99.8);
    // Decline, then flip up with tight-range bars (tiny ATR → flip bar too
    // extended for trigger A), then rise to just under the pivot
    double c = 99.8;
    for (int i = 0; i < 4; ++i) {
        c -= 1.0;
        feed_bar(s, 0, 605 + i, c + 1.0, c + 1.1, c - 0.1, c);
    }
    int m = 609;
    while (!s.onset_armed_l() && m < 618) {
        c += 1.5;
        feed_bar(s, 0, m, c - 1.5, c + 0.4, c - 1.4, c);
        flush(s, 0, m + 1, c);
        ++m;
    }
    ASSERT(s.onset_armed_l());
    // Rise toward the pivot with lows above the fast EMA (no trigger A)
    while (c < 101.0) {
        c += 1.2;
        feed_bar(s, 0, m, c - 1.2, c + 0.3, c - 0.4, c);
        flush(s, 0, m + 1, c);
        ++m;
        ASSERT(sigs.empty());
    }
    // Structural break: close beyond last pivot high 102, within +0.5*ATR
    feed_bar(s, 0, m, c, 102.6, c - 0.5, 102.3);
    flush(s, 0, m + 1, 102.3);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("onset_struct_long"));
}

TEST(onset_window_expires) {
    MtfScalperConfig cfg = onset_cfg(3);   // 3-bar window
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    double c = 100.0;
    for (int i = 0; i < 6; ++i) {
        c -= 1.0;
        feed_bar(s, 0, 600 + i, c + 1.0, c + 1.5, c - 0.5, c);
    }
    int m = 606;
    while (!s.onset_armed_l() && m < 615) {
        c += 1.5;
        feed_bar(s, 0, m, c - 1.5, c + 0.5, c - 2.0, c);
        flush(s, 0, m + 1, c);
        ++m;
    }
    ASSERT(s.onset_armed_l());
    // The rise loop's final flush tick (prev close) sits in minute m's bar and
    // would dip below the fast EMA — absorb it with a throwaway down bar whose
    // low still clears the EMA (no trigger A), then 4 quiet bars entirely
    // above the EMA so the 3-bar window expires.
    feed_flat(s, 0, m, c - 0.2, 0.1);
    flush(s, 0, m + 1, c + 0.4);            // == first quiet bar's open
    for (int i = 0; i < 4; ++i) {
        c += 0.5;
        feed_bar(s, 0, m + 1 + i, c - 0.1, c + 0.1, c - 0.15, c);
        flush(s, 0, m + i + 2, c + 0.4);    // == next quiet bar's open
    }
    // Pullback-shaped bar after expiry → no signal
    const double ef = s.ema_fast();
    feed_bar(s, 0, m + 5, ef, ef + 0.3, ef - 0.5, ef + 0.1);
    flush(s, 0, m + 6, ef + 0.1);
    ASSERT(sigs.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. FVG engine (spec §1.6)
// ═════════════════════════════════════════════════════════════════════════════
static MtfScalperConfig fvg_cfg() {
    MtfScalperConfig c = base_cfg();
    c.trigger_mode = "fvg";
    c.use_fvg = true;
    c.use_fvg_entry = true;
    return c;
}

TEST(fvg_creation_and_retest_one_shot) {
    MtfScalperConfig cfg = fvg_cfg();
    cfg.warmup_bars = 3;    // retest lands on the 4th bar — must be warm
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_bar(s, 0, 600, 100.0, 100.5, 99.5, 100.2);
    feed_bar(s, 0, 601, 100.2, 101.0, 100.0, 100.8);
    feed_bar(s, 0, 602, 101.2, 101.8, 101.0, 101.5);   // bull FVG [100.5, 101.0]
    flush(s, 0, 603, 101.5);
    ASSERT_EQ(s.live_fvg_bulls(), (size_t)1);

    // Retest: wick into the gap (low 100.8 ∈ (bot, top]), close ≥ mid 100.75
    // and above the fast EMA (require_price_beyond_fast trend gate)
    feed_bar(s, 0, 603, 101.4, 101.5, 100.8, 101.3);
    flush(s, 0, 604, 101.3);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("fvg_retest_long"));

    // One shot per gap: same touch again → no second signal
    feed_bar(s, 0, 604, 101.2, 101.3, 100.8, 101.1);
    flush(s, 0, 605, 101.1);
    ASSERT_EQ(sigs.size(), (size_t)1);
}

TEST(fvg_min_size_filter) {
    MtfScalperConfig cfg = fvg_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_bar(s, 0, 600, 100.0, 100.5, 99.5, 100.2);
    feed_bar(s, 0, 601, 100.2, 101.0, 100.0, 100.8);
    feed_bar(s, 0, 602, 101.2, 101.8, 100.6, 101.5);   // gap 0.1 < 0.25*ATR(~0.25)
    flush(s, 0, 603, 101.5);
    ASSERT_EQ(s.live_fvg_bulls(), (size_t)0);

    feed_bar(s, 0, 603, 101.4, 101.5, 100.7, 101.2);   // touch — no gap, no signal
    flush(s, 0, 604, 101.2);
    ASSERT(sigs.empty());
}

TEST(fvg_expires_after_max_bars) {
    MtfScalperConfig cfg = fvg_cfg();
    cfg.fvg_max_bars = 5;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_bar(s, 0, 600, 100.0, 100.5, 99.5, 100.2);
    feed_bar(s, 0, 601, 100.2, 101.0, 100.0, 100.8);
    feed_bar(s, 0, 602, 101.2, 101.8, 101.0, 101.5);   // gap [100.5, 101.0]
    flush(s, 0, 603, 101.5);
    ASSERT_EQ(s.live_fvg_bulls(), (size_t)1);

    // 6 drift bars that never touch the gap (low > 101.0; too small for new gaps)
    for (int i = 0; i < 6; ++i)
        feed_bar(s, 0, 603 + i, 101.5, 101.9, 101.15, 101.7);
    flush(s, 0, 609, 101.7);
    ASSERT_EQ(s.live_fvg_bulls(), (size_t)0);   // expired (bi - gapBar > 5)

    feed_bar(s, 0, 609, 101.4, 101.5, 100.8, 101.2);   // late touch — dead
    flush(s, 0, 610, 101.2);
    ASSERT(sigs.empty());
}

TEST(fvg_per_side_cap_retires_oldest) {
    MtfScalperConfig cfg = fvg_cfg();
    cfg.fvg_max_keep = 2;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    double c = 100.0;
    for (int i = 0; i < 5; ++i) {
        const double o = c; c += 2.0;
        feed_bar(s, 0, 600 + i, o, c + 0.3, c - 0.3, c);  // gaps at bars 602,603,604
    }
    flush(s, 0, 605, c);
    ASSERT_EQ(s.live_fvg_bulls(), (size_t)2);   // oldest retired at cap

    // A touch of the retired zone [100.3, 103.7] must not fire; closes below
    // the live zone midpoints so no live-gap trigger either.
    feed_bar(s, 0, 605, c, c + 0.2, 102.0, 103.9);
    flush(s, 0, 606, 103.9);
    ASSERT(sigs.empty());
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Sweeps: PDL poke-and-close-back fires once per day (spec §1.7)
// ═════════════════════════════════════════════════════════════════════════════
TEST(sweep_fires_once_per_day) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "sweep";
    cfg.use_levels = true;
    cfg.lv_pw = false; cfg.lv_sess = false; cfg.lv_pdc = false;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Day A (Apr 29): establish day low 100.0 with gently rising closes
    double c = 100.2;
    for (int i = 0; i < 6; ++i) {
        const double l = (i == 0) ? 100.0 : c - 0.3;
        feed_bar(s, -1, 600 + i, c - 0.2, c + 0.3, l, c);
        c += 0.2;
    }
    // Day B (Apr 30): poke below PDL and close back above
    feed_bar(s, 0, 600, 100.8, 101.4, 99.7, 101.3);
    flush(s, 0, 601, 101.3);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("sweep_long"));
    ASSERT_NEAR(s.pdl(), 100.0, 1e-9);
    s.notify_trade_filled(OrbSignal::BUY, "tp", 20.0);   // flat again (no pyramiding)

    // Second poke same day — pdlSwept blocks it
    feed_bar(s, 0, 601, 101.3, 101.5, 99.6, 101.4);
    flush(s, 0, 602, 101.4);
    ASSERT_EQ(sigs.size(), (size_t)1);

    // Next day: flag resets, new PDL = day-B low (99.6) → sweep fires again
    feed_bar(s, 1, 600, 101.4, 101.8, 99.3, 101.6);
    flush(s, 1, 601, 101.6);
    ASSERT_EQ(sigs.size(), (size_t)2);
    ASSERT(sigs[1].signal == OrbSignal::BUY);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Regime filter blocks when <2 of 3 components pass; ATR floor blocks
// ═════════════════════════════════════════════════════════════════════════════
TEST(regime_blocks_when_components_fail) {
    // Ranks default to 50 during warmup (nz(...,50)) — set rank mins above 50
    // for ADX + ATR components so only the EMA-spread component passes (1 < 2).
    MtfScalperConfig cfg = base_cfg();
    cfg.use_regime = true;
    cfg.regime_dir_only = false;   // isolate the component-count gate
    cfg.regime_components_required = 2;
    cfg.adx_rank_min = 95.0; cfg.adx_rising_pass = false;
    cfg.atr_rank_min_regime = 95.0; cfg.atr_expansion_pass = false;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(sigs.empty());                        // regime inactive → blocked
    ASSERT(!s.regime_active());

    // Control: default thresholds → ADX(50≥30) + ATR(50≥20) pass → fires
    MtfScalperConfig ok = base_cfg();
    ok.use_regime = true;
    ok.regime_dir_only = false;
    std::vector<CapturedSignal> sigs2;
    MtfScalperStrategy s2 = make_strategy(ok, sigs2);
    feed_flag_long(s2, 0, 600, 100.0);
    flush(s2, 0, 612, 110.5);
    ASSERT_EQ(sigs2.size(), (size_t)1);
}

TEST(atr_floor_blocks_low_rank) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_atr_floor = true;
    cfg.atr_rank_min = 95.0;    // atrRank defaults to 50 in warmup → fails
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(sigs.empty());

    MtfScalperConfig ok = base_cfg();
    ok.use_atr_floor = true;
    ok.atr_rank_min = 10.0;
    std::vector<CapturedSignal> sigs2;
    MtfScalperStrategy s2 = make_strategy(ok, sigs2);
    feed_flag_long(s2, 0, 600, 100.0);
    flush(s2, 0, 612, 110.5);
    ASSERT_EQ(sigs2.size(), (size_t)1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. auto_mode_flag_fix (spec §0)
// ═════════════════════════════════════════════════════════════════════════════
TEST(auto_mode_flag_fix_off_reproduces_pine_bug) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "auto";
    cfg.auto_mode_flag_fix = false;    // Pine bug: flag triggers dead in Auto
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(s.bull_break());            // pattern detected...
    ASSERT(sigs.empty());              // ...but the trigger chain is dead
}

TEST(auto_mode_flag_fix_on_fires) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "auto";
    cfg.auto_mode_flag_fix = true;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Exits (spec §2): BE latch, stop ratchet, time stop, osc exit, trend exit
// ═════════════════════════════════════════════════════════════════════════════
// Enter long via the flag break (base 100 → entry 110.5) with all exits off.
// Clears `sigs` on success so exit tests count only post-entry signals.
static void enter_long_flag(MtfScalperStrategy& s, std::vector<CapturedSignal>& sigs) {
    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT(s.in_position());
    sigs.clear();
}

TEST(breakeven_latch_at_exactly_entry_and_ratchet) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_breakeven = true;                 // be_trigger = 0.5 × tpDist
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    const double entry_atr = s.atr();          // ATR at the signal bar
    const double r_unit = 1.2 * entry_atr;
    ASSERT_NEAR(s.cur_stop(), 110.5 - r_unit, 1e-9);        // base stop
    ASSERT_NEAR(s.cur_tp(), 110.5 + r_unit * 1.5, 1e-9);    // TP = 1.5R

    // BE trigger: close > 110.5 + tpDist*0.5
    const double trig = 110.5 + (r_unit * 1.5) * 0.5;
    feed_bar(s, 0, 612, 110.5, trig + 0.2, 110.4, trig + 0.1);
    flush(s, 0, 613, trig + 0.1);
    ASSERT_NEAR(s.cur_stop(), 110.5, 1e-9);    // latched at EXACTLY avg
    ASSERT(sigs.empty());

    // Ratchet: lower closes must never pull the stop back below 110.5
    feed_bar(s, 0, 613, trig, trig + 0.1, 110.6, 110.8);
    flush(s, 0, 614, 110.8);
    ASSERT_NEAR(s.cur_stop(), 110.5, 1e-9);
    feed_bar(s, 0, 614, 110.8, 110.9, 110.0, 110.2);
    flush(s, 0, 615, 110.2);
    ASSERT_NEAR(s.cur_stop(), 110.5, 1e-9);
    ASSERT(sigs.empty());                        // no exit fired
}

TEST(time_stop_flattens_after_max_hold_bars) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_time_stop = true;
    cfg.max_hold_bars = 3;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    for (int i = 0; i < 3; ++i) {
        feed_bar(s, 0, 612 + i, 110.4, 110.6, 110.2, 110.4);
        flush(s, 0, 613 + i, 110.4);
        if (i < 2) ASSERT(sigs.empty());
    }
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::FLATTEN_EOD);
    ASSERT_EQ(sigs[0].reason, std::string("Time stop"));

    s.notify_trade_filled(OrbSignal::BUY, "Time stop", 5.0);
    ASSERT(!s.in_position());
}

TEST(osc_exit_on_stoch_crossunder_above_80) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_osc_exit = true;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    // 8 strong bars (close == high → kRaw 100) pin k and d at 100
    double c = 110.5;
    for (int i = 0; i < 8; ++i) {
        c += 0.5;
        feed_bar(s, 0, 612 + i, c - 0.5, c, c - 0.8, c);
        flush(s, 0, 613 + i, c);
    }
    ASSERT(sigs.empty());
    // Dip bar: kRaw ≈ 92 → k crosses under d while k > 80 → "Osc reversal"
    feed_bar(s, 0, 620, c, c + 0.2, c - 0.6, c - 0.1);
    flush(s, 0, 621, c - 0.1);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::FLATTEN_EOD);
    ASSERT_EQ(sigs[0].reason, std::string("Osc reversal"));
}

TEST(trend_exit_on_close_below_fast_ema) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_trend_exit = true;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    feed_bar(s, 0, 612, 110.4, 110.5, 108.8, 109.0);   // close back under emaFast
    flush(s, 0, 613, 109.0);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::FLATTEN_EOD);
    ASSERT_EQ(sigs[0].reason, std::string("Trend reversal"));
}

TEST(trailing_stop_arms_and_retires_tp) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_trailing = true;
    cfg.trail_atr_mult = 1.0;
    cfg.trail_start_r = 1.0;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    const double r_unit = 1.2 * s.atr();
    ASSERT(!std::isnan(s.cur_tp()));

    // close > avg + 1R arms trailing → TP retired (NaN), stop = close - 1*ATR
    const double trig = 110.5 + r_unit + 0.3;
    feed_bar(s, 0, 612, 110.6, trig + 0.1, 110.4, trig);
    flush(s, 0, 613, trig);
    ASSERT(std::isnan(s.cur_tp()));
    ASSERT_NEAR(s.cur_stop(), trig - 1.0 * s.atr(), 1e-6);
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Sizing (spec §3): floor to int, min_qty clamp, qty_max cap
// ═════════════════════════════════════════════════════════════════════════════
TEST(sizing_floor_clamp_cap) {
    MtfScalperConfig cfg = base_cfg();
    cfg.exit_sizing = "fixed";
    cfg.sl_points = 15.0;         // slDist × pv = 15 × 2 = $30 risk per contract
    cfg.point_value = 2.0;
    cfg.risk_pct = 1.0;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    for (int i = 0; i < 6; ++i) feed_flat(s, 0, 600 + i, 100.0);
    flush(s, 0, 606, 100.0);

    ASSERT_EQ(s.qty_calc(25000.0), 8);     // floor(25000×1% / 30) = floor(8.33)
    ASSERT_EQ(s.qty_calc(100.0), 1);       // floor(1×1% / 30)=0 → min_qty clamp 1
    ASSERT_EQ(s.qty_calc(0.0), 0);         // no equity → no size

    MtfScalperConfig capped = cfg;
    capped.qty_max = 3;
    MtfScalperStrategy s2 = make_strategy(capped, sigs);
    for (int i = 0; i < 6; ++i) feed_flat(s2, 0, 600 + i, 100.0);
    flush(s2, 0, 606, 100.0);
    ASSERT_EQ(s2.qty_calc(100000.0), 3);   // floor(33.3) capped at 3
}

TEST(sizing_atr_mode_uses_live_atr) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    enter_long_flag(s, sigs);
    const double atr = s.atr();
    const int expect = (int)std::floor(25000.0 * 0.01 / (1.2 * atr * 2.0));
    ASSERT(expect > 0);
    ASSERT_EQ(s.qty_calc(25000.0), expect);
    ASSERT_EQ(s.current_bracket().qty, expect);
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. Cooldown (spec §4): 2 consecutive losses → 5-bar block; win resets
// ═════════════════════════════════════════════════════════════════════════════
TEST(cooldown_blocks_after_two_losses) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_cooldown = true;
    cfg.cooldown_after_losses = 2;
    cfg.cooldown_bars = 25;      // spans the gap + 12 bars a flag pattern takes to rebuild
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Loss #1
    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    s.notify_trade_filled(OrbSignal::BUY, "stop", -50.0);
    feed_flat(s, 0, 612, 109.0);            // kill the stale retest arm (close
    flush(s, 0, 613, 109.0);                // < boLvl - 0.5*ATR) — Pine :1101

    // Loss #2 — streak 2 → cooldown armed at the notify bar (bi ≈ 631)
    feed_flag_long(s, 0, 620, 110.0);
    flush(s, 0, 632, 120.5);
    ASSERT_EQ(sigs.size(), (size_t)2);
    s.notify_trade_filled(OrbSignal::BUY, "stop", -50.0);
    feed_flat(s, 0, 632, 119.0);
    flush(s, 0, 633, 119.0);

    // Pattern inside the cooldown window → break bar blocked
    feed_flag_long(s, 0, 640, 120.0);
    flush(s, 0, 652, 130.5);
    ASSERT_EQ(sigs.size(), (size_t)2);

    // 4 neutral bars, then a fresh pattern whose break bar lands past the
    // window (break at bi ≈ 671 > 631 + 25)
    for (int i = 0; i < 4; ++i) feed_flat(s, 0, 652 + i, 119.7);
    flush(s, 0, 656, 119.7);
    feed_flag_long(s, 0, 660, 130.0);
    flush(s, 0, 672, 140.5);
    ASSERT_EQ(sigs.size(), (size_t)3);
    ASSERT(sigs[2].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[2].reason, std::string("flag_break_long"));
}

TEST(cooldown_win_resets_streak) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_cooldown = true;
    cfg.cooldown_after_losses = 2;
    cfg.cooldown_bars = 15;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    s.notify_trade_filled(OrbSignal::BUY, "stop", -50.0);   // streak 1
    feed_flat(s, 0, 612, 109.0);            // kill stale retest arm
    flush(s, 0, 613, 109.0);

    feed_flag_long(s, 0, 620, 110.0);
    flush(s, 0, 632, 120.5);
    ASSERT_EQ(sigs.size(), (size_t)2);
    s.notify_trade_filled(OrbSignal::BUY, "tp", +30.0);     // win → streak 0
    feed_flat(s, 0, 632, 119.0);
    flush(s, 0, 633, 119.0);

    feed_flag_long(s, 0, 640, 120.0);
    flush(s, 0, 652, 130.5);
    ASSERT_EQ(sigs.size(), (size_t)3);
    s.notify_trade_filled(OrbSignal::BUY, "stop", -50.0);   // streak 1 only
    feed_flat(s, 0, 652, 129.0);
    flush(s, 0, 653, 129.0);

    // Not blocked: streak never reached 2
    feed_flag_long(s, 0, 660, 130.0);
    flush(s, 0, 672, 140.5);
    ASSERT_EQ(sigs.size(), (size_t)4);
}

// ═════════════════════════════════════════════════════════════════════════════
// 12. HTF gate uses only the last COMPLETED HTF bar (spec §1.2, §6.1)
//     3m HTF with EMA 2/4. The forming 3m bar on Apr 29 rallies hard enough to
//     flip the gate if it were consulted — assert the signal stays blocked.
// ═════════════════════════════════════════════════════════════════════════════
TEST(mtf_gate_no_lookahead) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "sweep";
    cfg.use_mtf = true;
    cfg.htf_mult = 3;            // 3m HTF (test-scale stand-in for 12m)
    cfg.htf_fast_len = 2; cfg.htf_slow_len = 4;
    cfg.use_levels = true;
    cfg.lv_sess = false; cfg.lv_pdc = false;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // W0 (Apr 23, day -7): week low 124.0; day high 145.0 so the W1D1 decline
    // (highs ≤ 140.3) never pokes PDH/PWH — isolates the PDL/PWL sweeps below
    feed_bar(s, -7, 600, 130.2, 145.0, 124.0, 130.5);
    for (int i = 1; i < 6; ++i) feed_bar(s, -7, 600 + i, 130.5, 130.9, 129.8, 130.5);

    // W1D1 (Apr 28, day -2): six declining 3m buckets → HTF bearish;
    // day low 124.7 (bars 615/616); last bucket upticks so the 1m trend
    // is long again by Apr 29 (isolates the MTF gate as the only blocker).
    const double cl[18] = {139.5, 140.0, 140.0, 136.5, 136.4, 136.0,
                           132.5, 132.4, 132.0, 128.5, 128.4, 128.0,
                           126.5, 126.4, 126.0, 125.0, 126.2, 126.4};
    for (int i = 0; i < 18; ++i) {
        const double c = cl[i];
        const double o = (i == 0) ? 139.2 : cl[i - 1];
        const double l = (i == 1) ? 125.0 : std::min(o, c) - 0.3;
        feed_bar(s, -2, 600 + i, o, std::max(o, c) + 0.3, l, c);
    }

    // Apr 29 (day -1): PDL = 124.7 (Apr 28 low), PWL = 124.0 (W0 low)
    feed_bar(s, -1, 600, 126.4, 131.0, 126.2, 131.0);   // rally, no sweep
    flush(s, -1, 601, 131.0);
    ASSERT_NEAR(s.pdl(), 124.7, 1e-9);
    ASSERT_NEAR(s.pwl(), 124.0, 1e-9);
    ASSERT(s.htf_dn());                                  // last completed 3m bearish

    // PDL sweep while the forming 3m bar is rallying (would flip the gate if
    // the forming bar were read) — must stay blocked.
    feed_bar(s, -1, 601, 131.0, 131.4, 124.5, 131.2);   // low 124.5 < PDL 124.8
    flush(s, -1, 602, 131.2);
    ASSERT(sigs.empty());                                // no lookahead

    // Complete the rally bucket, then poke PWL → gate now legitimately open
    feed_bar(s, -1, 602, 131.2, 136.2, 131.0, 136.0);
    feed_bar(s, -1, 603, 136.0, 136.4, 123.5, 136.2);   // low 123.5 < PWL 124.0
    flush(s, -1, 604, 136.2);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("sweep_long"));
}

// ═════════════════════════════════════════════════════════════════════════════
// 13. Session: entries only inside 0900-1200 ET; flatAtEnd flattens at close
// ═════════════════════════════════════════════════════════════════════════════
TEST(session_window_and_flat_at_end) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_session = true;                 // "0900-1200" ET default
    cfg.flat_at_session_end = true;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // 08:00 ET — outside the window → blocked even with a clean flag break
    feed_flag_long(s, 0, 480, 100.0);
    flush(s, 0, 492, 110.5);
    ASSERT(sigs.empty());

    // 10:00 ET — inside → fires
    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);

    feed_flat(s, 0, 612, 109.0);            // kill stale retest arm
    flush(s, 0, 613, 109.0);

    // Window close: bars through 11:59 stay quiet, the 12:00 bar flattens
    for (int m = 715; m <= 719; ++m) feed_flat(s, 0, m, 110.4);
    ASSERT_EQ(sigs.size(), (size_t)1);      // still only the 10:00 entry
    feed_flat(s, 0, 720, 110.4);
    flush(s, 0, 721, 110.4);
    ASSERT_EQ(sigs.size(), (size_t)2);
    ASSERT(sigs[1].signal == OrbSignal::FLATTEN_EOD);
    ASSERT_EQ(sigs[1].reason, std::string("Session End / Out of Range"));

    // After the close: entries still blocked
    s.notify_trade_filled(OrbSignal::BUY, "Session End / Out of Range", 0.0);
    feed_flag_long(s, 0, 730, 100.0);
    flush(s, 0, 742, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)2);
}

// ═════════════════════════════════════════════════════════════════════════════
// 14. Warmup gate: inert until warmup_bars completed 1m bars
// ═════════════════════════════════════════════════════════════════════════════
TEST(warmup_blocks_signals) {
    MtfScalperConfig cfg = base_cfg();
    cfg.warmup_bars = 50;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(!s.warmed_up());                  // 12 < 50
    ASSERT(sigs.empty());

    feed_flat(s, 0, 612, 109.0);             // kill the retest arm set pre-warmup
    for (int i = 0; i < 40; ++i) feed_flat(s, 0, 613 + i, 110.5);
    flush(s, 0, 653, 110.5);
    ASSERT(s.warmed_up());
    feed_flag_long(s, 0, 660, 100.0);
    flush(s, 0, 672, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 15. Config JSON parsing (spec §8 schema)
// ═════════════════════════════════════════════════════════════════════════════
TEST(config_from_json_string) {
    const std::string json = R"({
        "_comment": "mtf scalper test",
        "auto_mode_flag_fix": false,
        "allow_longs": true, "allow_shorts": false,
        "ema_fast_len": 9, "ema_slow_len": 21,
        "use_mtf": true, "htf_mode": "auto", "htf_mult": 12,
        "trigger_mode": "auto",
        "session_window": "0930-1145", "day_rollover_hour_et": 18,
        "warmup_bars": 1500, "risk_pct": 0.5, "min_qty": 2,
        "use_smt_entry": false, "reference_symbol": "",
        "nested": { "warmup_bars": 1 }
    })";
    MtfScalperConfig c = MtfScalperConfig::from_json_string(json);
    ASSERT(!c.auto_mode_flag_fix);
    ASSERT(c.allow_longs && !c.allow_shorts);
    ASSERT_EQ(c.htf_minutes(), 12);
    ASSERT_EQ(c.warmup_bars, 1500);          // nested key must not shadow
    ASSERT_EQ(c.min_qty, 2);
    ASSERT_NEAR(c.risk_pct, 0.5, 1e-12);
    ASSERT_EQ(c.session_window, std::string("0930-1145"));
    ASSERT(!c.use_smt_entry);
    // untouched defaults
    ASSERT_EQ(c.pole_recency_bars, 7);
    ASSERT_EQ(c.max_hold_bars, 20);
}

// ═════════════════════════════════════════════════════════════════════════════
// 17. SMT / intermarket module (spec §1.8, Pine :994-1040)
// ═════════════════════════════════════════════════════════════════════════════
// Chart makes a lower pivot low while the reference makes a HIGHER pivot low
// (sell-side SMT) → long trigger. smt_pivot_len=2 keeps it compact: a pivot at
// bar c is confirmed when bar c+2 completes. Closes rise steadily so the trend
// gate (EMA2>EMA3, close>EMA2) stays up throughout.
static void feed_smt_divergence(MtfScalperStrategy& s, bool feed_ref) {
    for (int m = 0; m <= 11; ++m) {
        const double c = 100.0 + 0.4 * m;
        double lo = c - 0.1;
        if (m == 3) lo = 99.5;    // chart pivot low 1
        if (m == 9) lo = 98.9;    // chart pivot low 2 — LOWER (chart sweeps)
        feed_bar(s, 0, 600 + m, c - 0.05, c + 0.1, lo, c);
        if (feed_ref) {
            const double rc = 500.0 + 0.2 * m;
            double rlo = rc - 0.1;
            if (m == 3) rlo = 498.5;   // ref pivot low 1
            if (m == 9) rlo = 498.9;   // ref pivot low 2 — HIGHER (ref refuses)
            s.on_reference_bar(rc + 0.1, rlo, rc);
        }
    }
}

TEST(smt_bull_divergence_fires_long) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "smt";
    cfg.use_smt_entry = true;
    cfg.reference_symbol = "ES";
    cfg.smt_pivot_len = 2;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_smt_divergence(s, true);
    ASSERT(sigs.empty());            // nothing before bar 11 completes
    flush(s, 0, 612, 104.5);         // completes bar 11 → pivot c=9 evaluated
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
    ASSERT_EQ(sigs[0].reason, std::string("smt_long"));
}

TEST(smt_inert_without_reference_feed) {
    MtfScalperConfig cfg = base_cfg();
    cfg.trigger_mode = "smt";
    cfg.use_smt_entry = true;
    cfg.reference_symbol = "";       // no feed wired → SMT must never fire
    cfg.smt_pivot_len = 2;
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_smt_divergence(s, false);   // identical chart path, no ref bars
    flush(s, 0, 612, 104.5);
    ASSERT(sigs.empty());
    ASSERT(!s.wants_reference_feed());
}

TEST(im_filter_blocks_uncorrelated_reference) {
    MtfScalperConfig cfg = base_cfg();   // flag_any mode
    cfg.use_im_filter = true;
    cfg.im_corr_len = 10;
    cfg.im_min_corr = 0.3;
    cfg.reference_symbol = "ES";
    cfg.im_expected_corr = "positive";
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Alternating ref closes vs a rising chart → |corr| ≈ 0 → gate blocks
    for (int m = 600; m <= 611; ++m) {
        double rc = (m % 2 == 0) ? 500.0 : 500.4;
        s.on_reference_bar(rc + 0.2, rc - 0.2, rc);
    }
    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(sigs.empty());
}

TEST(im_filter_passes_correlated_reference) {
    MtfScalperConfig cfg = base_cfg();
    cfg.use_im_filter = true;
    cfg.im_corr_len = 10;
    cfg.im_min_corr = 0.3;
    cfg.reference_symbol = "ES";
    cfg.im_expected_corr = "positive";   // longs need the reference trending up
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    // Steadily rising ref vs rising chart → corr ≈ +1, ref EMAs up → passes
    for (int m = 600; m <= 611; ++m) {
        double rc = 500.0 + 0.25 * (m - 600);
        s.on_reference_bar(rc + 0.2, rc - 0.2, rc);
    }
    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(sigs[0].signal == OrbSignal::BUY);
}

// ═════════════════════════════════════════════════════════════════════════════
// Live-executor host-compatibility shims (mtf_scalper_strategy.hpp additions for
// run_executor<Strategy> in executor_main.cpp — pure host glue, no decision logic).
// ═════════════════════════════════════════════════════════════════════════════

TEST(host_session_reflects_state) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    auto sess0 = s.session();
    ASSERT_EQ(sess0.trades_today, 0);
    ASSERT(!sess0.in_position);
    ASSERT(!sess0.risk_halted);
    ASSERT(sess0.halt_reason.empty());
    ASSERT(!sess0.orb_set);   // mtf_scalper has no ORB concept — always false

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT_EQ(sigs.size(), (size_t)1);
    ASSERT(s.session().in_position);
    ASSERT_EQ(s.session().trades_today, 1);
}

TEST(host_halt_reason_set_and_cleared) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    s.halt_trading("pg_feed_stale");
    ASSERT(s.session().risk_halted);
    ASSERT_EQ(s.session().halt_reason, std::string("pg_feed_stale"));

    s.unhalt_trading("pg feed resumed");
    ASSERT(!s.session().risk_halted);
    ASSERT(s.session().halt_reason.empty());
}

// reset_session() must clear a halt — matches OrbStrategy/TrendStrategy (a halt set
// for an external condition does not carry across a fresh trading day; the host
// re-evaluates and re-halts at startup if still true).
TEST(reset_session_clears_halt) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    s.halt_trading("startup_stale_position_LONG");
    ASSERT(s.session().risk_halted);

    s.reset_session();
    ASSERT(!s.session().risk_halted);
    ASSERT(s.session().halt_reason.empty());
}

TEST(check_eod_forwards_to_check_time_flatten) {
    MtfScalperConfig cfg = base_cfg();   // base_cfg() turns use_session off; re-enable it
    cfg.use_session = true;
    cfg.flat_at_session_end = true;
    cfg.session_window = "0900-1200";
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(s.in_position());

    s.check_eod(13, 0);   // past session end (12:00 ET) — flattens like check_time_flatten
    ASSERT_EQ(sigs.back().signal, OrbSignal::FLATTEN_EOD);
}

TEST(seed_trades_today_sets_counter) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    s.seed_trades_today(3);
    ASSERT_EQ(s.trades_today(), 3);
    ASSERT_EQ(s.session().trades_today, 3);
}

TEST(orb_compat_stubs_are_inert) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    ASSERT_NEAR(s.orb_high(), 0.0, 0.0001);
    ASSERT_NEAR(s.orb_low(),  0.0, 0.0001);
    ASSERT(!s.orb_set());
}

// The live executor releases the engine with a direction-less notification after
// the warm-up replay (and when the order manager never acted on a signal). It must
// clear the leg the strategy believes it holds — a matching-direction-only rule left
// the engine "in position" for the whole session (2026-09-25 dry-run smoke test).
TEST(directionless_notify_releases_position) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    feed_flag_long(s, 0, 600, 100.0);
    flush(s, 0, 612, 110.5);
    ASSERT(s.in_position());
    ASSERT(!std::isnan(s.cur_tp()));

    // Wrong-direction leg report (reversal semantics) must NOT clear the open long…
    s.notify_trade_filled(OrbSignal::SELL, "stop");
    ASSERT(s.in_position());

    // …but the host's direction-less release always does.
    s.notify_trade_filled(OrbSignal::FLATTEN_EOD, "warmup_replay");
    ASSERT(!s.in_position());
    ASSERT(!s.exit_pending());
    ASSERT(std::isnan(s.cur_tp()));
    ASSERT(std::isnan(s.cur_stop()));
}

// allow_flips=false (the live setting): an opposite flag while positioned does NOT
// reverse the leg — the live executor cannot reverse a position and would otherwise
// push the new leg's stop onto the old one. Default true keeps paper parity.
TEST(allow_flips_false_blocks_reversal_entry) {
    {
        MtfScalperConfig cfg = base_cfg();   // default: flips allowed (paper)
        std::vector<CapturedSignal> sigs;
        MtfScalperStrategy s = make_strategy(cfg, sigs);
        feed_flag_long(s, 0, 600, 100.0);  flush(s, 0, 612, 110.5);
        ASSERT_EQ(sigs.size(), (size_t)1);
        feed_flag_bear(s, 0, 613, 110.5);  flush(s, 0, 625, 100.0);
        ASSERT(sigs.size() >= 2);
        ASSERT(sigs.back().signal == OrbSignal::SELL);
        ASSERT_EQ(s.pos_dir(), -1);
    }
    {
        MtfScalperConfig cfg = base_cfg();
        cfg.allow_flips = false;
        std::vector<CapturedSignal> sigs;
        MtfScalperStrategy s = make_strategy(cfg, sigs);
        feed_flag_long(s, 0, 600, 100.0);  flush(s, 0, 612, 110.5);
        ASSERT_EQ(sigs.size(), (size_t)1);
        feed_flag_bear(s, 0, 613, 110.5);  flush(s, 0, 625, 100.0);
        ASSERT_EQ(sigs.size(), (size_t)1);   // no flip
        ASSERT_EQ(s.pos_dir(), +1);          // still long
    }
}

// A direction-less release (order rejected / warm-up end) is not a trade: it must not
// touch the loss-streak cooldown or equity.
TEST(directionless_release_keeps_guardrail_accounting) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);
    feed_flag_long(s, 0, 600, 100.0);  flush(s, 0, 612, 110.5);
    s.notify_trade_filled(OrbSignal::BUY, "stop", -50.0);
    ASSERT_EQ(s.loss_streak(), 1);
    const double eq = s.equity();

    s.notify_trade_filled(OrbSignal::FLATTEN_EOD, "not_executed:no_order");
    ASSERT_EQ(s.loss_streak(), 1);
    ASSERT_NEAR(s.equity(), eq, 1e-9);
    ASSERT(!s.in_position());
}

TEST(last_price_tracks_ticks) {
    MtfScalperConfig cfg = base_cfg();
    std::vector<CapturedSignal> sigs;
    MtfScalperStrategy s = make_strategy(cfg, sigs);

    s.on_tick({bar_ts_us(0, 600, 0), 101.25, 1, true});
    ASSERT_NEAR(s.last_price(), 101.25, 0.0001);
}

// ═════════════════════════════════════════════════════════════════════════════
int main() {
    RUN(indicators_match_reference_250_bars);
    RUN(indicators_constant_series);
    RUN(bull_flag_break_fires_long);
    RUN(bear_flag_break_fires_short);
    RUN(retest_arms_then_fires);
    RUN(retest_invalidated_on_close_beyond_inval);
    RUN(retest_expires_after_max_bars);
    RUN(retest_invalidated_on_trend_lost);
    RUN(onset_flip_arm_and_pullback_trigger);
    RUN(onset_structural_break_trigger);
    RUN(onset_window_expires);
    RUN(fvg_creation_and_retest_one_shot);
    RUN(fvg_min_size_filter);
    RUN(fvg_expires_after_max_bars);
    RUN(fvg_per_side_cap_retires_oldest);
    RUN(sweep_fires_once_per_day);
    RUN(regime_blocks_when_components_fail);
    RUN(atr_floor_blocks_low_rank);
    RUN(auto_mode_flag_fix_off_reproduces_pine_bug);
    RUN(auto_mode_flag_fix_on_fires);
    RUN(breakeven_latch_at_exactly_entry_and_ratchet);
    RUN(time_stop_flattens_after_max_hold_bars);
    RUN(osc_exit_on_stoch_crossunder_above_80);
    RUN(trend_exit_on_close_below_fast_ema);
    RUN(trailing_stop_arms_and_retires_tp);
    RUN(sizing_floor_clamp_cap);
    RUN(sizing_atr_mode_uses_live_atr);
    RUN(cooldown_blocks_after_two_losses);
    RUN(cooldown_win_resets_streak);
    RUN(mtf_gate_no_lookahead);
    RUN(session_window_and_flat_at_end);
    RUN(warmup_blocks_signals);
    RUN(smt_bull_divergence_fires_long);
    RUN(smt_inert_without_reference_feed);
    RUN(im_filter_blocks_uncorrelated_reference);
    RUN(im_filter_passes_correlated_reference);
    RUN(config_from_json_string);

    RUN(host_session_reflects_state);
    RUN(host_halt_reason_set_and_cleared);
    RUN(reset_session_clears_halt);
    RUN(check_eod_forwards_to_check_time_flatten);
    RUN(seed_trades_today_sets_counter);
    RUN(orb_compat_stubs_are_inert);
    RUN(directionless_notify_releases_position);
    RUN(allow_flips_false_blocks_reversal_entry);
    RUN(directionless_release_keeps_guardrail_accounting);
    RUN(last_price_tracks_ticks);

    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
