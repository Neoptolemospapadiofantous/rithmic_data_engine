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
    // ── config parsing ──────────────────────────────────────────────────────
    {
        auto c = TrendConfig::from_json_string(R"({"mode":"squeeze","tf_min":15,"win_start":935,"allow_shorts":false,"bb_mult":2.5})");
        CHECK(c.mode == "squeeze" && c.tf_min == 15 && c.win_start == 935 && !c.allow_shorts && std::fabs(c.bb_mult - 2.5) < 1e-9,
              "config: json fields parsed");
    }

    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
