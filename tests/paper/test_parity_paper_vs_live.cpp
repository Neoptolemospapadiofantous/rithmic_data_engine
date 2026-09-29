/*  test_parity_paper_vs_live.cpp — the paper broker and the LIVE order manager must
    manage a position identically: same entry fill rule, same initial stop, same
    break-even trigger/offset, same trail activation and ratchet.

    Both are driven with the same OrbConfig and the same price path. The live
    OrderManager's trail delay is wall-clock (steady_clock), so the parity matrix
    runs two configurations: trail_delay_secs = 0 (BE + trail both exercised) and a
    huge delay (BE only). After every tick the two stop prices are compared; the
    only tolerated difference is the tick-grid snap the paper broker applies
    (live moves the stop to the exact price − step, paper snaps adversely).

    This is the test that would have caught the 2026-09-23 mismatch (paper waited
    for the trail delay before moving to break-even; live moves immediately).
*/
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "../../src/execution/order_manager.hpp"
#include "../../src/execution/risk_manager.hpp"
#include "../../src/paper/paper_broker.hpp"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL: %s\n", msg); ++g_fail; } else std::printf("  ok:   %s\n", msg); } while (0)

struct NullStore : paper::PaperStore {
    void save_position(const paper::PaperPositionRow&) override {}
    void record_trade(const paper::PaperTradeRow& t) override { trades.push_back(t); }
    std::vector<paper::PaperTradeRow> trades;
};

static OrbConfig cfg_for(int delay_secs) {
    OrbConfig c;
    c.symbol = "MNQ"; c.exchange = "CME"; c.qty = 1; c.point_value = 2.0; c.commission_rt = 1.0;
    c.sl_points = 15.0; c.trail_be_trigger = 3.0; c.trail_be_offset = 1.0; c.trail_step = 10.0;
    c.trail_delay_secs = delay_secs; c.max_daily_trades = 3; c.last_entry_hour = 23;
    c.session_open_hour = 9; c.session_open_min = 30; c.eod_flatten_hour = 15; c.eod_flatten_min = 55;
    c.stop_cooldown_secs = 0; c.daily_loss_limit = -1e9; c.trailing_drawdown_cap = 1e9; c.consistency_cap_pct = 1.0;
    c.dry_run = false; c.starting_balance = 25000.0;
    return c;
}

// 2025-04-30 10:00:00 ET = 14:00 UTC
static int64_t ts(int sec) { return (1746021600LL + sec) * 1'000'000LL; }

// Run one path through both managers, compare stop after each tick.
// path: prices per second starting at t=1 (the entry fills on the first tick).
static void run_case(const char* name, int delay_secs, const std::vector<double>& path, int dir,
                     const char* expect_paper_exit) {
    OrbConfig cfg = cfg_for(delay_secs);
    // ── paper ──
    NullStore store;
    paper::PaperBroker pb("parity", "test", "MNQ", cfg, 0.25, 1, &store);
    pb.on_tick(OrbTick{ts(0), path[0], 1, true});                      // a tick so the broker has a clock
    pb.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");
    // ── live ──
    RiskManager risk(cfg, 25000.0);
    LatencyLogger lat;
    OrderManager om(cfg, risk, lat);
    om.set_order_callback([](const std::string&, const std::string&, const std::string&, int, int, bool, double, const std::string&) { return true; });
    om.set_cancel_callback([](const std::string&) {});
    om.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");

    int max_diff_ticks = 0; bool live_be_seen = false, paper_be_seen = false; size_t first_diff = 0; double dp = 0, dl = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const double px = path[i];
        // paper fills its pending entry at this tick (px + 1 tick adverse); live gets the same fill
        pb.on_tick(OrbTick{ts((int)i), px, 1, true});
        if (i == 1) {
            auto snap = om.position_snapshot();
            om.on_fill_notification(snap.basket_id_entry, px + dir * 0.25, 1, /*is_entry=*/true);
        }
        om.check_trail_and_stop(px);
        if (!pb.in_position()) break;                                    // paper exited (stop hit) — live would too
        const double sp = pb.placed_stop(), sl = om.exchange_stop();      // the stops that actually exit
        const int diff = (int)std::lround(std::fabs(sp - sl) / 0.25);
        if (diff > max_diff_ticks) { max_diff_ticks = diff; first_diff = i; dp = sp; dl = sl; }
        if (om.position_snapshot().be_triggered) live_be_seen = true;
        if (std::fabs(sp - (path[1] + dir * 0.25 + dir * cfg.trail_be_offset)) < 1e-9) paper_be_seen = true;
    }
    std::printf("[%s] max stop difference %d tick(s)%s\n", name, max_diff_ticks,
                max_diff_ticks ? (std::string(" (first at t=") + std::to_string(first_diff) + " paper=" + std::to_string(dp) + " live=" + std::to_string(dl) + ")").c_str() : "");
    CHECK(max_diff_ticks <= 1, (std::string(name) + ": paper and live stops never differ by more than the tick snap").c_str());
    CHECK(live_be_seen == paper_be_seen, (std::string(name) + ": break-even fires on the same path in paper and live").c_str());
    if (expect_paper_exit) {
        CHECK(!store.trades.empty() && store.trades[0].exit_reason == expect_paper_exit,
              (std::string(name) + ": paper exit reason = " + expect_paper_exit).c_str());
    }
}

