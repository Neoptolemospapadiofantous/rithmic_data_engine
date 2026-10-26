// mm_robust — tries to break the market-making candidate (src/dex/hl_mm.hpp). Research only.
//
//   mm_robust --dir DIR [--coins A,B] [--threads 4] [--split YYYY-MM-DD] [--report FILE] [--dash DIR]
//
// The candidate is the setting mm_opt's clean holdout chose on 2024-11..2025-10 (2026-10-25): half width
// 5 ticks, quote only on spreads ≥ 4 ticks, pull the side the top-of-book imbalance (≥ 0.6) leans against,
// no quotes for 2 s after a ≥ 2-tick jump, keep a resting order for a 1-tick target change; Lighter fees
// (0/0), 300 ms, pessimistic queue, $100 quotes, cap 3. Every coin-day runs flat-to-flat through:
//   * neighbourhood — each setting nudged alone (a real edge is a plateau, a lucky one a spike),
//   * execution — order delay 100 ms..1 s, maker fee 0..1.5 bps, optimistic queue, quote size, inventory cap.
// "holdout" = days on/after --split (default 2025-11-01), the year the candidate was NOT chosen on.
// For the candidate itself: P&L by coin (and without the best 1 / 3 coins), worst coin-days, and a 95%
// interval for the mean holdout day by resampling whole DAYS (coins on one day move together, so resampling
// coin-days would overstate the certainty).

#include "dex/hl_events.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Variant { std::string group, name; hl::Params p; };
struct Cell { double pnl = 0, vol = 0, dd = 0; int64_t fills = 0; };

static hl::Params candidate() {
    hl::Params p;
    p.order_usd = 100; p.max_inv_units = 3;
    p.maker_fee_bps = 0; p.taker_fee_bps = 0; p.latency_ms = 300;
    p.cancels = hl::CancelModel::Pessimistic;
    p.half_width_ticks = 5; p.min_spread_ticks = 4; p.imb_pull = 0.6;
    p.pause_move_ticks = 2; p.pause_ms = 2000; p.requote_ticks = 1;
    p.track_markouts = false;
    return p;
}

static std::vector<Variant> variants() {
    std::vector<Variant> v;
    auto add = [&](const std::string& g, const std::string& n, auto edit) { hl::Params p = candidate(); edit(p); v.push_back({g, n, p}); };
    add("candidate", "candidate", [](hl::Params&) {});
    for (double w : {3.5, 4.0, 6.0, 7.0}) add("width", "width " + std::to_string(w).substr(0, 3), [w](hl::Params& p) { p.half_width_ticks = w; });
    for (double s : {0.0, 2.0, 3.0, 5.0, 6.0}) add("spread filter", s == 0 ? "spread filter off" : "spread ≥ " + std::to_string((int)s), [s](hl::Params& p) { p.min_spread_ticks = s; });
    for (double i : {0.0, 0.5, 0.7, 0.8}) add("imbalance", i == 0 ? "imbalance pull off" : "imbalance " + std::to_string(i).substr(0, 3), [i](hl::Params& p) { p.imb_pull = i; });
    add("pause", "pause off", [](hl::Params& p) { p.pause_move_ticks = 0; p.pause_ms = 0; });
    for (double t : {1.0, 3.0}) add("pause", "pause on " + std::to_string((int)t) + "-tick jumps", [t](hl::Params& p) { p.pause_move_ticks = t; });
    for (int64_t ms : {1000, 4000}) add("pause", "pause " + std::to_string(ms / 1000) + " s", [ms](hl::Params& p) { p.pause_ms = ms; });
    add("keep queue", "keep queue off", [](hl::Params& p) { p.requote_ticks = 0; });
    add("keep queue", "keep for 2-tick changes", [](hl::Params& p) { p.requote_ticks = 2; });
    // Delay grid (2026-10-25, VPS placement question): 20-50 ms = a server in the venue's region with no venue-imposed
    // delay; 200-250 = Lighter Standard (200 ms) from a server in its region; 300 = the backtests' value and this
    // box today (142 ms exchange→box lag); 350-500 = a far region; 1000 = a bad connection.
    for (int64_t ms : {20, 50, 100, 150, 200, 220, 250, 350, 500, 1000}) add("delay", std::to_string(ms) + " ms delay", [ms](hl::Params& p) { p.latency_ms = ms; });
    for (double f : {0.25, 0.5, 1.0, 1.5}) add("fee", "maker fee " + std::to_string(f).substr(0, 4) + " bps", [f](hl::Params& p) { p.maker_fee_bps = f; });
    add("queue", "optimistic queue", [](hl::Params& p) { p.cancels = hl::CancelModel::Optimistic; });
    for (double u : {10.0, 250.0}) add("size", "$" + std::to_string((int)u) + " quotes", [u](hl::Params& p) { p.order_usd = u; });
    for (int c : {1, 5}) add("inventory cap", "cap " + std::to_string(c) + " quotes", [c](hl::Params& p) { p.max_inv_units = c; });
    // Lighter's own account tiers (apidocs.lighter.xyz/docs/account-types, read 2026-10-25): Standard = 0 fees,
    // 200 ms maker/cancel delay imposed by the venue (a nearby server cannot remove it); Premium (0 LIT staked) =
    // 0.4 bps maker / 2.8 bps taker, 0 ms maker/cancel delay — so only network time is left (a close server).
    add("lighter account", "Lighter Standard (0 fee, 200 ms)", [](hl::Params& p) { p.latency_ms = 200; });
    for (int64_t ms : {10, 50}) add("lighter account", "Lighter Premium (0.4 bps, " + std::to_string(ms) + " ms network)",
                                   [ms](hl::Params& p) { p.maker_fee_bps = 0.4; p.taker_fee_bps = 2.8; p.latency_ms = ms; });
    return v;
}

