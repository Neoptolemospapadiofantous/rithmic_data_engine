// 2026-10-19 (founder: "more tests" after topone_sim): stress the TopOne 50K result for the plan's day trades (book F + weakness long +
// 5m flag with the day, eval $1,000 / funded $1,000 risk) against the funded rules we could NOT confirm and against execution:
// drawdown lock, consistency %, payout cap, split, profitable-day count, daily pause, whole micros, stop slippage, cost; then calendar
// years, a day-resampling bootstrap (5,000 years), and cash needed up front. Same chain model as topone_sim (one copy-traded account).
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <random>

struct T { long e; double g, stop, A; };
struct P { const char* name; double lockAt = 2100, cons = 0.40, cap = 1500, split = 0.9, dll = 1000, cost = 2, slip = 0; int pdays = 5; bool whole = false, lock = true; };
std::map<int, double> atr; std::vector<int> ses; std::map<int, std::vector<T>> src[3];

double dayPL(int d, double risk, const P& p) { std::vector<T> v; for (auto& m : src) if (m.count(d)) for (auto& t : m.at(d)) v.push_back(t);
  std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.e < b.e; }); double pl = 0;
  for (auto& t : v) { if (pl <= -p.dll) break; double c = std::min(30.0, risk / (t.stop * 2.0)); if (p.whole) { c = std::floor(c); if (c < 1) c = 1; }
    double g = t.g; if (g <= -t.stop + 0.01) g -= p.slip;   // stopped out: extra slippage
    pl += c * 2.0 * (g - p.cost * t.A / 320.0); } return pl; }

