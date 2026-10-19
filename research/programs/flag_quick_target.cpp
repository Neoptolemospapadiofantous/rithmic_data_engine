// 2026-10-20 (founder, after Globex 2026-10-19 evening: 8 flag entries, all stopped at the flag low after the measured-move target was
// missed — "the fill or the way this is being played out is incorrect. I see that each entry at least had 5-10 points"; 4 of 6 ran
// +12 to +22 pts first). Question: on the /flags page's flags, does a SMALL target (5-15 pts at today's volatility) beat the measured
// move? Exits were only tested on NY 3-5m flags before (flag_take_opt §2: measured move > 1.5/2/3R); 1-2m and Globex never.
// Flags = the page's exact detection (bot/frontend/src/lib/flags.ts, parity-checked by scripts/flag_parity.sh): researched tier
// (pole >= 4x bar, open + VWAP + straightness >= 0.55) and inner tier (1-2m, pole >= 2x bar, open + VWAP only, rejected by the
// researched rules). Sessions: NY 09:30-16:00 (registration 09:45-14:00, flat 15:55) and Globex 18:00-09:30 (registration 18:15-09:00,
// flat 09:25); the session open / VWAP / low of day are the session's own. Entry: 3-5m the 2m trigger if before the pole-top break,
// else the pole-top stop order (H + 1 tick); 1-2m the pole-top stop order. Stop = the page's (flag low − 1 tick). One position at a
// time per chart, session and side.
// Exits: measured move (H + pole) | fixed target 5 / 8 / 10 / 15 "points at today's volatility" (k × ATR / 356, ATR 356 = 2026-10-19)
// | 0.5R / 1R. Costs: (a) 2 pts × ATR / 320 (as all earlier flag programs), (b) a FIXED 1.5 NQ points per round trip (commission +
// a tick or two — closer to what a contract really pays; much larger than (a) in 2010-18 when NQ was 2,000-7,000).
// Also: share of trades whose best move before the stop reaches +5 / +10 (today-points). Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 14:00 = 1200, 15:55 = 1315, 16:00 = 1320; Globex 18:15 = 15, 09:00 = 900, 09:25 = 925.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
enum { X_MM, X_5, X_8, X_10, X_15, X_05R, X_1R, NX };
static const char* XN[NX] = {"measured move (the page)", "target 5 pts*", "target 8 pts*", "target 10 pts*", "target 15 pts*", "target 0.5R", "target 1R"};
struct Row { int date, era, sess, side, tf; bool inner, pb1; double risk, mfe, Ra[NX], Rb[NX], Rg[NX]; int kIn, kOut; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Row> rows; long nDays = 0;
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
        // exit: from entry minute k0 at price e; stop st; target tg; fillMin = the entry minute itself can stop (pole-top entries)
        auto sim = [&](size_t k0, double e, double st, double tg, bool fillMin, double cost, int& kOut) -> double { double px = NAN; kOut = n - 1;
          if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; kOut = k; break; } if (Hh[k] >= tg) { px = tg; kOut = k; break; } if (TM[k] >= FLAT[sess]) { px = O[k]; kOut = k; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / (e - st); };
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
            Row r{}; r.date = d.date; r.era = era; r.sess = sess; r.side = side; r.tf = tf; r.inner = inner; r.pb1 = pb == 1; r.risk = e - st; r.kIn = kE;
            { double mfe = use2m ? 0 : Hh[kE] - e; for (size_t k = kE + 1; k < n; ++k) { if (Lw[k] <= st) break; mfe = std::max(mfe, Hh[k] - e); if (TM[k] >= FLAT[sess]) break; } r.mfe = mfe / todayPt; }
            int ko = kE;
            for (int x = 0; x < NX; ++x) { const double tg = x == X_MM ? H + M : x == X_5 ? e + 5 * todayPt : x == X_8 ? e + 8 * todayPt : x == X_10 ? e + 10 * todayPt : x == X_15 ? e + 15 * todayPt : x == X_05R ? e + 0.5 * (e - st) : e + (e - st);
              r.Ra[x] = sim(kE, e, st, tg, !use2m, costA, ko); if (x == X_MM) r.kOut = ko; r.Rb[x] = sim(kE, e, st, tg, !use2m, costB, ko); r.Rg[x] = sim(kE, e, st, tg, !use2m, 0.0, ko); }
            lastOut = r.kOut; rows.push_back(r); } } } } }
  printf("Small targets on the /flags page's flags — NQ 2010-07 → 2026-10, %ld days, %zu trades (NY + Globex, 1-5m, researched + inner tiers).\n", nDays, rows.size());
  printf("* points at today's volatility (k × ATR/356). Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26, cost (a) 2×ATR/320 and (b) fixed 1.5 pts; win = share of 2025-26 trades closing above entry BEFORE costs.\n");
  printf("NB (b) in 2010-18: a 5 today-point target is only ~0.5-1.5 real points then, so a fixed 1.5-pt cost makes it a loss by construction — read 2025-26 for today.\n");
  printf("Best move before the stop is in today-points too.\n");
  auto block = [&](const char* title, std::function<bool(const Row&)> sel) {
    double mf5 = 0, mf10 = 0, nn = 0; for (auto& r : rows) if (sel(r)) { nn++; mf5 += r.mfe >= 5; mf10 += r.mfe >= 10; }
    if (nn < 30) return;
    printf("\n== %s — %.0f trades · reach +5 before the stop %.0f%% · reach +10 %.0f%%", title, nn, 100 * mf5 / nn, 100 * mf10 / nn);
    { double a5 = 0, a10 = 0, an = 0; for (auto& r : rows) if (sel(r) && r.era == 2) { an++; a5 += r.mfe >= 5; a10 += r.mfe >= 10; } if (an) printf(" (2025-26: %.0f%% / %.0f%% of %.0f)", 100 * a5 / an, 100 * a10 / an, an); }
    printf("\n");
    for (int x = 0; x < NX; ++x) { double ma[3] = {0, 0, 0}, mb[3] = {0, 0, 0}, c[3] = {0, 0, 0}, w = 0, N = 0; for (auto& r : rows) if (sel(r)) { ma[r.era] += r.Ra[x]; mb[r.era] += r.Rb[x]; c[r.era]++; w += r.Rb[x] > 0; N++; }
      double g2 = 0, gw2 = 0, gR2 = 0, gP2 = 0, rk2 = 0; for (auto& r : rows) if (sel(r) && r.era == 2) { g2++; gw2 += r.Rg[x] > 0; gR2 += r.Rg[x]; gP2 += r.Rg[x] * r.risk; rk2 += r.risk; } (void)w; (void)N;
      printf("  %-26s (a) %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f)   (b) %+.3f | %+.3f | %+.3f · 2025-26 gross: win %2.0f%%, %+.3fR, %+.2f pts/trade (stop %.1f pts)\n", XN[x], c[0] ? ma[0] / c[0] : NAN, c[0], c[1] ? ma[1] / c[1] : NAN, c[1], c[2] ? ma[2] / c[2] : NAN, c[2],
             c[0] ? mb[0] / c[0] : NAN, c[1] ? mb[1] / c[1] : NAN, c[2] ? mb[2] / c[2] : NAN, g2 ? 100 * gw2 / g2 : 0, g2 ? gR2 / g2 : NAN, g2 ? gP2 / g2 : NAN, g2 ? rk2 / g2 : NAN); } };
  static const char* SN[2] = {"NY", "GLOBEX"};
  for (int sess = 0; sess < 2; ++sess) for (int side : {1, -1}) {
    for (int tf = 1; tf <= 2; ++tf) { char t[128];
      snprintf(t, 128, "%s %s %dm INNER flags (untested tier)", SN[sess], side > 0 ? "bull" : "bear", tf); block(t, [=](const Row& r) { return r.sess == sess && r.side == side && r.tf == tf && r.inner; });
      snprintf(t, 128, "%s %s %dm researched flags", SN[sess], side > 0 ? "bull" : "bear", tf); block(t, [=](const Row& r) { return r.sess == sess && r.side == side && r.tf == tf && !r.inner; }); }
    { char t[128]; snprintf(t, 128, "%s %s 3-5m researched flags", SN[sess], side > 0 ? "bull" : "bear"); block(t, [=](const Row& r) { return r.sess == sess && r.side == side && r.tf >= 3; }); } }
  return 0;
}
