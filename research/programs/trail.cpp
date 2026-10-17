// 2026-10-17 pre-registered: let winners run when the tape trends (research/preregistered/2026-10-17_regime_trail.md).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <set>
using namespace lab;
#include "wf_trades.inc"
int main() {
  const double COST = 1.0;
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  const auto& A1 = m.agg(1); const auto& acr = m.series("acr", 1, 60);
  struct Src { const char* name; std::string spec; ExitRule cur; };
  const Src srcs[] = {{"cascade", "research/ideas/so_cascade.json", X_VWAP_STALL},
                      {"gap retest", "research/programs/ho/gap_or_retest_full.json", X_BASE}};
  const ExitRule rules[] = {X_BASE, X_VWAP_STALL, X_VWAP_RUN, X_TRAIL_RUN};
  const char* rn[] = {"base 2R", "vwap+stall", "vwap runner", "15m trail"};
  for (auto& src : srcs) {
    auto tr = wf_trades(m, ev, src.spec.c_str());
    struct Row { int date; bool trend; double x[4]; double cur, sw; };
    std::vector<Row> rows;
    for (auto& t : tr) {
      const int k = A1.comp[t.e]; const double a = k >= 0 ? acr[k] : NAN; if (std::isnan(a)) continue;
      Row r{s.days[t.day].date, a > 0, {}, 0, 0};
      for (int q = 0; q < 4; ++q) r.x[q] = sim.run(t, rules[q]).pts - COST;
      r.cur = sim.run(t, src.cur).pts - COST;
      r.sw = r.trend ? r.x[2] : r.cur;
      rows.push_back(r);
    }
    printf("\n== %s — %zu trades (current exit: %s)\n", src.name, rows.size(), src.cur == X_VWAP_STALL ? "vwap+stall" : "base 2R");
    for (int part = 0; part < 2; ++part) {
      printf("  %s\n", part ? "HOLDOUT 2025+ (judged once)" : "2010-07 → 2024-12");
      for (int g = 0; g < 2; ++g) {
        double sum[4] = {0, 0, 0, 0}; int n = 0;
        for (auto& r : rows) { if ((r.date >= 20250101) != (bool)part || r.trend != (bool)g) continue; ++n; for (int q = 0; q < 4; ++q) sum[q] += r.x[q]; }
        printf("    %-16s n %4d |", g ? "trending tape" : "not trending", n);
        for (int q = 0; q < 4; ++q) printf("  %s %+6.2f", rn[q], n ? sum[q] / n : NAN);
        printf("\n");
      }
      double c = 0, w = 0; int n = 0; double ce[3] = {0, 0, 0}, we[3] = {0, 0, 0}; int ne[3] = {0, 0, 0};
      for (auto& r : rows) { if ((r.date >= 20250101) != (bool)part) continue; ++n; c += r.cur; w += r.sw;
        int e = r.date < 20190101 ? 0 : r.date < 20230101 ? 1 : 2; ce[e] += r.cur; we[e] += r.sw; ++ne[e]; }
      printf("    SWITCH (trending → vwap runner, else current): %+.2f vs current %+.2f pts/trade (n %d)", n ? w / n : NAN, n ? c / n : NAN, n);
      if (!part) printf(" | eras switch/current: %+.2f/%+.2f  %+.2f/%+.2f  %+.2f/%+.2f", we[0] / ne[0], ce[0] / ne[0], we[1] / ne[1], ce[1] / ne[1], we[2] / ne[2], ce[2] / ne[2]);
      printf("\n");
    }
  }
}
