// 2026-10-21 flag_hhhl.cpp (founder: "if we are hitting higher highs higher lows and we see a a+ bull flag we take it" → "yes").
// flag_tiny_tp.cpp's flags, entries and walk-forward score unchanged (its header follows); only the report is new. At the minute a flag
// registers, the SWING STRUCTURE from bars already closed (overnight included, 09:30 grid), fixed before the run:
//   swing high = a bar whose high is strictly above the 2 bars on each side (confirmed when the 2nd bar after it closes); swing low mirrored
//   HH/HL = the last two confirmed swing highs rising AND the last two swing lows rising — on 5m (S5) and on 15m (S15); for bear flags the
//   same on mirrored prices = lower highs and lower lows (LH/LL)
// Grades = terciles of the walk-forward 14-feature score (weights from EARLIER years only, page cell E0 · S0, as flag_tiny_tp's top third):
// A = top third (the A list), B = middle, C = bottom; test years 2014-26 only. Entry = flag-mid limit, stop flag low − 0.1 ATR (E2 · S2),
// target measured move, flat 15:55, ONE position at a time across the 3-5m charts within each group, fixed cost NQ 1.5 / ES 0.5 pt.
// Groups per side: every flag / A / B / C, each alone, with S5, with S15, with both, and without S5; plus A + (B with S5) — the
// "more trades" rule. R per period 2014-17 | 2018-21 | 2022-24 | 2025-26, PF, win %, points at today's ATR, trades a year and a day,
// worst drawdown in R, periods > 0. Usage: flag_hhhl [NQ|ES] [export FILE [loose|thru|tf15|pole2|wide|wide15]] (export: every flag with registration / end / break minute, grade, 5m HH/HL — for fleet_flag_seq.cpp). Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 14:00 = 1200, 15:55 = 1315.
// (header of flag_tiny_tp.cpp:)
// 2026-10-21 flag_tiny_tp.cpp (founder: "how about having a huge SL and small TP like 1/10 1/20"). flag_stop_timing.cpp's flags and entries
// (its header follows) with nine high-win-rate exits added, fixed before the run: target TP = 0.02 / 0.03 / 0.05 ATR above the entry
// (≈ 6 / 9 / 15 NQ pts at today's ATR), stop = 10 × TP or 20 × TP below it, or NO stop (flat 15:55). Read in POINTS scaled to today's
// ATR (gross / ATR that day × the latest ATR, then the cost at today's size) — R is meaningless without a stop. Also: the worst running
// drawdown of each cell in those points (the whole history, one contract), the share of losers and the average loser. Reference rows:
// the page's measured-move cells E0 · S0 and E2 · S2. Twin = the same TP / stop from a random minute in the hour after registration.
// Added mid-run (founder: "or last high", "volume filter", "possible order flow filter", "test"), fixed before the re-run:
//  TP at the LAST HIGH = the pole top H (only the flag-mid entry sits below it) with the stop under the flag low − 0.1 ATR, or 10× / 20×
//  the distance to H. Filters on any group: VOL = the flag's bars average < 0.7 × the pole's bars average volume (volume dries up in the
//  flag) · OF-S = sellers in the flag (aggressor buy share < 0.49, flow days 2023-03+) · OF-B = buyers in the pole (buy share >= 0.55).
// PROP PAYOUTS (founder: "the thing is that we can get payouts fast", "we can have a 10 win streak and get payouts", "so we have maybe
// 5 accounts"): the cells run through Tradeify Select Flex 50K / Select Daily 50K (payout_sim.hpp plans: eval target, EOD trailing
// drawdown at each trade's worst open loss, daily loss, win days / consistency, caps) at 1 / 2 / 3 / 5 / 10 MNQ, points scaled to today's ATR,
// cost 1.5 pts = $3 per MNQ, one position at a time across the 3-5m charts (first entry wins), $165 per eval. P1 one account from every
// 5th trading day 2014 → 2025-10: eval pass / fail, median days to pass, % of funded accounts paid within 10 / 20 trading days, paid
// before the account dies. P2 programmes of 5 accounts for 12 months from every month start (all accounts take the SAME trades —
// cross-account hedging is banned), opened together or one a week, a dead account replaced by a new eval the next day.
// EXPORT (2026-10-21, for pass_good_day_funded.cpp): flag_tiny_tp NQ export FILE writes the flag-mid · S2 trades (bull 3-5m, one position at a
// time across charts, 2010+) as date,top,gross_pts,mae_pts,mfe_pts,k_entry (k_entry = the entry's 1m bar index from 09:30) — points scaled to today's ATR, before cost; top = 1 if in the walk-forward top third.
// Usage: flag_tiny_tp [NQ|ES] [export FILE]
// (header of flag_stop_timing.cpp:)
// 2026-10-20 flag_stop_timing.cpp (founder: "we are not filtering the best ones I think and also the way we exit and SL is bad",
// "so it has to be timed and have more R for SL than normal to not get taken out", "test these"). NY 3-5m flags of the page
// (flag_pb_then_flag.cpp detection and features, its header follows). Grid fixed before the run, target always the measured move
// (H + pole), flat 15:55, one position at a time per cell:
//  ENTRY  E0 page (2m trigger or pole-top stop)  E1 limit at the pole top on the retest, within 15 min after the break
//         E2 limit at the flag's middle ((H + flag low at registration) / 2) while the flag forms (before the break, until it fails/expires)
//         E3 the close of the chart bar that broke the pole top, only if it closes above it
//  STOP   S0 flag low − 1 tick (page) · S1 flag low − 0.05 ATR · S2 flag low − 0.1 ATR · S3 below 50 % of the pole · S4 below 61.8 %
//         of the pole · S5 below the pole low · S6 entry − 0.1 ATR · S7 entry − 0.2 ATR (flag low = lowest low from registration to entry)
//  Usage: flag_stop_timing [NQ|ES] [strict] — strict = limit entries (E1, E2) fill only when price trades 1 tick through the limit.
//  R = per the cell's own risk (equal-risk sizing); also points / trade. Twin per cell: 5 same-day random minutes in the hour after
//  registration, same stop / target distances. Fixed cost NQ 1.5 / ES 0.5.
//  SELECTION (walk-forward): flags scored on 14 yes/no features with weights = mean R(yes) − mean R(no) of the page cell on EARLIER
//  years only (2010-13 warm-up, test 2014-26); take the flags whose score is in the top third of the earlier years' scores.
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
#include "paper/payout_sim.hpp"
#include <numeric>
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k0, k; };
enum Var { V_PAGE, NV };
enum EF { P0, P1, P2, P3, T1, T2, N1, NE };
static const char* EN[NE] = {"P0 first leg (pole starts at the low of day)", "P1 pullback then flag, shallow (< 38 %)", "P2 pullback then flag, medium (38-62 %)",
  "P3 pullback then flag, deep (> 62 %)", "T1 pole low touched VWAP", "T2 pole low touched EMA20", "N1 new pole made a new high of day"};
