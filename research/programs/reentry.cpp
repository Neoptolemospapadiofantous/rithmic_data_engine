// 2026-10-17 re-entries in the day's direction (research/preregistered/2026-10-17_reentry.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <random>
using namespace lab;
struct Res { double pts; int xm; bool win; };
struct Acc { double n = 0, s = 0; void add(double x) { n++; s += x; } double m() const { return n ? s / n : NAN; } };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  const int NM = 386, LAST = 265;   // minutes after 09:30: 15:55 = 385, last entry 13:55 = 265
  const char* RN[4] = {"BASE", "+NEW", "+VWAP", "+HOLD"};
  struct Out { Acc day, trades, re, twin, era[3], reera[3], twera[3]; int ntr = 0; double days = 0; };
  Out R[4][2]; std::mt19937 rng(11);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || D.date > 20241231 || !(D.atr > 0) || std::isnan(D.pc)) continue;
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
    std::array<double, NM> o, h, l, c, vw; double pv = 0, vv = 0, last = s.o[i0]; int j = i0;
    for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
      if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; pv += (h[m] + l[m] + c[m]) / 3 * std::max(1.0f, s.v[j]); vv += std::max(1.0f, s.v[j]); }
      else { o[m] = h[m] = l[m] = c[m] = last; } last = c[m]; vw[m] = vv > 0 ? pv / vv : last; }
    const double op = o[0], atr = D.atr, gap = (op - D.pc) / atr; const int era = D.date < 20190101 ? 0 : D.date < 20230101 ? 1 : 2;
    auto px = [&](int m) { return m == 0 ? op : c[m - 1]; };   // price at minute m (time 09:30 + m)
    auto run = [&](int me, int dir, double stopk) -> Res { const double p = px(me), st = stopk * atr;
      for (int m = me; m < NM - 1; ++m) { if (dir > 0 ? l[m] <= p - st : h[m] >= p + st) return {-st - 1, m + 1, false}; if (dir > 0 ? h[m] >= p + 2 * st : l[m] <= p - 2 * st) return {2 * st - 1, m + 1, true}; }
      return {dir * (c[NM - 2] - p) - 1, NM - 1, false}; };
    auto b5 = [&](int k, double& O, double& H, double& L, double& C) { O = o[5 * k]; H = -1e18; L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); } C = c[5 * k + 4]; };
    double orh = -1e18, orl = 1e18; for (int m = 0; m < 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
    const int gdir = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
    for (int r = 0; r < 4; ++r) for (int fl = 0; fl < 2; ++fl) {
      Out& O = R[r][fl]; bool used[4] = {false, false, false, false}, gused = false, hused[4] = {false, false, false, false}; int D_ = 0, X = 0, ntr = 0; double dayp = 0; bool lastwin = true;
      const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30}; const int HM[4] = {150, 180, 210, 240};
      while (ntr < 3) {
        if (ntr > 0 && fl == 1 && !lastwin) break;
        int bm = 1 << 30, bdir = 0, src = -1, bi = -1; double bst = 0.25;
        auto cand = [&](int m, int d, double sk, int sc, int idx) { if (m < X || m > LAST || (D_ && d != D_) || !d) return; if (m < bm) { bm = m; bdir = d; bst = sk; src = sc; bi = idx; } };
        for (int q = 0; q < 4; ++q) if (!used[q] && CM[q] >= X) { const double mv = px(CM[q]) - op; if (std::fabs(mv) >= CK[q] * atr) cand(CM[q], mv > 0 ? 1 : -1, 0.25, 0, q); else if (ntr == 0) {} }
        if (!gused && gdir) for (int k = 3; 5 * k <= LAST; ++k) { if (5 * k < X) continue; double a, H, L, C; b5(k, a, H, L, C);
          if (gdir > 0 ? (L <= orh && C > orh) : (H >= orl && C < orl)) { cand(5 * k + 5, gdir, 0.35, 1, 0); break; } }
        if (ntr > 0 && r == 1) { double ex = D_ > 0 ? -1e18 : 1e18; for (int m = 0; m < X; ++m) ex = D_ > 0 ? std::max(ex, h[m]) : std::min(ex, l[m]);
          for (int k = (X + 4) / 5; 5 * k + 5 <= LAST; ++k) { double a, H, L, C; b5(k, a, H, L, C); if (D_ > 0 ? C > ex : C < ex) { cand(5 * k + 5, D_, 0.25, 2, 0); break; } } }
        if (ntr > 0 && r == 2) for (int k = (X + 4) / 5; 5 * k + 5 <= LAST; ++k) { double a, H, L, C; b5(k, a, H, L, C); const double v = vw[5 * k + 4];
          if (D_ > 0 ? (L <= v && C > v) : (H >= v && C < v)) { cand(5 * k + 5, D_, 0.25, 2, 0); break; } }
        if (ntr > 0 && r == 3) for (int q = 0; q < 4; ++q) if (!hused[q] && HM[q] >= X) { const double mv = D_ * (px(HM[q]) - op); if (mv >= 0.30 * atr) { cand(HM[q], D_, 0.25, 3, q); break; } }
        if (src < 0) break;
        if (src == 0) for (int q = 0; q <= bi; ++q) used[q] = true;   // a later check consumes the earlier ones
        if (src == 1) gused = true; if (src == 3) for (int q = 0; q <= bi; ++q) hused[q] = true;
        if (!D_) D_ = bdir;
        const Res res = run(bm, bdir, bst); dayp += res.pts; O.trades.add(res.pts); ++ntr;
        if (ntr > 1) { O.re.add(res.pts); O.reera[era].add(res.pts);
          const int tm = std::uniform_int_distribution<int>(X, LAST)(rng); const Res tw = run(tm, D_, 0.25); O.twin.add(tw.pts); O.twera[era].add(tw.pts); }
        X = res.xm; lastwin = res.win; if (X >= NM - 1) break;
      }
      if (ntr) { O.day.add(dayp); O.era[era].add(dayp); } O.days += 1;
    }
  }
  printf("NQ 2010-07 → 2024-12, one position at a time, ≤3 trades/day, cost 1.0 pt/trade (NQ pts)\n");
  for (int fl = 0; fl < 2; ++fl) { printf("\n-- re-enter after %s\n", fl ? "a WIN only" : "ANY exit");
    for (int r = 0; r < 4; ++r) { auto& O = R[r][fl];
      printf("  %-6s trade days %4.0f, trades/trade-day %.2f, per trade %+5.2f, per trade-day %+6.2f (eras %+.2f / %+.2f / %+.2f) | 2nd/3rd trades n %4.0f %+5.2f vs twins %+5.2f (eras %+.2f/%+.2f/%+.2f vs %+.2f/%+.2f/%+.2f)\n",
             RN[r], O.day.n, O.trades.n / O.day.n, O.trades.m(), O.day.m(), O.era[0].m(), O.era[1].m(), O.era[2].m(), O.re.n, O.re.m(), O.twin.m(),
             O.reera[0].m(), O.reera[1].m(), O.reera[2].m(), O.twera[0].m(), O.twera[1].m(), O.twera[2].m()); } }
}
