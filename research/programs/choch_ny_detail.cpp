// 2026-10-19: detail on the one family that passed flag_structure_matrix.cpp — NY bull CHoCH with the session direction (price above
// the 09:30 open; the chart's structure had turned down; a close above the last swing high = the change of character → buy at that close).
// Same rules as the matrix (2-bar pivots, entries 09:45-15:25, ≤ 5 a day, one at a time, flat 15:55, 2 pts cost, points at today's range).
// Here: 1-5m charts, exits 2R and session end, the structural stop vs the stop capped at 20 / 30 / 40 pts (smaller risk per contract),
// net by year, and the share of trades reaching +20 pts. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; int k; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int S0 = 930, SE = 1315; const int NV = 8; const char* VN[NV] = {"swing stop, 2R", "swing stop, session end", "stop<=40, 2R", "stop<=40, session end", "stop<=30, 2R", "stop<=30, session end", "stop<=20, 2R", "stop<=20, session end"};
  for (int tf = 1; tf <= 5; ++tf) { struct A { double n = 0, p = 0, w = 0, R = 0, big = 0; std::map<int, std::pair<double, double>> yr; } acc[NV]; double nd = 0;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0 && tm < SE) I.push_back(i); } if (I.size() < 300) continue; nd++;
      const double sc = d.atr / 320.0, so = s.o[I[0]]; std::vector<FB> b; double PH = NAN, PL = NAN; bool PHu = true; int trend = 0, nday = 0; size_t busy = 0; bool have = false;
      for (size_t k = 0; k < I.size(); ++k) { int i = I[k], tm = tday_min(s.t[i]);
        if ((tm - S0) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
        if ((tm + 1 - S0) % tf) continue; int q = b.size() - 1;
        if (q >= 4) { int p = q - 2; if (b[p].h > b[p - 1].h && b[p].h > b[p - 2].h && b[p].h > b[p + 1].h && b[p].h > b[p + 2].h) { PH = b[p].h; PHu = false; } if (b[p].l < b[p - 1].l && b[p].l < b[p - 2].l && b[p].l < b[p + 1].l && b[p].l < b[p + 2].l) PL = b[p].l; }
        const double c = b[q].c; bool sig = false;
        if (!std::isnan(PH) && !PHu && c > PH) { PHu = true; sig = trend == -1 && !std::isnan(PL) && c > so; trend = 1; } else if (!std::isnan(PL) && c < PL) { if (trend != -1) trend = -1; PL = NAN; }
        if (!sig || tm < S0 + 15 || tm >= SE - 30 || nday >= 5 || (have && k <= busy)) continue; double Rs = c - (PL - 0.25); (void)Rs;
        double swingR = NAN; { /* last swing low before the break */ } 
        // the swing low used is the last confirmed pivot low; PL was cleared on a break down, so recompute: lowest low since the last pivot high was set
        double lo = 1e18; for (int r = q; r >= 0 && r >= q - 30; --r) { lo = std::min(lo, b[r].l); if (b[r].h >= PH - 1e-9 && r < q) break; } swingR = c - (lo - 0.25); if (!(swingR > 0.25) || swingR > 2 * d.atr) continue;
        size_t kmax = 0;
        for (int v = 0; v < NV; ++v) { double cap = v < 2 ? 1e18 : (v < 4 ? 40 : v < 6 ? 30 : 20) * sc, R = std::min(swingR, cap), st = c - R, tg = v % 2 == 0 ? c + 2 * R : NAN, px = NAN, mfe = 0; size_t kx = I.size() - 1;
          for (size_t kk = k + 1; kk < I.size(); ++kk) { int j = I[kk]; if (s.l[j] <= st) { px = st; kx = kk; break; } mfe = std::max(mfe, s.h[j] - c); if (!std::isnan(tg) && s.h[j] >= tg) { px = tg; kx = kk; break; } }
          if (std::isnan(px)) px = s.c[I.back()]; double net = (px - c - 2 * sc) / sc; A& a = acc[v]; a.n++; a.p += net; a.w += net > 0; a.R += R / sc; a.big += mfe >= 20 * sc; auto& y = a.yr[d.date / 10000]; y.first += net; y.second++; if (v == 0) kmax = kx; }
        busy = kmax; have = true; nday++; } }
    printf("\n== NY %dm bull CHoCH with the session direction — %.2f trades a day\n", tf, acc[0].n / nd);
    for (int v = 0; v < NV; ++v) { A& a = acc[v]; int pos = 0, ny = 0; std::string ys; for (auto& [y, pr] : a.yr) { ny++; pos += pr.first > 0; char t[24]; snprintf(t, sizeof t, " %02d:%+.0f", y % 100, pr.first / pr.second); ys += t; }
      printf("  %-24s stop %5.1f | win %4.1f%% | net %+6.2f pts | %.0f%% reach +20 | years + %d/%d |%s\n", VN[v], a.R / a.n, 100 * a.w / a.n, a.p / a.n, 100 * a.big / a.n, pos, ny, ys.c_str()); } }
  return 0;
}
