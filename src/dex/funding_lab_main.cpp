// funding_lab — stage F0 of docs/funding_carry_plan.md: the pre-registered funding-carry backtest
// (research/preregistered/2026-10-28_funding_carry.md). Reads the history written by scripts/funding_history.sh.
//
//   funding_lab [--root DIR] [--out DIR] [--coverage]
//
// Runs the 12-cell grid on the TUNING half, picks the cell with the highest tuning net APR on capital (ties: fewer
// round trips), then scores the TEST half once, for that cell only. Writes <out>/report.md and <out>/results.json.
#include "dex/funding_lab.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <sstream>

using namespace flab;
namespace fs = std::filesystem;

static constexpr int64_t kT0 = 1761609600;      // 2025-10-28 00:00 UTC
static constexpr int64_t kSplit = 1777334400;   // 2026-04-28 00:00 UTC
static constexpr int64_t kT1 = 1793145600;      // 2026-10-28 00:00 UTC

static std::string pct(double x, int d = 1) { if (std::isnan(x)) return "—"; std::ostringstream o; o.setf(std::ios::fixed); o.precision(d); o << x * 100 << " %"; return o.str(); }
static std::string bps(double x) { std::ostringstream o; o.setf(std::ios::fixed); o.precision(1); o << x * 1e4; return o.str(); }
static std::string day(int64_t t) { char b[16]; std::time_t tt = t; std::strftime(b, sizeof b, "%Y-%m-%d", std::gmtime(&tt)); return b; }

static json summary_json(const Summary& s) {
    return {{"net_per_notional", s.net}, {"apr_on_avg_capital", s.apr_avg}, {"apr_on_peak_capital", s.apr_peak},
            {"avg_capital", s.avg_capital}, {"peak_capital", s.peak_capital}, {"round_trips", s.round_trips},
            {"avg_hold_h", s.avg_hold_h}, {"coins_traded", s.coins_traded}, {"coins_positive", s.coins_positive},
            {"blocks", s.blocks}, {"blocks_positive", s.blocks_positive}, {"net_without_best3", s.net_without_best3},
            {"funding", s.funding}, {"costs", s.costs}, {"by_pair", s.by_pair},
            {"venue_funding", {{"lighter", s.venue_funding[LT]}, {"hyperliquid", s.venue_funding[HL]}, {"binance", s.venue_funding[BN]}}},
            {"opposite_sign_share", std::isnan(s.opposite_share) ? json(nullptr) : json(s.opposite_share)}};
}

