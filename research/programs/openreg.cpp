// 2026-10-17 opening regime → EMA-14 trades (research/preregistered/2026-10-17_open_regime_ema.md).
#include "research/bars.hpp"
#include <array>
#include <map>
#include <cstdio>
#include <string>
using namespace lab;
constexpr int Y0 = 2010, NM = 386, LAST = 265, NK = 77, NR = 11; constexpr double COST = 1.6;
const char* RN[NR] = {"5m ER", "5m MOVE", "5m RANGE", "15m ER", "15m MOVE", "15m RANGE", "15m ACR", "30m ER", "30m MOVE", "30m RANGE", "30m ACR"};
const int RW[NR] = {0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2}; const int WM[3] = {5, 15, 30};
struct DayRec { int year; std::array<double, NR> rd; std::array<double, 6> r, n, pts; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  std::vector<DayRec> V;
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue;
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
    std::array<double, NM> o, h, l, c, vw; { double pv = 0, vv = 0, last = s.o[i0]; int j = i0;
      for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; pv += (h[m] + l[m] + c[m]) / 3 * std::max(1.0f, s.v[j]); vv += std::max(1.0f, s.v[j]); }
        else o[m] = h[m] = l[m] = c[m] = last;
        last = c[m]; vw[m] = vv > 0 ? pv / vv : last; } }
    const double op = o[0], atr = D.atr;
    std::array<double, NK + 2> emaB; { double e = NAN, lc = NAN; int bk = -1;
      for (int i = D.b0; i < i0; ++i) { const int b = tday_min(s.t[i]) / 5; if (b != bk) { if (!std::isnan(lc)) e = std::isnan(e) ? lc : e + (2.0 / 15) * (lc - e); bk = b; } lc = s.c[i]; }
      if (!std::isnan(lc)) e = std::isnan(e) ? lc : e + (2.0 / 15) * (lc - e); emaB[0] = e; }
    std::array<double, NK + 1> H5, L5, C5; for (int k = 0; k <= NK; ++k) { H5[k] = -1e18; L5[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5 && m < NM; ++m) { H5[k] = std::max(H5[k], h[m]); L5[k] = std::min(L5[k], l[m]); } C5[k] = c[std::min(5 * k + 4, NM - 1)];
      emaB[k + 1] = std::isnan(emaB[k]) ? C5[k] : emaB[k] + (2.0 / 15) * (C5[k] - emaB[k]); }
    int pwhen = 1 << 30, pd = 0; { const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { double mv = c[CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = std::max({H5[0], H5[1], H5[2]}), orl = std::min({L5[0], L5[1], L5[2]});
        for (int k = 3; 5 * k + 5 <= LAST; ++k) if (g > 0 ? (L5[k] <= orh && C5[k] > orh) : (H5[k] >= orl && C5[k] < orl)) { if (5 * k + 5 < pwhen) { pwhen = 5 * k + 5; pd = g; } break; } } }
    DayRec R; R.year = D.date / 10000; R.rd.fill(NAN); R.r.fill(0); R.n.fill(0); R.pts.fill(0); int odir[3] = {0, 0, 0};
    for (int w = 0; w < 3; ++w) { const int W = WM[w]; const double mv = c[W - 1] - op; double path = 0, hi = -1e18, lo = 1e18, num = 0, den = 0, pr = NAN;
      for (int m = 0; m < W; ++m) { hi = std::max(hi, h[m]); lo = std::min(lo, l[m]); if (m > 0) { const double r = c[m] - c[m - 1]; path += std::fabs(r); den += r * r; if (!std::isnan(pr)) num += pr * r; pr = r; } }
      odir[w] = mv > 0 ? 1 : mv < 0 ? -1 : 0;
      for (int q = 0; q < NR; ++q) if (RW[q] == w) { const int typ = q - (w == 0 ? 0 : w == 1 ? 3 : 7);
        R.rd[q] = typ == 0 ? (path > 0 ? std::fabs(mv) / path : 0) : typ == 1 ? std::fabs(mv) / atr : typ == 2 ? (hi - lo) / atr : (den > 0 ? num / den : 0); } }
    for (int dm = 0; dm < 2; ++dm) for (int w = 0; w < 3; ++w) { const int d = dm == 0 ? odir[w] : pd; const int t0 = std::max(WM[w], dm == 1 ? pwhen : 0); const int si = dm * 3 + w;
      if (!d) continue; int busy = 0, ntr = 0; bool lastwin = true;
      for (int k = 3; k < NK; ++k) { if (ntr >= 3 || !lastwin) break; const int me = 5 * k + 5; if (me > LAST || me < t0 || 5 * k < busy) continue;
        const double ema = emaB[k]; if (!(d > 0 ? (L5[k] <= ema && C5[k] > ema) : (H5[k] >= ema && C5[k] < ema))) continue;
        if (d * (C5[k] - vw[me - 1]) <= 0 || d * (C5[k] - op) <= 0) continue;
        const double pe = C5[k], st = 0.25 * atr; double stop = pe - d * st, best = pe; bool be = false; double pts = NAN; int xm = NM - 1;
        for (int m = me; m < NM - 1; ++m) { if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - pe); xm = m + 1; break; }
          best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (!be && d * (best - pe) >= st) { be = true; stop = pe + d * COST; } }
        if (std::isnan(pts)) pts = d * (c[NM - 2] - pe); pts -= COST;
        R.r[si] += pts / st; R.n[si] += 1; R.pts[si] += pts; busy = xm; ++ntr; lastwin = pts > 0; k = std::max(k, (xm + 4) / 5 - 1); } }
    V.push_back(R); }
  // cut points per (year, reading) from the two previous years (2025+: from 2023-24)
  auto cut = [&](int y, int q) { const int ya = y >= 2025 ? 2023 : y - 2, yb = y >= 2025 ? 2024 : y - 1; std::vector<double> v; for (auto& x : V) if (x.year >= ya && x.year <= yb && !std::isnan(x.rd[q])) v.push_back(x.rd[q]);
    if (v.size() < 100) return std::make_pair((double)NAN, (double)NAN); std::sort(v.begin(), v.end()); return std::make_pair(v[v.size() / 3], v[2 * v.size() / 3]); };
  std::map<std::pair<int, int>, std::pair<double, double>> C; for (int y = 2010; y <= 2026; ++y) for (int q = 0; q < NR; ++q) C[{y, q}] = cut(y, q);
  auto bucket = [&](const DayRec& x, int q) { auto c = C[{x.year, q}]; if (std::isnan(c.first)) return -1; return x.rd[q] < c.first ? 0 : x.rd[q] < c.second ? 1 : 2; };
  struct T { double r = 0, n = 0, pts = 0; void add(const DayRec& x, int si) { r += x.r[si]; n += x.n[si]; pts += x.pts[si]; } double m() const { return n ? r / n : NAN; } };
  auto era = [](int y) { return y <= 2018 ? 0 : y <= 2022 ? 1 : 2; };
  const char* DM[2] = {"OPEN direction", "PLAN direction"}; const char* BN[3] = {"low", "mid", "high"};
  for (int dm = 0; dm < 2; ++dm) { printf("\n===== EMA trades in the %s, R/trade (2012-24 by bucket; eras 12-18 / 19-22 / 23-24), cost 1.6 =====\n", DM[dm]);
    for (int w = 0; w < 3; ++w) { const int si = dm * 3 + w; T all, ae[3]; for (auto& x : V) if (x.year >= 2012 && x.year <= 2024) { all.add(x, si); ae[era(x.year)].add(x, si); }
      printf("  after %2dm, every day: %+.3f R (n %.0f) eras %+.3f / %+.3f / %+.3f\n", WM[w], all.m(), all.n, ae[0].m(), ae[1].m(), ae[2].m());
      for (int q = 0; q < NR; ++q) { if (RW[q] != w) continue; printf("    %-10s", RN[q]);
        for (int b = 0; b < 3; ++b) { T t, te[3]; for (auto& x : V) if (x.year >= 2012 && x.year <= 2024 && bucket(x, q) == b) { t.add(x, si); te[era(x.year)].add(x, si); }
          const bool allpos = te[0].m() > 0 && te[1].m() > 0 && te[2].m() > 0;
          printf(" | %-4s %+.3f (n %4.0f; %+.2f/%+.2f/%+.2f)%s", BN[b], t.m(), t.n, te[0].m(), te[1].m(), te[2].m(), allpos ? " *" : "  "); }
        printf("\n"); } } }
  // walk-forward over all (direction, window, reading, bucket) cells + "every day" cells: each year trades the best prior total-R cell
  printf("\nWalk-forward (each year: the cell with the best total R in 2012..year-1, ≥ 100 trades):\n"); T wf, base;
  auto cellsum = [&](int si, int q, int b, int y0, int y1) { T t; for (auto& x : V) if (x.year >= y0 && x.year <= y1 && (q < 0 || bucket(x, q) == b)) t.add(x, si); return t; };
  auto best_cell = [&](int y1, int& bsi, int& bq, int& bb) { double br = -1e18; for (int si = 0; si < 6; ++si) { const int w = si % 3; for (int q = -1; q < NR; ++q) { if (q >= 0 && RW[q] != w) continue;
        for (int b = 0; b < (q < 0 ? 1 : 3); ++b) { T t = cellsum(si, q, b, 2012, y1); if (t.n >= 100 && t.r > br) { br = t.r; bsi = si; bq = q; bb = b; } } } } };
  for (int y = 2014; y <= 2024; ++y) { int si = 0, q = -1, b = 0; best_cell(y - 1, si, q, b); T t = cellsum(si, q, b, y, y), tb = cellsum(4, -1, 0, y, y);
    wf.r += t.r; wf.n += t.n; wf.pts += t.pts; base.r += tb.r; base.n += tb.n; base.pts += tb.pts;
    printf("    %d %s after %dm, %s → %+.3f R (n %.0f) | plan direction after 15m every day %+.3f\n", y, DM[si / 3], WM[si % 3], q < 0 ? "every day" : (std::string(RN[q]) + " " + BN[b]).c_str(), t.m(), t.n, tb.m()); }
  printf("  WF 2014-24: %+.3f R/trade, %+.2f pts (n %.0f) | plan direction every day %+.3f R, %+.2f pts (n %.0f)\n", wf.m(), wf.pts / wf.n, wf.n, base.m(), base.pts / base.n, base.n);
  { int si = 0, q = -1, b = 0; best_cell(2024, si, q, b); T h = cellsum(si, q, b, 2025, 2026), hb = cellsum(4, -1, 0, 2025, 2026);
    printf("HOLDOUT 2025-26 (one look) %s after %dm, %s: %+.3f R %+.2f pts (n %.0f) | plan direction every day %+.3f R (n %.0f)\n", DM[si / 3], WM[si % 3], q < 0 ? "every day" : (std::string(RN[q]) + " " + BN[b]).c_str(), h.m(), h.pts / h.n, h.n, hb.m(), hb.n); }
}