enum { NEN = 4, NST = 20, NC = NEN * NST, NF = 14, NX = 5, NTC = 2 * 4 * NX };
static const char* XN[NX] = {"MM (no time rule)", "time stop 30 min (< +1R → out)", "time stop 60 min (< +1R → out)", "after 30 min trail under last 3 bars", "after 15 min BE if in profit"};
static const int TEN[2] = {0, 2}, TST[4] = {0, 3, 4, 5};
static const char* ENN[NEN] = {"E0 page", "E1 retest limit", "E2 flag-mid limit", "E3 close confirm"};
static const char* STN[NST] = {"S0 flag low", "S1 flag low-0.05ATR", "S2 flag low-0.1ATR", "S3 pole 50%", "S4 pole 61.8%", "S5 pole low", "S6 entry-0.1ATR", "S7 entry-0.2ATR",
  "TP .02ATR SL 10x", "TP .02ATR SL 20x", "TP .02ATR no SL", "TP .03ATR SL 10x", "TP .03ATR SL 20x", "TP .03ATR no SL", "TP .05ATR SL 10x", "TP .05ATR SL 20x", "TP .05ATR no SL", "TP last high, SL flag", "TP last high SL 10x", "TP last high SL 20x"};
static const char* FN[NF] = {"1st pullback", "broke a level", "gap with", "pole >= 5x", "60-min move with", "before 10:00", "10:00 straight path", "sellers in flag",
  "first leg", "medium pullback", "new high of day", "pole low at VWAP", "5m chart", "4m chart"};
