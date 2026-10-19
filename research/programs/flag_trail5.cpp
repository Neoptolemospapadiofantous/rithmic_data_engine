// 2026-10-19 (founder: "looks promising" — the flag grid's "with the day + straight path, trail previous bar" view, NY 5m bull +10.7).
// One concrete rule, validated: NY 5-min flags with the day on a straight path (session straightness >= 0.55, price on the day's side of the
// 09:30 open and of VWAP), pole 6 bars (>= 0.20 ATR, or any size in the no-ATR variant), flag 5 bars <= 0.6 x pole, close beyond the flag,
// entries 09:45-15:00, one at a time, <= 5 a day, 2 pts cost. Stop 1 tick beyond the flag; exits on the same entries:
//  T trail: once the trade is +1R (on a 1-min close) the stop trails the previous completed 5-min bar's low (bull) / high (bear); no target;
//    flat 15:55 · F15 fixed 1.5R to 15:55 · F3 fixed 3R to 15:55.
// Fair control: random day, random minute 09:45-15:00 where that day's session is straight and on the VWAP side (same context, other
// days and times), the day's direction, the trade's stop in ATR, the same exit. Writes inputs_20261019/flagtrail5_k.csv (rule T, no-ATR version).
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; int k; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int e, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[e] += pts; ne[e]++; } };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DI { std::vector<int> I; std::vector<int> D; std::vector<char> ctx; int era, year, date; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; DI x; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) x.I.push_back(i);
    if (x.I.size() < 300) continue; const size_t n = x.I.size(); x.D.resize(n); x.ctx.resize(n); double so = s.o[x.I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
    for (size_t k = 0; k < n; ++k) { int i = x.I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
      int D = c > so ? 1 : c < so ? -1 : 0; x.D[k] = D; int tm = tday_min(s.t[i]); x.ctx[k] = D && (c - vw) * D > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55 && tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0); }
    x.era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; x.year = d.date / 10000; x.date = d.date; x.A = d.atr; days.push_back(std::move(x)); }
  // exit engine shared by flags and control: returns exit position, fills pts / R
  auto exitT = [&](const DI& x, size_t k0, int D, double R, int mode, double& pts, double& rr) -> size_t {
    const auto& I = x.I; const size_t n = I.size(); double e = s.c[I[k0]], st = e - D * R, tg = mode == 1 ? e + D * 1.5 * R : mode == 2 ? e + D * 3 * R : NAN, px = NAN; size_t kx = n - 1; bool armed = false;
    double bh = -1e18, bl = 1e18, prevH = NAN, prevL = NAN;
    for (size_t k = k0 + 1; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
      if (D > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; kx = k; break; }
      if (!std::isnan(tg) && (D > 0 ? s.h[i] >= tg : s.l[i] <= tg)) { px = tg; kx = k; break; }
      if (tm >= hm_to_tday(15, 55)) { px = s.c[i]; kx = k; break; }
      bh = std::max(bh, (double)s.h[i]); bl = std::min(bl, (double)s.l[i]);
      if ((tm + 1 - hm_to_tday(9, 30)) % 5 == 0) { prevH = bh; prevL = bl; bh = -1e18; bl = 1e18; if (mode == 0 && armed) { double ns = D > 0 ? prevL - 0.25 : prevH + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } }
      if (mode == 0 && !armed && D * (s.c[i] - e) >= R) { armed = true; if (!std::isnan(prevL)) { double ns = D > 0 ? prevL - 0.25 : prevH + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } } }
    if (std::isnan(px)) px = s.c[I[n - 1]]; double net = D * (px - e) - 2.0 * x.A / 320; pts = net * 320 / x.A; rr = net / R; return kx; };
  std::mt19937 rng(127); FILE* out = fopen("research/programs/inputs_20261019/flagtrail5_k.csv", "w");
  const char* MN[3] = {"T trail previous 5m bar after +1R", "F15 fixed 1.5R to close", "F3 fixed 3R to close"};
  printf("NY 5-min flags with the day on a straight path — NQ %zu days, one at a time, <= 5 a day, 2 pts cost. pts = net NQ points per trade at today's range.\n", days.size());
  for (int atrRule = 1; atrRule >= 0; --atrRule) { printf("\n######## pole %s\n", atrRule ? ">= 0.20 ATR" : "any size (no ATR rule)");
    for (int mode = 0; mode < 3; ++mode) { St a[2]; std::map<int, std::pair<double, double>> yr; std::vector<double> RA;
      for (const DI& x : days) { const auto& I = x.I; const size_t n = I.size(); std::vector<FB> b; size_t busy = 0; bool have = false; int nd = 0;
        for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
          if ((tm - hm_to_tday(9, 30)) % 5 == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
          if ((tm + 1 - hm_to_tday(9, 30)) % 5 || !x.ctx[k] || (have && k <= busy) || nd >= 5) continue; int m = b.size(); if (m < 12) continue;
          int D = x.D[k]; double pole = D * (b[m - 7].c - b[m - 12].o); if (pole <= 0 || (atrRule && pole < 0.20 * x.A)) continue;
          double fh = -1e18, fl = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, b[q].h); fl = std::min(fl, b[q].l); } if (fh - fl > 0.6 * pole) continue;
          double c = b[m - 1].c; if (D > 0 ? !(c > fh) : !(c < fl)) continue; double R = D * (c - (D > 0 ? fl - 0.25 : fh + 0.25)); if (!(R > 0.25) || (atrRule && R > 0.6 * x.A)) continue;
          double pts, rr; size_t kx = exitT(x, k, D, R, mode, pts, rr); busy = kx; have = true; nd++; a[D > 0 ? 0 : 1].add(x.era, pts > 0, pts, rr); yr[x.year].first += pts; yr[x.year].second++; RA.push_back(R / x.A);
          if (!atrRule && mode == 0) fprintf(out, "%d,%ld,%ld,%d,%.4f,%.4f,0,0,5,%.5f\n", x.date, (long)s.t[I[k]], (long)s.t[I[kx]], D, pts * x.A / 320 + 2.0 * x.A / 320, pts * x.A / 320 + 2.0 * x.A / 320 - 1, R / x.A); } }
      double cp = 0, cn = 0; for (int rep = 0; rep < 10; ++rep) for (double ra : RA) { const DI& x = days[std::uniform_int_distribution<size_t>(0, days.size() - 1)(rng)]; std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) if (x.ctx[k]) cd.push_back(k); if (cd.empty()) continue;
          size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; double pts, rr; exitT(x, k0, x.D[k0], ra * x.A, mode, pts, rr); cp += pts; cn++; }
      printf("\n== %s   (fair control, same context on random days and times: %+.2f pts)\n", MN[mode], cp / std::max(1.0, cn));
      for (int sd = 0; sd < 3; ++sd) { St t; if (sd < 2) t = a[sd]; else { t = a[0]; t.n += a[1].n; t.w += a[1].w; t.p += a[1].p; t.r += a[1].r; for (int e = 0; e < 3; ++e) { t.pe[e] += a[1].pe[e]; t.ne[e] += a[1].ne[e]; } }
        printf("  %-5s %5.2f/day | win %4.1f%% | %+6.2f pts (%+.3fR) | 2010-18 %+6.2f / 2019-24 %+6.2f / 2025-26 %+6.2f (%3.0f)\n", sd == 0 ? "bull" : sd == 1 ? "bear" : "both", t.n / days.size(), 100 * t.w / std::max(1.0, t.n), t.p / std::max(1.0, t.n), t.r / std::max(1.0, t.n),
               t.pe[0] / std::max(1.0, t.ne[0]), t.pe[1] / std::max(1.0, t.ne[1]), t.pe[2] / std::max(1.0, t.ne[2]), t.ne[2]); }
      printf("  by year:"); for (auto& [y, q] : yr) printf(" %d %+.1f", y, q.first / q.second); printf("\n"); } }
  fclose(out); return 0;
}
