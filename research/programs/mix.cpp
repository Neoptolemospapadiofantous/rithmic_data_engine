// 2026-10-16 round 3 tests 5 + 6: account mix (paid months / withdrawn / evals) and worst case at 8 MNQ.
// cascade + VWAP/stall, 2019–26, measured cost 1.5 pts/MNQ.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <set>
using namespace lab;
#include "wf_trades.inc"
int main() {
  const double COST = 1.5;
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto cas = wf_trades(m, ev, "research/ideas/so_cascade.json");
  std::map<int, std::vector<std::array<double, 3>>> pts;
  std::vector<std::pair<int, std::array<double, 3>>> seqt;
  for (auto& t : cas) { if (s.days[t.day].date < 20190101) continue; ExitOut x = sim.run(t, X_VWAP_STALL);
    std::array<double, 3> v{x.pts - COST, x.mae - COST, std::max(0.0, x.mfe)}; pts[t.day].push_back(v); seqt.push_back({t.day, v}); }
  std::vector<std::vector<int>> months; int cur = 0;
  for (int d = 0; d < (int)s.days.size(); ++d) { const Day& D = s.days[d]; if (!D.ok || D.date < 20190101) continue;
    if (D.date / 100 != cur) { months.emplace_back(); cur = D.date / 100; } months.back().push_back(d); }
  // ── 6. worst case at 8 MNQ (real sequence) ──
  { const double usd = 8 * 2.0; double eq = 0, peak = 0, mdd = 0; int streak = 0, worst_streak = 0;
    std::map<int, double> dayp; for (auto& [d, v] : seqt) { double p = v[0] * usd; eq += p; peak = std::max(peak, eq); mdd = std::max(mdd, peak - eq);
      streak = p < 0 ? streak + 1 : 0; worst_streak = std::max(worst_streak, streak); dayp[d] += p; }
    std::vector<int> days; for (auto& mo : months) days.insert(days.end(), mo.begin(), mo.end());
    double w20 = 1e18; int w20_at = 0; for (size_t i = 0; i + 20 <= days.size(); ++i) { double sum = 0; for (size_t k = i; k < i + 20; ++k) { auto f = dayp.find(days[k]); if (f != dayp.end()) sum += f->second; } if (sum < w20) { w20 = sum; w20_at = s.days[days[i]].date; } }
    double worst_day = 0, worst_mae = 0; for (auto& kv : dayp) worst_day = std::min(worst_day, kv.second); for (auto& [d, v] : seqt) worst_mae = std::min(worst_mae, v[1] * usd);
    int over = 0; { double e2 = 0, pk = 0; for (auto& [d, v] : seqt) { e2 += v[0] * usd; pk = std::max(pk, e2); if (pk - e2 >= 2000) { ++over; pk = e2; } } }
    printf("WORST CASE 8 MNQ 2019-26: %zu trades, net $%.0f | max drawdown $%.0f | longest losing streak %d trades | worst 20 trading days $%.0f (from %d) | worst day $%.0f | worst open loss in a trade $%.0f | times a $2,000 drawdown was hit %d (%.1f / yr)\n",
           seqt.size(), eq, mdd, worst_streak, w20, w20_at, worst_day, worst_mae, over, over / 7.8); }
  // ── 5. account mix: full-year chains (eval → funded until breach → new eval), 10,000 resampled years ──
  auto plan_of = [&](const char* n) { for (auto& p : payout::plans()) if (p.name == n) return &p; return (const payout::Plan*)nullptr; };
  struct Acct { const char* plan; int start; };
  auto mix = [&](const char* label, std::vector<Acct> accts) {
    const int N = 10000; std::mt19937_64 rng(7); double withdrawn = 0, evals = 0, paid_months = 0, zero_years = 0, payouts = 0;
    for (int it = 0; it < N; ++it) {
      std::vector<int> cal; for (int mth = 0; mth < 12; ++mth) { auto& md = months[rng() % months.size()]; cal.insert(cal.end(), md.begin(), md.end()); }
      const int D = cal.size();
      auto build = [&](int q, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& c) {
        st.assign(D, {}); c.assign(D, nullptr); for (int i = 0; i < D; ++i) { auto f = pts.find(cal[i]); if (f == pts.end()) continue;
          for (auto& x : f->second) st[i].push_back({x[0] * 2 * q, x[1] * 2 * q, x[2] * 2 * q}); c[i] = &st[i]; } };
      std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf; build(8, se, ce); build(2, sf, cf);
      std::set<int> pm; double w = 0;
      for (auto& a : accts) { const payout::Plan* p = plan_of(a.plan); int i = a.start;
        while (i < D) { evals += 1; auto e = payout::run_eval(*p, ce, i, D - i); i += std::max(1, e.days); if (!e.passed) continue;
          auto r = payout::run_account(*p, cf, i, D - i); for (int pd : r.pay_days) pm.insert((i + pd - 1) / 21); w += r.paid_sum; payouts += r.payouts;
          i += std::max(1, r.days); if (!r.breach) break; } }
      withdrawn += w; paid_months += pm.size(); if (w <= 0) ++zero_years; }
    printf("%-34s | paid months/yr %.1f | withdrawn $%.0f/yr | payouts %.1f | evals bought %.1f/yr | years with $0 %.0f%% | net at $150/eval $%.0f, at $250/eval $%.0f\n",
           label, paid_months / N, withdrawn / N, payouts / N, evals / N, 100.0 * zero_years / N, (withdrawn - 150 * evals) / N, (withdrawn - 250 * evals) / N);
  };
  mix("1 Flex 50K", {{"select_flex_50k", 0}});
  mix("2 Flex 50K, same day", {{"select_flex_50k", 0}, {"select_flex_50k", 0}});
  mix("2 Flex 50K, 4 weeks apart", {{"select_flex_50k", 0}, {"select_flex_50k", 20}});
  mix("Flex + Daily 50K", {{"select_flex_50k", 0}, {"select_daily_50k", 0}});
  mix("1 Daily 50K", {{"select_daily_50k", 0}});
}
