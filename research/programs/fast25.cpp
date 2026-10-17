// 2026-10-17 founder: "1-3 trades a day ... 400-500 ... on a 25k if I take 3 winning days I can reach 1500 then daily payouts".
// The working plan's real 2019–26 trades on the 25K plans: eval (target $1,500, $1,000 drawdown, 40 % consistency, 3 days) and the
// funded Select Daily 25K (daily payouts above a $1,100 buffer) / Select Flex 25K, at 1–10 MNQ per trade. MNQ cost 1.5 pts.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe; int dir, e; };
int main() {
  const char* S = "research/programs/ho/";
  const double COST = 1.5;
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto get = [&](const std::string& spec, ExitRule x) { std::map<int, T3> out;
    for (auto& t : wf_trades(m, ev, spec.c_str())) { if (s.days[t.day].date < 20190101) continue; auto r = sim.run(t, x);
      if (!out.count(t.day)) out[t.day] = {r.pts - COST, r.mae - COST, std::max(0.0, r.mfe), t.dir, t.e}; } return out; };
  auto cas = get("research/ideas/so_cascade.json", X_VWAP_STALL), gor = get(std::string(S) + "gap_or_retest_full.json", X_BASE),
       wk = get("research/ideas/weak_day_long.json", X_BASE), mon = get(std::string(S) + "monday_full.json", X_BASE);
  std::vector<int> seq; for (int d = 0; d < (int)s.days.size(); ++d) if (s.days[d].ok && s.days[d].date >= 20190101) seq.push_back(d);
  const int D = seq.size();
  struct P { const char* name; bool w, mo; };
  const P plans[] = {{"cascade + gap retest (stops on every trade)", 0, 0}, {"all four (+ weak-day, Monday; no-stop holds)", 1, 1}};
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  for (auto& p : plans) {
    printf("\n== %s\n", p.name);
    printf("  %-5s | %-26s | %-58s | %s\n", "MNQ", "expected per day (all days)", "25K evaluation ($1,500 target, $1,000 drawdown), from every start day", "then Select Daily 25K funded, first 12 months");
    for (int q : {1, 2, 3, 5, 10}) {
      std::vector<std::vector<payout::Trade>> st(D); std::vector<const std::vector<payout::Trade>*> cal(D, nullptr); std::vector<double> day(D, 0.0);
      for (int i = 0; i < D; ++i) { const int d = seq[i];
        const T3* hold = nullptr; if (p.w && wk.count(d)) hold = &wk[d]; else if (p.mo && mon.count(d)) hold = &mon[d];
        const T3* dirt = nullptr;
        for (auto* src : {&cas, &gor}) { auto it = src->find(d); if (it == src->end()) continue; if (hold && it->second.dir < 0) continue; if (!dirt || it->second.e < dirt->e) dirt = &it->second; }
        if (hold) { st[i].push_back({hold->p * 2 * q, hold->mae * 2 * q, hold->mfe * 2 * q}); day[i] += hold->p * 2 * q; }
        if (dirt) { st[i].push_back({dirt->p * 2 * q, dirt->mae * 2 * q, dirt->mfe * 2 * q}); day[i] += dirt->p * 2 * q; }
        if (!st[i].empty()) cal[i] = &st[i]; }
      double mu = 0, sd = 0; for (double x : day) mu += x / D; for (double x : day) sd += (x - mu) * (x - mu) / D; sd = std::sqrt(sd);
      const payout::Plan* ev25 = plan_of("select_daily_25k");
      int starts = 0, pass = 0, fail = 0; std::vector<int> pd;
      double paid12 = 0; int npay12 = 0, breach12 = 0, funded = 0;
      for (int s0 = 0; s0 + 300 < D; s0 += 1) { ++starts;
        auto e = payout::run_eval(*ev25, cal, s0, 120);
        if (e.passed) { ++pass; pd.push_back(e.days);
          auto r = payout::run_account(*ev25, cal, s0 + e.days, 252); ++funded; paid12 += r.paid_sum; npay12 += r.payouts; breach12 += r.breach; }
        else if (e.failed) ++fail; }
      std::sort(pd.begin(), pd.end());
      printf("  %-5d | %+7.0f $/day (sd %5.0f)     | pass %4.1f%%  blown %4.1f%%  (rest unfinished in 120 days) · days to pass: median %3d, fastest 10%% %3d | %5.1f payouts, $%5.0f paid, account blown %3.0f%%\n",
             q, mu, sd, 100.0 * pass / starts, 100.0 * fail / starts, pd.empty() ? 0 : pd[pd.size() / 2], pd.empty() ? 0 : pd[pd.size() / 10],
             funded ? (double)npay12 / funded : 0, funded ? paid12 / funded : 0, funded ? 100.0 * breach12 / funded : 0);
    }
  }
}
