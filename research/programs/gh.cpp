// 2026-10-17 tests G and H on the adopted 25K plan: NQ cascade + gap retest, both when they agree, 2 micros per trade, the day's trades
// merged (pessimistic: their worst points coincide). Real 2019–26 walk-forward trades.
// H: size by signal strength — 3 micros on trades whose direction agrees with the overnight gap (≥ 0.1 ATR), else 1 or 2.
// G: N accounts started `gap` trading days apart, all trading the same plan: payouts per month across them, evals bought, $ paid.
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
  Series nq; std::string err; load_raw("data/edge_lab/cache", "NQ", nq, err); build_days(nq, fomc, vix); load_macro("data/edge_lab/cache", nq); load_events("data/edge_lab/cache", nq);
  Market mn(nq); Evaluator evn(mn); ExitSim sn(mn);
  auto get = [&](const std::string& spec, ExitRule x) { std::map<int, T3> out;
    for (auto& t : wf_trades(mn, evn, spec.c_str())) { const int d = nq.days[t.day].date; if (d < 20190101) continue; auto r = sn.run(t, x);
      T3 v{(r.pts - 1.5) * 2, (r.mae - 1.5) * 2, std::max(0.0, r.mfe) * 2, t.dir, nq.t[t.e]}; if (!out.count(d) || v.t < out[d].t) out[d] = v; } return out; };
  std::map<int, T3> src[2] = {get("research/ideas/so_cascade.json", X_VWAP_STALL), get(std::string(S) + "gap_or_retest_full.json", X_BASE)};
  std::vector<int> dates; std::map<int, double> gap; for (auto& d : nq.days) if (d.ok && d.date >= 20190101) { dates.push_back(d.date); gap[d.date] = (d.atr > 0 && !std::isnan(d.pc)) ? (d.ro - d.pc) / d.atr : 0; }
  const int D = dates.size();
  auto build = [&](int q_agree, int q_other, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal) {
    st.assign(D, {}); cal.assign(D, nullptr);
    for (int i = 0; i < D; ++i) { const int d = dates[i]; std::vector<const T3*> c;
      for (auto& m : src) { auto it = m.find(d); if (it != m.end()) c.push_back(&it->second); }
      std::sort(c.begin(), c.end(), [](const T3* a, const T3* b) { return a->t < b->t; });
      int dir = 0; double p = 0, ma = 0, mf = 0; bool any = false;
      for (auto* v : c) { if (dir && v->dir != dir) continue; dir = v->dir; const int q = v->dir * gap[d] >= 0.1 ? q_agree : q_other; p += v->p * q; ma += v->mae * q; mf += v->mfe * q; any = true; }
      if (any) { st[i] = {{p, std::min(ma, p), mf}}; cal[i] = &st[i]; } } };
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  // ── H
  for (const char* pname : {"select_flex_25k", "select_daily_25k"}) { const payout::Plan* pl = plan_of(pname);
    printf("\n== H: size by signal strength, %s\n", pname);
    for (auto [qa, qo, nm] : std::vector<std::tuple<int, int, const char*>>{{2, 2, "2 micros always (adopted plan)"}, {3, 2, "3 when the gap agrees, else 2"}, {3, 1, "3 when the gap agrees, else 1"}, {2, 1, "2 when the gap agrees, else 1"}}) {
      std::vector<std::vector<payout::Trade>> st; std::vector<const std::vector<payout::Trade>*> cal; build(qa, qo, st, cal);
      int starts = 0, pass = 0, blown = 0; double paid = 0; int ev = 0; std::vector<double> waits;
      for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; auto e = payout::run_eval(*pl, cal, s0, 120); pass += e.passed; blown += e.failed;
        int i = s0, first = -1; while (i < s0 + 252) { ++ev; auto e2 = payout::run_eval(*pl, cal, i, s0 + 252 - i); i += std::max(1, e2.days); if (!e2.passed) continue;
          auto r = payout::run_account(*pl, cal, i, s0 + 252 - i); paid += r.paid_sum; if (first < 0 && !r.pay_days.empty()) first = i + r.pay_days[0] - 1 - s0; i += std::max(1, r.days); if (!r.breach) break; }
        if (first >= 0) waits.push_back(first); }
      std::sort(waits.begin(), waits.end());
      printf("  %-32s eval pass %4.1f%% blown %4.1f%% · 1st payout %4.1f wks · paid/yr $%5.0f · evals/yr %4.1f\n", nm, 100.0 * pass / starts, 100.0 * blown / starts,
             waits.empty() ? NAN : waits[waits.size() / 2] / 5, paid / starts, (double)ev / starts); } }
  // ── G: N staggered accounts, 2 micros always
  { std::vector<std::vector<payout::Trade>> st; std::vector<const std::vector<payout::Trade>*> cal; build(2, 2, st, cal);
    for (const char* pname : {"select_flex_25k", "select_daily_25k"}) { const payout::Plan* pl = plan_of(pname);
      printf("\n== G: N accounts started a few days apart (each restarts with a new eval when blown), %s, first 12 months\n", pname);
      for (auto [n, gap_d] : std::vector<std::pair<int, int>>{{1, 0}, {2, 10}, {3, 10}, {3, 20}, {5, 10}, {5, 20}}) {
        double months_with = 0, payouts = 0, paid = 0, evals = 0, wait_first = 0; int starts = 0, got = 0;
        for (int s0 = 0; s0 + 252 + n * gap_d < D; s0 += 5) { ++starts; std::set<int> pay_months; int first = -1;
          for (int a = 0; a < n; ++a) { int i = s0 + a * gap_d;
            while (i < s0 + 252) { evals += 1; auto e = payout::run_eval(*pl, cal, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
              auto r = payout::run_account(*pl, cal, i, s0 + 252 - i); paid += r.paid_sum; payouts += r.payouts;
              for (int pdy : r.pay_days) { const int at = i + pdy - 1 - s0; pay_months.insert(at / 21); if (first < 0 || at < first) first = at; }
              i += std::max(1, r.days); if (!r.breach) break; } }
          months_with += pay_months.size(); if (first >= 0) { ++got; wait_first += first; } }
        printf("  %d account%s %2d days apart: first payout after %4.1f wks on average · months with a payout %4.1f of 12 · payouts/yr %4.1f · paid/yr $%6.0f · evals/yr %4.1f\n",
               n, n > 1 ? "s" : " ", gap_d, got ? wait_first / got / 5 : NAN, months_with / starts, payouts / starts, paid / starts, evals / starts); } } }
}
