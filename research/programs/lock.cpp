// 2026-10-17 daily profit lock (research/preregistered/2026-10-17_daily_lock.md): close and stop for the day once the day's open
// profit reaches X. Real 2019–26 walk-forward trades; 25K accounts.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe, cost; int dir; int64_t t; };   // per contract, in $ (p, mae net of cost; mfe gross)
int main() {
  const char* S = "research/programs/ho/";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series nq, es; std::string err; load_raw("data/edge_lab/cache", "NQ", nq, err); load_raw("data/edge_lab/cache", "ES", es, err);
  for (Series* x : {&nq, &es}) { build_days(*x, fomc, vix); load_macro("data/edge_lab/cache", *x); load_events("data/edge_lab/cache", *x); }
  Market mn(nq), me(es); Evaluator evn(mn), eve(me); ExitSim sn(mn), se(me);
  auto get = [&](Market& m, Evaluator& ev, ExitSim& sim, const std::string& spec, ExitRule x, double cost, double pv) { std::map<int, T3> out; const Series& s = m.s();
    for (auto& t : wf_trades(m, ev, spec.c_str())) { const int d = s.days[t.day].date; if (d < 20190101) continue; auto r = sim.run(t, x);
      T3 v{(r.pts - cost) * pv, (r.mae - cost) * pv, std::max(0.0, r.mfe) * pv, cost * pv, t.dir, s.t[t.e]};
      auto it = out.find(d); if (it == out.end() || v.t < it->second.t) out[d] = v; } return out; };
  auto merge = [](std::map<int, T3> a, const std::map<int, T3>& b) { for (auto& [d, v] : b) { auto it = a.find(d); if (it == a.end() || v.t < it->second.t) a[d] = v; } return a; };
  auto nqd = merge(get(mn, evn, sn, "research/ideas/so_cascade.json", X_VWAP_STALL, 1.5, 2.0), get(mn, evn, sn, std::string(S) + "gap_or_retest_full.json", X_BASE, 1.5, 2.0));
  auto esd = merge(get(me, eve, se, "research/ideas/es_cascade.json", X_BASE, 0.6, 5.0), get(me, eve, se, std::string(S) + "es_gap_or_retest_full.json", X_BASE, 0.6, 5.0));
  std::vector<int> dates; for (auto& d : nq.days) if (d.ok && d.date >= 20190101) dates.push_back(d.date);
  const int D = dates.size();
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  for (const char* pname : {"select_daily_25k", "select_flex_25k"}) {
    const payout::Plan* pl = plan_of(pname);
    for (int both = 0; both < 2; ++both) for (int q : {2, 3}) {
      printf("\n== %s · %s · %d micros per market\n", pname, both ? "NQ + ES (lock approximate, optimistic)" : "NQ only (lock exact)", q);
      for (double X : {0.0, 100.0, 150.0, 200.0, 300.0, 500.0}) {
        std::vector<std::vector<payout::Trade>> st(D); std::vector<const std::vector<payout::Trade>*> cal(D, nullptr);
        int days_locked = 0; double tot = 0;
        for (int i = 0; i < D; ++i) { const int d = dates[i];
          double p = 0, mae = 0, mfe = 0, cst = 0; int ndir = 0; bool any = false;
          auto it = nqd.find(d); if (it != nqd.end()) { auto& v = it->second; p += v.p * q; mae += v.mae * q; mfe += v.mfe * q; cst += v.cost * q; ndir = v.dir; any = true; }
          if (both) { auto jt = esd.find(d); if (jt != esd.end() && !(ndir && jt->second.dir != ndir)) { auto& v = jt->second; p += v.p * q; mae += v.mae * q; mfe += v.mfe * q; cst += v.cost * q; any = true; } }
          if (!any) continue;
          if (X > 0 && mfe >= X) { p = X - cst; mfe = X; ++days_locked; }
          st[i].push_back({p, std::min(mae, p), mfe}); cal[i] = &st[i]; tot += p; }
        int starts = 0, pass = 0, blown = 0; std::vector<int> pd; double paid = 0; int ev = 0; std::vector<double> waits;
        for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts;
          auto e = payout::run_eval(*pl, cal, s0, 120); if (e.passed) { ++pass; pd.push_back(e.days); } else if (e.failed) ++blown;
          int i = s0, first = -1; double py = 0;
          while (i < s0 + 252) { ++ev; auto e2 = payout::run_eval(*pl, cal, i, s0 + 252 - i); i += std::max(1, e2.days); if (!e2.passed) continue;
            auto r = payout::run_account(*pl, cal, i, s0 + 252 - i); py += r.paid_sum; if (first < 0 && !r.pay_days.empty()) first = i + r.pay_days[0] - 1 - s0;
            i += std::max(1, r.days); if (!r.breach) break; }
          paid += py; if (first >= 0) waits.push_back(first); }
        std::sort(pd.begin(), pd.end()); std::sort(waits.begin(), waits.end());
        printf("  lock %-6s days locked %4.1f%% · $%6.0f/yr · eval pass %4.1f%% blown %4.1f%% (median %3d days) · 1st payout %4.1f wks · paid/yr $%5.0f · evals/yr %4.1f\n",
               X > 0 ? ("$" + std::to_string((int)X)).c_str() : "none", 100.0 * days_locked / D, tot / (D / 252.0), 100.0 * pass / starts, 100.0 * blown / starts,
               pd.empty() ? 0 : pd[pd.size() / 2], waits.empty() ? NAN : waits[waits.size() / 2] / 5, paid / starts, (double)ev / starts);
      }
    }
  }
}
