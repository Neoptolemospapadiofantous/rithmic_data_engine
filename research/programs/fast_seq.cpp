// Fastest first payout: plan × evaluation size × funded size, 10,000 synthetic years (real 2019–26 months resampled),
// cascade + VWAP/stall exit, measured cost 2.4 pts/MNQ. Withdraw as soon as the plan allows (speed objective).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
#include "wf_trades.inc"
int main() {
  const double COST = 2.4;
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto cas = wf_trades(m, ev, "research/ideas/so_cascade.json");
  std::map<int, std::vector<std::array<double, 3>>> pts;   // day → (net pts, mae, mfe) per MNQ before sizing
  for (auto& t : cas) { if (s.days[t.day].date < 20190101) continue; ExitOut x = sim.run(t, X_VWAP_STALL); pts[t.day].push_back({x.pts - COST, x.mae - COST, std::max(0.0, x.mfe)}); }
  std::vector<std::vector<int>> months; int cur = 0;
  for (int d = 0; d < (int)s.days.size(); ++d) { const Day& D = s.days[d]; if (!D.ok || D.date < 20190101) continue;
    if (D.date / 100 != cur) { months.emplace_back(); cur = D.date / 100; } months.back().push_back(d); }
  auto study = [&](const char* plan_name, int eval_mnq, int fund_mnq, int accounts, int stagger) {
    const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == plan_name) plan = &p;
    const int N = 10000; std::mt19937_64 rng(42);
    std::vector<double> first; int within[3] = {0, 0, 0}; double evals_to_first = 0, paid60 = 0; int got = 0;
    for (int it = 0; it < N; ++it) {
      std::vector<int> cal_days; for (int mth = 0; mth < 12; ++mth) { auto& md = months[rng() % months.size()]; cal_days.insert(cal_days.end(), md.begin(), md.end()); }
      const int D = cal_days.size();
      auto build = [&](int q, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal) {
        st.assign(D, {}); cal.assign(D, nullptr);
        for (int i = 0; i < D; ++i) { auto f = pts.find(cal_days[i]); if (f == pts.end()) continue;
          for (auto& x : f->second) st[i].push_back({x[0] * 2 * q, x[1] * 2 * q, x[2] * 2 * q}); cal[i] = &st[i]; } };
      std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf;
      build(eval_mnq, se, ce); build(fund_mnq, sf, cf);
      int best = -1; int evals_best = 0; double paid_by60 = 0;
      for (int a = 0; a < accounts; ++a) {
        int i = a * stagger, evals = 0;
        while (i < D) {
          ++evals; auto e = payout::run_eval(*plan, ce, i, D - i); i += std::max(1, e.days);
          if (!e.passed) continue;
          auto r = payout::run_account(*plan, cf, i, D - i);
          if (!r.pay_days.empty()) {
            const int at = i + r.pay_days[0] - 1;
            if (best < 0 || at < best) { best = at; evals_best = evals; }
            double cum = 0; for (size_t k = 0; k < r.pay_days.size(); ++k) if (i + r.pay_days[k] - 1 < 60 * 1) cum += 0;   // (amounts per payout not kept)
            break;
          }
          i += std::max(1, r.days); if (!r.breach) break;
        }
      }
      if (best >= 0) { ++got; first.push_back(best); evals_to_first += evals_best;
        if (best < 21) ++within[0]; if (best < 42) ++within[1]; if (best < 63) ++within[2]; }
      (void)paid_by60;
    }
    std::sort(first.begin(), first.end());
    auto q = [&](double p) { return first.empty() ? NAN : first[(size_t)(p * (first.size() - 1))] / 5.0; };
    printf("%-17s eval %d MNQ, funded %d MNQ, %d acct%s | first payout ≤ 1 month %3.0f%%  ≤ 2 months %3.0f%%  ≤ 3 months %3.0f%%  within the year %3.0f%% | median %4.1f wks (75%%: %4.1f) | evals until it %.1f\n",
           plan_name, eval_mnq, fund_mnq, accounts, accounts > 1 ? "s" : " ", 100.0 * within[0] / N, 100.0 * within[1] / N, 100.0 * within[2] / N, 100.0 * got / N, q(0.5), q(0.75), got ? evals_to_first / got : 0);
  };
  // A) resampled months, funded 1 MNQ
  study("select_flex_50k", 6, 1, 1, 0); study("select_flex_50k", 8, 1, 1, 0);
  // B) the REAL 2019–26 sequence: start a fresh account chain on every trading day, count trading days to the first
  //    payout (failed evals are replaced the next day), horizon 252 days
  std::vector<int> seq; for (auto& md : months) seq.insert(seq.end(), md.begin(), md.end());
  const int D = seq.size();
  auto real_seq = [&](const char* plan_name, int e_mnq, int f_mnq) {
    const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == plan_name) plan = &p;
    auto build = [&](int q, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal) {
      st.assign(D, {}); cal.assign(D, nullptr);
      for (int i = 0; i < D; ++i) { auto f = pts.find(seq[i]); if (f == pts.end()) continue;
        for (auto& x : f->second) st[i].push_back({x[0] * 2 * q, x[1] * 2 * q, x[2] * 2 * q}); cal[i] = &st[i]; } };
    std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf;
    build(e_mnq, se, ce); build(f_mnq, sf, cf);
    std::vector<double> waits; int starts = 0, paid = 0, w60 = 0;
    for (int s0 = 0; s0 + 252 < D; ++s0) {
      ++starts; int i = s0, first = -1;
      while (i < s0 + 252) {
        auto e = payout::run_eval(*plan, ce, i, s0 + 252 - i); i += std::max(1, e.days);
        if (!e.passed) continue;
        auto r = payout::run_account(*plan, cf, i, s0 + 252 - i);
        if (!r.pay_days.empty()) { first = i + r.pay_days[0] - 1 - s0; break; }
        i += std::max(1, r.days); if (!r.breach) break;
      }
      if (first >= 0) { ++paid; waits.push_back(first); if (first <= 60) ++w60; }
    }
    std::sort(waits.begin(), waits.end());
    printf("REAL SEQUENCE %-16s eval %d → funded %d MNQ | starts %d | paid within 252 days %3.0f%% | first payout median %3.0f trading days (%.1f wks), 25%% %3.0f, 75%% %3.0f | ≤ 60 days %3.0f%%\n",
           plan_name, e_mnq, f_mnq, starts, 100.0 * paid / starts, waits[waits.size() / 2], waits[waits.size() / 2] / 5.0, waits[waits.size() / 4], waits[3 * waits.size() / 4], 100.0 * w60 / starts);
  };
  real_seq("select_flex_50k", 6, 1); real_seq("select_flex_50k", 6, 2); real_seq("select_flex_50k", 8, 2); real_seq("select_daily_50k", 6, 2);
}
