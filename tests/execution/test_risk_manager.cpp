/*  ═══════════════════════════════════════════════════════════════════════════
    test_risk_manager.cpp — Unit tests for RiskManager (C3 audit finding)

    Standalone: no external test framework required.
    Build (from repo root):
        g++ -std=c++20 -Isrc/execution -Isrc \
            tests/execution/test_risk_manager.cpp \
            -o build/test_risk_manager && ./build/test_risk_manager

    CMake target suggestion (add to CMakeLists.txt):
        add_executable(test_risk_manager tests/execution/test_risk_manager.cpp)
        target_include_directories(test_risk_manager PRIVATE src/execution src/)
        target_compile_features(test_risk_manager PRIVATE cxx_std_20)
        add_test(NAME risk_manager_tests COMMAND test_risk_manager)
    ═══════════════════════════════════════════════════════════════════════════ */
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>

// RiskManager is header-only (risk_manager.hpp inlines all logic).
// orb_config.hpp and log.hpp are in src/execution/ — no libpq needed.
#include "../../src/execution/risk_manager.hpp"

// ─── Minimal test harness ─────────────────────────────────────────────────────
static int tests_run = 0, tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN(name) do { \
    ++tests_run; \
    try { test_##name(); std::cout << "PASS " #name "\n"; } \
    catch (std::exception& e) { ++tests_failed; std::cout << "FAIL " #name ": " << e.what() << "\n"; } \
} while(0)
#define ASSERT(cond) do { if (!(cond)) throw std::runtime_error("Assert failed: " #cond); } while(0)
#define ASSERT_EQ(a, b) do { if ((a) != (b)) throw std::runtime_error("ASSERT_EQ failed: " #a " != " #b); } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (std::abs((a)-(b)) > (eps)) throw std::runtime_error("ASSERT_NEAR failed: " #a " vs " #b); } while(0)

// ─── Helper: build a minimal OrbConfig for testing ───────────────────────────
static OrbConfig make_cfg(double daily_loss_limit  = -1000.0,
                          double trailing_dd_cap   = 2500.0,
                          double consistency_cap   = 0.30) {
    OrbConfig c;
    c.daily_loss_limit      = daily_loss_limit;
    c.trailing_drawdown_cap = trailing_dd_cap;
    c.consistency_cap_pct   = consistency_cap;
    return c;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Tests
// ═══════════════════════════════════════════════════════════════════════════════

// 1. No halt when P&L is within all limits
TEST(no_halt_within_limits) {
    OrbConfig cfg = make_cfg(-2000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-500.0);
    ASSERT(!rm.halted());
    ASSERT(rm.can_trade());
    ASSERT_NEAR(rm.daily_pnl(), -500.0, 0.001);
}

// 2. Halt when daily loss limit is breached (daily_pnl <= limit)
TEST(halt_on_daily_loss_limit) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-1001.0);  // strictly below the -1000 limit
    ASSERT(rm.halted());
    std::string reason;
    ASSERT(!rm.can_trade(reason));
    ASSERT(!reason.empty());
}

// 3. No halt when loss exactly equals the limit boundary (boundary inclusive check)
//    API: halts when daily_pnl_ <= daily_loss_limit, so exactly at limit IS a halt
TEST(halt_at_daily_loss_limit_boundary) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-1000.0);  // exactly at limit — should halt (<=)
    ASSERT(rm.halted());
}

// 4. NaN guard — non-finite PnL triggers immediate halt
TEST(halt_on_nan_pnl) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(std::numeric_limits<double>::quiet_NaN());
    ASSERT(rm.halted());
}

// 5. Inf guard — positive infinity also triggers halt
TEST(halt_on_inf_pnl) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(std::numeric_limits<double>::infinity());
    ASSERT(rm.halted());
}

// 6. Negative infinity also triggers halt
TEST(halt_on_neg_inf_pnl) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-std::numeric_limits<double>::infinity());
    ASSERT(rm.halted());
}

// 7. Trailing drawdown: equity drop from peak >= cap triggers halt
TEST(halt_on_trailing_drawdown) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);  // large daily limit so only dd fires
    RiskManager rm(cfg, 50000.0);
    // Grow peak to 52000, then drop 2500 points -> drawdown exactly 2500 -> halt
    rm.on_trade_pnl(2000.0);   // equity = 52000, peak = 52000
    ASSERT(!rm.halted());
    rm.on_trade_pnl(-2500.0);  // equity = 49500, drawdown from peak = 2500 >= 2500
    ASSERT(rm.halted());
}

