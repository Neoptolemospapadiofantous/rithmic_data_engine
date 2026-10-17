// 2026-10-17 "we need to do better": NQ and ES together on one Flex 50K account. Real 2019–26 walk-forward trades.
// NQ: cascade (VWAP+stall) + gap retest, one directional trade a day (earliest). ES (as MES): cascade + gap retest, one a day.
// No opposite positions across the two (an ES trade against the day's NQ trade is skipped). MNQ $2/pt cost 1.5, MES $5/pt cost 0.6.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe; int dir; int64_t t; };
int main() {
  const char* S = "research/programs/ho/";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series nq, es; std::string err; load_raw("data/edge_lab/cache", "NQ", nq, err); load_raw("data/edge_lab/cache", "ES", es, err);
  for (Series* x : {&nq, &es}) { build_days(*x, fomc, vix); load_macro("data/edge_lab/cache", *x); load_events("data/edge_lab/cache", *x); }
  Market mn(nq), me(es); Evaluator evn(mn), eve(me); ExitSim sn(mn), se(me);
  auto get = [&](Market& m, Evaluator& ev, ExitSim& sim, const std::string& spec, ExitRule x, double cost, double pv) { std::map<int, T3> out; const Series& s = m.s();
    for (auto& t : wf_trades(m, ev, spec.c_str())) { const int d = s.days[t.day].date; if (d < 20190101) continue; auto r = sim.run(t, x);
      T3 v{(r.pts - cost) * pv, (r.mae - cost) * pv, std::max(0.0, r.mfe) * pv, t.dir, s.t[t.e]};
      auto it = out.find(d); if (it == out.end() || v.t < it->second.t) out[d] = v; } return out; };
  // keyed by DATE (the two markets' day indices differ); value = per-contract $ (MNQ / MES)
  auto merge = [](std::map<int, T3> a, const std::map<int, T3>& b) { for (auto& [d, v] : b) { auto it = a.find(d); if (it == a.end() || v.t < it->second.t) a[d] = v; } return a; };
  auto nqd = merge(get(mn, evn, sn, "research/ideas/so_cascade.json", X_VWAP_STALL, 1.5, 2.0), get(mn, evn, sn, std::string(S) + "gap_or_retest_full.json", X_BASE, 1.5, 2.0));
  std::string esgr = std::string(S) + "es_gap_or_retest_full.json";
  { std::ifstream in("research/ideas/es_gap_or_retest.json"); std::string all((std::istreambuf_iterator<char>(in)), {}); auto j = json::parse(all); j.erase("to"); std::ofstream(esgr) << j.dump(); }
  auto esd = merge(get(me, eve, se, "research/ideas/es_cascade.json", X_BASE, 0.6, 5.0), get(me, eve, se, esgr, X_BASE, 0.6, 5.0));
  std::vector<int> dates; for (auto& d : nq.days) if (d.ok && d.date >= 20190101) dates.push_back(d.date);
  const int D = dates.size();
  printf("2019-01 → 2026-10, %d sessions · NQ trade days %zu · ES trade days %zu\n", D, nqd.size(), esd.size());
  struct Cfg { const char* name; int nq_e, es_e, nq_f, es_f; };
  const Cfg cfgs[] = {{"NQ only 1 MNQ", 1, 0, 1, 0}, {"NQ only 2 MNQ", 2, 0, 2, 0}, {"NQ only 3 MNQ", 3, 0, 3, 0},
                      {"NQ + ES 1 + 1", 1, 1, 1, 1}, {"NQ + ES 2 + 2", 2, 2, 2, 2}, {"NQ + ES 3 + 3", 3, 3, 3, 3}, {"NQ + ES 4 + 4 (eval) then 2 + 2", 4, 4, 2, 2},
                      {"NQ + ES 5 + 5", 5, 5, 5, 5}};
  for (const char* pname : {"select_flex_25k", "select_daily_25k", "growth_25k"}) {
  const payout::Plan* flex = nullptr; for (auto& pl : payout::plans()) if (pl.name == std::string(pname)) flex = &pl;
  printf("\n== %s (eval: $1,500 target, $1,000 drawdown, 40 %% consistency; funded: the plan's payout rules)\n", pname);
  for (auto& c : cfgs) {
    auto build = [&](int qn, int qe, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal, std::vector<double>* pnl) {
      st.assign(D, {}); cal.assign(D, nullptr); if (pnl) pnl->assign(D, 0);
      for (int i = 0; i < D; ++i) { const int d = dates[i]; int ndir = 0;
        if (qn) { auto it = nqd.find(d); if (it != nqd.end()) { auto& v = it->second; ndir = v.dir; st[i].push_back({v.p * qn, v.mae * qn, v.mfe * qn}); if (pnl) (*pnl)[i] += v.p * qn; } }
        if (qe) { auto it = esd.find(d); if (it != esd.end() && !(ndir && it->second.dir != ndir)) { auto& v = it->second; st[i].push_back({v.p * qe, v.mae * qe, v.mfe * qe}); if (pnl) (*pnl)[i] += v.p * qe; } }
        if (!st[i].empty()) cal[i] = &st[i]; } };
    std::vector<std::vector<payout::Trade>> s1, se_, sf; std::vector<const std::vector<payout::Trade>*> c1, ce, cf; std::vector<double> pnl;
    build(c.nq_f, c.es_f, s1, c1, &pnl);
    int td = 0, wins = 0; double tot = 0; for (int i = 0; i < D; ++i) { if (!s1[i].empty()) { ++td; wins += pnl[i] > 0; } tot += pnl[i]; }
    build(c.nq_e, c.es_e, se_, ce, nullptr); build(c.nq_f, c.es_f, sf, cf, nullptr);
    int ps = 0, pp = 0, pf = 0; std::vector<int> pdays;
    for (int s0 = 0; s0 + 252 < D; ++s0) { ++ps; auto e = payout::run_eval(*flex, ce, s0, 120); if (e.passed) { ++pp; pdays.push_back(e.days); } else if (e.failed) ++pf; }
    std::sort(pdays.begin(), pdays.end());
    std::vector<double> waits; int starts = 0, w60 = 0; double evals = 0, paid_yr = 0;
    for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; int i = s0, first = -1, ne = 0; double paid = 0;
      while (i < s0 + 252) { ++ne; auto e = payout::run_eval(*flex, ce, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
        auto r = payout::run_account(*flex, cf, i, s0 + 252 - i); paid += r.paid_sum;
        if (first < 0 && !r.pay_days.empty()) first = i + r.pay_days[0] - 1 - s0;
        i += std::max(1, r.days); if (!r.breach) break; }
      evals += ne; paid_yr += paid; if (first >= 0) { waits.push_back(first); if (first <= 60) ++w60; } }
    std::sort(waits.begin(), waits.end());
    printf("  %-34s trade days %4.1f%% (win %4.1f%%), $%5.0f/yr at funded size | eval: pass %4.1f%%, blown %4.1f%%, days to pass median %3d (fastest 10%% %2d) | 1st payout median %4.1f wks, evals/yr %4.1f, paid in the year $%5.0f\n",
           c.name, 100.0 * td / D, 100.0 * wins / std::max(1, td), tot / (D / 252.0), 100.0 * pp / ps, 100.0 * pf / ps, pdays.empty() ? 0 : pdays[pdays.size() / 2], pdays.empty() ? 0 : pdays[pdays.size() / 10], waits.empty() ? NAN : waits[waits.size() / 2] / 5, evals / starts, paid_yr / starts);
  }
}
}
