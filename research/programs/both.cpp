// 2026-10-17 pre-registered A (research/preregistered/2026-10-17_more_trades.md): take both the cascade and the gap retest on the same
// day when they agree in direction (the first trade of the day sets the direction; never opposite positions), vs today's "first one only".
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe; int dir; int64_t t; };
static bool PESS = false;
int main(int argc, char**) {
  PESS = argc > 1;
  const char* S = "research/programs/ho/";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series nq, es; std::string err; load_raw("data/edge_lab/cache", "NQ", nq, err); load_raw("data/edge_lab/cache", "ES", es, err);
  for (Series* x : {&nq, &es}) { build_days(*x, fomc, vix); load_macro("data/edge_lab/cache", *x); load_events("data/edge_lab/cache", *x); }
  Market mn(nq), me(es); Evaluator evn(mn), eve(me); ExitSim sn(mn), se(me);
  auto get = [&](Market& m, Evaluator& ev, ExitSim& sim, const std::string& spec, ExitRule x, double cost, double pv) { std::map<int, T3> out; const Series& s = m.s();
    for (auto& t : wf_trades(m, ev, spec.c_str())) { const int d = s.days[t.day].date; if (d < 20190101) continue; auto r = sim.run(t, x);
      T3 v{(r.pts - cost) * pv, (r.mae - cost) * pv, std::max(0.0, r.mfe) * pv, t.dir, s.t[t.e]}; if (!out.count(d) || v.t < out[d].t) out[d] = v; } return out; };
  std::map<int, T3> src[4] = {get(mn, evn, sn, "research/ideas/so_cascade.json", X_VWAP_STALL, 1.5, 2.0), get(mn, evn, sn, std::string(S) + "gap_or_retest_full.json", X_BASE, 1.5, 2.0),
                              get(me, eve, se, "research/ideas/es_cascade.json", X_BASE, 0.6, 5.0), get(me, eve, se, std::string(S) + "es_gap_or_retest_full.json", X_BASE, 0.6, 5.0)};
  std::vector<int> dates; for (auto& d : nq.days) if (d.ok && d.date >= 20190101) dates.push_back(d.date);
  const int D = dates.size();
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  for (const char* pname : {"select_daily_25k", "select_flex_25k"}) { const payout::Plan* pl = plan_of(pname);
    printf("\n== %s, 2 micros per trade\n", pname);
    for (int es_on = 0; es_on < 2; ++es_on) for (int both = 0; both < 2; ++both) {
      const int q = 2;
      std::vector<std::vector<payout::Trade>> st(D); std::vector<const std::vector<payout::Trade>*> cal(D, nullptr);
      long ntr = 0; int tdays = 0, wdays = 0; double tot = 0;
      for (int i = 0; i < D; ++i) { const int d = dates[i];
        std::vector<const T3*> c; for (int k = 0; k < (es_on ? 4 : 2); ++k) { auto it = src[k].find(d); if (it != src[k].end()) c.push_back(&it->second); }
        std::sort(c.begin(), c.end(), [](const T3* a, const T3* b) { return a->t < b->t; });
        int dir = 0; bool took[2] = {false, false}; double day = 0;
        for (auto* v : c) {
          const int mk = (v >= &src[2].begin()->second && false) ? 1 : 0; (void)mk;
          if (dir && v->dir != dir) continue;
          // market of this trade: compare addresses against the ES maps
          bool is_es = false; for (int k = 2; k < 4; ++k) for (auto& [dd, w] : src[k]) { (void)dd; if (&w == v) { is_es = true; break; } }
          if (!both && took[is_es]) continue;
          dir = v->dir; took[is_es] = true; st[i].push_back({v->p * q, v->mae * q, v->mfe * q}); day += v->p * q; ++ntr; }
        if (PESS && st[i].size() > 1) { double p = 0, ma = 0, mf = 0; for (auto& x : st[i]) { p += x.pnl; ma += x.mae; mf += x.mfe; } st[i] = {{p, std::min(ma, p), mf}}; }
        if (!st[i].empty()) { cal[i] = &st[i]; ++tdays; wdays += day > 0; tot += day; } }
      int starts = 0, pass = 0, blown = 0; std::vector<int> pd; double paid = 0; int ev = 0; std::vector<double> waits;
      for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts;
        auto e = payout::run_eval(*pl, cal, s0, 120); if (e.passed) { ++pass; pd.push_back(e.days); } else if (e.failed) ++blown;
        int i = s0, first = -1; double py = 0;
        while (i < s0 + 252) { ++ev; auto e2 = payout::run_eval(*pl, cal, i, s0 + 252 - i); i += std::max(1, e2.days); if (!e2.passed) continue;
          auto r = payout::run_account(*pl, cal, i, s0 + 252 - i); py += r.paid_sum; if (first < 0 && !r.pay_days.empty()) first = i + r.pay_days[0] - 1 - s0;
          i += std::max(1, r.days); if (!r.breach) break; }
        paid += py; if (first >= 0) waits.push_back(first); }
      std::sort(pd.begin(), pd.end()); std::sort(waits.begin(), waits.end());
      printf("  %-9s %-28s trades/day %.2f · trade days %4.1f%% (win %4.1f%%) · $%6.0f/yr · eval pass %4.1f%% blown %4.1f%% (median %2d d) · 1st payout %4.1f wks · paid/yr $%5.0f · evals/yr %4.1f\n",
             es_on ? "NQ + ES" : "NQ", both ? "both strategies, same dir" : "first strategy per market", (double)ntr / D, 100.0 * tdays / D, 100.0 * wdays / std::max(1, tdays),
             tot / (D / 252.0), 100.0 * pass / starts, 100.0 * blown / starts, pd.empty() ? 0 : pd[pd.size() / 2], waits.empty() ? NAN : waits[waits.size() / 2] / 5, paid / starts, (double)ev / starts);
    } }
}
