// 2026-10-22 reversal_double.cpp (founder: "so a reversal would look like a double bottom. or something along those line with other confluences
// (test)"). Runs Part 2 of research/preregistered/2026-10-22_reversal_plan.md exactly as written there before the run: double bottom (double top
// mirrored) on 5m swings, entries D0 at the second low's confirmation / D1 close above the neck / D2 retest of the neck, stop bottom − 0.1 ATR,
// exits X1 2R / X2 measured move / X4 hold, confluences C1 level at the bottom / C2 higher second low / C3 volume divergence / C4 stretched under
// VWAP, combos >= 2 and >= 3. Same costs, one position at a time per entry type, other-day twin, +1× cost and 7-point pass as reversal_plan.cpp.
// Usage: reversal_double [NQ|ES] [SIGNALS.csv]. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930, 09:45 = 945, 14:00 = 1200).
#include "research/meta.hpp"
#include <cstdio>
#include <functional>
#include <map>
#include <random>
using namespace lab;
struct DB { int date; double atr; std::vector<double> O, H, L, C; std::vector<int> TM; };
struct Sig { int date, year, side, entry, em, nc, sigTm, low1, low2; bool c[4]; double e, st, tg[3], r[3]; int xm[3]; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<DB> all[2]; std::vector<Sig> sig; long nd = 0, patterns = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; const double A = d.atr;
    std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); }
    if (I.size() < 370) continue; nd++; const size_t n = I.size();
    for (int sd = 0; sd < 2; ++sd) { const double sg = sd ? -1 : 1; DB z{d.date, A, {}, {}, {}, {}, {}};
      for (int i : I) { z.O.push_back(sg * s.o[i]); z.H.push_back(sd ? -s.l[i] : s.h[i]); z.L.push_back(sd ? -s.h[i] : s.l[i]); z.C.push_back(sg * s.c[i]); z.TM.push_back(tday_min(s.t[i])); }
      std::vector<double> VW(n); { double pv = 0, vv = 0; for (size_t k = 0; k < n; ++k) { pv += (z.H[k] + z.L[k] + z.C[k]) / 3 * s.v[I[k]]; vv += s.v[I[k]]; VW[k] = vv > 0 ? pv / vv : z.C[k]; } }
      struct B5 { double h, l, c, v; int k0, k1, closeTm; bool rth; }; std::vector<B5> b; long key = LONG_MIN;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 1320) break; const long kk = (long)std::floor((tm - 930) / 5.0);
        const double hi = sd ? -s.l[i] : s.h[i], lo = sd ? -s.h[i] : s.l[i], cl = sg * s.c[i];
        const int kr = tm >= 930 ? (int)(std::lower_bound(z.TM.begin(), z.TM.end(), tm) - z.TM.begin()) : -1;
        if (kk != key) { b.push_back({hi, lo, cl, (double)s.v[i], kr, kr, tm + 1, tm >= 930}); key = kk; }
        else { B5& x = b.back(); x.h = std::max(x.h, hi); x.l = std::min(x.l, lo); x.c = cl; x.v += s.v[i]; x.k1 = kr; x.closeTm = tm + 1; if (x.k0 < 0) x.k0 = kr; } }
      std::vector<int> lows; for (int j = 2; j + 2 < (int)b.size(); ++j) { bool sl = true; for (int o = -2; o <= 2; ++o) if (o && !(b[j].l < b[j + o].l)) sl = false; if (sl) lows.push_back(j); }
      double onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 930) break; onl = std::min(onl, sd ? -(double)s.h[i] : (double)s.l[i]); }
      double orl = 1e18; for (size_t k = 0; k < n && z.TM[k] < 935; ++k) orl = std::min(orl, z.L[k]);
      const double lv[4] = {sd ? -d.ph : d.pl, onl < 1e17 ? onl : NAN, sd ? -d.pwh : d.pwl, orl};
      auto sim = [&](size_t k, bool limit, double e, double st, double tg, int& xm) -> double { xm = 1315; if (limit && z.L[k] <= st) { xm = z.TM[k]; return st - e; }
        for (size_t q = k + 1; q < n; ++q) { if (z.TM[q] >= 1315) { xm = z.TM[q]; return z.O[q] - e; } if (z.L[q] <= st) { xm = z.TM[q]; return st - e; }
          if (!std::isnan(tg) && z.H[q] >= tg) { xm = z.TM[q]; return tg - e; } } xm = z.TM[n - 1]; return z.C[n - 1] - e; };
      int curSig = 0, curL1 = 0, curL2 = 0;
      auto emit = [&](int entry, size_t ke, bool limit, double e, double st, double mm, const bool* c) {
        if (z.TM[ke] >= 1260 || !(e - st > 0.5)) return; Sig g; g.date = d.date; g.year = d.date / 10000; g.side = sd ? -1 : 1; g.entry = entry; g.em = z.TM[ke]; g.sigTm = curSig; g.low1 = curL1; g.low2 = curL2; g.e = e; g.st = st;
        g.nc = 0; for (int q = 0; q < 4; ++q) { g.c[q] = c[q]; g.nc += c[q]; } const double R = e - st; g.tg[0] = e + 2 * R; g.tg[1] = mm; g.tg[2] = NAN;
        for (int x = 0; x < 3; ++x) { const double gr = sim(ke, limit, e, st, g.tg[x], g.xm[x]); g.r[x] = (gr - cost) / R; } sig.push_back(g); };
      for (size_t a = 1; a < lows.size(); ++a) { const int j1 = lows[a - 1], j2 = lows[a]; const double L1 = b[j1].l, L2 = b[j2].l;
        if (j2 - j1 < 3 || j2 - j1 > 24 || std::fabs(L2 - L1) > 0.10 * A) continue;
        double neck = -1e18; for (int q = j1; q <= j2; ++q) neck = std::max(neck, b[q].h); const double bottom = std::min(L1, L2);
        if (neck - std::max(L1, L2) < 0.15 * A) continue;
        double pre = -1e18; for (int q = std::max(0, j1 - 12); q < j1; ++q) pre = std::max(pre, b[q].h); if (pre - L1 < 0.5 * A) continue;
        const int jc = j2 + 2; if (jc >= (int)b.size() || !b[jc].rth) continue; patterns++;
        const double st = bottom - 0.1 * A, mm = neck + (neck - bottom);
        bool c[4]; c[0] = false; for (int li = 0; li < 4; ++li) { if (std::isnan(lv[li]) || (li == 3 && b[j1].closeTm < 935)) continue; if (bottom <= lv[li] + 0.05 * A) c[0] = true; }
        c[1] = L2 >= L1; c[2] = b[j2].v < b[j1].v; { const int kb = b[L1 <= L2 ? j1 : j2].k1; c[3] = kb >= 0 && bottom <= VW[kb] - 0.5 * A; }
        // D0: at the confirmation close, neck not yet broken, price still above the invalidation
        bool dead = false; for (int q = j2 + 1; q <= jc; ++q) if (b[q].l <= st) dead = true;
        curL1 = b[j1].closeTm; curL2 = b[j2].closeTm; curSig = b[jc].closeTm;
        if (!dead && b[jc].closeTm >= 945 && b[jc].closeTm <= 1200 && b[jc].c < neck) emit(0, (size_t)b[jc].k1, false, b[jc].c, st, mm, c);
        // D1: first close above the neck within 24 bars of the confirmation, before the invalidation; D2: then a limit at the neck for 60 min
        for (int q = jc; q < (int)b.size() && q <= jc + 24 && !dead; ++q) { if (q > jc) for (int k = b[q].k0; k <= b[q].k1; ++k) if (k >= 0 && z.L[k] <= st) { dead = true; break; } if (dead) break;
          if (b[q].c > neck) { if (b[q].closeTm < 945 || b[q].closeTm > 1200) break; curSig = b[q].closeTm; emit(1, (size_t)b[q].k1, false, b[q].c, st, mm, c);
            for (size_t k = b[q].k1 + 1; k < n && z.TM[k] < b[q].closeTm + 60; ++k) { if (z.L[k] <= st) break; if (z.L[k] <= neck) { emit(2, k, true, std::min(z.O[k], neck), st, mm, c); break; } }
            break; } } }
      all[sd].push_back(std::move(z)); } }
  std::sort(sig.begin(), sig.end(), [](const Sig& a, const Sig& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  // 2026-10-22 `reversal_double SYM FILE` (orderflow_check.cpp): every entry as setup 10 + entry type (D0 10, D1 11, D2 12), same columns as reversal_plan
  if (argc > 2) { FILE* fo = fopen(argv[2], "w"); if (!fo) { perror(argv[2]); return 1; } fprintf(fo, "date,side,setup,sig,low1,low2,em,e,st,r1,r2,r4,x1,x2,x4\n");
    for (auto& g : sig) fprintf(fo, "%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.4f,%.4f,%.4f,%d,%d,%d\n", g.date, g.side, 10 + g.entry, g.sigTm, g.low1, g.low2, g.em, g.side * g.e, g.side * g.st, g.r[0], g.r[1], g.r[2], g.xm[0], g.xm[1], g.xm[2]);
    fclose(fo); fprintf(stderr, "exported %zu signals to %s\n", sig.size(), argv[2]); }
  static const char* EN[3] = {"D0 at the second low", "D1 close above the neck", "D2 neck retest"}; static const char* XN[3] = {"X1 2R", "X2 measured move", "X4 hold 15:55"};
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, sc = 0, yr[13] = {0}, yc[13] = {0}, r25 = 0, n25 = 0; void add(double x, double xc, int y) { n++; s += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); sc += xc; yr[y - 2014] += x; yc[y - 2014]++; if (y >= 2025) { r25 += x; n25++; } } };
  auto taken = [&](int entry, int x, std::function<bool(const Sig&)> f) { std::vector<const Sig*> o; int lastD = 0, busy = -1;
    for (auto& g : sig) { if (g.entry != entry || !f(g)) continue; if (g.date != lastD) { lastD = g.date; busy = -1; } if (g.em <= busy) continue; busy = g.xm[x]; o.push_back(&g); } return o; };
  std::mt19937 rng(20261022); std::map<int, size_t> di; for (size_t i = 0; i < all[0].size(); ++i) di[all[0][i].date] = i;
  auto twin = [&](const Sig& g, int x) { const size_t me = di[g.date]; const DB& zf = all[g.side > 0 ? 0 : 1][me]; double sum = 0; int cnt = 0;
    const double R = (g.e - g.st) / zf.atr, td = std::isnan(g.tg[x]) ? NAN : (g.tg[x] - g.e) / zf.atr;
    for (int t = 0; t < 5; ++t) { size_t j; do { j = rng() % all[0].size(); } while (j == me); const DB& o = all[g.side > 0 ? 0 : 1][j]; size_t k0 = 0; while (k0 < o.TM.size() && o.TM[k0] < g.em) ++k0;
      if (k0 + 1 >= o.TM.size()) continue; const double e = o.C[k0], st = e - R * o.atr, tg = std::isnan(td) ? NAN : e + td * o.atr; double gr = NAN;
      for (size_t q = k0 + 1; q < o.TM.size(); ++q) { if (o.TM[q] >= 1315) { gr = o.O[q] - e; break; } if (o.L[q] <= st) { gr = st - e; break; } if (!std::isnan(tg) && o.H[q] >= tg) { gr = tg - e; break; } }
      if (std::isnan(gr)) gr = o.C.back() - e; sum += (gr - cost * o.atr / zf.atr) / (R * o.atr); cnt++; } return cnt ? sum / cnt : 0.0; };
  printf("Double bottom / double top with confluences (preregistered/2026-10-22_reversal_plan.md, Part 2) — %s, %ld NY days 2014-01 → 2026-10, %ld patterns, %zu entries.\n", sym.c_str(), nd, patterns, sig.size());
  printf("Columns: trades (a year), avg R, win %%, PF, years > 0, 2025-26 R, R at +1× cost, other-day twin R, edge, PASS points 1-6 (ES = point 7, read across files).\n");
  struct Set { const char* n; std::function<bool(const Sig&)> f; bool judged; };
  const Set sets[] = {{"every double bottom / top", [](const Sig&) { return true; }, true}, {"C1 level at the bottom", [](const Sig& g) { return g.c[0]; }, false},
                      {"C2 higher second low", [](const Sig& g) { return g.c[1]; }, false}, {"C3 volume divergence", [](const Sig& g) { return g.c[2]; }, false},
                      {"C4 stretched under VWAP", [](const Sig& g) { return g.c[3]; }, false}, {">= 2 confluences", [](const Sig& g) { return g.nc >= 2; }, true},
                      {">= 3 confluences", [](const Sig& g) { return g.nc >= 3; }, true}};
  for (auto& S0 : sets) { printf("\n######## %s%s\n", S0.n, S0.judged ? "  (judged)" : "  (slice, not judged)");
    for (int en = 0; en < 3; ++en) for (int x = 0; x < 3; ++x) { auto T = taken(en, x, S0.f); St a; double tw = 0;
      for (auto* g : T) { a.add(g->r[x], g->r[x] - cost / (g->e - g->st), g->year); if (S0.judged) tw += twin(*g, x); }
      char nm[64]; snprintf(nm, sizeof nm, "%s · %s", EN[en], XN[x]);
      if (a.n < 20) { printf("    %-42s fewer than 20 trades (%.0f)\n", nm, a.n); continue; }
      int py = 0, ny = 0; for (int k = 0; k < 13; ++k) if (a.yc[k] >= 3) { ny++; py += a.yr[k] > 0; } const double m = a.s / a.n, pf = a.gl > 0 ? a.gw / a.gl : 99;
      printf("    %-42s %5.0f (%5.1f/yr) %+6.3fR win %4.1f%% PF %.2f · years > 0 %2d/%d · 2025-26 %+6.3fR (%3.0f) · +1× cost %+6.3fR", nm, a.n, a.n / 12.8, m, 100 * a.w / a.n, pf, py, ny,
             a.n25 ? a.r25 / a.n25 : 0, a.n25, a.sc / a.n);
      if (S0.judged) { const double twm = tw / a.n; const bool p[6] = {a.n >= 150, m >= 0.15 && pf >= 1.2, py >= 9, a.n25 > 0 && a.r25 > 0, a.sc > 0, m - twm >= 0.15};
        printf(" · twin %+6.3fR · edge %+6.3fR · PASS %d%d%d%d%d%d%s", twm, m - twm, p[0], p[1], p[2], p[3], p[4], p[5], p[0] && p[1] && p[2] && p[3] && p[4] && p[5] ? "  ← passes 1-6" : ""); }
      printf("\n"); } }
  return 0;
}