int main(int argc, char** argv) {
    fs::path root = "/media/theone/381C48C51C48803C/rithmic_dex/funding", out = "data/funding_lab";
    bool coverage_only = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--root" && i + 1 < argc) root = argv[++i];
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--coverage") coverage_only = true;
        else { std::fprintf(stderr, "usage: funding_lab [--root DIR] [--out DIR] [--coverage]\n"); return 2; }
    }
    if (!fs::exists(root / "coins.txt")) { std::fprintf(stderr, "%s/coins.txt missing — run scripts/funding_history.sh coins\n", root.c_str()); return 1; }
    auto coins = load(root);

    // coverage: share of expected hours present per venue (Binance: settlements ≥ 1 per 8 h)
    int complete = 0;
    std::ostringstream cov;
    for (const auto& c : coins) {
        auto count = [&](int v) { int n = 0; for (const auto& [t, p] : c.v[v].pay) if (t >= kT0 && t < kT1) ++n; return n; };
        double full = double(kT1 - kT0) / kHour;
        double lt = count(LT) / full, hl = count(HL) / full, bn = count(BN) / (full / 8), cd = double(c.day.size()) / 365.0;
        bool ok = lt > 0.95 && hl > 0.95 && bn > 0.95;
        if (ok) ++complete;
        cov << c.name << " lighter " << pct(lt, 0) << " hyperliquid " << pct(hl, 0) << " binance " << pct(bn, 0) << " candles " << pct(cd, 0) << (ok ? "" : "  (partial)") << "\n";
    }
    std::cout << coins.size() << " coins, " << complete << " with ≥ 95 % funding coverage on all three venues\n";
    if (coverage_only) { std::cout << cov.str(); return 0; }

    Costs costs;
    std::vector<Cell> grid;
    for (int L : {8, 24, 72}) for (double th : {0.10, 0.20, 0.40, 0.80}) grid.push_back({L, th});

    json tuning = json::array();
    int best = -1; Summary best_s;
    std::ostringstream tune_md;
    tune_md << "| L (h) | θ_in | net / notional | APR on capital | round trips | avg hold (h) |\n|---|---|---|---|---|---|\n";
    for (int i = 0; i < int(grid.size()); ++i) {
        auto runs = run_all(coins, grid[i], costs, kT0, kSplit);
        auto s = summarize(runs, costs, kT0, kSplit);
        tuning.push_back({{"L", grid[i].L}, {"theta", grid[i].theta}, {"summary", summary_json(s)}});
        tune_md << "| " << grid[i].L << " | " << pct(grid[i].theta, 0) << " | " << s.net << " | " << pct(s.apr_avg) << " | " << s.round_trips << " | " << int(s.avg_hold_h) << " |\n";
        bool better = best < 0 || (!std::isnan(s.apr_avg) && (std::isnan(best_s.apr_avg) || s.apr_avg > best_s.apr_avg ||
                                                               (s.apr_avg == best_s.apr_avg && s.round_trips < best_s.round_trips)));
        if (better) { best = i; best_s = s; }
    }
    const Cell chosen = grid[best];
    auto test_runs = run_all(coins, chosen, costs, kSplit, kT1);
    auto t = summarize(test_runs, costs, kSplit, kT1);
    auto v = judge(t);
    auto risk = hold_risk(coins, test_runs);
    double worst_move = 0; std::string worst_coin;
    for (const auto& [c, m] : risk) if (m > worst_move) { worst_move = m; worst_coin = c; }

    fs::create_directories(out);
    json res = {{"preregistration", "research/preregistered/2026-10-28_funding_carry.md"},
                {"window", {day(kT0), day(kSplit), day(kT1)}}, {"coins", coins.size()}, {"coins_full_coverage", complete},
                {"costs", {{"fee_bps", costs.fee_bps}, {"spread_bps", costs.spread_bps}, {"basis_rt_bps", costs.basis_rt_bps}, {"leverage", costs.leverage}}},
                {"tuning", tuning}, {"chosen", {{"L", chosen.L}, {"theta", chosen.theta}}}, {"test", summary_json(t)},
                {"verdict", {{"apr_ge_10pct", v.apr}, {"coins_60pct", v.coins}, {"blocks_4_of_6", v.blocks}, {"without_best3_positive", v.best3}, {"pass", v.pass()}}},
                {"hold_risk_max_intraday_move", risk}};
    std::ofstream(out / "results.json") << res.dump(1) << "\n";

    std::ostringstream md;
    md << "# Funding carry — F0 result\n\nPre-registration: `research/preregistered/2026-10-28_funding_carry.md`. Window "
       << day(kT0) << " → " << day(kT1) << " UTC, tuning to " << day(kSplit) << ", test after. " << coins.size() << " coins ("
       << complete << " with ≥ 95 % coverage on all three venues), pairs Lighter–Hyperliquid, Lighter–Binance, Hyperliquid–Binance.\n\n"
       << "Costs per leg each way: fee Lighter 0 / Hyperliquid 4.5 / Binance 5 bps, spread 2 bps; basis 5 bps per round trip; "
       << "capital = 2 × notional / 3.\n\n## Tuning half (all 12 cells)\n\n" << tune_md.str()
       << "\nChosen: L = " << chosen.L << " h, θ_in = " << pct(chosen.theta, 0) << " (highest tuning APR on capital).\n\n"
       << "## Test half — chosen cell, read once\n\n| KPI | Value | Threshold | |\n|---|---|---|---|\n"
       << "| Net APR on capital (time-weighted) | " << pct(t.apr_avg) << " | ≥ 10 % | " << (v.apr ? "✅" : "❌") << " |\n"
       << "| Coins net > 0 | " << t.coins_positive << " / " << t.coins_traded << " | ≥ 60 % | " << (v.coins ? "✅" : "❌") << " |\n"
       << "| Positive blocks (6 × 30.5 d) | " << t.blocks_positive << " / 6 | ≥ 4 | " << (v.blocks ? "✅" : "❌") << " |\n"
       << "| Net without best 3 coins | " << t.net_without_best3 << " | > 0 | " << (v.best3 ? "✅" : "❌") << " |\n"
       << "| **Verdict** | **" << (v.pass() ? "PASS" : "FAIL") << "** | | |\n\n"
       << "Reported: net per unit notional " << t.net << " (funding " << t.funding << ", costs −" << t.costs << "), APR on peak capital "
       << pct(t.apr_peak) << ", average capital in use " << t.avg_capital << " (peak " << t.peak_capital << ") per unit notional per position, "
       << t.round_trips << " round trips, average hold " << int(t.avg_hold_h) << " h, hours the two venues' funding had opposite signs "
       << pct(t.opposite_share, 0) << ".\n\nBlocks: ";
    for (double b : t.blocks) md << b << "  ";
    md << "\n\nBy pair: ";
    for (const auto& [p, n] : t.by_pair) md << p << " " << n << " · ";
    md << "\n\nFunding by venue (P&L from each venue's payments): lighter " << t.venue_funding[LT] << ", hyperliquid "
       << t.venue_funding[HL] << ", binance " << t.venue_funding[BN] << ".\n\nLargest intraday move while held: " << pct(worst_move)
       << " (" << worst_coin << ") — at 3× a leg is liquidated near a 30 % move against it.\n\n"
       << "Known gaps: survivorship (today's listings only, biases results upward), flat basis and spread, no liquidation "
       << "or margin-transfer model, one 12-month regime.\n\n## Coverage\n\n```\n" << cov.str() << "```\n";
    std::ofstream(out / "report.md") << md.str();
    std::cout << "chosen L=" << chosen.L << " theta=" << chosen.theta << " | test APR " << pct(t.apr_avg) << " | coins " << t.coins_positive
              << "/" << t.coins_traded << " | blocks " << t.blocks_positive << "/6 | w/o best3 " << t.net_without_best3 << " | "
              << (v.pass() ? "PASS" : "FAIL") << "\n→ " << (out / "report.md").string() << "\n";
    return 0;
}
