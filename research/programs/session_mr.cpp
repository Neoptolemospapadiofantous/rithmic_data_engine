// 2026-10-26 session_mr.cpp (founder: "NQ goes into mean reverse the whole day and then when NY comes it chooses a direction" — is
// that what the data says?). Descriptive, no trading rule. Per session window (ET): Asia 18:00-03:00 · London 03:00-09:30 ·
// NY open 09:30-11:00 · NY rest 11:00-16:00. 1m bars, 2010-07 → 2026-10-23, good days only.
//  A  VARIANCE RATIO VR(q) = Var(q-minute move) / (q × Var(1-minute move)), moves inside the window only, each in units of that day's
//     ATR. Random walk = 1.00; below 1 = mean-reverting (moves undo themselves), above 1 = trending. q = 5, 15, 30, 60.
//  B  STRAIGHTNESS = |window close − window open| / window range. A random walk gives ≈ 0.4-0.5 (simulated here per window
//     length from shuffled 1m moves of the same window — the baseline column).
//  C  BAND TEST with the 18:00-anchored VWAP (typical price × volume, VW SD): after the window's first touch of ±1 SD, does price
//     come back to VWAP before reaching ±2 SD (before the window ends)? % back to VWAP · % on to 2 SD · % neither.
//  E  (founder mid-run: "VWAP SD") per window: % of 1m closes inside ±1 SD, and % of days the window ENDS beyond ±1 SD / ±2 SD.
//  D  NY DIRECTION: does the 09:30-10:00 move keep going into 10:00-16:00 (same sign)? And is the NY day (09:30-16:00) on the same
//     side as the overnight move (18:00-09:30)?
// Eras 2010-20 | 2021-26. Usage: session_mr [NQ|ES]. tday_min = minutes since 18:00: 03:00 = 540, 09:30 = 930, 11:00 = 1020, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NW = 4, NQ = 4 };
static const char* WN[NW] = {"Asia 18:00-03:00", "London 03:00-09:30", "NY open 09:30-11:00", "NY rest 11:00-16:00"};
static const int W0[NW] = {0, 540, 930, 1020}, W1[NW] = {540, 930, 1020, 1320};
static const int QS[NQ] = {5, 15, 30, 60};
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; if (di != cur) { cur = di; pv = pq = vv = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  std::mt19937 rng(7);
  double v1[2][NW] = {}, n1[2][NW] = {}, vq[2][NW][NQ] = {}, nq[2][NW][NQ] = {};
  double st[2][NW] = {}, stb[2][NW] = {}, nst[2][NW] = {};
  double bt[2][NW][3] = {}, nbt[2][NW] = {}; double in1[2][NW] = {}, nin[2][NW] = {}, end1[2][NW] = {}, end2[2][NW] = {}, nend[2][NW] = {};
  double c1 = 0, c1n = 0, c2 = 0, c2n = 0, c1e[2] = {}, c1ne[2] = {}, c2e[2] = {}, c2ne[2] = {};
  for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
    const int er = d.date / 10000 <= 2020 ? 0 : 1; const double A = d.atr;
    // minute closes by tday_min (forward-filled), so windows are on a regular grid
    std::vector<double> C(1321, NAN); std::vector<long> I(1321, -1);
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm <= 1320) { C[tm] = s.c[i]; I[tm] = i; } }
    for (int m = 1; m <= 1320; ++m) if (std::isnan(C[m])) { C[m] = C[m - 1]; I[m] = I[m - 1]; }
    if (std::isnan(C[0])) continue;
    for (int w = 0; w < NW; ++w) { const int a = W0[w], b = W1[w]; std::vector<double> r;
      for (int m = a + 1; m <= b; ++m) r.push_back((C[m] - C[m - 1]) / A);
      for (double x : r) { v1[er][w] += x * x; n1[er][w]++; }
      for (int qi = 0; qi < NQ; ++qi) { const int q = QS[qi]; for (int m = a; m + q <= b; ++m) { const double x = (C[m + q] - C[m]) / A; vq[er][w][qi] += x * x; nq[er][w][qi]++; } }
      // straightness vs shuffled baseline
      auto straight = [&](const std::vector<double>& rr) { double p = 0, hi = 0, lo = 0; for (double x : rr) { p += x; hi = std::max(hi, p); lo = std::min(lo, p); } return hi > lo ? std::fabs(p) / (hi - lo) : 0.0; };
      st[er][w] += straight(r); std::vector<double> sh = r; std::shuffle(sh.begin(), sh.end(), rng); stb[er][w] += straight(sh); nst[er][w]++;
      // band test: first touch of ±1 SD inside the window
      int touchDir = 0; double outcome = 2;   // 0 back to VWAP, 1 on to 2 SD, 2 neither
      for (int m = a + 1; m <= b && touchDir == 0; ++m) { const long i = I[m]; if (i < 0 || std::isnan(VW[i]) || !(SDV[i] > 0) || tday_min(s.t[i]) < 15) continue;
        if (s.h[i] >= VW[i] + SDV[i]) touchDir = 1; else if (s.l[i] <= VW[i] - SDV[i]) touchDir = -1;
        if (!touchDir) continue;
        for (int m2 = m + 1; m2 <= b; ++m2) { const long j = I[m2]; if (j < 0 || j == I[m2 - 1]) continue; const double vw = VW[j], sd = SDV[j];
          const bool back = touchDir > 0 ? s.l[j] <= vw : s.h[j] >= vw, on = touchDir > 0 ? s.h[j] >= vw + 2 * sd : s.l[j] <= vw - 2 * sd;
          if (on) { outcome = 1; break; } if (back) { outcome = 0; break; } } }
      if (touchDir) { bt[er][w][(int)outcome]++; nbt[er][w]++; }
      for (int m = std::max(a + 1, 15); m <= b; ++m) { const long i = I[m]; if (i < 0 || std::isnan(VW[i]) || !(SDV[i] > 0)) continue; in1[er][w] += std::fabs(s.c[i] - VW[i]) <= SDV[i]; nin[er][w]++; }
      { const long i = I[b]; if (i >= 0 && !std::isnan(VW[i]) && SDV[i] > 0) { const double z = std::fabs(s.c[i] - VW[i]) / SDV[i]; end1[er][w] += z > 1; end2[er][w] += z > 2; nend[er][w]++; } } }
    // NY direction
    const double o930 = C[930], m1000 = C[960] - C[930], rest = C[1320] - C[960], ny = C[1320] - C[930], on = C[930] - C[0];
    if (m1000 != 0 && rest != 0) { const bool same = (m1000 > 0) == (rest > 0); c1 += same; c1n++; c1e[er] += same; c1ne[er]++; }
    if (on != 0 && ny != 0) { const bool same = (on > 0) == (ny > 0); c2 += same; c2n++; c2e[er] += same; c2ne[er]++; }
    (void)o930; }
  printf("Does NQ mean-revert overnight and pick a direction in NY? — %s 1m, 2010-07 → 2026-10-23. Eras 2010-20 | 2021-26.\n", sym.c_str());
  printf("\nA  VARIANCE RATIO (1.00 = random walk; < 1 mean-reverting; > 1 trending)\n%-22s", "");
  for (int qi = 0; qi < NQ; ++qi) printf("   VR(%2dm) 10-20|21-26", QS[qi]); printf("\n");
  for (int w = 0; w < NW; ++w) { printf("%-22s", WN[w]); for (int qi = 0; qi < NQ; ++qi) { printf("      ");
      for (int e = 0; e < 2; ++e) printf(" %5.2f", (vq[e][w][qi] / nq[e][w][qi]) / (QS[qi] * v1[e][w] / n1[e][w])); printf("    "); } printf("\n"); }
  printf("\nB  STRAIGHTNESS |close − open| / range (random-walk baseline from the same window's shuffled moves)\n");
  for (int w = 0; w < NW; ++w) { printf("%-22s", WN[w]); for (int e = 0; e < 2; ++e) printf("   %s %.3f (random %.3f)", e ? "21-26" : "10-20", st[e][w] / nst[e][w], stb[e][w] / nst[e][w]); printf("\n"); }
  printf("\nC  FIRST TOUCH OF ±1 SD (18:00 VWAP) → back to VWAP first / on to ±2 SD first / neither by window end\n");
  for (int w = 0; w < NW; ++w) { printf("%-22s", WN[w]); for (int e = 0; e < 2; ++e) printf("   %s n%4.0f  VWAP %2.0f%%  2SD %2.0f%%  neither %2.0f%%", e ? "21-26" : "10-20", nbt[e][w], 100 * bt[e][w][0] / nbt[e][w], 100 * bt[e][w][1] / nbt[e][w], 100 * bt[e][w][2] / nbt[e][w]); printf("\n"); }
  printf("\nE  TIME INSIDE ±1 SD and where the window ENDS (18:00 VWAP)\n");
  for (int w = 0; w < NW; ++w) { printf("%-22s", WN[w]); for (int e = 0; e < 2; ++e) printf("   %s inside 1SD %2.0f%% of minutes · ends beyond 1SD %2.0f%% / beyond 2SD %2.0f%% of days", e ? "21-26" : "10-20", 100 * in1[e][w] / nin[e][w], 100 * end1[e][w] / nend[e][w], 100 * end2[e][w] / nend[e][w]); printf("\n"); }
  printf("\nD  NY DIRECTION\n   09:30-10:00 move and 10:00-16:00 move same sign: %.1f%% (n %.0f)  · 2010-20 %.1f%% · 2021-26 %.1f%%   (coin flip = 50%%)\n", 100 * c1 / c1n, c1n, 100 * c1e[0] / c1ne[0], 100 * c1e[1] / c1ne[1]);
  printf("   overnight (18:00-09:30) and NY (09:30-16:00) same sign:      %.1f%% (n %.0f)  · 2010-20 %.1f%% · 2021-26 %.1f%%\n", 100 * c2 / c2n, c2n, 100 * c2e[0] / c2ne[0], 100 * c2e[1] / c2ne[1]);
  return 0;
}