struct Res { double net, worstCash; int firstPay; };
Res chain(const std::vector<int>& days, const P& p, double er = 1000, double fr = 1000) {
  double cash = 0, worst = 0, paid = 0; bool funded = false, first = true, locked = false; double bal = 0, peak = 0, floor = -2000, cyc = 0, big = 0; int pd = 0, firstPay = -1;
  auto newEval = [&]() { cash -= first ? 39 : 35; first = false; funded = false; bal = 0; peak = 0; };
  newEval();
  for (size_t i = 0; i < days.size(); ++i) { int d = days[i];
    if (!funded) { P pe = p; pe.dll = 1e9; bal += dayPL(d, er, pe);   // no daily limit on the eval peak = std::max(peak, bal);
      if (bal >= 3000) { funded = true; cash -= 189; bal = 0; peak = 0; floor = -2000; locked = false; pd = 0; cyc = 0; big = 0; } else if (bal <= peak - 2000) newEval(); }
    else { double pl = dayPL(d, fr, p); bal += pl; cyc += pl; if (pl >= 250) pd++; big = std::max(big, pl);
      if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (p.lock && peak >= p.lockAt) { locked = true; floor = 100; } }
      if (bal <= floor) newEval();
      else if (pd >= p.pdays && bal >= 2500 && cyc > 0 && (p.cons <= 0 || big <= p.cons * cyc)) { double w = std::min(p.cap, bal - 2000);
        if (w >= 500) { paid += p.split * w; cash += p.split * w; bal -= w; if (p.lock) { locked = true; floor = 100; } pd = 0; cyc = 0; big = 0; if (firstPay < 0) firstPay = i; } } }
    worst = std::min(worst, cash); }
  return {cash, worst, firstPay}; }

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "research/programs/inputs_20261019";
  { std::ifstream f(dir + "/nq_atr_edge_lab.csv"); std::string l; while (std::getline(f, l)) { int d; double a; if (sscanf(l.c_str(), "%d,%lf", &d, &a) == 2) atr[d] = a; } }
  { std::ifstream f(dir + "/sessions_all.csv"); std::string l; while (std::getline(f, l)) { int d; if (sscanf(l.c_str(), "%d", &d) == 1 && atr.count(d)) ses.push_back(d); } }
  const char* F[3] = {"book_F_k.csv", "weakness_long_1p5atr_k.csv", "flag5_k.csv"};
  for (int k = 0; k < 3; ++k) { std::ifstream f(dir + "/" + F[k]); std::string l; while (std::getline(f, l)) { int d; long e, x; int di; double g, n, mae, mfe, sr, kk;
      if (sscanf(l.c_str(), "%d,%ld,%ld,%d,%lf,%lf,%lf,%lf,%lf,%lf", &d, &e, &x, &di, &g, &n, &mae, &mfe, &sr, &kk) != 10 || !atr.count(d)) continue; src[k][d].push_back({e, g, kk * atr[d], atr[d]}); } }
  // 12-month windows started each month
  std::vector<std::vector<int>> W; std::vector<int> W0;
  for (size_t s = 0; s < ses.size(); ++s) { if (s > 0 && ses[s] / 100 == ses[s - 1] / 100) continue; if (ses[s] > 20251016) break; size_t e = s; while (e < ses.size() && ses[e] < ses[s] + 10000) ++e; if (e >= ses.size()) break;
    W.emplace_back(ses.begin() + s, ses.begin() + e); W0.push_back(ses[s]); }
  std::vector<P> V; P base{"base (published Elite Access rules)"}; V.push_back(base);
  { P p = base; p.name = "drawdown never locks (trails forever)"; p.lock = false; V.push_back(p); }
  { P p = base; p.name = "lock only at +$3,000"; p.lockAt = 3000; V.push_back(p); }
  { P p = base; p.name = "consistency 30 %"; p.cons = 0.30; V.push_back(p); }
  { P p = base; p.name = "consistency 20 %"; p.cons = 0.20; V.push_back(p); }
  { P p = base; p.name = "no consistency rule"; p.cons = 0; V.push_back(p); }
  { P p = base; p.name = "payout cap $1,000"; p.cap = 1000; V.push_back(p); }
  { P p = base; p.name = "payout cap $2,500"; p.cap = 2500; V.push_back(p); }
  { P p = base; p.name = "split 80 % (live account)"; p.split = 0.8; V.push_back(p); }
  { P p = base; p.name = "10 profitable days per payout"; p.pdays = 10; V.push_back(p); }
  { P p = base; p.name = "daily pause at $500"; p.dll = 500; V.push_back(p); }
  { P p = base; p.name = "whole micros only (floor, min 1)"; p.whole = true; V.push_back(p); }
  { P p = base; p.name = "every stop fills 4 pts worse"; p.slip = 4; V.push_back(p); }
  { P p = base; p.name = "cost 6 pts + stops 4 pts worse"; p.cost = 6; p.slip = 4; V.push_back(p); }
  { P p = base; p.name = "worst case: no lock, cons 20 %, cap $1k, 80 %, cost 6, slip 4"; p.lock = false; p.cons = 0.2; p.cap = 1000; p.split = 0.8; p.cost = 6; p.slip = 4; V.push_back(p); }
  printf("TopOne 50K stress — book F + weakness long + 5m flag with the day, eval $1,000 / funded $1,000 risk, one copy-traded account chain,\n%zu twelve-month windows started each month %d → %d.\n\n", W.size(), W0.front(), W0.back());
  printf("  %-62s %8s %8s %8s %8s %7s %10s %11s\n", "variant", "mean $/yr", "median", "P10", "worst", "losing", "1st pay d", "cash needed");
  for (auto& p : V) { std::vector<double> n; std::vector<int> fp; double wc = 0;
    for (auto& w : W) { Res r = chain(w, p); n.push_back(r.net); if (r.firstPay >= 0) fp.push_back(r.firstPay); wc = std::min(wc, r.worstCash); }
    std::vector<double> so = n; std::sort(so.begin(), so.end()); std::sort(fp.begin(), fp.end()); double m = 0; int lo = 0; for (double x : n) { m += x; if (x < 0) lo++; }
    printf("  %-62s %+8.0f %+8.0f %+8.0f %+8.0f %6.1f%% %10d %+11.0f\n", p.name, m / n.size(), so[n.size() / 2], so[n.size() / 10], so[0], 100.0 * lo / n.size(), fp.empty() ? -1 : fp[fp.size() / 2], wc); }
  printf("\n  calendar years (base, start in January):\n");
  for (size_t i = 0; i < W.size(); ++i) if (W0[i] % 10000 / 100 == 1) { Res r = chain(W[i], base); printf("    %d  %+7.0f  (cash low %+5.0f, first payout day %d)\n", W0[i] / 10000, r.net, r.worstCash, r.firstPay); }
  { std::vector<int> d26; for (int d : ses) if (d >= 20260101) d26.push_back(d); Res r = chain(d26, base); printf("    2026 YTD (to 10-16, no data 03-20 → 09-18)  %+7.0f\n", r.net); }
  // bootstrap: 252 days drawn with replacement from 2019 → 2026 (today's market structure), 5,000 years
  std::vector<int> pool; for (int d : ses) if (d >= 20190101) pool.push_back(d);
  std::mt19937 rng(17); std::uniform_int_distribution<size_t> U(0, pool.size() - 1);
  for (auto* pp : {&V[0], &V.back()}) { std::vector<double> n; for (int y = 0; y < 5000; ++y) { std::vector<int> days(252); for (auto& d : days) d = pool[U(rng)]; n.push_back(chain(days, *pp).net); }
    std::sort(n.begin(), n.end()); int lo = 0; for (double x : n) if (x < 0) lo++;
    printf("\n  bootstrap 5,000 years of days drawn from 2019-26 — %s:\n    P5 %+.0f  P25 %+.0f  median %+.0f  P75 %+.0f  P95 %+.0f  | losing %.1f %%\n", pp->name, n[250], n[1250], n[2500], n[3750], n[4750], 100.0 * lo / 5000); }
  return 0;
}