// 8. Trailing drawdown does NOT halt when drawdown is just below cap
TEST(no_halt_below_trailing_drawdown_cap) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(2000.0);   // equity = 52000, peak = 52000
    rm.on_trade_pnl(-2499.0);  // equity = 49501, drawdown = 2499 < 2500
    ASSERT(!rm.halted());
}

// 9. Peak equity tracking — profitable trade raises the peak, subsequent loss
//    is measured from that higher peak
TEST(peak_equity_rises_after_profit) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    ASSERT_NEAR(rm.peak_equity(), 50000.0, 0.001);

    rm.on_trade_pnl(3000.0);
    ASSERT_NEAR(rm.peak_equity(), 53000.0, 0.001);
    ASSERT_NEAR(rm.equity(),      53000.0, 0.001);

    // Lose 1000 — peak stays at 53000, drawdown = 1000, no halt
    rm.on_trade_pnl(-1000.0);
    ASSERT_NEAR(rm.peak_equity(), 53000.0, 0.001);
    ASSERT_NEAR(rm.equity(),      52000.0, 0.001);
    ASSERT(!rm.halted());
}

// 10. Peak does not fall after a loss
TEST(peak_does_not_fall_on_loss) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(1000.0);  // peak = 51000
    rm.on_trade_pnl(-500.0);  // equity = 50500, peak still 51000
    ASSERT_NEAR(rm.peak_equity(), 51000.0, 0.001);
}

// 11. Daily reset clears daily_pnl and halted flag; peak and equity persist
TEST(reset_daily_clears_halt) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-1001.0);
    ASSERT(rm.halted());

    rm.reset_daily();
    ASSERT(!rm.halted());
    ASSERT(rm.can_trade());
    ASSERT_NEAR(rm.daily_pnl(), 0.0, 0.001);
    // Equity and peak are NOT reset — they persist across days
    ASSERT_NEAR(rm.equity(), 48999.0, 0.001);
}

// 12. Consistency cap: today's profit > 30% of prior cumulative profit triggers halt
//     Example: prior_profit = 1000, daily_pnl = 301 → 30.1% > 30% → halt
TEST(halt_on_consistency_cap) {
    OrbConfig cfg = make_cfg(-999999.0, 9999999.0, 0.30);
    RiskManager rm(cfg, 50000.0);
    // Seed 1000 of historical profit across prior days
    rm.seed_total_profit(1000.0);
    // Now trade today — 301 profit → daily_pnl/prior_profit = 301/1000 = 30.1% > 30%
    rm.on_trade_pnl(301.0);
    ASSERT(rm.halted());
}

// 13. Consistency cap not triggered when profit is within 30%
TEST(no_halt_under_consistency_cap) {
    OrbConfig cfg = make_cfg(-999999.0, 9999999.0, 0.30);
    RiskManager rm(cfg, 50000.0);
    rm.seed_total_profit(1000.0);
    rm.on_trade_pnl(299.0);  // 299/1000 = 29.9% < 30%
    ASSERT(!rm.halted());
}

// 14. Consistency cap not applied when prior_profit <= 0 (first trading day)
TEST(no_consistency_cap_on_first_day) {
    OrbConfig cfg = make_cfg(-999999.0, 9999999.0, 0.30);
    RiskManager rm(cfg, 50000.0);
    // total_profit_ starts at 0; after a +500 trade: prior_profit = 0 - 500 = -500 (no halt)
    rm.on_trade_pnl(500.0);
    ASSERT(!rm.halted());
}

// 15. set_equity updates equity and peak but does NOT trigger halt
TEST(set_equity_updates_peak_no_halt) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.set_equity(55000.0);
    ASSERT_NEAR(rm.equity(),      55000.0, 0.001);
    ASSERT_NEAR(rm.peak_equity(), 55000.0, 0.001);
    ASSERT(!rm.halted());
}

// 16. Multiple small losses accumulate: each within limit, but total exceeds it
TEST(cumulative_loss_breaches_daily_limit) {
    OrbConfig cfg = make_cfg(-1000.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-400.0);
    ASSERT(!rm.halted());
    rm.on_trade_pnl(-400.0);
    ASSERT(!rm.halted());
    rm.on_trade_pnl(-201.0);  // daily_pnl = -1001 -> halt
    ASSERT(rm.halted());
}

// ─── Audit finding 1: daily loss limit must survive intra-day restarts ────────

