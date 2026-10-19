// 2026-10-19 (founder: "optimize bull and bear as much as possible", "looks like there is a lot of room based on the chart"). NY wick-break
// flags on the 2/3/4/5-min charts, every signal found with relaxed rules and described by its levels; then a grid of rule settings is
// evaluated per side and chart, chosen on 2010-18 ONLY and read on 2019-24 and 2025-26.
// Relaxed detection (bull; bear mirror): pole = swing low → swing high 3-15 bars, >= 3x the session's average bar on that chart, >= 50 % of
// its bars with the move; flag after the pole top 2-12 bars so far, no new high, pullback 10-60 % of the pole, range <= 0.6 pole; wick candle
// = last flag bar with a wick against the trade >= 40 % of its range and a close in its trade-side half; entry = stop order 1 tick beyond it,
// filled within 1-3 bars, cancelled if the stop (1 tick beyond the wick) trades first; entries 09:45-15:25 ET. One signal at a time per chart.
// Grid (per side x chart): pole >= 3/4/5/6 x average bar · pullback window 10-60 / 15-50 / 20-50 / 25-45 % · wick >= 40/50/60 % · filled
// within 1/2/3 bars · context none / with the day (session side of the open) / + VWAP side / + straightness .40 / .55 / .70 · last entry
// 11:30 / 13:00 / 15:25 · pole bars with the move >= 50/60/70 % · exit trail (previous bar after +1R) / measured move x0.75 / x1 / x1.5.
// Score = net R (2 pts cost scaled to the day's range). Selection: 2010-18 n >= 120, positive in 2010-14 AND 2015-18, ranked by 2010-18 t.
#include "research/meta.hpp"
#include <cstdio>
#include <algorithm>
using namespace lab;
struct FB { double o, h, l, c; int k; };
enum { NX = 4 };
struct Cell { float n[4] = {0}, s[4][NX] = {{0}}, q[4][NX] = {{0}}, w[4][NX] = {{0}}; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[4] = {2, 3, 4, 5};
  // cell index: pm(4) x pullMask(16) x wick(3) x trig(3) x ctx(6) x time(3) x up(3)
  auto CI = [](int pm, int pmask, int wk, int tr, int cx, int tb, int up) { return ((((((pm * 16 + pmask) * 3 + wk) * 3 + tr) * 6 + cx) * 3 + tb) * 3 + up); };
  const int NC = 4 * 16 * 3 * 3 * 6 * 3 * 3;
  static std::vector<Cell> cube[2][4]; for (auto& a : cube) for (auto& b : a) b.assign(NC, Cell());
  double eraDays[4] = {0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr;
    const int era = d.date < 20150101 ? 0 : d.date < 20190101 ? 1 : d.date < 20250101 ? 2 : 3; eraDays[era]++;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(15, 55) + 1; ++i) I.push_back(i); if (I.size() < 300) continue; const size_t n = I.size();
    std::vector<double> ST(n), VW(n); std::vector<int> D(n); double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
    for (size_t k = 0; k < n; ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
      VW[k] = vv > 0 ? pv / vv : s.c[i]; ST[k] = hi > lo ? std::fabs(s.c[i] - so) / (hi - lo) : 0; D[k] = s.c[i] > so ? 1 : s.c[i] < so ? -1 : 0; }
    for (int ti = 0; ti < 4; ++ti) { const int tf = TFS[ti]; std::vector<FB> b; size_t busy = 0;
      for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
        if ((tm - hm_to_tday(9, 30)) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
        if ((tm + 1 - hm_to_tday(9, 30)) % tf || k <= busy) continue; if (tm < hm_to_tday(9, 45) || tm >= hm_to_tday(15, 25)) continue; int q = b.size() - 1; if (q < 6) continue;
        double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= (q + 1);
        bool done = false;
        for (int Dd : {1, -1}) { if (done) break;
          const FB& w = b[q]; double rg = w.h - w.l; if (rg <= 0) continue; double wk = Dd > 0 ? std::min(w.o, w.c) - w.l : w.h - std::max(w.o, w.c); double pos = Dd > 0 ? (w.c - w.l) / rg : (w.h - w.c) / rg;
          if (wk < 0.4 * rg || pos < 0.5) continue;
          for (int F = 2; F <= 12 && !done; ++F) { int h = q - F; if (h < 3) break;
            double H = Dd > 0 ? b[h].h : b[h].l; bool ok = true; for (int r = h + 1; r <= q; ++r) if (Dd > 0 ? b[r].h > H : b[r].l < H) { ok = false; break; } if (!ok) continue;
            int l = -1; double L = Dd > 0 ? 1e18 : -1e18; for (int r = std::max(0, h - 15); r <= h - 3; ++r) if (Dd > 0 ? b[r].l < L : b[r].h > L) { L = Dd > 0 ? b[r].l : b[r].h; l = r; }
            if (l < 0) continue; for (int r = l; r <= h; ++r) if (Dd > 0 ? (b[r].l < L || b[r].h > H) : (b[r].h > L || b[r].l < H)) ok = false; if (!ok) continue;
            double M = Dd * (H - L); if (M <= 0 || M < 3 * ar) continue; int up = 0; for (int r = l; r <= h; ++r) if (Dd * (b[r].c - b[r].o) > 0) up++; double upF = (double)up / (h - l + 1); if (upF < 0.5) continue;
            double FH = -1e18, FL = 1e18; for (int r = h + 1; r <= q; ++r) { FH = std::max(FH, b[r].h); FL = std::min(FL, b[r].l); }
            double pull = std::fabs(Dd > 0 ? H - FL : FH - H) / M; if (pull < 0.10 || pull > 0.60 || (FH - FL) > 0.6 * M) continue;
            // trigger within 3 bars of this chart
            double trig = Dd > 0 ? w.h + 0.25 : w.l - 0.25, stop = Dd > 0 ? w.l - 0.25 : w.h + 0.25; size_t ke = 0; long lastT = s.t[i] + 60L * tf * 3; int trBars = 0;
            for (size_t kk = k + 1; kk < n && s.t[I[kk]] < lastT; ++kk) { int j = I[kk]; if (Dd > 0 ? s.l[j] <= stop : s.h[j] >= stop) break; if (Dd > 0 ? s.h[j] >= trig : s.l[j] <= trig) { ke = kk; trBars = 1 + (int)((s.t[j] - s.t[i] - 60) / (60L * tf)); break; } }
            done = true; if (!ke) break; trBars = std::clamp(trBars, 1, 3);
            double R = Dd * (trig - stop); if (!(R > 0.25)) break; double cost = 2.0 * A / 320.0;
            // exits from the fill: trail, MM x0.75, x1, x1.5
            double outR[NX]; size_t exMM = n - 1;
            for (int xm = 0; xm < NX; ++xm) { double tgM = xm == 1 ? 0.75 : xm == 2 ? 1.0 : 1.5; double st = stop, tg = trig + Dd * tgM * M, px = NAN, bh = -1e18, bl = 1e18, ph = NAN, pl = NAN; bool armed = false; bool sameBar = Dd > 0 ? s.l[I[ke]] <= stop : s.h[I[ke]] >= stop;
              if (sameBar) { outR[xm] = -1 - cost / R; continue; }
              for (size_t kk = ke + 1; kk < n; ++kk) { int j = I[kk], tj = tday_min(s.t[j]); if (Dd > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; if (xm == 2) exMM = kk; break; }
                if (xm > 0 && (Dd > 0 ? s.h[j] >= tg : s.l[j] <= tg)) { px = tg; if (xm == 2) exMM = kk; break; } if (tj >= hm_to_tday(15, 55)) { px = s.c[j]; if (xm == 2) exMM = kk; break; }
                bh = std::max(bh, (double)s.h[j]); bl = std::min(bl, (double)s.l[j]);
                if ((tj + 1 - hm_to_tday(9, 30)) % tf == 0) { ph = bh; pl = bl; bh = -1e18; bl = 1e18; if (xm == 0 && armed) { double ns = Dd > 0 ? pl - 0.25 : ph + 0.25; st = Dd > 0 ? std::max(st, ns) : std::min(st, ns); } }
                if (xm == 0 && !armed && Dd * (s.c[j] - trig) >= R) { armed = true; if (!std::isnan(pl)) { double ns = Dd > 0 ? pl - 0.25 : ph + 0.25; st = Dd > 0 ? std::max(st, ns) : std::min(st, ns); } } }
              if (std::isnan(px)) px = s.c[I[n - 1]]; outR[xm] = (Dd * (px - trig) - cost) / R; }
            busy = exMM;
            // levels
            double pmult = M / ar; int pm = pmult >= 6 ? 3 : pmult >= 5 ? 2 : pmult >= 4 ? 1 : 0;
            int pmask = 1 | ((pull >= 0.15 && pull <= 0.50) ? 2 : 0) | ((pull >= 0.20 && pull <= 0.50) ? 4 : 0) | ((pull >= 0.25 && pull <= 0.45) ? 8 : 0);
            double wf = wk / rg; int wkl = wf >= 0.6 ? 2 : wf >= 0.5 ? 1 : 0; int trl = trBars - 1;
            int cx = 0; if (D[ke] == Dd) { cx = 1; if (Dd * (s.c[I[ke]] - VW[ke]) > 0) { cx = 2; if (ST[ke] >= 0.40) cx = 3; if (ST[ke] >= 0.55) cx = 4; if (ST[ke] >= 0.70) cx = 5; } }
            int tmE = tday_min(s.t[I[ke]]); int tb = tmE < hm_to_tday(11, 30) ? 0 : tmE < hm_to_tday(13, 0) ? 1 : 2; int upl = upF >= 0.7 ? 2 : upF >= 0.6 ? 1 : 0;
            Cell& c = cube[Dd > 0 ? 0 : 1][ti][CI(pm, pmask, wkl, trl, cx, tb, upl)];
            c.n[era]++; for (int xm = 0; xm < NX; ++xm) { c.s[era][xm] += outR[xm]; c.q[era][xm] += outR[xm] * outR[xm]; c.w[era][xm] += outR[xm] > 0; } } } } } }
  const char* XN[NX] = {"trail", "MM x0.75", "MM x1", "MM x1.5"}; const char* CXN[6] = {"none", "with day", "+VWAP", "+st.40", "+st.55", "+st.70"}; const char* TBN[3] = {"11:30", "13:00", "15:25"};
  const double PM[4] = {3, 4, 5, 6}; const char* PULN[4] = {"10-60", "15-50", "20-50", "25-45"};
  const double isDays = eraDays[0] + eraDays[1];
  printf("Wick-break flag optimisation — NQ NY, chosen on 2010-18, read on 2019-24 and 2025-26. R = net R per trade; /day = trades a day.\n");
  for (int side = 0; side < 2; ++side) for (int ti = 0; ti < 4; ++ti) {
    struct Cfg { int pm, pu, wk, tr, cx, tb, up, xm; double n[4], s[4], q[4], w[4]; double t; };
    std::vector<Cfg> all; const auto& C = cube[side][ti];
    for (int pm = 0; pm < 4; ++pm) for (int pu = 0; pu < 4; ++pu) for (int wk = 0; wk < 3; ++wk) for (int tr = 0; tr < 3; ++tr) for (int cx = 0; cx < 6; ++cx) for (int tb = 0; tb < 3; ++tb) for (int up = 0; up < 3; ++up) for (int xm = 0; xm < NX; ++xm) {
      Cfg g{pm, pu, wk, tr, cx, tb, up, xm, {0}, {0}, {0}, {0}, 0};
      for (int a = pm; a < 4; ++a) for (int msk = 0; msk < 16; ++msk) { if (!(msk >> pu & 1)) continue; for (int b2 = wk; b2 < 3; ++b2) for (int c2 = 0; c2 <= tr; ++c2) for (int d2 = cx; d2 < 6; ++d2) for (int e2 = 0; e2 <= tb; ++e2) for (int f2 = up; f2 < 3; ++f2) {
          const Cell& c = C[CI(a, msk, b2, c2, d2, e2, f2)]; for (int er = 0; er < 4; ++er) { if (!c.n[er]) continue; g.n[er] += c.n[er]; g.s[er] += c.s[er][xm]; g.q[er] += c.q[er][xm]; g.w[er] += c.w[er][xm]; } } }
      if (pm == 1 && pu == 1 && wk == 1 && tr == 1 && cx == 4 && tb == 2 && up == 1 && xm == 2) { double nn = g.n[0] + g.n[1] + g.n[2] + g.n[3]; printf("  [reference = the current rule (pole>=4x, pull 15-50, wick>=50, fill<=2, +st.55, to 15:25, up>=60, MM x1)] all years n %.0f, R %+.3f\n", nn, (g.s[0] + g.s[1] + g.s[2] + g.s[3]) / std::max(1.0, nn)); }
      double ni = g.n[0] + g.n[1]; if (ni < 120 || g.s[0] <= 0 || g.s[1] <= 0) continue; double m = (g.s[0] + g.s[1]) / ni, sd = std::sqrt(std::max(1e-9, (g.q[0] + g.q[1]) / ni - m * m)); g.t = m / (sd / std::sqrt(ni)); all.push_back(g); }
    std::sort(all.begin(), all.end(), [](const Cfg& a, const Cfg& b) { return a.t > b.t; });
    printf("\n== %s flags, %dm — %zu settings pass the 2010-18 screen\n", side ? "BEAR" : "BULL", TFS[ti], all.size());
    printf("  %-82s | 2010-18: /day  R    t   | 2019-24: /day   R    win%% | 2025-26: n    R\n", "setting (pole · pullback · wick · fill · context · last entry · pole bars · exit)");
    for (size_t i = 0; i < std::min<size_t>(6, all.size()); ++i) { const Cfg& g = all[i]; double ni = g.n[0] + g.n[1];
      char lab[120]; snprintf(lab, sizeof lab, "pole>=%.0fx · pull %s%% · wick>=%d%% · fill<=%d bars · %s · to %s · up>=%d%% · %s", PM[g.pm], PULN[g.pu], 40 + 10 * g.wk, g.tr + 1, CXN[g.cx], TBN[g.tb], 50 + 10 * g.up, XN[g.xm]);
      printf("  %-82s | %5.2f %+.3f %4.1f | %5.2f %+.3f %4.1f%% | %4.0f %+.3f\n", lab, ni / isDays, (g.s[0] + g.s[1]) / ni, g.t, g.n[2] / eraDays[2], g.n[2] ? g.s[2] / g.n[2] : 0, g.n[2] ? 100 * g.w[2] / g.n[2] : 0, g.n[3], g.n[3] ? g.s[3] / g.n[3] : 0); }
    if (!all.empty()) { std::vector<double> top, every; for (size_t i = 0; i < all.size(); ++i) { double v = all[i].n[2] ? all[i].s[2] / all[i].n[2] : 0; every.push_back(v); if (i < 20) top.push_back(v); }
      std::sort(top.begin(), top.end()); std::sort(every.begin(), every.end());
      printf("  2019-24 R: median of the top-20 by 2010-18 = %+.3f, median of all passing settings = %+.3f\n", top[top.size() / 2], every[every.size() / 2]); } }
  return 0;
}
