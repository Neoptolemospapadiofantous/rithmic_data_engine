// 2026-10-17 T1, T4, T5 (research/preregistered/2026-10-17_day_engine_plan.md).
// T1: cascade walk-forward trades 2023-03 → 2026-03 (order-flow years): all / meta-take / flow-agree / both (NQ pts after 1.0 cost).
// T4, T5: the day engine on 25K plans, NQ, 2 micros per trade, the day's trades merged (pessimistic), real 2019–26 trades.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
using namespace lab;
#include "wf_trades.inc"
struct TR { double p, mae, mfe; int dir; int64_t t; bool meta = true, flow = false, has_flow = false; int64_t tx = 0; };   // $ per MNQ (p, mae net of cost)
int main() {
  const char* S = "research/programs/ho/";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  std::map<int64_t, bool> meta_take; { std::ifstream f("data/edge_lab/reports/so_cascade__meta.trades.csv"); std::string l; std::getline(f, l);
    while (std::getline(f, l)) { std::stringstream ss(l); std::string a[7]; for (auto& x : a) std::getline(ss, x, ','); meta_take[std::stoll(a[1])] = a[6] == "1"; } }
  auto cdelta = [&](const Trade& t) { const Day& D = s.days[t.day]; double d = 0, v = 0; for (int i = D.rth0; i >= 0 && i < t.e; ++i) { d += 2.0 * s.bv[i] - s.v[i]; v += s.v[i]; } return std::make_pair(d, v); };
  auto get = [&](const std::string& spec, ExitRule x, bool cascade) { std::map<int, TR> out;
    for (auto& t : wf_trades(m, ev, spec.c_str())) { const int d = s.days[t.day].date; if (d < 20190101) continue; auto r = sim.run(t, x);
      TR v{(r.pts - 1.5) * 2, (r.mae - 1.5) * 2, std::max(0.0, r.mfe) * 2, t.dir, s.t[t.e]}; v.tx = s.t[std::min<int>(r.x, (int)s.size() - 1)];
      if (cascade) { auto it = meta_take.find(s.t[t.e]); v.meta = it == meta_take.end() ? true : it->second; auto [cd, cv] = cdelta(t); v.has_flow = s.days[t.day].flow && cv > 0; v.flow = v.has_flow && t.dir * cd > 0; }
      if (!out.count(d) || v.t < out[d].t) out[d] = v; } return out; };
  auto cas = get("research/ideas/so_cascade.json", X_VWAP_STALL, true), gor = get(std::string(S) + "gap_or_retest_full.json", X_BASE, false);
  std::string bbf = std::string(S) + "bb_on_gap_full.json";
  { std::ifstream in("research/ideas/bb_on_gap.json"); std::string all((std::istreambuf_iterator<char>(in)), {}); auto j = json::parse(all); j.erase("to"); std::ofstream(bbf) << j.dump(); }
  auto box = get(bbf, X_BASE, false);
  // ── T1 (NQ pts after 1.0 cost: $/2 + 0.5)
  { struct A { double s = 0; int n = 0; } all, mt, fl, both;
    for (auto& [d, v] : cas) { if (!v.has_flow) continue; const double p = v.p / 2 + 0.5; all.s += p; ++all.n; if (v.meta) { mt.s += p; ++mt.n; } if (v.flow) { fl.s += p; ++fl.n; } if (v.meta && v.flow) { both.s += p; ++both.n; } }
    printf("T1 cascade trades on order-flow days (2023-03 → 2026-03), NQ pts/trade after cost:\n  all %+.2f (n %d) · model takes %+.2f (n %d) · flow agrees %+.2f (n %d) · both %+.2f (n %d)\n",
           all.s / all.n, all.n, mt.s / mt.n, mt.n, fl.s / fl.n, fl.n, both.s / both.n, both.n); }
  std::vector<int> dates; for (auto& d : s.days) if (d.ok && d.date >= 20190101) dates.push_back(d.date);
  const int D = dates.size();
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  struct Cfg { const char* name; bool c, g, b, meta; int cap; bool stop_after_loss; };
  const Cfg cfgs[] = {{"T5 today's plan: cascade + gap retest", 1, 1, 0, 0, 9, 0}, {"T5 + box_go (3 entries)", 1, 1, 1, 0, 9, 0},
                      {"T5 + box_go, cascade only if the model takes it", 1, 1, 1, 1, 9, 0}, {"T4 cap 1 trade a day", 1, 1, 1, 0, 1, 0},
                      {"T4 cap 2 trades a day", 1, 1, 1, 0, 2, 0}, {"T4 stop after the first loss", 1, 1, 1, 0, 9, 1}, {"T4 cap 2 + stop after a loss", 1, 1, 1, 0, 2, 1}};
  for (const char* pname : {"select_flex_25k", "select_daily_25k"}) { const payout::Plan* pl = plan_of(pname);
    printf("\n== %s, NQ, 2 micros per trade (day's trades merged, worst points together)\n", pname);
    for (auto& c : cfgs) {
      std::vector<std::vector<payout::Trade>> st(D); std::vector<const std::vector<payout::Trade>*> cal(D, nullptr); long ntr = 0; int tdays = 0, wdays = 0; double tot = 0;
      for (int i = 0; i < D; ++i) { const int d = dates[i]; std::vector<const TR*> v;
        if (c.c) { auto it = cas.find(d); if (it != cas.end() && (!c.meta || it->second.meta)) v.push_back(&it->second); }
        if (c.g) { auto it = gor.find(d); if (it != gor.end()) v.push_back(&it->second); }
        if (c.b) { auto it = box.find(d); if (it != box.end()) v.push_back(&it->second); }
        std::sort(v.begin(), v.end(), [](const TR* a, const TR* b) { return a->t < b->t; });
        int dir = 0, taken = 0; double p = 0, ma = 0, mf = 0; std::vector<const TR*> done;
        for (auto* x : v) { if (dir && x->dir != dir) continue; if (taken >= c.cap) break;
          if (c.stop_after_loss) { bool lost = false; for (auto* y : done) if (y->tx <= x->t && y->p < 0) lost = true; if (lost) break; }   // only a loss already CLOSED counts
          dir = x->dir; ++taken; done.push_back(x); p += x->p * 2; ma += x->mae * 2; mf += x->mfe * 2; }
        if (taken) { st[i] = {{p, std::min(ma, p), mf}}; cal[i] = &st[i]; ntr += taken; ++tdays; wdays += p > 0; tot += p; } }
      int starts = 0, pass = 0, blown = 0; double paid = 0; int evs = 0; std::vector<double> waits;
      for (int s0 = 0; s0 + 252 < D; ++s0) { ++starts; auto e = payout::run_eval(*pl, cal, s0, 120); pass += e.passed; blown += e.failed;
        int i = s0, first = -1; while (i < s0 + 252) { ++evs; auto e2 = payout::run_eval(*pl, cal, i, s0 + 252 - i); i += std::max(1, e2.days); if (!e2.passed) continue;
          auto r = payout::run_account(*pl, cal, i, s0 + 252 - i); paid += r.paid_sum; if (first < 0 && !r.pay_days.empty()) first = i + r.pay_days[0] - 1 - s0; i += std::max(1, r.days); if (!r.breach) break; }
        if (first >= 0) waits.push_back(first); }
      std::sort(waits.begin(), waits.end());
      printf("  %-50s trades/day %.2f · trade days %4.1f%% (win %4.1f%%) · $%6.0f/yr · eval pass %4.1f%% blown %4.1f%% · 1st payout %4.1f wks · paid/yr $%5.0f · evals/yr %4.1f\n",
             c.name, (double)ntr / D, 100.0 * tdays / D, 100.0 * wdays / std::max(1, tdays), tot / (D / 252.0), 100.0 * pass / starts, 100.0 * blown / starts,
             waits.empty() ? NAN : waits[waits.size() / 2] / 5, paid / starts, (double)evs / starts); } }
}
