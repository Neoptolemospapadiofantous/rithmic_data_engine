// 2026-10-19 (founder: "we want to see how long it would take to pass the account. maybe size up"): TopOne 50K evaluation as pasted —
// +$3,000 target, $2,000 end-of-day trailing drawdown, 30 calendar days (21 trading days), no daily limit, max 3 minis (= 30 micros) — run
// from every trading day 2010-07 → 2026-09 with each trade sized to $risk at its stop (micro NQ, $2 a point, capped at 30 micros = 3
// minis), cost 2 NQ pts at today's cost-to-range. Streams (inputs_20261019 *_k.csv): the 5m trailing flag alone, book F + weakness long,
// and both. Per stream and risk: pass %, fail %, out of time %, median and 75th-percentile trading days to pass, pass % by era, and the
// eval cost per pass at $85 an attempt. argv: inputs dir.
#include <cstdio>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
struct T { long e; double g, stop, A; };
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "research/programs/inputs_20261019";
  std::map<int, double> atr; std::vector<int> ses;
  { std::ifstream f(dir + "/nq_atr_edge_lab.csv"); std::string l; while (std::getline(f, l)) { int d; double a; if (sscanf(l.c_str(), "%d,%lf", &d, &a) == 2) atr[d] = a; } }
  { std::ifstream f(dir + "/sessions_all.csv"); std::string l; while (std::getline(f, l)) { int d; if (sscanf(l.c_str(), "%d", &d) == 1 && atr.count(d) && d <= 20261016) ses.push_back(d); } }
  const char* F[3] = {"flagtrail5_k.csv", "book_F_k.csv", "weakness_long_1p5atr_k.csv"}; std::map<int, std::vector<T>> src[3];
  for (int k = 0; k < 3; ++k) { std::ifstream f(dir + "/" + F[k]); std::string l; while (std::getline(f, l)) { int d; long e, x; int di; double g, n, mae, mfe, sr, kk;
      if (sscanf(l.c_str(), "%d,%ld,%ld,%d,%lf,%lf,%lf,%lf,%lf,%lf", &d, &e, &x, &di, &g, &n, &mae, &mfe, &sr, &kk) != 10 || !atr.count(d)) continue; src[k][d].push_back({e, g, kk * atr[d], atr[d]}); } }
  auto dayPL = [&](int d, double risk, int mask) { std::vector<T> v; for (int k = 0; k < 3; ++k) if ((mask >> k & 1) && src[k].count(d)) for (auto& t : src[k][d]) v.push_back(t);
    double pl = 0; for (auto& t : v) { double c = std::min(30.0, risk / (t.stop * 2.0)); pl += c * 2.0 * (t.g - 2.0 * t.A / 320.0); } return pl; };
  struct SM { const char* n; int mask; }; SM sm[3] = {{"5m trailing flag alone", 1}, {"book F + weakness long", 6}, {"book F + weakness long + 5m trailing flag", 7}};
  printf("TopOne 50K evaluation: +$3,000 target, $2,000 EOD trailing drawdown, 21 trading days, max 3 minis. Evals started on every day %d → %d.\n", ses.front(), ses.back());
  for (auto& st : sm) { printf("\n== %s\n  risk/trade | pass %%  fail %%  out of time %% | days to pass: median  75th pct | pass %% 2010-18 / 2019-24 / 2025-26 | eval cost per pass ($85 each)\n", st.n);
    for (double risk : {500.0, 750.0, 1000.0, 1500.0, 2000.0, 3000.0}) { long pass = 0, fail = 0, tot = 0, pe[3] = {0}, te[3] = {0}; std::vector<int> dd;
      for (size_t s0 = 0; s0 + 21 < ses.size(); ++s0) { double bal = 0, peak = 0; int res = 0, k;
        for (k = 0; k < 21 && !res; ++k) { bal += dayPL(ses[s0 + k], risk, st.mask); peak = std::max(peak, bal); if (bal >= 3000) res = 1; else if (bal <= peak - 2000) res = -1; }
        int e = ses[s0] < 20190101 ? 0 : ses[s0] < 20250101 ? 1 : 2; tot++; te[e]++; if (res == 1) { pass++; pe[e]++; dd.push_back(k); } else if (res == -1) fail++; }
      std::sort(dd.begin(), dd.end()); double pp = (double)pass / tot;
      printf("  $%5.0f    | %5.1f  %5.1f  %5.1f          | %5d   %5d             | %5.1f / %5.1f / %5.1f              | $%.0f\n", risk, 100 * pp, 100.0 * fail / tot, 100.0 * (tot - pass - fail) / tot,
             dd.empty() ? 0 : dd[dd.size() / 2], dd.empty() ? 0 : dd[dd.size() * 3 / 4], 100.0 * pe[0] / te[0], 100.0 * pe[1] / te[1], 100.0 * pe[2] / std::max(1L, te[2]), pp > 0 ? 85 / pp : 0); } }
  // ---- then the funded stage (founder: "and then we need 5 winning days"): payout needs 5 days of >= $250 and balance >= +$2,500
  // ($2,000 buffer stays); funded drawdown $2,000 EOD trailing, locking at +$100 once the peak reaches +$2,100 (published Elite Access,
  // NOT confirmed); funded consistency off (the pasted payout rules list none) or 40 %. From every start day: eval at $risk_e, then
  // funded at $risk_f from the next day; reported: share of evals that reach a FIRST PAYOUT, trading days from buying the eval to that
  // payout (median / 75th pct), and the first payout's size.
  printf("\n\n######## FROM BUYING THE EVAL TO THE FIRST PAYOUT (book F + weakness long + 5m trailing flag)\n");
  printf("  eval risk  funded risk  consistency | reach 1st payout %% of evals | trading days to it: median  75th pct | 1st payout (after 90 %%) | funded accounts lost before paying %%\n");
  for (double cons : {0.0, 0.4}) for (double er : {1000.0, 2000.0, 3000.0}) for (double fr : {500.0, 750.0, 1000.0, 1500.0}) {
    long tot = 0, paid = 0, funded = 0, lost = 0; std::vector<int> dd; double psum = 0;
    for (size_t s0 = 0; s0 + 21 < ses.size(); ++s0) { tot++; double bal = 0, peak = 0; int res = 0; size_t k;
      for (k = 0; k < 21 && !res; ++k) { bal += dayPL(ses[s0 + k], er, 7); peak = std::max(peak, bal); if (bal >= 3000) res = 1; else if (bal <= peak - 2000) res = -1; }
      if (res != 1) continue; funded++; size_t f0 = s0 + k; bal = 0; peak = 0; double floor = -2000, cyc = 0, big = 0; bool locked = false; int pd = 0;
      for (size_t i = f0; i < ses.size() && i < f0 + 250; ++i) { double pl = dayPL(ses[i], fr, 7); bal += pl; cyc += pl; big = std::max(big, pl); if (pl >= 250) pd++;
        if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (peak >= 2100) { locked = true; floor = 100; } }
        if (bal <= floor) { lost++; break; }
        if (pd >= 5 && bal >= 2500 && (cons <= 0 || (cyc > 0 && big <= cons * cyc))) { double w = std::min(1500.0, bal - 2000); if (w >= 500) { paid++; dd.push_back(i - s0 + 1); psum += 0.9 * w; break; } } } }
    std::sort(dd.begin(), dd.end());
    printf("  $%5.0f     $%5.0f       %3.0f %%      | %5.1f %%                     | %5d   %5d                   | $%5.0f                 | %5.1f %%\n", er, fr, 100 * cons, 100.0 * paid / tot,
           dd.empty() ? 0 : dd[dd.size() / 2], dd.empty() ? 0 : dd[dd.size() * 3 / 4], paid ? psum / paid : 0, funded ? 100.0 * lost / funded : 0); }
  return 0;
}
