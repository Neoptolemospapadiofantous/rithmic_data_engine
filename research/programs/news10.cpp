// 2026-10-17 test F (research/preregistered/2026-10-17_test_f_news10.md): 10:00 ET releases (JOLTS, ISM manufacturing) and the cascade.
// (1) verify the dates: NQ's 10:00–10:02 volume vs its 09:45–09:59 rate; (2) the cascade's walk-forward trades by check slot on
// 10:00-release days vs other days, per era (VWAP + stall exits, NQ cost 1.0).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
static std::set<int> rd(const std::string& p) { std::set<int> s; std::ifstream f(p); std::string l; while (std::getline(f, l)) { int y, m, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &m, &d) == 3) s.insert(y * 10000 + m * 100 + d); } return s; }
int main() {
  const std::string S = "research/programs/";
  std::set<int> fomc = rd("data/edge_lab/cache/fomc.txt"); paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  std::set<int> jolts = rd(S + "news/jolts.txt"), ism;
  // ISM manufacturing: 10:00 on the first business day of the month = the first ok session of each month in our data
  { int cur = 0; for (auto& d : s.days) if (d.ok) { if (d.date / 100 != cur) { cur = d.date / 100; ism.insert(d.date); } } }
  // (1) verification
  auto ratio = [&](const Day& d) { double pre = 0, ev = 0; int np = 0; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]);
      if (tm >= hm_to_tday(9, 45) && tm < hm_to_tday(10, 0)) { pre += s.v[i]; ++np; } else if (tm >= hm_to_tday(10, 0) && tm < hm_to_tday(10, 3)) ev += s.v[i]; }
    return np >= 10 && pre > 0 ? (ev / 3) / (pre / np) : NAN; };
  auto check = [&](const char* nm, const std::set<int>& set) { int n = 0, hit = 0, n19 = 0, hit19 = 0;
    for (auto& d : s.days) if (d.ok && set.count(d.date)) { double r = ratio(d); if (std::isnan(r)) continue; ++n; hit += r >= 1.5; if (d.date >= 20190101) { ++n19; hit19 += r >= 1.5; } }
    printf("  %-6s days %3d: 10:00 volume ≥ 1.5× the 09:45–09:59 rate on %4.1f%% (2019+: %4.1f%%)\n", nm, n, 100.0 * hit / n, n19 ? 100.0 * hit19 / n19 : NAN); };
  { int n = 0, hit = 0; for (auto& d : s.days) if (d.ok && !jolts.count(d.date) && !ism.count(d.date)) { double r = ratio(d); if (std::isnan(r)) continue; ++n; hit += r >= 1.5; }
    printf("date check (10:00 volume spike):\n"); check("JOLTS", jolts); check("ISM", ism); printf("  other days %d: %4.1f%%\n", n, 100.0 * hit / n); }
  // (2) cascade by slot, release days vs others
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto tr = wf_trades(m, ev, "research/ideas/so_cascade.json");
  const char* sn[] = {"09:50", "10:30", "11:00", "11:30"};
  double sum[2][4][4] = {}; int n[2][4][4] = {};
  for (auto& t : tr) { if (t.src < 0 || t.src > 3) continue; const int d = s.days[t.day].date, e = d < 20190101 ? 0 : d < 20230101 ? 1 : d < 20250101 ? 2 : 3;
    const int rel = (jolts.count(d) || ism.count(d)) ? 1 : 0; const double p = sim.run(t, X_VWAP_STALL).pts - 1.0; sum[rel][t.src][e] += p; ++n[rel][t.src][e]; }
  printf("\ncascade walk-forward trades, NQ pts/trade after cost, by slot: 10:00-release days vs other days\n%-18s %18s %18s %18s %18s\n", "", "2010-18", "2019-22", "2023-24", "2025+ holdout");
  for (int k = 0; k < 4; ++k) for (int rel = 1; rel >= 0; --rel) { printf("%-6s %-11s", sn[k], rel ? "release day" : "other days");
      for (int e = 0; e < 4; ++e) printf("   %+7.2f (n %4d)", n[rel][k][e] ? sum[rel][k][e] / n[rel][k][e] : NAN, n[rel][k][e]); printf("\n"); }
}
