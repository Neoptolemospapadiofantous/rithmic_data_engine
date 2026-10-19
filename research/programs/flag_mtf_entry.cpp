// 2026-10-19 (founder: "maybe test multi time frame flags entry", "do more tests"): the flag is found on the HIGHER chart (3/4/5/15m,
// the structural flags of flag_sequence.cpp: pole ≥ 4× the chart's average bar, pullback 15-50 %, with the session direction — above
// the 09:30 open and VWAP on a straight path), the ENTRY is timed on the LOWER chart (1m or 2m) while that flag is live.
// Bull + bear (bears on the mirrored session). NQ 2010-07 → 2026-10, NY, entries 09:45-14:00.
// Entries (same flag, same target):
//  H  higher-chart reference: stop order 1 tick above the pole top, stop = flag low
//  L1 lower-chart break: first lower-chart close above the previous lower-chart bar's high after at least one lower high inside the
//     flag; stop = the flag low so far
//  L2 same trigger, tight stop = lowest low of the last 3 lower-chart bars
//  L3 lower-chart wick candle (lower wick ≥ 50 % of its range, close in its top half) inside the flag, entry at its close, stop under it
//  L4 lower-chart change of character: close above the last lower-chart swing high (2-bar pivots) after it made a lower high; stop under
//     the lower-chart swing low since then
// Targets: higher-chart measured move (pole top + pole) · 2R · hold to 15:55. Pole size: ≥ 0 or ≥ 0.30 × ATR. 2 pts cost scaled to the
// day. Selection by period is reported (2010-18 / 2019-24 / 2025-26) — no parameter is fitted here. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; int k; };
constexpr int NE = 5, NX = 3;
struct C { int htf, ltf, side, era; double pa; float R[NE][NX]; float risk[NE]; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const char* EN[NE] = {"H  pole-top stop (higher chart)", "L1 lower-chart break, flag stop", "L2 lower-chart break, tight stop", "L3 lower-chart wick close", "L4 lower-chart CHoCH"}; const char* XN[NX] = {"measured", "2R", "hold"};
  std::vector<C> cs; double nd[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; nd[era]++; const double A = d.atr, sc = A / 320.0, cost = 2 * sc; const size_t n = I.size();
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3 * s.v[I[k]]; vv += s.v[I[k]]; vw[k] = vv > 0 ? pv / vv : Cc[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); hiS[k] = hi; loS[k] = lo; } }
      const double so = O[0];
      auto exits = [&](size_t k0, double e, double st, double tgM, bool fillMin, float out[NX]) { const double risk = e - st;
        for (int x = 0; x < NX; ++x) { double tg = x == 0 ? tgM : x == 1 ? e + 2 * risk : 1e18, px = NAN; if (tg <= e) { out[x] = NAN; continue; } if (fillMin && Lw[k0] <= st) px = st;
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; out[x] = (px - e - cost) / risk; } };
      for (int htf : {3, 4, 5, 15}) { std::vector<FB> b; int busy = -1;
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          if ((tm - 930) % htf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.k = k; }
          if ((tm + 1 - 930) % htf) continue; const int q = b.size() - 1; if (q < 6 || q <= busy || tm < 944 || tm >= 1260) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 4 * ar) continue;
          const double FL0 = std::min(b[h + 1].l, b[q].l), pull = (H - FL0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const size_t kq = b[q].k; const double cl = b[q].c; if (!(cl > so && cl > vw[kq] && hiS[kq] > loS[kq] && (cl - so) / (hiS[kq] - loS[kq]) >= 0.55)) continue;
          const double tgM = H + M;
          for (int ltf : {1, 2}) { if (ltf >= htf) continue; C c{}; c.htf = htf; c.ltf = ltf; c.side = side; c.era = era; c.pa = M / A;
            for (int e = 0; e < NE; ++e) { for (int x = 0; x < NX; ++x) c.R[e][x] = NAN; c.risk[e] = NAN; }
            // follow the flag minute by minute until it resolves on the higher chart
            double fl = FL0; int hNb = 0; double hbh = -1e18, hbl = 1e18;
            std::vector<FB> lb; bool lowerHigh = false, wickDone = false; double sh = NAN, shLow = 1e18; int trend = 0; bool doneH = false;
            // lower-chart pivots for L4
            for (size_t kk = kq + 1; kk < n && !doneH; ++kk) { const int t2 = TM[kk]; if (t2 >= 1260) break;
              if (Hh[kk] > H && std::isnan(c.R[0][0])) { exits(kk, H + 0.25, fl - 0.25, tgM, true, c.R[0]); c.risk[0] = (H + 0.25 - fl + 0.25) / sc; }
              // lower chart bars
              if ((t2 - 930) % ltf == 0 || lb.empty()) lb.push_back({O[kk], Hh[kk], Lw[kk], Cc[kk], (int)kk}); else { FB& z = lb.back(); z.h = std::max(z.h, Hh[kk]); z.l = std::min(z.l, Lw[kk]); z.c = Cc[kk]; z.k = kk; }
              hbh = std::max(hbh, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
              if ((t2 + 1 - 930) % ltf == 0 && lb.size() >= 2) { const int m = lb.size() - 1; const FB& z = lb[m]; const FB& pz = lb[m - 1];
                if (z.h < pz.h) lowerHigh = true;
                if (z.h <= H) {
                  if (lowerHigh && std::isnan(c.R[1][0]) && z.c > pz.h && z.c - std::min(fl, hbl) > 0.5) { const double flNow = std::min(fl, hbl); exits(kk, z.c, flNow - 0.25, tgM, false, c.R[1]); c.risk[1] = (z.c - flNow + 0.25) / sc;
                    double lo3 = 1e18; for (int r = std::max(0, m - 2); r <= m; ++r) lo3 = std::min(lo3, lb[r].l); exits(kk, z.c, lo3 - 0.25, tgM, false, c.R[2]); c.risk[2] = (z.c - lo3 + 0.25) / sc; }
                  if (!wickDone) { double rg = z.h - z.l, wk = std::min(z.o, z.c) - z.l; if (rg > 0 && wk >= 0.5 * rg && (z.c - z.l) / rg >= 0.5) { wickDone = true; exits(kk, z.c, z.l - 0.25, tgM, false, c.R[3]); c.risk[3] = (z.c - z.l + 0.25) / sc; } }
                  // L4: 2-bar pivots on the lower chart inside the flag
                  if (m >= 4) { int p = m - 2; if (lb[p].h > lb[p - 1].h && lb[p].h > lb[p - 2].h && lb[p].h > lb[p + 1].h && lb[p].h > lb[p + 2].h) { if (!std::isnan(sh) && lb[p].h < sh) trend = -1; sh = lb[p].h; shLow = 1e18; } }
                  shLow = std::min(shLow, z.l);
                  if (trend == -1 && !std::isnan(sh) && z.c > sh && std::isnan(c.R[4][0])) { exits(kk, z.c, shLow - 0.25, tgM, false, c.R[4]); c.risk[4] = (z.c - shLow + 0.25) / sc; trend = 1; } } }
              if ((t2 + 1 - 930) % htf == 0) { hNb++; fl = std::min(fl, hbl); if (hbh > H || Cc[kk] < H - 0.5 * M || hNb >= 12) doneH = true; hbh = -1e18; hbl = 1e18; } }
            busy = std::max(busy, q + hNb); cs.push_back(c); } } } } }
  printf("Multi-timeframe flag entries — flag on the higher chart, entry on the lower chart. NY bull + bear, NQ 2010-07 → 2026-10, %zu flag × lower-chart pairs.\n", cs.size());
  printf("Cells: fill %% | win %% | avg R 2010-18 / 2019-24 / 2025-26 (n 2025-26) | median stop in pts at today's range.\n");
  const double ND = nd[0] + nd[1] + nd[2];
  for (int side = 1; side >= -1; side -= 2) for (int htf : {3, 4, 5, 15}) for (int ltf : {1, 2}) for (double pmin : {0.0, 0.30}) { std::vector<const C*> a; for (auto& c : cs) if (c.side == side && c.htf == htf && c.ltf == ltf && c.pa >= pmin) a.push_back(&c); if (a.size() < 60) continue;
    printf("\n== %s flag on %dm, entry on %dm, pole >= %.2f ATR — %zu flags (%.2f a day)\n", side > 0 ? "bull" : "bear", htf, ltf, pmin, a.size(), a.size() / ND);
    for (int e = 0; e < NE; ++e) { std::vector<double> rk; for (auto* c : a) if (!std::isnan(c->risk[e])) rk.push_back(c->risk[e]); if (rk.size() < 20) continue; std::sort(rk.begin(), rk.end());
      printf("  %-33s fill %3.0f%% | stop %4.1f |", EN[e], 100.0 * rk.size() / a.size(), rk[rk.size() / 2]);
      for (int x = 0; x < NX; ++x) { double s3[3] = {0, 0, 0}, n3[3] = {0, 0, 0}, w = 0, nn = 0; for (auto* c : a) { float r = c->R[e][x]; if (std::isnan(r)) continue; s3[c->era] += r; n3[c->era]++; w += r > 0; nn++; }
        printf(" %s %2.0f%% %+.2f/%+.2f/%+.2f (%.0f) |", XN[x], nn ? 100 * w / nn : 0, n3[0] ? s3[0] / n3[0] : 0, n3[1] ? s3[1] / n3[1] : 0, n3[2] ? s3[2] / n3[2] : 0, n3[2]); } printf("\n"); } }
  return 0;
}
