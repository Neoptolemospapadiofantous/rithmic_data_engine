// 2026-10-20 flag_agrade12.cpp (founder: "if we take A+ on 1 and 2m"). flag_agrade.cpp (its header follows) on NY bull 1-2m flags
// (researched tier, pole-top stop entry or the flag-mid limit; no 2m trigger on 1-2m, as on the page). A1-A7 as there; A2 here = the
// SAME market's 3-5m weights (flag_loose_mid.cpp L0) applied to the 1-2m flags instead of the other market's. Usage: flag_agrade12 [NQ|ES]
// (header of flag_agrade.cpp:)
//   2026-10-20 flag_agrade.cpp (founder: "do more tests", after the plan named the A grade = flag-mid limit + 14-feature score top third
//   + registered before 12:00). flag_pf_boost.cpp's flags (its header follows), NY bull 3-5m, test years 2014-26. Checks fixed before the run:
//    A1 permutation: 200 runs with each year's flag-mid R shuffled across that year's flags, weights relearned walk-forward — the share of
//       runs whose top third beats the rest by as much as the real score says whether the score is luck
//    A2 cross-market: the OTHER market's weights (learned 2010-2025 in flag_loose_mid.cpp L0 — same years, so not fully independent),
//       threshold = top third of this market's earlier-year scores
//    A3 cost x2 · A4 year by year · A5 worst losing streak and max drawdown (R, trades in entry order) · A6 trades a week
//    A7 Vest $5k Silver (fee $40): pass = +$500 before −$300 (static) or a −$150 day; fixed $ risk a trade ($50 / $75 / $100, i.e. R × risk),
//       started at every A-grade trade 2014-26, played forward in time order; pass %, median trades and calendar days to the result
//   Usage: flag_agrade [NQ|ES]
//   (header of flag_pf_boost.cpp:)
//     2026-10-20 flag_pf_boost.cpp (founder: "more research how can we increase the PF"). flag_mid_validate.cpp's flags and cells (its header
//     follows) on the flag-mid entry (E2 · S2) and the page's break (E0 · S0), NY bull 3-5m, with filters fixed before the run, alone and combined:
//      F1 minimum stop: the trade's risk >= k × the round-trip cost (k = 5 / 10 / 15 / 20) — costs eat small stops
//      F2 registration before 12:00 (the 12:00-14:00 flags are the weakest in flag_mid_validate V5)
//      F3 the 14-feature score's TOP third, weights learned on EARLIER years only (test years 2014-26; flag_loose_mid.cpp method)
//      F4 daily stop: no new trade after the day's first win or second loss, counting only trades already closed at the new entry
//     PF, avg R after cost, win %, trades a year, periods 2010-15 | 2016-20 | 2021-24 | 2025-26 (F3 rows: 2014-26 only, with the
//     unfiltered 2014-26 baseline beside them), and a bootstrap of days for the best combination vs the page. Usage: flag_pf_boost [NQ|ES]
//     (header of flag_mid_validate.cpp:)
//       2026-10-20 flag_mid_validate.cpp (founder: "validate all", after the flag-mid entry came out best in flag_stop_timing.cpp — its header
//       follows). Same flags, cells, costs and twins. Checks fixed before the run, for E0 S0 (the page: break, flag low) vs E2 S2 (flag-mid
//       limit, stop 0.1 ATR under the flag low) vs E2 S3 (flag-mid, stop under 50 % of the pole):
//        V1 year by year · V2 cost × 2 and × 3 · V3 bootstrap of trading DAYS (2000 draws): 95 % interval of E2 S2 minus E0 S0 mean R
//        V4 walk-forward cell chooser: each year 2014-26 trades the cell (of 32) with the best mean R on EARLIER years — if the grid's best
//           cell were luck, the chooser would not beat always-E0 S0 · V5 split by chart (3 / 4 / 5m) and by registration time
//        V6 partials: half at +1R, the rest to the measured move with the stop at break-even, on E0 S0 and E2 S2
//        V7 fill rate: flags with a flag-mid fill; break-entry winners the flag-mid entry never filled
//       Usage: flag_mid_validate [NQ|ES]
//       (header of flag_stop_timing.cpp:)
//         2026-10-20 flag_stop_timing.cpp (founder: "we are not filtering the best ones I think and also the way we exit and SL is bad",
//         "so it has to be timed and have more R for SL than normal to not get taken out", "test these"). NY 3-5m flags of the page
//         (flag_pb_then_flag.cpp detection and features, its header follows). Grid fixed before the run, target always the measured move
//         (H + pole), flat 15:55, one position at a time per cell:
//          ENTRY  E0 page (2m trigger or pole-top stop)  E1 limit at the pole top on the retest, within 15 min after the break
//                 E2 limit at the flag's middle ((H + flag low at registration) / 2) while the flag forms (before the break, until it fails/expires)
//                 E3 the close of the chart bar that broke the pole top, only if it closes above it
//          STOP   S0 flag low − 1 tick (page) · S1 flag low − 0.05 ATR · S2 flag low − 0.1 ATR · S3 below 50 % of the pole · S4 below 61.8 %
//                 of the pole · S5 below the pole low · S6 entry − 0.1 ATR · S7 entry − 0.2 ATR (flag low = lowest low from registration to entry)
//          Usage: flag_stop_timing [NQ|ES] [strict] — strict = limit entries (E1, E2) fill only when price trades 1 tick through the limit.
//          R = per the cell's own risk (equal-risk sizing); also points / trade. Twin per cell: 5 same-day random minutes in the hour after
//          registration, same stop / target distances. Fixed cost NQ 1.5 / ES 0.5.
//          SELECTION (walk-forward): flags scored on 14 yes/no features with weights = mean R(yes) − mean R(no) of the page cell on EARLIER
//          years only (2010-13 warm-up, test 2014-26); take the flags whose score is in the top third of the earlier years' scores.
//         (header of flag_pb_then_flag.cpp:)
//           2026-10-20 flag_pb_then_flag.cpp (founder: "what about pullback then flag?"). Where does the flag's pole start? The /flags page's NY flags
//           (flag_wr_pf.cpp detection, page entry, fixed cost NQ 1.5 / ES 0.5 pt, honest twins), classified at registration by the session BEFORE
//           the pole started (mirrored for bears): low of day so far LOD, high of day so far HOD, leg = HOD − LOD, depth = (HOD − pole low) / leg.
//           Fixed before the run (no cuts searched; 38.2 / 61.8 are the usual fib bands):
//            P0 first leg — the pole starts at the low of day (pole low <= LOD + 1 tick): no pullback before it
//            P1 pullback then flag, shallow (pole low a higher low, depth < 38.2 %)   P2 medium (38.2-61.8 %)   P3 deep (> 61.8 %)
//            T1 the pole low touched the session VWAP   T2 the pole low touched the chart's EMA20 (seeded 18:00, flag_ema_feat.cpp)
//            N1 the new pole made a new high of day (pole top > HOD before the pole)
//           Groups: NY bull 3-5m, its 1st pullbacks, TAKE, NY bear 3-5m. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26.
//           Usage: flag_pb_then_flag [NQ|ES]
//           tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 10:00 = 960, 14:00 = 1200, 15:00 = 1260, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <ctime>
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
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC], cK[NC], pR[2]; bool pok[2]; int regMin, cE[NC], cX[NC]; bool tok[NTC]; double tR[NTC], tT[NTC]; };
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
      for (int tf = 1; tf <= 2; ++tf) {
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
          if (tf >= 3) { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
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
              r.ok[c] = true; r.cP[c] = p; r.cR[c] = p / risk; r.cK[c] = risk; r.cE[c] = (int)kE; r.cX[c] = ko; lastC[c] = ko; any = true;
              if (c == 0 || c == 2 * NST + 2) { const int pi = c == 0 ? 0 : 1; r.pok[pi] = true; r.pR[pi] = (simP(kE, e, st, H + M, fillMin) - costB) / risk; }
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k]; tw += (sim(k, ce, ce - risk, ce + (H + M - e), false, k2) - costB) / risk; }
              r.cT[c] = tw / 5;
              for (int ei = 0; ei < 2; ++ei) for (int si = 0; si < 4; ++si) if (TEN[ei] == en && TST[si] == sn) for (int x = 0; x < NX; ++x) { const int tc = (ei * 4 + si) * NX + x;
                r.tok[tc] = true; r.tR[tc] = (simTm(x, kE, e, st, H + M, fillMin, tf) - costB) / risk; double t2 = 0;
                for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k]; t2 += (simTm(x, k, ce, ce - risk, ce + (H + M - e), false, tf) - costB) / risk; } r.tT[tc] = t2 / 5; } } }
          if (any) rows.push_back(r); } } } }

  const int C2 = 2 * NST + 2; const bool isNQ = sym == "NQ";
  static const double WNQ[NF] = {-0.034, 0.239, 0.487, 0.313, 0.568, 0.328, 0.154, 0.122, -0.078, 0.171, 0.055, -0.141, 0.025, 0.012};
  static const double WES[NF] = {0.116, 0.331, 0.196, 0.246, 0.182, 0.164, 0.038, 0.000, 0.190, -0.097, -0.105, 0.128, -0.013, 0.028};
  const double* WX = isNQ ? WNQ : WES;   // this market's 3-5m weights
  std::vector<size_t> B; for (size_t i = 0; i < rows.size(); ++i) if (rows[i].side > 0 && rows[i].ok[C2]) B.push_back(i);
  // walk-forward top third for a given R vector (indexed like B); fixedW = use these weights every year (cross-market)
  auto select = [&](const std::vector<double>& R, const double* fixedW) {
    std::vector<char> top(B.size(), 0), tst(B.size(), 0);
    for (int Y = 2014; Y <= 2026; ++Y) { double w[NF];
      if (fixedW) for (int f = 0; f < NF; ++f) w[f] = fixedW[f];
      else for (int f = 0; f < NF; ++f) { double sy = 0, ny = 0, sn = 0, nn = 0; for (size_t b = 0; b < B.size(); ++b) { auto& r = rows[B[b]]; if (r.year >= Y) continue; if (r.f[f]) { sy += R[b]; ny++; } else { sn += R[b]; nn++; } } w[f] = ny >= 30 && nn >= 30 ? sy / ny - sn / nn : 0; }
      auto sc = [&](const Row& r) { double v = 0; for (int f = 0; f < NF; ++f) v += r.f[f] ? w[f] : 0; return v; };
      std::vector<double> tr; for (size_t b = 0; b < B.size(); ++b) if (rows[B[b]].year < Y) tr.push_back(sc(rows[B[b]]));
      if (tr.size() < 100) continue; std::sort(tr.begin(), tr.end()); const double thr = tr[tr.size() * 2 / 3];
      for (size_t b = 0; b < B.size(); ++b) if (rows[B[b]].year == Y) { tst[b] = 1; top[b] = sc(rows[B[b]]) >= thr; } }
    return std::make_pair(top, tst); };
  std::vector<double> R0(B.size()); for (size_t b = 0; b < B.size(); ++b) R0[b] = rows[B[b]].cR[C2];
  auto gap = [&](const std::vector<double>& R, const std::vector<char>& top, const std::vector<char>& tst, bool am) {   // top-third mean minus the rest (2014-26)
    double st = 0, nt = 0, sr = 0, nr = 0; for (size_t b = 0; b < B.size(); ++b) { if (!tst[b]) continue; if (am && rows[B[b]].regMin >= 1080) continue; if (top[b]) { st += R[b]; nt++; } else { sr += R[b]; nr++; } }
    return std::array<double, 4>{nt ? st / nt : NAN, nr ? sr / nr : NAN, nt, nr}; };
  printf("A grade on 1-2m — %s, NY bull 1-2m flag-mid (E2 · S2), score walk-forward 2014-26, fixed cost %.1f pts.\n", sym.c_str(), costB);
  auto [top, tst] = select(R0, nullptr); const auto g0 = gap(R0, top, tst, false), g0a = gap(R0, top, tst, true);
  printf("\n  real score: top third %+.3fR (%.0f) vs the rest %+.3fR (%.0f) — gap %+.3fR; before 12:00 only: %+.3f vs %+.3f — gap %+.3fR\n", g0[0], g0[2], g0[1], g0[3], g0[0] - g0[1], g0a[0], g0a[1], g0a[0] - g0a[1]);
  { // A1 permutation
    std::mt19937 pr(2026); int ge = 0, n = 0; std::vector<double> gaps;
    std::map<int, std::vector<size_t>> byY; for (size_t b = 0; b < B.size(); ++b) byY[rows[B[b]].year].push_back(b);
    for (int it = 0; it < 200; ++it) { std::vector<double> R = R0; for (auto& [y, v] : byY) { std::vector<double> vals; for (size_t b : v) vals.push_back(R0[b]); std::shuffle(vals.begin(), vals.end(), pr); for (size_t k = 0; k < v.size(); ++k) R[v[k]] = vals[k]; }
      auto [t2, s2] = select(R, nullptr); const auto g = gap(R, t2, s2, false); const double d = g[0] - g[1]; gaps.push_back(d); n++; ge += d >= g0[0] - g0[1]; }
    std::sort(gaps.begin(), gaps.end());
    printf("  A1 permutation (200 shuffles of R within each year, weights relearned): shuffled gaps median %+.3f, 95th pct %+.3f, max %+.3f · runs >= the real gap: %d / %d (p ≈ %.3f)\n", gaps[100], gaps[189], gaps.back(), ge, n, (ge + 1.0) / (n + 1)); }
  { // A2 cross-market weights
    auto [tx, sx] = select(R0, WX); const auto g = gap(R0, tx, sx, false), ga = gap(R0, tx, sx, true);
    printf("  A2 (in-sample 2010-2025 weights — NOT an independent test) %s 3-5m weights on %s 1-2m: top third %+.3fR (%.0f) vs the rest %+.3fR — gap %+.3f; before 12:00: %+.3f (%.0f) vs %+.3f\n", isNQ ? "NQ" : "ES", sym.c_str(), g[0], g[2], g[1], g[0] - g[1], ga[0], ga[2], ga[1]); }
  // the A-grade trade list: top third + before 12:00, test years, in entry order
  struct T { int date, year; double R, R2, tw; int e, x; };
  std::vector<T> A; for (size_t b = 0; b < B.size(); ++b) { auto& r = rows[B[b]]; if (!tst[b] || !top[b] || r.regMin >= 1080) continue; A.push_back({r.date, r.year, r.cR[C2], (r.cP[C2] - costB) / r.cK[C2], r.cT[C2], r.cE[C2], r.cX[C2]}); }
  std::sort(A.begin(), A.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.e < b.e; });
  // 2026-10-20 audit: one position across all charts of the group is the honest headline (counting every chart double-counts
  // one move); the same-day twin (random minute in the hour after registration, same distances) shows how much is the DAY
  std::vector<T> A1; { int day = -1, lastX = -1; for (auto& t : A) { if (t.date != day) { day = t.date; lastX = -1; } if (t.e <= lastX) continue; lastX = t.x; A1.push_back(t); } }
  for (int pass = 0; pass < 2; ++pass) { auto& L = pass ? A : A1; double s1 = 0, gw = 0, gl = 0, w = 0, tw = 0; for (auto& t : L) { s1 += t.R; tw += t.tw; w += t.R > 0; (t.R > 0 ? gw : gl) += std::fabs(t.R); }
    printf("  %s: %zu trades (%.1f a week) · %+.3fR · win %.0f%% · PF %.2f · same-day twin %+.3fR → flag minus twin %+.3fR\n", pass ? "every chart counted (old headline)" : "ONE POSITION across charts (headline)", L.size(), L.size() / (12.8 * 52), s1 / std::max<size_t>(L.size(), 1), 100 * w / std::max<size_t>(L.size(), 1), gl > 0 ? gw / gl : 99.0, tw / std::max<size_t>(L.size(), 1), (s1 - tw) / std::max<size_t>(L.size(), 1)); }
  A = A1;   // A3-A7 below run on the one-position list
  { double s1 = 0, s2 = 0, gw = 0, gl = 0, gw2 = 0, gl2 = 0, w = 0; for (auto& t : A) { s1 += t.R; s2 += t.R2; w += t.R > 0; (t.R > 0 ? gw : gl) += std::fabs(t.R); (t.R2 > 0 ? gw2 : gl2) += std::fabs(t.R2); }
    printf("\n  A grade 2014-26, one position: %zu trades (%.1f a week) · %+.3fR · win %.0f%% · PF %.2f\n  A3 cost x2: %+.3fR · PF %.2f\n", A.size(), A.size() / (12.8 * 52), s1 / A.size(), 100 * w / A.size(), gw / gl, s2 / A.size(), gw2 / gl2); }
  { printf("  A4 by year:"); std::map<int, std::pair<double, int>> y; for (auto& t : A) { y[t.year].first += t.R; y[t.year].second++; }
    int pos = 0; for (auto& [yy, v] : y) { printf(" %d %+.2f(%d)", yy, v.first / v.second, v.second); pos += v.first > 0; } printf(" · %d/%zu years > 0\n", pos, y.size()); }
  { // A6 one position across ALL charts of the group (a trade on any chart blocks the others until it closes; first by entry minute)
    double s1 = 0, gw = 0, gl = 0, w = 0; int n = 0, day = -1, lastX = -1; for (auto& t : A) { if (t.date != day) { day = t.date; lastX = -1; } if (t.e <= lastX) continue; lastX = t.x; n++; s1 += t.R; w += t.R > 0; (t.R > 0 ? gw : gl) += std::fabs(t.R); }
    printf("  A6 one position across all charts of the group: %d trades (%.1f a week) · %+.3fR · win %.0f%% · PF %.2f (the counts above add every chart)\n", n, n / (12.8 * 52), s1 / std::max(n, 1), 100.0 * w / std::max(n, 1), gl > 0 ? gw / gl : 99.0); }
  { int st = 0, worst = 0; double eq = 0, peak = 0, dd = 0; for (auto& t : A) { st = t.R <= 0 ? st + 1 : 0; worst = std::max(worst, st); eq += t.R; peak = std::max(peak, eq); dd = std::max(dd, peak - eq); }
    printf("  A5 worst losing streak %d trades · max drawdown %.1fR · total %+.1fR\n", worst, dd, eq); }
  { // A7 Vest $5k Silver
    for (double risk : {50.0, 75.0, 100.0}) { int pass = 0, fail = 0, open = 0; std::vector<int> tr, days;
      for (size_t s0 = 0; s0 < A.size(); ++s0) { double eq = 0, dayPnl = 0; int day = A[s0].date, res = 0; size_t k = s0;
        for (; k < A.size(); ++k) { if (A[k].date != day) { day = A[k].date; dayPnl = 0; } const double p = A[k].R * risk; eq += p; dayPnl += p;
          if (dayPnl <= -150 || eq <= -300) { res = -1; break; } if (eq >= 500) { res = 1; break; } }
        if (res == 1) { pass++; tr.push_back(k - s0 + 1); const int d0 = A[s0].date, d1 = A[std::min(k, A.size() - 1)].date; std::tm a{}, b{}; a.tm_year = d0 / 10000 - 1900; a.tm_mon = d0 / 100 % 100 - 1; a.tm_mday = d0 % 100; b.tm_year = d1 / 10000 - 1900; b.tm_mon = d1 / 100 % 100 - 1; b.tm_mday = d1 % 100; days.push_back((int)((std::mktime(&b) - std::mktime(&a)) / 86400)); }
        else if (res == -1) fail++; else open++; }
      std::sort(tr.begin(), tr.end()); std::sort(days.begin(), days.end());
      printf("  A7 Vest $5k Silver, $%.0f a trade: pass %.0f%% · fail %.0f%% · unresolved %d · median %d trades / %d calendar days to pass\n", risk, 100.0 * pass / std::max(1, pass + fail), 100.0 * fail / std::max(1, pass + fail), open, tr.empty() ? 0 : tr[tr.size() / 2], days.empty() ? 0 : days[days.size() / 2]); } }
  return 0;
}
