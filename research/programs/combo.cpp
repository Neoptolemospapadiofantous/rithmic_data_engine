// 2026-10-17 "do all and test all": the working pieces as ONE daily plan, on the real 2019–26 sequence.
// Pieces (walk-forward trades from edge_lab specs, measured MNQ cost 1.5 pts):
//   D1 cascade (so_cascade, VWAP + stall exits)        directional, 09:50–12:00
//   D2 gap_or_retest (stop/target)                      directional, 09:45–13:55
//   H1 weak-day long (VIX >= 20, 09:30 → 15:55)          long hold
//   H2 Monday long (09:30 → 15:55)                       long hold
// Conflict rules: at most ONE directional trade a day (the earlier entry wins); a long hold that day → directional shorts
// are skipped (no opposite positions); two holds on the same day = one hold position.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
using namespace lab;
#include "wf_trades.inc"
struct T3 { double p, mae, mfe; int dir, e; };
int main() {
  const double COST = 1.5;
  const char* S = "research/programs/ho/";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto get = [&](const std::string& spec, ExitRule x) { std::map<int, T3> out;
    for (auto& t : wf_trades(m, ev, spec.c_str())) { if (s.days[t.day].date < 20190101) continue; auto r = sim.run(t, x);
      if (!out.count(t.day)) out[t.day] = {r.pts - COST, r.mae - COST, std::max(0.0, r.mfe), t.dir, t.e}; } return out; };
  auto cas = get("research/ideas/so_cascade.json", X_VWAP_STALL), gor = get(std::string(S) + "gap_or_retest_full.json", X_BASE);
  auto wk = get("research/ideas/weak_day_long.json", X_BASE), mon = get(std::string(S) + "monday_full.json", X_BASE);
  std::vector<int> seq; for (int d = 0; d < (int)s.days.size(); ++d) if (s.days[d].ok && s.days[d].date >= 20190101) seq.push_back(d);
  const int D = seq.size();
  int both = 0, same = 0; for (auto& [d, a] : cas) { auto it = gor.find(d); if (it != gor.end()) { ++both; same += it->second.dir == a.dir; } }
  printf("days %d · trades: cascade %zu, gap retest %zu (both on %d days, same direction on %d), weak-day %zu, Monday %zu\n\n",
         D, cas.size(), gor.size(), both, same, wk.size(), mon.size());

  struct Plan { const char* name; bool c, g, w, mo; };
  const Plan plans[] = {{"cascade only (today's plan)", 1, 0, 0, 0}, {"cascade + gap retest", 1, 1, 0, 0}, {"gap retest only", 0, 1, 0, 0},
                        {"cascade + weak-day", 1, 0, 1, 0}, {"cascade + weak-day + Monday", 1, 0, 1, 1}, {"cascade + gap + weak-day", 1, 1, 1, 0},
                        {"all four", 1, 1, 1, 1}};
  // day trades for a plan at sizes (directional dq MNQ, hold hq MNQ)
  auto build = [&](const Plan& p, int dq, int hq, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal, std::vector<double>* pts) {
    st.assign(D, {}); cal.assign(D, nullptr); if (pts) pts->assign(D, 0.0);
    for (int i = 0; i < D; ++i) { const int d = seq[i];
      const T3* hold = nullptr; if (p.w && wk.count(d)) hold = &wk[d]; else if (p.mo && mon.count(d)) hold = &mon[d];
      const T3* dirt = nullptr;
      auto consider = [&](std::map<int, T3>& src, bool on) { if (!on) return; auto it = src.find(d); if (it == src.end()) return;
        if (hold && it->second.dir < 0) return; if (!dirt || it->second.e < dirt->e) dirt = &it->second; };
      consider(cas, p.c); consider(gor, p.g);
      if (hold && hq > 0) { st[i].push_back({hold->p * 2 * hq, hold->mae * 2 * hq, hold->mfe * 2 * hq}); if (pts) (*pts)[i] += hold->p; }
      if (dirt && dq > 0) { st[i].push_back({dirt->p * 2 * dq, dirt->mae * 2 * dq, dirt->mfe * 2 * dq}); if (pts) (*pts)[i] += dirt->p; }
      if (!st[i].empty()) cal[i] = &st[i]; } };
  const payout::Plan* flex = nullptr; for (auto& pl : payout::plans()) if (pl.name == std::string("select_flex_50k")) flex = &pl;
  printf("%-30s | %7s %8s %9s %8s %7s %6s | %s\n", "plan (1 MNQ per piece, pts)", "trades", "pts/yr", "max DD", "worst m", "+months", "days/wk",
         "Flex 50K: eval 8 dir + 2 hold → funded 2 dir + 1 hold");
  for (auto& p : plans) {
    std::vector<std::vector<payout::Trade>> st, se, sf; std::vector<const std::vector<payout::Trade>*> cal, ce, cf; std::vector<double> pts;
    build(p, 1, 1, st, cal, &pts);
    int n = 0, tdays = 0; double eq = 0, pk = 0, dd = 0; std::map<int, double> mon_net;
    for (int i = 0; i < D; ++i) { n += st[i].size(); tdays += !st[i].empty(); eq += pts[i]; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); mon_net[s.days[seq[i]].date / 100] += pts[i]; }
    int mp = 0; double worst = 0; for (auto& [k, v] : mon_net) { mp += v > 0; worst = std::min(worst, v); }
    const double yrs = D / 252.0;
    // payout chain from every start day, 252-day horizon (as the payout-speed study)
    build(p, 8, 2, se, ce, nullptr); build(p, 2, 1, sf, cf, nullptr);
    std::vector<double> waits; int starts = 0, w60 = 0; double evals = 0, amt = 0;
    for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; int i = s0, ne = 0, first = -1; double a = 0;
      while (i < s0 + 252) { ++ne; auto e = payout::run_eval(*flex, ce, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
        auto r = payout::run_account(*flex, cf, i, s0 + 252 - i);
        if (!r.pay_days.empty()) { first = i + r.pay_days[0] - 1 - s0; a = r.payouts ? r.paid_sum / r.payouts : 0; break; }
        i += std::max(1, r.days); if (!r.breach) break; }
      if (first >= 0) { waits.push_back(first); evals += ne; amt += a; if (first <= 60) ++w60; } }
    std::sort(waits.begin(), waits.end());
    const size_t g = waits.size();
    // the same, by the start date: 2019-22 vs 2023+
    std::string halves;
    for (int h = 0; h < 2; ++h) { std::vector<double> w; int st = 0;
      for (int s0 = 0; s0 + 252 < D; ++s0) { const int dt = s.days[seq[s0]].date; if ((dt >= 20230101) != (bool)h) continue; ++st; int i = s0, first = -1;
        while (i < s0 + 252) { auto e = payout::run_eval(*flex, ce, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
          auto r = payout::run_account(*flex, cf, i, s0 + 252 - i); if (!r.pay_days.empty()) { first = i + r.pay_days[0] - 1 - s0; break; }
          i += std::max(1, r.days); if (!r.breach) break; }
        if (first >= 0) w.push_back(first); }
      std::sort(w.begin(), w.end()); char b[64]; snprintf(b, sizeof b, " %s %.1f wks (%d starts)", h ? "2023+" : "2019-22", w.empty() ? NAN : w[w.size() / 2] / 5, st); halves += b; }
    printf("%-30s | %7d %+8.0f %9.0f %+8.0f %6.0f%% %6.1f | paid in yr %3.0f%%, 1st payout median %4.1f wks, <=12 wks %3.0f%%, evals %.1f, avg 1st $%.0f |%s\n",
           p.name, n, eq / yrs, dd, worst, 100.0 * mp / mon_net.size(), 5.0 * tdays / D, 100.0 * g / starts, g ? waits[g / 2] / 5 : NAN, 100.0 * w60 / starts, g ? evals / g : 0, g ? amt / g : 0, halves.c_str());
  }
}
