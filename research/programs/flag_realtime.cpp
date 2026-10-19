// 2026-10-19 (founder: "the flag already happened on the move — how can we identify that"): flags recognised AS THEY FORM, candle by
// candle, instead of after the whole pole + flag is visible. NY 1-5m, bull + bear (bears on the mirrored session), NQ 2010-07 → 2026-10.
// State machine per chart, decided at each chart-bar close:
//  1 IMPULSE  a candle closing in the trend direction with a body ≥ 50 % of its range and a range ≥ k × the session's average bar
//             (k = 1.5 / 2.0 / 2.5); following strong candles extend it (pole low = the first impulse candle's low, pole high = the
//             highest high so far)
//  2 PAUSE    the first candle that makes no new high, has a range ≤ 60 % of the average impulse candle and stays above the pole's
//             midpoint → a buy stop 1 tick above its high, protective stop 1 tick under the lowest pause low
//  3 TRIGGER  filled when price trades through the buy stop; if instead the bar is another small inside candle the buy stop moves to its
//             high (up to 4 pause candles); a close below the pole's midpoint, a big candle, or a 5th pause cancels
// Exits: hold to 15:55 · measured move (entry + pole) · 2R. 2 pts cost scaled to the day. Entries 09:45-14:00.
// LATENESS: at entry, share of the move already done = (entry − pole low) / (top − pole low), top = the highest high from the pole
// low to 60 min after entry; and the room left = (top − entry) / risk. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
constexpr int NX = 3;
struct T { int tf, side, ki, era; bool ctx; double done, left, R[NX]; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const double K[3] = {1.5, 2.0, 2.5}; const char* XN[NX] = {"hold", "measured", "2R"};
  std::vector<T> ts; double nd[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; nd[era]++; const double cost = 2 * d.atr / 320.0; const size_t n = I.size();
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3 * s.v[I[k]]; vv += s.v[I[k]]; vw[k] = vv > 0 ? pv / vv : Cc[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); hiS[k] = hi; loS[k] = lo; } }
      const double so = O[0];
      for (int tf = 1; tf <= 5; ++tf) for (int ki = 0; ki < 3; ++ki) {
        int state = 0, npause = 0; double L = 0, H = 0, sumImp = 0; int nImp = 0; size_t kL = 0; double trig = 0, pLow = 0; double bo = 0, bh = -1e18, bl = 1e18, sumR = 0; int nb = 0; size_t busyK = 0;
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          // the armed buy stop works inside the bar, minute by minute
          if (state == 2 && k > busyK && Hh[k] >= trig && tm >= 945 && tm < 1400) {
            const double e = trig, st = pLow - 0.25, risk = e - st, M = H - L; T t{}; t.tf = tf; t.side = side; t.ki = ki; t.era = era;
            t.ctx = e > so && e > vw[k] && hiS[k] > loS[k] && (e - so) / (hiS[k] - loS[k]) >= 0.55;
            double top = std::max(H, Hh[k]); for (size_t kk = k; kk < n && TM[kk] < tm + 60; ++kk) top = std::max(top, Hh[kk]);
            t.done = (e - L) / std::max(0.25, top - L); t.left = (top - e) / risk; size_t kx0 = k;
            for (int x = 0; x < NX; ++x) { double tg = x == 0 ? 1e18 : x == 1 ? e + M : e + 2 * risk, px = NAN; if (Lw[k] <= st) px = st;
              for (size_t kk = k + 1; kk < n && std::isnan(px); ++kk) { if (Lw[kk] <= st) { px = st; break; } if (Hh[kk] >= tg) { px = tg; break; } if (TM[kk] >= 1315) { px = O[kk]; break; } }
              if (std::isnan(px)) px = Cc[n - 1]; t.R[x] = (px - e - cost) / risk; }
            ts.push_back(t); state = 0; busyK = kx0 + 1; }
          if (bh < -1e17) bo = O[k]; bh = std::max(bh, Hh[k]); bl = std::min(bl, Lw[k]);
          if ((tm + 1 - 930) % tf) continue;
          const double bc = Cc[k], rg = bh - bl; nb++; sumR += rg; const double ar = sumR / nb;
          const bool strong = bc > bo && rg > 0 && (bc - bo) >= 0.5 * rg && rg >= K[ki] * ar && nb > 3;
          if (state == 0) { if (strong) { state = 1; L = bl; H = bh; sumImp = rg; nImp = 1; kL = k; } }
          else if (state == 1) { if (bh > H && bc > bo && (bc - bo) >= 0.5 * rg) { H = bh; sumImp += rg; nImp++; }
            else if (bh <= H && rg <= 0.6 * sumImp / nImp && bl > L + 0.5 * (H - L)) { state = 2; npause = 1; trig = bh + 0.25; pLow = bl; }
            else if (strong) { L = bl; H = bh; sumImp = rg; nImp = 1; kL = k; } else state = 0; }
          else if (state == 2) { if (bh <= trig - 0.25 && rg <= 0.6 * sumImp / nImp && bl > L + 0.5 * (H - L) && npause < 4) { npause++; trig = bh + 0.25; pLow = std::min(pLow, bl); }
            else { state = strong ? 1 : 0; if (strong) { L = bl; H = bh; sumImp = rg; nImp = 1; kL = k; } } }
          (void)kL; bh = -1e18; bl = 1e18; } } } }
  const double ND = nd[0] + nd[1] + nd[2];
  printf("Flags recognised as they form (impulse → pause → buy stop above the pause candle) — NY 1-5m, %zu trades. R after 2 pts cost.\n", ts.size());
  printf("Cells: trades a day | median share of the move already done at entry | median room left (R) | avg R 2010-18 / 2019-24 / 2025-26 (n 2025-26) | win %% — per exit.\n");
  for (int cx = 0; cx < 2; ++cx) { printf("\n######## %s\n", cx ? "with the session direction (above the open and VWAP, straight path)" : "no context filter");
    for (int side = 1; side >= -1; side -= 2) for (int tf = 1; tf <= 5; ++tf) for (int ki = 0; ki < 3; ++ki) { std::vector<const T*> a; for (auto& t : ts) if (t.tf == tf && t.side == side && t.ki == ki && (!cx || t.ctx)) a.push_back(&t); if (a.size() < 50) continue;
      std::vector<double> dn, lf; for (auto* t : a) { dn.push_back(t->done); lf.push_back(t->left); } std::sort(dn.begin(), dn.end()); std::sort(lf.begin(), lf.end());
      printf("  %s %dm impulse >= %.1fx | %.2f/day | done %3.0f%% | left %.1fR |", side > 0 ? "bull" : "bear", tf, K[ki], a.size() / ND, 100 * dn[dn.size() / 2], lf[lf.size() / 2]);
      for (int x = 0; x < NX; ++x) { double s3[3] = {0, 0, 0}, n3[3] = {0, 0, 0}, w = 0; for (auto* t : a) { s3[t->era] += t->R[x]; n3[t->era]++; w += t->R[x] > 0; }
        printf(" %s %+.2f/%+.2f/%+.2f (%.0f) %2.0f%% |", XN[x], n3[0] ? s3[0] / n3[0] : 0, n3[1] ? s3[1] / n3[1] : 0, n3[2] ? s3[2] / n3[2] : 0, n3[2], 100 * w / a.size()); } printf("\n"); } }
  return 0;
}
