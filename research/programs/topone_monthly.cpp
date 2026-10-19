// 2026-10-19 (founder: "5 accounts a month at ~$70-100 each … pass at least 2 … then 5 winning days … lets make this work"): the founder's
// buying plan with the REAL plan trades on TopOne 50K. Every month 5 evals are bought on staggered days (trading days 1, 5, 9, 13, 17 of
// the month); each runs the pasted eval (+$3,000 target, $2,000 EOD trailing, 30 calendar days ≈ 21 trading days, no daily limit, no
// consistency); a pass goes funded (published Elite Access rules: EOD trailing locks at +$100 once +$2,100, 5 days ≥ $250, balance
// ≥ +$2,500, $500–1,500 a request, 90 %; own $500 daily stop). No replacement inside the month — a blown account is just done.
// Trades: book F + weakness long + 5m flag with the day (inputs_20261019 *_k.csv), sized to $risk at each stop, cost 2 NQ pts at today's
// cost-to-range. Per month of purchases: passes out of 5, accounts with a payout within 3 months, payouts within 6 months, net after
// $85 per eval and $189 per activation. argv: inputs dir.
#include <cstdio>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <random>
struct T { long e; double g, stop, A; };
std::map<int, double> atr; std::vector<int> ses; std::map<int, std::vector<T>> src[3]; static double COSTPTS = 2.0, SLIP = 0; static bool WHOLE = false;   // argv[2] cost pts, argv[3] extra pts on stopped trades, argv[4] 1 = whole micros
double dayPL(int d, double risk, double dll) { std::vector<T> v; for (auto& m : src) if (m.count(d)) for (auto& t : m.at(d)) v.push_back(t);
  std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.e < b.e; }); double pl = 0;
  for (auto& t : v) { if (pl <= -dll) break; double c = std::min(30.0, risk / (t.stop * 2.0)); if (WHOLE) c = std::max(1.0, std::floor(c)); double g = t.g; if (g <= -t.stop + 0.01) g -= SLIP; pl += c * 2.0 * (g - COSTPTS * t.A / 320.0); } return pl; }
