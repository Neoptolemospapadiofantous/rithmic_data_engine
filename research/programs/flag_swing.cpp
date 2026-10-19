// 2026-10-19 (founder: "flags should be calculated better"). A structural flag detector instead of the fixed 6-bar pole / 5-bar flag:
//  POLE  = a swing: from the lowest low L to the highest high H, H 3-12 bars after L (bull; mirror for bear), and the pole must be big for
//          its chart: H − L >= 3 x the average bar range of the session so far on that chart (no ATR).
//  FLAG  = the 2-8 bars after H: none makes a new high (H stays the top), the pullback keeps the flag low >= H − 0.5 (H − L), and the
//          flag's own range <= 0.5 (H − L). The most recent valid H is used.
//  BREAKOUT = the current bar closes above the flag's high (and above its own open). Entry at that close, stop 1 tick below the flag low.
//  Targets: 1.5R, or the measured move (entry + pole length). Flat 15:55.
// NY, charts 3/4/5 min, one at a time, <= 5 a day, 2 pts cost. Variants: direction (any / the 10:00 direction) x entry window (10:00-12:00
// / 10:00-15:00) x target. Fair control: random day + random minute in the same window, the 10:00 direction (or a coin flip for "any"),
// the trade's stop in ATR (ATR is used ONLY to transfer the stop size to another day in the control), same exit.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int e, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[e] += pts; ne[e]++; } };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DI { std::vector<int> I; int d10, era; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i);
    if (I.size() < 300) continue; double so = s.o[I[0]]; int d10 = 0; for (size_t k = 0; k < I.size(); ++k) if (tday_min(s.t[I[k]]) == hm_to_tday(9, 59)) d10 = s.c[I[k]] > so ? 1 : s.c[I[k]] < so ? -1 : 0;
    days.push_back({I, d10, d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2, d.atr}); }
  std::mt19937 rng(113); const int TF[3] = {5, 4, 3};
  printf("Structural flags (swing pole, consolidating flag, breakout of the flag) — NQ NY 3/4/5m, one at a time, <= 5 a day, 2 pts cost, %zu days.\n"
         "Columns: trades/day | win %% | pts per trade (R) | pts 2010-18 / 2019-24 / 2025-26 (n) | fair control pts.\n", days.size());
  for (int dirMode = 1; dirMode >= 0; --dirMode) for (int win = 0; win < 2; ++win) for (int tg = 0; tg < 2; ++tg) {
    const int endT = win == 0 ? hm_to_tday(12, 0) : hm_to_tday(15, 0); St a; std::vector<double> RA; std::vector<int> dirs;
    for (const DI& x : days) { if (dirMode && !x.d10) continue; const auto& I = x.I; const size_t n = I.size(); const double A = x.A;
      std::vector<FB> b[3]; size_t busy = 0; bool have = false; int nd = 0;
      for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
        for (int t = 0; t < 3; ++t) { int tf = TF[t]; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]}); else { FB& z = b[t].back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; } }
        if (tm < hm_to_tday(10, 0) || tm >= endT || (have && k <= busy) || nd >= 5) continue;
        for (int t = 0; t < 3; ++t) { int tf = TF[t]; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; const auto& v = b[t]; int q = v.size() - 1; if (q < 6) continue;
          bool done = false;
          for (int D : {1, -1}) { if (done) break; if (dirMode && D != x.d10) continue;
            for (int F = 2; F <= 8 && !done; ++F) { int h = q - 1 - F; if (h < 3) break;
              double H = D > 0 ? v[h].h : v[h].l; bool ok = true; for (int r = h + 1; r < q; ++r) if (D > 0 ? v[r].h >= H : v[r].l <= H) { ok = false; break; } if (!ok) continue;
              int l = -1; double L = D > 0 ? 1e18 : -1e18; for (int r = std::max(0, h - 12); r <= h - 3; ++r) if (D > 0 ? v[r].l < L : v[r].h > L) { L = D > 0 ? v[r].l : v[r].h; l = r; }
              for (int r = h - 2; r < h; ++r) if (D > 0 ? v[r].l < L : v[r].h > L) ok = false; if (!ok || l < 0) continue;   // the swing low must be the low of the whole pole
              double P = D * (H - L); double ar = 0; for (int r = 0; r <= q; ++r) ar += v[r].h - v[r].l; ar /= (q + 1); if (P < 3 * ar || P <= 0) continue;
              double FH = -1e18, FL = 1e18; for (int r = h + 1; r < q; ++r) { FH = std::max(FH, v[r].h); FL = std::min(FL, v[r].l); }
              double pullEnd = D > 0 ? FL : FH, flagTop = D > 0 ? FH : FL; if (D * (H - pullEnd) > 0.5 * P) continue; if (FH - FL > 0.5 * P) continue;
              double c = v[q].c; if (D > 0 ? !(c > flagTop && c > v[q].o) : !(c < flagTop && c < v[q].o)) continue;
              double stop = D > 0 ? FL - 0.25 : FH + 0.25, R = D * (c - stop); if (!(R > 0.25)) continue;
              double tgt = tg == 0 ? c + D * 1.5 * R : c + D * P, px = NAN; size_t kx = n - 1;
              for (size_t kk = k + 1; kk < n; ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= stop : s.h[j] >= stop) { px = stop; kx = kk; break; } if (D > 0 ? s.h[j] >= tgt : s.l[j] <= tgt) { px = tgt; kx = kk; break; } if (tday_min(s.t[j]) >= hm_to_tday(15, 55)) { px = s.c[j]; kx = kk; break; } }
              if (std::isnan(px)) px = s.c[I[n - 1]]; double net = D * (px - c) - 2.0 * A / 320; a.add(x.era, net > 0, net * 320 / A, net / R); RA.push_back(R / A); dirs.push_back(D); busy = kx; have = true; nd++; done = true; } }
          if (done) break; } } }
    double cp = 0, cn = 0; for (int rep = 0; rep < 10; ++rep) for (size_t ti = 0; ti < RA.size(); ++ti) { const DI& x = days[std::uniform_int_distribution<size_t>(0, days.size() - 1)(rng)]; int dir = dirMode ? x.d10 : ((rng() & 1) ? 1 : -1); if (!dir) continue;
        std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) { int tm = tday_min(s.t[x.I[k]]); if (tm >= hm_to_tday(10, 0) && tm < endT) cd.push_back(k); } if (cd.empty()) continue;
        size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; double R = RA[ti] * x.A, e = s.c[x.I[k0]], st = e - dir * R, tgp = tg == 0 ? e + dir * 1.5 * R : e + dir * 2.5 * R, px = NAN;
        for (size_t k = k0 + 1; k < x.I.size(); ++k) { int i = x.I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tgp : s.l[i] <= tgp) { px = tgp; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
        if (std::isnan(px)) px = s.c[x.I.back()]; cp += (dir * (px - e) - 2.0 * x.A / 320) * 320 / x.A; cn++; }
    printf("  %-14s %-12s %-14s %5.2f/day | %4.1f%% | %+6.2f (%+.3fR) | %+6.2f / %+6.2f / %+6.2f (%3.0f) | control %+6.2f\n", dirMode ? "10:00 dir" : "any dir", win ? "10:00-15:00" : "10:00-12:00", tg ? "measured move" : "1.5R",
           a.n / days.size(), 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n), a.r / std::max(1.0, a.n), a.pe[0] / std::max(1.0, a.ne[0]), a.pe[1] / std::max(1.0, a.ne[1]), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2], cp / std::max(1.0, cn)); }
  return 0;
}
