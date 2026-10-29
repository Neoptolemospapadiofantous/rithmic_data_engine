// 2026-10-22 reversal_plan.cpp (founder: "and reversals. I think if we make a good plan for reversals we can identify them"). Runs the plan in
// research/preregistered/2026-10-22_reversal_plan.md exactly as written there before the run:
//   R1 CHoCH pullback · R2 sweep and reclaim · R3 sweep + CHoCH · R4 exhausted CHoCH;  exits X1 2R · X2 measured move · X3 VWAP (R2, R4) · X4 hold
// 5m structure (swing = 2 bars each side, confirmed 2 bars later, overnight bars included), signals 09:45-14:00, entries before 15:00, flat 15:55, one
// position at a time per setup per market, cost 1.5 NQ / 0.5 ES pts (and +1×), other-day twin, slices, and the 7-point PASS check (NQ; ES = point 7).
// Implementation note written with the code: a sweep needs the 09:30 open above the level (a gap through a level is not a sweep).
// Usage: reversal_plan [NQ|ES] [SIGNALS.csv]. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930, 09:45 = 945, 14:00 = 1200).
#include "research/meta.hpp"
#include <cstdio>
#include <functional>
#include <map>
#include <random>
using namespace lab;
struct DB { int date; double atr; std::vector<double> O, H, L, C; std::vector<int> TM; };   // one RTH session, mirrored per side (0 bull, 1 bear)
struct Sig { int date, year, side, setup, sigTm, em; bool limitFill; double e, st, tg[4], r[4]; int xm[4]; bool early, gapW, orbOther, flowBuy, hasFlow; int lowClose; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<DB> all[2]; std::vector<Sig> sig; long nd = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; const double A = d.atr;
    std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); }
    if (I.size() < 370) continue; nd++; const size_t n = I.size();
    for (int sd = 0; sd < 2; ++sd) { const double sg = sd ? -1 : 1; DB z{d.date, A, {}, {}, {}, {}, {}};
      for (int i : I) { z.O.push_back(sg * s.o[i]); z.H.push_back(sd ? -s.l[i] : s.h[i]); z.L.push_back(sd ? -s.h[i] : s.l[i]); z.C.push_back(sg * s.c[i]); z.TM.push_back(tday_min(s.t[i])); }
      // RTH VWAP per minute (mirrored), session high so far
      std::vector<double> VW(n); { double pv = 0, vv = 0; for (size_t k = 0; k < n; ++k) { const double tp = (z.H[k] + z.L[k] + z.C[k]) / 3; pv += tp * s.v[I[k]]; vv += s.v[I[k]]; VW[k] = vv > 0 ? pv / vv : z.C[k]; } }
      // 5m bars from 18:00 on the 09:30 grid
      struct B5 { double h, l, c, v, bv; int k0, k1, closeTm; bool rth; }; std::vector<B5> b; long key = LONG_MIN;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 1320) break; const long kk = (long)std::floor((tm - 930) / 5.0);
        const double hi = sd ? -s.l[i] : s.h[i], lo = sd ? -s.h[i] : s.l[i], cl = sg * s.c[i], bvv = sd ? s.v[i] - s.bv[i] : s.bv[i];
        const int kr = tm >= 930 ? (int)(std::lower_bound(z.TM.begin(), z.TM.end(), tm) - z.TM.begin()) : -1;
        if (kk != key) { b.push_back({hi, lo, cl, (double)s.v[i], bvv, kr, kr, tm + 1, tm >= 930}); key = kk; }
        else { B5& x = b.back(); x.h = std::max(x.h, hi); x.l = std::min(x.l, lo); x.c = cl; x.v += s.v[i]; x.bv += bvv; x.k1 = kr; x.closeTm = tm + 1; if (x.k0 < 0) x.k0 = kr; } }
      // swings, confirmed at the close of bar j+2
      struct SW { int conf, j; bool hi; double px; }; std::vector<SW> sw;
      for (int j = 2; j + 2 < (int)b.size(); ++j) { bool sh = true, sl = true; for (int o = -2; o <= 2; ++o) if (o) { if (!(b[j].h > b[j + o].h)) sh = false; if (!(b[j].l < b[j + o].l)) sl = false; }
        if (sh) sw.push_back({j + 2, j, true, b[j].h}); if (sl) sw.push_back({j + 2, j, false, b[j].l}); }
      std::sort(sw.begin(), sw.end(), [](const SW& a, const SW& c) { return a.conf < c.conf; });
      // levels known before the day (mirrored): yesterday's low, overnight low, last week's low; 09:30-09:35 range low (after 09:35)
      double onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 930) break; onl = std::min(onl, sd ? -(double)s.h[i] : (double)s.l[i]); }
      double orl = 1e18, orh = -1e18; for (size_t k = 0; k < n && z.TM[k] < 935; ++k) { orl = std::min(orl, z.L[k]); orh = std::max(orh, z.H[k]); }
      const double lv[4] = {sd ? -d.ph : d.pl, onl < 1e17 ? onl : NAN, sd ? -d.pwh : d.pwl, orl};
      // the opening range broken the other way (down, mirrored) before minute t
      int orbDownAt = 99999; for (size_t k = 0; k < n && z.TM[k] < 1200; ++k) if (z.TM[k] >= 930 && z.C[k] < orl && z.TM[k] >= 934) { orbDownAt = z.TM[k] + 1; break; }
      const double gap = std::isnan(d.pc) ? 0 : z.O[0] - sg * d.pc;
      // simulate from minute index k (limit fill at k: stop checked in that minute; market at k's close: from k+1); returns gross pts, exit minute
      auto sim = [&](size_t k, bool limit, double e, double st, double tg, int& xm) -> double { xm = 1315; if (limit && z.L[k] <= st) { xm = z.TM[k]; return st - e; }
        for (size_t q = k + 1; q < n; ++q) { if (z.TM[q] >= 1315) { xm = z.TM[q]; return z.O[q] - e; } if (z.L[q] <= st) { xm = z.TM[q]; return st - e; }
          if (!std::isnan(tg) && z.H[q] >= tg) { xm = z.TM[q]; return tg - e; } } xm = z.TM[n - 1]; return z.C[n - 1] - e; };
      auto emit = [&](int setup, int sigTm, size_t ke, bool limit, double e, double st, double mm, int lowBar) {
        if (z.TM[ke] >= 1260 || !(e - st > 0.5)) return; Sig g; g.date = d.date; g.year = d.date / 10000; g.side = sd ? -1 : 1; g.setup = setup; g.sigTm = sigTm; g.em = z.TM[ke]; g.limitFill = limit;
        g.e = e; g.st = st; const double R = e - st, vw = VW[ke]; g.tg[0] = e + 2 * R; g.tg[1] = mm; g.tg[2] = vw >= e + 0.5 * R ? vw : e + 2 * R; g.tg[3] = NAN;
        for (int x = 0; x < 4; ++x) { const double gr = sim(ke, limit, e, st, g.tg[x], g.xm[x]); g.r[x] = (gr - cost) / R; }
        g.early = sigTm < 990; g.gapW = gap > 0; g.orbOther = orbDownAt <= sigTm; g.lowClose = lowBar >= 0 ? b[lowBar].closeTm : -1; g.hasFlow = d.flow && lowBar >= 0 && b[lowBar].v > 0; g.flowBuy = g.hasFlow && b[lowBar].bv / b[lowBar].v >= 0.55; sig.push_back(g); };
      // ── R1 / R3 / R4: CHoCH in a 5m downtrend ──
      { double h1 = NAN, h2 = NAN, l1 = NAN, l2 = NAN; int jh2 = -1, usedH = -1; size_t p = 0;
        for (int k = 0; k < (int)b.size(); ++k) {
          const bool down = !std::isnan(h1) && !std::isnan(l1) && h2 < h1 && l2 < l1;   // structure known BEFORE this bar closes
          if (b[k].rth && down && jh2 >= 0 && usedH != jh2 && b[k].closeTm >= 945 && b[k].closeTm <= 1200 && b[k].c > h2) { usedH = jh2;
            int lowBar = jh2; double rl = 1e18; for (int q = jh2; q <= k; ++q) if (b[q].l < rl) { rl = b[q].l; lowBar = q; }
            double top = -1e18; for (int q = lowBar; q <= k; ++q) top = std::max(top, b[q].h);
            const double mid = (rl + top) / 2, st = rl - 0.1 * A, mm = top + (top - rl);
            // R3: the reversal low swept a level; R4: exhausted (>= 1 ATR off the session high so far, >= 0.5 ATR under VWAP at the low)
            bool swept = false; for (int li = 0; li < 4; ++li) { if (std::isnan(lv[li]) || (li == 3 && b[lowBar].closeTm < 935)) continue; if (rl < lv[li] - 0.05 * A && z.O[0] > lv[li]) swept = true; }
            double hod = -1e18; for (size_t q = 0; q < n && z.TM[q] < b[lowBar].closeTm; ++q) hod = std::max(hod, z.H[q]);
            const int kl = b[lowBar].k1; const bool exh = b[lowBar].rth && kl >= 0 && hod - rl >= A && rl <= VW[kl] - 0.5 * A;
            // the limit: from the next minute, 60 minutes; cancelled if the reversal low trades first
            size_t k0 = (size_t)std::max(0, b[k].k1 + 1); size_t ke = n;
            for (size_t q = k0; q < n && z.TM[q] < b[k].closeTm + 60; ++q) { if (z.L[q] <= mid) { if (z.L[q] <= rl && z.O[q] <= rl) break; ke = q; break; } if (z.L[q] <= rl) break; }
            if (ke < n) { const double e = std::min(z.O[ke], mid); emit(1, b[k].closeTm, ke, true, e, st, mm, lowBar); if (swept) emit(3, b[k].closeTm, ke, true, e, st, mm, lowBar); if (exh) emit(4, b[k].closeTm, ke, true, e, st, mm, lowBar); } }
          while (p < sw.size() && sw[p].conf <= k) { if (sw[p].hi) { h1 = h2; h2 = sw[p].px; jh2 = sw[p].j; } else { l1 = l2; l2 = sw[p].px; } ++p; } } }
      // ── R2: sweep and reclaim ──
      for (int li = 0; li < 4; ++li) { const double L = lv[li]; if (std::isnan(L) || !(z.O[0] > L)) continue;
        size_t ks = n; for (size_t k = 0; k < n && z.TM[k] < 1200; ++k) { if (li == 3 && z.TM[k] < 935) continue; if (z.L[k] < L - 0.05 * A) { ks = k; break; } } if (ks >= n) continue;
        int bs = -1; for (int q = 0; q < (int)b.size(); ++q) if (b[q].rth && b[q].k0 <= (int)ks && b[q].k1 >= (int)ks) { bs = q; break; } if (bs < 0) continue;
        for (int q = bs; q <= bs + 2 && q < (int)b.size(); ++q) { if (b[q].c > L) { if (b[q].closeTm < 945 || b[q].closeTm > 1200) break;
            double sl = 1e18; for (int k = (int)ks; k <= b[q].k1; ++k) sl = std::min(sl, z.L[k]); const double e = b[q].c, st = sl - 0.1 * A, mm = e + 2 * (e - sl);
            int lowBar = bs; for (int r = bs; r <= q; ++r) if (b[r].l <= sl + 1e-9) { lowBar = r; break; }
            emit(2, b[q].closeTm, (size_t)b[q].k1, false, e, st, mm, lowBar); break; } } }
      all[sd].push_back(std::move(z)); } }
  std::sort(sig.begin(), sig.end(), [](const Sig& a, const Sig& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  const double Anow = all[0].empty() ? 1 : all[0].back().atr; (void)Anow;
  // 2026-10-22 `reversal_plan SYM FILE` (orderflow_check.cpp): every filled signal — date, side, setup, signal minute, low bar close minute,
  // entry minute, entry, stop, R at X1 / X2 / X4, exit minute at X1 / X2 / X4
  if (argc > 2) { FILE* fo = fopen(argv[2], "w"); if (!fo) { perror(argv[2]); return 1; } fprintf(fo, "date,side,setup,sig,low1,low2,em,e,st,r1,r2,r4,x1,x2,x4\n");
    for (auto& g : sig) fprintf(fo, "%d,%d,%d,%d,%d,-1,%d,%.2f,%.2f,%.4f,%.4f,%.4f,%d,%d,%d\n", g.date, g.side, g.setup, g.sigTm, g.lowClose, g.em, g.side * g.e, g.side * g.st, g.r[0], g.r[1], g.r[3], g.xm[0], g.xm[1], g.xm[3]);
    fclose(fo); fprintf(stderr, "exported %zu signals to %s\n", sig.size(), argv[2]); }
  static const char* SN[5] = {"", "R1 CHoCH pullback", "R2 sweep and reclaim", "R3 sweep + CHoCH", "R4 exhausted CHoCH"}; static const char* XN[4] = {"X1 2R", "X2 measured move", "X3 VWAP", "X4 hold 15:55"};
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, sc = 0, yr[13] = {0}, yc[13] = {0}, r25 = 0, n25 = 0; void add(double x, double xc, int y) { n++; s += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); sc += xc; yr[y - 2014] += x; yc[y - 2014]++; if (y >= 2025) { r25 += x; n25++; } } };
  // the trades one setup × exit takes (one position at a time per market)
  auto taken = [&](int setup, int x, std::function<bool(const Sig&)> extra) { std::vector<const Sig*> o; int lastD = 0, busy = -1;
    for (auto& g : sig) { if (g.setup != setup || !extra(g)) continue; if (g.date != lastD) { lastD = g.date; busy = -1; } if (g.em <= busy) continue; busy = g.xm[x]; o.push_back(&g); } return o; };
  std::mt19937 rng(20261022); std::map<int, size_t> di; for (size_t i = 0; i < all[0].size(); ++i) di[all[0][i].date] = i;
  auto twin = [&](const Sig& g, int x) { const size_t me = di[g.date]; const DB& zf = all[g.side > 0 ? 0 : 1][me]; double sum = 0; int cnt = 0;
    const double R = (g.e - g.st) / zf.atr, td = std::isnan(g.tg[x]) ? NAN : (g.tg[x] - g.e) / zf.atr;
    for (int t = 0; t < 5; ++t) { size_t j; do { j = rng() % all[0].size(); } while (j == me); const DB& o = all[g.side > 0 ? 0 : 1][j]; size_t k0 = 0; while (k0 < o.TM.size() && o.TM[k0] < g.em) ++k0;
      if (k0 + 1 >= o.TM.size()) continue; const double e = o.C[k0], st = e - R * o.atr, tg = std::isnan(td) ? NAN : e + td * o.atr; double gr = NAN;
      for (size_t q = k0 + 1; q < o.TM.size(); ++q) { if (o.TM[q] >= 1315) { gr = o.O[q] - e; break; } if (o.L[q] <= st) { gr = st - e; break; } if (!std::isnan(tg) && o.H[q] >= tg) { gr = tg - e; break; } }
      if (std::isnan(gr)) gr = o.C.back() - e; sum += (gr - cost * o.atr / zf.atr) / (R * o.atr); cnt++; } return cnt ? sum / cnt : 0.0; };
  printf("Reversal plan (research/preregistered/2026-10-22_reversal_plan.md) — %s, %ld NY days 2014-01 → 2026-10, %zu reversal signals with a fill. Cost %.1f pts, one position at a time per setup.\n",
         sym.c_str(), nd, sig.size(), cost);
  printf("Columns: trades (a year), avg R, win %%, PF, years > 0, 2025-26 R, R at +1× cost, other-day twin R, edge over twin, PASS points 1-6 (NQ rule; ES is point 7, read across files).\n");
  for (int st = 1; st <= 4; ++st) { printf("\n######## %s\n", SN[st]);
    for (int x = 0; x < 4; ++x) { if (x == 2 && (st == 1 || st == 3)) continue; auto T = taken(st, x, [](const Sig&) { return true; }); St a; double tw = 0;
      for (auto* g : T) { const double R = g->e - g->st; a.add(g->r[x], g->r[x] - cost / R, g->year); tw += twin(*g, x); }
      if (a.n < 20) { printf("    %-18s fewer than 20 trades (%.0f)\n", XN[x], a.n); continue; }
      int py = 0, ny = 0; for (int k = 0; k < 13; ++k) if (a.yc[k] >= 3) { ny++; py += a.yr[k] > 0; } const double m = a.s / a.n, twm = tw / a.n, pf = a.gl > 0 ? a.gw / a.gl : 99;
      const bool p1 = a.n >= 150, p2 = m >= 0.15 && pf >= 1.2, p3 = py >= 9, p4 = a.n25 && a.r25 > 0, p5 = a.sc > 0, p6 = m - twm >= 0.15;
      printf("    %-18s %5.0f (%5.1f/yr) %+6.3fR win %4.1f%% PF %.2f · years > 0 %2d/%d · 2025-26 %+6.3fR (%3.0f) · +1× cost %+6.3fR · twin %+6.3fR · edge %+6.3fR · PASS %d%d%d%d%d%d%s\n",
             XN[x], a.n, a.n / 12.8, m, 100 * a.w / a.n, pf, py, ny, a.n25 ? a.r25 / a.n25 : 0, a.n25, a.sc / a.n, twm, m - twm, p1, p2, p3, p4, p5, p6, p1 && p2 && p3 && p4 && p5 && p6 ? "  ← passes 1-6" : ""); }
    // slices at X1 (fixed before the run would have been better stated; X1 = the plan's first exit)
    struct Sl { const char* n; std::function<bool(const Sig&)> f; };
    const Sl sl[] = {{"bull", [](const Sig& g) { return g.side > 0; }}, {"bear", [](const Sig& g) { return g.side < 0; }}, {"signal before 10:30", [](const Sig& g) { return g.sigTm < 990; }},
                     {"10:30-12:00", [](const Sig& g) { return g.sigTm >= 990 && g.sigTm < 1080; }}, {"12:00-14:00", [](const Sig& g) { return g.sigTm >= 1080; }},
                     {"gap with the trade", [](const Sig& g) { return g.gapW; }}, {"gap against", [](const Sig& g) { return !g.gapW; }},
                     {"range broke the other way first", [](const Sig& g) { return g.orbOther; }}, {"range not broken the other way", [](const Sig& g) { return !g.orbOther; }},
                     {"buyers at the low (2023+)", [](const Sig& g) { return g.hasFlow && g.flowBuy; }}, {"sellers at the low (2023+)", [](const Sig& g) { return g.hasFlow && !g.flowBuy; }}};
    printf("    slices at X1:");
    for (auto& q : sl) { auto T = taken(st, 0, q.f); double sR = 0; for (auto* g : T) sR += g->r[0]; printf(" %s %+.2f (%zu) ·", q.n, T.empty() ? 0 : sR / T.size(), T.size()); } printf("\n"); }
  return 0;
}