struct Out { bool pass; int passDay; double paid3, paid6; bool payout3; };
Out account(size_t s0, double er, double fr) {
  Out o{false, -1, 0, 0, false}; double bal = 0, peak = 0; size_t i = s0;
  for (; i < ses.size() && i < s0 + 21; ++i) { bal += dayPL(ses[i], er, 1e9); peak = std::max(peak, bal); if (bal >= 3000) { o.pass = true; o.passDay = i - s0 + 1; ++i; break; } if (bal <= peak - 2000) return o; }
  if (!o.pass) return o;
  bal = 0; peak = 0; double floor = -2000; bool locked = false; int pd = 0; const size_t f0 = i;
  for (; i < ses.size() && i < f0 + 126; ++i) { double pl = dayPL(ses[i], fr, 500); bal += pl; if (pl >= 250) pd++;
    if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (peak >= 2100) { locked = true; floor = 100; } }
    if (bal <= floor) break;
    if (pd >= 5 && bal >= 2500) { double w = std::min(1500.0, bal - 2000); if (w >= 500) { double p = 0.9 * w; o.paid6 += p; if (i < f0 + 63) { o.paid3 += p; o.payout3 = true; } bal -= w; locked = true; floor = 100; pd = 0; } } }
  return o; }
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "research/programs/inputs_20261019"; if (argc > 2) COSTPTS = atof(argv[2]); if (argc > 3) SLIP = atof(argv[3]); if (argc > 4) WHOLE = atoi(argv[4]) != 0;
  printf("cost %.0f pts, stops %.0f pts worse, %s micros\n", COSTPTS, SLIP, WHOLE ? "whole" : "fractional");
  { std::ifstream f(dir + "/nq_atr_edge_lab.csv"); std::string l; while (std::getline(f, l)) { int d; double a; if (sscanf(l.c_str(), "%d,%lf", &d, &a) == 2) atr[d] = a; } }
  { std::ifstream f(dir + "/sessions_all.csv"); std::string l; while (std::getline(f, l)) { int d; if (sscanf(l.c_str(), "%d", &d) == 1 && atr.count(d)) ses.push_back(d); } }
  const char* F[3] = {"book_F_k.csv", "weakness_long_1p5atr_k.csv", "flag5_k.csv"};
  for (int k = 0; k < 3; ++k) { std::ifstream f(dir + "/" + F[k]); std::string l; while (std::getline(f, l)) { int d; long e, x; int di; double g, n, mae, mfe, sr, kk;
      if (sscanf(l.c_str(), "%d,%ld,%ld,%d,%lf,%lf,%lf,%lf,%lf,%lf", &d, &e, &x, &di, &g, &n, &mae, &mfe, &sr, &kk) != 10 || !atr.count(d)) continue; src[k][d].push_back({e, g, kk * atr[d], atr[d]}); } }
  printf("TopOne 50K, 5 evals bought each month (trading days 1/5/9/13/17), real plan trades. Per month of purchases, months 2010-08 → 2025-04.\n\n");
  printf("  eval$ fund$ | passes of 5: mean  0   1   >=2  >=3 (%% of months) | funded w/ payout in 3 mo | payouts 3 mo  6 mo | net 6 mo after fees: mean  median  losing%%  | net 2010-18 / 2019-22 / 2023-25\n");
  for (double er : {500.0, 750.0, 1000.0, 1500.0}) for (double fr : {500.0, 750.0, 1000.0}) {
    long months = 0, h[6] = {0}; double sp = 0, sp3 = 0, sp6 = 0, spay = 0; std::vector<double> nets; double es[3] = {0}; long en[3] = {0};
    for (size_t s = 1; s < ses.size(); ++s) { if (ses[s] / 100 == ses[s - 1] / 100) continue; if (ses[s] < 20100801 || ses[s] > 20250430) continue;
      int passes = 0, pay = 0; double p3 = 0, p6 = 0, fees = 0;
      for (int off : {0, 4, 8, 12, 16}) { if (s + off >= ses.size()) break; Out o = account(s + off, er, fr); fees += 85; if (o.pass) { passes++; fees += 189; } if (o.payout3) pay++; p3 += o.paid3; p6 += o.paid6; }
      months++; h[passes]++; sp += passes; spay += pay; sp3 += p3; sp6 += p6; double net = p6 - fees; nets.push_back(net); int e = ses[s] < 20190101 ? 0 : ses[s] < 20230101 ? 1 : 2; es[e] += net; en[e]++; }
    std::sort(nets.begin(), nets.end()); double sm = 0; long lo = 0; for (double x : nets) { sm += x; if (x < 0) lo++; }
    printf("  %5.0f %5.0f | %4.2f  %4.1f %4.1f %4.1f %4.1f            | %4.2f                     | %+6.0f %+6.0f | %+7.0f %+7.0f %6.1f   | %+6.0f %+6.0f %+6.0f\n", er, fr, sp / months, 100.0 * h[0] / months, 100.0 * h[1] / months,
           100.0 * (months - h[0] - h[1]) / months, 100.0 * (h[3] + h[4] + h[5]) / months, spay / months, sp3 / months, sp6 / months, sm / months, nets[nets.size() / 2], 100.0 * lo / months, es[0] / en[0], es[1] / en[1], es[2] / en[2]); }
  // ---- continuous operation: each month buy up to 5 evals (days 1/5/9/13/17) while active accounts (evals + funded) < CAP; accounts live
  // until they blow (no time limit when funded); calendar-year cash flow.
  printf("\n\n######## CONTINUOUS OPERATION — buy up to 5 evals a month while active accounts < cap; eval $85, activation $189; per calendar year\n");
  printf("  rule      cap  eval$ fund$ | net / yr: mean  median  worst   losing yrs | evals/yr  passes/yr  payouts/yr | max cash out | 2011-18 / 2019-22 / 2023-25 mean\n");
  struct Ac { bool funded; double bal, peak, floor; bool locked; int pd, age; double cyc, big; };
  printf("  (cons = funded consistency rule: largest day <= that share of the cycle's profit; the pasted payout rules list none, the published Elite Access rules say 40 %%)\n");
  for (double cons : {0.0, 0.4}) for (int cap : {5, 10, 20}) for (double er : {1000.0}) for (double fr : {500.0, 750.0}) {
    std::map<int, double> yr; std::map<int, double> ev, ps, pay; std::vector<Ac> ac; double cash = 0, worst = 0;
    for (size_t i = 0; i < ses.size(); ++i) { int d = ses[i]; if (d < 20110101 || d > 20251231) continue; int y = d / 10000;
      int dom = 0; for (size_t j = i; j > 0 && ses[j - 1] / 100 == d / 100; --j) dom++;
      if (dom == 0 || dom == 4 || dom == 8 || dom == 12 || dom == 16) if ((int)ac.size() < cap) { ac.push_back({false, 0, 0, -2000, false, 0, 0, 0, 0}); cash -= 85; yr[y] -= 85; ev[y]++; }
      double plE = dayPL(d, er, 1e9), plF = dayPL(d, fr, 500);
      for (auto& a : ac) { a.age++;
        if (!a.funded) { a.bal += plE; a.peak = std::max(a.peak, a.bal);
          if (a.bal >= 3000) { a = {true, 0, 0, -2000, false, 0, 0, 0, 0}; cash -= 189; yr[y] -= 189; ps[y]++; continue; }
          if (a.bal <= a.peak - 2000 || a.age > 21) a.age = -1; continue; }
        a.bal += plF; a.cyc += plF; a.big = std::max(a.big, plF); if (plF >= 250) a.pd++;
        if (!a.locked) { a.peak = std::max(a.peak, a.bal); a.floor = a.peak - 2000; if (a.peak >= 2100) { a.locked = true; a.floor = 100; } }
        if (a.bal <= a.floor) { a.age = -1; continue; }
        if (a.pd >= 5 && a.bal >= 2500 && (cons <= 0 || (a.cyc > 0 && a.big <= cons * a.cyc))) { double w = std::min(1500.0, a.bal - 2000); if (w >= 500) { cash += 0.9 * w; yr[y] += 0.9 * w; pay[y]++; a.bal -= w; a.locked = true; a.floor = 100; a.pd = 0; a.cyc = 0; a.big = 0; } } }
      ac.erase(std::remove_if(ac.begin(), ac.end(), [](const Ac& a) { return a.age < 0; }), ac.end()); worst = std::min(worst, cash); }
    std::vector<double> v; double s = 0, E[3] = {0}; int N[3] = {0}, lose = 0; double tev = 0, tps = 0, tpay = 0;
    for (auto& [y, x] : yr) { v.push_back(x); s += x; if (x < 0) lose++; int e = y < 2019 ? 0 : y < 2023 ? 1 : 2; E[e] += x; N[e]++; tev += ev[y]; tps += ps[y]; tpay += pay[y]; }
    std::vector<double> so = v; std::sort(so.begin(), so.end());
    printf("  cons %2.0f%%  %3d  %5.0f %5.0f | %+8.0f %+8.0f %+7.0f   %2d of %2zu  | %6.1f  %8.1f  %9.1f | %+8.0f    | %+7.0f / %+7.0f / %+7.0f\n", 100 * cons, cap, er, fr, s / v.size(), so[v.size() / 2], so[0], lose, v.size(),
           tev / v.size(), tps / v.size(), tpay / v.size(), worst, E[0] / N[0], E[1] / N[1], E[2] / N[2]);
    if (cap == 10 && fr == 750) { printf("      years:"); for (auto& [y, x] : yr) printf(" %d %+.0fk", y, x / 1000); printf("\n"); } }
  // ---- stress of the continuous operation (cap 10, eval $1,000, funded $750, own $500 daily stop): payout limits, 2025-26 alone, and a
  // day-resampling bootstrap of 2019-26. Purchase days = every 21-trading-day block, offsets 0/4/8/12/16.
  struct OP { double cons = 0.4; double gapDays = 0; double lifeCap = 1e18; double monthCap = 1e18; int maxPay = 1 << 30; };
  auto runOp = [&](const std::vector<int>& days, const OP& o) {
    struct A2 { bool funded; double bal, peak, floor; bool locked; int pd, age; double cyc, big, paidLife; int lastPay, nPay; std::map<int, double> mon; };
    std::vector<A2> ac; double cash = 0;
    for (size_t k = 0; k < days.size(); ++k) { int d = days[k]; int off = k % 21;
      if ((off == 0 || off == 4 || off == 8 || off == 12 || off == 16) && ac.size() < 10) { ac.push_back({false, 0, 0, -2000, false, 0, 0, 0, 0, 0, -999, 0, {}}); cash -= 85; }
      double plE = dayPL(d, 1000, 1e9), plF = dayPL(d, 750, 500);
      for (auto& a : ac) { a.age++;
        if (!a.funded) { a.bal += plE; a.peak = std::max(a.peak, a.bal); if (a.bal >= 3000) { a.funded = true; a.bal = 0; a.peak = 0; a.floor = -2000; a.locked = false; a.pd = 0; a.cyc = 0; a.big = 0; cash -= 189; continue; }
          if (a.bal <= a.peak - 2000 || a.age > 21) a.age = -1; continue; }
        a.bal += plF; a.cyc += plF; a.big = std::max(a.big, plF); if (plF >= 250) a.pd++;
        if (!a.locked) { a.peak = std::max(a.peak, a.bal); a.floor = a.peak - 2000; if (a.peak >= 2100) { a.locked = true; a.floor = 100; } }
        if (a.bal <= a.floor) { a.age = -1; continue; }
        int mkey = (int)(k / 21);
        if (a.pd >= 5 && a.bal >= 2500 && (o.cons <= 0 || (a.cyc > 0 && a.big <= o.cons * a.cyc)) && (int)k - a.lastPay >= o.gapDays && a.nPay < o.maxPay) {
          double w = std::min({1500.0, a.bal - 2000, o.lifeCap - a.paidLife, o.monthCap - a.mon[mkey]}); if (w >= 500) { cash += 0.9 * w; a.paidLife += w; a.mon[mkey] += w; a.bal -= w; a.locked = true; a.floor = 100; a.pd = 0; a.cyc = 0; a.big = 0; a.lastPay = k; a.nPay++;
            if (a.paidLife >= o.lifeCap - 1 || a.nPay >= o.maxPay) a.age = -1; } } }   // account closed at a lifetime cap (as if moved to live)
      ac.erase(std::remove_if(ac.begin(), ac.end(), [](const A2& a) { return a.age < 0; }), ac.end()); }
    return cash; };
  std::vector<int> all; for (int d : ses) if (d >= 20110101 && d <= 20251231) all.push_back(d);
  const double YRS = all.size() / 252.0;
  printf("\n\n######## OPERATION STRESS — 10 accounts, eval $1,000 / funded $750, own $500 daily stop, 2011-2025 (%.1f yrs), $ per year\n", YRS);
  struct V { const char* n; OP o; }; std::vector<V> vs;
  { OP o; vs.push_back({"base: 40 %% consistency, payout any time", o}); }
  { OP o; o.cons = 0; vs.push_back({"no consistency", o}); }
  { OP o; o.gapDays = 5; vs.push_back({"one payout a week per account", o}); }
  { OP o; o.gapDays = 10; vs.push_back({"one payout every 2 weeks", o}); }
  { OP o; o.gapDays = 21; vs.push_back({"one payout a month", o}); }
  { OP o; o.monthCap = 3000; vs.push_back({"max $3,000 paid a month per account", o}); }
  { OP o; o.lifeCap = 6000; vs.push_back({"account closed after $6,000 lifetime payouts", o}); }
  { OP o; o.lifeCap = 12000; vs.push_back({"account closed after $12,000 lifetime payouts", o}); }
  { OP o; o.maxPay = 4; vs.push_back({"account closed after 4 payouts", o}); }
  { OP o; o.gapDays = 10; o.lifeCap = 6000; vs.push_back({"every 2 weeks + closed after $6,000", o}); }
  for (auto& v : vs) printf("  %-48s %+9.0f\n", v.n, runOp(all, v.o) / YRS);
  std::vector<int> h; for (int d : ses) if (d >= 20250101) h.push_back(d);
  printf("\n  2025-01 → 2026-10-16 only (%zu trading days, data gap 2026-03-20 → 09-18):\n", h.size());
  for (auto& v : vs) printf("  %-48s %+9.0f total\n", v.n, runOp(h, v.o));
  std::vector<int> pool; for (int d : ses) if (d >= 20190101) pool.push_back(d);
  std::mt19937 rng(41); std::uniform_int_distribution<size_t> U(0, pool.size() - 1);
  printf("\n  bootstrap — 2,000 synthetic years of days drawn from 2019-2026 ($ per year):\n");
  for (int vi : {0, 1, 3, 9}) { std::vector<double> r; for (int y = 0; y < 2000; ++y) { std::vector<int> days(252); for (auto& d : days) d = pool[U(rng)]; r.push_back(runOp(days, vs[vi].o)); }
    std::sort(r.begin(), r.end()); int lo = 0; for (double x : r) if (x < 0) lo++;
    printf("  %-48s P5 %+8.0f  P25 %+8.0f  median %+8.0f  P75 %+8.0f  losing %4.1f %%\n", vs[vi].n, r[100], r[500], r[1000], r[1500], 100.0 * lo / r.size()); }
  // ---- operating-parameter optimisation, chosen on 2011-2018 ONLY, read on 2019-2025, 2025-26 and the bootstrap. 40 % consistency.
  // Grid: eval risk x funded risk x own daily stop (funded) x evals bought per 21-day block, cap 10 accounts.
  auto runP = [&](const std::vector<int>& days, double er, double fr, double dll, int buys) {
    struct A3 { bool funded; double bal, peak, floor; bool locked; int pd, age; double cyc, big; }; std::vector<A3> ac; double cash = 0;
    const int OFF[8][8] = {{0}, {0, 10}, {0, 7, 14}, {0, 5, 10, 15}, {0, 4, 8, 12, 16}, {0, 3, 7, 10, 14, 17}, {0, 3, 6, 9, 12, 15, 18}, {0, 2, 5, 7, 10, 12, 15, 18}};
    for (size_t k = 0; k < days.size(); ++k) { int d = days[k], off = k % 21; bool buy = false; for (int j = 0; j < buys; ++j) if (OFF[buys - 1][j] == off) buy = true;
      if (buy && ac.size() < 10) { ac.push_back({false, 0, 0, -2000, false, 0, 0, 0, 0}); cash -= 85; }
      double plE = dayPL(d, er, 1e9), plF = dayPL(d, fr, dll);
      for (auto& a : ac) { a.age++;
        if (!a.funded) { a.bal += plE; a.peak = std::max(a.peak, a.bal); if (a.bal >= 3000) { a = {true, 0, 0, -2000, false, 0, 0, 0, 0}; cash -= 189; continue; } if (a.bal <= a.peak - 2000 || a.age > 21) a.age = -1; continue; }
        a.bal += plF; a.cyc += plF; a.big = std::max(a.big, plF); if (plF >= 250) a.pd++;
        if (!a.locked) { a.peak = std::max(a.peak, a.bal); a.floor = a.peak - 2000; if (a.peak >= 2100) { a.locked = true; a.floor = 100; } }
        if (a.bal <= a.floor) { a.age = -1; continue; }
        if (a.pd >= 5 && a.bal >= 2500 && a.cyc > 0 && a.big <= 0.4 * a.cyc) { double w = std::min(1500.0, a.bal - 2000); if (w >= 500) { cash += 0.9 * w; a.bal -= w; a.locked = true; a.floor = 100; a.pd = 0; a.cyc = 0; a.big = 0; } } }
      ac.erase(std::remove_if(ac.begin(), ac.end(), [](const A3& a) { return a.age < 0; }), ac.end()); }
    return cash; };
  std::vector<int> IS, OOS, H; for (int d : ses) { if (d >= 20110101 && d < 20190101) IS.push_back(d); else if (d >= 20190101 && d <= 20251231) OOS.push_back(d); if (d >= 20250101) H.push_back(d); }
  struct G { double er, fr, dll; int buys; double is, oos, h; }; std::vector<G> grid;
  for (double er : {500.0, 750.0, 1000.0, 1500.0, 2000.0}) for (double fr : {250.0, 500.0, 750.0, 1000.0, 1250.0}) for (double dll : {250.0, 500.0, 750.0, 1e9}) for (int buys : {2, 3, 5, 8}) {
    G g{er, fr, dll, buys, runP(IS, er, fr, dll, buys) / (IS.size() / 252.0), runP(OOS, er, fr, dll, buys) / (OOS.size() / 252.0), runP(H, er, fr, dll, buys)}; grid.push_back(g); }
  std::sort(grid.begin(), grid.end(), [](const G& a, const G& b) { return a.is > b.is; });
  printf("\n\n######## OPTIMISATION — %zu settings, ranked on 2011-2018 only (40 %% consistency, cap 10). $ per year (2025-26 = total)\n", grid.size());
  printf("  eval$ fund$ own-stop buys/mo | 2011-18 (chosen on) | 2019-25 (unseen) | 2025-26 total | bootstrap 2019-26: P5  median  losing%%\n");
  auto boot = [&](const G& g, double& p5, double& med, double& lo) { std::mt19937 r2(43); std::uniform_int_distribution<size_t> U2(0, pool.size() - 1); std::vector<double> r;
    for (int y = 0; y < 500; ++y) { std::vector<int> days(252); for (auto& d : days) d = pool[U2(r2)]; r.push_back(runP(days, g.er, g.fr, g.dll, g.buys)); }
    std::sort(r.begin(), r.end()); p5 = r[25]; med = r[250]; int l = 0; for (double x : r) if (x < 0) l++; lo = 100.0 * l / r.size(); };
  auto line = [&](const G& g) { double p5, med, lo; boot(g, p5, med, lo);
    printf("  %5.0f %5.0f %8s %4d     | %+10.0f          | %+10.0f       | %+10.0f    | %+8.0f %+8.0f %5.1f\n", g.er, g.fr, g.dll > 1e8 ? "none" : (std::string("$") + std::to_string((int)g.dll)).c_str(), g.buys, g.is, g.oos, g.h, p5, med, lo); };
  for (int i = 0; i < 10; ++i) line(grid[i]);
  printf("  ... current plan (eval $1,000, funded $750, own stop $500, 5 a month):\n");
  for (auto& g : grid) if (g.er == 1000 && g.fr == 750 && g.dll == 500 && g.buys == 5) line(g);
  { std::vector<double> o; for (auto& g : grid) o.push_back(g.oos); std::vector<double> top; for (int i = 0; i < 20; ++i) top.push_back(grid[i].oos); std::sort(o.begin(), o.end()); std::sort(top.begin(), top.end());
    printf("  2019-25 per year: median of all %zu settings %+.0f, median of the top-20 picked on 2011-18 %+.0f\n", grid.size(), o[o.size() / 2], top[10]); }
  return 0;
}