static double safe_div(double a, double b) { return b != 0 ? a / b : 0; }

int main(int argc, char** argv) {
    fs::path dir, report, dash_dir;
    std::set<std::string> only;
    int threads = 4;
    std::string split = "2025-11-01";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--dir") dir = val();
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) only.insert(c); }
        else if (a == "--threads") threads = std::max(1, std::atoi(val().c_str()));
        else if (a == "--split") split = val();
        else if (a == "--report") report = val();
        else if (a == "--dash") dash_dir = val();
        else { std::fprintf(stderr, "usage: mm_robust --dir DIR [--coins A,B] [--threads N] [--split YYYY-MM-DD] [--report FILE] [--dash DIR]\n"); return 2; }
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) { std::fprintf(stderr, "--dir must be an existing directory\n"); return 2; }
    std::vector<hl::DayFile> jobs;
    for (auto& d : hl::list_days(dir)) if (only.empty() || only.count(d.coin)) jobs.push_back(d);
    if (jobs.empty()) { std::fprintf(stderr, "no coin-days under %s\n", dir.c_str()); return 1; }
    const auto V = variants();
    std::vector<std::vector<Cell>> res(jobs.size(), std::vector<Cell>(V.size()));
    std::vector<char> ok(jobs.size(), 0);

    std::atomic<size_t> next{0}, done{0};
    std::mutex mu;
    auto worker = [&]() {
        for (size_t j; (j = next++) < jobs.size();) {
            const auto ev = hl::load_day(jobs[j]);
            for (const auto& e : ev) if (e.is_book) { ok[j] = 1; break; }
            for (size_t k = 0; k < V.size(); ++k) {
                hl::Simulator sim(V[k].p);
                hl::run_events(ev, sim);
                const auto r = sim.finish();
                res[j][k] = {r.flat_pnl, r.volume_usd, r.max_dd, r.fills};
            }
            const size_t n = ++done;
            std::lock_guard lk(mu);
            std::fprintf(stderr, "[%zu/%zu] %s %s\n", n, jobs.size(), jobs[j].coin.c_str(), jobs[j].day.c_str());
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    // ── per variant ─────────────────────────────────────────────────────────
    json vrows = json::array();
    for (size_t k = 0; k < V.size(); ++k) {
        double all = 0, ho = 0, ho_vol = 0; int64_t ho_cd = 0, ho_pos_cd = 0;
        std::map<std::string, double> by_day;
        for (size_t j = 0; j < jobs.size(); ++j) {
            if (!ok[j]) continue;
            all += res[j][k].pnl;
            if (jobs[j].day < split) continue;
            ho += res[j][k].pnl; ho_vol += res[j][k].vol; ++ho_cd; ho_pos_cd += res[j][k].pnl > 0;
            by_day[jobs[j].day] += res[j][k].pnl;
        }
        double worst = 0; int64_t pos_days = 0;
        for (const auto& [d, p] : by_day) { worst = std::min(worst, p); pos_days += p > 0; }
        vrows.push_back({{"group", V[k].group}, {"variant", V[k].name}, {"pnl_all_usd", all}, {"pnl_holdout_usd", ho},
                         {"net_bps_holdout", safe_div(ho, ho_vol) * 1e4}, {"volume_holdout_usd", ho_vol},
                         {"positive_coin_days_pct", safe_div(100.0 * ho_pos_cd, ho_cd)}, {"holdout_days", (int64_t)by_day.size()},
                         {"positive_days", pos_days}, {"worst_day_usd", worst}});
    }

    // ── candidate deep-dive (holdout) ───────────────────────────────────────
    std::map<std::string, double> coin_pnl, day_pnl, day_pnl_all;
    std::vector<std::tuple<double, std::string, std::string>> coin_days;
    for (size_t j = 0; j < jobs.size(); ++j) {
        if (!ok[j]) continue;
        const double p = res[j][0].pnl;
        day_pnl_all[jobs[j].day] += p;
        if (jobs[j].day < split) continue;
        coin_pnl[jobs[j].coin] += p; day_pnl[jobs[j].day] += p;
        coin_days.emplace_back(p, jobs[j].coin, jobs[j].day);
    }
    std::vector<std::pair<double, std::string>> ranked;
    for (const auto& [c, p] : coin_pnl) ranked.push_back({p, c});
    std::sort(ranked.rbegin(), ranked.rend());
    double total = 0; for (const auto& r : ranked) total += r.first;
    double top1 = ranked.empty() ? 0 : ranked[0].first, top3 = 0;
    for (size_t i = 0; i < ranked.size() && i < 3; ++i) top3 += ranked[i].first;
    int64_t pos_coins = 0; for (const auto& r : ranked) pos_coins += r.first > 0;
    std::sort(coin_days.begin(), coin_days.end());

    std::vector<double> dp; for (const auto& [d, p] : day_pnl) dp.push_back(p);
    auto stats = [](const std::vector<double>& x) {
        double m = 0; for (double v : x) m += v; m /= std::max<size_t>(1, x.size());
        double s = 0; for (double v : x) s += (v - m) * (v - m);
        const double sd = x.size() > 1 ? std::sqrt(s / (x.size() - 1)) : 0;
        return std::pair<double, double>{m, sd};
    };
    const auto [mean, sd] = stats(dp);
    std::mt19937_64 rng(20261025);
    std::uniform_int_distribution<size_t> pick(0, dp.empty() ? 0 : dp.size() - 1);
    std::vector<double> boot;
    int64_t above = 0;
    for (int b = 0; b < 20000 && !dp.empty(); ++b) {
        double m = 0; for (size_t i = 0; i < dp.size(); ++i) m += dp[pick(rng)];
        m /= dp.size(); boot.push_back(m); above += m > 0;
    }
    std::sort(boot.begin(), boot.end());
    const double lo = boot.empty() ? 0 : boot[boot.size() * 25 / 1000], hi = boot.empty() ? 0 : boot[boot.size() * 975 / 1000];
    const double tstat = sd > 0 ? mean / (sd / std::sqrt((double)dp.size())) : 0;

    // ── report ──────────────────────────────────────────────────────────────
    std::ostringstream o;
    char b[512];
    std::snprintf(b, sizeof b, "# mm_robust — trying to break the candidate\n\n%zu coin-days, holdout = days from %s\n\n", jobs.size(), split.c_str());
    o << b << "| group | variant | holdout P&L $ | net bps | positive days | worst day $ | positive coin-days | all-days P&L $ |\n|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : vrows) {
        std::snprintf(b, sizeof b, "| %s | %s | %.0f | %.2f | %lld/%lld | %.0f | %.0f%% | %.0f |\n", r["group"].get<std::string>().c_str(),
                      r["variant"].get<std::string>().c_str(), r["pnl_holdout_usd"].get<double>(), r["net_bps_holdout"].get<double>(),
                      (long long)r["positive_days"].get<int64_t>(), (long long)r["holdout_days"].get<int64_t>(), r["worst_day_usd"].get<double>(),
                      r["positive_coin_days_pct"].get<double>(), r["pnl_all_usd"].get<double>());
        o << b;
    }
    std::snprintf(b, sizeof b, "\n## Candidate, holdout year\n\nTotal $%.0f over %zu days; mean day $%.1f (sd $%.1f, t = %.2f).\n"
                  "95%% interval for the mean day, resampling whole days: $%.1f .. $%.1f; share of resamples above zero: %.1f%%.\n"
                  "Coins positive: %lld of %zu. Without the best coin: $%.0f; without the best three: $%.0f.\n\n",
                  total, dp.size(), mean, sd, tstat, lo, hi, 100.0 * above / std::max<size_t>(1, boot.size()), (long long)pos_coins,
                  ranked.size(), total - top1, total - top3);
    o << b << "| coin | holdout P&L $ |\n|---|---|\n";
    for (const auto& [p, c] : ranked) { std::snprintf(b, sizeof b, "| %s | %.1f |\n", c.c_str(), p); o << b; }
    o << "\nWorst coin-days:\n\n| coin | day | P&L $ |\n|---|---|---|\n";
    for (size_t i = 0; i < coin_days.size() && i < 8; ++i) {
        std::snprintf(b, sizeof b, "| %s | %s | %.1f |\n", std::get<1>(coin_days[i]).c_str(), std::get<2>(coin_days[i]).c_str(), std::get<0>(coin_days[i]));
        o << b;
    }
    std::fputs(o.str().c_str(), stdout);
    if (!report.empty()) { fs::create_directories(report.parent_path(), ec); std::ofstream(report) << o.str(); }
    if (!dash_dir.empty()) {
        fs::create_directories(dash_dir, ec);
        auto write = [&](const char* name, const json& rows) { std::ofstream(dash_dir / name) << json{{"rows", rows}}.dump(); };
        write("robust_variants.json", vrows);
        json cr = json::array(); for (const auto& [p, c] : ranked) cr.push_back({{"coin", c}, {"pnl_usd", p}});
        write("robust_coins.json", cr);
        json dr = json::array();
        for (const auto& [d, p] : day_pnl_all) dr.push_back({{"day", d}, {"pnl_usd", p}, {"holdout", d >= split}});
        write("robust_days.json", dr);
        write("robust_stats.json", json::array({{{"metric", "mean_day_usd"}, {"value", mean}}, {{"metric", "ci_low_usd"}, {"value", lo}},
                                                {{"metric", "ci_high_usd"}, {"value", hi}}, {{"metric", "share_above_zero_pct"}, {"value", 100.0 * above / std::max<size_t>(1, boot.size())}},
                                                {{"metric", "t_stat"}, {"value", tstat}}, {{"metric", "total_usd"}, {"value", total}},
                                                {{"metric", "without_best_coin_usd"}, {"value", total - top1}}, {{"metric", "without_best_3_usd"}, {"value", total - top3}},
                                                {{"metric", "coins_positive"}, {"value", pos_coins}}, {{"metric", "coins"}, {"value", (int64_t)ranked.size()}}}));
    }
    return 0;
}
