// Monte Carlo of the prop plan: 10,000 synthetic years built from real calendar months (2019–26, sampled with
// replacement, each month's days in order), run through Select Daily 50K: eval → funded (one withdrawal a month) → new
// eval after a breach. Scratch harness.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
static double COST = 1.5;
#include "wf_trades.inc"
#include "run_paced.inc"
struct Acct { const std::vector<Trade>* tr; int rule; int mnq; };
int main(int argc, char** argv) {
  if (argc > 1) COST = atof(argv[1]);
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto cas = wf_trades(m, ev, "research/ideas/so_cascade.json");
  auto wd = wf_trades(m, ev, "research/ideas/weak_day_long.json");
  const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == "select_daily_50k") plan = &p;
  // calendar of ok days 2019+ grouped by month (skip the 2026-03..08 data hole)
  std::vector<std::vector<int>> months; int cur = 0;
  for (int d = 0; d < (int)s.days.size(); ++d) { const Day& D = s.days[d]; if (!D.ok || D.date < 20190101) continue;
    if (D.date / 100 != cur) { months.emplace_back(); cur = D.date / 100; } months.back().push_back(d); }
  auto day_trades = [&](const std::vector<Acct>& accts) {          // per account: day → its trades ($)
    std::vector<std::map<int, std::vector<payout::Trade>>> out(accts.size());
    for (size_t a = 0; a < accts.size(); ++a) for (auto& t : *accts[a].tr) {
      if (s.days[t.day].date < 20190101) continue;
      ExitOut x = accts[a].rule >= 0 ? sim.run(t, accts[a].rule) : ExitOut{t.x, t.pts, t.mae, t.mfe, false};
      const double k = 2.0 * accts[a].mnq;
      out[a][t.day].push_back({(x.pts - COST) * k, (x.mae - COST) * k, std::max(0.0, x.mfe) * k}); }
    return out;
  };
  auto study = [&](const char* label, std::vector<Acct> accts, bool one_account) {
    auto dt = day_trades(accts);
    const int N = 10000; std::mt19937_64 rng(20261016);
    std::vector<double> paid, evals, first_pay_days, pay_months; int first_pass = 0, any_pay = 0;
    for (int it = 0; it < N; ++it) {
      std::vector<int> cal_days, month_of;                            // one synthetic year = 12 sampled months
      for (int mth = 0; mth < 12; ++mth) { auto& md = months[rng() % months.size()]; for (int d : md) { cal_days.push_back(d); month_of.push_back(mth); } }
      const int D = cal_days.size();
      std::vector<std::vector<std::vector<payout::Trade>>> store(one_account ? 1 : accts.size(), std::vector<std::vector<payout::Trade>>(D));
      std::vector<std::vector<const std::vector<payout::Trade>*>> cal(store.size(), std::vector<const std::vector<payout::Trade>*>(D, nullptr));
      for (int i = 0; i < D; ++i) for (size_t a = 0; a < accts.size(); ++a) {
        auto f = dt[a].find(cal_days[i]); if (f == dt[a].end()) continue;
        auto& dst = store[one_account ? 0 : a][i]; dst.insert(dst.end(), f->second.begin(), f->second.end()); }
      for (size_t a = 0; a < store.size(); ++a) for (int i = 0; i < D; ++i) if (!store[a][i].empty()) cal[a][i] = &store[a][i];
      double y_paid = 0; int y_evals = 0, first = -1; std::set<int> pm; bool passed_first = false;
      for (size_t a = 0; a < store.size(); ++a) {
        int i = 0, k = 0;
        while (i < D) {
          ++y_evals; auto e = payout::run_eval(*plan, cal[a], i, D - i); i += std::max(1, e.days);
          if (k++ == 0 && e.passed) passed_first = true;
          if (!e.passed) continue;
          auto r = run_paced(*plan, cal[a], i, D - i, 0.0, month_of);
          for (int pd : r.pay_days) { const int at = std::min(D - 1, i + pd - 1); pm.insert(month_of[at]); if (first < 0 || at < first) first = at; }
          y_paid += r.paid_sum; i += std::max(1, r.days); if (!r.breach) break;
        }
      }
      paid.push_back(y_paid); evals.push_back(y_evals); pay_months.push_back(pm.size());
      if (first >= 0) { ++any_pay; first_pay_days.push_back(first); }
      if (passed_first) ++first_pass;
    }
    auto pct = [](std::vector<double> v, double q) -> double { if (v.empty()) return NAN; std::sort(v.begin(), v.end()); return v[(size_t)(q * (v.size() - 1))]; };
    double mp = 0, me = 0, mm = 0; for (int i = 0; i < N; ++i) { mp += paid[i] / N; me += evals[i] / N; mm += pay_months[i] / N; }
    printf("%-46s 1st eval passes %3.0f%% | any payout in the year %3.0f%% | first payout: median day %3.0f (75%%: %3.0f) | months paid %4.1f/12 | "
           "paid/yr: mean $%5.0f, worst 10%% $%5.0f, best 10%% $%6.0f | evals/yr mean %.1f (worst 10%% %.0f)\n",
           label, 100.0 * first_pass / N, 100.0 * any_pay / N, pct(first_pay_days, 0.5), pct(first_pay_days, 0.75), mm,
           mp, pct(paid, 0.1), pct(paid, 0.9), me, pct(evals, 0.9));
  };
  study("cascade 2 MNQ", {{&cas, X_BASE, 2}}, true);
  study("cascade 4 MNQ", {{&cas, X_BASE, 4}}, true);
  study("cascade + VWAP/stall exit, 2 MNQ", {{&cas, X_VWAP_STALL, 2}}, true);
  study("cascade + VWAP/stall exit, 4 MNQ", {{&cas, X_VWAP_STALL, 4}}, true);
  study("ONE account: cascade 4 MNQ + weak-day 1 MNQ", {{&cas, X_VWAP_STALL, 4}, {&wd, -1, 1}}, true);
  study("TWO accounts: cascade 4 MNQ | weak-day 1 MNQ", {{&cas, X_VWAP_STALL, 4}, {&wd, -1, 1}}, false);
}
