// 2026-10-20 flag_pb_break.cpp (founder: "pullback is not correct. it needs a pull back WITH a break" → chose "break, then pullback":
// leg 1 breaks a prior high, price pulls back to a higher low, the flag's pole is leg 2). Same flags, entries, costs and honest twins as
// flag_pb_then_flag.cpp (its header follows). Classified at the pole's first bar from what is known then (mirrored for bears):
//  leg 1 = the low of day so far (L1, minute kL1) → the high of day so far P; the pullback = P → the pole low L (a higher low); depth = (P − L)/(P − L1)
//  "broke" = P exceeded S60, the highest high of the 60 one-minute bars before leg 1 started (overnight bars included), by >= 1 tick
//  Fixed before the run:
//  F0 first leg (no pullback before the pole)            N0 pullback WITHOUT a break (leg 1 did not take out S60)
//  B1 break, then pullback (any depth <= 78.6 %)          B2 break, then a 38.2-61.8 % pullback
//  B3 break, then the pullback low retested the broken level (L <= S60 + 0.1 ATR)
//  B4 leg 1 broke a KEY level: the overnight high or the prior day's high (above L1)
// (header of flag_pb_then_flag.cpp:)
//   2026-10-20 flag_pb_then_flag.cpp (founder: "what about pullback then flag?"). Where does the flag's pole start? The /flags page's NY flags
//   (flag_wr_pf.cpp detection, page entry, fixed cost NQ 1.5 / ES 0.5 pt, honest twins), classified at registration by the session BEFORE
//   the pole started (mirrored for bears): low of day so far LOD, high of day so far HOD, leg = HOD − LOD, depth = (HOD − pole low) / leg.
//   Fixed before the run (no cuts searched; 38.2 / 61.8 are the usual fib bands):
//    P0 first leg — the pole starts at the low of day (pole low <= LOD + 1 tick): no pullback before it
//    P1 pullback then flag, shallow (pole low a higher low, depth < 38.2 %)   P2 medium (38.2-61.8 %)   P3 deep (> 61.8 %)
//    T1 the pole low touched the session VWAP   T2 the pole low touched the chart's EMA20 (seeded 18:00, flag_ema_feat.cpp)
//    N1 the new pole made a new high of day (pole top > HOD before the pole)
//   Groups: NY bull 3-5m, its 1st pullbacks, TAKE, NY bear 3-5m. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26.
//   Usage: flag_pb_then_flag [NQ|ES]
//   tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 10:00 = 960, 14:00 = 1200, 15:00 = 1260, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k0, k; };
enum Var { V_PAGE, NV };
enum EF { F0, N0, B1, B2, B3, B4, NE };
static const char* EN[NE] = {"F0 first leg (no pullback before)", "N0 pullback WITHOUT a break", "B1 break, then pullback", "B2 break, then 38-62 % pullback", "B3 break, then retest of the broken level", "B4 leg 1 broke overnight / prior-day high"};
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; };
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
  std::vector<Row> rows; long nDays = 0, flagsTotal = 0; std::mt19937 rng(20261020);
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
        // EMAs of this chart seeded at 18:00: the overnight minutes aggregated on the same 09:30 grid (floor), then the session bars
        std::vector<double> e9(b.size()), e20(b.size()), e15(n, NAN);
        { double a9 = NAN, a20 = NAN; long key = LONG_MIN; double pc = NAN;
          auto push = [&](double c) { a9 = std::isnan(a9) ? c : a9 + (2.0 / 10) * (c - a9); a20 = std::isnan(a20) ? c : a20 + (2.0 / 21) * (c - a20); };
          for (int i = d.b0; i < I[0]; ++i) { const int tm = tday_min(s.t[i]); const long kk = (long)std::floor((tm - 930) / (double)tf); const double c = side > 0 ? s.c[i] : -s.c[i];
            if (kk != key && key != LONG_MIN) push(pc); key = kk; pc = c; }
          if (key != LONG_MIN) push(pc);
          for (size_t q = 0; q < b.size(); ++q) { push(b[q].c); e9[q] = a9; e20[q] = a20; } }
        { double a = NAN; long key = LONG_MIN; double pc = NAN; size_t k = 0;   // 15m EMA20 of closed 15m bars, known at each RTH minute
          for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); const long kk = (long)std::floor((tm - 930) / 15.0); const double c = side > 0 ? s.c[i] : -s.c[i];
            if (kk != key && key != LONG_MIN) a = std::isnan(a) ? pc : a + (2.0 / 21) * (pc - a); key = kk; pc = c;
            while (k < n && I[k] == i) { e15[k] = ((tm - 930 + 1) % 15 == 0) ? (std::isnan(a) ? c : a + (2.0 / 21) * (c - a)) : a; ++k; } } }
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
          { const int k0 = b[l].k0; double L1 = 1e18, P = -1e18; int kL1 = 0;
            for (int k = 0; k < k0; ++k) { if (Hh[k] > P) P = Hh[k]; }
            int kP = 0; for (int k = 0; k < k0; ++k) if (Hh[k] >= P) { kP = k; break; }
            for (int k = 0; k <= kP && k < k0; ++k) if (Lw[k] < L1) { L1 = Lw[k]; kL1 = k; }
            // S60: highest high of the 60 minutes before leg 1 started, reaching into the overnight bars when needed
            double S60 = -1e18; for (int k = std::max(0, kL1 - 60); k < kL1; ++k) S60 = std::max(S60, Hh[k]);
            for (int back = 60 - kL1, i = I[0] - 1; back > 0 && i >= d.b0; --back, --i) S60 = std::max(S60, side > 0 ? (double)s.h[i] : -(double)s.l[i]);
            double lodAll = 1e18; for (int k = 0; k < k0; ++k) lodAll = std::min(lodAll, Lw[k]);
            const bool first = k0 == 0 || L <= lodAll + 0.25, hasLeg = k0 > 0 && P > L1 && L > L1;
            const double depth = hasLeg ? (P - L) / (P - L1) : NAN;
            const bool brk = hasLeg && S60 > -1e17 && P >= S60 + 0.25;
            const double onM = side > 0 ? onh : -onl, pdM = std::isnan(d.ph) ? NAN : (side > 0 ? d.ph : -d.pl);
            const bool key = hasLeg && ((onM > L1 && P >= onM + 0.25) || (!std::isnan(pdM) && pdM > L1 && P >= pdM + 0.25));
            base.e[F0] = first; base.e[N0] = !first && hasLeg && !brk && depth <= 0.786;
            base.e[B1] = !first && brk && depth <= 0.786; base.e[B2] = base.e[B1] && depth >= 0.382 && depth <= 0.618;
            base.e[B3] = base.e[B1] && L <= S60 + 0.1 * A; base.e[B4] = !first && key && depth <= 0.786; }
          for (int v = 0; v < NV; ++v) {
            size_t kE = 0; double e = NAN, st = NAN; bool fillMin = false;
            if (v == V_PAGE) { const bool use2m = k2m && (!kPole || k2m <= kPole); kE = use2m ? k2m : kPole; if (!kE) continue; e = use2m ? e2m : H + 0.25; st = use2m ? s2m : sPole; fillMin = !use2m; }
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
  printf("Flags: break, then pullback, then flag (fixed before the run) — %s, %ld NY days, %ld flags (3-5m, researched tier).\n", sym.c_str(), nDays, flagsTotal);
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
    cell("all", pg);
    for (int f = 0; f < NE; ++f) { char a[96], b2[96]; snprintf(a, 96, "%s: yes", EN[f]); snprintf(b2, 96, "%s: no", EN[f]);
      cell(a, [&, f](const Row& r) { return pg(r) && r.e[f]; }); cell(b2, [&, f](const Row& r) { return pg(r) && !r.e[f]; }); }
  }
  return 0;
}
