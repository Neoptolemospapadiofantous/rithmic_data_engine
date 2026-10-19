// 2026-10-19 (founder: "more tests"). Why do frequent flags fail in 2025-26, and does fading FAILED flags work there?
// PART 1 — trend after 10:00 by year: mean of dir10 x (15:55 close − 10:00 price) / ATR, share of days that keep going, and the same for
// a random with-the-day entry at 11:00 (to 15:55). PART 2 — failed flags: every NY flag on 1/2/3/4/5/15m (pole 6 bars >= 0.10 ATR, flag
// 5 bars <= 0.8 x pole, close beyond the flag); if within the next 3 bars of that chart a bar CLOSES back inside the flag, enter the
// OTHER way at that close, stop beyond the extreme reached since the breakout + 0.25, 1.5R target held to 15:55, one at a time, <= 5 a
// day, entries 09:45-15:00, 2 pts cost. Variants: all failed flags; only failed flags AGAINST the 10:00 direction (fading a counter-move);
// only on days whose 09:30-10:00 range >= 0.35 ATR (the 2025-26 kind of day). Per year and per period, with a fair control (random day,
// random minute 10:00-15:00, random direction, same stop in ATR, same exit).
#include "research/meta.hpp"
#include <cstdio>
#include <random>
#include <algorithm>
using namespace lab;
struct B { double o, h, l, c; int k; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DI { std::vector<int> I; int d10, era, year; double A, or30; }; std::vector<DI> days;
  std::map<int, double> mom_s, mom_n, mom_pos, r11_s;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 300) continue;
    double so = s.o[I[0]], p10 = NAN, p11 = NAN, pc = NAN, h3 = -1e18, l3 = 1e18; for (size_t k = 0; k < I.size(); ++k) { int tm = tday_min(s.t[I[k]]); if (tm < hm_to_tday(10, 0)) { h3 = std::max(h3, (double)s.h[I[k]]); l3 = std::min(l3, (double)s.l[I[k]]); }
      if (tm == hm_to_tday(9, 59)) p10 = s.c[I[k]]; if (tm == hm_to_tday(10, 59)) p11 = s.c[I[k]]; if (tm <= hm_to_tday(15, 54)) pc = s.c[I[k]]; }
    if (std::isnan(p10) || std::isnan(pc)) continue; int d10 = p10 > so ? 1 : p10 < so ? -1 : 0; int y = d.date / 10000;
    if (d10) { double m = d10 * (pc - p10) / A; mom_s[y] += m; mom_n[y]++; mom_pos[y] += m > 0; if (!std::isnan(p11)) r11_s[y] += d10 * (pc - p11) / A; }
    days.push_back({I, d10, d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2, y, A, (h3 - l3) / A}); }
  printf("PART 1 — trend after 10:00 by year (10:00 direction = side of the 09:30 open at 10:00). In ATR (1 ATR ≈ 320 NQ pts today).\n  year  10:00→close  days that kept going  11:00→close\n");
  for (auto& [y, n] : mom_n) printf("  %d   %+6.3f        %5.1f %%             %+6.3f\n", y, mom_s[y] / n, 100 * mom_pos[y] / n, r11_s[y] / n);
  // PART 2
  const int TFS[6] = {1, 2, 3, 4, 5, 15}; std::mt19937 rng(101);
  const char* VN[3] = {"all failed flags, faded", "failed flags against the 10:00 direction (fade back WITH the day)", "all failed flags, only days with 09:30-10:00 range >= 0.35 ATR"};
  printf("\nPART 2 — fading failed flags (NY, 1-15m, 1.5R to close, one at a time, <= 5 a day). pts = net NQ points per trade at today's range.\n");
  for (int v = 0; v < 3; ++v) { double n[3] = {}, p[3] = {}, w = 0, nt = 0, ndays = 0; std::map<int, std::pair<double, double>> yr; std::vector<double> RA;
    for (size_t di = 0; di < days.size(); ++di) { const DI& x = days[di]; if (v == 2 && x.or30 < 0.35) { continue; } ndays++; const auto& I = x.I; const size_t nn = I.size(); const double A = x.A;
      struct C { size_t k; int dir; double R; }; std::vector<C> cand;
      for (int tf : TFS) { std::vector<B> b; for (size_t k = 0; k < nn; ++k) { int i = I[k], g = (tday_min(s.t[i]) - hm_to_tday(9, 30)) / tf;
          if (b.empty() || (tday_min(s.t[I[b.back().k]]) - hm_to_tday(9, 30)) / tf != g) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { B& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; } }
        for (int q = 11; q + 1 < (int)b.size(); ++q) { int p0 = q - 11, p1 = q - 6; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < 0.10 * A) continue;
          double fh = -1e18, fl = 1e18; for (int r = q - 5; r < q; ++r) { fh = std::max(fh, b[r].h); fl = std::min(fl, b[r].l); } if (fh - fl > 0.8 * ap) continue; if (fd > 0 ? !(b[q].c > fh) : !(b[q].c < fl)) continue;
          double ext = fd > 0 ? b[q].h : b[q].l; for (int r = q + 1; r <= q + 3 && r < (int)b.size(); ++r) { ext = fd > 0 ? std::max(ext, b[r].h) : std::min(ext, b[r].l);
            bool back = fd > 0 ? b[r].c < fh : b[r].c > fl; if (!back) continue; int dir = -fd; size_t k = b[r].k; int tm = tday_min(s.t[I[k]]); if (tm < hm_to_tday(9, 45) || tm >= hm_to_tday(15, 0)) break;
            if (v == 1 && !(tm >= hm_to_tday(10, 0) && dir == x.d10)) break; double c = b[r].c, R = dir * (c - (dir > 0 ? ext + 0.25 * 0 - (ext - ext) : 0)); (void)R;
            double stop = fd > 0 ? ext + 0.25 : ext - 0.25; double RR = dir * (c - stop); if (!(RR > 0.25) || RR > 0.6 * A) break; cand.push_back({k, dir, RR}); break; } } }
      std::sort(cand.begin(), cand.end(), [](const C& a, const C& b) { return a.k < b.k; }); size_t busy = 0; bool have = false; int cnt = 0;
      for (auto& cc : cand) { if ((have && cc.k <= busy) || cnt >= 5) continue; double e = s.c[I[cc.k]], st = e - cc.dir * cc.R, tg = e + cc.dir * 1.5 * cc.R, px = NAN; size_t kx = nn - 1;
        for (size_t k = cc.k + 1; k < nn; ++k) { int i = I[k]; if (cc.dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; kx = k; break; } if (cc.dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; kx = k; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; kx = k; break; } }
        if (std::isnan(px)) px = s.c[I[nn - 1]]; busy = kx; have = true; cnt++; double pts = (cc.dir * (px - e) - 2.0 * A / 320) * 320 / A; n[x.era]++; p[x.era] += pts; w += pts > 0; nt++; yr[x.year].first += pts; yr[x.year].second++; RA.push_back(cc.R / A); } }
    double cp = 0, cn = 0; for (int rep = 0; rep < 5; ++rep) for (double ra : RA) { const DI& x = days[std::uniform_int_distribution<size_t>(0, days.size() - 1)(rng)]; std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) { int tm = tday_min(s.t[x.I[k]]); if (tm >= hm_to_tday(10, 0) && tm < hm_to_tday(15, 0)) cd.push_back(k); } if (cd.empty()) continue;
        size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; int dir = (rng() & 1) ? 1 : -1; double R = ra * x.A, e = s.c[x.I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN;
        for (size_t k = k0 + 1; k < x.I.size(); ++k) { int i = x.I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
        if (std::isnan(px)) px = s.c[x.I.back()]; cp += (dir * (px - e) - 2.0 * x.A / 320) * 320 / x.A; cn++; }
    printf("\n== %s\n  %.2f trades a day | win %.1f%% | pts 2010-18 %+.2f / 2019-24 %+.2f / 2025-26 %+.2f | fair control (random direction) %+.2f\n  by year:", VN[v], nt / ndays, 100 * w / nt, p[0] / std::max(1.0, n[0]), p[1] / std::max(1.0, n[1]), p[2] / std::max(1.0, n[2]), cp / cn);
    for (auto& [y, q] : yr) printf(" %d %+.1f", y, q.first / q.second); printf("\n"); }
  return 0;
}
