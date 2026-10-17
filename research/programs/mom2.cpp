// 2026-10-17 1-min momentum direction, round 2 (research/preregistered/2026-10-17_momentum_round2.md).
// Frozen rule from 2026-10-17_mtf_1m_dir_5m_entry.md, tested on ES, on the London / Asia sessions, and with other exits.
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <map>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, ND = 1380;
struct A { double n = 0, r = 0, pts = 0; void add(double rr, double p) { n++; r += rr; pts += p; } double m() const { return n ? r / n : NAN; } };
const char* SN[3] = {"New York", "London", "Asia"}; const char* EN[2] = {"any 5-min close", "5-min EMA-20 near"};
const char* XN[8] = {"BE +1R hold (frozen)", "BE +0.5R hold", "BE +1.5R hold", "2R target", "trail 0.25 ATR", "BE +1R then trail 1R", "time exit 60 min", "time exit 120 min"};
struct Sess { int open, e0, e1, flat; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  const Sess SS[3] = {{930, 945, 1195, 1315}, {540, 555, 780, 840}, {0, 15, 420, 510}};
  static A R[2][3][2][8][NY]; static A SPL[6][NY];   // splits: NQ NY frozen: ≤11:30, >11:30, plan agrees, disagrees, no plan dir
  for (int sym = 0; sym < 2; ++sym) {
    Series s; std::string err; if (!load_raw("data/edge_lab/cache", sym ? "ES" : "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    build_days(s, fomc, vix); const double COST = sym ? 0.65 : 1.6;
    std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND);
    for (auto& D : s.days) {
      if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
      { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
          if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = 0; } last = c[m]; } }
      const double atr = D.atr, st = 0.25 * atr;
      // 5-min EMA-20 after each 5-min bar (by bar index = start / 5), from 18:00
      std::vector<double> e20(ND / 5); { double x = c[4]; for (int k = 0; k < ND / 5; ++k) { x += 2.0 / 21 * (c[5 * k + 4] - x); e20[k] = x; } }
      // NY plan direction (for the splits only)
      int pwhen = 1 << 30, pd = 0; { const double op = o[930]; const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
        for (int q = 0; q < 4; ++q) { const double mv = c[930 + CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = 930 + CM[q]; pd = mv > 0 ? 1 : -1; break; } } }
      for (int se = 0; se < 3; ++se) { const Sess& S = SS[se]; const double sop = o[S.open];
        std::vector<double> vw(ND, NAN); { double pv = 0, vv = 0; for (int m = S.open; m < S.flat; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
        for (int en = 0; en < 2; ++en) for (int ex = 0; ex < 8; ++ex) {
          int busy = 0, ntr = 0; bool lastwin = true;
          for (int e = S.e0; e <= S.e1; e += 5) { if (ntr >= 3 || !lastwin) break; if (e - 5 < busy || e < 31) continue;
            const int mm = e - 1; double path = 0; for (int q = mm - 29; q <= mm; ++q) path += std::fabs(c[q] - c[q - 1]); const double net = c[mm] - c[mm - 30];
            if (path <= 0 || std::fabs(net) / path < 0.3) continue; const int d = net > 0 ? 1 : -1; const double C = c[mm];
            if (std::isnan(vw[mm]) || d * (C - vw[mm]) <= 0 || d * (C - sop) <= 0) continue;
            if (en == 1) { const int k = e / 5 - 1; if (k < 1) continue; double H = -1e18, L = 1e18; for (int q = e - 5; q < e; ++q) { H = std::max(H, h[q]); L = std::min(L, l[q]); }
              const double ep = e20[k - 1], tol = 0.03 * atr; if (!(d > 0 ? (L <= ep + tol && C > ep) : (H >= ep - tol && C < ep))) continue; }
            // exit simulation
            double stop = C - d * st, best = C; bool be = false; double pts = NAN; int xm = S.flat;
            const double be_at = ex == 1 ? 0.5 : ex == 2 ? 1.5 : 1.0; const int tlim = ex == 6 ? e + 60 : ex == 7 ? e + 120 : S.flat;
            for (int m = e; m < std::min(tlim, S.flat); ++m) {
              if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - C); xm = m + 1; break; }
              if (ex == 3 && (d > 0 ? h[m] >= C + 2 * st : l[m] <= C - 2 * st)) { pts = 2 * st; xm = m + 1; break; }
              best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); const double fav = d * (best - C);
              if ((ex <= 2 || ex == 5 || ex >= 6) && ex != 3 && ex != 4 && !be && fav >= be_at * st) { be = true; stop = C + d * COST; }
              if (ex == 4) stop = d > 0 ? std::max(stop, best - st) : std::min(stop, best + st);
              if (ex == 5 && be) stop = d > 0 ? std::max(stop, best - st) : std::min(stop, best + st); }
            if (std::isnan(pts)) { const int xe = std::min(tlim, S.flat) - 1; pts = d * (c[xe] - C); xm = xe + 1; }
            pts -= COST; R[sym][se][en][ex][yi].add(pts / st, pts);
            if (sym == 0 && se == 0 && en == 0 && ex == 0) { SPL[e <= 1050 ? 0 : 1][yi].add(pts / st, pts); SPL[e < pwhen || !pd ? 4 : (pd == d ? 2 : 3)][yi].add(pts / st, pts); }
            busy = xm; ++ntr; lastwin = pts > 0; } } } } }
  auto sum = [](A* y, int y0, int y1) { A t; for (int k = y0; k <= y1; ++k) { t.n += y[k - Y0].n; t.r += y[k - Y0].r; t.pts += y[k - Y0].pts; } return t; };
  auto line = [&](const char* nm, A* y) { A a = sum(y, 2010, 2024), e0 = sum(y, 2010, 2018), e1 = sum(y, 2019, 2022), e2 = sum(y, 2023, 2024), hh = sum(y, 2025, 2026);
    printf("  %-26s n %5.0f %+.3f R (%+.3f / %+.3f / %+.3f)%s %+6.2f pts | 25-26 %+.3f R (n %.0f)\n", nm, a.n, a.m(), e0.m(), e1.m(), e2.m(), (e0.r > 0 && e1.r > 0 && e2.r > 0) ? " *" : "  ", a.pts / a.n, hh.m(), hh.n); };
  for (int sym = 0; sym < 2; ++sym) for (int se = 0; se < 3; ++se) { printf("\n=== %s %s (cost %.2f pts) ===\n", sym ? "ES" : "NQ", SN[se], sym ? 0.65 : 1.6);
    for (int en = 0; en < 2; ++en) for (int ex = 0; ex < 8; ++ex) { if (!(se == 0 && sym == 0) && ex > 0) continue; char b[64]; snprintf(b, 64, "%s | %s", en ? "EMA20" : "any", XN[ex]); line(b, R[sym][se][en][ex]); } }
  printf("\nSplits, NQ New York, frozen rule (any 5-min close, BE +1R hold):\n");
  const char* SPN[5] = {"entry ≤ 11:30", "entry > 11:30", "plan direction agrees", "plan direction disagrees", "no plan direction yet"};
  for (int q = 0; q < 5; ++q) line(SPN[q], SPL[q]);
  printf("\nWalk-forward exit choice, NQ New York, any 5-min close (each year the best total R of 2010..year-1):\n"); A w, f;
  for (int y = 2013; y <= 2024; ++y) { int best = 0; double br = -1e18; for (int ex = 0; ex < 8; ++ex) { A a = sum(R[0][0][0][ex], 2010, y - 1); if (a.r > br) { br = a.r; best = ex; } }
    auto& a = R[0][0][0][best][y - Y0]; w.n += a.n; w.r += a.r; auto& b = R[0][0][0][0][y - Y0]; f.n += b.n; f.r += b.r; printf("    %d %-22s %+.3f R (frozen %+.3f)\n", y, XN[best], a.m(), b.m()); }
  printf("  WF %+.3f R/trade vs frozen %+.3f\n", w.r / w.n, f.r / f.n);
}
