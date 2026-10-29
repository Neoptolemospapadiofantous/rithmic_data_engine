// 2026-10-20 flag_mid_validate.cpp (founder: "validate all", after the flag-mid entry came out best in flag_stop_timing.cpp — its header
// follows). Same flags, cells, costs and twins. Checks fixed before the run, for E0 S0 (the page: break, flag low) vs E2 S2 (flag-mid
// limit, stop 0.1 ATR under the flag low) vs E2 S3 (flag-mid, stop under 50 % of the pole):
//  V1 year by year · V2 cost × 2 and × 3 · V3 bootstrap of trading DAYS (2000 draws): 95 % interval of E2 S2 minus E0 S0 mean R
//  V4 walk-forward cell chooser: each year 2014-26 trades the cell (of 32) with the best mean R on EARLIER years — if the grid's best
//     cell were luck, the chooser would not beat always-E0 S0 · V5 split by chart (3 / 4 / 5m) and by registration time
//  V6 partials: half at +1R, the rest to the measured move with the stop at break-even, on E0 S0 and E2 S2
//  V7 fill rate: flags with a flag-mid fill; break-entry winners the flag-mid entry never filled
// Usage: flag_mid_validate [NQ|ES]
// (header of flag_stop_timing.cpp:)
//   2026-10-20 flag_stop_timing.cpp (founder: "we are not filtering the best ones I think and also the way we exit and SL is bad",
//   "so it has to be timed and have more R for SL than normal to not get taken out", "test these"). NY 3-5m flags of the page
//   (flag_pb_then_flag.cpp detection and features, its header follows). Grid fixed before the run, target always the measured move
//   (H + pole), flat 15:55, one position at a time per cell:
//    ENTRY  E0 page (2m trigger or pole-top stop)  E1 limit at the pole top on the retest, within 15 min after the break
//           E2 limit at the flag's middle ((H + flag low at registration) / 2) while the flag forms (before the break, until it fails/expires)
//           E3 the close of the chart bar that broke the pole top, only if it closes above it
//    STOP   S0 flag low − 1 tick (page) · S1 flag low − 0.05 ATR · S2 flag low − 0.1 ATR · S3 below 50 % of the pole · S4 below 61.8 %
//           of the pole · S5 below the pole low · S6 entry − 0.1 ATR · S7 entry − 0.2 ATR (flag low = lowest low from registration to entry)
//    Usage: flag_stop_timing [NQ|ES] [strict] — strict = limit entries (E1, E2) fill only when price trades 1 tick through the limit.
//    R = per the cell's own risk (equal-risk sizing); also points / trade. Twin per cell: 5 same-day random minutes in the hour after
//    registration, same stop / target distances. Fixed cost NQ 1.5 / ES 0.5.
//    SELECTION (walk-forward): flags scored on 14 yes/no features with weights = mean R(yes) − mean R(no) of the page cell on EARLIER
//    years only (2010-13 warm-up, test 2014-26); take the flags whose score is in the top third of the earlier years' scores.
//   (header of flag_pb_then_flag.cpp:)
//     2026-10-20 flag_pb_then_flag.cpp (founder: "what about pullback then flag?"). Where does the flag's pole start? The /flags page's NY flags
//     (flag_wr_pf.cpp detection, page entry, fixed cost NQ 1.5 / ES 0.5 pt, honest twins), classified at registration by the session BEFORE
//     the pole started (mirrored for bears): low of day so far LOD, high of day so far HOD, leg = HOD − LOD, depth = (HOD − pole low) / leg.
//     Fixed before the run (no cuts searched; 38.2 / 61.8 are the usual fib bands):
//      P0 first leg — the pole starts at the low of day (pole low <= LOD + 1 tick): no pullback before it
//      P1 pullback then flag, shallow (pole low a higher low, depth < 38.2 %)   P2 medium (38.2-61.8 %)   P3 deep (> 61.8 %)
//      T1 the pole low touched the session VWAP   T2 the pole low touched the chart's EMA20 (seeded 18:00, flag_ema_feat.cpp)
//      N1 the new pole made a new high of day (pole top > HOD before the pole)
//     Groups: NY bull 3-5m, its 1st pullbacks, TAKE, NY bear 3-5m. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26.
//     Usage: flag_pb_then_flag [NQ|ES]
//     tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 10:00 = 960, 14:00 = 1200, 15:00 = 1260, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k0, k; };
enum Var { V_PAGE, NV };
enum EF { P0, P1, P2, P3, T1, T2, N1, NE };
static const char* EN[NE] = {"P0 first leg (pole starts at the low of day)", "P1 pullback then flag, shallow (< 38 %)", "P2 pullback then flag, medium (38-62 %)",
  "P3 pullback then flag, deep (> 62 %)", "T1 pole low touched VWAP", "T2 pole low touched EMA20", "N1 new pole made a new high of day"};
