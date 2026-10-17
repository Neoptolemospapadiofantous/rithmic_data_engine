// 2026-10-17 fleet × tape regime × daily profit lock, all 2026 1-minute data we have (Jan, Mar, Sep, Oct).
// Lock = once the strategy's realized P&L that trading day reaches X points per contract, it takes no more trades that day.
// Regime side = take only trades entered on trending tape / only reverting / all (acr over the 30 one-minute moves before entry).
// Per strategy, the best (side, lock) of 3 × 5 combinations is CHOSEN on January (by net points per trading day) and JUDGED on
// Mar + Sep + Oct. Baselines: no filter / no lock in the test months; and a RANDOM combination per strategy (selection without information).
#include "research/edge_lab.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct T { int64_t t; int day, r; double p; bool test; };
static const double LOCKS[5] = {0, 5, 10, 20, 40};
// net points and number of trading days of the strategy's trades in one part, under (side, lock)
static void run(const std::vector<T>& v, bool test, int side, double lock, double& net, int& days, int& trades) {
  net = 0; days = 0; trades = 0; int cur = -1; double day_pnl = 0; bool stopped = false; std::set<int> seen;
  for (auto& t : v) { if (t.test != test) continue; seen.insert(t.day);
    if (t.day != cur) { cur = t.day; day_pnl = 0; stopped = false; }
    if (stopped) continue; if (side != 0 && t.r != side) continue;
    net += t.p; day_pnl += t.p; ++trades; if (lock > 0 && day_pnl >= lock) stopped = true; }
  days = seen.size();
}
int main(int, char** argv) {
  std::set<int> fomc; paper::VixRegime vix; Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  std::map<std::string, std::vector<T>> by;
  std::ifstream f(argv[1]); std::string line;
  while (std::getline(f, line)) {
    std::stringstream ss(line); std::string id, a, b, c; std::getline(ss, id, ','); std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, c, ',');
    const int64_t te = std::stoll(a); const size_t i = std::lower_bound(s.t.begin(), s.t.end(), te) - s.t.begin(); if (i < 32 || i >= s.size()) continue;
    double num = 0, den = 0; for (size_t j = i - 30; j < i; ++j) { const double r = s.c[j] - s.c[j - 1], q = s.c[j - 1] - s.c[j - 2]; num += r * q; den += r * r; }
    if (den <= 0) continue; time_t tt = (time_t)(te + 6 * 3600); struct tm g; gmtime_r(&tt, &g);   // trading day: 18:00 belongs to the next date
    by[id].push_back({te, (g.tm_year + 1900) * 10000 + (g.tm_mon + 1) * 100 + g.tm_mday, num / den > 0 ? 1 : -1, std::stod(c), g.tm_mon != 0});
  }
  for (auto& [id, v] : by) std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.t < b.t; });
  std::mt19937_64 rng(5);
  long nstrat = 0; int pos_base = 0, pos_pick = 0, pos_rand = 0, better = 0, better_rand = 0; double sum_base = 0, sum_pick = 0, sum_rand = 0;
  std::map<std::string, int> pick_count; double lock_only_gain[5] = {0, 0, 0, 0, 0};
  for (auto& [id, v] : by) {
    double n0; int d0, t0; run(v, false, 0, 0, n0, d0, t0); double n1; int d1, t1; run(v, true, 0, 0, n1, d1, t1);
    if (d0 < 10 || d1 < 10 || t0 < 20 || t1 < 20) continue; ++nstrat;
    double best = -1e18; int bs = 0, bl = 0;
    for (int sd = -1; sd <= 1; ++sd) for (int l = 0; l < 5; ++l) { double n; int d, t; run(v, false, sd, LOCKS[l], n, d, t); if (t < 8) continue; if (n / d0 > best) { best = n / d0; bs = sd; bl = l; } }
    double np; int dp, tp; run(v, true, bs, LOCKS[bl], np, dp, tp);
    const int rs = (int)(rng() % 3) - 1, rl = (int)(rng() % 5); double nr; int dr, tr; run(v, true, rs, LOCKS[rl], nr, dr, tr);
    const double base = n1 / d1, pick = np / d1, rnd = nr / d1;
    sum_base += base; sum_pick += pick; sum_rand += rnd; pos_base += base > 0; pos_pick += pick > 0; pos_rand += rnd > 0; better += pick > base; better_rand += rnd > base;
    char k[48]; snprintf(k, sizeof k, "%s / lock %s", bs == 0 ? "all trades" : bs > 0 ? "trending only" : "reverting only", bl ? std::to_string((int)LOCKS[bl]).c_str() : "none"); ++pick_count[k];
    for (int l = 1; l < 5; ++l) { double n; int d, t; run(v, true, 0, LOCKS[l], n, d, t); lock_only_gain[l] += n / d1 - base; }
  }
  printf("2026 fleet, %ld strategies with >= 10 trading days and >= 20 trades in January and in the test months (Mar + Sep + Oct)\n", nstrat);
  printf("net points per trading day per strategy, test months (average over strategies):\n");
  printf("  no filter, no lock (baseline)                 %+6.2f   profitable strategies %5d\n", sum_base / nstrat, pos_base);
  printf("  best side + lock chosen on January            %+6.2f   profitable strategies %5d   better than baseline for %4.1f%%\n", sum_pick / nstrat, pos_pick, 100.0 * better / nstrat);
  printf("  random side + lock (luck baseline)            %+6.2f   profitable strategies %5d   better than baseline for %4.1f%%\n", sum_rand / nstrat, pos_rand, 100.0 * better_rand / nstrat);
  printf("  the same lock on every strategy, no regime:  "); for (int l = 1; l < 5; ++l) printf("  %2.0f pts: %+5.2f", LOCKS[l], lock_only_gain[l] / nstrat); printf("  (change vs baseline)\n");
  printf("  what January picked:"); for (auto& [k, c] : pick_count) printf("  %s %d;", k.c_str(), c); printf("\n");
}