// 17. Restart seeding: a restarted process that already lost $490 today on a
//     $500-limit account must halt after only $10 more loss — not a fresh $500.
TEST(seed_daily_pnl_enforces_limit_after_restart) {
    OrbConfig cfg = make_cfg(-500.0, 999999.0);
    RiskManager rm(cfg, 25000.0);
    rm.seed_daily_pnl(-490.0);      // from OrbDB::seed_daily_pnl() at startup
    ASSERT_NEAR(rm.daily_pnl(), -490.0, 0.001);
    ASSERT(!rm.halted());           // -490 > -500: still allowed
    rm.on_trade_pnl(-15.0);         // daily_pnl = -505 <= -500 -> halt
    ASSERT(rm.halted());
}

// 18. Restart seeding keeps consistency-cap "prior profit" correct: total_profit
//     seeded from DB INCLUDES today's trades, so seeding daily_pnl excludes them
//     from "prior" instead of double-counting today as both prior and today.
TEST(seed_daily_pnl_excludes_today_from_consistency_prior) {
    OrbConfig cfg = make_cfg(-999999.0, 9999999.0, 0.30);
    RiskManager rm(cfg, 50000.0);
    // DB says: total lifetime profit 2000, of which 1000 was earned today.
    rm.seed_total_profit(2000.0);
    rm.seed_daily_pnl(1000.0);
    // True prior = 2000 - 1000 = 1000. Another 301 today -> daily=1301,
    // 1301/1000 = 130% > 30% -> halt. Without the daily seed, prior would be
    // wrongly computed as 2000 - 301 = 1699 and today would still be just 301.
    rm.on_trade_pnl(301.0);
    ASSERT(rm.halted());
    ASSERT(rm.halt_reason().rfind("consistency_cap", 0) == 0);
}

// 19. Already over the limit at seed time: first new signal must be rejected
//     even before any new trade closes.
TEST(can_trade_rejects_when_seeded_daily_pnl_over_limit) {
    OrbConfig cfg = make_cfg(-500.0, 999999.0);
    RiskManager rm(cfg, 25000.0);
    rm.seed_daily_pnl(-550.0);
    ASSERT(!rm.can_trade());
}

// ─── Audit finding 4: drawdown/consistency halts must persist past midnight ───

// 20. Daily-loss-limit halt IS cleared by reset_daily (it is a per-day rule).
TEST(reset_daily_clears_daily_loss_halt_only) {
    OrbConfig cfg = make_cfg(-1000.0, 999999.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(-1001.0);
    ASSERT(rm.halted());
    rm.reset_daily();
    ASSERT(!rm.halted());
    ASSERT(rm.can_trade());
}

// 21. Trailing-drawdown halt persists across reset_daily — prop-firm trailing
//     DD breach is account-killing, not a per-day rule.
TEST(reset_daily_preserves_drawdown_halt) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(2000.0);    // peak = 52000
    rm.on_trade_pnl(-2500.0);   // dd = 2500 >= cap -> halt
    ASSERT(rm.halted());
    rm.reset_daily();
    ASSERT(rm.halted());                    // still halted the next day
    ASSERT(!rm.can_trade());
    ASSERT(rm.halt_reason().rfind("trailing_drawdown_cap", 0) == 0);
    ASSERT_NEAR(rm.daily_pnl(), 0.0, 0.001); // daily accumulator still resets
}

// 22. Consistency-cap halt also persists across reset_daily.
TEST(reset_daily_preserves_consistency_halt) {
    OrbConfig cfg = make_cfg(-999999.0, 9999999.0, 0.30);
    RiskManager rm(cfg, 50000.0);
    rm.seed_total_profit(1000.0);
    rm.on_trade_pnl(301.0);     // 30.1% > 30% -> halt
    ASSERT(rm.halted());
    rm.reset_daily();
    ASSERT(rm.halted());
}

// 23. Manual clear_halt() releases a drawdown halt.
TEST(clear_halt_releases_drawdown_halt) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(2000.0);
    rm.on_trade_pnl(-2500.0);
    ASSERT(rm.halted());
    rm.reset_daily();
    ASSERT(rm.halted());
    rm.clear_halt();            // operator reviewed and reset
    ASSERT(!rm.halted());
    // can_trade still independently blocks: dd is still >= cap until new profits
    // raise equity — clear_halt only clears the latch, not the live check.
    ASSERT(!rm.can_trade());
}

// ─── Audit finding 5: drawdown check must see open-position unrealized P&L ────

