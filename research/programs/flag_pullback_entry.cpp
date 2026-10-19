// 2026-10-19 (founder: "flags with pullback"): the structural flag of flag_swing.cpp (swing pole >= 3x the session's average bar, 2-8 bar
// flag with no new extreme, pullback <= 50 % of the pole, flag range <= 0.5 pole, breakout close beyond the flag) entered on a PULLBACK after
// the breakout instead of at the breakout close. Entries (all within 30 minutes of the breakout, cancelled if the stop trades first):
//  E0 breakout close (base) · E1 limit at the flag's edge (retest of the breakout level) · E2 limit halfway between the breakout close and
//  the flag's edge · E3 pullback-and-resume: first 1-min bar that closes against the trade, then entry when a later 1-min bar closes
//  beyond that bar's extreme.
// Stop 1 tick beyond the flag; target 1.5R or the measured move (pole length) from the actual entry; flat 15:55. NQ NY 3/4/5m, 10:00
// direction, one at a time, <= 5 a day, 2 pts cost. Fill rate = share of breakouts that produced an entry.
// Second block (founder: "remove pullback"): the same without the "pullback <= 50 % of the pole" rule (flag range <= 0.5 pole kept), E0 and E1.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}, sig = 0; void add(int e, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[e] += pts; ne[e]++; } };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DI { std::vector<int> I; int d10, era; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i);
    if (I.size() < 300) continue; double so = s.o[I[0]]; int d10 = 0; for (size_t k = 0; k < I.size(); ++k) if (tday_min(s.t[I[k]]) == hm_to_tday(9, 59)) d10 = s.c[I[k]] > so ? 1 : s.c[I[k]] < so ? -1 : 0;
    days.push_back({I, d10, d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2, d.atr}); }
  const int TF[3] = {5, 4, 3}; const char* EN[4] = {"E0 breakout close", "E1 limit at the flag edge (retest)", "E2 limit halfway back", "E3 1-min pullback, then resume"};
  printf("Structural flags with a pullback entry — NQ NY 3/4/5m, 10:00 direction, one at a time, <= 5 a day, 2 pts cost, %zu days.\n"
         "Columns: trades/day | filled %% of breakouts | win %% | pts per trade (R) | pts 2010-18 / 2019-24 / 2025-26 (n) | avg stop (pts at today's range).\n", days.size());
  for (int pbRule = 1; pbRule >= 0; --pbRule) for (int win = 0; win < 2; ++win) for (int tg = 0; tg < 2; ++tg) { printf("\n== %s, %s, target %s\n", pbRule ? "flag pullback <= 50 %% of the pole" : "NO pullback-depth rule", win ? "10:00-15:00" : "10:00-12:00", tg ? "measured move" : "1.5R");
    const int endT = win == 0 ? hm_to_tday(12, 0) : hm_to_tday(15, 0);
    for (int em = 0; em < 4; ++em) { if (!pbRule && em >= 2) continue; St a; double stopSum = 0;
      for (const DI& x : days) { if (!x.d10) continue; const auto& I = x.I; const size_t n = I.size(); const double A = x.A; const int D = x.d10;
        std::vector<FB> b[3]; size_t busy = 0; bool have = false; int nd = 0;
        for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
          for (int t = 0; t < 3; ++t) { int tf = TF[t]; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]}); else { FB& z = b[t].back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; } }
          if (tm < hm_to_tday(10, 0) || tm >= endT || (have && k <= busy) || nd >= 5) continue;
          for (int t = 0; t < 3; ++t) { int tf = TF[t]; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; const auto& v = b[t]; int q = v.size() - 1; if (q < 6) continue; bool done = false;
            for (int F = 2; F <= 8 && !done; ++F) { int h = q - 1 - F; if (h < 3) break;
              double H = D > 0 ? v[h].h : v[h].l; bool ok = true; for (int r = h + 1; r < q; ++r) if (D > 0 ? v[r].h >= H : v[r].l <= H) { ok = false; break; } if (!ok) continue;
              int l = -1; double L = D > 0 ? 1e18 : -1e18; for (int r = std::max(0, h - 12); r <= h - 3; ++r) if (D > 0 ? v[r].l < L : v[r].h > L) { L = D > 0 ? v[r].l : v[r].h; l = r; }
              for (int r = h - 2; r < h; ++r) if (D > 0 ? v[r].l < L : v[r].h > L) ok = false; if (!ok || l < 0) continue;
              double P = D * (H - L), ar = 0; for (int r = 0; r <= q; ++r) ar += v[r].h - v[r].l; ar /= (q + 1); if (P < 3 * ar || P <= 0) continue;
              double FH = -1e18, FL = 1e18; for (int r = h + 1; r < q; ++r) { FH = std::max(FH, v[r].h); FL = std::min(FL, v[r].l); }
              double pullEnd = D > 0 ? FL : FH, edge = D > 0 ? FH : FL; if ((pbRule && D * (H - pullEnd) > 0.5 * P) || FH - FL > 0.5 * P) continue;
              double c = v[q].c; if (D > 0 ? !(c > edge && c > v[q].o) : !(c < edge && c < v[q].o)) continue;
              double stop = D > 0 ? FL - 0.25 : FH + 0.25; if (!(D * (c - stop) > 0.25)) continue;
              done = true; a.sig++;
              // entry
              double ent = NAN; size_t ke = 0; long tEnd = s.t[i] + 30 * 60;
              if (em == 0) { ent = c; ke = k; }
              else if (em == 1 || em == 2) { double lim = em == 1 ? edge : (c + edge) / 2;
                for (size_t kk = k + 1; kk < n && s.t[I[kk]] <= tEnd; ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= stop : s.h[j] >= stop) break; if (D > 0 ? s.l[j] <= lim : s.h[j] >= lim) { ent = lim; ke = kk; break; } } }
              else { int pb = -1; for (size_t kk = k + 1; kk < n && s.t[I[kk]] <= tEnd; ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= stop : s.h[j] >= stop) break;
                  if (pb < 0) { if (D * (s.c[j] - s.o[j]) < 0) pb = j; } else if (D > 0 ? s.c[j] > s.h[pb] : s.c[j] < s.l[pb]) { ent = s.c[j]; ke = kk; break; } } }
              if (std::isnan(ent)) break;
              double R = D * (ent - stop); if (!(R > 0.25)) break; double tgt = tg == 0 ? ent + D * 1.5 * R : ent + D * P, px = NAN; size_t kx = n - 1;
              bool sameBarStop = (em == 1 || em == 2) && (D > 0 ? s.l[I[ke]] <= stop : s.h[I[ke]] >= stop);
              if (sameBarStop) { px = stop; kx = ke; }
              else for (size_t kk = ke + 1; kk < n; ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= stop : s.h[j] >= stop) { px = stop; kx = kk; break; } if (D > 0 ? s.h[j] >= tgt : s.l[j] <= tgt) { px = tgt; kx = kk; break; } if (tday_min(s.t[j]) >= hm_to_tday(15, 55)) { px = s.c[j]; kx = kk; break; } }
              if (std::isnan(px)) px = s.c[I[n - 1]]; double net = D * (px - ent) - 2.0 * A / 320; a.add(x.era, net > 0, net * 320 / A, net / R); stopSum += R * 320 / A; busy = kx; have = true; nd++; }
            if (done) break; } } }
      printf("  %-36s %5.2f/day | filled %5.1f%% | %4.1f%% | %+6.2f (%+.3fR) | %+6.2f / %+6.2f / %+6.2f (%3.0f) | stop %5.1f pts\n", EN[em], a.n / days.size(), 100 * a.n / std::max(1.0, a.sig), 100 * a.w / std::max(1.0, a.n),
             a.p / std::max(1.0, a.n), a.r / std::max(1.0, a.n), a.pe[0] / std::max(1.0, a.ne[0]), a.pe[1] / std::max(1.0, a.ne[1]), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2], stopSum / std::max(1.0, a.n)); } }
  return 0;
}
