// 2026-10-17 "payouts need to be faster": where the time to the first payout goes, and which levers shorten it.
// Real 2019–26 sequence, a fresh chain (eval → funded, failed eval replaced next day) started on every trading day,
// horizon 252 days. Cascade + VWAP/stall exit, weak-day long (VIX ≥ 20) plain. Measured cost 1.5 pts per MNQ RT.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe; int dir; };
int main() {
  const double COST = 1.5;
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  std::map<int, std::vector<T3>> cas, wk;
  for (auto& t : wf_trades(m, ev, "research/ideas/so_cascade.json")) { if (s.days[t.day].date < 20190101) continue; auto x = sim.run(t, X_VWAP_STALL); cas[t.day].push_back({x.pts - COST, x.mae - COST, std::max(0.0, x.mfe), t.dir}); }
  for (auto& t : wf_trades(m, ev, "research/ideas/weak_day_long.json")) { if (s.days[t.day].date < 20190101) continue; auto x = sim.run(t, X_BASE); wk[t.day].push_back({x.pts - COST, x.mae - COST, std::max(0.0, x.mfe), t.dir}); }
  std::vector<int> seq; for (int d = 0; d < (int)s.days.size(); ++d) if (s.days[d].ok && s.days[d].date >= 20190101) seq.push_back(d);
  const int D = seq.size();
  printf("days %d, cascade trades %zu, weak-day trades %zu\n", D, cas.size(), wk.size());
  // sizes: cascade c MNQ, weak w MNQ; when both fire the same day the cascade is taken only in the weak trade's direction
  auto build = [&](int c, int w, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal) {
    st.assign(D, {}); cal.assign(D, nullptr);
    for (int i = 0; i < D; ++i) { int d = seq[i]; auto fw = wk.find(d); bool hw = w > 0 && fw != wk.end();
      if (hw) for (auto& x : fw->second) st[i].push_back({x.p * 2 * w, x.mae * 2 * w, x.mfe * 2 * w});
      auto fc = cas.find(d); if (c > 0 && fc != cas.end()) for (auto& x : fc->second) { if (hw && x.dir < 0) continue; st[i].push_back({x.p * 2 * c, x.mae * 2 * c, x.mfe * 2 * c}); }
      if (!st[i].empty()) cal[i] = &st[i]; } };
  auto study = [&](const char* label, const char* plan_name, int ec, int ew, int fc, int fw) {
    const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == plan_name) plan = &p;
    std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf; build(ec, ew, se, ce); build(fc, fw, sf, cf);
    std::vector<double> waits, ed, fd; int starts = 0, w40 = 0, w60 = 0; double evals = 0, amt = 0;
    for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; int i = s0, n = 0, eday = 0, fday = 0, first = -1; double a = 0;
      while (i < s0 + 252) { ++n; auto e = payout::run_eval(*plan, ce, i, s0 + 252 - i); i += std::max(1, e.days); eday += std::max(1, e.days); if (!e.passed) continue;
        auto r = payout::run_account(*plan, cf, i, s0 + 252 - i);
        if (!r.pay_days.empty()) { first = i + r.pay_days[0] - 1 - s0; fday += r.pay_days[0]; a = r.payouts ? r.paid_sum / r.payouts : 0; break; }
        i += std::max(1, r.days); fday += std::max(1, r.days); if (!r.breach) break; }
      if (first >= 0) { waits.push_back(first); ed.push_back(eday); fd.push_back(fday); evals += n; amt += a; if (first <= 40) ++w40; if (first <= 60) ++w60; } }
    auto med = [](std::vector<double> v) { if (v.empty()) return (double)NAN; std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
    size_t g = waits.size();
    printf("%-44s | paid in yr %3.0f%% | first payout median %4.1f wks (eval %4.1f + funded %4.1f) | <=8 wks %3.0f%% <=12 wks %3.0f%% | evals %.1f | avg 1st payout $%.0f\n",
           label, 100.0 * g / starts, med(waits) / 5, med(ed) / 5, med(fd) / 5, 100.0 * w40 / starts, 100.0 * w60 / starts, g ? evals / g : 0, g ? amt / g : 0);
  };
  auto base_cas = cas;
  for (double k : {2.0, 4.0, 6.0, 10.0}) { cas = base_cas; for (auto& [d, v] : cas) for (auto& x : v) x.p += k;
    char b[96]; snprintf(b, sizeof b, "EDGE +%.0f pts/trade (synthetic): 8 -> 2", k); study(b, "select_flex_50k", 8, 0, 2, 0); }
  cas = base_cas;
  // staggered accounts: first payout on ANY of n accounts started `gap` trading days apart
  auto stagger = [&](int n, int gap) { const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == std::string("select_flex_50k")) plan = &p;
    std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf; build(8, 0, se, ce); build(2, 0, sf, cf);
    std::vector<double> w; int starts = 0, w40 = 0; double evals = 0;
    for (int s0 = 0; s0 + 252 + n * gap < D; ++s0) { ++starts; int best = -1;
      for (int a = 0; a < n; ++a) { int i = s0 + a * gap; while (i < s0 + 252) { evals += 1; auto e = payout::run_eval(*plan, ce, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
          auto r = payout::run_account(*plan, cf, i, s0 + 252 - i); if (!r.pay_days.empty()) { int at = i + r.pay_days[0] - 1 - s0; if (best < 0 || at < best) best = at; break; }
          i += std::max(1, r.days); if (!r.breach) break; } }
      if (best >= 0) { w.push_back(best); if (best <= 40) ++w40; } }
    std::sort(w.begin(), w.end());
    printf("%d Flex accounts, %2d days apart (any account)            | first payout median %4.1f wks | <=8 wks %3.0f%% | evals per start %.1f\n", n, gap, w[w.size() / 2] / 5, 100.0 * w40 / starts, evals / starts); };
  stagger(1, 0); stagger(2, 10); stagger(3, 10); stagger(3, 20);
  printf("── baseline + funded size ──\n");
  study("Flex: eval 8 -> funded 2 (today's plan)", "select_flex_50k", 8, 0, 2, 0);
  study("Flex: eval 8 -> funded 3", "select_flex_50k", 8, 0, 3, 0);
  study("Flex: eval 8 -> funded 4", "select_flex_50k", 8, 0, 4, 0);
  study("Flex: eval 8 -> funded 6", "select_flex_50k", 8, 0, 6, 0);
  printf("── eval size ──\n");
  study("Flex: eval 6 -> funded 3", "select_flex_50k", 6, 0, 3, 0);
  study("Flex: eval 10 -> funded 3", "select_flex_50k", 10, 0, 3, 0);
  study("Flex: eval 12 -> funded 3", "select_flex_50k", 12, 0, 3, 0);
  printf("── add the weak-day long on the same account ──\n");
  study("Flex: eval 8+wk2 -> funded 2+wk2", "select_flex_50k", 8, 2, 2, 2);
  study("Flex: eval 8+wk4 -> funded 3+wk3", "select_flex_50k", 8, 4, 3, 3);
  study("Flex: eval 8+wk4 -> funded 4+wk4", "select_flex_50k", 8, 4, 4, 4);
  printf("── other plans, same strategy ──\n");
  for (const char* p : {"select_daily_50k", "mff_rapid_50k", "mff_rapid_eod_50k", "lucid_flex_50k", "apex_eod_50k", "topstep_50k", "growth_50k"})
    for (int f : {2, 4}) { char b[96]; snprintf(b, sizeof b, "%s: eval 8 -> funded %d", p, f); study(b, p, 8, 0, f, 0); }
}
