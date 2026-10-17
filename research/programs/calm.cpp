// Calm-day sizing: the cascade's walk-forward trades at 2 units when the prior VIX close < 20, else 1 — vs flat 1 unit.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
#include "wf_trades.inc"
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m);
  auto cas = wf_trades(m, ev, "research/ideas/so_cascade.json");
  const char* era[3] = {"2010-18", "2019-22", "2023-26"};
  for (int sized = 0; sized < 2; ++sized) {
    double net[3] = {0,0,0}, units[3] = {0,0,0}; int n[3] = {0,0,0};
    double eq = 0, pk = 0, dd = 0, tot = 0, totu = 0;
    for (auto& t : cas) { const Day& D = s.days[t.day]; const int e = era_of(D.date);
      const double w = sized ? ((D.vix > 0 && D.vix < 20) ? 2.0 : 1.0) : 1.0; const double x = (t.pts - 1.0) * w;
      net[e] += x; units[e] += w; ++n[e]; tot += x; totu += w; eq += x; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); }
    printf("%-22s", sized ? "2 units when VIX < 20" : "flat 1 unit");
    for (int e = 0; e < 3; ++e) printf(" | %s: %+.2f pts per unit (%d trades)", era[e], net[e] / units[e], n[e]);
    printf(" | total %+.0f pts, max drawdown %.0f pts, return/drawdown %.1f, avg units %.2f\n", tot, dd, tot / dd, totu / cas.size());
  }
}
