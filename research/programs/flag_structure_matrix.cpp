// 2026-10-19 (founder: "if it's not NY we only take 10-20 points max", "maybe a lesser pole?", "try market structure BOS / CHoCH",
// "test all", "matrix"): one matrix over Asia / London / NY × 1-2-3-4-5-15m × bull / bear × 7 setups × 6 exits.
// Setups (all decided at a chart bar's close, entered at that close):
//  WICK4 / WICK3 / WICK2  wick-close flag of flag_wickentry.cpp (W1), pole ≥ 4 / 3 / 2 × the session's average chart bar; with the
//                         session direction on a straight path (as before). Stop beyond the wick candle.
//  BOS-dir / BOS-any      close beyond the last confirmed swing (2-bar pivots) IN the current structure trend; stop beyond the
//                         opposite swing. "dir" = only with the session direction (open side), "any" = no filter.
//  CHOCH-dir / CHOCH-any  close beyond the last swing AGAINST the current structure trend (the first break the other way).
// Exits: target 10 / 15 / 20 NQ points at today's range (× ATR/320), 1R, 2R, hold to the session end — structural stop always.
// Flat at the session end (Asia 03:00, London 09:30, NY 15:55). One trade at a time per cell (busy until the 2R exit), ≤ 5 a day,
// entries from session open +15 min to session end −30 min. Net of 2 pts cost. Control = same-session random minute, same side,
// same stop distance in ATR, same exits (3 draws) on a RANDOM day of the same session (a same-day control is hindsight). Units: points at today's range. Writes out/flag_structure_matrix.csv (every cell).
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; int k; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {540, 930, 1315}; const char* SN[3] = {"Asia", "London", "NY"};
  const int NS = 7, NX = 6; const char* SET[NS] = {"WICK4", "WICK3", "WICK2", "BOS-dir", "BOS-any", "CHOCH-dir", "CHOCH-any"}; const char* XN[NX] = {"T10", "T15", "T20", "1R", "2R", "END"};
  struct DI { std::vector<int> I, D; std::vector<char> ctx; int era, se, date; double A; }; std::vector<DI> days; double ndays[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    for (int se = 0; se < 3; ++se) { DI x; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) x.I.push_back(i); } if (x.I.size() < 60) continue;
      const size_t n = x.I.size(); x.D.resize(n); x.ctx.resize(n); double so = s.o[x.I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < n; ++k) { int i = x.I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
        x.D[k] = c > so ? 1 : c < so ? -1 : 0; x.ctx[k] = x.D[k] && (c - vw) * x.D[k] > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55; }
      x.era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; x.se = se; x.date = d.date; x.A = d.atr; ndays[se]++; days.push_back(std::move(x)); } }
  // one exit run: returns net pts at today's range for each of the NX exits, and the 2R exit index
  auto run = [&](const DI& x, size_t k0, double e, int D, double R, double out[NX], size_t& k2R) {
    const double sc = x.A / 320.0, cost = 2.0 * sc; const auto& I = x.I; const size_t n = I.size();
    for (int xx = 0; xx < NX; ++xx) { double tg = xx < 3 ? (10 + 5 * xx) * sc : xx == 3 ? R : xx == 4 ? 2 * R : NAN, st = e - D * R, px = NAN; size_t kx = n - 1;
      for (size_t k = k0 + 1; k < n; ++k) { int j = I[k]; if (D > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = k; break; } if (!std::isnan(tg) && (D > 0 ? s.h[j] >= e + tg : s.l[j] <= e - tg)) { px = e + D * tg; kx = k; break; } }
      if (std::isnan(px)) px = s.c[I[n - 1]]; out[xx] = (D * (px - e) - cost) / sc; if (xx == 4) k2R = kx; } };
  struct Acc { double n = 0, w[NX] = {}, p[NX] = {}, pe[NX][3] = {}, ne[3] = {}, c[NX] = {}, cn = 0, R = 0; };
  std::vector<Acc> M(3 * 6 * 2 * NS); auto cell = [&](int se, int ti, int D, int st) -> Acc& { return M[((se * 6 + ti) * 2 + (D > 0)) * NS + st]; };
  std::mt19937 rng(20261019); std::vector<size_t> byS[3]; for (size_t j = 0; j < days.size(); ++j) byS[days[j].se].push_back(j);
  for (const DI& x : days) { const int se = x.se; const auto& I = x.I; const size_t n = I.size(); const double sc = x.A / 320.0;
    for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<FB> b; size_t busy[NS] = {}; bool have[NS] = {}; int nd[NS] = {};
      // structure state per bar list
      double PH = NAN, PL = NAN; bool PHu = true, PLu = true; int trend = 0;
      for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
        if ((tm - S0[se]) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
        if ((tm + 1 - S0[se]) % tf) continue; const int q = b.size() - 1; const bool win = tm >= S0[se] + 15 && tm < SE[se] - 30;
        struct Sig { int st, D; double stop; }; std::vector<Sig> sig;
        // --- market structure: confirm the pivot at q-2, then check breaks at this close
        if (q >= 4) { int p = q - 2; if (b[p].h > b[p - 1].h && b[p].h > b[p - 2].h && b[p].h > b[p + 1].h && b[p].h > b[p + 2].h) { PH = b[p].h; PHu = false; }
          if (b[p].l < b[p - 1].l && b[p].l < b[p - 2].l && b[p].l < b[p + 1].l && b[p].l < b[p + 2].l) { PL = b[p].l; PLu = false; } }
        const double c = b[q].c;
        if (!std::isnan(PH) && !PHu && c > PH) { PHu = true; if (!std::isnan(PL) && trend != 0) { int typ = trend == 1 ? 3 : 5; sig.push_back({typ + 1, 1, PL - 0.25}); if (x.D[k] == 1) sig.push_back({typ, 1, PL - 0.25}); } trend = 1; }
        else if (!std::isnan(PL) && !PLu && c < PL) { PLu = true; if (!std::isnan(PH) && trend != 0) { int typ = trend == -1 ? 3 : 5; sig.push_back({typ + 1, -1, PH + 0.25}); if (x.D[k] == -1) sig.push_back({typ, -1, PH + 0.25}); } trend = -1; }
        // --- wick-close flags, pole ≥ 4 / 3 / 2 × average bar
        if (x.ctx[k] && q >= 6) { const int D = x.D[k]; double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= (q + 1);
          const FB& w = b[q]; double rg = w.h - w.l; bool wickok = false; if (rg > 0) { double wk = D > 0 ? std::min(w.o, w.c) - w.l : w.h - std::max(w.o, w.c); double pos = D > 0 ? (w.c - w.l) / rg : (w.h - w.c) / rg; wickok = wk >= 0.5 * rg && pos >= 0.5; }
          if (wickok) for (int pm = 0; pm < 3; ++pm) { const double PMUL = 4 - pm; bool done = false;
            for (int F = 2; F <= 12 && !done; ++F) { int h = q - F; if (h < 3) break; double H = D > 0 ? b[h].h : b[h].l; bool ok = true; for (int r = h + 1; r <= q; ++r) if (D > 0 ? b[r].h > H : b[r].l < H) { ok = false; break; } if (!ok) continue;
              int l = -1; double L = D > 0 ? 1e18 : -1e18; for (int r = std::max(0, h - 15); r <= h - 3; ++r) if (D > 0 ? b[r].l < L : b[r].h > L) { L = D > 0 ? b[r].l : b[r].h; l = r; } if (l < 0) continue;
              for (int r = l; r <= h; ++r) if (D > 0 ? (b[r].l < L || b[r].h > H) : (b[r].h > L || b[r].l < H)) ok = false; if (!ok) continue;
              double Mp = D * (H - L); if (Mp < PMUL * ar || Mp <= 0) continue; int up = 0; for (int r = l; r <= h; ++r) if (D * (b[r].c - b[r].o) > 0) up++; if (up < 0.6 * (h - l + 1)) continue;
              double FH = -1e18, FL = 1e18; for (int r = h + 1; r <= q; ++r) { FH = std::max(FH, b[r].h); FL = std::min(FL, b[r].l); }
              double pull = std::fabs(D > 0 ? H - FL : FH - H); if (pull < 0.15 * Mp || pull > 0.5 * Mp || FH - FL > 0.5 * Mp) continue;
              sig.push_back({pm, D, D > 0 ? w.l - 0.25 : w.h + 0.25}); done = true; } } }
        if (!win) continue;
        for (auto& g : sig) { if ((have[g.st] && k <= busy[g.st]) || nd[g.st] >= 5) continue; const double e = c, R = g.D * (e - g.stop); if (!(R > 0.25) || R > 2.0 * x.A) continue;
          double out[NX]; size_t k2; run(x, k, e, g.D, R, out, k2); busy[g.st] = k2; have[g.st] = true; nd[g.st]++;
          Acc& a = cell(se, ti, g.D, g.st); a.n++; a.ne[x.era]++; a.R += R / sc; for (int xx = 0; xx < NX; ++xx) { a.p[xx] += out[xx]; a.w[xx] += out[xx] > 0; a.pe[xx][x.era] += out[xx]; }
          for (int dr = 0; dr < 3; ++dr) { const DI& y = days[byS[se][std::uniform_int_distribution<size_t>(0, byS[se].size() - 1)(rng)]]; const auto& J = y.I; size_t lo = 0, hi = 0; for (size_t kk = 0; kk < J.size(); ++kk) { int t2 = tday_min(s.t[J[kk]]); if (t2 < S0[se] + 15) lo = kk + 1; if (t2 < SE[se] - 30) hi = kk; } if (hi <= lo) continue;
            size_t kr = std::uniform_int_distribution<size_t>(lo, hi)(rng); double er = s.c[J[kr]], oc[NX]; size_t kk2; run(y, kr, er, g.D, R * y.A / x.A, oc, kk2); a.cn++; for (int xx = 0; xx < NX; ++xx) a.c[xx] += oc[xx]; } } } } }
  FILE* csv = fopen("research/programs/out/flag_structure_matrix.csv", "w");
  fprintf(csv, "session,chart,side,setup,exit,trades_per_day,avg_stop_pts,win_pct,net_pts,net_2010_18,net_2019_24,net_2025_26,n_2025_26,control_pts,edge_vs_control\n");
  printf("Flag / market-structure matrix — NQ 2010-07 → 2026-10, 3 sessions × 6 charts × 2 sides × 7 setups × 6 exits. Points at today's range, net of 2 pts.\n");
  printf("A cell PASSES when net > 0 in 2010-18, 2019-24 AND 2025-26 (≥ 20 trades), beats its random control by ≥ 2 pts, and has ≥ 100 trades.\n");
  for (int se = 0; se < 3; ++se) { printf("\n==================== %s\n", SN[se]); int npass = 0, ncell = 0; std::vector<std::pair<double, std::string>> best;
    for (int ti = 0; ti < 6; ++ti) for (int D = -1; D <= 1; D += 2) for (int st = 0; st < NS; ++st) { Acc& a = cell(se, ti, D, st); if (a.n < 1) continue;
      for (int xx = 0; xx < NX; ++xx) { ncell++; double net = a.p[xx] / a.n, e0 = a.ne[0] ? a.pe[xx][0] / a.ne[0] : NAN, e1 = a.ne[1] ? a.pe[xx][1] / a.ne[1] : NAN, e2 = a.ne[2] ? a.pe[xx][2] / a.ne[2] : NAN, ct = a.cn ? a.c[xx] / a.cn : NAN;
        fprintf(csv, "%s,%d,%s,%s,%s,%.3f,%.1f,%.1f,%.2f,%.2f,%.2f,%.2f,%.0f,%.2f,%.2f\n", SN[se], TFS[ti], D > 0 ? "bull" : "bear", SET[st], XN[xx], a.n / ndays[se], a.R / a.n, 100 * a.w[xx] / a.n, net, e0, e1, e2, a.ne[2], ct, net - ct);
        bool pass = a.n >= 100 && a.ne[2] >= 20 && e0 > 0 && e1 > 0 && e2 > 0 && net - ct >= 2; npass += pass;
        char ln[256]; snprintf(ln, sizeof ln, "  %s %2dm %-4s %-9s %-3s  %.2f/day stop %4.1f | win %4.1f%% | net %+6.2f | eras %+6.2f %+6.2f %+6.2f (%.0f) | ctrl %+6.2f%s", SN[se], TFS[ti], D > 0 ? "bull" : "bear", SET[st], XN[xx], a.n / ndays[se], a.R / a.n, 100 * a.w[xx] / a.n, net, e0, e1, e2, a.ne[2], ct, pass ? "  PASS" : "");
        if (a.n >= 100) best.push_back({e0, ln}); } }
    printf("  %d of %d cells pass.\n  Top 12 cells by 2010-18 net (selection on the first era only — the later columns are the test):\n", npass, ncell);
    std::sort(best.begin(), best.end(), [](auto& a, auto& b) { return a.first > b.first; }); for (size_t j = 0; j < best.size() && j < 12; ++j) printf("%s\n", best[j].second.c_str());
    printf("  Setup summary (mean over charts and sides, best exit per setup, net pts | edge vs control):\n");
    for (int st = 0; st < NS; ++st) { double bn = -1e9, be = 0; int bx = 0; for (int xx = 0; xx < NX; ++xx) { double sn = 0, sv = 0, sc2 = 0; for (int ti = 0; ti < 6; ++ti) for (int D = -1; D <= 1; D += 2) { Acc& a = cell(se, ti, D, st); if (!a.n) continue; sn += a.n; sv += a.p[xx]; sc2 += a.cn ? a.c[xx] / a.cn * a.n : 0; } if (sn && sv / sn > bn) { bn = sv / sn; be = (sv - sc2) / sn; bx = xx; } }
      double tn = 0; for (int ti = 0; ti < 6; ++ti) for (int D = -1; D <= 1; D += 2) tn += cell(se, ti, D, st).n; printf("    %-9s %5.2f trades/day (all charts) | best exit %-3s %+6.2f pts | vs control %+5.2f\n", SET[st], tn / ndays[se], XN[bx], bn, be); } }
  fclose(csv); return 0;
}
