// 2026-10-20 flag_setforget.cpp (founder: "I want to test everything set and forget but it has to be perfect." — "the strategy",
// "the execution", "and the risk profile", "with as many accounts as possible"). The A grade (flag_agrade.cpp — its detection,
// features and flag-mid E2 · S2 trade, header below) as a bot would run it with nobody watching. Fixed before the run:
//  STRATEGY  S1 the walk-forward A list (honest, 2014-26) and the FROZEN page rule (weights WNQ, score >= 1.307, registered before
//            12:00 — what a bot would run; learned on these years, so in-sample) by period; S2 how many trades the two lists share
//  EXECUTION (on the walk-forward list) X1 cost 1.5 / 3 / 4.5 pts · X2 stop exits filled 2 / 4 / 8 ticks worse, 15:55 exits 1 tick
//            worse · X3 10 % / 25 % of trades missed at random (outage, no fill; 500 draws: mean, 5th percentile) · X4 all bad at once
//            (cost 3, stops 4 ticks worse, 10 % missed) · the strict fill (limit fills only on a trade-through) = run with `strict`
//  RISK      (founder: "tradeify", "ignore vest" — Vest code kept, not run) points scaled to today's ATR (stop distance × ATR now / ATR then), realistic fills (cost 1.5, stops 2 ticks worse) and the
//            X4 stress. Accounts: Vest $5k Silver ($40 eval, +$500 goal, −$300 static, −$150 day incl. open loss; funded = the same
//            limits — ASSUMED, unconfirmed — claim when +$500, keep $250, 80 % share, max 10 funded; fractional size) and Tradeify
//            Select Flex / Select Daily 50K (payout_sim.hpp plans, $165 eval, whole MNQ only — a trade whose 1 MNQ risk is above
//            1.5 × the $ risk is skipped, max 5 funded, a filler 1-MNQ trade (−$5) every eval week without a trade).
//            R1 one account: pass %, median days to pass, payout $ a funded account, net $ per eval bought
//            R2 programmes of K = 1 / 3 / 5 / 10 accounts (Tradeify ≤ 5) run 12 months from every month start 2014-01 … 2025-10,
//               a failed account replaced by a new eval the next day, all accounts taking the SAME trades (hedging banned):
//               accounts opened (a) together on day 1, (b) one a week; net = payouts − fees: mean, median, 10th pct, % of years > 0,
//               worst running loss
// Usage: flag_setforget [NQ] [strict]
// (header of flag_agrade.cpp follows)
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
#include "paper/payout_sim.hpp"
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
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC], cK[NC], pR[2]; bool pok[2]; int regMin, cE[NC], cX[NC]; bool tok[NTC]; double tR[NTC], tT[NTC]; double mae2, mfe2, atr; int kind2; };
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
              if (c == 2 * NST + 2) { double mae = 0, mfe = 0; for (size_t k = fillMin ? kE : kE + 1; k <= (size_t)ko && k < n; ++k) { mae = std::min(mae, Lw[k] - e); mfe = std::max(mfe, Hh[k] - e); }
                const double g = p + costB; r.mae2 = std::max(mae, st - e); r.mfe2 = std::min(mfe, H + M - e); r.atr = A;
                r.kind2 = std::fabs(g - (st - e)) < 1e-9 ? 1 : std::fabs(g - (H + M - e)) < 1e-9 ? 2 : 3; }
              r.ok[c] = true; r.cP[c] = p; r.cR[c] = p / risk; r.cK[c] = risk; r.cE[c] = (int)kE; r.cX[c] = ko; lastC[c] = ko; any = true;
              if (c == 0 || c == 2 * NST + 2) { const int pi = c == 0 ? 0 : 1; r.pok[pi] = true; r.pR[pi] = (simP(kE, e, st, H + M, fillMin) - costB) / risk; }
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); int k2; const double ce = Cc[k]; tw += (sim(k, ce, ce - risk, ce + (H + M - e), false, k2) - costB) / risk; }
              r.cT[c] = tw / 5;
              for (int ei = 0; ei < 2; ++ei) for (int si = 0; si < 4; ++si) if (TEN[ei] == en && TST[si] == sn) for (int x = 0; x < NX; ++x) { const int tc = (ei * 4 + si) * NX + x;
                r.tok[tc] = true; r.tR[tc] = (simTm(x, kE, e, st, H + M, fillMin, tf) - costB) / risk; double t2 = 0;
                for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k]; t2 += (simTm(x, k, ce, ce - risk, ce + (H + M - e), false, tf) - costB) / risk; } r.tT[tc] = t2 / 5; } } }
          if (any) rows.push_back(r); } } } }
  const int C2 = 2 * NST + 2;
  static const double WNQ[NF] = {-0.034, 0.239, 0.487, 0.313, 0.568, 0.328, 0.154, 0.122, -0.078, 0.171, 0.055, -0.141, 0.025, 0.012};
  const double SCORE_TOP = 1.307;   // the /flags page (flags.ts SCORE_W / SCORE_TOP)
  std::vector<size_t> B; for (size_t i = 0; i < rows.size(); ++i) if (rows[i].side > 0 && rows[i].ok[C2]) B.push_back(i);
  std::vector<char> top(B.size(), 0), tst(B.size(), 0);
  for (int Y = 2014; Y <= 2026; ++Y) { double w[NF];
    for (int f = 0; f < NF; ++f) { double sy = 0, ny = 0, sn = 0, nn = 0; for (size_t b = 0; b < B.size(); ++b) { auto& r = rows[B[b]]; if (r.year >= Y) continue; if (r.f[f]) { sy += r.cR[C2]; ny++; } else { sn += r.cR[C2]; nn++; } }
      w[f] = ny > 20 && nn > 20 ? sy / ny - sn / nn : 0; }
    auto sc = [&](const Row& r) { double v = 0; for (int f = 0; f < NF; ++f) v += r.f[f] ? w[f] : 0; return v; };
    std::vector<double> tr; for (size_t b = 0; b < B.size(); ++b) if (rows[B[b]].year < Y) tr.push_back(sc(rows[B[b]]));
    if (tr.size() < 100) continue; std::sort(tr.begin(), tr.end()); const double thr = tr[tr.size() * 2 / 3];
    for (size_t b = 0; b < B.size(); ++b) if (rows[B[b]].year == Y) { tst[b] = 1; top[b] = sc(rows[B[b]]) >= thr; } }
  struct T { int date, year, e, x, kind; double R, risk, mae, mfe, atr; };
  auto mk = [&](const Row& r) { return T{r.date, r.year, r.cE[C2], r.cX[C2], r.kind2, r.cR[C2], r.cK[C2], r.mae2, r.mfe2, r.atr}; };
  auto onePos = [](std::vector<T> A) { std::sort(A.begin(), A.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.e < b.e; });
    std::vector<T> o; int day = -1, lastX = -1; for (auto& t : A) { if (t.date != day) { day = t.date; lastX = -1; } if (t.e <= lastX) continue; lastX = t.x; o.push_back(t); } return o; };
  std::vector<T> W, F;
  for (size_t b = 0; b < B.size(); ++b) { auto& r = rows[B[b]]; if (r.regMin >= 1080) continue;
    if (tst[b] && top[b]) W.push_back(mk(r));
    double v = 0; for (int f = 0; f < NF; ++f) v += r.f[f] ? WNQ[f] : 0; if (v >= SCORE_TOP) F.push_back(mk(r)); }
  W = onePos(W); F = onePos(F);
  // R after an execution model: cost (pts round trip), stop exits `st` ticks worse, 15:55 exits `fl` ticks worse
  auto adj = [&](const T& t, double cost, double stT, double flT) { const double g = t.R * t.risk + costB; return (g - cost - (t.kind == 1 ? stT * 0.25 : t.kind == 3 ? flT * 0.25 : 0)) / t.risk; };
  struct St { int n; double mean, win, pf, tot, dd; int streak; };
  auto stats = [&](const std::vector<T>& L, const std::vector<double>& R) { St s{}; double gw = 0, gl = 0, eq = 0, pk = 0; int st = 0;
    for (size_t i = 0; i < L.size(); ++i) { const double r = R[i]; s.n++; s.tot += r; s.win += r > 0; (r > 0 ? gw : gl) += std::fabs(r); eq += r; pk = std::max(pk, eq); s.dd = std::max(s.dd, pk - eq); st = r <= 0 ? st + 1 : 0; s.streak = std::max(s.streak, st); }
    s.mean = s.n ? s.tot / s.n : NAN; s.win = s.n ? 100 * s.win / s.n : NAN; s.pf = gl > 0 ? gw / gl : NAN; return s; };
  auto Rof = [&](const std::vector<T>& L, double cost, double stT, double flT) { std::vector<double> R; for (auto& t : L) R.push_back(adj(t, cost, stT, flT)); return R; };
  auto pr = [&](const char* name, const St& s, double years) { printf("    %-44s %4d trades (%.1f/yr) · %+.3fR · win %2.0f%% · PF %.2f · total %+6.1fR · max DD %4.1fR · worst streak %d\n", name, s.n, s.n / years, s.mean, s.win, s.pf, s.tot, s.dd, s.streak); };
  printf("SET AND FORGET — %s A grade (NY bull 3-5m flag-mid, registered before 12:00, one position across charts)%s\n", sym.c_str(), thru > 0 ? " · STRICT fills (limit fills only on a trade-through)" : "");
  printf("\nSTRATEGY\n");
  { auto sub = [&](const std::vector<T>& L, int y0, int y1) { std::vector<T> o; for (auto& t : L) if (t.year >= y0 && t.year <= y1) o.push_back(t); return o; };
    pr("S1 walk-forward 2014-26 (honest)", stats(W, Rof(W, costB, 0, 0)), 12.8);
    printf("    frozen page rule (score >= %.3f; weights learned on these years — IN-SAMPLE):\n", SCORE_TOP);
    for (auto [y0, y1, yrs] : {std::tuple{2010, 2013, 4.0}, {2014, 2019, 6.0}, {2020, 2024, 5.0}, {2025, 2026, 1.8}, {2010, 2026, 16.8}}) {
      auto L = sub(F, y0, y1); char nm[64]; snprintf(nm, sizeof nm, "S1 frozen %d-%d", y0, y1); pr(nm, stats(L, Rof(L, costB, 0, 0)), yrs); }
    std::set<std::pair<int, int>> wk; for (auto& t : W) wk.insert({t.date, t.e}); int both = 0, f14 = 0; for (auto& t : F) if (t.year >= 2014) { f14++; both += wk.count({t.date, t.e}); }
    printf("    S2 2014-26: walk-forward %zu trades, frozen %d, shared %d (%.0f%% of frozen)\n", W.size(), f14, both, 100.0 * both / std::max(1, f14)); }
  printf("\nEXECUTION (walk-forward list)\n");
  pr("X0 base (cost 1.5)", stats(W, Rof(W, costB, 0, 0)), 12.8);
  for (double c : {3.0, 4.5}) { char nm[64]; snprintf(nm, sizeof nm, "X1 cost %.1f pts", c); pr(nm, stats(W, Rof(W, c, 0, 0)), 12.8); }
  for (double s2 : {2.0, 4.0, 8.0}) { char nm[64]; snprintf(nm, sizeof nm, "X2 stops %.0f ticks worse, 15:55 1 tick", s2); pr(nm, stats(W, Rof(W, costB, s2, 1)), 12.8); }
  { int nst = 0, ntg = 0, nfl = 0; for (auto& t : W) (t.kind == 1 ? nst : t.kind == 2 ? ntg : nfl)++; printf("    exits: stop %d · target %d · 15:55 %d\n", nst, ntg, nfl); }
  auto missed = [&](const std::vector<double>& R, double p, const char* nm) { std::mt19937 g(77); std::vector<double> tots; double m = 0;
    for (int it = 0; it < 500; ++it) { double s = 0; int k = 0; for (double r : R) if (std::uniform_real_distribution<>(0, 1)(g) >= p) { s += r; k++; } tots.push_back(s); m += s / std::max(k, 1); }
    std::sort(tots.begin(), tots.end()); printf("    %-44s mean %+.3fR a trade · total R: mean %+.1f, 5th pct %+.1f\n", nm, m / 500, std::accumulate(tots.begin(), tots.end(), 0.0) / 500, tots[25]); };
  missed(Rof(W, costB, 0, 0), 0.10, "X3 10% of trades missed"); missed(Rof(W, costB, 0, 0), 0.25, "X3 25% of trades missed");
  pr("X4 all bad (cost 3, stops 4 ticks, 15:55 1 tick)", stats(W, Rof(W, 3.0, 4, 1)), 12.8);
  missed(Rof(W, 3.0, 4, 1), 0.10, "X4 + 10% missed");
  // ── RISK ──
  double atrNow = 0; int dNow = 0; for (auto& d : s.days) if (d.ok && d.atr > 0 && d.date <= 20261016 && d.date > dNow) { dNow = d.date; atrNow = d.atr; }
  std::vector<int> cal; for (auto& d : s.days) if (d.ok && !d.rollwin && d.date >= 20140101 && d.date <= 20261016) cal.push_back(d.date);
  std::sort(cal.begin(), cal.end()); cal.erase(std::unique(cal.begin(), cal.end()), cal.end());
  std::map<int, int> ci; for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = (int)i;
  auto dayNum = [](int dt) { std::tm a{}; a.tm_year = dt / 10000 - 1900; a.tm_mon = dt / 100 % 100 - 1; a.tm_mday = dt % 100; a.tm_hour = 12; return (int)(timegm(&a) / 86400); };
  std::vector<int> week(cal.size()); std::vector<char> lastOfWeek(cal.size(), 0);
  for (size_t i = 0; i < cal.size(); ++i) week[i] = (dayNum(cal[i]) + 3) / 7;
  for (size_t i = 0; i < cal.size(); ++i) lastOfWeek[i] = i + 1 == cal.size() || week[i + 1] != week[i];
  {
    std::vector<double> rn; for (auto& t : W) rn.push_back(t.risk * atrNow / t.atr); std::sort(rn.begin(), rn.end());
    printf("\nRISK (ATR now %.0f pts on %d; stop distance scaled to it: median %.1f pts, 10th %.1f, 90th %.1f — 1 MNQ risks $%.0f median)\n", atrNow, dNow, rn[rn.size() / 2], rn[rn.size() / 10], rn[rn.size() * 9 / 10], 2 * rn[rn.size() / 2]); }
  // per-day trade lists for an account type and $ risk; fx = 0 realistic (cost 1.5, stops 2 ticks), 1 stress (X4 without misses)
  enum { VEST, FLEX, DAILY };
  auto dayTrades = [&](int kind, double usd, int fx, int& skipped) { std::vector<std::vector<payout::Trade>> D(cal.size()); skipped = 0;
    const double cost = fx ? 3.0 : costB, stT = fx ? 4 : 2, flT = 1;
    for (auto& t : W) { auto it = ci.find(t.date); if (it == ci.end()) continue; const double R = adj(t, cost, stT, flT);
      const double maeR = std::min((t.mae - (cost - costB) - costB) / t.risk, R), mfeR = std::max(t.mfe / t.risk, 0.0);
      double per = usd;   // $ per 1R
      if (kind != VEST) { const double rn = t.risk * atrNow / t.atr; int q = (int)std::floor(usd / (2 * rn)); if (q < 1 && 2 * rn <= 1.5 * usd) q = 1; if (q < 1) { skipped++; continue; } per = q * 2 * rn; }
      D[it->second].push_back({R * per, maeR * per, mfeR * per}); }
    return D; };
  struct Acct { bool funded = false, dead = false; double bal = 0, peak = 0, floor = 0, best = 0, cyc = 0; int wins = 0, days = 0, payouts = 0; bool traded = false; };
  const payout::Plan* PL[3] = {nullptr, nullptr, nullptr};
  for (auto& p : payout::plans()) { if (p.name == "select_flex_50k") PL[FLEX] = &p; if (p.name == "select_daily_50k") PL[DAILY] = &p; }
  auto fresh = [&](int kind) { Acct a; if (kind != VEST) { a.bal = a.peak = PL[kind]->start; a.floor = a.bal - PL[kind]->drawdown; } return a; };
  // one trading day; returns the payout paid that day (after the trader's share), sets dead / funded
  auto step = [&](int kind, Acct& a, const std::vector<payout::Trade>& tr, size_t ix) -> double {
    if (kind == VEST) { double dp = 0;
      for (auto& t : tr) { const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= -300 || dp + w <= -150) { a.dead = true; return 0; } dp += t.pnl; }
      a.bal += dp; if (!a.funded) { if (a.bal >= 500) { a.funded = true; a.bal = 0; } return 0; }
      if (a.bal >= 500) { const double pay = (a.bal - 250) * 0.8; a.bal = 250; a.payouts++; return pay; } return 0; }
    const payout::Plan& p = *PL[kind];
    if (!a.funded) { double cut = p.eval_daily_loss, dp = 0; bool any = false;
      for (auto& t : tr) { any = true; const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; }
      a.traded |= any; if (lastOfWeek[ix] && !a.traded) { dp -= 5; any = true; } if (lastOfWeek[ix]) a.traded = false;
      if (!any) return 0;
      a.bal += dp; a.days++; a.best = std::max(a.best, dp); a.peak = std::max(a.peak, a.bal); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, p.start + p.lock_above_start));
      if (a.bal <= a.floor) { a.dead = true; return 0; }
      const double profit = a.bal - p.start;
      if (profit >= p.eval_target && a.days >= p.eval_min_days && (p.eval_consistency <= 0 || a.best <= p.eval_consistency * profit)) { a = fresh(kind); a.funded = true; }
      return 0; }
    const double cut = p.daily_loss, lock = p.start + p.lock_above_start; double dp = 0;
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
  const char* KN[3] = {"Vest $5k Silver", "Tradeify Select Flex 50K", "Tradeify Select Daily 50K"}; const double FEE[3] = {40, 165, 165};
  for (int fx = 0; fx < 2; ++fx) {
    printf("\n  %s fills\n", fx ? "STRESS (cost 3, stops 4 ticks worse)" : "REALISTIC (cost 1.5, stops 2 ticks worse)");
    for (int kind = FLEX; kind < 3; ++kind) for (double usd : kind == VEST ? std::vector<double>{50, 75, 100, 150} : std::vector<double>{250, 400, 600}) {
      int skipped; const auto D = dayTrades(kind, usd, fx, skipped);
      // R1 single account from every 5th trading day 2014 … 2025-10: eval to the end; a passed one funded until it dies or the data ends
      int starts = 0, pass = 0, fail = 0; std::vector<int> dtp; double payF = 0; int nF = 0, lifeD = 0;
      for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; s0 += 5) { Acct a = fresh(kind); starts++; size_t i = s0;
        for (; i < cal.size() && !a.funded && !a.dead; ++i) step(kind, a, D[i], i);
        if (a.dead) { fail++; continue; } if (!a.funded) continue; pass++; dtp.push_back((int)(i - s0));
        double pay = 0; size_t j = i; for (; j < cal.size() && !a.dead; ++j) pay += step(kind, a, D[j], j); payF += pay; nF++; lifeD += (int)(j - i); }
      std::sort(dtp.begin(), dtp.end()); const double pp = 100.0 * pass / std::max(1, pass + fail), mp = nF ? payF / nF : 0;
      printf("  R1 %-26s $%3.0f/trade%s: pass %3.0f%% (unresolved %d of %d) · median %3d trading days to pass · funded account pays $%5.0f over %3.0f days alive · net per eval bought $%+5.0f\n",
             KN[kind], usd, skipped ? (" (" + std::to_string(skipped) + " trades skipped: 1 MNQ too big)").c_str() : "", pp, starts - pass - fail, starts, dtp.empty() ? -1 : dtp[dtp.size() / 2], mp, nF ? (double)lifeD / nF : 0.0, pp / 100 * mp - FEE[kind]);
      // R2 programmes
      for (int K : {1, 3, 5, 10}) { if (kind != VEST && K > 5) continue;
        for (int mode = 0; mode < 2; ++mode) { std::vector<double> nets, worst; double fees = 0, pays = 0; int nw = 0;
          for (int y = 2014; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break;
            const int d1 = d0 + 10000; size_t s0 = std::lower_bound(cal.begin(), cal.end(), d0) - cal.begin(); if (s0 >= cal.size()) break;
            std::vector<Acct> A(K); std::vector<char> open(K, 0); double net = 0, low = 0, f = 0, p = 0;
            for (size_t i = s0; i < cal.size() && cal[i] < d1; ++i) for (int k = 0; k < K; ++k) {
              if (!open[k]) { if ((int)(i - s0) < (mode ? 5 * k : 0)) continue; open[k] = 1; A[k] = fresh(kind); net -= FEE[kind]; f += FEE[kind]; }
              else if (A[k].dead) { A[k] = fresh(kind); net -= FEE[kind]; f += FEE[kind]; continue; }   // the replacement starts the next day
              const double pay = step(kind, A[k], D[i], i); net += pay; p += pay; low = std::min(low, net); }
            nets.push_back(net); worst.push_back(low); fees += f; pays += p; nw++; }
          std::vector<double> sn = nets; std::sort(sn.begin(), sn.end()); int pos = 0; for (double v : nets) pos += v > 0; std::sort(worst.begin(), worst.end());
          printf("     R2 %2d account%s %-9s 12 months (%d windows): net mean $%+6.0f · median $%+6.0f · 10th pct $%+6.0f · %3.0f%% of windows > 0 · fees $%5.0f · payouts $%6.0f · worst running loss $%6.0f (10th pct $%5.0f)\n",
                 K, K > 1 ? "s" : " ", mode ? "one/week" : "together", nw, std::accumulate(nets.begin(), nets.end(), 0.0) / nw, sn[nw / 2], sn[nw / 10], 100.0 * pos / nw, fees / nw, pays / nw, worst.front(), worst[nw / 10]); } } } }
  return 0;
}
