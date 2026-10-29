// 2026-10-22 flag_prop_ev.cpp (founder: "well the drawdown is either 1000 for 25k or 2000 for 50k", "it doesnt have to be but it needs to be
// at least close because if we manage to get a payout out of the account that means we are in the green. EV of an account"). Expected value of
// a prop account traded with the flag rules: evaluation (payout_sim.hpp run_eval, the plan's EOD-trailing drawdown checked at each trade's worst
// open loss) → from the next day the funded account (run_account, 120 trading days, the plan's payout rules) → payouts. Started every 5th trading
// day 2014-01 → 2025-04 (and 2022-01 → 2025-04 alone), at most 60 trading days to pass. Rules (wide15 exports, NQ + ES, MNQ $2 / MES $5 per point,
// q micros on each market per trade, points scaled to today's ATR, cost 1.5 NQ / 0.5 ES pts per trade):
//   RULE   3-5m, pole >= 4×, pullback 15-50 %, A-list, 5m HH/HL, with the break, both sides, 1 open per market
//   BOOK1  3-5m, pole >= 4×, pullback 15-50 %, A or B, 5m HH/HL, both sides, 2 open per market (~1 a day)
//   BOOK2  4-5m, pole >= 3×, pullback 5-38 %, A or B, both sides, 2 open per market (~2 a day, flag_sortino.cpp's pick)
//   BULL   RULE, bull only
// Each trade's worst / best open P&L is rebuilt from the 1m bars between entry and exit. Output per plan × rule × size: eval pass %, median days
// to pass, funded accounts paid %, payouts and $ per funded account, and $ PER EVAL BOUGHT (pass % × $ per funded) — the break-even total fee
// (eval + activation) before the trader's split. Plans and their rules are payout_sim.hpp's (2026-10 table) — check each firm's current rules.
// Usage: flag_prop_ev W15_NQ.csv W15_ES.csv [stress|regime|sizing|timing|evalsize|dd|ddfix|budget|prob|pagedata OUT|tradelist OUT]. Read-only.
#include "research/meta.hpp"
#include "paper/payout_sim.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
using namespace lab;
struct Fl { int di, mk, em, xm, tf, side, grade, hh5, orb; double mult, pull, usd, mae, mfe, ppt; bool dt200, fomc; double risk1; double ent, stp, tgt, xpx, rr; double szm; };   // szm: optional size multiplier column 72 (regime_size.cpp), default 1   // $ at 1 micro, today's ATR
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_prop_ev W15_NQ.csv W15_ES.csv\n"); return 1; }
  std::vector<int> cal; std::map<int, int> ci; std::vector<Fl> v;
  std::map<int, std::vector<std::array<double, 4>>> mbars[2];   // date → 1m (tm, high, low, close), 09:30-16:00, for `mtm`
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; const double cost = mk ? 0.5 : 1.5, ppt = mk ? 5 : 2;
    std::set<int> fomc; { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
    paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    struct DB { double atr, pc, s200; bool fomc; std::vector<int> TM; std::vector<double> H, L; }; std::map<int, DB> days; double Anow = 1;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.atr, d.pc, d.sma200, d.fomc, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.TM.push_back(tm); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); mbars[mk][d.date].push_back({(double)tm, s.h[i], s.l[i], s.c[i]}); }
      if (z.TM.size() >= 370) { Anow = d.atr; if (mk == 0) cal.push_back(d.date); days[d.date] = std::move(z); } }
    if (mk == 0) for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = (int)i;
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; const int dt = (int)a[0]; auto it = ci.find(dt); auto dz = days.find(dt); if (it == ci.end() || dz == days.end()) continue;
      const DB& z = dz->second; const int side = (int)a[1], em = (int)a[12], xm = (int)a[13]; const double ent = a[14], st = a[15], fct = Anow / z.atr;
      double lo = 1e18, hi = -1e18; for (size_t k = 0; k < z.TM.size(); ++k) if (z.TM[k] >= em && z.TM[k] <= xm) { lo = std::min(lo, z.L[k]); hi = std::max(hi, z.H[k]); }
      double mae = side > 0 ? lo - ent : ent - hi, mfe = side > 0 ? hi - ent : ent - lo; const double stopDist = std::fabs(ent - st); mae = std::max(mae, -stopDist); mae = std::min(mae, 0.0); mfe = std::max(mfe, 0.0);
      v.push_back({it->second, mk, em, xm, (int)a[2], side, (int)a[6], (int)a[7], (int)a[47], a[22], a[24] * 100, ((a[19] + cost) * fct - cost) * ppt, (mae * fct - cost) * ppt, mfe * fct * ppt, ppt, std::isnan(z.s200) || (side > 0 ? z.pc > z.s200 : z.pc < z.s200), z.fomc, stopDist * fct * ppt, a[14], a[15], a[16], a[17], a[18], a.size() > 71 && a[71] >= 0 ? a[71] : 1.0}); } }
  std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.di != b.di ? a.di < b.di : a.em < b.em; });
  struct Rule { const char* n; std::function<bool(const Fl&)> f; int conc; };
  const Rule rules[] = {{"RULE  A-list + HH/HL + break, 1 open", [](const Fl& x) { return x.tf >= 3 && x.mult >= 4 && x.pull >= 15 && x.pull < 50 && x.grade == 1 && x.hh5 && x.orb > 0; }, 1},
                        {"BOOK1 A or B + HH/HL, 2 open (~1/day)", [](const Fl& x) { return x.tf >= 3 && x.mult >= 4 && x.pull >= 15 && x.pull < 50 && (x.grade == 1 || x.grade == 2) && x.hh5; }, 2},
                        {"BOOK2 4-5m A or B, pb <= 38 %, 2 open (~2/day)", [](const Fl& x) {   // env FPEV_B2_MULT / FPEV_B2_PULLMAX: preregistered/2026-10-23_realism_plateau.md F3 (defaults = BOOK2)
                           static const double mm = std::getenv("FPEV_B2_MULT") ? std::atof(std::getenv("FPEV_B2_MULT")) : 3, px = std::getenv("FPEV_B2_PULLMAX") ? std::atof(std::getenv("FPEV_B2_PULLMAX")) : 38;
                           return x.tf >= 4 && x.mult >= mm && x.pull >= 5 && x.pull < px && (x.grade == 1 || x.grade == 2); },
                         std::getenv("FPEV_BOOK2_CONC") ? std::max(1, std::atoi(std::getenv("FPEV_BOOK2_CONC"))) : 2},   // env override: preregistered/2026-10-22_book3.md R1
                        {"BULL  bull rule, 1 open", [](const Fl& x) { return x.side > 0 && x.tf >= 3 && x.mult >= 4 && x.pull >= 15 && x.pull < 50 && x.grade == 1 && x.hh5 && x.orb > 0; }, 1}};
  // 2026-10-22 `flag_prop_ev W15_NQ W15_ES stress` (founder: "more tests"): stress tests of the account EV for BOOK2 and RULE — extra cost per
  // trade, market / size mix, daily stops, EV by start year, funded horizon, and a monthly program (buy one eval every 5th trading day ≈ 4 a
  // month at a given fee). Default run unchanged.
  // 2026-10-22 `… regime` (research/preregistered/2026-10-22_book2_account.md): BOOK2 + daily loss stop, regime filters DT200 / NOFOMC / both
  // judged on three plans, and the size × stop grid. Default run unchanged.
  // 2026-10-22 `… sizing` (preregistered/2026-10-22_book2_account.md, Part 2): position sizing for BOOK2 — FIXED2 vs RISK$ / CUSHION / GRADE.
  // The eval and funded runs are payout_sim.hpp's run_eval / run_account logic copied with a size that can depend on the account's profit
  // (CUSHION); with a constant size they reproduce payout_sim exactly (checked below).
  // 2026-10-22 `… timing` (founder: "how long to take a payout"): BOOK2, 2 micros each market, daily stop $150 / $200 — trading days from buying
  // the eval to passing, from passing to the first payout, and the share of evals bought paid within 21 / 42 / 63 / 126 trading days.
  // 2026-10-22 `… evalsize` (founder: "what about risking more on the evals"): BOOK2 eval traded at q_e micros each market (2-10) with the
  // daily stop either the same $ or scaled with q_e, the funded account at 2 micros with the same funded stop ($150 / $200).
  // 2026-10-22 `… dd` (founder: "what is the worst drawdown now."): BOOK2 at 2 micros each market as ONE continuous account 2014-01 → 2026-10,
  // $ at today's ATR, the daily stop applied as in payout_sim (a trade whose worst open loss reaches the day's stop is closed there, no more
  // trades that day). Worst drawdown end-of-day and intraday (at each trade's worst point), worst day / month, longest below a high, per year.
  // 2026-10-22 `… ddfix` (preregistered/2026-10-22_book2_account.md, Part 3): weekly stop, equity-curve filter and losing-streak pause on BOOK2
  // (2 micros each market, daily stop $150), judged on the continuous drawdown and the account EV.
  // 2026-10-22 `… budget` (founder: "I have 300 usd which can buy us probably 4 evals"): 4 evals of BOOK2 (funded 2 micros, daily stop $200),
  // bought together (same start — same trades, so they move together) or one a week (starts s0, +5, +10, +15 trading days). For every historical
  // s0 (every 5th trading day 2014-01 → 2025-03): total payouts over the 4 accounts' 120 funded days, any payout by 2 / 3 / 6 months, and the
  // distribution. Plans: select_daily_50k (eval 2 micros), apex_eod_50k (eval 3 micros). $ before the split; fees are the founder's ($300 total).
  // 2026-10-22 `… prob` (founder: "expand all the sections with more research to have a full understanding of all probabilities and results"):
  // the RULE and BOOK2 books at 2 micros each market (BOOK2 with the $150 daily stop) — trade-level, day / week / month probabilities, losing
  // streaks, a month-block bootstrap (200 resamples) for ranges of R a trade, $ a month, 12-month max drawdown and the chance a 60 / 120-day
  // stretch dips past $1,000 / $2,000 / $3,000, account EV ranges (select_daily_50k, apex_eod_50k; 100 resamples), and the paper-test odds
  // (20 / 40-session windows: chance of avg >= +0.05R with the real edge and with the edge removed).
  // 2026-10-22 `… pagedata OUT.json` (founder: "show me a simulation of the full process"): the BOOK2 trades at 1 micro each market for the
  // simulator page — session dates and, per trade, the session index, net $, worst and best open $ (NQ + ES, in entry order).
  if (argc > 4 && std::string(argv[3]) == "pagedata") {
    const Rule& B2 = rules[2]; FILE* fo = fopen(argv[4], "w"); if (!fo) { perror(argv[4]); return 1; }
    fprintf(fo, "{\"dates\":["); for (size_t i = 0; i < cal.size(); ++i) fprintf(fo, "%s%d", i ? "," : "", cal[i]); fprintf(fo, "],\"t\":[");
    int lastD = -1, nt = 0; std::vector<int> busy[2];
    for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
      b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm);
      fprintf(fo, "%s[%d,%.1f,%.1f,%.1f,%d,%d]", nt++ ? "," : "", x.di, x.usd, x.mae, x.mfe, x.mk, x.em); }
    fprintf(fo, "]}\n"); fclose(fo); fprintf(stderr, "wrote %d trades over %zu sessions\n", nt, cal.size()); return 0; }
  // 2026-10-22 `… tradelist OUT.json` (founder: "show me list of trades of each year"): every BOOK2 trade, one position at a time as booked, with
  // prices, times, R (the export's, after cost) and $ at 2 micros at today's ATR, the $150 daily stop applied (the trade that hits it is cut at
  // the stop level, the day's later trades are marked skipped).
  if (argc > 4 && std::string(argv[3]) == "tradelist") {
    const Rule& B2 = rules[2]; FILE* fo = fopen(argv[4], "w"); if (!fo) { perror(argv[4]); return 1; } const double STOP = 150; const int q = 2;
    fprintf(fo, "{\"stop\":%.0f,\"q\":%d,\"t\":[", STOP, q); int lastD = -1, nt = 0; std::vector<int> busy[2]; double dp = 0; bool stopped = false;
    for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); dp = 0; stopped = false; } auto& b = busy[x.mk];
      b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm);
      double usd = q * x.usd; int status = 0;   // 0 traded, 1 cut at the daily stop, 2 skipped (stop already hit)
      if (stopped) status = 2; else { const double worst = q * std::min(x.mae, x.usd); if (dp + worst <= -STOP) { usd = -STOP - dp; status = 1; stopped = true; } dp += usd; }
      fprintf(fo, "%s[%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.3f,%.0f,%d]", nt++ ? "," : "", cal[x.di], x.em, x.xm, x.mk, x.side, x.tf, x.grade, x.ent, x.stp, x.tgt, x.xpx, x.rr, status == 2 ? 0.0 : usd, status); }
    fprintf(fo, "]}\n"); fclose(fo); fprintf(stderr, "wrote %d trades\n", nt); return 0; }
  if (argc > 3 && std::string(argv[3]) == "prob") {
    std::mt19937 rng(20261022);
    for (int bi : {2, 0}) { const Rule& RB = rules[bi]; const double STOP = bi == 2 ? 150 : 0; const int q = 2;
      std::vector<std::vector<payout::Trade>> D(cal.size()); std::vector<double> tradeR; std::vector<int> tradeDay, tradeMk; { int lastD = -1; std::vector<int> busy[2];
        for (auto& x : v) { if (!RB.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
          b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= RB.conc) continue; b.push_back(x.xm);
          D[x.di].push_back({q * x.usd, q * x.mae, q * x.mfe}); tradeR.push_back(x.usd / std::max(1.0, x.risk1)); tradeDay.push_back(x.di); tradeMk.push_back(x.mk); } }
      printf("\n################ %s — 2 micros each market%s, NQ + ES, 2014-01 → 2026-10, $ at today's ATR\n", RB.n, STOP > 0 ? ", daily stop $150" : ", no daily stop");
      // 2026-10-22 (preregistered/2026-10-22_book_tests.md): avg R of the booked trades per market × half, for the pass rule
      { double sr[2][2] = {}, nr[2][2] = {}; for (size_t t = 0; t < tradeR.size(); ++t) { const int h = cal[tradeDay[t]] >= 20200101; sr[tradeMk[t]][h] += tradeR[t]; nr[tradeMk[t]][h]++; }
        printf("  BY MARKET × HALF (avg R, trades) 2014-19 / 2020-26: NQ %+.3f (%.0f) / %+.3f (%.0f) · ES %+.3f (%.0f) / %+.3f (%.0f)\n", sr[0][0] / std::max(1.0, nr[0][0]), nr[0][0], sr[0][1] / std::max(1.0, nr[0][1]), nr[0][1],
               sr[1][0] / std::max(1.0, nr[1][0]), nr[1][0], sr[1][1] / std::max(1.0, nr[1][1]), nr[1][1]); }
      // trade level
      { std::vector<double> r = tradeR; std::sort(r.begin(), r.end()); double w = 0, s1 = 0, ge1 = 0, ge2 = 0; for (double x : r) { w += x > 0; s1 += x; ge1 += x >= 1; ge2 += x >= 2; }
        int streak = 0, longest = 0; for (double x : tradeR) { streak = x < 0 ? streak + 1 : 0; longest = std::max(longest, streak); }
        auto pct = [&](double qq) { return r[(size_t)(qq * (r.size() - 1))]; };
        printf("  TRADES: %zu (%.2f a day) · win %4.1f%% · avg %+5.3fR · R percentiles 10/25/50/75/90: %+5.2f / %+5.2f / %+5.2f / %+5.2f / %+5.2f · >= +1R %4.1f%% · >= +2R %4.1f%% · longest losing streak %d\n",
               r.size(), r.size() / (double)cal.size(), 100 * w / r.size(), s1 / r.size(), pct(.1), pct(.25), pct(.5), pct(.75), pct(.9), 100 * ge1 / r.size(), 100 * ge2 / r.size(), longest);
        // chance of a losing streak of >= k somewhere in the next 50 trades (sliding windows over history)
        printf("  chance of >= 5 / 8 / 10 losses in a row within 50 trades:"); for (int k : {5, 8, 10}) { double hit = 0, nw = 0; for (size_t i = 0; i + 50 <= tradeR.size(); i += 10) { int st = 0, lg = 0; for (size_t j = i; j < i + 50; ++j) { st = tradeR[j] < 0 ? st + 1 : 0; lg = std::max(lg, st); } hit += lg >= k; nw++; } printf(" %3.0f%%", 100 * hit / nw); } printf("\n"); }
      // day / week / month in $
      std::vector<double> day(cal.size(), 0); std::vector<int> nt(cal.size(), 0);
      for (size_t i = 0; i < cal.size(); ++i) { double dp = 0; for (auto& t : D[i]) { const double wst = std::min(t.mae, t.pnl); nt[i]++; if (STOP > 0 && dp + wst <= -STOP) { dp = -STOP; break; } dp += t.pnl; } day[i] = dp; }
      { std::vector<double> td; double pos = 0; for (size_t i = 0; i < cal.size(); ++i) if (nt[i]) { td.push_back(day[i]); pos += day[i] > 0; } std::sort(td.begin(), td.end());
        auto pc = [&](double qq) { return td[(size_t)(qq * (td.size() - 1))]; };
        printf("  DAYS: trades on %4.1f%% of sessions · traded days > 0: %4.1f%% · traded-day $ percentiles 10/25/50/75/90: %+5.0f / %+5.0f / %+5.0f / %+5.0f / %+5.0f · worst %+5.0f · best %+5.0f\n",
               100.0 * td.size() / cal.size(), 100 * pos / td.size(), pc(.1), pc(.25), pc(.5), pc(.75), pc(.9), td.front(), td.back()); }
      for (int per = 0; per < 2; ++per) { std::map<long, double> agg; for (size_t i = 0; i < cal.size(); ++i) { long key; if (per == 0) { struct tm g{}; g.tm_year = cal[i] / 10000 - 1900; g.tm_mon = cal[i] / 100 % 100 - 1; g.tm_mday = cal[i] % 100; g.tm_hour = 12; key = (long)((timegm(&g) / 86400 + 3) / 7); } else key = cal[i] / 100; agg[key] += day[i]; }
        std::vector<double> x; for (auto& [k, val] : agg) x.push_back(val); double pos = 0; int st = 0, lg = 0; for (double y : x) { pos += y > 0; st = y < 0 ? st + 1 : 0; lg = std::max(lg, st); }
        std::vector<double> xs = x; std::sort(xs.begin(), xs.end()); auto pc = [&](double qq) { return xs[(size_t)(qq * (xs.size() - 1))]; };
        printf("  %s: > 0 in %4.1f%% · $ percentiles 10/50/90: %+6.0f / %+6.0f / %+6.0f · worst %+6.0f · longest losing run %d %s\n", per ? "MONTHS" : "WEEKS ", 100 * pos / x.size(), pc(.1), pc(.5), pc(.9), xs.front(), lg, per ? "months" : "weeks"); }
      // month-block bootstrap: resample calendar months with replacement into 12-month paths
      std::map<int, std::vector<int>> monDays; for (size_t i = 0; i < cal.size(); ++i) monDays[cal[i] / 100].push_back((int)i); std::vector<int> monKeys; for (auto& [k, vv] : monDays) monKeys.push_back(k);
      { std::vector<double> mR, mUsd, mDD; double over[3][2] = {}; int ow = 0;
        for (int b = 0; b < 200; ++b) { std::vector<int> path; for (int m = 0; m < 12; ++m) { const auto& dd = monDays[monKeys[rng() % monKeys.size()]]; path.insert(path.end(), dd.begin(), dd.end()); }
          double bal = 0, pk = 0, mdd = 0, sR = 0, nR = 0; for (int i : path) { bal += day[i]; pk = std::max(pk, bal); mdd = std::max(mdd, pk - bal); }
          for (size_t t = 0; t < tradeR.size(); ++t) { (void)t; } mUsd.push_back(bal / 12); mDD.push_back(mdd);
          // 60 / 120-day windows within the path: did the drawdown from the window's own high pass $1k / 2k / 3k?
          for (int hz = 0; hz < 2; ++hz) { const int H = hz ? 120 : 60; for (size_t s0 = 0; s0 + H <= path.size(); s0 += 20) { double bb = 0, pp = 0, md = 0; for (int k = 0; k < H; ++k) { bb += day[path[s0 + k]]; pp = std::max(pp, bb); md = std::max(md, pp - bb); }
              const double L[3] = {1000, 2000, 3000}; for (int l = 0; l < 3; ++l) over[l][hz] += md > L[l]; if (hz == 0) ow++; } } (void)sR; (void)nR; }
        std::sort(mUsd.begin(), mUsd.end()); std::sort(mDD.begin(), mDD.end()); auto pc = [](const std::vector<double>& x, double qq) { return x[(size_t)(qq * (x.size() - 1))]; };
        printf("  BOOTSTRAP (200 resampled 12-month paths): $ a month 5/50/95%%: %+5.0f / %+5.0f / %+5.0f · 12-month max drawdown 50/90/95%%: $%5.0f / $%5.0f / $%5.0f\n", pc(mUsd, .05), pc(mUsd, .5), pc(mUsd, .95), pc(mDD, .5), pc(mDD, .9), pc(mDD, .95));
        double nw60 = ow, nw120 = 0; for (int b = 0; b < 1; ++b) {} (void)nw120;
        printf("  chance a 60-day stretch dips past $1,000 / $2,000 / $3,000 from its high: %3.0f%% / %3.0f%% / %3.0f%%\n", 100 * over[0][0] / nw60, 100 * over[1][0] / nw60, 100 * over[2][0] / nw60); }
      // trade-level bootstrap of the average R (resample months, 200 times)
      { std::map<int, std::vector<double>> monR; for (size_t t = 0; t < tradeR.size(); ++t) monR[cal[tradeDay[t]] / 100].push_back(tradeR[t]); std::vector<double> avg;
        for (int b = 0; b < 200; ++b) { double s1 = 0, n1 = 0; for (size_t m = 0; m < monKeys.size(); ++m) { const auto it = monR.find(monKeys[rng() % monKeys.size()]); if (it == monR.end()) continue; for (double x : it->second) { s1 += x; n1++; } } avg.push_back(s1 / std::max(1.0, n1)); }
        std::sort(avg.begin(), avg.end()); printf("  avg R a trade, 90%% range (month bootstrap): %+5.3f … %+5.3f (median %+5.3f)\n", avg[10], avg[189], avg[100]); }
      // paper-test odds: windows of 20 / 40 sessions — chance the window's avg R >= +0.05 with the real trades, and with the edge removed
      { const double mean = std::accumulate(tradeR.begin(), tradeR.end(), 0.0) / tradeR.size();
        for (int W : {20, 40, 60}) { double real = 0, zero = 0, nwin = 0, ntr = 0; for (size_t s0 = 0; s0 + W <= cal.size(); s0 += 5) { double sr = 0, sz = 0, n1 = 0;
            for (size_t t = 0; t < tradeR.size(); ++t) if (tradeDay[t] >= (int)s0 && tradeDay[t] < (int)s0 + W) { sr += tradeR[t]; sz += tradeR[t] - mean; n1++; }
            if (n1 < 5) continue; nwin++; ntr += n1; real += sr / n1 >= 0.05; zero += sz / n1 >= 0.05; }
          printf("  PAPER TEST of %d sessions (~%.0f trades): avg >= +0.05R in %3.0f%% of windows with the real edge · %3.0f%% if the edge were zero (luck)\n", W, ntr / nwin, 100 * real / nwin, 100 * zero / nwin); } }
      // account EV ranges: resample months into a new 2014-2026-length history, rerun the accounts (100 resamples)
      if (bi == 2) for (const char* pn : {"select_daily_50k", "apex_eod_50k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p; std::vector<double> evs;
        for (int b = 0; b < 100; ++b) { std::vector<int> path; while (path.size() < cal.size()) { const auto& dd = monDays[monKeys[rng() % monKeys.size()]]; path.insert(path.end(), dd.begin(), dd.end()); } path.resize(cal.size());
          std::vector<const std::vector<payout::Trade>*> ptr(path.size()); for (size_t i = 0; i < path.size(); ++i) ptr[i] = D[path[i]].empty() ? nullptr : &D[path[i]];
          double e = 0, n1 = 0; for (size_t s0 = 0; s0 + 200 < path.size(); s0 += 10) { n1++; const payout::EvalOut eo = payout::run_eval(*P, ptr, (int)s0, 60, {0, STOP, 0}); if (!eo.passed) continue; e += payout::run_account(*P, ptr, (int)s0 + eo.days, 120, {0, STOP, 0}).paid_sum; }
          evs.push_back(e / n1); }
        std::sort(evs.begin(), evs.end()); printf("  ACCOUNT EV %s (eval and funded 2 micros, $150 stop), 100 resampled histories: 5/25/50/75/95%%: $%5.0f / $%5.0f / $%5.0f / $%5.0f / $%5.0f per eval bought\n", pn, evs[5], evs[25], evs[50], evs[75], evs[95]); } }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "budget") {
    const Rule& B2 = rules[2]; auto days = [&](int q) { std::vector<std::vector<payout::Trade>> D(cal.size()); int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm); D[x.di].push_back({q * x.usd, q * x.mae, q * x.mfe}); } return D; };
    const auto D2 = days(2), D3 = days(3); auto ptrs = [&](const std::vector<std::vector<payout::Trade>>& D) { std::vector<const std::vector<payout::Trade>*> p(cal.size()); for (size_t i = 0; i < cal.size(); ++i) p[i] = D[i].empty() ? nullptr : &D[i]; return p; };
    const auto p2 = ptrs(D2), p3 = ptrs(D3);
    printf("Four evals with a $300 budget — BOOK2, funded at 2 micros each market, daily stop $200. Every historical first-purchase day (every 5th trading day 2014-01 → 2025-03).\n");
    printf("Total payouts = everything the 4 accounts pay over their 120 funded days; 'any payout by N months' counts from the first purchase. $ before the trader's split.\n");
    for (int pl = 0; pl < 2; ++pl) { const char* pn = pl ? "apex_eod_50k" : "select_daily_50k"; const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p;
      const auto& pe = pl ? p3 : p2; const int qe = pl ? 3 : 2;
      for (int stag = 0; stag < 2; ++stag) { std::vector<double> tot; double any2 = 0, any3 = 0, any6 = 0, n = 0, accPaid = 0;
        for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250331) continue; n++; double sum = 0; int first = 99999, paidN = 0;
          for (int k = 0; k < 4; ++k) { const int st = (int)s0 + (stag ? 5 * k : 0); const payout::EvalOut eo = payout::run_eval(*P, pe, st, 60, {0, 200, 0}); if (!eo.passed) continue;
            const payout::RunOut ro = payout::run_account(*P, p2, st + eo.days, 120, {0, 200, 0});
            if (ro.paid) { paidN++; first = std::min(first, st - (int)s0 + eo.days + ro.first_pay); } sum += ro.paid_sum; }
          tot.push_back(sum); any2 += first <= 42; any3 += first <= 63; any6 += first <= 126; accPaid += paidN; }
        std::sort(tot.begin(), tot.end()); double mean = 0, zero = 0, cover = 0; for (double x : tot) { mean += x; zero += x <= 0; cover += x >= 300; } mean /= tot.size();
        printf("\n  %s, eval %d micros, 4 evals %s:\n", pn, qe, stag ? "ONE A WEEK" : "ALL ON THE SAME DAY");
        printf("    any payout by 2 / 3 / 6 months: %3.0f%% / %3.0f%% / %3.0f%% · accounts paid out of 4: %.1f on average\n", 100 * any2 / n, 100 * any3 / n, 100 * any6 / n, accPaid / n);
        printf("    total payouts from the 4: mean $%5.0f · median $%5.0f · 25th pct $%5.0f · 75th pct $%5.0f · $0 in %3.0f%% of cases · >= $300 back in %3.0f%%\n",
               mean, tot[tot.size() / 2], tot[tot.size() / 4], tot[tot.size() * 3 / 4], 100 * zero / tot.size(), 100 * cover / tot.size()); } }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "ddfix") {
    const Rule& B2 = rules[2]; const double STOP = 150; const int q = 2;
    std::vector<std::vector<payout::Trade>> D(cal.size()); { int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm); D[x.di].push_back({q * x.usd, q * x.mae, q * x.mfe}); } }
    // the day's result with the daily stop (the shadow record)
    std::vector<double> dayR(cal.size(), 0); for (size_t i = 0; i < cal.size(); ++i) { double dp = 0; for (auto& t : D[i]) { const double w = std::min(t.mae, t.pnl); if (dp + w <= -STOP) { dp = -STOP; break; } dp += t.pnl; } dayR[i] = dp; }
    auto weekOf = [&](int ymd) { struct tm g{}; g.tm_year = ymd / 10000 - 1900; g.tm_mon = ymd / 100 % 100 - 1; g.tm_mday = ymd % 100; g.tm_hour = 12; const time_t t = timegm(&g); return (long)((t / 86400 + 3) / 7); };
    // which days are traded under each rule (decided with information up to the day before, except the weekly stop which uses the week so far)
    auto tradeMask = [&](int rule) { std::vector<char> on(cal.size(), 1); double eq = 0; std::vector<double> eqs; long wk = -1; double wkR = 0; int streak = 0; bool paused = false;
      for (size_t i = 0; i < cal.size(); ++i) { if (rule == 1 || rule == 2) { const long w = weekOf(cal[i]); if (w != wk) { wk = w; wkR = 0; } if (wkR <= -(rule == 1 ? 300.0 : 500.0)) on[i] = 0; }
        if (rule == 3 || rule == 4) { const int n = rule == 3 ? 10 : 20; if ((int)eqs.size() >= n) { double ma = 0; for (int k = 0; k < n; ++k) ma += eqs[eqs.size() - 1 - k]; ma /= n; on[i] = eqs.back() >= ma; } }
        if (rule == 5) on[i] = !paused;
        if (on[i]) wkR += dayR[i]; eq += dayR[i]; eqs.push_back(eq);
        if (rule == 5) { if (D[i].empty()) {} else if (dayR[i] < 0) { if (++streak >= 3) paused = true; } else if (dayR[i] > 0) { streak = 0; paused = false; } } }
      return on; };
    static const char* RN[6] = {"base (daily stop $150 only)", "WEEK300", "WEEK500", "EQ10", "EQ20", "STREAK3"};
    printf("Cutting the drawdown — BOOK2, 2 micros each market, daily stop $150. Continuous account 2014-01 → 2026-10 at today's ATR, and the account EV.\n");
    double baseDD = 0, baseH[2] = {0, 0}, baseEV[3] = {0, 0, 0};
    for (int r = 0; r < 6; ++r) { const auto on = tradeMask(r);
      double bal = 0, peak = 0, dd = 0, sum = 0, ddH[2] = {0, 0}; int days = 0; for (size_t i = 0; i < cal.size(); ++i) { if (on[i]) { bal += dayR[i]; sum += dayR[i]; days += !D[i].empty(); } peak = std::max(peak, bal); dd = std::max(dd, peak - bal); const int h = cal[i] >= 20200101; ddH[h] = std::max(ddH[h], peak - bal); }
      // EV with only the allowed days
      std::vector<const std::vector<payout::Trade>*> ptr(cal.size()); for (size_t i = 0; i < cal.size(); ++i) ptr[i] = on[i] && !D[i].empty() ? &D[i] : nullptr;
      double ev[3]; const char* SP[3] = {"select_flex_50k", "select_daily_50k", "apex_eod_50k"};
      for (int pi = 0; pi < 3; ++pi) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == SP[pi]) P = &p; double e = 0, n = 0;
        for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; n++; const payout::EvalOut eo = payout::run_eval(*P, ptr, (int)s0, 60, {0, STOP, 0}); if (!eo.passed) continue;
          e += payout::run_account(*P, ptr, (int)s0 + eo.days, 120, {0, STOP, 0}).paid_sum; } ev[pi] = e / n; }
      if (r == 0) { baseDD = dd; baseH[0] = ddH[0]; baseH[1] = ddH[1]; for (int k = 0; k < 3; ++k) baseEV[k] = ev[k]; }
      int evOk = 0; for (int k = 0; k < 3; ++k) evOk += ev[k] >= baseEV[k]; const bool pass = r > 0 && dd <= 0.6 * baseDD && evOk >= 2 && ddH[0] < baseH[0] && ddH[1] < baseH[1];
      printf("  %-28s worst DD $%5.0f (2014-19 $%5.0f / 2020-26 $%5.0f) · $%5.0f a month · traded on %4d days · EV per eval: flex $%5.0f · daily $%5.0f · apex $%5.0f%s\n", RN[r], dd, ddH[0], ddH[1],
             sum / (cal.size() / 21.0), days, ev[0], ev[1], ev[2], pass ? "  ← PASSES" : ""); }
    // bigger accounts (more drawdown room): the base book at 2 and 3 micros on the 50K / 100K / 150K Select plans (reported, not judged)
    printf("\n  Bigger accounts, base book (daily stop $150 at 2 micros, scaled at 3), $ per eval bought · pass %% · buy → 1st payout median days:\n");
    for (const char* pn : {"select_flex_50k", "select_flex_100k", "select_flex_150k", "select_daily_50k", "select_daily_100k", "select_daily_150k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p; if (!P) continue;
      for (int qq : {2, 3}) { std::vector<std::vector<payout::Trade>> Dq(cal.size()); for (size_t i = 0; i < cal.size(); ++i) for (auto& t : D[i]) Dq[i].push_back({t.pnl * qq / 2, t.mae * qq / 2, t.mfe * qq / 2});
        std::vector<const std::vector<payout::Trade>*> pq(cal.size()); for (size_t i = 0; i < cal.size(); ++i) pq[i] = Dq[i].empty() ? nullptr : &Dq[i]; const double st = STOP * qq / 2;
        double e = 0, n = 0, ps = 0; std::vector<int> tt; for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; n++; const payout::EvalOut eo = payout::run_eval(*P, pq, (int)s0, 60, {0, st, 0}); if (!eo.passed) continue;
          ps++; const payout::RunOut ro = payout::run_account(*P, pq, (int)s0 + eo.days, 120, {0, st, 0}); e += ro.paid_sum; if (ro.paid) tt.push_back(eo.days + ro.first_pay); }
        std::sort(tt.begin(), tt.end()); printf("    %-18s %d micros (stop $%.0f): $%5.0f · pass %3.0f%% · %3d days\n", pn, qq, st, e / n, 100 * ps / n, tt.empty() ? -1 : tt[tt.size() / 2]); } }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "dd") {
    const Rule& B2 = rules[2];
    for (int q : {2, 1}) for (double stop : {0.0, 150.0, 200.0}) {
      std::vector<std::vector<payout::Trade>> D(cal.size()); { int lastD = -1; std::vector<int> busy[2];
        for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
          b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm); D[x.di].push_back({q * x.usd, q * x.mae, q * x.mfe}); } }
      double bal = 0, peak = 0, ddE = 0, ddI = 0, worstDay = 0, sum = 0; int below = 0, longest = 0, belowStart = 0, lonStart = 0, lonEnd = 0; std::map<int, double> mon; std::map<int, double> yDD; std::map<int, double> yPk;
      for (size_t i = 0; i < cal.size(); ++i) { double dp = 0; for (auto& t : D[i]) { const double worst = std::min(t.mae, t.pnl); ddI = std::max(ddI, peak - (bal + dp + worst));
          if (stop > 0 && dp + worst <= -stop) { dp = -stop; break; } dp += t.pnl; }
        bal += dp; sum += dp; worstDay = std::min(worstDay, dp); mon[cal[i] / 100] += dp;
        if (bal >= peak) { peak = bal; if (below > longest) { longest = below; lonStart = belowStart; lonEnd = cal[i]; } below = 0; } else { if (!below) belowStart = cal[i]; below++; }
        ddE = std::max(ddE, peak - bal); const int y = cal[i] / 10000; yDD[y] = std::max(yDD[y], peak - bal); }
      if (below > longest) { longest = below; lonStart = belowStart; lonEnd = cal.back(); }
      double worstMon = 0; int negMon = 0; for (auto& [m, x] : mon) { worstMon = std::min(worstMon, x); negMon += x < 0; }
      printf("\n######## BOOK2, %d micro%s each market, daily stop %s\n", q, q > 1 ? "s" : "", stop > 0 ? ("$" + std::to_string((int)stop)).c_str() : "none");
      printf("  total $%.0f over %zu sessions = $%.0f a month · worst drawdown END OF DAY $%.0f · INTRADAY (at the worst open point) $%.0f\n", sum, cal.size(), sum / (cal.size() / 21.0), ddE, ddI);
      printf("  worst day $%.0f · worst month $%.0f · losing months %d of %zu · longest below a high %d sessions (%d → %d)\n", worstDay, worstMon, negMon, mon.size(), longest, lonStart, lonEnd);
      printf("  worst drawdown reached in each year (from the running high): "); for (auto& [y, d] : yDD) printf("%d $%.0f · ", y, d); printf("\n"); }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "evalsize") {
    const Rule& B2 = rules[2]; auto days = [&](int q) { std::vector<std::vector<payout::Trade>> D(cal.size()); int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm); D[x.di].push_back({q * x.usd, q * x.mae, q * x.mfe}); } return D; };
    const auto DF = days(2); std::vector<const std::vector<payout::Trade>*> pf(cal.size()); for (size_t i = 0; i < cal.size(); ++i) pf[i] = DF[i].empty() ? nullptr : &DF[i];
    printf("Bigger size in the eval — BOOK2, eval at q micros each market, funded at 2 micros; starts every 5th trading day 2014-01 → 2025-04, eval <= 60 days, funded 120.\n");
    printf("Columns: pass %%, median days to pass, buy → 1st payout median days, paid within 2 / 3 months (share of all evals), $ per eval bought (before fees and split).\n");
    for (const char* pn : {"select_flex_50k", "select_daily_50k", "apex_eod_50k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p; if (!P) continue;
      for (double fs : {150.0, 200.0}) { printf("\n######## %s, funded 2 micros, funded daily stop $%.0f\n", pn, fs);
        for (int scaled = 0; scaled < 2; ++scaled) for (int qe : {2, 3, 4, 6, 8, 10}) { if (scaled && qe == 2) continue; const auto DE = days(qe); std::vector<const std::vector<payout::Trade>*> pe(cal.size());
            for (size_t i = 0; i < cal.size(); ++i) pe[i] = DE[i].empty() ? nullptr : &DE[i]; const double es = scaled ? fs * qe / 2.0 : fs;
            double n = 0, pass = 0, ev = 0, w2 = 0, w3 = 0; std::vector<int> dp, dt;
            for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; n++; const payout::EvalOut eo = payout::run_eval(*P, pe, (int)s0, 60, {0, es, 0}); if (!eo.passed) continue;
              pass++; dp.push_back(eo.days); const payout::RunOut ro = payout::run_account(*P, pf, (int)s0 + eo.days, 120, {0, fs, 0}); ev += ro.paid_sum;
              if (ro.paid) { const int t = eo.days + ro.first_pay; dt.push_back(t); w2 += t <= 42; w3 += t <= 63; } }
            auto med = [](std::vector<int> x) { if (x.empty()) return -1; std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
            printf("    eval %2d micros, eval stop $%4.0f%s: pass %3.0f%% in %2d days · buy → payout %3d days · paid in 2 / 3 months %3.0f%% / %3.0f%% · $%5.0f per eval bought\n", qe, es, scaled ? " (scaled)" : "         ",
                   100 * pass / n, med(dp), med(dt), 100 * w2 / n, 100 * w3 / n, ev / n); } } }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "timing") {
    const Rule& B2 = rules[2]; std::vector<std::vector<payout::Trade>> D(cal.size()); { int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm); D[x.di].push_back({2 * x.usd, 2 * x.mae, 2 * x.mfe}); } }
    std::vector<const std::vector<payout::Trade>*> ptr(cal.size()); for (size_t i = 0; i < cal.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
    printf("Time to the first payout — BOOK2, 2 micros each market (MNQ + MES), starts every 5th trading day 2014-01 → 2025-04, eval <= 60 days, funded 120 days.\n");
    printf("Trading days (about 21 a month). 'Paid within N days' = share of ALL evals bought whose first payout came within N trading days of buying.\n");
    for (const char* pn : {"select_flex_50k", "select_daily_50k", "apex_eod_50k", "select_flex_25k", "select_daily_25k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p; if (!P) continue;
      for (double stop : {150.0, 200.0}) { std::vector<int> dPass, dFund, dTot; double n = 0, w[4] = {0, 0, 0, 0}, first = 0, nPay = 0; const int H[4] = {21, 42, 63, 126};
        for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; n++; const payout::EvalOut eo = payout::run_eval(*P, ptr, (int)s0, 60, {0, stop, 0}); if (!eo.passed) continue;
          dPass.push_back(eo.days); const payout::RunOut ro = payout::run_account(*P, ptr, (int)s0 + eo.days, 120, {0, stop, 0}); if (!ro.paid) continue;
          dFund.push_back(ro.first_pay); const int tot = eo.days + ro.first_pay; dTot.push_back(tot); for (int k = 0; k < 4; ++k) w[k] += tot <= H[k]; nPay += ro.payouts; first += ro.pay_days.empty() ? 0 : 1; }
        auto med = [](std::vector<int> x) { if (x.empty()) return -1; std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
        auto pct = [](std::vector<int> x, double q) { if (x.empty()) return -1; std::sort(x.begin(), x.end()); return x[(size_t)(q * (x.size() - 1))]; };
        printf("  %-17s stop $%3.0f: pass %3.0f%% in median %2d days · funded → 1st payout median %2d days · buy → 1st payout median %3d days (25th %3d, 75th %3d) · paid within 1 / 2 / 3 / 6 months %3.0f%% / %3.0f%% / %3.0f%% / %3.0f%% · %.1f payouts per paid account\n",
               pn, stop, 100 * dPass.size() / n, med(dPass), med(dFund), med(dTot), pct(dTot, 0.25), pct(dTot, 0.75), 100 * w[0] / n, 100 * w[1] / n, 100 * w[2] / n, 100 * w[3] / n, first ? nPay / first : 0); } }
    return 0; }
  if (argc > 3 && std::string(argv[3]) == "sizing") {
    const Rule& B2 = rules[2]; auto planOf = [&](const char* n) { for (auto& p : payout::plans()) if (p.name == n) return &p; return (const payout::Plan*)nullptr; };
    struct T1 { double pnl, mae, mfe; int q; };
    using Days = std::vector<std::vector<T1>>;
    auto evalS = [](const payout::Plan& p, const Days& D, int s0, int horizon, double stop, double X, int& daysOut) -> bool {
      double bal = p.start, peak = p.start, floor = p.start - p.drawdown, best = 0; const double lock = p.start + p.lock_above_start;
      double cut = p.eval_daily_loss; if (stop > 0 && (cut <= 0 || stop < cut)) cut = stop;
      for (int k = 0; k < horizon && s0 + k < (int)D.size(); ++k) { double dp = 0; const double m = X > 0 ? (bal - p.start >= X ? 2 : 1) : 1;
        for (const T1& t : D[s0 + k]) { const double q = m * t.q, worst = q * std::min(t.mae, t.pnl); if (bal + dp + worst <= floor) { daysOut = k + 1; return false; }
          if (cut > 0 && dp + worst <= -cut) { dp = -cut; break; } dp += q * t.pnl; }
        bal += dp; daysOut = k + 1; best = std::max(best, dp); peak = std::max(peak, bal); floor = std::max(floor, std::min(peak - p.drawdown, lock));
        const double profit = bal - p.start; if (profit >= p.eval_target && daysOut >= p.eval_min_days && (p.eval_consistency <= 0 || best <= p.eval_consistency * profit)) return true; }
      daysOut = horizon; return false; };
    auto acctS = [](const payout::Plan& p, const Days& D, int s0, int horizon, double stop, double X) -> double {
      double cut = p.daily_loss; if (stop > 0 && (cut <= 0 || stop < cut)) cut = stop;
      double bal = p.start, peak = p.start, floor = p.start - p.drawdown; const double lock = p.start + p.lock_above_start; double base = p.start;
      auto trail = [&](double eq) { peak = std::max(peak, eq); floor = std::max(floor, std::min(peak - p.drawdown, lock)); };
      int wins = 0, payouts = 0; double best = 0, cyc = 0, paid = 0;
      for (int k = 0; k < horizon && s0 + k < (int)D.size(); ++k) { double dp = 0; const double m = X > 0 ? (bal - base >= X ? 2 : 1) : 1;
        for (const T1& t : D[s0 + k]) { const double q = m * t.q; if (p.intraday_trail && t.mfe > 0) trail(bal + dp + q * t.mfe); const double worst = q * std::min(t.mae, t.pnl);
          if (bal + dp + worst <= floor) return paid; if (cut > 0 && dp + worst <= -cut) { dp = -cut; break; } dp += q * t.pnl; if (p.intraday_trail) trail(bal + dp); }
        bal += dp; if (dp >= p.win_day_min) ++wins; cyc += dp; best = std::max(best, dp); trail(bal);
        const double profit = bal - p.start; const bool consistent = p.consistency <= 0 || (cyc > 0 && best <= p.consistency * cyc);
        if (wins >= p.win_days_needed && profit > 0 && consistent && (payouts == 0 || cyc > 0)) { const double cap = p.caps[std::min<size_t>(payouts, p.caps.size() - 1)]; double amt = 0;
          if (p.payout_mult > 0) { if (profit > p.buffer && cyc > 0) amt = std::min({cap, p.payout_mult * cyc, profit - p.buffer}); if (amt < p.min_payout) amt = 0; }
          else if (profit >= p.min_profit) amt = std::min(cap, p.profit_share * profit);
          if (amt > 0) { bal -= amt; paid += amt; ++payouts; wins = 0; best = 0; cyc = 0; peak = bal; base = bal; } } }
      return paid; };
    struct Ev { double ev = 0, n = 0, h[2] = {0, 0}, hn[2] = {0, 0}, pass = 0; double m() const { return n ? ev / n : 0; } double hm(int k) const { return hn[k] ? h[k] / hn[k] : 0; } };
    auto build = [&](std::function<int(const Fl&)> qOf, double extraPts) { Days D(cal.size()); int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm);
        const double ex = (x.mk ? extraPts / 3 : extraPts) * x.ppt; D[x.di].push_back({x.usd - ex, x.mae - ex, x.mfe, qOf(x)}); } return D; };
    auto run = [&](const payout::Plan& P, const Days& D, double stop, double X) { Ev o;
      for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; int dd = 0; const bool ok = evalS(P, D, (int)s0, 60, stop, X, dd);
        const double got = ok ? acctS(P, D, (int)s0 + dd, 120, stop, X) : 0; const int k = cal[s0] >= 20200101; o.n++; o.ev += got; o.h[k] += got; o.hn[k]++; o.pass += ok; } return o; };
    { const Days D = build([](const Fl&) { return 1; }, 0); printf("check (1 micro, $150 stop, select_flex_50k): $%.0f per eval (payout_sim gave $1,973)\n", run(*planOf("select_flex_50k"), D, 150, 0).m()); }
    struct Me { const char* n; std::function<int(const Fl&)> q; double X; };
    const Me me[] = {{"FIXED2 (2 micros)", [](const Fl&) { return 2; }, 0},
                     {"RISK$ $40 a trade", [](const Fl& x) { return std::max(1, std::min(4, (int)std::lround(40 / std::max(1.0, x.risk1)))); }, 0},
                     {"RISK$ $60 a trade", [](const Fl& x) { return std::max(1, std::min(4, (int)std::lround(60 / std::max(1.0, x.risk1)))); }, 0},
                     {"RISK$ $80 a trade", [](const Fl& x) { return std::max(1, std::min(4, (int)std::lround(80 / std::max(1.0, x.risk1)))); }, 0},
                     {"CUSHION 1 → 2 at +$500", [](const Fl&) { return 1; }, 500}, {"CUSHION 1 → 2 at +$1,000", [](const Fl&) { return 1; }, 1000},
                     {"GRADE A 2 / B 1", [](const Fl& x) { return x.grade == 1 ? 2 : 1; }, 0}};
    { double r = 0, n = 0; for (auto& x : v) if (B2.f(x)) { r += x.risk1; n++; } printf("BOOK2 trades' stop risk at 1 micro: average $%.0f (NQ $2 / ES $5 a point at today's ATR)\n", r / n); }
    const char* SP[3] = {"select_flex_50k", "select_daily_50k", "apex_eod_50k"}; bool adopt[7]; for (bool& a : adopt) a = true;
    for (double stop : {150.0, 200.0}) for (int pi = 0; pi < 3; ++pi) { const payout::Plan& P = *planOf(SP[pi]); printf("\n######## %s, daily stop $%.0f — $ per eval bought (2014-19 / 2020-25) · with +1 pt cost · pass %%\n", SP[pi], stop);
      Ev base, baseC;
      for (int k = 0; k < 7; ++k) { const Days D = build(me[k].q, 0), DC = build(me[k].q, 1.0); const Ev e = run(P, D, stop, me[k].X), ec = run(P, DC, stop, me[k].X); if (k == 0) { base = e; baseC = ec; }
        const bool ok = k > 0 && e.m() >= 1.10 * base.m() && e.hm(0) > base.hm(0) && e.hm(1) > base.hm(1) && ec.m() > baseC.m(); if (k > 0 && !ok) adopt[k] = false;
        printf("    %-26s $%5.0f ($%5.0f / $%5.0f) · cost $%5.0f · pass %3.0f%%%s\n", me[k].n, e.m(), e.hm(0), e.hm(1), ec.m(), 100 * e.pass / e.n, ok ? "  ← better" : ""); } }
    printf("\nADOPTED (better than FIXED2 at both stops, all three plans, both halves, with cost):"); for (int k = 1; k < 7; ++k) printf(" %s %s ·", me[k].n, adopt[k] ? "YES" : "no"); printf("\n");
    return 0; }
  if (argc > 3 && (std::string(argv[3]) == "regime" || std::string(argv[3]) == "firms")) {
    const Rule& B2 = rules[2]; auto planOf = [&](const char* n) { for (auto& p : payout::plans()) if (p.name == n) return &p; return (const payout::Plan*)nullptr; };
    struct Ev { double ev = 0, n = 0, h[2] = {0, 0}, hn[2] = {0, 0}, pass = 0, paid = 0; double m() const { return n ? ev / n : 0; } double hm(int k) const { return hn[k] ? h[k] / hn[k] : 0; } };
    auto evRun = [&](const payout::Plan& P, int q, double stop, double extraPts, std::function<bool(const Fl&)> filt) {
      std::vector<std::vector<payout::Trade>> D(cal.size()); int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!B2.f(x) || !filt(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= B2.conc) continue; b.push_back(x.xm);
        const double ex = (x.mk ? extraPts / 3 : extraPts) * x.ppt; D[x.di].push_back({q * (x.usd - ex), q * (x.mae - ex), q * x.mfe}); }
      std::vector<const std::vector<payout::Trade>*> ptr(cal.size()); for (size_t i = 0; i < cal.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
      Ev o; const payout::Rules dr{0, stop, 0};
      for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const payout::EvalOut eo = payout::run_eval(P, ptr, (int)s0, 60, dr); double got = 0; bool paid = false;
        if (eo.passed) { const payout::RunOut ro = payout::run_account(P, ptr, (int)s0 + eo.days, 120, dr); got = ro.paid_sum; paid = ro.paid; }
        const int k = cal[s0] >= 20200101; o.n++; o.ev += got; o.h[k] += got; o.hn[k]++; o.pass += eo.passed; o.paid += paid; } return o; };
    const char* SP[3] = {"select_flex_50k", "select_daily_50k", "apex_eod_50k"}; const int SQ[3] = {1, 1, 2};
    struct Fi { const char* n; std::function<bool(const Fl&)> f; };
    const Fi fis[] = {{"base (no filter)", [](const Fl&) { return true; }}, {"DT200", [](const Fl& x) { return x.dt200; }}, {"NOFOMC", [](const Fl& x) { return !x.fomc; }}, {"DT200 + NOFOMC", [](const Fl& x) { return x.dt200 && !x.fomc; }}};
    // 2026-10-22 `… firms` (preregistered/2026-10-22_book_tests.md T8): BOOK2 through EVERY plan in payout_sim.hpp, 1-5 micros each market,
    // daily stop $150 / $200 — $ per eval bought (before fees and the split), pass %, paid % of funded; rules as coded there
    if (std::string(argv[3]) == "firms") {
      printf("BOOK2 through every prop plan in payout_sim.hpp — $ of payouts per eval bought (before fees and the split), starts every 5th trading day 2014-01 → 2025-04, eval <= 60 days, funded 120.\n");
      for (auto& P : payout::plans()) { double best = -1; int bq = 0; double bst = 0; printf("  %-18s", P.name.c_str());
        for (double st : {150.0, 200.0}) for (int q : {1, 2, 3, 4, 5}) { const Ev e = evRun(P, q, st, 0, fis[0].f); if (e.m() > best) { best = e.m(); bq = q; bst = st; } }
        for (double st : {150.0, 200.0}) { const Ev e = evRun(P, 2, st, 0, fis[0].f); printf(" | 2 micros stop $%.0f: $%5.0f pass %3.0f%% paid %3.0f%%", st, e.m(), 100 * e.pass / e.n, e.pass ? 100 * e.paid / e.pass : 0); }
        const Ev eb = evRun(P, bq, bst, 0, fis[0].f); printf(" | BEST %d micros stop $%.0f: $%5.0f (2014-19 $%5.0f / 2020-25 $%5.0f) pass %3.0f%%\n", bq, bst, eb.m(), eb.hm(0), eb.hm(1), 100 * eb.pass / eb.n); }
      return 0; }
    printf("BOOK2 account EV with a $150 daily loss stop — regime filters (judged) and size × stop (reported). preregistered/2026-10-22_book2_account.md.\n");
    printf("$ of payouts per eval bought (before fees and the split), starts every 5th trading day 2014-01 → 2025-04; halves = starts 2014-19 / 2020-25.\n");
    bool adopt[4] = {false, true, true, true};
    for (int pi = 0; pi < 3; ++pi) { const payout::Plan& P = *planOf(SP[pi]); printf("\n######## A  %s, %d micro(s) each market, daily stop $150\n", SP[pi], SQ[pi]);
      Ev base = evRun(P, SQ[pi], 150, 0, fis[0].f), baseC = evRun(P, SQ[pi], 150, 1.0, fis[0].f);
      for (int fi = 0; fi < 4; ++fi) { const Ev e = fi ? evRun(P, SQ[pi], 150, 0, fis[fi].f) : base, ec = fi ? evRun(P, SQ[pi], 150, 1.0, fis[fi].f) : baseC;
        const bool ok = e.m() >= 1.10 * base.m() && e.hm(0) > base.hm(0) && e.hm(1) > base.hm(1) && ec.m() > baseC.m(); if (fi && !ok) adopt[fi] = false;
        printf("    %-18s pass %3.0f%% · paid %3.0f%% of funded · $%5.0f per eval (2014-19 $%5.0f / 2020-25 $%5.0f) · with +1 pt cost $%5.0f%s\n", fis[fi].n, 100 * e.pass / e.n, e.pass ? 100 * e.paid / e.pass : 0, e.m(), e.hm(0), e.hm(1), ec.m(),
               fi ? (ok ? "  ← better on this plan" : "") : ""); } }
    printf("\n  ADOPTED (better on all three plans, both halves, and with cost): DT200 %s · NOFOMC %s · DT200 + NOFOMC %s\n", adopt[1] ? "YES" : "no", adopt[2] ? "YES" : "no", adopt[3] ? "YES" : "no");
    for (int pi = 0; pi < 3; ++pi) { const payout::Plan& P = *planOf(SP[pi]); printf("\n######## B  %s — size × daily stop, no regime filter ($ per eval bought; 2014-19 / 2020-25)\n    %-10s", SP[pi], "");
      const double ST[5] = {100, 150, 200, 300, 400}; for (double st : ST) printf(" | stop $%-17.0f", st); printf("\n");
      for (int q : {1, 2, 3}) { printf("    %d micro%s ", q, q > 1 ? "s" : " "); for (double st : ST) { const Ev e = evRun(P, q, st, 0, fis[0].f); printf(" | $%5.0f (%5.0f/%5.0f)  ", e.m(), e.hm(0), e.hm(1)); } printf("\n"); } }
    return 0; }
  if (argc > 3 && (std::string(argv[3]) == "stress" || std::string(argv[3]) == "mix" || std::string(argv[3]) == "mtm")) {
    struct Out { double pass = 0, paid = 0, ev = 0, n = 0; std::map<int, std::pair<double, double>> byYear; std::vector<std::pair<int, double>> perStart; };
    auto evRun = [&](const Rule& R, const payout::Plan& P, int qNQ, int qES, double extraPts, const payout::Rules& dr, int horizon) {
      std::vector<std::vector<payout::Trade>> D(cal.size()); int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!R.f(x)) continue; const int q = x.mk ? qES : qNQ; if (q <= 0) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= R.conc) continue; b.push_back(x.xm);
        const double ex = (x.mk ? extraPts / 3 : extraPts) * x.ppt;   // ES extra = a third of NQ's in points (tick 0.25 both; ES moves ~1/3 as many points)
        D[x.di].push_back({q * (x.usd - ex), q * (x.mae - ex), q * x.mfe}); }
      std::vector<const std::vector<payout::Trade>*> ptr(cal.size()); for (size_t i = 0; i < cal.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
      Out o; for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const payout::EvalOut eo = payout::run_eval(P, ptr, (int)s0, 60, dr); double got = 0; bool paid = false;
        if (eo.passed) { const payout::RunOut ro = payout::run_account(P, ptr, (int)s0 + eo.days, horizon, dr); got = ro.paid_sum; paid = ro.paid; }
        o.n++; o.pass += eo.passed; o.paid += paid; o.ev += got; auto& y = o.byYear[cal[s0] / 10000]; y.first += got; y.second++; o.perStart.push_back({cal[s0], got}); }
      return o; };
    auto planOf = [&](const char* n) { for (auto& p : payout::plans()) if (p.name == n) return &p; return (const payout::Plan*)nullptr; };
    const char* SP[3] = {"select_flex_50k", "select_daily_50k", "apex_eod_50k"}; const int SQ[2][3] = {{1, 1, 2}, {4, 5, 4}};   // BOOK2 / RULE best sizes per plan (default run)
    const Rule* RR[2] = {&rules[2], &rules[0]};
    // 2026-10-22 `… mix` (preregistered/2026-10-22_book_more.md T7): BOOK2 at NQ / ES micros 2/2 (base), 3/1, 3/2, 4/1, 2/1, 4/2 on three plans,
    // daily stop $150 / $200, history starts every 5th trading day 2014-01 → 2025-04.
    // 2026-10-22 `… mtm` (preregistered/2026-10-22_book3.md validation): the daily stop applied MARK-TO-MARKET. The default modes test the
    // stop one trade at a time (day so far + that trade's worst point), which misses two trades losing at the same minute. Here every
    // minute marks all open trades together at their adverse extreme of the minute (low for longs, high for shorts — all at once, the
    // conservative reading), capped at each stop; when the day's realised + open worst reaches −STOP everything is closed at −STOP and
    // nothing more is taken. Each day becomes one payout::Trade {day P&L, worst point, best point} for run_eval / run_account. BOOK2 rule
    // (env FPEV_BOOK2_CONC), 2 micros each market, daily stop $150 and $200, starts every 5th trading day 2014-01 → 2025-04.
    if (std::string(argv[3]) == "mtm") {
      const Rule& R = rules[std::getenv("FPEV_MTM_RULE") ? std::atoi(std::getenv("FPEV_MTM_RULE")) : 2];   // 0 RULE · 2 BOOK2 (default)
      // Builder 50K (founder's paste 2026-10-22, firm not named): eval $3K target, $2K EOD drawdown, $1K daily loss, 4 minis, no eval
      // consistency, 1 day min; funded $2K EOD, $1K DLL, 50 % consistency, payouts every 2 days above a $2.1K buffer, min $500, max $2,000,
      // 80 % split (EV here is before the split, like every plan). Floor lock ASSUMED start + 100, payout = everything above the buffer
      // (MFF-style payout_mult), "every 2 days" modelled as any day.
      static const payout::Plan builder{"builder_50k", 50000, 2000, 100, 1000, 0, 0, 0, 0.50, 0, {2000}, false, 2100, 100, 500, 4, 3000, 0, 1, 1000};
      // Builder 25K (founder's paste 2026-10-22, same firm): eval $1.5K target, $1K EOD drawdown, $600 daily loss, 2 minis (20 micros), no eval
      // consistency; funded $1K EOD, $600 DLL, 50 % consistency, payouts every 2 days above a $1.1K buffer, min $250, max $1,000, 80 % split.
      // Same assumptions as builder_50k (floor lock +100, payout = everything above the buffer, any day).
      static const payout::Plan builder25{"builder_25k", 25000, 1000, 100, 600, 0, 0, 0, 0.50, 0, {1000}, false, 1100, 100, 250, 2, 1500, 0, 1, 600};
      std::vector<const payout::Plan*> PL = {planOf("select_daily_50k"), planOf("apex_eod_50k"), &builder, planOf("topstep_50k"), planOf("mff_rapid_eod_50k"), planOf("lucid_flex_50k"), &builder25, planOf("select_daily_25k"), planOf("select_flex_25k"), planOf("mff_rapid_50k"), planOf("apex_intraday_50k"), planOf("growth_50k"),
                                             planOf("select_daily_100k"), planOf("select_daily_150k"), planOf("select_flex_100k"), planOf("select_flex_150k")};
      // sweep knobs: micros per market, and the slippage of flattening at the daily stop (1 tick per micro still open: MNQ $0.50, MES $1.25)
      const int qq = std::getenv("FPEV_MTM_Q") ? std::max(1, std::atoi(std::getenv("FPEV_MTM_Q"))) : 2; const char* tag = std::getenv("FPEV_MTM_TAG") ? std::getenv("FPEV_MTM_TAG") : "-";
      // optimisation knobs (preregistered/2026-10-22_book_optimize.md), all off by default:
      auto envd = [](const char* k, double d) { const char* e = std::getenv(k); return e ? std::atof(e) : d; };
      const int GLOBAL = (int)envd("FPEV_MTM_GLOBAL", 0);          // max open trades across BOTH markets (0 = only the per-market rule)
      const double RISKUSD = envd("FPEV_MTM_RISKUSD", 0);         // size each trade to ~this $ risk: micros = clamp(round(X / 1-micro risk), 1, RISKMAX)
      const int RISKMAX = (int)envd("FPEV_MTM_RISKMAX", 3);        // cap on those micros (2 minis = 20)
      const double PROG = envd("FPEV_MTM_PROG", 0);               // > 0: 5-account programs, success = an account paid >= this (before the split)
      const double MAXRISK = envd("FPEV_MTM_MAXRISK", 0);         // skip trades whose 1-micro stop risk exceeds this $
      const int LASTENTRY = (int)envd("FPEV_MTM_LASTENTRY", 0);   // no entries at / after this tday minute (11:00 = 1020, 12:00 = 1080)
      const bool GRADEA = envd("FPEV_MTM_GRADEA", 0) > 0;          // A grade only
      const int MKT = (int)envd("FPEV_MTM_MKT", -1);               // 0 NQ only · 1 ES only
      const double PSTOP = envd("FPEV_MTM_PSTOP", 0);             // lock the day when realised + open (at the minute's close) reaches +this
      const int MAXLOSS = (int)envd("FPEV_MTM_MAXLOSS", 0);       // no new entries after this many losing trades in the day
      const int QF = (int)envd("FPEV_MTM_QF", 0);                 // funded micros (0 = same as the eval's)
      const bool NOSCALE = envd("FPEV_MTM_NOSCALE", 0) > 0;
      const double EXTRA = envd("FPEV_MTM_EXTRA", 0);             // extra cost per trade in NQ points (ES a third), robustness check       // $ in actual points (no rescaling to today's ATR) — for the /flags page check only
      const bool closeMark = std::getenv("FPEV_MTM_CLOSE") != nullptr;   // mark open trades at the minute's close instead of its extreme (the optimistic bound)
      std::vector<double> stops = {150.0, 200.0}; if (std::getenv("FPEV_MTM_NOSTOP")) stops = {1e12};   // check: with no stop the total must equal the trades' own sum
      if (const char* e = std::getenv("FPEV_MTM_STOPS")) { stops.clear(); std::stringstream ss(e); std::string t; while (std::getline(ss, t, ',')) { const double x = std::atof(t.c_str()); stops.push_back(x > 0 ? x : 1e12); } }   // 0 = no daily stop of our own
      for (double STOP : stops) {
        std::vector<std::vector<payout::Trade>> Dm(cal.size()); int lastD = -1; std::vector<int> busy[2]; std::map<int, std::vector<const Fl*>> byDay;
        for (auto& x : v) { if (!R.f(x)) continue; if (GRADEA && x.grade != 1) continue; if (MKT >= 0 && x.mk != MKT) continue; if (LASTENTRY && x.em >= LASTENTRY) continue; if (MAXRISK > 0 && x.risk1 > MAXRISK) continue;
          if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
          for (auto& bb : busy) bb.erase(std::remove_if(bb.begin(), bb.end(), [&](int t) { return t < x.em; }), bb.end());
          if ((int)b.size() >= R.conc) continue; if (GLOBAL && (int)(busy[0].size() + busy[1].size()) >= GLOBAL) continue; b.push_back(x.xm); byDay[x.di].push_back(&x); }
        int stopDays = 0, seqStopDays = 0; double sumM = 0, sumS = 0; const bool dbg = std::getenv("FPEV_MTM_DEBUG") != nullptr;
        struct DD { int date; double seq, mtm; int stopMin; std::vector<const Fl*> ts; }; std::vector<DD> dd; long cushion = 0, checks = 0;
        auto qOf = [&](const Fl& t, int qbase) { return (RISKUSD > 0 ? std::max(1, std::min(RISKMAX, (int)std::lround(RISKUSD / std::max(1.0, t.risk1)))) * qbase / std::max(1, qq) : qbase) * (int)std::lround(t.szm); };
        std::vector<std::vector<payout::Trade>> DmF(cal.size());
        auto dayRun = [&](int qbase, std::vector<std::vector<payout::Trade>>& DM, bool primary) {
        for (auto& [di, ts] : byDay) { const int date = cal[di];
          // sequential reference (what the other modes do)
          double seqDay = 0;
          { double day = 0; bool st = false; for (size_t a2 = 0; a2 < ts.size(); ++a2) { auto* t = ts[a2]; if (st) break;
              // look-ahead count: an earlier trade still open at this trade's entry, already counted at its final P&L
              for (size_t b2 = 0; b2 < a2; ++b2) { checks++; if (ts[b2]->xm > t->em) cushion++; }
              const double worst = std::min(qOf(*t, qbase) * t->mae, qOf(*t, qbase) * t->usd); if (day + worst <= -STOP) { day = -STOP; st = true; break; } day += qOf(*t, qbase) * t->usd; }
            seqStopDays += st; sumS += day; seqDay = day; }
          int stopMin = -1;
          double real = 0, worstDay = 0, bestDay = 0; bool stopped = false; std::vector<char> done(ts.size(), 0); int losses = 0; bool noEntry = false;
          auto barsOf = [&](int mk) -> const std::vector<std::array<double, 4>>* { auto it = mbars[mk].find(date); return it == mbars[mk].end() ? nullptr : &it->second; };
          for (int tm = 930; tm < 1320 && !stopped; ++tm) {
            double openW = 0, openB = 0, openC = 0;
            for (size_t k = 0; k < ts.size(); ++k) { const Fl& t = *ts[k]; if (done[k] || tm < t.em) continue; const auto* B = barsOf(t.mk); if (!B) { done[k] = 1; continue; }
              if (noEntry && tm == t.em) { done[k] = 2; continue; }   // not taken: the loss counter closed the day for new entries
              const int qt = qOf(t, qbase);
              const double sd = std::fabs(t.ent - t.stp), fct0 = t.risk1 / (sd * t.ppt), fct = NOSCALE ? 1.0 : fct0, cost = t.mk ? 0.5 : 1.5; const double* bar = nullptr;
              const double xc = (t.mk ? EXTRA / 3 : EXTRA) * t.ppt;   // extra cost $ at 1 micro
              const double fin = (NOSCALE ? ((t.usd / t.ppt + cost) / fct0 - cost) * t.ppt : t.usd) - xc;   // the trade's final $ at 1 micro
              for (auto& r : *B) if ((int)r[0] == tm) { bar = r.data(); break; }
              auto usd = [&](double gross) { return qt * ((gross * fct - cost) * t.ppt - xc); };
              if (tm >= t.xm) { real += qt * fin; done[k] = 1; if (fin < 0 && MAXLOSS && ++losses >= MAXLOSS) noEntry = true; continue; }   // exits at its recorded price (the export's own outcome)
              if (!bar) continue;
              const double cm = t.side > 0 ? bar[3] - t.ent : t.ent - bar[3], adv = closeMark ? cm : t.side > 0 ? bar[2] - t.ent : t.ent - bar[1], fav = closeMark ? cm : t.side > 0 ? bar[1] - t.ent : t.ent - bar[2];
              openW += usd(std::max(adv, -sd)); openB += usd(fav); openC += usd(std::max(cm, -sd)); }
            worstDay = std::min(worstDay, real + openW); bestDay = std::max(bestDay, real + openB);
            if (real + openW <= -STOP) { double slip = 0; for (size_t k = 0; k < ts.size(); ++k) if (!done[k] && tm >= ts[k]->em) slip += qOf(*ts[k], qbase) * (ts[k]->mk ? 1.25 : 0.5);
              stopped = true; real = -STOP - slip; worstDay = real; stopMin = tm; }
            else if (PSTOP > 0 && real + openC >= PSTOP) { double slip = 0; for (size_t k = 0; k < ts.size(); ++k) if (!done[k] && tm >= ts[k]->em) slip += qOf(*ts[k], qbase) * (ts[k]->mk ? 1.25 : 0.5);
              stopped = true; real = real + openC - slip; bestDay = std::max(bestDay, real); }
          }
          if (!stopped) for (size_t k = 0; k < ts.size(); ++k) if (!done[k]) real += qOf(*ts[k], qbase) * ts[k]->usd;   // trades whose exit minute had no bar
          DM[di].push_back({real, std::min(worstDay, real), std::max(bestDay, real)}); if (!primary) continue;
          stopDays += stopped; sumM += real; if (dbg) dd.push_back({date, seqDay, real, stopMin, ts}); } };
        dayRun(qq, Dm, true); if (QF > 0) dayRun(QF, DmF, false);
        if (const char* fn = std::getenv("FPEV_MTM_DUMP")) { FILE* fo = fopen(fn, "w"); if (fo) { fprintf(fo, "date,pnl,worst,best\n"); for (size_t i = 0; i < cal.size(); ++i) if (!Dm[i].empty()) fprintf(fo, "%d,%.2f,%.2f,%.2f\n", cal[i], Dm[i][0].pnl, Dm[i][0].mae, Dm[i][0].mfe); fclose(fo); } }   // per-day check against the /flags page
        if (dbg) { printf("\n  pairs (later trade, earlier trade same day) checked: %ld · earlier trade still OPEN at the later trade's entry: %ld (%.1f %%)\n", checks, cushion, checks ? 100.0 * cushion / checks : 0);
          int sameStop = 0, onlyM = 0, onlyS = 0; for (auto& d : dd) { const bool ss = d.seq <= -STOP + 1e-6, sm = d.stopMin >= 0; sameStop += ss && sm; onlyM += sm && !ss; onlyS += ss && !sm; }
          printf("  days stopped by both %d · only mark-to-market %d · only one-at-a-time %d\n", sameStop, onlyM, onlyS);
          std::sort(dd.begin(), dd.end(), [](const DD& x, const DD& y) { return x.seq - x.mtm > y.seq - y.mtm; });
          auto hmS = [](int tm) { const int m = tm - 360; char b2[16]; snprintf(b2, sizeof b2, "%02d:%02d", m / 60, m % 60); return std::string(b2); };
          for (int k = 0; k < 3 && k < (int)dd.size(); ++k) { const DD& d = dd[k]; printf("  EXAMPLE %d: one-at-a-time $%+.0f · mark-to-market $%+.0f (stopped at %s)\n", d.date, d.seq, d.mtm, d.stopMin >= 0 ? hmS(d.stopMin).c_str() : "—");
            for (auto* t : d.ts) printf("      %s %s in %s out %s · final $%+.0f · worst while open $%+.0f\n", t->mk ? "ES" : "NQ", t->side > 0 ? "long " : "short", hmS(t->em).c_str(), hmS(t->xm).c_str(), qq * t->usd, qq * t->mae); } }
        std::vector<const std::vector<payout::Trade>*> ptr(cal.size()), ptrF(cal.size()); for (size_t i = 0; i < cal.size(); ++i) { ptr[i] = Dm[i].empty() ? nullptr : &Dm[i]; ptrF[i] = QF > 0 ? (DmF[i].empty() ? nullptr : &DmF[i]) : ptr[i]; }
        printf("\n  daily stop $%.0f, %d open per market: days stopped mark-to-market %d vs one-trade-at-a-time %d · total $%.0f vs $%.0f\n", STOP, R.conc, stopDays, seqStopDays, sumM, sumS);
        { double cum = 0, pk = 0, ddE = 0, ddI = 0; std::map<int, double> mon; int tdays = 0;
          for (size_t i = 0; i < cal.size(); ++i) { if (Dm[i].empty() || cal[i] < 20140101) continue; const auto& t = Dm[i][0]; ddI = std::max(ddI, pk - (cum + t.mae)); cum += t.pnl; pk = std::max(pk, cum); ddE = std::max(ddE, pk - cum); mon[cal[i] / 100] += t.pnl; tdays++; }
          int lm = 0; double wm = 0; for (auto& [m, x] : mon) { lm += x < 0; wm = std::min(wm, x); }
          { double s1 = 0, s2 = 0, n = 0, wd = 0; for (size_t i = 0; i < cal.size(); ++i) { if (cal[i] < 20140101) continue; const double x = Dm[i].empty() ? 0 : Dm[i][0].pnl; s1 += x; s2 += x * x; n++; wd += x > 0; }
            const double mu = s1 / n, sd = std::sqrt(std::max(0.0, s2 / n - mu * mu));
            printf("DAILY|%s|conc%d|q%d|stop%.0f|%s|sessions %.0f|mean %.1f|sd %.1f|winning_days %.1f%%\n", tag, R.conc, qq, STOP > 1e11 ? 0.0 : STOP, closeMark ? "close" : "extreme", n, mu, sd, 100 * wd / n); }
          printf("ACCT|%s|conc%d|q%d|stop%.0f|%s|total %.0f|dd_eod %.0f|dd_intraday %.0f|worst_month %.0f|losing_months %d/%zu|traded_days %d\n", tag, R.conc, qq, STOP > 1e11 ? 0.0 : STOP, closeMark ? "close" : "extreme", cum, ddE, ddI, wm, lm, mon.size(), tdays); }
        for (const payout::Plan* PP : PL) { const payout::Plan& P = *PP; double ev = 0, n = 0, pass = 0, paidN = 0, h[2] = {0, 0}, hn[2] = {0, 0}; std::vector<int> dPass, dPay; std::map<int, std::pair<double, double>> yr;   // yr: preregistered/2026-10-23_live_planning.md H1
          for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const payout::EvalOut eo = payout::run_eval(P, ptr, (int)s0, 60, {}); double got = 0;
            if (eo.passed) { const payout::RunOut ro = payout::run_account(P, ptrF, (int)s0 + eo.days, 120, {}); got = ro.paid_sum; dPass.push_back(eo.days); if (ro.paid) { paidN++; dPay.push_back(eo.days + ro.first_pay); } }
            ev += got; n++; pass += eo.passed; const int hh = cal[s0] >= 20200101; h[hh] += got; hn[hh]++; yr[cal[s0] / 10000].first += got; yr[cal[s0] / 10000].second++; }
          auto med = [](std::vector<int> x) { if (x.empty()) return -1; std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
          printf("PLAN|%s|conc%d|q%d|stop%.0f|%s|%s|ev %.0f|h1 %.0f|h2 %.0f|pass %.0f|paid %.0f|days_pass %d|days_first_pay %d\n", tag, R.conc, qq, STOP > 1e11 ? 0.0 : STOP, closeMark ? "close" : "extreme", P.name.c_str(),
            ev / n, h[0] / std::max(1.0, hn[0]), h[1] / std::max(1.0, hn[1]), 100 * pass / n, 100 * paidN / n, med(dPass), med(dPay));
          printf("YEAR|%s|%s|%s", tag, closeMark ? "close" : "extreme", P.name.c_str()); for (auto& [y, v] : yr) printf("|%d %.0f", y, v.first / v.second); printf("\n");
          printf("    %-18s mark-to-market $%5.0f per eval (2014-19 $%5.0f / 2020-25 $%5.0f) · pass %3.0f%%\n", P.name.c_str(), ev / n, h[0] / std::max(1.0, hn[0]), h[1] / std::max(1.0, hn[1]), 100 * pass / n);
          // 5-account programs (founder: "5 accounts at $50 … at least one of them should pass and get at least 300"): accounts started at
          // s0, s0+g, …, s0+4g trading days, each bought once (a blown account is NOT replaced), every start day 2014-01 → 2025-04
          if (PROG > 0) { std::vector<double> got(cal.size(), -1); std::vector<char> passed(cal.size(), 0);
            for (size_t s0 = 0; s0 < cal.size(); ++s0) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const payout::EvalOut eo = payout::run_eval(P, ptr, (int)s0, 60, {});
              passed[s0] = eo.passed; got[s0] = eo.passed ? payout::run_account(P, ptrF, (int)s0 + eo.days, 120, {}).paid_sum : 0; }
            const int NA = std::max(1, (int)envd("FPEV_MTM_PROGN", 5));   // accounts per program (default 5)
            for (int g : {0, 1, 5, 10, 20}) { double n5 = 0, any = 0, anyPass = 0, tot = 0, back = 0;
              for (size_t s0 = 0; s0 + (NA - 1) * g < cal.size(); ++s0) { bool ok = true; for (int j = 0; j < NA; ++j) if (got[s0 + j * g] < 0) ok = false; if (!ok) continue;
                double sum = 0; bool a1 = false, p1 = false; for (int j = 0; j < NA; ++j) { sum += got[s0 + j * g]; a1 |= got[s0 + j * g] >= PROG; p1 |= passed[s0 + j * g]; }
                n5++; any += a1; anyPass += p1; tot += sum; back += 0.8 * sum >= 250; }
              printf("PROG|%s|%s|accounts %d|gap %d|programs %.0f|any account paid >= %.0f: %.1f%%|any passed: %.1f%%|avg total paid %.0f (after 80%% split %.0f)|got >= $250 back after split: %.1f%%\n",
                tag, P.name.c_str(), NA, g, n5, PROG, 100 * any / n5, 100 * anyPass / n5, tot / n5, 0.8 * tot / n5, 100 * back / n5); } } } }
      return 0; }
    if (std::string(argv[3]) == "mix") {
      const int MX[6][2] = {{2, 2}, {3, 1}, {3, 2}, {4, 1}, {2, 1}, {4, 2}};
      printf("BOOK2 size by market — $ of payouts per eval bought (pass %%), starts every 5th trading day 2014-01 → 2025-04\n");
      for (int pi = 0; pi < 3; ++pi) { const payout::Plan& P = *planOf(SP[pi]); printf("\n  %s\n", SP[pi]);
        for (double ls : {150.0, 200.0}) { printf("    daily stop $%.0f:", ls); for (auto& m : MX) { const Out o = evRun(rules[2], P, m[0], m[1], 0, {0, ls, 0}, 120); printf("  NQ %d/ES %d $%5.0f (%3.0f%%)", m[0], m[1], o.ev / o.n, 100 * o.pass / o.n); } printf("\n"); } }
      return 0; }
    printf("Stress tests of the account EV — $ of payouts per eval bought (before fees and the split), starts every 5th trading day 2014-01 → 2025-04.\n");
    for (int ri = 0; ri < 2; ++ri) { printf("\n################ %s\n", RR[ri]->n);
      for (int pi = 0; pi < 3; ++pi) { const payout::Plan& P = *planOf(SP[pi]); const int q = SQ[ri][pi]; printf("\n  ---- %s, %d micro(s) on each market ----\n", SP[pi], q);
        auto show = [&](const char* nm, const Out& o) { printf("    %-44s pass %3.0f%% · paid %3.0f%% of funded · $%5.0f per eval bought\n", nm, 100 * o.pass / o.n, o.pass ? 100 * o.paid / o.pass : 0, o.ev / o.n); };
        for (double ex : {0.0, 0.5, 1.0, 2.0}) { char nm[80]; snprintf(nm, sizeof nm, "extra cost +%.1f NQ pts a trade (ES a third)", ex); show(nm, evRun(*RR[ri], P, q, q, ex, {}, 120)); }
        show("NQ only", evRun(*RR[ri], P, q, 0, 0, {}, 120)); show("ES only", evRun(*RR[ri], P, 0, q, 0, {}, 120));
        { char nm[80]; snprintf(nm, sizeof nm, "NQ %d + ES %d micros", q, 2 * q); show(nm, evRun(*RR[ri], P, q, 2 * q, 0, {}, 120)); snprintf(nm, sizeof nm, "NQ %d + ES %d micros", 2 * q, q); show(nm, evRun(*RR[ri], P, 2 * q, q, 0, {}, 120)); }
        for (double ls : {100.0, 150.0, 200.0, 250.0, 300.0}) { char nm[80]; snprintf(nm, sizeof nm, "daily loss stop $%.0f", ls); show(nm, evRun(*RR[ri], P, q, q, 0, {0, ls, 0}, 120)); }
        { const Out o = evRun(*RR[ri], P, q, q, 1.0, {0, 150, 0}, 120); show("daily loss stop $150 + extra cost +1 NQ pt", o); printf("      by start year: "); for (auto& [y, p] : o.byYear) printf("%d $%.0f · ", y, p.first / p.second); printf("\n"); }
        for (double ps : {300.0, 600.0}) { char nm[80]; snprintf(nm, sizeof nm, "daily profit stop $%.0f", ps); show(nm, evRun(*RR[ri], P, q, q, 0, {ps, 0, 0}, 120)); }
        for (int mt : {2, 3, 5}) { char nm[80]; snprintf(nm, sizeof nm, "max %d trades a day", mt); show(nm, evRun(*RR[ri], P, q, q, 0, {0, 0, mt}, 120)); }
        for (int hz : {60, 250}) { char nm[80]; snprintf(nm, sizeof nm, "funded %d trading days (base 120)", hz); show(nm, evRun(*RR[ri], P, q, q, 0, {}, hz)); }
        const Out base = evRun(*RR[ri], P, q, q, 0, {}, 120); printf("    by start year: "); for (auto& [y, p] : base.byYear) printf("%d $%.0f · ", y, p.first / p.second); printf("\n");
        // monthly program: one eval bought at every start (~4 a month), fee F each; net per calendar month = payouts of that month's starts − fees
        for (double fee : {100.0, 150.0, 200.0, 300.0}) { std::map<int, double> mon; for (auto& [d, got] : base.perStart) mon[d / 100] += got - fee;
          std::vector<double> nets; for (auto& [m, x] : mon) nets.push_back(x); std::sort(nets.begin(), nets.end()); double sum = 0; int pos = 0; for (double x : nets) { sum += x; pos += x > 0; }
          printf("    monthly program at $%3.0f per eval (~4 evals a month): net per month mean $%5.0f · median $%5.0f · 10th pct $%5.0f · months > 0 %3.0f%%\n", fee, sum / nets.size(), nets[nets.size() / 2], nets[nets.size() / 10], 100.0 * pos / nets.size()); } } }
    return 0; }
  const char* PLANS[7] = {"select_flex_25k", "select_daily_25k", "growth_25k", "select_flex_50k", "select_daily_50k", "growth_50k", "apex_eod_50k"};
  printf("EV of a prop account traded with the flag rules — NQ + ES 1-5m flags (wide15 export), trades in $ at today's ATR, worst open loss from the 1m bars.\n");
  printf("Accounts started every 5th trading day 2014-01 → 2025-04 (in brackets: starts 2022-01 → 2025-04). Eval <= 60 trading days, funded 120 days. $ before the split and fees.\n");
  for (auto& R : rules) { printf("\n################ %s\n", R.n);
    // the day lists at 1 micro
    std::vector<std::vector<payout::Trade>> D1(cal.size()); { int lastD = -1; std::vector<int> busy[2];
      for (auto& x : v) { if (!R.f(x)) continue; if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk];
        b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= R.conc) continue; b.push_back(x.xm); D1[x.di].push_back({x.usd, x.mae, x.mfe}); } }
    long nt = 0; for (auto& d : D1) nt += d.size(); printf("  %.2f trades a day\n", (double)nt / cal.size());
    for (const char* pn : PLANS) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == pn) P = &p; if (!P) continue;
      double bestEV = -1e9; int bestQ = 0;
      for (int q : {1, 2, 3, 4, 5, 6, 8, 10}) { std::vector<std::vector<payout::Trade>> D(cal.size()); for (size_t i = 0; i < cal.size(); ++i) for (auto& t : D1[i]) D[i].push_back({q * t.pnl, q * t.mae, q * t.mfe});
        std::vector<const std::vector<payout::Trade>*> ptr(cal.size()); for (size_t i = 0; i < cal.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
        double ev[2] = {0, 0}, passR[2] = {0, 0}, paidR[2] = {0, 0}, perF[2] = {0, 0}, nS[2] = {0, 0}; std::vector<int> dtp;
        for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const int rec = cal[s0] >= 20220101;
          const payout::EvalOut eo = payout::run_eval(*P, ptr, (int)s0, 60); double got = 0; bool paid = false;
          if (eo.passed) { dtp.push_back(eo.days); const payout::RunOut ro = payout::run_account(*P, ptr, (int)s0 + eo.days, 120); got = ro.paid_sum; paid = ro.paid; }
          for (int k = 0; k <= rec; ++k) { nS[k]++; passR[k] += eo.passed; paidR[k] += paid; ev[k] += got; } }
        std::sort(dtp.begin(), dtp.end());
        printf("  %-17s %2d micros: pass %3.0f%% (%3.0f%%) median %3d days · paid %3.0f%% of funded · $%5.0f per funded · $ PER EVAL BOUGHT %5.0f (%5.0f)\n", pn, q, 100 * passR[0] / nS[0], 100 * passR[1] / nS[1],
               dtp.empty() ? -1 : dtp[dtp.size() / 2], passR[0] ? 100 * paidR[0] / passR[0] : 0, passR[0] ? ev[0] / passR[0] : 0, ev[0] / nS[0], ev[1] / nS[1]);
        if (ev[0] / nS[0] > bestEV) { bestEV = ev[0] / nS[0]; bestQ = q; } }
      printf("  → %s best size %d micros: $%.0f payouts per eval bought (break-even total fee)\n\n", pn, bestQ, bestEV); } }
  return 0;
}