// 24. Unrealized loss trips the can_trade() drawdown gate before the trade closes.
TEST(update_unrealized_blocks_new_entry_on_drawdown) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.on_trade_pnl(2000.0);        // equity = peak = 52000
    ASSERT(rm.can_trade());
    rm.update_unrealized(-2600.0);  // open position deep underwater
    ASSERT(!rm.can_trade());        // effective equity 49400 -> dd 2600 >= 2500
    rm.update_unrealized(-1000.0);  // position recovers
    ASSERT(rm.can_trade());
    rm.update_unrealized(0.0);      // flat again
    ASSERT(rm.can_trade());
}

// 25. Unrealized P&L does not move realized equity or peak, and does not halt.
TEST(update_unrealized_does_not_touch_realized_state) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.update_unrealized(-9999.0);
    ASSERT_NEAR(rm.equity(),      50000.0, 0.001);
    ASSERT_NEAR(rm.peak_equity(), 50000.0, 0.001);
    ASSERT(!rm.halted());
}

// 26. Non-finite unrealized P&L is ignored, not latched.
TEST(update_unrealized_ignores_nan) {
    OrbConfig cfg = make_cfg(-999999.0, 2500.0);
    RiskManager rm(cfg, 50000.0);
    rm.update_unrealized(std::numeric_limits<double>::quiet_NaN());
    ASSERT(rm.can_trade());
    ASSERT(!rm.halted());
}

// ─── Audit findings 3/6/8: config parser + validation ─────────────────────────

static std::string write_temp_config(const char* name, const std::string& body) {
    std::string path = std::string("/tmp/test_orb_cfg_") + name + ".json";
    std::ofstream f(path);
    f << body;
    f.close();
    return path;
}

// 27. Nested-object bleed (bulenox-style): "daily_loss_limit" only inside a
//     nested "prop_firm" object must NOT be read as a top-level key. Before the
//     fix this silently set daily_loss_limit=0.0, disabling the daily-loss check.
TEST(config_nested_object_keys_do_not_bleed) {
    std::string path = write_temp_config("nested", R"({
        "_comment": "Bulenox-style overrides",
        "starting_balance": 25000.0,
        "trailing_drawdown_cap": 1500.0,
        "prop_firm": {
            "name": "Bulenox 25K",
            "daily_loss_limit": 0.0,
            "max_daily_trades": 3
        }
    })");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_NEAR(c.daily_loss_limit, -1000.0, 0.001);  // default preserved
    ASSERT_NEAR(c.trailing_drawdown_cap, 1500.0, 0.001);
    ASSERT_EQ(c.max_daily_trades, 3);                  // same value by default — no bleed check
    ASSERT_NEAR(c.starting_balance, 25000.0, 0.001);
}

// 28. A "_comment" string quoting a key name must not shadow the real key.
TEST(config_comment_string_does_not_shadow_key) {
    std::string path = write_temp_config("comment", R"({
        "_comment": "set \"qty\": 99 and \"daily_loss_limit\": 0.0 to reproduce the bug",
        "qty": 2,
        "daily_loss_limit": -500.0
    })");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.qty, 2);                        // real key wins, not the quoted 99
    ASSERT_NEAR(c.daily_loss_limit, -500.0, 0.001);
}

// 29. Escaped quotes inside a string value must not truncate it.
TEST(config_string_value_with_escaped_quote) {
    std::string path = write_temp_config("escape",
        "{\n  \"trade_contract\": \"MNQ\\\"U6\"\n}\n");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.trade_contract, std::string("MNQ\"U6"));
}

// 30. New keys parse: strategy and commission_rt.
TEST(config_parses_strategy_and_commission_rt) {
    std::string path = write_temp_config("newkeys", R"({
        "strategy": "VWAP",
        "commission_rt": 4.0
    })");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.strategy, std::string("VWAP"));
    ASSERT_NEAR(c.commission_rt, 4.0, 0.001);
}

// 31. Defaults for strategy and commission_rt.
TEST(config_strategy_commission_defaults) {
    OrbConfig c;
    ASSERT_EQ(c.strategy, std::string("ORB"));
    ASSERT_NEAR(c.commission_rt, 1.0, 0.001);
}

// 32. Numeric validation: qty <= 0 throws a FATAL naming the key.
TEST(config_validation_rejects_zero_qty) {
    std::string path = write_temp_config("badqty", "{ \"qty\": 0 }");
    bool threw = false;
    try {
        OrbConfig c = OrbConfig::from_file(path);
        (void)c;
    } catch (std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        ASSERT(msg.find("FATAL") != std::string::npos);
        ASSERT(msg.find("qty") != std::string::npos);
    }
    std::remove(path.c_str());
    ASSERT(threw);
}

