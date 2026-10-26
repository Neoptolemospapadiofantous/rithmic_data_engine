// mm_opt — walk-forward optimizer for the market-making strategy (src/dex/hl_mm.hpp). Research only.
//
//   mm_opt --dir DIR [--coins A,B] [--threads 4] [--train 6] [--order-usd 100] [--max-inv 3]
//          [--report FILE] [--dash DIR] [--best-dir DIR]
//
// DIR is either layout hl_events.hpp reads (Tardis history or the live archive). Every coin-day runs
// the whole grid from a flat start — 121 strategies (grid() below) × 2 venues (Hyperliquid 1.5/4.5 bps 200 ms,
// Lighter 0/0 300 ms), pessimistic queue — and ends flat (taker fee).
//
// Walk-forward, the project rule (CLAUDE.md: parameters are never chosen from an in-sample ranking):
// for each test day, each coin trades the strategy with the best PESSIMISTIC-queue P&L over its previous
// `train` days that have data (at least 3), and stands aside when that best is not positive. It is
// compared with "join" (half width 0.5, no skew, mid: always quote at the best bid/ask) and "hindsight"
// (the best strategy of the test day itself — look-ahead, an upper bound no one can trade). Two stricter
// tests: "one-setting" (one strategy for ALL coins, re-chosen each day from the previous `train` days) and
// "holdout" (one strategy chosen on the first half of the days, judged only on the second half).
// --best-dir writes best_<venue>.json (per-coin choice) and best_<venue>_one.json (one setting for all coins,
// the form that held up out of sample) from the LAST `train` days, for mm_sim --params.

#include "dex/hl_events.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

struct Venue { std::string name; double maker, taker; int64_t lat; };
struct Cell { double pnl = 0, vol = 0, dd = 0; int64_t fills = 0; bool ok = false; };

// Grid, round 3 (2026-10-25). Rounds 1-2 (width × skew × mid/micro) found nothing profitable: skew only hurt,
// microprice only hurt, the best width was ~5 ticks and every setting lost to adverse selection. Round 3 keeps
// mid, no skew, and adds the defences against being picked off (hl_mm.hpp): spread filter, imbalance pull,
// pause after a jump, requote hysteresis. Plus the "join" baseline. Beyond the 5 levels Tardis shows, the
// queue is assumed equal to the deepest visible level (hl_mm.hpp).
struct Strat { double h, min_spread, imb; bool pause; double requote; };
static const std::vector<Venue> VENUES = {{"hyperliquid", 1.5, 4.5, 200}, {"lighter", 0.0, 0.0, 300}};

static std::vector<Strat> grid() {
    std::vector<Strat> g = {{0.5, 0, 0, false, 0}};                      // join, the baseline
    for (double h : {0.5, 1.5, 2.5, 5.0, 8.0})
        for (double ms : {0.0, 2.0, 4.0})
            for (double imb : {0.0, 0.6})
                for (bool pause : {false, true})
                    for (double rq : {0.0, 1.0})
                        if (!(h == 0.5 && ms == 0 && imb == 0 && !pause && rq == 0)) g.push_back({h, ms, imb, pause, rq});
    return g;
}
static void apply(const Strat& s, hl::Params& p) {
    p.half_width_ticks = s.h; p.skew_ticks = 0; p.fair = hl::Fair::Mid;
    p.min_spread_ticks = s.min_spread; p.imb_pull = s.imb;
    p.pause_move_ticks = s.pause ? 2 : 0; p.pause_ms = s.pause ? 2000 : 0;
    p.requote_ticks = s.requote;
}
static std::string strat_name(const Strat& s) {
    char b[96];
    std::snprintf(b, sizeof b, "w%.1f%s%s%s%s", s.h, s.min_spread > 0 ? (" sp" + std::to_string((int)s.min_spread)).c_str() : "",
                  s.imb > 0 ? " imb" : "", s.pause ? " pause" : "", s.requote > 0 ? " keep" : "");
    return b;
}
static double safe_div(double a, double b) { return b > 0 ? a / b : 0; }

