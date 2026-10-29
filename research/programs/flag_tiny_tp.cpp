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
struct Row { double cMae[17 + 3], cMfe[17 + 3]; int cKE[17 + 3], cKO[17 + 3]; bool vdry, pbuy; double atr; int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC]; bool tok[NTC]; double tR[NTC], tT[NTC]; };
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
  std::vector<int> cal; std::vector<Row> rows; long nDays = 0, flagsTotal = 0; std::mt19937 rng(20261020);
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue;
    const double A = d.atr;
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
          Row base{}; base.atr = A; base.date = d.date; base.year = d.date / 10000; base.side = side; base.tf = tf;
          base.pb1 = pb == 1; base.broke = broke; base.gapw = gapA > 0.1; base.pole5 = M >= 5 * ar; base.m60w = m60 > 0.1;
          base.early = sessMin < 960; base.path = path && sessMin >= 960;   // 2026-10-20 audit: the 09:30-10:00 path only for flags registered at / after 10:00 (was look-ahead) base.pathgap = path && gapA > 0;
          base.flow = d.flow && fv > 0; base.sellers = base.flow && fb / fv < 0.49;
          { double pv2 = 0, pb2 = 0; for (int x = l; x <= h; ++x) { pv2 += b[x].v; pb2 += b[x].bv; }
            base.vdry = pv2 > 0 && fv / (q - h) < 0.7 * pv2 / (h - l + 1); base.pbuy = d.flow && pv2 > 0 && pb2 / pv2 >= 0.55; }
          { const int k0 = b[l].k0; double lod = 1e18, hod = -1e18; for (int k = 0; k < k0; ++k) { lod = std::min(lod, Lw[k]); hod = std::max(hod, Hh[k]); }
            const bool first = k0 == 0 || L <= lod + 0.25, hasLeg = k0 > 0 && hod > lod; const double depth = hasLeg ? (hod - L) / (hod - lod) : NAN;
            base.e[P0] = first; base.e[P1] = !first && hasLeg && depth < 0.382; base.e[P2] = !first && hasLeg && depth >= 0.382 && depth <= 0.618; base.e[P3] = !first && hasLeg && depth > 0.618;
            double vwl = NAN; for (int k = b[l].k0; k <= b[l].k; ++k) if (Lw[k] <= L + 1e-9) { vwl = VW[k]; break; }
            base.e[T1] = !std::isnan(vwl) && L <= vwl; base.e[T2] = L <= e20[l] + 1e-9; base.e[N1] = k0 > 0 && H > hod; }
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
              r.ok[c] = true; r.cP[c] = p; r.cR[c] = p / risk; lastC[c] = ko; any = true; if (en == 2) { r.cMae[sn] = gMae; r.cMfe[sn] = gMfe; r.cKE[sn] = (int)kE; r.cKO[sn] = ko; }
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k]; tw += sim(k, ce, ce - risk, ce + (tgt - e), false, k2) - costB; }
              r.cT[c] = tw / 5;   // twin kept in POINTS here
              for (int ei = 0; ei < 2; ++ei) for (int si = 0; si < 4; ++si) if (TEN[ei] == en && TST[si] == sn) for (int x = 0; x < NX; ++x) { const int tc = (ei * 4 + si) * NX + x;
                r.tok[tc] = true; r.tR[tc] = (simTm(x, kE, e, st, H + M, fillMin, tf) - costB) / risk; double t2 = 0;
                for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k]; t2 += (simTm(x, k, ce, ce - risk, ce + (H + M - e), false, tf) - costB) / risk; } r.tT[tc] = t2 / 5; } } }
          if (any) rows.push_back(r); } } } }

  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  // walk-forward selection mask (on the page cell E0 S0), per side
  std::vector<char> top(rows.size(), 0), tested(rows.size(), 0);
  for (int side = 1; side >= -1; side -= 2) for (int Y = 2014; Y <= 2026; ++Y) {
    double w[NF]; for (int f = 0; f < NF; ++f) { double sy = 0, ny = 0, sn = 0, nn = 0; for (auto& r : rows) if (r.side == side && r.year < Y && r.ok[0]) { if (r.f[f]) { sy += r.cR[0]; ny++; } else { sn += r.cR[0]; nn++; } } w[f] = ny >= 30 && nn >= 30 ? sy / ny - sn / nn : 0; }
    auto score = [&](const Row& r) { double sc = 0; for (int f = 0; f < NF; ++f) sc += r.f[f] ? w[f] : 0; return sc; };
    std::vector<double> tr; for (auto& r : rows) if (r.side == side && r.year < Y && r.ok[0]) tr.push_back(score(r));
    if (tr.size() < 100) continue; std::sort(tr.begin(), tr.end()); const double thr = tr[(size_t)(tr.size() * 2 / 3)];
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].side == side && rows[i].year == Y) { tested[i] = 1; top[i] = score(rows[i]) >= thr; } }
  printf("Flags: entry timing × stop placement × walk-forward selection — %s, %ld NY days, %ld flags (3-5m, researched tier). Fixed cost %.1f pts. Target = measured move.\n", sym.c_str(), nDays, flagsTotal, costB);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 avg pts at today's ATR (n) · all: pts/trade, win %%, PF, avg loser, worst drawdown (pts, 1 contract, whole history) · twin = trade minus a random minute in the hour after the flag, same TP / stop · periods > 0.\n");
  const double Anow = rows.empty() ? 1 : rows.back().atr;
  auto sc = [&](const Row& r, double pts) { return (pts + costB) / r.atr * Anow - costB; };   // points at today's ATR, cost at today's size
  auto line = [&](const char* name, std::function<bool(size_t)> sel, int c) { double m[4] = {0}, cn[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, tw = 0, cum = 0, peak = 0, dd = 0, nl = 0, sl = 0, sumR = 0;
    for (size_t i = 0; i < rows.size(); ++i) { auto& r = rows[i]; if (!r.ok[c] || !sel(i)) continue; const double x = sc(r, r.cP[c]); m[era(r.year)] += x; cn[era(r.year)]++; sum += x; N++; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); tw += x - sc(r, r.cT[c]);
      if (x <= 0) { nl++; sl += x; } sumR += r.cR[c]; cum += x; peak = std::max(peak, cum); dd = std::max(dd, peak - cum); }
    if (N < 30) return; int pos = 0, have = 0; for (int e = 0; e < 4; ++e) if (cn[e] >= 10) { have++; pos += m[e] > 0; }
    printf("    %-34s", name); for (int e = 0; e < 4; ++e) { if (cn[e] < 10) printf("   —    (%4.0f) |", cn[e]); else printf(" %+6.2f (%4.0f) |", m[e] / cn[e], cn[e]); }
    printf(" all %+6.2f pts win %4.1f%% PF %.2f · avg loser %7.1f · worst drawdown %6.0f pts · twin %+5.2f · %d/%d\n", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, nl ? sl / nl : 0.0, dd, tw / N, pos, have); };
  struct G { const char* name; std::function<bool(const Row&)> sel; };
  std::vector<G> groups = {
    {"NY bull 3-5m", [](const Row& r) { return r.side > 0; }},
    {"TAKE (bull 4/5m, 1st pb, broke, gap with, pole >= 5x)", [](const Row& r) { return r.side > 0 && r.tf >= 4 && r.pb1 && r.broke && r.gapw && r.pole5; }},
    {"NY bear 3-5m", [](const Row& r) { return r.side < 0; }},
    {"bull 3-5m + VOL (flag volume dries up)", [](const Row& r) { return r.side > 0 && r.vdry; }},
    {"bull 3-5m, flow days (2023-03+)", [](const Row& r) { return r.side > 0 && r.flow; }},
    {"bull 3-5m + OF-S (sellers in the flag)", [](const Row& r) { return r.side > 0 && r.sellers; }},
    {"bull 3-5m + OF-B (buyers in the pole)", [](const Row& r) { return r.side > 0 && r.pbuy; }},
    {"bull 3-5m + VOL + OF-S", [](const Row& r) { return r.side > 0 && r.vdry && r.sellers; }},
    {"TAKE + VOL", [](const Row& r) { return r.side > 0 && r.tf >= 4 && r.pb1 && r.broke && r.gapw && r.pole5 && r.vdry; }}};
  for (auto& g : groups) { printf("\n######## %s — entry × stop\n", g.name);
    for (int c = 0; c < NC; ++c) { const int en = c / NST, sn = c % NST; if (!(en == 0 || en == 2) || !((en == 0 && sn == 0) || (en == 2 && sn == 2) || sn >= 8)) continue; char nm[64]; snprintf(nm, 64, "%s · %s", ENN[en], STN[sn]); line(nm, [&](size_t i) { return g.sel(rows[i]); }, c); } }
  printf("\n######## WALK-FORWARD TOP THIRD (bull, test years 2014-26) — the A-like selection with the tiny-target exits\n");
  for (int c = 0; c < NC; ++c) { const int en = c / NST, sn = c % NST; if (!((en == 0 && sn == 0) || (en == 2 && (sn == 2 || sn >= 8)))) continue; char nm[64]; snprintf(nm, 64, "TOP %s · %s", ENN[en], STN[sn]);
    line(nm, [&](size_t i) { return rows[i].side > 0 && tested[i] && top[i]; }, c); }

  // ── PROP PAYOUTS ──
  printf("\n######## PROP PAYOUTS — flag-mid entry, bull 3-5m flags (one position at a time across charts), Tradeify 50K plans, 2014 → 2025-10\n");
  const payout::Plan *PF = nullptr, *PD = nullptr; for (auto& p : payout::plans()) { if (p.name == "select_flex_50k") PF = &p; if (p.name == "select_daily_50k") PD = &p; }
  std::map<int, size_t> ci; for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = i;
  struct Acct { bool funded = false, dead = false; double bal = 0, peak = 0, floor = 0, best = 0, cyc = 0; int wins = 0, days = 0, payouts = 0, fdays = 0; };
  auto fresh = [&](const payout::Plan& p) { Acct a; a.bal = a.peak = p.start; a.floor = p.start - p.drawdown; return a; };
  auto step = [&](const payout::Plan& p, Acct& a, const std::vector<payout::Trade>& tr) -> double {
    if (!a.funded) { double cut = p.eval_daily_loss, dp = 0; if (tr.empty()) return 0;
      for (auto& t : tr) { const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; }
      a.bal += dp; a.days++; a.best = std::max(a.best, dp); a.peak = std::max(a.peak, a.bal); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, p.start + p.lock_above_start));
      if (a.bal <= a.floor) { a.dead = true; return 0; } const double profit = a.bal - p.start;
      if (profit >= p.eval_target && a.days >= p.eval_min_days && (p.eval_consistency <= 0 || a.best <= p.eval_consistency * profit)) { a = fresh(p); a.funded = true; }
      return 0; }
    a.fdays++; const double cut = p.daily_loss, lock = p.start + p.lock_above_start; double dp = 0;
    auto trail = [&](double eq) { a.peak = std::max(a.peak, eq); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, lock)); };
    for (auto& t : tr) { if (p.intraday_trail && t.mfe > 0) trail(a.bal + dp + t.mfe); const double w = std::min(t.mae, t.pnl);
      if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; if (p.intraday_trail) trail(a.bal + dp); }
    a.bal += dp; if (dp >= p.win_day_min && !tr.empty()) a.wins++; a.cyc += dp; a.best = std::max(a.best, dp); trail(a.bal);
    const double profit = a.bal - p.start; const bool cons = p.consistency <= 0 || (a.cyc > 0 && a.best <= p.consistency * a.cyc);
    if (a.wins >= p.win_days_needed && profit > 0 && cons && (a.payouts == 0 || a.cyc > 0)) {
      const double cap = p.caps[std::min<size_t>(a.payouts, p.caps.size() - 1)]; double amt = 0;
      if (p.payout_mult > 0) { if (profit > p.buffer && a.cyc > 0) amt = std::min({cap, p.payout_mult * a.cyc, profit - p.buffer}); if (amt < p.min_payout) amt = 0; }
      else if (profit >= p.min_profit) amt = std::min(cap, p.profit_share * profit);
      if (amt > 0) { a.bal -= amt; a.payouts++; a.wins = 0; a.best = 0; a.cyc = 0; a.peak = a.bal; return amt * 0.9; } }
    return 0; };
  struct PG { const char* name; std::function<bool(size_t)> sel; };
  std::vector<PG> pgs = {{"all bull 3-5m flags", [&](size_t i) { return rows[i].side > 0; }},
                         {"walk-forward top third (A-like, 2014+)", [&](size_t i) { return rows[i].side > 0 && tested[i] && top[i]; }}};
  const int cellsP[6] = {2, 9, 11, 14, 17, 19}; const double FEE = 165;
  for (auto& g : pgs) for (int sn : cellsP) for (int q : {1, 2, 3, 5, 10}) {
    const int c = 2 * NST + sn;
    // day → trades, one position at a time across charts (sorted by entry minute)
    std::vector<std::vector<std::pair<int, int>>> idx(cal.size()); for (size_t i = 0; i < rows.size(); ++i) if (rows[i].ok[c] && g.sel(i) && rows[i].year >= 2014) idx[ci[rows[i].date]].push_back({rows[i].cKE[sn], (int)i});
    std::vector<std::vector<payout::Trade>> D(cal.size()); long ntr = 0, nw = 0;
    for (size_t di = 0; di < cal.size(); ++di) { auto& v = idx[di]; std::sort(v.begin(), v.end()); int busyTo = -1;
      for (auto& pr : v) { const Row& r = rows[pr.second]; if (r.cKE[sn] <= busyTo) continue; busyTo = r.cKO[sn]; const double f = Anow / r.atr;
        D[di].push_back({q * (2 * (r.cP[c] + costB) * f - 3.0), -q * (2 * r.cMae[sn] * f + 3.0), q * 2 * r.cMfe[sn] * f}); ntr++; nw += D[di].back().pnl > 0; } }
    for (int pk = 0; pk < 2; ++pk) { const payout::Plan& P = pk ? *PD : *PF;
      int starts = 0, pass = 0, fail = 0, paid10 = 0, paid20 = 0, paidAny = 0, nF = 0; std::vector<int> dtp; double payF = 0;
      for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; s0 += 5) { if (cal[s0] < 20140101) continue; Acct a = fresh(P); starts++; size_t i = s0;
        for (; i < cal.size() && !a.funded && !a.dead; ++i) step(P, a, D[i]); if (a.dead) { fail++; continue; } if (!a.funded) continue; pass++; dtp.push_back((int)(i - s0));
        nF++; double pay = 0; int first = -1; for (size_t j = i; j < cal.size() && !a.dead; ++j) { const double x = step(P, a, D[j]); if (x > 0 && first < 0) first = (int)(j - i + 1); pay += x; }
        payF += pay; paidAny += first > 0; paid10 += first > 0 && first <= 10; paid20 += first > 0 && first <= 20; }
      std::sort(dtp.begin(), dtp.end()); const double pp = pass + fail ? (double)pass / (pass + fail) : 0;
      printf("  %-38s %-22s %d MNQ (%4.1f%% win, %ld trades) %-16s eval pass %3.0f%% (median %3d days) · funded: paid in 10d %3.0f%% 20d %3.0f%% ever %3.0f%% · $%5.0f a funded acct · net/eval $%+5.0f\n",
             g.name, STN[sn], q, ntr ? 100.0 * nw / ntr : 0, ntr, P.name.c_str(), 100 * pp, dtp.empty() ? -1 : dtp[dtp.size() / 2], nF ? 100.0 * paid10 / nF : 0, nF ? 100.0 * paid20 / nF : 0, nF ? 100.0 * paidAny / nF : 0, nF ? payF / nF : 0, pp * (nF ? payF / nF : 0) - FEE);
      // P2: 5 accounts, 12 months
      for (int mode = 0; mode < 2; ++mode) { std::vector<double> nets; double fees = 0, pays = 0; int win = 0;
        for (int y = 2014; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break; const int d1 = d0 + 10000;
          size_t s0 = std::lower_bound(cal.begin(), cal.end(), d0) - cal.begin(); if (s0 >= cal.size()) break; std::vector<Acct> A(5); std::vector<char> open(5, 0); double net = 0;
          for (size_t i = s0; i < cal.size() && cal[i] < d1; ++i) for (int k = 0; k < 5; ++k) {
            if (!open[k]) { if ((int)(i - s0) < (mode ? 5 * k : 0)) continue; open[k] = 1; A[k] = fresh(P); net -= FEE; fees += FEE; }
            else if (A[k].dead) { A[k] = fresh(P); net -= FEE; fees += FEE; continue; }
            const double x = step(P, A[k], D[i]); net += x; pays += x; }
          nets.push_back(net); win += net > 0; }
        std::vector<double> sn2 = nets; std::sort(sn2.begin(), sn2.end()); const int nwn = nets.size();
        printf("      5 accounts %-9s 12 months: net mean $%+6.0f · median $%+6.0f · 10th pct $%+6.0f · %3.0f%% of years > 0 · fees $%5.0f · payouts $%6.0f\n", mode ? "one/week" : "together",
               std::accumulate(nets.begin(), nets.end(), 0.0) / nwn, sn2[nwn / 2], sn2[nwn / 10], 100.0 * win / nwn, fees / nwn, pays / nwn); } } }

  if (argc > 3 && std::string(argv[2]) == "export") { FILE* fo = fopen(argv[3], "w"); if (!fo) { perror(argv[3]); return 1; }
    const int sn = 2, c = 2 * NST + sn; std::vector<std::vector<std::pair<int, int>>> idx(cal.size());
    for (size_t i = 0; i < rows.size(); ++i) if (rows[i].ok[c] && rows[i].side > 0) idx[ci[rows[i].date]].push_back({rows[i].cKE[sn], (int)i});
    fprintf(fo, "date,top,gross_pts,mae_pts,mfe_pts,k_entry\n");
    for (size_t di = 0; di < cal.size(); ++di) { auto& v = idx[di]; std::sort(v.begin(), v.end()); int busyTo = -1;
      for (auto& pr : v) { const Row& r = rows[pr.second]; if (r.cKE[sn] <= busyTo) continue; busyTo = r.cKO[sn]; const double f = Anow / r.atr;
        fprintf(fo, "%d,%d,%.4f,%.4f,%.4f,%d\n", r.date, (int)(tested[pr.second] && top[pr.second]), (r.cP[c] + costB) * f, r.cMae[sn] * f, r.cMfe[sn] * f, r.cKE[sn]); } }
    fclose(fo); fprintf(stderr, "exported %s\n", argv[3]); }
  return 0;
}