// 33. Numeric validation: negative sl_points throws.
TEST(config_validation_rejects_negative_sl_points) {
    std::string path = write_temp_config("badsl", "{ \"sl_points\": -5.0 }");
    bool threw = false;
    try {
        OrbConfig c = OrbConfig::from_file(path);
        (void)c;
    } catch (std::runtime_error& e) {
        threw = true;
        ASSERT(std::string(e.what()).find("sl_points") != std::string::npos);
    }
    std::remove(path.c_str());
    ASSERT(threw);
}

// 34. Numeric validation: zero trailing_drawdown_cap throws.
TEST(config_validation_rejects_zero_drawdown_cap) {
    std::string path = write_temp_config("baddd", "{ \"trailing_drawdown_cap\": 0.0 }");
    bool threw = false;
    try {
        OrbConfig c = OrbConfig::from_file(path);
        (void)c;
    } catch (std::runtime_error& e) {
        threw = true;
        ASSERT(std::string(e.what()).find("trailing_drawdown_cap") != std::string::npos);
    }
    std::remove(path.c_str());
    ASSERT(threw);
}

// 35. account_label charset: SQL-unsafe labels rejected at load.
TEST(config_validation_rejects_unsafe_account_label) {
    std::string path = write_temp_config("badlabel",
        "{ \"account_label\": \"x'; DROP TABLE live_trades;--\" }");
    bool threw = false;
    try {
        OrbConfig c = OrbConfig::from_file(path);
        (void)c;
    } catch (std::runtime_error& e) {
        threw = true;
        ASSERT(std::string(e.what()).find("account_label") != std::string::npos);
    }
    std::remove(path.c_str());
    ASSERT(threw);
}

// 36. A valid full config loads cleanly end-to-end.
TEST(config_valid_file_loads) {
    std::string path = write_temp_config("valid", R"({
        "account_label": "tradeify1",
        "strategy": "ORB",
        "qty": 2,
        "orb_minutes": 10,
        "sl_points": 12.0,
        "trail_step": 8.0,
        "trailing_drawdown_cap": 1000.0,
        "daily_loss_limit": -500.0,
        "commission_rt": 1.04,
        "dry_run": true
    })");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    // dry_run=true → the label carries the "_dry" suffix (test 38): simulated fills
    // must never be written under a live label.
    ASSERT_EQ(c.account_label, std::string("tradeify1_dry"));
    ASSERT_EQ(c.qty, 2);
    ASSERT(c.dry_run);
    ASSERT_NEAR(c.commission_rt, 1.04, 0.0001);
}


// 38. dry_run rows never land in the live history: the label gets a "_dry" suffix.
TEST(config_dry_run_forces_dry_label) {
    std::string path = write_temp_config("drylabel", R"({
        "account_label": "tradeify",
        "dry_run": true
    })");
    OrbConfig c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.account_label, std::string("tradeify_dry"));
    ASSERT(c.dry_label_forced);
    ASSERT(!c.apply_dry_run_label());                     // idempotent
    ASSERT_EQ(OrbConfig::base_label(c.account_label), std::string("tradeify"));

    path = write_temp_config("drylabel2", R"({ "account_label": "tradeify_dry", "dry_run": true })");
    c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.account_label, std::string("tradeify_dry"));
    ASSERT(!c.dry_label_forced);

    path = write_temp_config("livelabel", R"({ "account_label": "tradeify", "dry_run": false })");
    c = OrbConfig::from_file(path);
    std::remove(path.c_str());
    ASSERT_EQ(c.account_label, std::string("tradeify"));    // live labels untouched
    ASSERT_EQ(OrbConfig::base_label("tradeify"), std::string("tradeify"));
    ASSERT_EQ(OrbConfig::base_label("_dry"), std::string("_dry"));   // no empty base
}

// ═══════════════════════════════════════════════════════════════════════════════
// ─── Broker room guard (2026-10-01: account $321.76 above the Tradeify floor on 1 NQ) ───

static OrbConfig make_nq_cfg() {
    OrbConfig c = make_cfg(-500.0, 1000.0);
    c.sl_points = 15.0; c.point_value = 20.0; c.qty = 1; c.commission_rt = 1.82;
    return c;
}

// One full stop on 1 NQ = 15 x 20 + 1.82 fees.
TEST(entry_risk_is_full_stop_plus_fees) {
    RiskManager rm(make_nq_cfg(), 25000.0);
    ASSERT_NEAR(rm.entry_risk_usd(), 301.82, 0.001);
}

