// 2026-10-20 flag_wide_stop.cpp (founder: "how about you leave lets say 3 R risk and 1R win"): the /flags page's flags with a WIDER
// stop and a SMALLER target. Stop = entry − s × R0 (R0 = the page's risk: entry − flag low + 1 tick), s = 1 / 1.5 / 2 / 3; target =
// +0.5 R0 / +1 R0 / the measured move. Results in R0 units (a full 3R stop = −3). Same flags, sessions, entries as flag_exits.cpp
// (header there); one position at a time per chart using the PAGE's exit (so every grid cell trades the same entries).
// Controls (honest, see memory feedback_twin_lookahead): (a) same day, a random minute from the registration bar to +60 min, same
// direction, same stop/target distances; (b) NY only: the entry's clock minute on a random OTHER NY session. Costs: 2 pts × ATR/320
// (eras) and a fixed 1.5 pts (2025-26). PF = gross wins / gross losses (R0, after cost). Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 14:00 = 1200, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
static const double SM[4] = {1, 1.5, 2, 3}; static const double TG[3] = {0.5, 1, 0};   // 0 = measured move
enum { NS = 4, NT = 3, NE = NS * NT };
struct Ses { std::vector<double> H, L, O, C; std::vector<int> TM; };
struct Row { int date, year, era, sess, side, tf, dix; bool inner, pb1, pole5; double risk, mmR, cost, Ra[NE], Rb[NE], Ta[NE]; int kIn, kOut, tmIn; };
// one simulated trade on a session: entry minute k0 at e, stop e − sR0, target e + tgt (tgt <= 0 → none beyond flat), flat at FLAT
static double simT(const Ses& S, size_t k0, double e, double R0, double sm, double tgt, bool fillMin, double cost, int flat) {
  const size_t n = S.H.size(); const double st = e - sm * R0; double px = NAN;
  if (fillMin && S.L[k0] <= st) px = st;
  for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) {
    if (S.L[k] <= st) { px = st; break; }
    if (S.H[k] >= e + tgt) { px = e + tgt; break; }
    if (S.TM[k] >= flat) { px = S.O[k]; break; } }
  if (std::isnan(px)) px = S.C[n - 1];
  return (px - e - cost) / R0; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Ses> nyS[2];   // NY sessions per side (index = side>0 ? 0 : 1), for control (b)
  std::vector<Row> rows; long nDays = 0; std::mt19937 tw_rng(20261020);
  const int S0[2] = {930, 0}, S1[2] = {1320, 930}, RG0[2] = {945, 15}, RG1[2] = {1200, 900}, FLAT[2] = {1315, 925};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; nDays++;
    const double A = d.atr, todayPt = A / 356.0; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    const double costA = 2 * A / 320.0, costB = 1.5;
    for (int sess = 0; sess < 2; ++sess) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[sess] && tm < S1[sess]) I.push_back(i); }
      if (I.size() < (sess == 0 ? 370u : 800u)) continue;
      const size_t n = I.size();
      for (int side = 1; side >= -1; side -= 2) {
        std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
        for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
          if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
        { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18;
          for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
            const double vw = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > vw; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
        Ses S{Hh, Lw, O, Cc, TM}; int dix = -1;
        if (sess == 0) { nyS[side > 0 ? 0 : 1].push_back(S); dix = (int)nyS[side > 0 ? 0 : 1].size() - 1; }
        for (int tf = 1; tf <= 5; ++tf) for (int inner = 0; inner <= (tf <= 2 ? 1 : 0); ++inner) {
          std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - S0[sess]) / tf != (TM[b.back().k0] - S0[sess]) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } }
          int busy = -1, lastOut = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
          for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
            const int tmc = TM[b[q].k] + 1; if (tmc < RG0[sess] || tmc >= RG1[sess]) continue;
            const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
            int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
            bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
            double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
            const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
            const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
            int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
            // the page's follow (busy) and entries
            int nb = 0; size_t kPole = 0; double sPole = NAN, flo = fl0;
            for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; break; } flo = std::min(flo, Lw[kk]);
              if ((TM[kk] - S0[sess]) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
            busy = q + nb;
            size_t k2m = 0; double e2m = NAN, s2m = NAN;
            if (tf >= 3) { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
              for (size_t k = kc + 1; k < n; ++k) { const int mm = TM[k] - S0[sess]; if (TM[k] + 1 >= RG1[sess] + 60) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
                if (mm % 2 == 1) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
                if (mm % tf == tf - 1) { nb2++; fl = std::min(fl, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
            const bool use2m = k2m && (!kPole || k2m <= kPole);
            const size_t kE = use2m ? k2m : kPole; if (!kE) continue; const double e = use2m ? e2m : H + 0.25, st = use2m ? s2m : sPole;
            if (!(e - st > 0.25) || (int)kE <= lastOut) continue;   // one position at a time per chart / tier
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.era = era; r.sess = sess; r.side = side; r.tf = tf; r.inner = inner; r.pb1 = pb == 1; r.pole5 = M >= 5 * ar; r.risk = e - st; r.kIn = kE;
            r.mmR = (H + M - e) / r.risk; r.dix = dix; r.cost = costA; r.tmIn = TM[kE];
            { const double R0 = r.risk; double px = NAN; size_t k = kE; if (!use2m && Lw[kE] <= st) { px = st; } else for (k = kE + 1; k < n; ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= H + M) { px = H + M; break; } if (TM[k] >= FLAT[sess]) break; } r.kOut = std::min(k, n - 1); (void)R0; (void)px; }
            for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) { const int x = si * NT + ti; const double tgt = TG[ti] > 0 ? TG[ti] * r.risk : H + M - e;
              r.Ra[x] = simT(S, kE, e, r.risk, SM[si], tgt, !use2m, costA, FLAT[sess]); r.Rb[x] = simT(S, kE, e, r.risk, SM[si], tgt, !use2m, costB, FLAT[sess]);
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t lo2 = kc, hi2 = std::min(n - 2, (size_t)kc + 60); const size_t k = lo2 + tw_rng() % (hi2 - lo2 + 1); tw += simT(S, k, Cc[k], r.risk, SM[si], tgt, false, costA, FLAT[sess]); } r.Ta[x] = tw / 5; }
            lastOut = r.kOut; rows.push_back(r); } } } } }
  // control (b): NY rows, same clock minute on a random other NY session of the same side
  std::vector<std::array<double, NE>> Tb(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) { auto& r = rows[i]; if (r.sess != 0) continue; auto& pool = nyS[r.side > 0 ? 0 : 1];
    for (int x = 0; x < NE; ++x) Tb[i][x] = 0;
    for (int z = 0; z < 5; ++z) { int j; do j = tw_rng() % pool.size(); while (j == r.dix); const Ses& S = pool[j]; size_t k = 0; while (k + 1 < S.TM.size() && S.TM[k] < r.tmIn) ++k;
      for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) { const int x = si * NT + ti; const double tgt = (TG[ti] > 0 ? TG[ti] : r.mmR) * r.risk;
        Tb[i][x] += simT(S, k, S.C[k], r.risk, SM[si], tgt, false, r.cost, 1315) / 5; } } }
  printf("Wider stop / smaller target on the /flags page's flags — NQ 2010-07 → 2026-10, %ld days, %zu trades. R in units of the PAGE's risk R0 (a 3R stop loses 3).\n", nDays, rows.size());
  printf("Cells: avg R0 2010-18 | 2019-24 | 2025-26 (cost 2 pts × ATR/320) · 2025-26 fixed 1.5 pts · win %% · PF · worst year · vs control (a) same day +0-60 min · vs (b) other NY day, same minute.\n");
  static const char* TN[3] = {"0.5R", "1R  ", "MM  "};
  auto line = [&](std::function<bool(const Row&)> sel, int x) { double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}, b2 = 0, n2 = 0, w = 0, N = 0, ta = 0, tb = 0, nb = 0, gw = 0, gl = 0; std::map<int, double> yr;
    for (size_t i = 0; i < rows.size(); ++i) { auto& r = rows[i]; if (!sel(r)) continue; m[r.era] += r.Ra[x]; c[r.era]++; N++; w += r.Ra[x] > 0; ta += r.Ra[x] - r.Ta[x]; yr[r.year] += r.Ra[x];
      if (r.Ra[x] > 0) gw += r.Ra[x]; else gl -= r.Ra[x]; if (r.sess == 0) { tb += r.Ra[x] - Tb[i][x]; nb++; } if (r.era == 2) { b2 += r.Rb[x]; n2++; } }
    if (N < 30) return; double wy = 1e9; int wyr = 0; for (auto& [y, v] : yr) if (v < wy) { wy = v; wyr = y; }
    printf("  stop %.1fR  target %s  %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f) · fixed %+.3f · win %2.0f%% · PF %.2f · worst %d %+.0fR · vs (a) %+.3f", SM[x / NT], TN[x % NT], c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2], n2 ? b2 / n2 : NAN, 100 * w / N, gl > 0 ? gw / gl : NAN, wyr, wy, ta / N);
    if (nb) printf(" · vs (b) %+.3f", tb / nb); printf("\n"); };
  struct Grp { const char* name; std::function<bool(const Row&)> f; };
  std::vector<Grp> groups = {
    {"NY bull 4-5m TAKE-like (1st pb, pole >= 5x)", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.pb1 && r.pole5; }},
    {"NY bull 3-5m", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3; }},
    {"NY bear 3-5m", [](const Row& r) { return r.sess == 0 && r.side < 0 && r.tf >= 3; }},
    {"NY 1-2m (both sides, both tiers)", [](const Row& r) { return r.sess == 0 && r.tf <= 2; }},
    {"Globex/Asia/London 3-5m (both sides)", [](const Row& r) { return r.sess == 1 && r.tf >= 3; }},
    {"Globex/Asia/London 1-2m (both sides, both tiers)", [](const Row& r) { return r.sess == 1 && r.tf <= 2; }} };
  for (auto& g : groups) { printf("\n== %s\n", g.name); for (int x = 0; x < NE; ++x) line(g.f, x); }
  return 0;
}
