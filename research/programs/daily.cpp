// 2026-10-17 "a trade every day": (1) the cascade's trades by check slot (09:50 / 10:30 / 11:00 / 11:30 / 12:00) per era, with the
// current VWAP + stall exits; (2) how many days the plan trades, by weekday, with and without the 12:00 check; (3) payout speed.
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
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  // (1) slots, NQ cost 1.0, all years
  { auto tr = wf_trades(m, ev, "research/ideas/cascade_noon.json");
    const char* sn[] = {"09:50", "10:30", "11:00", "11:30", "12:00"};
    double sum[5][4] = {}; int n[5][4] = {};
    for (auto& t : tr) { if (t.src < 0 || t.src > 4) continue; const int d = s.days[t.day].date, e = d < 20190101 ? 0 : d < 20230101 ? 1 : d < 20250101 ? 2 : 3;
      const double p = sim.run(t, X_VWAP_STALL).pts - 1.0; sum[t.src][e] += p; ++n[t.src][e]; }
    printf("cascade + 12:00, walk-forward trades by check slot (VWAP+stall exits, NQ pts/trade after 1.0 cost)\n%-6s %16s %16s %16s %16s\n", "slot", "2010-18", "2019-22", "2023-24", "2025+ (holdout)");
    for (int k = 0; k < 5; ++k) { printf("%-6s", sn[k]); for (int e = 0; e < 4; ++e) printf("   %+6.2f (n %4d)", n[k][e] ? sum[k][e] / n[k][e] : NAN, n[k][e]); printf("\n"); } }
  // (2)(3) the plan, 2019+, MNQ cost 1.5
  const double COST = 1.5;
  auto get = [&](const std::string& spec, ExitRule x) { std::map<int, T3> out;
    for (auto& t : wf_trades(m, ev, spec.c_str())) { if (s.days[t.day].date < 20190101) continue; auto r = sim.run(t, x);
      if (!out.count(t.day)) out[t.day] = {r.pts - COST, r.mae - COST, std::max(0.0, r.mfe), t.dir, t.e}; } return out; };
  auto cas = get("research/ideas/so_cascade.json", X_VWAP_STALL), noon = get("research/ideas/cascade_noon.json", X_VWAP_STALL),
       gor = get(std::string(S) + "gap_or_retest_full.json", X_BASE), wk = get("research/ideas/weak_day_long.json", X_BASE), mon = get(std::string(S) + "monday_full.json", X_BASE);
  std::vector<int> seq; for (int d = 0; d < (int)s.days.size(); ++d) if (s.days[d].ok && s.days[d].date >= 20190101) seq.push_back(d);
  const int D = seq.size();
  struct Plan { const char* name; std::map<int, T3>* c; bool g, w, mo; };
  Plan plans[] = {{"cascade (today)", &cas, 0, 0, 0}, {"cascade + gap retest", &cas, 1, 0, 0}, {"cascade + 12:00 + gap retest", &noon, 1, 0, 0},
                  {"all four (cascade, gap, weak-day, Monday)", &cas, 1, 1, 1}, {"all four, cascade + 12:00", &noon, 1, 1, 1}};
  const payout::Plan* flex = nullptr; for (auto& pl : payout::plans()) if (pl.name == std::string("select_flex_50k")) flex = &pl;
  printf("\nthe plan, 2019-01 → 2026-10 (%d sessions), MNQ cost 1.5, 1 MNQ per piece\n", D);
  for (auto& p : plans) {
    auto build = [&](int dq, int hq, std::vector<std::vector<payout::Trade>>& st, std::vector<const std::vector<payout::Trade>*>& cal, std::vector<double>* pts) {
      st.assign(D, {}); cal.assign(D, nullptr); if (pts) pts->assign(D, 0.0);
      for (int i = 0; i < D; ++i) { const int d = seq[i];
        const T3* hold = nullptr; if (p.w && wk.count(d)) hold = &wk[d]; else if (p.mo && mon.count(d)) hold = &mon[d];
        const T3* dirt = nullptr;
        auto consider = [&](std::map<int, T3>& src, bool on) { if (!on) return; auto it = src.find(d); if (it == src.end()) return;
          if (hold && it->second.dir < 0) return; if (!dirt || it->second.e < dirt->e) dirt = &it->second; };
        consider(*p.c, true); consider(gor, p.g);
        if (hold && hq > 0) { st[i].push_back({hold->p * 2 * hq, hold->mae * 2 * hq, hold->mfe * 2 * hq}); if (pts) (*pts)[i] += hold->p; }
        if (dirt && dq > 0) { st[i].push_back({dirt->p * 2 * dq, dirt->mae * 2 * dq, dirt->mfe * 2 * dq}); if (pts) (*pts)[i] += dirt->p; }
        if (!st[i].empty()) cal[i] = &st[i]; } };
    std::vector<std::vector<payout::Trade>> st, se, sf; std::vector<const std::vector<payout::Trade>*> cal, ce, cf; std::vector<double> pts;
    build(1, 1, st, cal, &pts);
    int tdays = 0, wdn[6] = {}, wdt[6] = {}, win = 0; double net = 0, pk = 0, eq = 0, dd = 0;
    for (int i = 0; i < D; ++i) { const int wd = s.days[seq[i]].dow; ++wdn[wd]; if (!st[i].empty()) { ++tdays; ++wdt[wd]; win += pts[i] > 0; }
      eq += pts[i]; net += pts[i]; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); }
    build(8, 2, se, ce, nullptr); build(2, 1, sf, cf, nullptr);
    std::vector<double> waits; int starts = 0, w60 = 0;
    for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; int i = s0, first = -1;
      while (i < s0 + 252) { auto e = payout::run_eval(*flex, ce, i, s0 + 252 - i); i += std::max(1, e.days); if (!e.passed) continue;
        auto r = payout::run_account(*flex, cf, i, s0 + 252 - i); if (!r.pay_days.empty()) { first = i + r.pay_days[0] - 1 - s0; break; }
        i += std::max(1, r.days); if (!r.breach) break; }
      if (first >= 0) { waits.push_back(first); if (first <= 60) ++w60; } }
    std::sort(waits.begin(), waits.end());
    printf("  %-42s trades on %4.1f%% of days (Mon %2.0f Tue %2.0f Wed %2.0f Thu %2.0f Fri %2.0f%%) · winning days %4.1f%% of trade days · %+5.1f pts/day traded · max DD %4.0f · Flex 1st payout %4.1f wks, <=12 wks %3.0f%%\n",
           p.name, 100.0 * tdays / D, 100.0 * wdt[1] / wdn[1], 100.0 * wdt[2] / wdn[2], 100.0 * wdt[3] / wdn[3], 100.0 * wdt[4] / wdn[4], 100.0 * wdt[5] / wdn[5],
           100.0 * win / std::max(1, tdays), net / std::max(1, tdays), dd, waits.empty() ? NAN : waits[waits.size() / 2] / 5, 100.0 * w60 / starts);
  }
}