// No broker balance yet (dry run / PnL plant not up) -> the guard stays out of the way.
TEST(broker_room_unset_does_not_block) {
    RiskManager rm(make_nq_cfg(), 25000.0);
    ASSERT(std::isnan(rm.broker_room()));
    ASSERT(rm.can_trade());
}

// The 2026-10-01 numbers: 321.76 covers one stop; after a loss (~12 left) the next entry is refused.
TEST(broker_room_blocks_entry_it_cannot_cover) {
    RiskManager rm(make_nq_cfg(), 25000.0);
    rm.set_broker_room(321.76);
    ASSERT(rm.can_trade());
    rm.set_broker_room(12.0);
    std::string why;
    ASSERT(!rm.can_trade(why));
    ASSERT(why.rfind("broker_room", 0) == 0);
    ASSERT(!rm.halted());              // a gate, not a halt
}

// Exactly one stop of room is enough; a cent less is not.
TEST(broker_room_boundary) {
    RiskManager rm(make_nq_cfg(), 25000.0);
    rm.set_broker_room(301.82);
    ASSERT(rm.can_trade());
    rm.set_broker_room(301.81);
    ASSERT(!rm.can_trade());
}

// Not sticky: a win that rebuilds the room re-opens entries; NaN updates are ignored.
TEST(broker_room_recovers_and_ignores_nan) {
    RiskManager rm(make_nq_cfg(), 25000.0);
    rm.set_broker_room(50.0);
    ASSERT(!rm.can_trade());
    rm.set_broker_room(std::nan(""));
    ASSERT_NEAR(rm.broker_room(), 50.0, 0.001);
    rm.set_broker_room(450.0);
    ASSERT(rm.can_trade());
}

int main() {
    RUN(no_halt_within_limits);
    RUN(halt_on_daily_loss_limit);
    RUN(halt_at_daily_loss_limit_boundary);
    RUN(halt_on_nan_pnl);
    RUN(halt_on_inf_pnl);
    RUN(halt_on_neg_inf_pnl);
    RUN(halt_on_trailing_drawdown);
    RUN(no_halt_below_trailing_drawdown_cap);
    RUN(peak_equity_rises_after_profit);
    RUN(peak_does_not_fall_on_loss);
    RUN(reset_daily_clears_halt);
    RUN(halt_on_consistency_cap);
    RUN(no_halt_under_consistency_cap);
    RUN(no_consistency_cap_on_first_day);
    RUN(set_equity_updates_peak_no_halt);
    RUN(cumulative_loss_breaches_daily_limit);
    // Finding 1: restart seeding
    RUN(seed_daily_pnl_enforces_limit_after_restart);
    RUN(seed_daily_pnl_excludes_today_from_consistency_prior);
    RUN(can_trade_rejects_when_seeded_daily_pnl_over_limit);
    // Finding 4: halt persistence across reset_daily
    RUN(reset_daily_clears_daily_loss_halt_only);
    RUN(reset_daily_preserves_drawdown_halt);
    RUN(reset_daily_preserves_consistency_halt);
    RUN(clear_halt_releases_drawdown_halt);
    // Finding 5: unrealized P&L in drawdown gate
    RUN(update_unrealized_blocks_new_entry_on_drawdown);
    RUN(update_unrealized_does_not_touch_realized_state);
    RUN(update_unrealized_ignores_nan);
    // Findings 3/6/8: config parser + validation
    RUN(config_nested_object_keys_do_not_bleed);
    RUN(config_comment_string_does_not_shadow_key);
    RUN(config_string_value_with_escaped_quote);
    RUN(config_parses_strategy_and_commission_rt);
    RUN(config_strategy_commission_defaults);
    RUN(config_validation_rejects_zero_qty);
    RUN(config_validation_rejects_negative_sl_points);
    RUN(config_validation_rejects_zero_drawdown_cap);
    RUN(config_validation_rejects_unsafe_account_label);
    RUN(config_valid_file_loads);
    RUN(config_dry_run_forces_dry_label);
    RUN(entry_risk_is_full_stop_plus_fees);
    RUN(broker_room_unset_does_not_block);
    RUN(broker_room_blocks_entry_it_cannot_cover);
    RUN(broker_room_boundary);
    RUN(broker_room_recovers_and_ignores_nan);

    std::cout << "\n" << (tests_run - tests_failed) << "/" << tests_run << " passed\n";
    return tests_failed > 0 ? 1 : 0;
}
