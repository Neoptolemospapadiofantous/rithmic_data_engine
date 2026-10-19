// 2026-10-19 (founder: "not good at all, loosen the filter a bit" — ~1.5 filtered flags a week is too few). NY flags on the 1/2/3/4/5-min
// charts as ONE stream (any chart, one position at a time, <= 5 a day), 1.5R target held to 15:55, 2 pts cost, entries 09:45–15:00.
// Pre-declared loosening ladder (nothing tuned):
//  L0 straightness >= 0.55, day's side of the open AND VWAP, pole 6 bars >= 0.20 ATR, flag 5 bars <= 0.6 x pole   (current)
//  L1 L0 with straightness >= 0.40
//  L2 L1 with pole >= 0.15 ATR and flag <= 0.7 x pole
//  L3 L2 without the VWAP condition (only the day's side of the open)
//  L4 L3 with the shorter shape: pole 4 bars, flag 3 bars
// each with and without "gap agrees" (the 09:30 open gapped in the trade's direction vs the prior close).
// Control (20 random draws per trade, averaged): same number of entries per day at random minutes in the same context, same stop size, same exit.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
#include <algorithm>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0; void add(bool win, double pts) { n++; if (win) w++; p += pts; } };
int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const double STR[5] = {0.55, 0.40, 0.40, 0.40, 0.40}, PM[5] = {0.20, 0.20, 0.15, 0.15, 0.15}, FT[5] = {0.6, 0.6, 0.7, 0.7, 0.7}; const bool VW[5] = {1, 1, 1, 0, 0}; const int PN[5] = {6, 6, 6, 6, 4}, FN_[5] = {5, 5, 5, 5, 3};
  std::mt19937 rng(61); St acc[5][2][3], ctl[5][2][3], ctl2[5][2][3]; double days = 0;
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; days++; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue;
    const int gap = !std::isnan(d.pc) ? (s.o[I[0]] > d.pc ? 1 : s.o[I[0]] < d.pc ? -1 : 0) : 0;
    std::vector<float> ST(I.size()); std::vector<signed char> D(I.size()), VS(I.size());
    { double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0; for (size_t k = 0; k < I.size(); ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; ST[k] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; D[k] = c > so ? 1 : c < so ? -1 : 0; VS[k] = c > vw ? 1 : c < vw ? -1 : 0; } }
    auto run = [&](size_t k0, int dir, double R, bool& win, double& pts) -> size_t { double e = s.c[I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN; size_t k = k0;
      for (k = k0 + 1; k < I.size(); ++k) { int i = I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
      if (k >= I.size()) { k = I.size() - 1; px = s.c[I[k]]; } double net = dir * (px - e) - 2.0 * A / 320; win = net > 0; pts = net * 320 / A; return k; };
    for (int L = 0; L < 5; ++L) for (int g = 0; g < 2; ++g) {
      auto ctx = [&](size_t k) { int tm = tday_min(s.t[I[k]]); return D[k] && ST[k] >= STR[L] && (!VW[L] || VS[k] == D[k]) && tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0) && (!g || gap == D[k]); };
      std::vector<FB> b[5]; size_t busy = 0; bool have = false; int nd = 0; std::vector<double> Rs;
      for (size_t k = 0; k < I.size(); ++k) { int i = I[k], tm = tday_min(s.t[i]);
        for (int t = 0; t < 5; ++t) { int tf = t + 1; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
          else { FB& x = b[t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } }
        if (!ctx(k) || (have && k <= busy) || nd >= 5) continue;
        for (int t = 4; t >= 0; --t) { int tf = t + 1; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; const auto& v = b[t]; int n = v.size(), N = PN[L], F = FN_[L]; if (n < N + F + 1) continue;
          int p0 = n - 1 - F - N, p1 = n - 2 - F, Dd = D[k]; double pole = Dd * (v[p1].c - v[p0].o); if (pole < PM[L] * A) continue;
          double fh = -1e18, fl = 1e18; for (int q = n - 1 - F; q < n - 1; ++q) { fh = std::max(fh, v[q].h); fl = std::min(fl, v[q].l); } if (fh - fl > FT[L] * pole) continue;
          double c = v[n - 1].c; if (Dd > 0 ? !(c > fh) : !(c < fl)) continue; double R = Dd * (c - (Dd > 0 ? fl - 0.25 : fh + 0.25)); if (!(R > 0.25) || R > 0.6 * A) continue;
          bool win; double pts; busy = run(k, Dd, R, win, pts); have = true; nd++; acc[L][g][era].add(win, pts); Rs.push_back(R); break; } }
      std::vector<size_t> cand; for (size_t k = 0; k < I.size(); ++k) if (ctx(k)) cand.push_back(k);
      for (int rep = 0; rep < 20; ++rep) { size_t after = 0; for (double R : Rs) { std::vector<size_t> ok; for (size_t k : cand) if (k > after) ok.push_back(k); if (ok.empty()) break; size_t k = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)];
        bool win; double pts; after = run(k, D[k], R, win, pts); ctl[L][g][era].add(win, pts); } }
      std::vector<size_t> any; for (size_t k = 0; k < I.size(); ++k) { int tm = tday_min(s.t[I[k]]); if (D[k] && tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0)) any.push_back(k); }
      for (int rep = 0; rep < 20; ++rep) { size_t after = 0; for (double R : Rs) { std::vector<size_t> ok; for (size_t k : any) if (k > after) ok.push_back(k); if (ok.empty()) break; size_t k = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)];
        bool win; double pts; after = run(k, D[k], R, win, pts); ctl2[L][g][era].add(win, pts); } } } }
  const char* LN[5] = {"L0 current (st .55, open+VWAP, pole 6 >= .20, flag 5 <= .6)", "L1 straightness >= 0.40", "L2 + pole >= 0.15, flag <= 0.7", "L3 + no VWAP condition", "L4 + short shape (pole 4, flag 3)"};
  printf("%s NY 1-5m flags with the day as one stream (<= 5 a day, one at a time), 1.5R to 15:55, 2 pts cost — loosening ladder. %.0f days.\n"
         "Per level: trades/day | win %% 2010-18 / 2019-24 / 2025-26 | net pts/trade all years / 2025-26 | control win %% and pts (random timing).\n", sym, days);
  for (int g = 0; g < 2; ++g) { printf("\n== %s\n", g ? "with gap agrees" : "no gap filter");
    for (int L = 0; L < 5; ++L) { St* a = acc[L][g]; St* c = ctl[L][g]; St* c2 = ctl2[L][g]; double c2n = c2[0].n + c2[1].n + c2[2].n; double n = a[0].n + a[1].n + a[2].n, p = a[0].p + a[1].p + a[2].p, cn = c[0].n + c[1].n + c[2].n;
      printf("  %-58s %5.2f/day | WR %4.1f / %4.1f / %4.1f | pts %+5.2f / %+6.2f | same-context control WR %4.1f pts %+6.2f | any-time with-the-day control %+6.2f\n", LN[L], n / days, 100 * a[0].w / a[0].n, 100 * a[1].w / a[1].n, 100 * a[2].w / std::max(1.0, a[2].n),
             p / n, a[2].p / std::max(1.0, a[2].n), 100 * (c[0].w + c[1].w + c[2].w) / cn, (c[0].p + c[1].p + c[2].p) / cn, (c2[0].p + c2[1].p + c2[2].p) / c2n); } }
  return 0;
}