struct Row { bool hh5, hh15; int regM, endM, poleM; double xf[35]; double pH, pL, pF0, cE, cSt, cTg; int cEm, cXm; double cMae[17 + 3], cMfe[17 + 3]; int cKE[17 + 3], cKO[17 + 3]; bool vdry, pbuy; double atr; int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC]; bool tok[NTC]; double tR[NTC], tT[NTC]; };
struct DB { std::vector<double> O, H, L, C; std::vector<int> TM; };   // one NY session, mirrored per side (index 0 = bull, 1 = bear)
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double thru = (argc > 2 && std::string(argv[2]) == "strict") || (argc > 4 && std::string(argv[2]) == "export" && std::string(argv[4]) == "thru") ? 0.25 : 0.0; const double costB = sym == "ES" ? 0.5 : 1.5;
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
  std::vector<int> cal; std::vector<Row> rows; long nDays = 0, flagsTotal = 0; std::mt19937 rng(20261020);
  // 2026-10-22 `export FILE loose` (flag_model.cpp): looser candidates — pullback 10-60 %, pole >= 3 bars' average, context above the open and
  // VWAP only (no straight-path test); the default run and the plain export are unchanged
  const bool LOOSE = argc > 4 && std::string(argv[2]) == "export" && std::string(argv[4]) == "loose"; std::vector<double> atrHist;
  // `export FILE tf15` (flag_more_trades.cpp): flags on the 1 and 2 minute charts too (1-5m); grades are then fitted on all five charts
  // `export FILE pole2` (flag_pole_size.cpp): the pole needs only 2× the average bar (default 4×); everything else unchanged
  // `export FILE wide` (flag_size_grid.cpp): pole >= 2× the average bar AND pullback 5-70 % (default 4× and 15-50 %); context unchanged
  const bool WIDE = argc > 4 && std::string(argv[2]) == "export" && (std::string(argv[4]) == "wide" || std::string(argv[4]) == "wide15");   // wide15 = wide on the 1-5m charts
  const bool POLE2 = argc > 4 && std::string(argv[2]) == "export" && std::string(argv[4]) == "pole2";
  const int TF0 = argc > 4 && std::string(argv[2]) == "export" && (std::string(argv[4]) == "tf15" || std::string(argv[4]) == "wide15") ? 1 : 3;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue;
    const double A = d.atr; atrHist.push_back(A); const double atrR = atrHist.size() > 20 ? A / atrHist[atrHist.size() - 21] : 1.0;
    std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); }
    if (I.size() < 370u) continue; nDays++; cal.push_back(d.date);
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
      double gMae = 0, gMfe = 0;   // worst / best open move of the last sim() call, points (>= 0)
      auto sim = [&](size_t k0, double e, double st, double tg, bool fillMin, int& kOut) -> double {
        double px = NAN; kOut = n - 1; gMae = fillMin ? std::max(0.0, e - Lw[k0]) : 0; gMfe = 0;
        { int ko2 = n - 1; for (size_t k = k0 + 1; k < n; ++k) { if (Lw[k] <= st || Hh[k] >= tg || TM[k] >= 1315) { ko2 = k; break; } }
          for (size_t k = k0 + 1; k <= (size_t)ko2 && k < n; ++k) { gMae = std::max(gMae, std::min(e - Lw[k], e - st)); gMfe = std::max(gMfe, std::min(Hh[k] - e, tg - e)); } }
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
      // swing structure (HH/HL on this side's mirrored prices) known at each session minute, 5m and 15m, overnight bars included
      std::vector<char> hhS[2]; for (int zi = 0; zi < 2; ++zi) { const int tz = zi == 0 ? 5 : 15; hhS[zi].assign(n, 0);
        struct ZB { double h, l; int last; }; std::vector<ZB> zb; long key = LONG_MIN;
        for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); const long kk = (long)std::floor((tm - 930) / (double)tz);
          const double hi = side > 0 ? s.h[i] : -s.l[i], lo = side > 0 ? s.l[i] : -s.h[i];
          if (kk != key) { zb.push_back({hi, lo, i}); key = kk; } else { ZB& z = zb.back(); z.h = std::max(z.h, hi); z.l = std::min(z.l, lo); z.last = i; } }
        struct SW { int conf; bool hi; double px; }; std::vector<SW> sw;
        for (int j = 2; j + 2 < (int)zb.size(); ++j) { bool sh = true, sl = true;
          for (int o = -2; o <= 2; ++o) if (o) { if (!(zb[j].h > zb[j + o].h)) sh = false; if (!(zb[j].l < zb[j + o].l)) sl = false; }
          if (sh) sw.push_back({zb[j + 2].last, true, zb[j].h}); if (sl) sw.push_back({zb[j + 2].last, false, zb[j].l}); }
        std::sort(sw.begin(), sw.end(), [](const SW& a, const SW& b) { return a.conf < b.conf; });
        double h1 = NAN, h2 = NAN, l1 = NAN, l2 = NAN; size_t p = 0;   // h2 / l2 = most recent, h1 / l1 = the one before
        for (size_t k = 0; k < n; ++k) { while (p < sw.size() && sw[p].conf <= I[k]) { if (sw[p].hi) { h1 = h2; h2 = sw[p].px; } else { l1 = l2; l2 = sw[p].px; } ++p; }
          hhS[zi][k] = !std::isnan(h1) && !std::isnan(l1) && h2 > h1 && l2 > l1; } }
      for (int tf = TF0; tf <= 5; ++tf) {
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
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (LOOSE ? 3 : POLE2 || WIDE ? 2 : 4) * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (LOOSE ? (pull < 0.10 || pull > 0.60) : WIDE ? (pull < 0.05 || pull > 0.70) : (pull < 0.15 || pull > 0.5)) continue;
          const int kc = b[q].k; if (!(LOOSE ? loose[kc] : strict[kc])) continue;
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
          Row base{}; base.hh5 = hhS[0][kc]; base.hh15 = hhS[1][kc]; base.atr = A; base.date = d.date; base.year = d.date / 10000; base.side = side; base.tf = tf; base.regM = sessMin; base.pH = H; base.pL = L; base.pF0 = fl0; base.endM = TM[kEndF] + 1; base.poleM = kPole ? TM[kPole] : -1;
          base.pb1 = pb == 1; base.broke = broke; base.gapw = gapA > 0.1; base.pole5 = M >= 5 * ar; base.m60w = m60 > 0.1;
          base.early = sessMin < 960; base.path = path && sessMin >= 960;   // 2026-10-20 audit: the 09:30-10:00 path only for flags registered at / after 10:00 (was look-ahead) base.pathgap = path && gapA > 0;
          base.flow = d.flow && fv > 0; base.sellers = base.flow && fb / fv < 0.49;
          { double pv2 = 0, pb2 = 0; for (int x = l; x <= h; ++x) { pv2 += b[x].v; pb2 += b[x].bv; }
            base.vdry = pv2 > 0 && fv / (q - h) < 0.7 * pv2 / (h - l + 1); base.xf[33] = pv2 > 0 ? (fv / (q - h)) / (pv2 / (h - l + 1)) : 1.0; base.pbuy = d.flow && pv2 > 0 && pb2 / pv2 >= 0.55; }
          { const int k0 = b[l].k0; double lod = 1e18, hod = -1e18; for (int k = 0; k < k0; ++k) { lod = std::min(lod, Lw[k]); hod = std::max(hod, Hh[k]); }
            const bool first = k0 == 0 || L <= lod + 0.25, hasLeg = k0 > 0 && hod > lod; const double depth = hasLeg ? (hod - L) / (hod - lod) : NAN;
            base.e[P0] = first; base.e[P1] = !first && hasLeg && depth < 0.382; base.e[P2] = !first && hasLeg && depth >= 0.382 && depth <= 0.618; base.e[P3] = !first && hasLeg && depth > 0.618;
            double vwl = NAN; for (int k = b[l].k0; k <= b[l].k; ++k) if (Lw[k] <= L + 1e-9) { vwl = VW[k]; break; }
            base.e[T1] = !std::isnan(vwl) && L <= vwl; base.e[T2] = L <= e20[l] + 1e-9; base.e[N1] = k0 > 0 && H > hod; }
          { double* x = base.xf; double dh = -1e18, dl = 1e18; for (size_t k = 0; k <= (size_t)kc; ++k) { dh = std::max(dh, Hh[k]); dl = std::min(dl, Lw[k]); }
            double oh = -1e18, ol = 1e18; size_t k = 0; for (; k < n && TM[k] < 935; ++k) { oh = std::max(oh, Hh[k]); ol = std::min(ol, Lw[k]); }
            int od = 0; for (; k <= (size_t)kc; ++k) if (Cc[k] > oh || Cc[k] < ol) { od = Cc[k] > oh ? 1 : -1; break; }
            x[0] = tf; x[1] = sessMin; x[2] = M / ar; x[3] = M / A; x[4] = pull; x[5] = gapA; x[6] = m60; x[7] = base.hh5; x[8] = base.hh15; x[9] = base.pb1; x[10] = pb; x[11] = broke;
            x[12] = base.path; x[13] = base.sellers; x[14] = base.vdry; x[15] = base.pbuy; x[16] = base.flow; x[17] = base.e[P0]; x[18] = base.e[P2]; x[19] = base.e[P3]; x[20] = base.e[T1];
            x[21] = base.e[T2]; x[22] = base.e[N1]; x[23] = dh > dl ? (Cc[kc] - O[0]) / (dh - dl) : 0; x[24] = (Cc[kc] - VW[kc]) / A; x[25] = h - l; x[26] = M / std::max(1, h - l) / ar; x[27] = od;
            x[28] = std::isnan(d.r5) ? 0 : side * d.r5; x[29] = std::isnan(d.r20) ? 0 : side * d.r20; x[30] = std::isnan(d.vix) ? 18 : d.vix; x[31] = atrR; x[32] = side; x[34] = (dh - dl) / A; }
          Row r = base; r.var = V_PAGE;
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
              double st = sn == 0 ? FL - 0.25 : sn == 1 ? FL - 0.05 * A : sn == 2 ? FL - 0.1 * A : sn == 3 ? H - 0.5 * M - 0.25 : sn == 4 ? H - 0.618 * M - 0.25 : sn == 5 ? L - 0.25 : sn == 6 ? e - 0.1 * A : e - 0.2 * A;
              double tgt = H + M;
              if (sn >= 17) { tgt = H; const double tp = H - e; st = sn == 17 ? FL - 0.1 * A : sn == 18 ? e - 10 * tp : e - 20 * tp; }
              else if (sn >= 8) { const int z = sn - 8, ti = z / 3, ri = z % 3; const double tp = (ti == 0 ? 0.02 : ti == 1 ? 0.03 : 0.05) * A; tgt = e + tp; st = ri == 0 ? e - 10 * tp : ri == 1 ? e - 20 * tp : -1e9; }
              if (!(e - st > 0.5) || (int)kE <= lastC[c] || tgt <= e) continue;
              int ko; const double risk = e - st; const double p = sim(kE, e, st, tgt, fillMin, ko) - costB;
              r.ok[c] = true; r.cP[c] = p; r.cR[c] = p / risk; lastC[c] = ko; any = true; if (en == 2 && sn == 2) { r.cE = e; r.cSt = st; r.cTg = tgt; r.cEm = TM[kE]; r.cXm = TM[std::min((size_t)ko, n - 1)]; } if (en == 2) { r.cMae[sn] = gMae; r.cMfe[sn] = gMfe; r.cKE[sn] = (int)kE; r.cKO[sn] = ko; }
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k]; tw += sim(k, ce, ce - risk, ce + (tgt - e), false, k2) - costB; }
              r.cT[c] = tw / 5;   // twin kept in POINTS here
              for (int ei = 0; ei < 2; ++ei) for (int si = 0; si < 4; ++si) if (TEN[ei] == en && TST[si] == sn) for (int x = 0; x < NX; ++x) { const int tc = (ei * 4 + si) * NX + x;
                r.tok[tc] = true; r.tR[tc] = (simTm(x, kE, e, st, H + M, fillMin, tf) - costB) / risk; double t2 = 0;
                for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k]; t2 += (simTm(x, k, ce, ce - risk, ce + (H + M - e), false, tf) - costB) / risk; } r.tT[tc] = t2 / 5; } } }
          if (any) rows.push_back(r); } } } }

  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  // walk-forward selection mask (on the page cell E0 S0), per side
  std::vector<char> grade(rows.size(), 0);   // 1 A top third, 2 B middle, 3 C bottom, 0 untested
  for (int side = 1; side >= -1; side -= 2) for (int Y = 2014; Y <= 2026; ++Y) {
    double w[NF]; for (int f = 0; f < NF; ++f) { double sy = 0, ny = 0, sn = 0, nn = 0; for (auto& r : rows) if (r.side == side && r.year < Y && r.ok[0]) { if (r.f[f]) { sy += r.cR[0]; ny++; } else { sn += r.cR[0]; nn++; } } w[f] = ny >= 30 && nn >= 30 ? sy / ny - sn / nn : 0; }
    auto score = [&](const Row& r) { double sc = 0; for (int f = 0; f < NF; ++f) sc += r.f[f] ? w[f] : 0; return sc; };
    std::vector<double> tr; for (auto& r : rows) if (r.side == side && r.year < Y && r.ok[0]) tr.push_back(score(r));
    if (tr.size() < 100) continue; std::sort(tr.begin(), tr.end()); const double thr = tr[(size_t)(tr.size() * 2 / 3)], thr2 = tr[(size_t)(tr.size() / 3)];
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].side == side && rows[i].year == Y) { const double sc = score(rows[i]); grade[i] = sc >= thr ? 1 : sc >= thr2 ? 2 : 3; }
    // 2026-10-22 FH_WEIGHTS=1 (the /flags page's BOOK2 / RULE grades): the frozen weights and A / B cut-offs used for 2026, to stderr
    if (Y == 2026 && getenv("FH_WEIGHTS")) { fprintf(stderr, "WEIGHTS %s side %+d thrA %.17g thrB %.17g w", sym.c_str(), side, thr, thr2); for (int f = 0; f < NF; ++f) fprintf(stderr, " %.17g", w[f]); fprintf(stderr, "\n"); } }
  if (argc > 3 && std::string(argv[2]) == "export") { FILE* fo = fopen(argv[3], "w"); if (!fo) { perror(argv[3]); return 1; }
    // 2026-10-22 export for fleet_flag_seq.cpp: every flag (both sides, 3-5m, 2014+ graded), minutes since 18:00 ET
    // + the pole (low / high) and flag low in real prices, and the flag-mid · S2 trade (ok, entry / exit minute, entry / stop / target / exit price, net R, net pts)
    fprintf(fo, "date,side,tf,reg_min,end_min,pole_min,grade,hh5,pole_lo,pole_hi,flag_lo,ok,entry_min,exit_min,entry,stop,target,exit,r,pts"); for (int j = 0; j < 35; ++j) fprintf(fo, ",x%d", j); for (int en = 0; en < 4; ++en) for (int sn : {0, 2, 5, 7}) fprintf(fo, ",c%d_%d", en, sn); fprintf(fo, "\n");
    const int Cx = 2 * NST + 2; const double cst = costB;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const int sd = r.side; const bool ok = r.ok[Cx];
      fprintf(fo, "%d,%d,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.4f,%.4f", r.date, sd, r.tf, r.regM, r.endM, r.poleM, (int)grade[i], (int)r.hh5,
              sd > 0 ? r.pL : -r.pH, sd > 0 ? r.pH : -r.pL, sd * r.pF0, (int)ok, ok ? r.cEm : -1, ok ? r.cXm : -1,
              ok ? sd * r.cE : 0.0, ok ? sd * r.cSt : 0.0, ok ? sd * r.cTg : 0.0, ok ? sd * (r.cE + r.cP[Cx] + cst) : 0.0, ok ? r.cR[Cx] : 0.0, ok ? r.cP[Cx] : 0.0); for (int j = 0; j < 35; ++j) fprintf(fo, ",%.5g", r.xf[j]);
      for (int en = 0; en < 4; ++en) for (int sn : {0, 2, 5, 7}) { const int cc = en * NST + sn; if (r.ok[cc]) fprintf(fo, ",%.4f", r.cR[cc]); else fprintf(fo, ",nan"); } fprintf(fo, "\n"); }
    fclose(fo); fprintf(stderr, "exported %s\n", argv[3]); return 0; }
  const int C = 2 * NST + 2; const double Anow = rows.empty() ? 1 : rows.back().atr;
  auto sc = [&](const Row& r, double pts) { return (pts + costB) / r.atr * Anow - costB; };   // points at today's ATR, cost at today's size
  auto per = [](int y) { return y <= 2017 ? 0 : y <= 2021 ? 1 : y <= 2024 ? 2 : 3; };
  long nd = 0; for (int dt : cal) nd += dt >= 20140101;
  long hh5n[2] = {0, 0}, hh15n[2] = {0, 0}, totn[2] = {0, 0};
  for (auto& r : rows) if (r.year >= 2014 && r.ok[C]) { const int sd = r.side > 0 ? 0 : 1; totn[sd]++; hh5n[sd] += r.hh5; hh15n[sd] += r.hh15; }
  printf("Higher highs + higher lows and the A-list flag — %s, %ld NY days 2014-26, flag-mid limit · stop flag low − 0.1 ATR · measured move · flat 15:55, one position at a time across 3-5m, fixed cost %.1f pts.\n", sym.c_str(), nd, costB);
  printf("Structure at the flag's registration (closed bars only): bull flags in HH/HL on 5m %.0f%%, on 15m %.0f%% · bear flags in LH/LL on 5m %.0f%%, on 15m %.0f%%.\n",
         100.0 * hh5n[0] / std::max(1L, totn[0]), 100.0 * hh15n[0] / std::max(1L, totn[0]), 100.0 * hh5n[1] / std::max(1L, totn[1]), 100.0 * hh15n[1] / std::max(1L, totn[1]));
  printf("Cells: 2014-17 | 2018-21 | 2022-24 | 2025-26 avg R (n) · all: R, win %%, PF, points/trade at today's ATR (%.0f), trades a year / a day, worst drawdown R, periods > 0.\n", Anow);
  auto run = [&](const std::string& name, std::function<bool(size_t)> sel) {
    std::map<int, std::vector<size_t>> byDay; for (size_t i = 0; i < rows.size(); ++i) if (rows[i].ok[C] && rows[i].year >= 2014 && sel(i)) byDay[rows[i].date].push_back(i);
    double m[4] = {0}, cn[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, pts = 0, cum = 0, peak = 0, dd = 0;
    for (auto& kv : byDay) { auto& v = kv.second; std::sort(v.begin(), v.end(), [&](size_t a, size_t b) { return rows[a].cKE[2] < rows[b].cKE[2]; }); int busy = -1;
      for (size_t i : v) { const Row& r = rows[i]; if (r.cKE[2] <= busy) continue; busy = r.cKO[2]; const double x = r.cR[C];
        m[per(r.year)] += x; cn[per(r.year)]++; sum += x; N++; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); pts += sc(r, r.cP[C]); cum += x; peak = std::max(peak, cum); dd = std::max(dd, peak - cum); } }
    if (N < 20) { printf("    %-40s fewer than 20 trades (%.0f)\n", name.c_str(), N); return; }
    int pos = 0, have = 0; for (int e = 0; e < 4; ++e) if (cn[e] >= 10) { have++; pos += m[e] > 0; }
    printf("    %-40s", name.c_str()); for (int e = 0; e < 4; ++e) { if (cn[e] < 10) printf("   —    (%4.0f) |", cn[e]); else printf(" %+6.2f (%4.0f) |", m[e] / cn[e], cn[e]); }
    printf(" all %+6.3fR win %4.1f%% PF %.2f · %+6.1f pts · %5.1f/yr %.2f/day · DD %5.1fR · %d/%d\n", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, pts / N, N / nd * 252.0, N / nd, dd, pos, have); };
  for (int side = 1; side >= -1; side -= 2) { const char* st = side > 0 ? "HH/HL" : "LH/LL";
    printf("\n######## %s 3-5m — structure %s (S5 = 5m, S15 = 15m)\n", side > 0 ? "BULL" : "BEAR", st);
    const char* GN[4] = {"every flag", "A (top third)", "B (middle third)", "C (bottom third)"};
    for (int g = 0; g < 4; ++g) { auto inG = [&, g](size_t i) { return rows[i].side == side && (g == 0 ? grade[i] > 0 : grade[i] == g); };
      run(std::string(GN[g]), inG);
      run(std::string(GN[g]) + " + S5", [&](size_t i) { return inG(i) && rows[i].hh5; });
      run(std::string(GN[g]) + " + S15", [&](size_t i) { return inG(i) && rows[i].hh15; });
      run(std::string(GN[g]) + " + S5 + S15", [&](size_t i) { return inG(i) && rows[i].hh5 && rows[i].hh15; });
      run(std::string(GN[g]) + " WITHOUT S5", [&](size_t i) { return inG(i) && !rows[i].hh5; });
      printf("\n"); }
    run("A + (B with S5)", [&](size_t i) { return rows[i].side == side && (grade[i] == 1 || (grade[i] == 2 && rows[i].hh5)); });
    run("A + (B with S5 + S15)", [&](size_t i) { return rows[i].side == side && (grade[i] == 1 || (grade[i] == 2 && rows[i].hh5 && rows[i].hh15)); }); }
  return 0;
}
