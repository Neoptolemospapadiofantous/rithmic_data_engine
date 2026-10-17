// 2026-10-17 EMA-14 hand trade: entries × stops × trailing exits (research/preregistered/2026-10-17_ema_entries_trails.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, NM = 386, LAST = 265, NK = 77; constexpr double COST = 1.6;
struct YA { double n = 0, r = 0, pts = 0, win = 0; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  const char* EN[4] = {"E0 touch-bar close", "E1 stop above bar high", "E2 limit at EMA", "E3 confirm bar close"};
  const char* SN[3] = {"0.25ATR", "0.15ATR", "swing10-2"};
  const char* XN[8] = {"X0 2R", "X1 2R+BE", "X2 BE hold", "X3 trail .25ATR", "X4 BE+trail 1R", "X5 swing3 trail", "X6 EMA exit", "X7 VWAP exit+2R"};
  const int NC = 4 * 3 * 8; std::vector<std::array<YA, NY>> A(NC);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc)) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
    std::array<double, NM> o, h, l, c, vw; { double pv = 0, vv = 0, last = s.o[i0]; int j = i0;
      for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; pv += (h[m] + l[m] + c[m]) / 3 * std::max(1.0f, s.v[j]); vv += std::max(1.0f, s.v[j]); }
        else o[m] = h[m] = l[m] = c[m] = last;
        last = c[m]; vw[m] = vv > 0 ? pv / vv : last; } }
    const double op = o[0], atr = D.atr;
    // EMA-14 of 5-min closes from 18:00: emaB[k] = EMA before RTH bar k (k = 0 → 09:30), emaB[k+1] = EMA after bar k; pre-RTH lows for swings
    std::array<double, NK + 2> emaB; emaB.fill(NAN); std::vector<double> preL, preH; { double e = NAN, lc = NAN; int bk = -1;
      for (int i = D.b0; i < i0; ++i) { const int b = tday_min(s.t[i]) / 5; if (b != bk) { if (!std::isnan(lc)) e = std::isnan(e) ? lc : e + (2.0 / 15) * (lc - e); bk = b; preL.push_back(s.l[i]); preH.push_back(s.h[i]); }
        else { preL.back() = std::min<double>(preL.back(), s.l[i]); preH.back() = std::max<double>(preH.back(), s.h[i]); } lc = s.c[i]; }
      if (!std::isnan(lc)) e = std::isnan(e) ? lc : e + (2.0 / 15) * (lc - e); emaB[0] = e; }
    std::array<double, NK + 1> H5, L5, C5; for (int k = 0; k <= NK; ++k) { H5[k] = -1e18; L5[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5 && m < NM; ++m) { H5[k] = std::max(H5[k], h[m]); L5[k] = std::min(L5[k], l[m]); } C5[k] = c[std::min(5 * k + 4, NM - 1)];
      emaB[k + 1] = std::isnan(emaB[k]) ? C5[k] : emaB[k] + (2.0 / 15) * (C5[k] - emaB[k]); }
    auto swing = [&](int k, int n, int d) { double v = d > 0 ? 1e18 : -1e18; for (int q = 0; q < n; ++q) { const int kk = k - q; double x;
        if (kk >= 0) x = d > 0 ? L5[kk] : H5[kk]; else { const int pi = (int)preL.size() + kk; if (pi < 0) break; x = d > 0 ? preL[pi] : preH[pi]; } v = d > 0 ? std::min(v, x) : std::max(v, x); } return v; };
    int pwhen = 1 << 30, pd = 0; { const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { double mv = c[CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = std::max({H5[0], H5[1], H5[2]}), orl = std::min({L5[0], L5[1], L5[2]});
        for (int k = 3; 5 * k + 5 <= LAST; ++k) if (g > 0 ? (L5[k] <= orh && C5[k] > orh) : (H5[k] >= orl && C5[k] < orl)) { if (5 * k + 5 < pwhen) { pwhen = 5 * k + 5; pd = g; } break; } } }
    if (!pd) continue; const int d = pd;
    // simulate from minute m0 (inclusive) at entry price pe with stop distance st; returns pts net, exit minute
    auto sim = [&](int m0, double pe, double st, int x) -> std::pair<double, int> {
      double stop = pe - d * st, best = pe; const double tgt = (x == 0 || x == 1 || x == 7) ? pe + d * 2 * st : NAN; bool be = false;
      for (int m = m0; m < NM - 1; ++m) {
        if (d > 0 ? l[m] <= stop : h[m] >= stop) return {d * (stop - pe) - COST, m + 1};
        if (!std::isnan(tgt) && (d > 0 ? h[m] >= tgt : l[m] <= tgt)) return {d * (tgt - pe) - COST, m + 1};
        best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); const double fav = d * (best - pe);
        if ((x == 1 || x == 2 || x == 4) && !be && fav >= st) { be = true; stop = d > 0 ? std::max(stop, pe + COST) : std::min(stop, pe - COST); }
        if (x == 3) stop = d > 0 ? std::max(stop, best - 0.25 * atr) : std::min(stop, best + 0.25 * atr);
        if (x == 4 && be) stop = d > 0 ? std::max(stop, best - st) : std::min(stop, best + st);
        if ((m + 1) % 5 == 0) { const int k = (m + 1) / 5 - 1;   // 5-min bar k just closed
          if (x == 5) { const double sw = swing(k, 3, d); stop = d > 0 ? std::max(stop, sw) : std::min(stop, sw); }
          if (x == 6 && d * (C5[k] - emaB[k + 1]) < 0) return {d * (C5[k] - pe) - COST, m + 1};
          if (x == 7 && d * (C5[k] - vw[m]) < 0) return {d * (C5[k] - pe) - COST, m + 1}; } }
      return {d * (c[NM - 2] - pe) - COST, NM - 1}; };
    for (int e = 0; e < 4; ++e) for (int sx = 0; sx < 3; ++sx) for (int x = 0; x < 8; ++x) {
      auto& Y = A[(e * 3 + sx) * 8 + x][yi]; int busy = 0, ntr = 0; bool lastwin = true;
      for (int k = 3; k < NK; ++k) {
        if (ntr >= 3 || !lastwin) break; if (5 * k < busy) continue;
        int m0 = -1; double pe = NAN; int sigk = k;
        const double ema = emaB[k];
        if (e == 2) {   // limit at the EMA during bar k; permission at the previous close
          const double pc5 = C5[k - 1]; if (5 * k < pwhen || 5 * k > LAST) continue;
          if (!(d * (pc5 - ema) > 0 && d * (pc5 - vw[5 * k - 1]) > 0 && d * (pc5 - op) > 0)) continue;
          for (int m = 5 * k; m < 5 * k + 5; ++m) if (d > 0 ? l[m] <= ema : h[m] >= ema) { m0 = m; pe = d > 0 ? std::min(ema, o[m]) : std::max(ema, o[m]); break; }
          if (m0 < 0) continue; sigk = k - 1;
        } else {
          const int me = 5 * k + 5; if (me > LAST || me < pwhen) continue;
          const bool touch = d > 0 ? (L5[k] <= ema && C5[k] > ema) : (H5[k] >= ema && C5[k] < ema); if (!touch) continue;
          if (d * (C5[k] - vw[me - 1]) <= 0 || d * (C5[k] - op) <= 0) continue;
          if (e == 0) { m0 = me; pe = C5[k]; }
          else if (e == 1) { const double trig = d > 0 ? H5[k] + 0.25 : L5[k] - 0.25;
            for (int m = me; m < me + 10 && m < NM - 1 && m <= LAST + 5; ++m) if (d > 0 ? h[m] >= trig : l[m] <= trig) { m0 = m; pe = d > 0 ? std::max(trig, o[m]) : std::min(trig, o[m]); break; }
            if (m0 < 0) continue; }
          else { if (k + 1 >= NK || 5 * k + 10 > LAST) continue; if (d * (C5[k + 1] - emaB[k + 2]) <= 0) continue; m0 = 5 * k + 10; pe = C5[k + 1]; sigk = k + 1; }
        }
        double st = sx == 0 ? 0.25 * atr : sx == 1 ? 0.15 * atr : d * (pe - (swing(sigk, 10, d) - d * 2.0));
        if (st <= 0) continue; st = std::min(st, 0.25 * atr);
        auto [pts, xm] = sim(m0, pe, st, x); Y.n++; Y.r += pts / st; Y.pts += pts; Y.win += pts > 0; busy = xm; ++ntr; lastwin = pts > 0;
        k = std::max(k, (xm + 4) / 5 - 1); } } }
  auto sum = [&](int ci, int y0, int y1) { YA t; for (int y = y0; y <= y1; ++y) { auto& a = A[ci][y - Y0]; t.n += a.n; t.r += a.r; t.pts += a.pts; t.win += a.win; } return t; };
  auto nm = [&](int ci) { return std::string(EN[ci / 24]) + " | " + SN[(ci / 8) % 3] + " | " + XN[ci % 8]; };
  auto line = [&](int ci) { YA a = sum(ci, 2010, 2024), e0 = sum(ci, 2010, 2018), e1 = sum(ci, 2019, 2022), e2 = sum(ci, 2023, 2024), h = sum(ci, 2025, 2026); int yp = 0; for (int y = 2010; y <= 2024; ++y) yp += A[ci][y - Y0].r > 0;
    printf("  %-58s n %5.0f win %4.1f%% %+.3f R (eras %+.3f/%+.3f/%+.3f) %+6.2f pts yrs+ %2d/15\n", nm(ci).c_str(), a.n, 100 * a.win / a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n, a.pts / a.n, yp); (void)h; };
  printf("EMA-14 hand trade, %d combinations, cost %.1f pts, 2010-2024 (R = per unit of risk; pts = NQ points)\nTODAY: ", NC, COST); line(0);
  std::vector<std::pair<double, int>> rk; for (int ci = 0; ci < NC; ++ci) rk.push_back({sum(ci, 2010, 2024).r / std::max(1.0, sum(ci, 2010, 2024).n), ci}); std::sort(rk.rbegin(), rk.rend());
  printf("TOP 15 by R/trade:\n"); for (int q = 0; q < 15; ++q) line(rk[q].second);
  printf("Best per ENTRY:\n"); for (int e = 0; e < 4; ++e) { for (auto& [r, ci] : rk) if (ci / 24 == e) { line(ci); break; } }
  printf("Best per EXIT:\n"); for (int x = 0; x < 8; ++x) { for (auto& [r, ci] : rk) if (ci % 8 == x) { line(ci); break; } }
  printf("Best per STOP:\n"); for (int sx = 0; sx < 3; ++sx) { for (auto& [r, ci] : rk) if ((ci / 8) % 3 == sx) { line(ci); break; } }
  YA wf; printf("Walk-forward (best total R of 2010..year-1):\n");
  for (int y = 2013; y <= 2024; ++y) { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, y - 1); if (a.n > 100 && a.r > br) { br = a.r; best = ci; } }
    auto& a = A[best][y - Y0]; wf.n += a.n; wf.r += a.r; wf.pts += a.pts; printf("    %d %-58s %+6.3f R (n %3.0f) | today %+6.3f\n", y, nm(best).c_str(), a.r / a.n, a.n, A[0][y - Y0].r / A[0][y - Y0].n); }
  YA t = sum(0, 2013, 2024); printf("  WF 2013-24 %+.3f R/trade %+.2f pts (n %.0f) | today %+.3f R %+.2f pts\n", wf.r / wf.n, wf.pts / wf.n, wf.n, t.r / t.n, t.pts / t.n);
  { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, 2024); if (a.n > 100 && a.r > br) { br = a.r; best = ci; } }
    YA hh = sum(best, 2025, 2026), hb = sum(0, 2025, 2026);
    printf("HOLDOUT 2025-26 (one look) %s: %+.3f R %+.2f pts (n %.0f) | today %+.3f R %+.2f pts (n %.0f)\n", nm(best).c_str(), hh.r / hh.n, hh.pts / hh.n, hh.n, hb.r / hb.n, hb.pts / hb.n, hb.n); }
}
