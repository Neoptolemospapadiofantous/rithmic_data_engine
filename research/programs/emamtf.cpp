// 2026-10-17 EMA by hand: timeframe × length × pullback × trend × momentum × exit (research/preregistered/2026-10-17_ema_sweep.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, ND = 1320, R0 = 930; constexpr double COST = 1.6;
const int TFS[5] = {1, 2, 3, 5, 15}, LENS[6] = {9, 14, 20, 21, 34, 50};
const char* PBN[3] = {"TOUCH", "DEEP", "NEAR"}; const char* TRN[4] = {"-", "SLOPE", "ALIGN", "HTF15"}; const char* MON[4] = {"-", "RSI>50", "MACD>0", "IMPULSE"}; const char* EXN[2] = {"BE+1R hold", "0.5R target"};
constexpr int NC = 6 * 4 * 2;
struct YA { float n = 0, r = 0, pts = 0; };
static int cid(int tf, int ln, int pb, int tr, int mo, int ex) { return ((((tf * 6 + ln) * 3 + pb) * 4 + tr) * 4 + mo) * 2 + ex; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  std::vector<std::array<YA, NY>> A(NC);
  const char* DN[6] = {"D-PLAN", "D-EMA 1m 9>21", "D-EMA50 1m", "D-MOM 1m 30min", "D-BOTH plan+1m EMA", "D-NONE"};
  const char* EN[4] = {"5m EMA9 near", "5m EMA14 near", "5m EMA20 near", "5m break prev bar"};
  std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND), vw(ND);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = 0; } last = c[m]; } }
    if (tday_min(s.t[std::min(D.b1 - 1, first_bar_at(s, D, R0, D.b0))]) != R0) continue;
    { double pv = 0, vv = 0; for (int m = R0; m < ND; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
    const double op = o[R0], atr = D.atr;
    int pwhen = 1 << 30, pd = 0; { const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { const double mv = c[R0 + CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = R0 + CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = -1e18, orl = 1e18; for (int m = R0; m < R0 + 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
        for (int k = 3; 5 * k + 5 <= 265; ++k) { double H = -1e18, L = 1e18; for (int m = R0 + 5 * k; m < R0 + 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); }
          if (g > 0 ? (L <= orh && c[R0 + 5 * k + 4] > orh) : (H >= orl && c[R0 + 5 * k + 4] < orl)) { if (R0 + 5 * k + 5 < pwhen) { pwhen = R0 + 5 * k + 5; pd = g; } break; } } } }
    // 1-minute EMAs (value after minute m)
    std::vector<double> e9(ND), e21(ND), e50(ND); { double a = c[0], b = c[0], d5 = c[0]; for (int m = 0; m < ND; ++m) { a += 0.2 * (c[m] - a); b += 2.0 / 22 * (c[m] - b); d5 += 2.0 / 51 * (c[m] - d5); e9[m] = a; e21[m] = b; e50[m] = d5; } }
    // 5-minute bars + EMAs
    const int NB = ND / 5; std::vector<double> BH(NB), BL(NB), BC(NB); for (int k = 0; k < NB; ++k) { BH[k] = -1e18; BL[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { BH[k] = std::max(BH[k], h[m]); BL[k] = std::min(BL[k], l[m]); } BC[k] = c[5 * k + 4]; }
    std::array<std::vector<double>, 3> E5; const int LN[3] = {9, 14, 20}; for (int q = 0; q < 3; ++q) { E5[q].resize(NB); double x = BC[0]; for (int k = 0; k < NB; ++k) { x += 2.0 / (LN[q] + 1) * (BC[k] - x); E5[q][k] = x; } }
    const double st = 0.25 * atr;
    for (int dm = 0; dm < 6; ++dm) for (int en = 0; en < 4; ++en) for (int ex = 0; ex < 2; ++ex) {
      auto& Y = A[(dm * 4 + en) * 2 + ex][yi]; int busy = 0, ntr = 0; bool lastwin = true;
      for (int k = (R0 + 15) / 5; k < NB; ++k) { const int endm = 5 * k + 5; if (endm > R0 + 265) break; if (ntr >= 3 || !lastwin) break; if (5 * k < busy) continue;
        const double C = BC[k]; int sig = 0;
        if (en < 3) { const double ep = E5[en][k - 1], tol = 0.03 * atr; if (BL[k] <= ep + tol && C > ep) sig = 1; else if (BH[k] >= ep - tol && C < ep) sig = -1; }
        else { if (C > BH[k - 1]) sig = 1; else if (C < BL[k - 1]) sig = -1; }
        if (!sig) continue;
        const int mm = endm - 1; int dir = 0;
        if (dm == 0) dir = endm >= pwhen ? pd : 0;
        else if (dm == 1) dir = e9[mm] > e21[mm] ? 1 : e9[mm] < e21[mm] ? -1 : 0;
        else if (dm == 2) dir = c[mm] > e50[mm] ? 1 : c[mm] < e50[mm] ? -1 : 0;
        else if (dm == 3) { double path = 0; for (int q = mm - 29; q <= mm; ++q) path += std::fabs(c[q] - c[q - 1]); const double net = c[mm] - c[mm - 30]; dir = path > 0 && std::fabs(net) / path >= 0.3 ? (net > 0 ? 1 : -1) : 0; }
        else if (dm == 4) { const int d1 = e9[mm] > e21[mm] ? 1 : -1; dir = (endm >= pwhen && pd == d1) ? pd : 0; }
        else dir = sig;
        if (dir != sig) continue;
        if (sig * (C - vw[endm - 1]) <= 0 || sig * (C - op) <= 0) continue;
        double stop = C - sig * st, best = C; bool be = false; double pts = NAN; int xm = R0 + 385;
        for (int m = endm; m < R0 + 385; ++m) { if (sig > 0 ? l[m] <= stop : h[m] >= stop) { pts = sig * (stop - C); xm = m + 1; break; }
          if (ex == 1 && (sig > 0 ? h[m] >= C + 0.5 * st : l[m] <= C - 0.5 * st)) { pts = 0.5 * st; xm = m + 1; break; }
          best = sig > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (ex == 0 && !be && sig * (best - C) >= st) { be = true; stop = C + sig * COST; } }
        if (std::isnan(pts)) pts = sig * (c[R0 + 384] - C); pts -= COST;
        Y.n++; Y.r += pts / st; Y.pts += pts; busy = xm; ++ntr; lastwin = pts > 0; k = std::max(k, (xm - 1) / 5); } } }
  auto sum = [&](int ci, int y0, int y1) { YA t; for (int y = y0; y <= y1; ++y) { auto& a = A[ci][y - Y0]; t.n += a.n; t.r += a.r; t.pts += a.pts; } return t; };
  auto nm = [&](int ci) { return std::string(DN[ci / 8]) + " | " + EN[ci / 2 % 4] + " | " + (ci % 2 ? "0.5R target" : "BE+1R hold"); };
  printf("1-min direction → 5-min entry, cost 1.6, 2010-24, R/trade (eras 10-18 / 19-22 / 23-24) · pts/trade · 2025-26 (one look per row)\n");
  for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, 2024), e0 = sum(ci, 2010, 2018), e1 = sum(ci, 2019, 2022), e2 = sum(ci, 2023, 2024), hh = sum(ci, 2025, 2026);
    if (ci % 8 == 0) printf("\n");
    printf("  %-52s n %5.0f %+.3f R (%+.3f / %+.3f / %+.3f) %+6.2f pts | 25-26 %+.3f R (n %.0f)\n", nm(ci).c_str(), a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n, a.pts / a.n, hh.r / hh.n, hh.n); }
  YA w; printf("\nWalk-forward (best total R of 2010..year-1):\n");
  for (int y = 2013; y <= 2024; ++y) { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, y - 1); if (a.n >= 150 && a.r > br) { br = a.r; best = ci; } }
    auto& a = A[best][y - Y0]; w.n += a.n; w.r += a.r; printf("    %d %-52s %+.3f R (n %.0f)\n", y, nm(best).c_str(), a.r / a.n, a.n); }
  printf("  WF 2013-24 %+.3f R/trade (n %.0f)\n", w.r / w.n, w.n);
}
