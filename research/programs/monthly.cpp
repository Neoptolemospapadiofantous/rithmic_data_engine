// How many staggered accounts for ≥ 1 payout every calendar month? cascade walk-forward trades, base vs VWAP+stall exit.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
int main(int argc, char** argv) {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, m, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &m, &d) == 3) fomc.insert(y * 10000 + m * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m);
  std::ifstream f("research/ideas/so_cascade.json"); Spec sp = parse_spec(json::parse(f));
  std::vector<std::vector<Trade>> vt;
  for (auto& p : grid(sp)) { Variant v; v.p = p; for (auto& c : sp.checks) { v.checks.emplace_back(parse(c.when, p), parse(c.dir, p)); ev.bind(*v.checks.back().first); ev.bind(*v.checks.back().second); }
    v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); ev.bind(*v.stop); ev.bind(*v.target); vt.push_back(run_variant(ev, s, sp, v, 0)); }
  WfOut w = walk(s, vt, 1.0, sp.train_months, 5);
  ExitSim sim(m);
  const int FROM = argc > 1 ? atoi(argv[1]) : 20190101;   // judge from this date (the cascade only pays since 2019)
  for (int rule : {X_BASE, X_VWAP, X_VWAP_STALL}) {
    std::map<int, std::vector<payout::Trade>> byday;
    for (auto& t : w.trades) {
      if (s.days[t.day].date < FROM) continue;
      ExitOut x = sim.run(t, rule);
      byday[t.day].push_back({x.pts - 1.5, x.mae - 1.5, std::max(0.0, x.mfe)});   // per MNQ in points; scaled below
    }
    if (byday.empty()) continue;
    std::vector<int> cal_day;
    for (int d = byday.begin()->first; d <= byday.rbegin()->first; ++d) if (s.days[d].ok) cal_day.push_back(d);
    const int D = cal_day.size();
    for (const char* pn : {"select_flex_50k", "select_daily_50k", "select_flex_25k", "select_daily_25k"}) {
      const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == pn) plan = &p;
      for (int mnq : {2, 4, 6}) {
        if (std::string(pn).find("25k") != std::string::npos && mnq > 2) continue;
        std::vector<std::vector<payout::Trade>> store(D);
        std::vector<const std::vector<payout::Trade>*> cal(D, nullptr);
        for (int i = 0; i < D; ++i) { auto it = byday.find(cal_day[i]); if (it == byday.end()) continue;
          for (auto x : it->second) store[i].push_back({x.pnl * 2 * mnq, x.mae * 2 * mnq, x.mfe * 2 * mnq}); cal[i] = &store[i]; }
        printf("%-7s %-17s %d MNQ |", exit_key(rule), pn, mnq);
        for (int N : {1, 2, 3, 4, 6, 8}) {
          std::set<int> pay_months; int evals = 0; double paid = 0; int pays = 0;
          for (int a = 0; a < N; ++a) {
            int i = a * 7;                                             // accounts opened a week (7 trading days) apart
            while (i < D) {
              ++evals; payout::EvalOut e = payout::run_eval(*plan, cal, i, D - i); i += std::max(1, e.days);
              if (!e.passed) continue;
              payout::RunOut r = payout::run_account(*plan, cal, i, D - i);
              for (int k : r.pay_days) pay_months.insert(s.days[cal_day[std::min(D - 1, i + k - 1)]].date / 100);
              paid += r.paid_sum; pays += r.payouts; i += std::max(1, r.days);
              if (!r.breach) break;
            }
          }
          int months = 0; for (int y = s.days[cal_day[0]].date / 100; y <= s.days[cal_day[D - 1]].date / 100; y = (y % 100 == 12) ? (y / 100 + 1) * 100 + 1 : y + 1) ++months;
          const double yrs = D / 252.0;
          printf(" N%d %3.0f%% %4.1fev %5.0f$ |", N, 100.0 * pay_months.size() / months, evals / yrs, paid / yrs);
        }
        printf("\n");
      }
    }
  }
}
