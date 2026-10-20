// 2026-10-20 flag_wr_pf.cpp (founder: "what else can we do to increase WR and PF without over fiting" → "yes run all three to test").
// Three rules written BEFORE the run, each with at most one setting, on the /flags page's NY flags (flag_all_kpi.cpp detection,
// parity-checked), judged in every period at a FIXED cost (NQ 1.5 pts, ES 0.5 pt) and against random-timing twins:
//  T1 RETEST ENTRY  instead of the page entry (2m trigger or pole-top stop), wait for the pole-top break, then a LIMIT at the pole top H
//                   filled only when price trades through it (low <= H − 0.25) within N minutes (N = 15 pre-registered; 5 / 30 shown
//                   as sensitivity). Cancelled when the target (H + M) prints first or at 15:55. Stop = the flag low at the break − 0.25.
//                   A flag that never retests is a missed trade (fill rate shown).
//  T2 DAY TYPE      the 2026-10-18 path × direction result (confirmed 2019-24 + holdout): at the 10:00 candle close the 09:30-10:00 path is
//                   straight (|close − open| / range >= 0.55) and on the flag's side; "+ gap" also needs the gap on the flag's side.
//                   Judged on flags registered from 10:00 (earlier ones cannot know it).
//  T3 FIXED SCORE   +1 each, equal weights, nothing fitted: 60-min move with the flag > 0.1 ATR, 1st pullback, pole broke a level; on
//                   aggressor-flow days (2023-03+) also sellers in the flag (buy share < 0.49, the /flags checklist value).
//  ALL              T1 (N = 15) + T2 + score 3 together, as one rule.
// Groups: NY bull 3-5m, its 1st pullbacks, TAKE (bull 4/5m), NY bear 3-5m. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26.
// Usage: flag_wr_pf [NQ|ES]   (default NQ; ES runs the same rules unchanged — a market they were never fitted to)
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 10:00 = 960, 14:00 = 1200, 15:00 = 1260, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k0, k; };
enum Var { V_PAGE, V_RT5, V_RT15, V_RT30, NV };
static const char* VN[NV] = {"page entry", "retest N=5", "retest N=15 (pre-registered)", "retest N=30"};
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers; double R, pts, twin, twinD; };
struct DB { std::vector<double> O, H, L, C; std::vector<int> TM; };   // one NY session, mirrored per side (index 0 = bull, 1 = bear)
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double costB = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  // every NY session's bars, both sides, for the other-day twin
  std::vector<DB> all[2];
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z[2];
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
      z[0].O.push_back(s.o[i]); z[0].H.push_back(s.h[i]); z[0].L.push_back(s.l[i]); z[0].C.push_back(s.c[i]); z[0].TM.push_back(tm);
      z[1].O.push_back(-s.o[i]); z[1].H.push_back(-s.l[i]); z[1].L.push_back(-s.h[i]); z[1].C.push_back(-s.c[i]); z[1].TM.push_back(tm); }
    if (z[0].O.size() >= 370) for (int sd = 0; sd < 2; ++sd) all[sd].push_back(std::move(z[sd])); }
  // a long at the close of the bar at clock minute `tm` of session `z`, stop / target distances in points; flat 15:55
  auto simOther = [&](const DB& z, int tm, double risk, double tgd) -> double { size_t k0 = 0; while (k0 < z.TM.size() && z.TM[k0] < tm) ++k0; if (k0 >= z.TM.size()) return NAN;
    const double e = z.C[k0], st = e - risk, tg = e + tgd; for (size_t k = k0 + 1; k < z.TM.size(); ++k) { if (z.L[k] <= st) return st - e; if (z.H[k] >= tg) return tg - e; if (z.TM[k] >= 1315) return z.O[k] - e; }
    return z.C.back() - e; };
  std::vector<Row> rows; long nDays = 0, flagsBroke[NV] = {0}, flagsTotal = 0; std::mt19937 rng(20261020);
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue;
    const double A = d.atr;
    std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); }
    if (I.size() < 370u) continue; nDays++;
    const size_t n = I.size();
    double onh = -1e18, onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { if (tday_min(s.t[i]) >= 930) break; onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < 960; ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), BV(n), VW(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; BV[k] = side > 0 ? s.bv[i] : s.v[i] - s.bv[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18;
        for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
          VW[k] = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > VW[k]; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
      const double pcM = side * d.pc, gapA = std::isnan(d.pc) ? 0 : (O[0] - pcM) / A;
      // T2 day type at the 10:00 candle close (09:30-10:00 path, mirrored)
      bool path = false; { double hi = -1e18, lo = 1e18, c10 = NAN; for (size_t k = 0; k < n && TM[k] < 960; ++k) { hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); c10 = Cc[k]; }
        if (!std::isnan(c10)) path = std::fabs(c10 - O[0]) / std::max(hi - lo, 1.0) >= 0.55 && c10 > O[0]; }
      auto sim = [&](size_t k0, double e, double st, double tg, bool fillMin, int& kOut) -> double {
        double px = NAN; kOut = n - 1;
        if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; kOut = k; break; }
          if (Hh[k] >= tg) { px = tg; kOut = k; break; } if (TM[k] >= 1315) { px = O[k]; kOut = k; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return px - e; };
      for (int tf = 3; tf <= 5; ++tf) {
        std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - 930) / tf != (TM[b.back().k0] - 930) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], BV[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.bv += BV[k]; z.k = k; } }
        int busy = -1, lastOut[NV]; for (int v = 0; v < NV; ++v) lastOut[v] = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
        for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
          const int tmc = TM[b[q].k] + 1; if (tmc < 945 || tmc >= 1200) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 4 * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const int kc = b[q].k; if (!strict[kc]) continue;
          int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
          int nb = 0; size_t kPole = 0; double sPole = NAN, flo = fl0;
          for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; break; } flo = std::min(flo, Lw[kk]);
            if ((TM[kk] - 930) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M) break; if (nb >= 12) break; } }
          busy = q + nb; flagsTotal++;
          const int sessMin = TM[kc] + 1;
          size_t k2m = 0; double e2m = NAN, s2m = NAN;
          { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
            for (size_t k = kc + 1; k < n; ++k) { const int mm = TM[k] - 930; if (TM[k] + 1 >= 1260) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
              if (mm % 2 == 1) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
              if (mm % tf == tf - 1) { nb2++; fl = std::min(fl, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
          bool broke = std::floor(std::max(side * H, side * L) / 100) * 100 > std::min(side * H, side * L);
          for (double x : {side > 0 ? d.ph : -d.pl, side > 0 ? d.pl : -d.ph}) if (!std::isnan(x) && x > L && x <= H) broke = true;
          for (double x : {side > 0 ? onh : -onl, side > 0 ? onl : -onh}) if (x > L && x <= H) broke = true;
          if (sessMin >= 960) for (double x : {side > 0 ? orh : -orl, side > 0 ? orl : -orh}) if (x > L && x <= H) broke = true;
          const size_t k60 = kc >= 60 ? kc - 60 : 0; const double m60 = (Cc[kc] - Cc[k60]) / A;
          double fv = 0, fb = 0; for (int x = h + 1; x <= q; ++x) { fv += b[x].v; fb += b[x].bv; }
          Row base{}; base.date = d.date; base.year = d.date / 10000; base.side = side; base.tf = tf;
          base.pb1 = pb == 1; base.broke = broke; base.gapw = gapA > 0.1; base.pole5 = M >= 5 * ar; base.m60w = m60 > 0.1;
          base.early = sessMin < 960; base.path = path; base.pathgap = path && gapA > 0;
          base.flow = d.flow && fv > 0; base.sellers = base.flow && fb / fv < 0.49;
          for (int v = 0; v < NV; ++v) {
            size_t kE = 0; double e = NAN, st = NAN; bool fillMin = false;
            if (v == V_PAGE) { const bool use2m = k2m && (!kPole || k2m <= kPole); kE = use2m ? k2m : kPole; if (!kE) continue; e = use2m ? e2m : H + 0.25; st = use2m ? s2m : sPole; fillMin = !use2m; }
            else { if (!kPole) continue; flagsBroke[v]++; const int N = v == V_RT5 ? 5 : v == V_RT15 ? 15 : 30; st = sPole; e = H;
              for (size_t k = kPole + 1; k < n && TM[k] <= TM[kPole] + N && TM[k] < 1315; ++k) { if (Lw[k] <= H - 0.25) { kE = k; break; } if (Hh[k] >= H + M) break; }
              if (!kE) continue; fillMin = true; }
            if (!(e - st > 0.25) || (int)kE <= lastOut[v]) continue;
            int ko; Row r = base; r.var = v; const double risk = e - st;
            r.pts = sim(kE, e, st, H + M, fillMin, ko) - costB; r.R = r.pts / risk;
            // twins with the same stop / target distances: (a) same day, a random minute from the registration to 60 minutes after it (no
            // look-ahead — the flag is known then); (b) a random other session at the same clock minute (removes "this day trended")
            { double tw = 0, td = 0; int nd = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k];
                tw += (sim(k, ce, ce - risk, ce + (H + M - e), false, k2) - costB) / risk;
                const auto& od = all[side > 0 ? 0 : 1][rng() % all[side > 0 ? 0 : 1].size()]; const double x = simOther(od, TM[kE], risk, H + M - e); if (!std::isnan(x)) { td += (x - costB) / risk; nd++; } }
              r.twin = tw / 5; r.twinD = nd ? td / nd : 0; }
            lastOut[v] = ko; rows.push_back(r); } } } } }
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  printf("Flags: three rules for win rate / PF, written before the run — %s, %ld NY days, %ld flags (3-5m, researched tier).\n", sym.c_str(), nDays, flagsTotal);
  printf("Fixed cost %.1f pts a round trip. Cells per period 2010-15 | 2016-20 | 2021-24 | 2025-26: avg R / win %% / PF (trades). Twins = the same stop / target at a random minute in the hour after the flag (same day) and at the same clock time on a random other day.\n", costB);
  auto cell = [&](const char* name, std::function<bool(const Row&)> sel) {
    double sR[4] = {0}, w[4] = {0}, gp[4] = {0}, gl[4] = {0}, c[4] = {0}, tw = 0, twd = 0, pts = 0, ct = 0;
    for (auto& r : rows) { if (!sel(r)) continue; const int e = era(r.year); sR[e] += r.R; c[e]++; w[e] += r.R > 0; (r.R > 0 ? gp[e] : gl[e]) += r.R; tw += r.twin; twd += r.twinD; pts += r.pts; ct++; }
    if (ct < 20) { printf("    %-46s (%.0f trades)\n", name, ct); return; }
    printf("    %-46s", name);
    for (int e = 0; e < 4; ++e) { if (c[e] < 5) printf("   —          (%3.0f)", c[e]); else printf(" %+.2f %2.0f%% %4.2f (%4.0f)", sR[e] / c[e], 100 * w[e] / c[e], gl[e] < 0 ? gp[e] / -gl[e] : 99.0, c[e]); printf(" |"); }
    printf(" all %+.3fR %+.1f pts · twins: same day %+.3f, other day %+.3f · %.0f/yr\n", (sR[0] + sR[1] + sR[2] + sR[3]) / ct, pts / ct, tw / ct, twd / ct, ct / 16.8); };
  struct G { const char* name; std::function<bool(const Row&)> sel; };
  std::vector<G> groups = {
    {"NY bull 3-5m", [](const Row& r) { return r.side > 0; }},
    {"NY bull 3-5m, 1st pullback", [](const Row& r) { return r.side > 0 && r.pb1; }},
    {"TAKE (bull 4/5m, 1st pb, broke, gap with, pole >= 5x)", [](const Row& r) { return r.side > 0 && r.tf >= 4 && r.pb1 && r.broke && r.gapw && r.pole5; }},
    {"NY bear 3-5m", [](const Row& r) { return r.side < 0; }}};
  for (auto& g : groups) {
    auto pg = [&](const Row& r) { return g.sel(r) && r.var == V_PAGE; };
    printf("\n######## %s\n", g.name);
    printf("  baseline\n"); cell("page entry (all)", pg);
    printf("  T1 retest entry (limit at the pole top after the break)\n");
    for (int v = V_RT5; v < NV; ++v) cell(VN[v], [&, v](const Row& r) { return g.sel(r) && r.var == v; });
    printf("  T2 day type at 10:00 (flags registered from 10:00)\n");
    cell("from 10:00, all", [&](const Row& r) { return pg(r) && !r.early; });
    cell("straight path with it", [&](const Row& r) { return pg(r) && !r.early && r.path; });
    cell("no straight path with it", [&](const Row& r) { return pg(r) && !r.early && !r.path; });
    cell("straight path + gap with it", [&](const Row& r) { return pg(r) && !r.early && r.pathgap; });
    printf("  T3 fixed score: 60-min move with it + 1st pullback + broke a level (equal weights)\n");
    auto sc = [](const Row& r) { return (int)r.m60w + r.pb1 + r.broke; };
    cell("score 3", [&](const Row& r) { return pg(r) && sc(r) == 3; });
    cell("score 2", [&](const Row& r) { return pg(r) && sc(r) == 2; });
    cell("score 0-1", [&](const Row& r) { return pg(r) && sc(r) <= 1; });
    printf("  T3 with order flow (flow days 2023-03+): + sellers in the flag\n");
    cell("flow days, all", [&](const Row& r) { return pg(r) && r.flow; });
    cell("score 4", [&](const Row& r) { return pg(r) && r.flow && sc(r) + r.sellers == 4; });
    cell("score 3", [&](const Row& r) { return pg(r) && r.flow && sc(r) + r.sellers == 3; });
    cell("score 0-2", [&](const Row& r) { return pg(r) && r.flow && sc(r) + r.sellers <= 2; });
    printf("  ALL three together\n");
    cell("retest N=15 + straight path + score 3", [&](const Row& r) { return g.sel(r) && r.var == V_RT15 && !r.early && r.path && sc(r) == 3; });
    cell("page entry + straight path + score 3", [&](const Row& r) { return pg(r) && !r.early && r.path && sc(r) == 3; });
  }
  printf("\nRetest fill rate (flags whose pole top broke that then traded back through it):");
  for (int v = V_RT5; v < NV; ++v) { long f = 0; for (auto& r : rows) f += r.var == v; printf("  %s %.0f%%", VN[v], flagsBroke[v] ? 100.0 * f / flagsBroke[v] : 0); }
  printf("  (one position at a time per chart, so fills / breaks is approximate)\n");
  return 0;
}