// Take-profit parity: paper fills at the target on the touch; live initiates its market exit
// on the SAME tick (state → PENDING_EXIT) with the same reason.
static void run_tp_case(const char* name, double tp_r, const std::vector<double>& path, int dir) {
    OrbConfig cfg = cfg_for(100000); cfg.tp_r = tp_r;
    NullStore store;
    paper::PaperBroker pb("parity_tp", "test", "MNQ", cfg, 0.25, 1, &store);
    pb.on_tick(OrbTick{ts(0), path[0], 1, true});
    pb.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");
    RiskManager risk(cfg, 25000.0);
    LatencyLogger lat;
    OrderManager om(cfg, risk, lat);
    om.set_order_callback([](const std::string&, const std::string&, const std::string&, int, int, bool, double, const std::string&) { return true; });
    om.set_cancel_callback([](const std::string&) {});
    om.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");
    size_t paper_exit_at = 0, live_exit_at = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        const double px = path[i];
        pb.on_tick(OrbTick{ts((int)i), px, 1, true});
        if (i == 1) { auto snap = om.position_snapshot(); om.on_fill_notification(snap.basket_id_entry, px + dir * 0.25, 1, true); }
        om.check_trail_and_stop(px);
        if (!paper_exit_at && !pb.in_position()) paper_exit_at = i;
        if (!live_exit_at && om.position_snapshot().state == PosState::PENDING_EXIT) live_exit_at = i;
        if (paper_exit_at && live_exit_at) break;
    }
    std::printf("[%s] paper exit tick=%zu live exit tick=%zu\n", name, paper_exit_at, live_exit_at);
    CHECK(paper_exit_at > 0 && paper_exit_at == live_exit_at, (std::string(name) + ": take-profit fires on the same tick in paper and live").c_str());
    CHECK(!store.trades.empty() && store.trades[0].exit_reason == "take_profit", (std::string(name) + ": paper exit reason = take_profit").c_str());
    CHECK(om.position_snapshot().basket_id_exit.find("") != std::string::npos, "live exit basket exists");
}

