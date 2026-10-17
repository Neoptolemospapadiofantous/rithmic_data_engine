// 2026-10-17 volatility sizing: do the cascade / weak-day / Monday trades earn more per point of risk on quiet or wild days?
// Forecast known before 09:30: overnight range (18:00-09:29) / ATR, VIX 1-day change, |gap| / ATR. Rule chosen on 2010-18 only.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <set>
using namespace lab;
#include "wf_trades.inc"
int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto onr = [&](int k) { const Day& d = s.days[k]; if (!(d.atr > 0) || d.rth0 < 0) return (double)NAN; double h = -1e18, l = 1e18; for (int i = d.b0; i < d.rth0; ++i) { h = std::max(h, (double)s.h[i]); l = std::min(l, (double)s.l[i]); } return (h - l) / d.atr; };
  struct T { int day; double p, mae; };
  auto get = [&](const char* spec, ExitRule x) { std::vector<T> v; for (auto& t : wf_trades(m, ev, spec)) { auto r = sim.run(t, x); v.push_back({t.day, r.pts - 1.0, r.mae - 1.0}); } return v; };
  struct Src { const char* nm; std::vector<T> v; };
  std::vector<Src> src = {{"cascade (vwap+stall)", get("research/ideas/so_cascade.json", X_VWAP_STALL)}, {"weak-day long", get("research/ideas/weak_day_long.json", X_BASE)},
                          {"monday long", get("research/programs/ho/monday_full.json", X_BASE)}};
  for (auto& [nm, v] : src) {
    printf("\n== %s: net pts per trade (in ATR) and worst-case by forecast bucket\n", nm);
    for (int e = 0; e < 2; ++e) {
      printf("  %s\n", e ? "2019-26 (judged)" : "2010-18 (rule chosen here)");
      auto row = [&](const char* lab, auto f) { double a = 0, b = 0, w = 0; int n = 0; std::vector<double> x;
        for (auto& t : v) { const Day& d = s.days[t.day]; if ((d.date >= 20190101) != (bool)e || !f(t.day)) continue; ++n; a += t.p / d.atr; b += t.p; x.push_back(t.p / d.atr); w = std::min(w, t.mae / d.atr); }
        double sd = 0; for (double y : x) sd += (y - a / std::max(1, n)) * (y - a / std::max(1, n)); sd = n > 1 ? std::sqrt(sd / (n - 1)) : NAN;
        printf("    %-30s n %4d  net %+.3f atr/trade (%+6.1f pts)  sd %.3f  edge/sd %+.3f  worst MAE %+.2f atr\n", lab, n, n ? a / n : NAN, n ? b / n : NAN, sd, n ? a / n / sd : NAN, w); };
      row("all", [](int) { return true; });
      row("overnight range < 0.4 ATR", [&](int k) { double r = onr(k); return r < 0.4; });
      row("overnight range 0.4-0.8", [&](int k) { double r = onr(k); return r >= 0.4 && r <= 0.8; });
      row("overnight range > 0.8 ATR", [&](int k) { double r = onr(k); return r > 0.8; });
      row("VIX rose > 10% yesterday", [&](int k) { return s.days[k].vixc1 > 0.10; });
      row("VIX not up > 10%", [&](int k) { return !(s.days[k].vixc1 > 0.10); });
    }
  }
}
