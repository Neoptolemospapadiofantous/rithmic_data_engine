// 2026-10-17 EMA by hand: timeframe × length × pullback × trend × momentum × exit (research/preregistered/2026-10-17_ema_sweep.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, ND = 1320, R0 = 930; constexpr double COST = 1.6;
const int TFS[5] = {1, 2, 3, 5, 15}, LENS[6] = {9, 14, 20, 21, 34, 50};
const char* PBN[3] = {"TOUCH", "DEEP", "NEAR"}; const char* TRN[4] = {"-", "SLOPE", "ALIGN", "HTF15"}; const char* MON[4] = {"-", "RSI>50", "MACD>0", "IMPULSE"}; const char* EXN[2] = {"BE+1R hold", "0.5R target"};
constexpr int NC = 5 * 6 * 3 * 4 * 4 * 2;
struct YA { float n = 0, r = 0, pts = 0; };
static int cid(int tf, int ln, int pb, int tr, int mo, int ex) { return ((((tf * 6 + ln) * 3 + pb) * 4 + tr) * 4 + mo) * 2 + ex; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  std::vector<std::array<YA, NY>> A(NC);
  std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND), vw(ND);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = 0; } last = c[m]; } }
    if (tday_min(s.t[std::min(D.b1 - 1, first_bar_at(s, D, R0, D.b0))]) != R0) continue;
    { double pv = 0, vv = 0; for (int m = R0; m < ND; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
    const double op = o[R0], atr = D.atr;
    // plan direction
    int pwhen = 1 << 30, pd = 0; { const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { const double mv = c[R0 + CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = R0 + CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = -1e18, orl = 1e18; for (int m = R0; m < R0 + 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
        for (int k = 3; 5 * k + 5 <= 265; ++k) { double H = -1e18, L = 1e18; for (int m = R0 + 5 * k; m < R0 + 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); }
          if (g > 0 ? (L <= orh && c[R0 + 5 * k + 4] > orh) : (H >= orl && c[R0 + 5 * k + 4] < orl)) { if (R0 + 5 * k + 5 < pwhen) { pwhen = R0 + 5 * k + 5; pd = g; } break; } } } }
    if (!pd) continue; const int d = pd;
    // 15-min EMA-20 after each 15-min bar (by end minute)
    std::vector<double> e15(ND / 15 + 1, NAN); { double e = NAN; for (int k = 0; k < ND / 15; ++k) { const double cl = c[15 * k + 14]; e = std::isnan(e) ? cl : e + 2.0 / 21 * (cl - e); e15[k] = e; } }
    auto htf = [&](int endm) { const int k = endm / 15 - 1; return k >= 0 ? e15[k] : NAN; };
    auto sim = [&](int m0, double pe, double st, int ex) { double stop = pe - d * st, best = pe; bool be = false;
      for (int m = m0; m < R0 + 385; ++m) { if (d > 0 ? l[m] <= stop : h[m] >= stop) return std::make_pair(d * (stop - pe) - COST, m + 1);
        if (ex == 1 && (d > 0 ? h[m] >= pe + 0.5 * st : l[m] <= pe - 0.5 * st)) return std::make_pair(0.5 * st - COST, m + 1);
        best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (ex == 0 && !be && d * (best - pe) >= st) { be = true; stop = pe + d * COST; } }
      return std::make_pair(d * (c[R0 + 384] - pe) - COST, R0 + 385); };
    const double st = 0.25 * atr;
    for (int ti = 0; ti < 5; ++ti) { const int TF = TFS[ti], NB = ND / TF;
      std::vector<double> BO(NB), BH(NB), BL(NB), BC(NB); for (int k = 0; k < NB; ++k) { BO[k] = o[k * TF]; BH[k] = -1e18; BL[k] = 1e18; for (int m = k * TF; m < k * TF + TF; ++m) { BH[k] = std::max(BH[k], h[m]); BL[k] = std::min(BL[k], l[m]); } BC[k] = c[k * TF + TF - 1]; }
      auto ema = [&](int n) { std::vector<double> e(NB); double x = BC[0]; for (int k = 0; k < NB; ++k) { x = k ? x + 2.0 / (n + 1) * (BC[k] - x) : BC[0]; e[k] = x; } return e; };
      std::array<std::vector<double>, 6> E; for (int q = 0; q < 6; ++q) E[q] = ema(LENS[q]);
      std::vector<double> e12 = ema(12), e26 = ema(26), mh(NB), rsi(NB, 50); { double sig = 0; for (int k = 0; k < NB; ++k) { const double mc = e12[k] - e26[k]; sig = k ? sig + 0.2 * (mc - sig) : mc; mh[k] = mc - sig; }
        double up = 0, dn = 0; for (int k = 1; k < NB; ++k) { const double ch = BC[k] - BC[k - 1]; if (k <= 14) { up += std::max(ch, 0.0) / 14; dn += std::max(-ch, 0.0) / 14; } else { up = (up * 13 + std::max(ch, 0.0)) / 14; dn = (dn * 13 + std::max(-ch, 0.0)) / 14; }
          rsi[k] = up + dn > 0 ? 100 * up / (up + dn) : 50; } }
      const int k0 = (R0 + 15) / TF;   // first bar starting at or after 09:45 (all TFs divide 945 minutes from 18:00? use ≥)
      for (int ln = 0; ln < 6; ++ln) for (int pb = 0; pb < 3; ++pb) for (int tr = 0; tr < 4; ++tr) for (int mo = 0; mo < 4; ++mo) for (int ex = 0; ex < 2; ++ex) {
        auto& Y = A[cid(ti, ln, pb, tr, mo, ex)][yi]; int busy = 0, ntr = 0; bool lastwin = true; const auto& e = E[ln];
        for (int k = std::max(k0, 12); k < NB; ++k) { const int endm = k * TF + TF; if (endm > R0 + 265) break; if (ntr >= 3 || !lastwin) break;
          if (endm < pwhen || k * TF < busy || k * TF < R0 + 15) continue;
          const double ep = e[k - 1], C = BC[k], tol = pb == 1 ? -0.03 * atr : pb == 2 ? 0.03 * atr : 0;
          if (!(d > 0 ? (BL[k] <= ep + tol && C > ep) : (BH[k] >= ep - tol && C < ep))) continue;
          if (d * (C - vw[endm - 1]) <= 0 || d * (C - op) <= 0) continue;
          if (tr == 1 && !(d * (e[k - 1] - e[k - 4]) > 0)) continue;
          if (tr == 2 && !(d * (E[0][k - 1] - E[3][k - 1]) > 0)) continue;
          if (tr == 3) { const double hv = htf(endm); if (std::isnan(hv) || d * (C - hv) <= 0) continue; }
          if (mo == 1 && !(d * (rsi[k] - 50) > 0)) continue;
          if (mo == 2 && !(d * mh[k] > 0)) continue;
          if (mo == 3) { double hh = d > 0 ? -1e18 : 1e18, ll = d > 0 ? 1e18 : -1e18; for (int q = k - 5; q <= k; ++q) hh = d > 0 ? std::max(hh, BH[q]) : std::min(hh, BL[q]);
            for (int q = k - 11; q <= k - 6; ++q) ll = d > 0 ? std::min(ll, BL[q]) : std::max(ll, BH[q]); if (d * (hh - ll) < 0.15 * atr) continue; }
          auto [pts, xm] = sim(endm, C, st, ex); Y.n++; Y.r += pts / st; Y.pts += pts; busy = xm; ++ntr; lastwin = pts > 0; k = std::max(k, (xm - 1) / TF); } } } }
  auto sum = [&](int ci, int y0, int y1) { YA t; for (int y = y0; y <= y1; ++y) { auto& a = A[ci][y - Y0]; t.n += a.n; t.r += a.r; t.pts += a.pts; } return t; };
  auto nm = [&](int ci) { int ex = ci % 2, mo = ci / 2 % 4, tr = ci / 8 % 4, pb = ci / 32 % 3, ln = ci / 96 % 6, tf = ci / 576; char b[128];
    snprintf(b, 128, "%2dm EMA%-2d %-5s trend %-5s mom %-7s %s", TFS[tf], LENS[ln], PBN[pb], TRN[tr], MON[mo], EXN[ex]); return std::string(b); };
  auto allpos = [&](int ci) { return sum(ci, 2010, 2018).r > 0 && sum(ci, 2019, 2022).r > 0 && sum(ci, 2023, 2024).r > 0; };
  auto line = [&](int ci) { YA a = sum(ci, 2010, 2024), e0 = sum(ci, 2010, 2018), e1 = sum(ci, 2019, 2022), e2 = sum(ci, 2023, 2024); int yp = 0; for (int y = 2010; y <= 2024; ++y) yp += A[ci][y - Y0].r > 0;
    printf("  %-62s n %5.0f %+.3f R (%+.3f / %+.3f / %+.3f) %+6.2f pts yrs+ %2d/15\n", nm(ci).c_str(), a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n, a.pts / a.n, yp); };
  printf("EMA sweep, %d combinations, cost %.1f pts, 2010-2024, R per trade (eras 2010-18 / 2019-22 / 2023-24)\nTODAY'S PANEL RULE:\n", NC, COST); line(cid(3, 1, 0, 0, 0, 0));
  std::vector<std::pair<double, int>> rk; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, 2024); if (a.n >= 300) rk.push_back({a.r / a.n, ci}); } std::sort(rk.rbegin(), rk.rend());
  printf("TOP 20 (n ≥ 300):\n"); for (int q = 0; q < 20 && q < (int)rk.size(); ++q) line(rk[q].second);
  int cells = 0, pos = 0, acells = 0, apos = 0; for (auto& [r, ci] : rk) { ++cells; pos += allpos(ci); const bool al = ci / 8 % 4 && ci / 2 % 4; acells += al; apos += al && allpos(ci); }
  printf("cells positive in all three eras: %d of %d (chance alone ≈ %d) · trend AND momentum cells: %d of %d (chance ≈ %d)\n", pos, cells, cells / 8, apos, acells, acells / 8);
  printf("TOP 10 'trend AND momentum align':\n"); { int q = 0; for (auto& [r, ci] : rk) if (ci / 8 % 4 && ci / 2 % 4) { line(ci); if (++q == 10) break; } }
  // marginals: mean R/trade over cells of each value (n ≥ 300)
  auto marg = [&](const char* nm2, int nv, auto key, auto lab) { printf("  %-9s", nm2); for (int vv = 0; vv < nv; ++vv) { double sr = 0, sn = 0; for (auto& [r, ci] : rk) if (key(ci) == vv) { YA a = sum(ci, 2010, 2024); sr += a.r; sn += a.n; } printf(" %s %+.3f |", lab(vv).c_str(), sr / sn); } printf("\n"); };
  printf("Average by dimension (all cells pooled):\n");
  marg("timeframe", 5, [](int ci) { return ci / 576; }, [](int v2) { return std::to_string(TFS[v2]) + "m"; });
  marg("length", 6, [](int ci) { return ci / 96 % 6; }, [](int v2) { return "EMA" + std::to_string(LENS[v2]); });
  marg("pullback", 3, [](int ci) { return ci / 32 % 3; }, [](int v2) { return std::string(PBN[v2]); });
  marg("trend", 4, [](int ci) { return ci / 8 % 4; }, [](int v2) { return std::string(TRN[v2]); });
  marg("momentum", 4, [](int ci) { return ci / 2 % 4; }, [](int v2) { return std::string(MON[v2]); });
  marg("exit", 2, [](int ci) { return ci % 2; }, [](int v2) { return std::string(EXN[v2]); });
  auto wf = [&](bool align_only) { YA w; printf("Walk-forward%s:\n", align_only ? " (trend AND momentum cells only)" : " (all cells)");
    for (int y = 2013; y <= 2024; ++y) { int best = -1; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { if (align_only && !(ci / 8 % 4 && ci / 2 % 4)) continue; YA a = sum(ci, 2010, y - 1); if (a.n >= 150 && a.r > br) { br = a.r; best = ci; } }
      auto& a = A[best][y - Y0]; w.n += a.n; w.r += a.r; w.pts += a.pts; printf("    %d %-62s %+6.3f R (n %3.0f)\n", y, nm(best).c_str(), a.r / a.n, a.n); }
    printf("  WF 2013-24 %+.3f R/trade, %+.2f pts (n %.0f)\n", w.r / w.n, w.pts / w.n, w.n);
    int best = -1; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { if (align_only && !(ci / 8 % 4 && ci / 2 % 4)) continue; YA a = sum(ci, 2010, 2024); if (a.n >= 150 && a.r > br) { br = a.r; best = ci; } }
    YA hh = sum(best, 2025, 2026); printf("  HOLDOUT 2025-26 (one look) %s: %+.3f R, %+.2f pts (n %.0f)\n", nm(best).c_str(), hh.r / hh.n, hh.pts / hh.n, hh.n); };
  wf(false); wf(true);
}