int main(int argc, char** argv) {
    fs::path dir, report, dash_dir, best_dir;
    std::set<std::string> only;
    int threads = 4, train = 6, max_inv = 3;
    double order_usd = 100;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto val = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--dir") dir = val();
        else if (a == "--coins") { std::stringstream ss(val()); for (std::string c; std::getline(ss, c, ',');) only.insert(c); }
        else if (a == "--threads") threads = std::max(1, std::atoi(val().c_str()));
        else if (a == "--train") train = std::max(1, std::atoi(val().c_str()));
        else if (a == "--order-usd") order_usd = std::atof(val().c_str());
        else if (a == "--max-inv") max_inv = std::atoi(val().c_str());
        else if (a == "--report") report = val();
        else if (a == "--dash") dash_dir = val();
        else if (a == "--best-dir") best_dir = val();
        else {
            std::fprintf(stderr, "usage: mm_opt --dir DIR [--coins A,B] [--threads N] [--train N] [--order-usd N] [--max-inv N] "
                                 "[--report FILE] [--dash DIR] [--best-dir DIR]\n");
            return 2;
        }
    }
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) { std::fprintf(stderr, "--dir must be an existing directory\n"); return 2; }

    std::vector<hl::DayFile> jobs;
    for (auto& d : hl::list_days(dir)) if (only.empty() || only.count(d.coin)) jobs.push_back(d);
    if (jobs.empty()) { std::fprintf(stderr, "no coin-days under %s\n", dir.c_str()); return 1; }
    const auto G = grid();
    const size_t NS = G.size(), NV = VENUES.size(), NQ = 1;   // pessimistic only: rounds 1-2 showed < 0.1 bps between the bounds
    auto idx = [&](size_t s, size_t v, size_t q) { return (s * NV + v) * NQ + q; };
    std::vector<std::vector<Cell>> res(jobs.size(), std::vector<Cell>(NS * NV * NQ));
    std::vector<double> spread(jobs.size(), 0), traded(jobs.size(), 0);

    std::atomic<size_t> next{0}, done{0};
    std::mutex log_mu;
    auto worker = [&]() {
        for (size_t j; (j = next++) < jobs.size();) {
            const auto ev = hl::load_day(jobs[j]);
            int64_t nb = 0;
            for (const auto& e : ev) {
                if (e.is_book) { spread[j] += (e.book.asks[0].px - e.book.bids[0].px) / e.book.mid() * 1e4; ++nb; }
                else traded[j] += e.trade.px * e.trade.sz;
            }
            spread[j] = safe_div(spread[j], nb);
            for (size_t s = 0; s < NS; ++s)
                for (size_t v = 0; v < NV; ++v)
                    for (size_t q = 0; q < NQ; ++q) {
                        hl::Params p;
                        p.order_usd = order_usd; p.max_inv_units = max_inv;
                        p.maker_fee_bps = VENUES[v].maker; p.taker_fee_bps = VENUES[v].taker; p.latency_ms = VENUES[v].lat;
                        p.cancels = q ? hl::CancelModel::Optimistic : hl::CancelModel::Pessimistic;
                        apply(G[s], p);
                        p.track_markouts = false;
                        hl::Simulator sim(p);
                        hl::run_events(ev, sim);
                        const auto r = sim.finish();
                        res[j][idx(s, v, q)] = {r.flat_pnl, r.volume_usd, r.max_dd, r.fills, nb > 0};
                    }
            const size_t n = ++done;
            std::lock_guard lk(log_mu);
            std::fprintf(stderr, "[%zu/%zu] %s %s  %zu events  spread %.2f bps\n", n, jobs.size(), jobs[j].coin.c_str(),
                         jobs[j].day.c_str(), ev.size(), spread[j]);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 0; t < threads; ++t) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    // ── index: coin → day → job ─────────────────────────────────────────────
    std::set<std::string> day_set, coin_set;
    std::map<std::string, std::map<std::string, size_t>> cd;
    for (size_t j = 0; j < jobs.size(); ++j) { cd[jobs[j].coin][jobs[j].day] = j; day_set.insert(jobs[j].day); coin_set.insert(jobs[j].coin); }
    const std::vector<std::string> days(day_set.begin(), day_set.end());
    size_t join_s = 0;
    join_s = 0;                                               // grid()[0] is the join baseline

    // The strategy a coin would trade on day index i (from days before it), or -1 to stand aside.
    auto choose = [&](const std::string& coin, size_t i, size_t v, double* score_out) -> int {
        std::vector<double> score(NS, 0);
        int have = 0;
        for (size_t k = (i >= (size_t)train ? i - train : 0); k < i; ++k) {
            auto it = cd[coin].find(days[k]);
            if (it == cd[coin].end() || !res[it->second][0].ok) continue;
            ++have;
            for (size_t s = 0; s < NS; ++s) score[s] += res[it->second][idx(s, v, 0)].pnl;
        }
        if (have < std::min(3, train)) return -1;
        const size_t best = std::max_element(score.begin(), score.end()) - score.begin();
        if (score_out) *score_out = score[best];
        return score[best] > 0 ? (int)best : -1;
    };

    // One setting for every coin (far fewer choices than one per coin, so far less room to overfit):
    // the best summed pessimistic P&L over days [from, to) across all coins; -1 if that best is not positive.
    auto choose_all = [&](size_t from, size_t to, size_t v) -> int {
        std::vector<double> score(NS, 0);
        for (size_t k = from; k < to; ++k)
            for (const auto& coin : coin_set) {
                auto it = cd[coin].find(days[k]);
                if (it == cd[coin].end() || !res[it->second][0].ok) continue;
                for (size_t s = 0; s < NS; ++s) score[s] += res[it->second][idx(s, v, 0)].pnl;
            }
        const size_t best = std::max_element(score.begin(), score.end()) - score.begin();
        return score[best] > 0 ? (int)best : -1;
    };
    const size_t half = days.size() / 2;               // clean holdout: one setting from the first half, judged on the second
    std::vector<int> holdout_pick(NV);
    for (size_t v = 0; v < NV; ++v) holdout_pick[v] = choose_all(0, half, v);

    json daily = json::array(), summary = json::array(), coin_rows = json::array(), grid_rows = json::array(), picks = json::array();
    struct Acc { double pnl = 0, vol = 0; int64_t days = 0, pos = 0, coin_days = 0; };
    std::map<std::string, Acc> acc;                                    // key venue|queue|method
    std::map<std::string, std::map<std::string, std::pair<double, int64_t>>> per_coin;   // venue|queue → coin → (pnl, days traded)
    std::map<std::string, std::map<size_t, int64_t>> pick_count;       // venue → strategy → times chosen
    for (size_t v = 0; v < NV; ++v)
        for (size_t q = 0; q < NQ; ++q)
            for (size_t i = (size_t)train; i < days.size(); ++i) {
                constexpr int NM = 5;
                double m_pnl[NM] = {}, m_vol[NM] = {};
                int64_t m_n[NM] = {};
                const int global = choose_all(i - train, i, v);
                for (const auto& coin : coin_set) {
                    auto it = cd[coin].find(days[i]);
                    if (it == cd[coin].end() || !res[it->second][0].ok) continue;
                    const auto& row = res[it->second];
                    const int c = choose(coin, i, v, nullptr);
                    if (c >= 0) {
                        const Cell& x = row[idx(c, v, q)];
                        m_pnl[0] += x.pnl; m_vol[0] += x.vol; ++m_n[0];
                        auto& pc = per_coin[VENUES[v].name + "|" + (q ? "optimistic" : "pessimistic")][coin];
                        pc.first += x.pnl; ++pc.second;
                        if (q == 0) ++pick_count[VENUES[v].name][c];
                    }
                    const Cell& jx = row[idx(join_s, v, q)];
                    m_pnl[1] += jx.pnl; m_vol[1] += jx.vol; ++m_n[1];
                    size_t hb = 0;
                    for (size_t s = 1; s < NS; ++s) if (row[idx(s, v, q)].pnl > row[idx(hb, v, q)].pnl) hb = s;
                    m_pnl[2] += std::max(0.0, row[idx(hb, v, q)].pnl); m_vol[2] += row[idx(hb, v, q)].pnl > 0 ? row[idx(hb, v, q)].vol : 0; ++m_n[2];
                    if (global >= 0) { const Cell& g = row[idx(global, v, q)]; m_pnl[3] += g.pnl; m_vol[3] += g.vol; ++m_n[3]; }
                    if (i >= half && holdout_pick[v] >= 0) { const Cell& h = row[idx(holdout_pick[v], v, q)]; m_pnl[4] += h.pnl; m_vol[4] += h.vol; ++m_n[4]; }
                }
                static const char* MN[NM] = {"walk-forward", "join", "hindsight", "one-setting", "holdout"};
                for (int m = 0; m < NM; ++m) {
                    if (m == 4 && i < half) continue;          // the holdout only exists on the second half
                    daily.push_back({{"day", days[i]}, {"venue", VENUES[v].name}, {"queue", q ? "optimistic" : "pessimistic"},
                                     {"method", MN[m]}, {"pnl_usd", m_pnl[m]}, {"volume_usd", m_vol[m]}, {"coins", m_n[m]}});
                    auto& a = acc[VENUES[v].name + "|" + (q ? "optimistic" : "pessimistic") + "|" + MN[m]];
                    a.pnl += m_pnl[m]; a.vol += m_vol[m]; ++a.days; a.pos += m_pnl[m] > 0; a.coin_days += m_n[m];
                }
            }
    for (const auto& [key, a] : acc) {
        std::stringstream ss(key); std::string v, q, m;
        std::getline(ss, v, '|'); std::getline(ss, q, '|'); std::getline(ss, m, '|');
        summary.push_back({{"venue", v}, {"queue", q}, {"method", m}, {"pnl_usd", a.pnl}, {"volume_usd", a.vol},
                           {"net_bps", safe_div(a.pnl, a.vol) * 1e4}, {"test_days", a.days}, {"positive_days", a.pos},
                           {"coin_days", a.coin_days}, {"pnl_per_coin_day", safe_div(a.pnl, a.coin_days)}});
    }
    for (const auto& [key, m] : per_coin) {
        std::stringstream ss(key); std::string v, q;
        std::getline(ss, v, '|'); std::getline(ss, q, '|');
        for (const auto& [coin, x] : m) coin_rows.push_back({{"coin", coin}, {"venue", v}, {"queue", q}, {"pnl_usd", x.first}, {"days_traded", x.second}});
    }
    // In-sample grid (every coin-day, pessimistic queue): how each strategy does before any selection.
    for (size_t s = 0; s < NS; ++s)
        for (size_t v = 0; v < NV; ++v) {
            double pnl = 0, vol = 0; int64_t fills = 0, pos = 0, n = 0;
            for (size_t j = 0; j < jobs.size(); ++j) {
                if (!res[j][0].ok) continue;
                const Cell& x = res[j][idx(s, v, 0)];
                pnl += x.pnl; vol += x.vol; fills += x.fills; pos += x.pnl > 0; ++n;
            }
            grid_rows.push_back({{"strategy", strat_name(G[s])}, {"half_width_ticks", G[s].h}, {"min_spread_ticks", G[s].min_spread},
                                 {"imb_pull", G[s].imb}, {"pause", G[s].pause}, {"keep_queue", G[s].requote > 0}, {"venue", VENUES[v].name}, {"pnl_usd", pnl}, {"volume_usd", vol},
                                 {"net_bps", safe_div(pnl, vol) * 1e4}, {"fills", fills}, {"positive_coin_days", pos}, {"coin_days", n},
                                 {"times_chosen", pick_count[VENUES[v].name][s]}});
        }
    json markets = json::array();
    for (const auto& coin : coin_set) {
        double sp = 0, tr = 0; int64_t n = 0;
        for (const auto& [d, j] : cd[coin]) { sp += spread[j]; tr += traded[j]; ++n; }
        markets.push_back({{"coin", coin}, {"avg_spread_bps", safe_div(sp, n)}, {"avg_traded_usd", safe_div(tr, n)}, {"days", n}});
    }

    // ── report ──────────────────────────────────────────────────────────────
    std::ostringstream o;
    char b[512];
    std::snprintf(b, sizeof b, "# mm_opt — walk-forward market making\n\n%zu coin-days, %zu days (%s .. %s), %zu coins, train window %d days, $%.0f quotes, cap %d\n\n",
                  jobs.size(), days.size(), days.front().c_str(), days.back().c_str(), coin_set.size(), train, order_usd, max_inv);
    o << b << "| venue | queue | method | P&L $ | volume $ | net bps | test days | positive days | coin-days | $ per coin-day |\n|---|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& r : summary) {
        std::snprintf(b, sizeof b, "| %s | %s | %s | %.2f | %.0f | %.2f | %lld | %lld | %lld | %.3f |\n",
                      r["venue"].get<std::string>().c_str(), r["queue"].get<std::string>().c_str(), r["method"].get<std::string>().c_str(),
                      r["pnl_usd"].get<double>(), r["volume_usd"].get<double>(), r["net_bps"].get<double>(),
                      (long long)r["test_days"].get<int64_t>(), (long long)r["positive_days"].get<int64_t>(),
                      (long long)r["coin_days"].get<int64_t>(), r["pnl_per_coin_day"].get<double>());
        o << b;
    }
    o << "\nIn-sample grid, pessimistic queue (before selection; NOT a result):\n\n| strategy | venue | P&L $ | net bps | fills | positive coin-days | times chosen |\n|---|---|---|---|---|---|---|\n";
    for (const auto& r : grid_rows) {
        std::snprintf(b, sizeof b, "| %s | %s | %.2f | %.2f | %lld | %lld/%lld | %lld |\n", r["strategy"].get<std::string>().c_str(),
                      r["venue"].get<std::string>().c_str(), r["pnl_usd"].get<double>(), r["net_bps"].get<double>(),
                      (long long)r["fills"].get<int64_t>(), (long long)r["positive_coin_days"].get<int64_t>(),
                      (long long)r["coin_days"].get<int64_t>(), (long long)r["times_chosen"].get<int64_t>());
        o << b;
    }
    for (size_t v = 0; v < NV; ++v) {
        std::snprintf(b, sizeof b, "\nHoldout setting (%s, chosen on %s..%s, judged on %s..%s): %s\n", VENUES[v].name.c_str(),
                      days.front().c_str(), days[half - 1].c_str(), days[half].c_str(), days.back().c_str(),
                      holdout_pick[v] >= 0 ? strat_name(G[holdout_pick[v]]).c_str() : "none positive — stand aside");
        o << b;
    }
    std::fputs(o.str().c_str(), stdout);
    if (!report.empty()) { fs::create_directories(report.parent_path(), ec); std::ofstream(report) << o.str(); }
    if (!dash_dir.empty()) {
        fs::create_directories(dash_dir, ec);
        auto write = [&](const char* name, const json& rows) { std::ofstream(dash_dir / name) << json{{"rows", rows}}.dump(); };
        write("wf_daily.json", daily);
        write("wf_summary.json", summary);
        write("wf_coins.json", coin_rows);
        write("wf_grid.json", grid_rows);
        write("wf_markets.json", markets);
    }
    if (!best_dir.empty()) {
        fs::create_directories(best_dir, ec);
        for (size_t v = 0; v < NV; ++v) {
            json bj = {{"venue", VENUES[v].name}, {"maker_bps", VENUES[v].maker}, {"taker_bps", VENUES[v].taker},
                       {"latency_ms", VENUES[v].lat}, {"order_usd", order_usd}, {"max_inv", max_inv},
                       {"chosen_from", days.size() >= (size_t)train ? days[days.size() - train] : days.front()},
                       {"chosen_to", days.back()}, {"coins", json::object()}};
            for (const auto& coin : coin_set) {
                double score = 0;
                const int c = choose(coin, days.size(), v, &score);
                if (c < 0) continue;
                hl::Params q;
                apply(G[c], q);
                bj["coins"][coin] = {{"strategy", strat_name(G[c])}, {"half_width_ticks", q.half_width_ticks},
                                     {"min_spread_ticks", q.min_spread_ticks}, {"imb_pull", q.imb_pull},
                                     {"pause_move_ticks", q.pause_move_ticks}, {"pause_ms", q.pause_ms},
                                     {"requote_ticks", q.requote_ticks}, {"train_pnl_usd", score}};
            }
            std::ofstream(best_dir / ("best_" + VENUES[v].name + ".json")) << bj.dump(2);
            // The robust form: ONE setting for every coin, chosen from the last `train` days across all coins
            // (beat the per-coin choice in both the one-setting walk-forward and the clean holdout, 2026-10-25).
            const int g = choose_all(days.size() >= (size_t)train ? days.size() - train : 0, days.size(), v);
            json one = bj;
            one["coins"] = json::object();
            if (g >= 0) {
                hl::Params q;
                apply(G[g], q);
                one["setting"] = strat_name(G[g]);
                for (const auto& coin : coin_set)
                    one["coins"][coin] = {{"strategy", strat_name(G[g])}, {"half_width_ticks", q.half_width_ticks},
                                          {"min_spread_ticks", q.min_spread_ticks}, {"imb_pull", q.imb_pull},
                                          {"pause_move_ticks", q.pause_move_ticks}, {"pause_ms", q.pause_ms},
                                          {"requote_ticks", q.requote_ticks}};
            }
            std::ofstream(best_dir / ("best_" + VENUES[v].name + "_one.json")) << one.dump(2);
        }
    }
    return 0;
}