enum { NEN = 4, NST = 8, NC = NEN * NST, NF = 14, NX = 5, NTC = 2 * 4 * NX };
static const char* XN[NX] = {"MM (no time rule)", "time stop 30 min (< +1R → out)", "time stop 60 min (< +1R → out)", "after 30 min trail under last 3 bars", "after 15 min BE if in profit"};
static const int TEN[2] = {0, 2}, TST[4] = {0, 3, 4, 5};
static const char* ENN[NEN] = {"E0 page", "E1 retest limit", "E2 flag-mid limit", "E3 close confirm"};
static const char* STN[NST] = {"S0 flag low", "S1 flag low-0.05ATR", "S2 flag low-0.1ATR", "S3 pole 50%", "S4 pole 61.8%", "S5 pole low", "S6 entry-0.1ATR", "S7 entry-0.2ATR"};
static const char* FN[NF] = {"1st pullback", "broke a level", "gap with", "pole >= 5x", "60-min move with", "before 10:00", "10:00 straight path", "sellers in flag",
  "first leg", "medium pullback", "new high of day", "pole low at VWAP", "5m chart", "4m chart"};
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC], cK[NC], pR[2]; bool pok[2]; int regMin; bool tok[NTC]; double tR[NTC], tT[NTC]; };
struct DB { std::vector<double> O, H, L, C; std::vector<int> TM; };   // one NY session, mirrored per side (index 0 = bull, 1 = bear)
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double thru = argc > 2 && std::string(argv[2]) == "strict" ? 0.25 : 0.0; const double costB = sym == "ES" ? 0.5 : 1.5;
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
      // timed exits: x 1/2 time stop (not yet +1R after 30 / 60 min → out at that minute's close), 3 after 30 min the stop trails under the
      // lowest low of the last 3 chart bars (tfx minutes each), 4 after 15 min the stop goes to break-even if the close is above entry
      auto simTm = [&](int x, size_t k0, double e, double st, double tg, bool fillMin, int tfx) -> double {
        const double R = e - st; double stop = st, best = e; if (fillMin && Lw[k0] <= st) return st - e;
        for (size_t k = k0 + 1; k < n; ++k) { if (Lw[k] <= stop) return stop - e; if (Hh[k] >= tg) return tg - e; if (TM[k] >= 1315) return O[k] - e;
          best = std::max(best, Hh[k]); const int el = TM[k] - TM[k0];
          if ((x == 1 && el >= 30 && best < e + R) || (x == 2 && el >= 60 && best < e + R)) return Cc[k] - e;
          if (x == 3 && el >= 30) { double lo = 1e18; for (int j = 0; j < 3 * tfx && (int)k - j >= 0; ++j) lo = std::min(lo, Lw[k - j]); stop = std::max(stop, lo - 0.25); }
          if (x == 4 && el >= 15 && Cc[k] > e) stop = std::max(stop, e); }
        return Cc[n - 1] - e; };
      // partial: half at +1R (stop to break-even from then), the other half to tg; returns gross points for one unit
      auto simP = [&](size_t k0, double e, double st, double tg, bool fillMin) -> double {
        const double R = e - st; double stop = st; bool half = false; if (fillMin && Lw[k0] <= st) return st - e;
        for (size_t k = k0 + 1; k < n; ++k) { if (Lw[k] <= stop) return half ? 0.5 * R + 0.5 * (stop - e) : stop - e;
          if (!half && Hh[k] >= e + R) { half = true; stop = std::max(stop, e); }
          if (Hh[k] >= tg) return half ? 0.5 * R + 0.5 * (tg - e) : tg - e; if (TM[k] >= 1315) return half ? 0.5 * R + 0.5 * (O[k] - e) : O[k] - e; }
        return half ? 0.5 * R + 0.5 * (Cc[n - 1] - e) : Cc[n - 1] - e; };
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
        int busy = -1, lastOut[NV]; for (int v = 0; v < NV; ++v) lastOut[v] = -1; std::vector<int> lastC(NC, -1); double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
        for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
          const int tmc = TM[b[q].k] + 1; if (tmc < 945 || tmc >= 1200) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 4 * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const int kc = b[q].k; if (!strict[kc]) continue;
          int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
          int nb = 0; size_t kPole = 0, kEndF = n - 1; double sPole = NAN, flo = fl0;
          for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; kEndF = kk; break; } flo = std::min(flo, Lw[kk]);
            if ((TM[kk] - 930) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M) { kEndF = kk; break; } if (nb >= 12) { kEndF = kk; break; } } }
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
          base.early = sessMin < 960; base.path = path && sessMin >= 960;   // 2026-10-20 audit: the 09:30-10:00 path only for flags registered at / after 10:00 (was look-ahead) base.pathgap = path && gapA > 0;
          base.flow = d.flow && fv > 0; base.sellers = base.flow && fb / fv < 0.49;
          { const int k0 = b[l].k0; double lod = 1e18, hod = -1e18; for (int k = 0; k < k0; ++k) { lod = std::min(lod, Lw[k]); hod = std::max(hod, Hh[k]); }
            const bool first = k0 == 0 || L <= lod + 0.25, hasLeg = k0 > 0 && hod > lod; const double depth = hasLeg ? (hod - L) / (hod - lod) : NAN;
            base.e[P0] = first; base.e[P1] = !first && hasLeg && depth < 0.382; base.e[P2] = !first && hasLeg && depth >= 0.382 && depth <= 0.618; base.e[P3] = !first && hasLeg && depth > 0.618;
            double vwl = NAN; for (int k = b[l].k0; k <= b[l].k; ++k) if (Lw[k] <= L + 1e-9) { vwl = VW[k]; break; }
            base.e[T1] = !std::isnan(vwl) && L <= vwl; base.e[T2] = L <= e20[l] + 1e-9; base.e[N1] = k0 > 0 && H > hod; }
          Row r = base; r.var = V_PAGE; r.regMin = TM[kc] + 1;
          r.f[0] = base.pb1; r.f[1] = base.broke; r.f[2] = base.gapw; r.f[3] = base.pole5; r.f[4] = base.m60w; r.f[5] = base.early; r.f[6] = base.path; r.f[7] = base.sellers;
          r.f[8] = base.e[P0]; r.f[9] = base.e[P2]; r.f[10] = base.e[N1]; r.f[11] = base.e[T1]; r.f[12] = tf == 5; r.f[13] = tf == 4;
          bool any = false;
          for (int en = 0; en < NEN; ++en) {
            size_t kE = 0; double e = NAN; bool fillMin = true;
            if (en == 0) { const bool use2m = k2m && (!kPole || k2m <= kPole); kE = use2m ? k2m : kPole; e = use2m ? e2m : H + 0.25; fillMin = !use2m; }
            if (en == 1 && kPole) for (size_t k = kPole + 1; k < n && TM[k] <= TM[kPole] + 15; ++k) if (Lw[k] <= H - thru) { kE = k; e = std::min(O[k], H); break; }
            if (en == 2) { const double mid = (H + fl0) / 2; for (size_t k = kc + 1; k <= kEndF && k < n; ++k) { if (kPole && k >= kPole) break; if (Lw[k] <= mid - thru) { kE = k; e = std::min(O[k], mid); break; } } }
            if (en == 3 && kPole) { size_t k = kPole; while (k < n && (TM[k] - 930) % tf != tf - 1) ++k; if (k < n && Cc[k] > H) { kE = k; e = Cc[k]; fillMin = false; } }
            if (!kE || std::isnan(e) || TM[kE] >= 1260) continue;
            double FL = fl0; for (size_t k = kc + 1; k < kE; ++k) FL = std::min(FL, Lw[k]);
            for (int sn = 0; sn < NST; ++sn) { const int c = en * NST + sn;
              const double st = sn == 0 ? FL - 0.25 : sn == 1 ? FL - 0.05 * A : sn == 2 ? FL - 0.1 * A : sn == 3 ? H - 0.5 * M - 0.25 : sn == 4 ? H - 0.618 * M - 0.25 : sn == 5 ? L - 0.25 : sn == 6 ? e - 0.1 * A : e - 0.2 * A;
              if (!(e - st > 0.5) || (int)kE <= lastC[c] || H + M <= e) continue;
              int ko; const double risk = e - st; const double p = sim(kE, e, st, H + M, fillMin, ko) - costB;
              r.ok[c] = true; r.cP[c] = p; r.cR[c] = p / risk; r.cK[c] = risk; lastC[c] = ko; any = true;
              if (c == 0 || c == 2 * NST + 2) { const int pi = c == 0 ? 0 : 1; r.pok[pi] = true; r.pR[pi] = (simP(kE, e, st, H + M, fillMin) - costB) / risk; }
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k]; tw += (sim(k, ce, ce - risk, ce + (H + M - e), false, k2) - costB) / risk; }
              r.cT[c] = tw / 5;
              for (int ei = 0; ei < 2; ++ei) for (int si = 0; si < 4; ++si) if (TEN[ei] == en && TST[si] == sn) for (int x = 0; x < NX; ++x) { const int tc = (ei * 4 + si) * NX + x;
                r.tok[tc] = true; r.tR[tc] = (simTm(x, kE, e, st, H + M, fillMin, tf) - costB) / risk; double t2 = 0;
                for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k]; t2 += (simTm(x, k, ce, ce - risk, ce + (H + M - e), false, tf) - costB) / risk; } r.tT[tc] = t2 / 5; } } }
          if (any) rows.push_back(r); } } } }

  const int C0 = 0, C2 = 2 * NST + 2, C3 = 2 * NST + 3; const int CS[3] = {C0, C2, C3}; const char* CN[3] = {"E0 S0 page (break, flag low)", "E2 S2 flag-mid, flag low-0.1ATR", "E2 S3 flag-mid, pole 50%"};
  printf("Validation of the flag-mid entry — %s, %ld NY days, %ld flags (3-5m). Fixed cost %.1f pts, target = measured move, avg R per trade after cost.\n", sym.c_str(), nDays, flagsTotal, costB);
  struct G { const char* name; std::function<bool(const Row&)> sel; };
  std::vector<G> groups = {
    {"NY bull 3-5m", [](const Row& r) { return r.side > 0; }},
    {"TAKE (bull 4/5m, 1st pb, broke, gap with, pole >= 5x)", [](const Row& r) { return r.side > 0 && r.tf >= 4 && r.pb1 && r.broke && r.gapw && r.pole5; }}};
  auto mean = [&](std::function<bool(const Row&)> sel, int c, double costMul) { double s2 = 0, n2 = 0; for (auto& r : rows) if (r.ok[c] && sel(r)) { s2 += (r.cP[c] - (costMul - 1) * costB) / r.cK[c]; n2++; } return std::make_pair(n2 ? s2 / n2 : NAN, n2); };
  for (auto& g : groups) {
    printf("\n######## %s\n", g.name);
    // V1 year by year
    printf("  V1 year by year (avg R, trades):\n    %-34s", "year"); for (int y = 2010; y <= 2026; ++y) printf(" %6d", y); printf("  years>0\n");
    for (int ci = 0; ci < 3; ++ci) { printf("    %-34s", CN[ci]); int pos = 0, have = 0;
      for (int y = 2010; y <= 2026; ++y) { auto m = mean([&](const Row& r) { return g.sel(r) && r.year == y; }, CS[ci], 1); if (m.second < 5) printf("      —"); else { printf(" %+6.2f", m.first); have++; pos += m.first > 0; } } printf("  %d/%d\n", pos, have);
      printf("    %-34s", "  trades"); for (int y = 2010; y <= 2026; ++y) printf(" %6.0f", mean([&](const Row& r) { return g.sel(r) && r.year == y; }, CS[ci], 1).second); printf("\n"); }
    // V2 cost
    printf("  V2 cost sensitivity (avg R all years):\n");
    for (int ci = 0; ci < 3; ++ci) printf("    %-34s cost x1 %+.3f · x2 %+.3f · x3 %+.3f\n", CN[ci], mean(g.sel, CS[ci], 1).first, mean(g.sel, CS[ci], 2).first, mean(g.sel, CS[ci], 3).first);
    // V3 bootstrap by day
    { std::map<int, std::array<double, 4>> byDay; for (auto& r : rows) if (g.sel(r)) { auto& a = byDay[r.date]; if (r.ok[C0]) { a[0] += r.cR[C0]; a[1]++; } if (r.ok[C2]) { a[2] += r.cR[C2]; a[3]++; } }
      std::vector<std::array<double, 4>> v; for (auto& [k, a] : byDay) v.push_back(a); std::mt19937 bs(7); std::vector<double> dif;
      for (int it = 0; it < 2000; ++it) { double s0 = 0, n0 = 0, s2 = 0, n2 = 0; for (size_t i = 0; i < v.size(); ++i) { auto& a = v[bs() % v.size()]; s0 += a[0]; n0 += a[1]; s2 += a[2]; n2 += a[3]; } dif.push_back(s2 / std::max(n2, 1.0) - s0 / std::max(n0, 1.0)); }
      std::sort(dif.begin(), dif.end()); int below = 0; for (double x : dif) below += x <= 0;
      printf("  V3 bootstrap of %zu days: E2 S2 minus E0 S0 = %+.3f R, 95%% interval %+.3f … %+.3f, share of draws <= 0: %.1f%%\n", v.size(), dif[1000], dif[50], dif[1949], 100.0 * below / dif.size()); }
    // V4 walk-forward chooser
    { double sc = 0, nc = 0, s0 = 0, n0 = 0, s2 = 0, n2 = 0; std::map<int, int> used;
      for (int Y = 2014; Y <= 2026; ++Y) { int best = -1; double bm = -1e9; for (int c = 0; c < NC; ++c) { auto m = mean([&](const Row& r) { return g.sel(r) && r.year < Y; }, c, 1); if (m.second >= 100 && m.first > bm) { bm = m.first; best = c; } }
        if (best < 0) continue; used[best]++;
        for (auto& r : rows) if (g.sel(r) && r.year == Y) { if (r.ok[best]) { sc += r.cR[best]; nc++; } if (r.ok[C0]) { s0 += r.cR[C0]; n0++; } if (r.ok[C2]) { s2 += r.cR[C2]; n2++; } } }
      printf("  V4 walk-forward cell chooser 2014-26: %+.3f R (%.0f trades) vs always E0 S0 %+.3f (%.0f) vs always E2 S2 %+.3f (%.0f) · cells chosen:", sc / std::max(nc, 1.0), nc, s0 / std::max(n0, 1.0), n0, s2 / std::max(n2, 1.0), n2);
      for (auto& [c, k] : used) printf(" %s/%s x%d", ENN[c / NST], STN[c % NST], k); printf("\n"); }
    // V5 splits
    printf("  V5 split by chart and registration time (avg R, trades):\n");
    for (int tf = 3; tf <= 5; ++tf) { printf("    %dm chart  ", tf); for (int ci = 0; ci < 3; ++ci) { auto m = mean([&](const Row& r) { return g.sel(r) && r.tf == tf; }, CS[ci], 1); printf(" | %s %+.3f (%4.0f)", ci == 0 ? "E0S0" : ci == 1 ? "E2S2" : "E2S3", m.first, m.second); } printf("\n"); }
    // tday_min 945 / 990 / 1080 / 1200 = 09:45 / 10:30 / 12:00 / 14:00 (fixed 2026-10-20: the first edge was 1030 = 11:10)
    const int TB[4] = {945, 990, 1080, 1200}; const char* TBN[3] = {"09:45-10:30", "10:30-12:00", "12:00-14:00"};
    for (int t = 0; t < 3; ++t) { printf("    %s", TBN[t]); for (int ci = 0; ci < 3; ++ci) { auto m = mean([&](const Row& r) { return g.sel(r) && r.regMin >= TB[t] && r.regMin < TB[t + 1]; }, CS[ci], 1); printf(" | %s %+.3f (%4.0f)", ci == 0 ? "E0S0" : ci == 1 ? "E2S2" : "E2S3", m.first, m.second); } printf("\n"); }
    // V6 partials
    for (int pi = 0; pi < 2; ++pi) { double sp = 0, np = 0, sf = 0, wf = 0, wp = 0; const int c = pi ? C2 : C0; for (auto& r : rows) if (g.sel(r) && r.pok[pi]) { sp += r.pR[pi]; np++; sf += r.cR[c]; wp += r.pR[pi] > 0; wf += r.cR[c] > 0; }
      printf("  V6 partial half at +1R, rest MM, BE · %s: %+.3f R win %.0f%% vs hold to MM %+.3f R win %.0f%% (%.0f trades)\n", pi ? "E2 S2" : "E0 S0", sp / std::max(np, 1.0), 100 * wp / std::max(np, 1.0), sf / std::max(np, 1.0), 100 * wf / std::max(np, 1.0), np); }
    // V7 fill rate
    { double nf = 0, nm = 0, w0 = 0, w0miss = 0; for (auto& r : rows) if (g.sel(r)) { nf++; nm += r.ok[C2]; if (r.ok[C0] && r.cR[C0] > 0) { w0++; w0miss += !r.ok[C2]; } }
      printf("  V7 flags %.0f · flag-mid filled %.0f (%.0f%%) · break-entry winners %.0f, of which the flag-mid never filled %.0f (%.0f%%)\n", nf, nm, 100 * nm / std::max(nf, 1.0), w0, w0miss, 100 * w0miss / std::max(w0, 1.0)); }
  }
  return 0;
}
