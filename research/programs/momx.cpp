// 2026-10-17 momentum direction: definitions × entries × exits (research/preregistered/2026-10-17_momentum_entries.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, ND = 1380, R0 = 930, NMO = 5, NEN = 9, NEX = 2, NC = NMO * NEN * NEX;
const char* MN[NMO] = {"M-EFF30", "M-EFF45", "M-ROC", "M-STAIR", "M-SLOPE"};
const char* EN[NEN] = {"E-ANY", "E-EMA9", "E-EMA20", "E-VWAP", "E-BRK", "E-HOD", "E-OR", "E-DIP", "E-INSIDE"};
const char* XN[NEX] = {"stop .25 BE1R", "stop .15 noBE"};
struct YA { double n = 0, r = 0, pts = 0; };
static void run(const Series& s, double COST, std::vector<std::array<YA, NY>>& A) {
  A.assign(NC, {});
  std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND), vw(ND), e21(ND);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = 0; } last = c[m]; } }
    { double pv = 0, vv = 0; for (int m = R0; m < ND; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
    { double x = c[0]; for (int m = 0; m < ND; ++m) { x += 2.0 / 22 * (c[m] - x); e21[m] = x; } }
    const double op = o[R0], atr = D.atr;
    const int NB = ND / 5; std::vector<double> BH(NB), BL(NB), BC(NB); for (int k = 0; k < NB; ++k) { BH[k] = -1e18; BL[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { BH[k] = std::max(BH[k], h[m]); BL[k] = std::min(BL[k], l[m]); } BC[k] = c[5 * k + 4]; }
    std::vector<double> E9(NB), E20(NB); { double a = BC[0], b = BC[0]; for (int k = 0; k < NB; ++k) { a += 0.2 * (BC[k] - a); b += 2.0 / 21 * (BC[k] - b); E9[k] = a; E20[k] = b; } }
    double orh = -1e18, orl = 1e18; for (int m = R0; m < R0 + 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
    auto mom = [&](int md, int k) -> int { const int mm = 5 * k + 4;
      if (md <= 1) { const int W = md ? 45 : 30; double path = 0; for (int q = mm - W + 1; q <= mm; ++q) path += std::fabs(c[q] - c[q - 1]); const double net = c[mm] - c[mm - W];
        return path > 0 && std::fabs(net) / path >= 0.3 ? (net > 0 ? 1 : -1) : 0; }
      if (md == 2) { const double net = c[mm] - c[mm - 30]; return std::fabs(net) >= 0.15 * atr ? (net > 0 ? 1 : -1) : 0; }
      if (md == 3) { if (BC[k] > BC[k - 1] && BC[k - 1] > BC[k - 2] && BC[k - 2] > BC[k - 3]) return 1; if (BC[k] < BC[k - 1] && BC[k - 1] < BC[k - 2] && BC[k - 2] < BC[k - 3]) return -1; return 0; }
      const double sl = e21[mm] - e21[mm - 15]; return std::fabs(sl) >= 0.05 * atr ? (sl > 0 ? 1 : -1) : 0; };
    auto entry = [&](int en, int k, int d) -> bool { const double C = BC[k];
      switch (en) {
        case 0: return true;
        case 1: { const double ep = E9[k - 1]; return d > 0 ? (BL[k] <= ep + 0.06 * atr && C > ep) : (BH[k] >= ep - 0.06 * atr && C < ep); }
        case 2: { const double ep = E20[k - 1]; return d > 0 ? (BL[k] <= ep + 0.03 * atr && C > ep) : (BH[k] >= ep - 0.03 * atr && C < ep); }
        case 3: { const double ep = vw[5 * k - 1]; return d > 0 ? (BL[k] <= ep + 0.05 * atr && C > ep) : (BH[k] >= ep - 0.05 * atr && C < ep); }
        case 4: return d > 0 ? C > BH[k - 1] : C < BL[k - 1];
        case 5: { double hh = -1e18, ll = 1e18; for (int m = R0; m < 5 * k; ++m) { hh = std::max(hh, h[m]); ll = std::min(ll, l[m]); } return d > 0 ? C > hh : C < ll; }
        case 6: return d > 0 ? C > orh : C < orl;
        case 7: { double ex = d > 0 ? -1e18 : 1e18; for (int m = 5 * k - 30; m < 5 * k; ++m) ex = d > 0 ? std::max(ex, h[m]) : std::min(ex, l[m]);
                  const double back = d > 0 ? ex - BL[k] : BH[k] - ex; return back >= 0.1 * atr && (d > 0 ? C > BC[k - 1] : C < BC[k - 1]); }
        default: { const bool inside = BH[k - 1] <= BH[k - 2] && BL[k - 1] >= BL[k - 2]; return inside && (d > 0 ? C > BH[k - 1] : C < BL[k - 1]); } } };
    const int K0 = (R0 + 15) / 5 - 1, K1 = 1195 / 5;
    for (int md = 0; md < NMO; ++md) for (int en = 0; en < NEN; ++en) for (int ex = 0; ex < NEX; ++ex) {
      auto& Y = A[(md * NEN + en) * NEX + ex][yi]; int busy = 0, ntr = 0; bool lastwin = true; const double st = (ex ? 0.15 : 0.25) * atr;
      for (int k = K0; k < K1; ++k) { const int end = 5 * k + 5; if (ntr >= 3 || !lastwin) break; if (5 * k < busy) continue;
        const int d = mom(md, k); if (!d) continue; const double C = BC[k];
        if (d * (C - vw[end - 1]) <= 0 || d * (C - op) <= 0) continue; if (!entry(en, k, d)) continue;
        double stop = C - d * st, best = C; bool be = false; double pts = NAN; int xm = R0 + 385;
        for (int m = end; m < R0 + 385; ++m) { if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - C); xm = m + 1; break; }
          best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (ex == 0 && !be && d * (best - C) >= st) { be = true; stop = C + d * COST; } }
        if (std::isnan(pts)) pts = d * (c[R0 + 384] - C); pts -= COST;
        Y.n++; Y.r += pts / st; Y.pts += pts; busy = xm; ++ntr; lastwin = pts > 0; } } }
}
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series nq, es; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", nq, err) || !load_raw("data/edge_lab/cache", "ES", es, err)) return 1;
  build_days(nq, fomc, vix); build_days(es, fomc, vix);
  std::vector<std::array<YA, NY>> A, B; run(nq, 1.6, A); run(es, 0.65, B);
  auto sum = [&](const std::array<YA, NY>& y, int y0, int y1) { YA t; for (int k = y0; k <= y1; ++k) { t.n += y[k - Y0].n; t.r += y[k - Y0].r; t.pts += y[k - Y0].pts; } return t; };
  auto nm = [](int ci) { return std::string(MN[ci / (NEN * NEX)]) + " " + EN[ci / NEX % NEN] + " " + XN[ci % NEX]; };
  printf("Momentum definitions × entries × exits — NQ New York, cost 1.6 (ES cost 0.65). R/trade 2010-24 (eras 10-18 / 19-22 / 23-24) | ES 2010-24 | 2025-26\n");
  for (int ci = 0; ci < NC; ++ci) { YA a = sum(A[ci], 2010, 2024), e0 = sum(A[ci], 2010, 2018), e1 = sum(A[ci], 2019, 2022), e2 = sum(A[ci], 2023, 2024), hh = sum(A[ci], 2025, 2026), b = sum(B[ci], 2010, 2024);
    if (ci % (NEN * NEX) == 0) printf("\n");
    printf("  %-34s n %5.0f %+.3f (%+.3f / %+.3f / %+.3f)%s %+6.2f pts | ES %+.3f (n %.0f) | 25-26 %+.3f (n %.0f)\n", nm(ci).c_str(), a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n,
           (e0.r > 0 && e1.r > 0 && e2.r > 0) ? (b.r > 0 ? " **" : " * ") : "   ", a.pts / a.n, b.r / b.n, b.n, hh.r / hh.n, hh.n); }
  YA w; printf("\nWalk-forward over all %d (best total R of 2010..year-1, ≥ 100 trades):\n", NC);
  for (int y = 2013; y <= 2024; ++y) { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(A[ci], 2010, y - 1); if (a.n >= 100 && a.r > br) { br = a.r; best = ci; } }
    auto& a = A[best][y - Y0]; w.n += a.n; w.r += a.r; w.pts += a.pts; printf("    %d %-34s %+.3f (n %.0f)\n", y, nm(best).c_str(), a.r / a.n, a.n); }
  const int BASE = (0 * NEN + 2) * NEX + 0; YA b = sum(A[BASE], 2013, 2024);
  printf("  WF 2013-24 %+.3f R/trade, %+.2f pts (n %.0f) | current rule (M-EFF30 E-EMA20 X1) %+.3f R (n %.0f)\n", w.r / w.n, w.pts / w.n, w.n, b.r / b.n, b.n);
}