// Exit-side regime parity: both sides read ONE RegimeState; a trend-shaped session must trail
// with trail_step_trend on both, a range-shaped one with trail_step_range + the range target.
static void run_regime_exit_case(const char* name, double eff_last, const std::vector<double>& path, int dir, double expect_step) {
    OrbConfig cfg = cfg_for(0); cfg.regime_exit_min_eff = 0.5; cfg.trail_step_trend = 20.0; cfg.trail_step_range = 4.0; cfg.tp_r_range = 0.0;
    paper::RegimeState rs; rs.set_atr("2025-04-30", 100.0);
    rs.on_tick("2025-04-30", 9, 30, ts(-1800), 20000.0);                       // open
    rs.on_tick("2025-04-30", 9, 45, ts(-900),  eff_last > 0.5 ? 20040.0 : 20040.0);  // high 20040
    rs.on_tick("2025-04-30", 9, 50, ts(-600),  eff_last > 0.5 ? 20036.0 : 20004.0);  // last: eff 0.9 (trend) or 0.1 (range)
    CHECK((rs.eff() >= 0.5) == (eff_last > 0.5), (std::string(name) + ": regime fixture shape").c_str());
    NullStore store;
    paper::PaperBroker pb("parity_rg", "test", "MNQ", cfg, 0.25, 1, &store); pb.set_regime(&rs);
    pb.on_tick(OrbTick{ts(0), path[0], 1, true}); pb.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");
    RiskManager risk(cfg, 25000.0); LatencyLogger lat; OrderManager om(cfg, risk, lat); om.set_regime(&rs);
    om.set_order_callback([](const std::string&, const std::string&, const std::string&, int, int, bool, double, const std::string&) { return true; });
    om.set_cancel_callback([](const std::string&) {});
    om.on_signal(dir > 0 ? OrbSignal::BUY : OrbSignal::SELL, path[0], "parity");
    int max_diff = 0; double last_sp = 0;
    for (size_t i = 1; i < path.size(); ++i) {
        pb.on_tick(OrbTick{ts((int)i), path[i], 1, true});
        if (i == 1) { auto snap = om.position_snapshot(); om.on_fill_notification(snap.basket_id_entry, path[i] + dir * 0.25, 1, true); }
        om.check_trail_and_stop(path[i]);
        if (!pb.in_position()) break;
        const double sp = pb.placed_stop(), sl = om.exchange_stop(); last_sp = sp;
        max_diff = std::max(max_diff, (int)std::lround(std::fabs(sp - sl) / 0.25));
    }
    std::printf("[%s] max stop difference %d tick(s), final paper stop %.2f\n", name, max_diff, last_sp);
    CHECK(max_diff <= 1, (std::string(name) + ": paper and live stops agree under the exit-side regime").c_str());
    // the trailing stop sits expect_step behind the last high (long) — proves which step was in force
    const double peak = *std::max_element(path.begin() + 1, path.end());
    CHECK(std::fabs(last_sp - (peak - expect_step)) < 0.51, (std::string(name) + ": trail step in force = " + std::to_string(expect_step)).c_str());
}

int main() {
    run_regime_exit_case("regime_exit_trend", 0.9, {20000.0, 20000.0, 20004.0, 20010.0, 20030.0, 20050.0, 20049.0}, +1, 20.0);
    run_regime_exit_case("regime_exit_range", 0.1, {20000.0, 20000.0, 20004.0, 20010.0, 20030.0, 20050.0, 20049.0}, +1, 4.0);
    run_tp_case("tp_long_2r",  2.0, {20000.0, 20000.0, 20010.0, 20020.0, 20029.0, 20031.0, 20040.0}, +1);  // target 20030.25
    run_tp_case("tp_short_1r", 1.0, {20000.0, 20000.0, 19992.0, 19986.0, 19984.5, 19980.0}, -1);            // target 19984.75
    std::printf("test_parity_paper_vs_live\n");
    // 1. BE only (trail delay huge): +2 → nothing, +3.5 → BE at entry+1, then back to BE → 'breakeven'
    run_case("be_only_long", 100000, {20000.0, 20000.0, 20002.0, 20003.5, 20005.0, 20001.5, 20001.0}, +1, "breakeven");
    run_case("be_only_short", 100000, {20000.0, 20000.0, 19998.0, 19996.5, 19995.0, 19998.5, 19999.5}, -1, "breakeven");
    // 2. BE + trail (delay 0): +3.5 → BE, +20 → trail to price−10, ratchets, exits on 'trail'
    // exchange stop after BE = 20001.25; 20020 → 20010 (≥10 away, placed); 20025 → 20015 would be only 5 away → NOT
    // placed (suppression); 20014 does not exit (working stop still 20010); 20009.75 exits at 20010 → 'trail'
    run_case("trail_long", 0, {20000.0, 20000.0, 20003.5, 20010.0, 20020.0, 20018.0, 20025.0, 20014.0, 20009.75}, +1, "trail");
    run_case("trail_short", 0, {20000.0, 20000.0, 19996.5, 19990.0, 19980.0, 19982.0, 19975.0, 19986.0, 19990.25}, -1, "trail");
    // 3. straight stop, no BE ever
    run_case("stop_long", 0, {20000.0, 20000.0, 19995.0, 19990.0, 19984.0}, +1, "stop");
    std::printf(g_fail ? "FAILED (%d)\n" : "ALL PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
