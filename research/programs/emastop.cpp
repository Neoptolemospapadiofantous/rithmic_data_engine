// 2026-10-17 EMA-14 hand trade with structural stops (research/preregistered/2026-10-17_ema_stops.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, NM = 386, LAST = 265; constexpr double COST = 1.6;
struct StopCfg { int type; int n; double buf, floor; std::string name; };   // type 0 ATR(n=k*100) 1 BAR 2 SWING 3 PIVOT 4 EMA
struct YA { double n = 0, r = 0, pts = 0, win = 0, stop = 0; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  std::vector<StopCfg> SC = {{0, 25, 0, 0, "ATR 0.25 (today)"}, {0, 15, 0, 0, "ATR 0.15"}, {0, 10, 0, 0, "ATR 0.10"}};
  for (double b : {0.0, 2.0}) for (double f : {0.0, 0.03}) {
    auto tag = [&](std::string x) { return x + (b > 0 ? " -2pt" : "") + (f > 0 ? " floor.03" : ""); };
    SC.push_back({1, 0, b, f, tag("BAR low")}); for (int n : {3, 5, 10}) SC.push_back({2, n, b, f, tag("SWING-" + std::to_string(n))});
    SC.push_back({3, 0, b, f, tag("PIVOT")}); }
  SC.push_back({4, 0, 0, 0, "EMA-0.05ATR"}); SC.push_back({4, 0, 0, 0.03, "EMA-0.05ATR floor.03"});
  const double TG[5] = {1, 1.5, 2, 3, 0}; const char* TN[5] = {"1R", "1.5R", "2R", "3R", "no tgt"};
  const int NC = SC.size() * 5; std::vector<std::array<YA, NY>> A(NC);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc)) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
    std::array<double, NM> o, h, l, c, vw; { double pv = 0, vv = 0, last = s.o[i0]; int j = i0;
      for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; pv += (h[m] + l[m] + c[m]) / 3 * std::max(1.0f, s.v[j]); vv += std::max(1.0f, s.v[j]); }
        else o[m] = h[m] = l[m] = c[m] = last;
        last = c[m]; vw[m] = vv > 0 ? pv / vv : last; } }
    const double op = o[0], atr = D.atr;
    // full-day 5-min bars from 18:00 with the EMA-14 before each bar
    std::vector<double> O5, H5, L5, C5, EB; std::vector<int> T5; { double e = NAN; int bk = -1;
      for (int i = D.b0; i < D.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(16, 0)) break; const int b = tm / 5;
        if (b != bk) { if (!C5.empty()) e = std::isnan(e) ? C5.back() : e + (2.0 / 15) * (C5.back() - e); bk = b; T5.push_back(b * 5); O5.push_back(s.o[i]); H5.push_back(s.h[i]); L5.push_back(s.l[i]); C5.push_back(s.c[i]); EB.push_back(e); }
        else { H5.back() = std::max<double>(H5.back(), s.h[i]); L5.back() = std::min<double>(L5.back(), s.l[i]); C5.back() = s.c[i]; } } }
    // plan direction
    int pwhen = 1 << 30, pd = 0; { auto px = [&](int m) { return c[m - 1]; }; const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { double mv = px(CM[q]) - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = -1e18, orl = 1e18; for (int m = 0; m < 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
        for (int k = 3; 5 * k + 5 <= LAST; ++k) { double H = -1e18, L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); }
          if (g > 0 ? (L <= orh && c[5 * k + 4] > orh) : (H >= orl && c[5 * k + 4] < orl)) { if (5 * k + 5 < pwhen) { pwhen = 5 * k + 5; pd = g; } break; } } } }
    if (!pd) continue;
    auto run = [&](int me, int d, double st, double tm) { const double p = c[me - 1];
      for (int m = me; m < NM - 1; ++m) { if (d > 0 ? l[m] <= p - st : h[m] >= p + st) return std::make_pair(-st - COST, m + 1);
        if (tm > 0 && (d > 0 ? h[m] >= p + tm * st : l[m] <= p - tm * st)) return std::make_pair(tm * st - COST, m + 1); }
      return std::make_pair(d * (c[NM - 2] - p) - COST, NM - 1); };
    for (size_t si = 0; si < SC.size(); ++si) for (int ti = 0; ti < 5; ++ti) { const StopCfg& sc = SC[si]; auto& Y = A[si * 5 + ti][yi];
      int busy = 0, ntr = 0; bool lastwin = true;
      for (size_t j = 1; j < T5.size(); ++j) { const int k = (T5[j] - hm_to_tday(9, 30)) / 5; if (T5[j] < hm_to_tday(9, 45) || (T5[j] - hm_to_tday(9, 30)) % 5) continue;
        const int me = 5 * k + 5; if (me > LAST || me < busy || me < pwhen || ntr >= 3 || !lastwin) continue;
        const double e = EB[j]; if (std::isnan(e)) continue; const double C = C5[j];
        const int sig = (L5[j] <= e && C > e) ? 1 : (H5[j] >= e && C < e) ? -1 : 0; if (sig != pd) continue;
        if (pd * (C - vw[me - 1]) <= 0 || pd * (C - op) <= 0) continue;
        double lvl = NAN;
        if (sc.type == 0) lvl = C - pd * sc.n / 100.0 * atr;
        else if (sc.type == 1) lvl = pd > 0 ? L5[j] : H5[j];
        else if (sc.type == 2) { lvl = pd > 0 ? 1e18 : -1e18; for (int q = 0; q < sc.n && (int)j - q >= 0; ++q) lvl = pd > 0 ? std::min(lvl, L5[j - q]) : std::max(lvl, H5[j - q]); }
        else if (sc.type == 3) { for (int i = (int)j - 2; i >= 2 && i >= (int)j - 24; --i) { const bool piv = pd > 0 ? (L5[i] < L5[i - 1] && L5[i] < L5[i - 2] && L5[i] < L5[i + 1] && L5[i] < L5[i + 2])
                                                                                                   : (H5[i] > H5[i - 1] && H5[i] > H5[i - 2] && H5[i] > H5[i + 1] && H5[i] > H5[i + 2]); if (piv) { lvl = pd > 0 ? L5[i] : H5[i]; break; } } }
        else lvl = e - pd * 0.05 * atr;
        if (std::isnan(lvl)) continue;
        double st = pd * (C - lvl) + (sc.type ? sc.buf : 0); if (st <= 0) continue;
        st = std::max(st, sc.floor * atr); st = std::min(st, 0.25 * atr);
        auto [pts, xm] = run(me, pd, st, TG[ti]); Y.n++; Y.r += pts / st; Y.pts += pts; Y.win += pts > 0; Y.stop += st; busy = xm; ++ntr; lastwin = pts > 0; } } }
  auto sum = [&](int ci, int y0, int y1) { YA t; for (int y = y0; y <= y1; ++y) { auto& a = A[ci][y - Y0]; t.n += a.n; t.r += a.r; t.pts += a.pts; t.win += a.win; t.stop += a.stop; } return t; };
  auto nm = [&](int ci) { return SC[ci / 5].name + " / " + TN[ci % 5]; };
  auto line = [&](int ci) { YA a = sum(ci, 2010, 2024), e0 = sum(ci, 2010, 2018), e1 = sum(ci, 2019, 2022), e2 = sum(ci, 2023, 2024); int yp = 0; for (int y = 2010; y <= 2024; ++y) yp += A[ci][y - Y0].r > 0;
    printf("  %-34s n %5.0f  win %4.1f%%  %+.3f R/trade (eras %+.3f / %+.3f / %+.3f)  %+6.2f pts  stop 23-24 %5.1f pts  yrs+ %2d/15\n",
           nm(ci).c_str(), a.n, 100 * a.win / a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n, a.pts / a.n, e2.stop / e2.n, yp); };
  printf("EMA-14 panel rule, cost %.1f pts/trade, 2010-2024. %d combinations.\nTODAY'S RULE:\n", COST, NC); line(2);
  std::vector<std::pair<double, int>> rk; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, 2024); if (a.n > 300) rk.push_back({a.r, ci}); } std::sort(rk.rbegin(), rk.rend());
  printf("TOP 20 by total R (in-sample, 2010-24):\n"); for (int q = 0; q < 20 && q < (int)rk.size(); ++q) line(rk[q].second);
  printf("BOTTOM 5:\n"); for (int q = 0; q < 5; ++q) line(rk[rk.size() - 1 - q].second);
  printf("By stop type, best target each (total R / R per trade):\n");
  for (size_t si = 0; si < SC.size(); ++si) { int bt = 0; double br = -1e18; for (int ti = 0; ti < 5; ++ti) { double r = sum(si * 5 + ti, 2010, 2024).r; if (r > br) { br = r; bt = ti; } } line(si * 5 + bt); }
  // walk-forward
  YA wf, base; printf("Walk-forward (each year trades the best total-R combination of 2010..year-1):\n");
  for (int y = 2013; y <= 2024; ++y) { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, y - 1); if (a.n > 100 && a.r > br) { br = a.r; best = ci; } }
    auto& a = A[best][y - Y0]; wf.n += a.n; wf.r += a.r; wf.pts += a.pts; auto& b = A[2][y - Y0]; base.n += b.n; base.r += b.r; base.pts += b.pts;
    printf("    %d  %-34s %+6.3f R/trade (n %3.0f)   today's rule %+6.3f\n", y, nm(best).c_str(), a.r / a.n, a.n, b.r / b.n); }
  printf("  WF 2013-24: %+.3f R/trade, %+.2f pts/trade (n %.0f, total %+.1f R) | today's rule %+.3f R, %+.2f pts (total %+.1f R)\n", wf.r / wf.n, wf.pts / wf.n, wf.n, wf.r, base.r / base.n, base.pts / base.n, base.r);
  { int best = 0; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(ci, 2010, 2024); if (a.n > 100 && a.r > br) { br = a.r; best = ci; } }
    YA h = sum(best, 2025, 2026), hb = sum(2, 2025, 2026);
    printf("HOLDOUT 2025-26 (one look) for %s: %+.3f R/trade, %+.2f pts (n %.0f) | today's rule %+.3f R, %+.2f pts (n %.0f)\n", nm(best).c_str(), h.r / h.n, h.pts / h.n, h.n, hb.r / hb.n, hb.pts / hb.n, hb.n); }
}
