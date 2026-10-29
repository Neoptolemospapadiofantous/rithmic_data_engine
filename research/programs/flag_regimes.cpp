// 2026-10-21 flag_regimes.cpp (founder: "test regims"). The A grade (flag_setforget.cpp / flag_agrade.cpp — same detection,
// features, flag-mid E2 · S2 trade, one position across the 3-5m charts, registered before 12:00) split by REGIMES KNOWN BEFORE
// THE OPEN. Fixed before the run (no bucket chosen after looking):
//   G1 VIX prior close <15 / 15-20 / 20-25 / >=25      G2 VIX last daily change: fell >3 % / flat / rose >3 %
//   G3 ATR14 percentile vs the prior 250 sessions: low / mid / high third
//   G4 prior close vs SMA50 above / below               G5 prior close vs SMA200 above / below
//   G6 20-session daily path e20: choppy (<0.2) / mixed / trending (>=0.35)   G7 20-session direction r20: down / up
//   G8 prior day IBS: weak (<0.3) / middle / strong (>0.7)   G9 gap at 09:30 vs ATR: down < -0.1 / flat / up > 0.1
//   G10 weekday   G11 event day (FOMC / CPI / NFP / opex) vs none   G12 year
// Lists: W = walk-forward A list 2014-26 (honest), F = the frozen page rule 2010-26 (in-sample weights, more trades).
// Per bucket: trades, mean R (after 1.5 pts cost), win %, PF, mean R in 2010/14-19 and 2020-26 (both halves must agree).
// Per family: permutation p (2,000 shuffles of R across the list's trades): the share of shuffles whose best-minus-worst bucket
// spread (buckets with >= 20 trades) is at least the real one — with 12 families, p < 0.004 (0.05 / 12) is what counts.
// Usage: flag_regimes [NQ|ES]
#include "paper/payout_sim.hpp"
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
#include <ctime>
#include <functional>
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
struct Row { int date, year, side, tf, var; bool pb1, broke, gapw, pole5, m60w, path, pathgap, early, flow, sellers, e[NE]; double R, pts, twin, twinD; bool ok[NC], f[NF]; double cR[NC], cP[NC], cT[NC], cK[NC], pR[2]; bool pok[2]; int regMin, cE[NC], cX[NC]; bool tok[NTC]; double tR[NTC], tT[NTC]; double mae2, mfe2, atr; int kind2, di; };
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
          Row base{}; base.di = (int)(&d - s.days.data()); base.date = d.date; base.year = d.date / 10000; base.side = side; base.tf = tf;
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
  struct T { int date, year, e, x, kind; double R, risk, mae, mfe, atr; int di; };
  auto mk = [&](const Row& r) { return T{r.date, r.year, r.cE[C2], r.cX[C2], r.kind2, r.cR[C2], r.cK[C2], r.mae2, r.mfe2, r.atr, r.di}; };
  auto onePos = [](std::vector<T> A) { std::sort(A.begin(), A.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.e < b.e; });
    std::vector<T> o; int day = -1, lastX = -1; for (auto& t : A) { if (t.date != day) { day = t.date; lastX = -1; } if (t.e <= lastX) continue; lastX = t.x; o.push_back(t); } return o; };
  std::vector<T> W, F;
  for (size_t b = 0; b < B.size(); ++b) { auto& r = rows[B[b]]; if (r.regMin >= 1080) continue;
    if (tst[b] && top[b]) W.push_back(mk(r));
    double v = 0; for (int f = 0; f < NF; ++f) v += r.f[f] ? WNQ[f] : 0; if (v >= SCORE_TOP) F.push_back(mk(r)); }
  W = onePos(W); F = onePos(F);

  // ── regimes known before the open ──
  std::vector<double> atrPct(s.days.size(), NAN);
  { std::vector<double> past; for (size_t i = 0; i < s.days.size(); ++i) { auto& d = s.days[i]; if (!d.ok || !(d.atr > 0)) continue;
      if (past.size() >= 100) { size_t lo = past.size() > 250 ? past.size() - 250 : 0; int below = 0, tot = 0; for (size_t j = lo; j < past.size(); ++j) { tot++; below += past[j] < d.atr; } atrPct[i] = (double)below / tot; }
      past.push_back(d.atr); } }
  struct Fam { const char* name; std::vector<const char*> lab; std::function<int(const Day&, size_t)> f; };
  auto gapOf = [&](const Day& d) { return std::isnan(d.pc) || !(d.atr > 0) ? NAN : (d.ro - d.pc) / d.atr; };
  std::vector<Fam> fams = {
    {"G1 VIX prior close", {"<15", "15-20", "20-25", ">=25"}, [](const Day& d, size_t) { return std::isnan(d.vix) ? -1 : d.vix < 15 ? 0 : d.vix < 20 ? 1 : d.vix < 25 ? 2 : 3; }},
    {"G2 VIX last daily change", {"fell >3%", "flat", "rose >3%"}, [](const Day& d, size_t) { return std::isnan(d.vixc1) ? -1 : d.vixc1 < -0.03 ? 0 : d.vixc1 > 0.03 ? 2 : 1; }},
    {"G3 ATR14 vs prior 250 sessions", {"low third", "mid third", "high third"}, [&](const Day&, size_t i) { const double p = atrPct[i]; return std::isnan(p) ? -1 : p < 1.0 / 3 ? 0 : p < 2.0 / 3 ? 1 : 2; }},
    {"G4 prior close vs SMA50", {"below", "above"}, [](const Day& d, size_t) { return std::isnan(d.sma50) || std::isnan(d.pc) ? -1 : d.pc > d.sma50; }},
    {"G5 prior close vs SMA200", {"below", "above"}, [](const Day& d, size_t) { return std::isnan(d.sma200) || std::isnan(d.pc) ? -1 : d.pc > d.sma200; }},
    {"G6 20-session path e20", {"choppy <0.2", "mixed", "trending >=0.35"}, [](const Day& d, size_t) { return std::isnan(d.e20) ? -1 : d.e20 < 0.2 ? 0 : d.e20 < 0.35 ? 1 : 2; }},
    {"G7 20-session direction r20", {"down", "up"}, [](const Day& d, size_t) { return std::isnan(d.r20) ? -1 : d.r20 > 0; }},
    {"G8 prior day IBS", {"weak <0.3", "middle", "strong >0.7"}, [](const Day& d, size_t) { return std::isnan(d.pibs) ? -1 : d.pibs < 0.3 ? 0 : d.pibs > 0.7 ? 2 : 1; }},
    {"G9 gap at 09:30 vs ATR", {"down <-0.1", "flat", "up >0.1"}, [&](const Day& d, size_t) { const double g = gapOf(d); return std::isnan(g) ? -1 : g < -0.1 ? 0 : g > 0.1 ? 2 : 1; }},
    {"G10 weekday", {"Mon", "Tue", "Wed", "Thu", "Fri"}, [](const Day& d, size_t) { return d.dow >= 1 && d.dow <= 5 ? d.dow - 1 : -1; }},
    {"G11 event day", {"none", "FOMC/CPI/NFP/opex"}, [](const Day& d, size_t) { return d.fomc || d.cpi || d.nfp || d.opex ? 1 : 0; }},
    {"G12 year", {"2010-13", "2014-16", "2017-19", "2020-22", "2023-24", "2025-26"}, [](const Day& d, size_t) { const int y = d.date / 10000; return y < 2014 ? 0 : y < 2017 ? 1 : y < 2020 ? 2 : y < 2023 ? 3 : y < 2025 ? 4 : 5; }},
  };
  auto run = [&](const char* title, const std::vector<T>& L, int split) {
    printf("\n%s — %zu trades\n", title, L.size());
    { double t = 0, gw = 0, gl = 0; int w = 0; for (auto& x : L) { t += x.R; w += x.R > 0; (x.R > 0 ? gw : gl) += std::fabs(x.R); } printf("  all: %+.3fR · win %.0f%% · PF %.2f\n", t / L.size(), 100.0 * w / L.size(), gw / gl); }
    std::mt19937 g(4242);
    for (auto& F : fams) {
      std::vector<int> bk(L.size()); for (size_t i = 0; i < L.size(); ++i) bk[i] = F.f(s.days[L[i].di], L[i].di);
      const int nb = F.lab.size();
      auto spread = [&](const std::vector<double>& R) { std::vector<double> sm(nb, 0); std::vector<int> c(nb, 0); for (size_t i = 0; i < R.size(); ++i) if (bk[i] >= 0) { sm[bk[i]] += R[i]; c[bk[i]]++; }
        double mx = -1e9, mn = 1e9; int ok = 0; for (int b = 0; b < nb; ++b) if (c[b] >= 20) { ok++; mx = std::max(mx, sm[b] / c[b]); mn = std::min(mn, sm[b] / c[b]); } return ok >= 2 ? mx - mn : NAN; };
      std::vector<double> R; for (auto& x : L) R.push_back(x.R); const double real = spread(R); int ge = 0, tot = 0;
      if (!std::isnan(real)) for (int it = 0; it < 2000; ++it) { auto Q = R; std::shuffle(Q.begin(), Q.end(), g); const double sp = spread(Q); if (!std::isnan(sp)) { tot++; ge += sp >= real - 1e-12; } }
      printf("  %-34s spread %s · permutation p %s\n", F.name, std::isnan(real) ? "  n/a" : (std::to_string(real).substr(0, 5) + "R").c_str(), tot ? std::to_string((double)ge / tot).substr(0, 5).c_str() : "n/a");
      for (int b = 0; b < nb; ++b) { int n = 0, w = 0, n1 = 0, n2 = 0; double t = 0, gw = 0, gl = 0, t1 = 0, t2 = 0;
        for (size_t i = 0; i < L.size(); ++i) if (bk[i] == b) { const double r = L[i].R; n++; t += r; w += r > 0; (r > 0 ? gw : gl) += std::fabs(r); if (L[i].year < split) { n1++; t1 += r; } else { n2++; t2 += r; } }
        if (!n) { printf("      %-20s    0\n", F.lab[b]); continue; }
        printf("      %-20s %4d  %+.3fR  win %2.0f%%  PF %4.2f   halves %+.2fR (%d) / %+.2fR (%d)%s\n", F.lab[b], n, t / n, 100.0 * w / n, gl > 0 ? gw / gl : 99.0,
               n1 ? t1 / n1 : NAN, n1, n2 ? t2 / n2 : NAN, n2, n < 20 ? "   (thin)" : ""); } } };
  run("W  walk-forward A list 2014-26 (honest) · halves 2014-19 / 2020-26", W, 2020);
  run("F  frozen page rule 2010-26 (in-sample weights) · halves 2010-19 / 2020-26", F, 2020);
  return 0;
}
