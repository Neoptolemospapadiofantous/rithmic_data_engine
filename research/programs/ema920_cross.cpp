// 2026-10-21 ema920_cross.cpp (founder: "on the 1m. when a cross happens 1m chart 9 20 ema. check to see if there some profit to be
// made there"). The 9/14 cross study (ema_cross_study.cpp, 2026-10-19) found no direction after a cross on 1/5/15m; this is the 9/20
// pair as a TRADE, fixed before the run. 1-minute EMA9 / EMA20 of closes, continuous across sessions (a chart's EMAs). A cross = EMA9
// moves from one side of EMA20 to the other at a 1m close; enter at that close.
//  ENTRY   (founder, mid-run: "add a limit order after calculation") M0 market at the cross close · M1 limit at the EMA9 of the cross
//          bar · M2 limit at the EMA20 of the cross bar — each a resting limit for 15 minutes after the cross (cancelled at the opposite
//          cross or the session end), filled when price trades AT the limit, entry = the limit (or the open if it gapped through)
//  SIDE    FOLLOW (buy the up cross, sell the down cross) and FADE (the opposite)
//  EXIT    X1 hold to the opposite cross (no reverse)       X2 stop = 10-bar swing low/high − 1 tick, target 2R
//          X3 stop 0.05 ATR, target 0.10 ATR (≈ 15 / 30 pts at today's ATR)   X4 out after 30 minutes
//          X5 (founder, mid-run: "or possible 1R") the X2 swing stop with a 1R target
//          every exit also flat at the end of the window's session (Asia 03:00, London 09:30, NY 15:55)
//  WINDOW  Asia 18:00-03:00 · London 03:00-09:30 · NY open 09:30-10:30 · NY mid 10:30-12:00 · NY pm 12:00-15:30 (entries)
//  FILTER  F0 every cross · F1 price on the cross's side of the session VWAP · F2 with the session's move AND a straight path (|close −
//          session open| / session range >= 0.55) · F3 clean (no other cross in the previous 30 min) · F4 EMA20 sloping the cross's way
//          over 5 bars · F5 = F1 + F2 + F4
// One position at a time per cell. Cost NQ 1.5 / ES 0.5 pts per round trip. Gross points are scaled to today's ATR (pnl / ATR that day ×
// the latest ATR) and the cost is then taken at today's size, so 2010 and 2026 add up. CONTROL per trade: 5 random OTHER days, the same minute of day, same direction, the same limit
// distance in ATR and the same exit (X1: the same holding time; X2/X3: the same stop / target distances in ATR; X4: 30 min) — "twin" = trade minus control.
// Periods 2010-15 | 2016-20 | 2021-24 | 2025-26. Usage: ema920_cross [NQ|ES]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 10:30 = 990, 12:00 = 1080, 15:30 = 1290, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NM = 3, NSIDE = 2, NX = 5, NW = 5, NF = 6 };
static const char* MN[NM] = {"M0 market at the cross", "M1 limit at EMA9", "M2 limit at EMA20"};
static const char* SIDEN[NSIDE] = {"FOLLOW", "FADE"};
static const char* XN[NX] = {"X1 to opposite cross", "X2 swing stop, 2R", "X3 0.05/0.10 ATR", "X4 30 min", "X5 swing stop, 1R"};
static const char* WN[NW] = {"Asia", "London", "NY open", "NY mid", "NY pm"};
static const char* FN[NF] = {"F0 all", "F1 VWAP side", "F2 with move+straight", "F3 clean 30 min", "F4 EMA20 slope", "F5 F1+F2+F4"};
struct Acc { double s = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}, ny = 0; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back(di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  auto sessEnd = [](int tm) { return tm < 540 ? 540 : tm < 930 ? 930 : 1315; };
  // one trade from cross bar i: entry mode (limit `lim`, NAN = market at the close), valid until `lim_until` (unix s), then exit x.
  // Returns gross points; holdOut = minutes held; for x == 0 `hold` is the minutes to the opposite cross counted from the cross bar.
  auto trade = [&](size_t i, int dir, double lim, int64_t lim_until, int x, double stA, double tgA, int hold, int endTm, int& minsOut, double A) -> double {
    size_t ie = i; double e = s.c[i]; const int di = s.day_of[i];
    if (!std::isnan(lim)) { ie = SIZE_MAX; for (size_t j = i + 1; j < s.size() && s.day_of[j] == di && s.t[j] <= lim_until; ++j) { if (tday_min(s.t[j]) >= endTm) break;
        if (dir * ((dir > 0 ? s.l[j] : s.h[j]) - lim) <= 0) { ie = j; e = dir > 0 ? std::min((double)s.o[j], lim) : std::max((double)s.o[j], lim); break; } }
      if (ie == SIZE_MAX) return NAN; }
    double stD = stA * A, tgD = tgA * A;
    if (x == 1 || x == 4) { double lo = 1e18, hi = -1e18; for (size_t j = i >= 9 ? i - 9 : 0; j <= ie; ++j) { lo = std::min(lo, (double)s.l[j]); hi = std::max(hi, (double)s.h[j]); }
      stD = std::max(dir > 0 ? e - lo + 0.25 : hi - e + 0.25, 1.0); if (!std::isnan(stA) && stA < 0) stD = -stA * A; tgD = x == 4 ? stD : 2 * stD; }
    const int64_t t0 = s.t[ie]; const int64_t tExit = x == 0 ? s.t[i] + (int64_t)hold * 60 : x == 3 ? t0 + 30 * 60 : INT64_MAX;
    if (ie != i && (x == 1 || x == 2 || x == 4)) { const double adverse = dir > 0 ? e - s.l[ie] : s.h[ie] - e; if (adverse >= stD) { minsOut = 0; return -stD; } }
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      if (tm >= endTm) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.o[j] - e); }
      if (x == 1 || x == 2 || x == 4) { const double lo = dir > 0 ? s.l[j] : -s.h[j], hi = dir > 0 ? s.h[j] : -s.l[j], ee = dir * e;
        if (lo <= ee - stD) { minsOut = (int)((s.t[j] - t0) / 60); return -stD; } if (hi >= ee + tgD) { minsOut = (int)((s.t[j] - t0) / 60); return tgD; } }
      else if (s.t[j] >= tExit) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.c[j] - e); } }
    minsOut = 0; return NAN; };
  // the first bar of day di at or after minute tm
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  static Acc acc[NM][NSIDE][NX][NW][NF]; std::mt19937 rng(20261021);
  static int64_t busy[NM][NSIDE][NX][NW][NF]; for (auto& m : busy) for (auto& a : m) for (auto& b : a) for (auto& c : b) for (auto& x : c) x = 0;   // last exit bar per cell
  double e9 = NAN, e20 = NAN; int sign = 0; std::vector<double> e20h; int64_t lastCross = 0;
  int curDay = -1, curSess = -1; double so = 0, shi = 0, slo = 0, pv = 0, vv = 0; long nCross = 0;
  std::vector<std::pair<size_t, int>> crosses;   // every cross (bar, new sign) — X1's exit is the next opposite one
  for (size_t i = 0; i < s.size(); ++i) { const double c = s.c[i]; if (std::isnan(e9)) { e9 = e20 = c; continue; }
    e9 += 2.0 / 10 * (c - e9); e20 += 2.0 / 21 * (c - e20); const int sg = e9 > e20 ? 1 : e9 < e20 ? -1 : 0;
    if (sg && sign && sg != sign) crosses.push_back({i, sg}); if (sg) sign = sg; }
  std::vector<size_t> nextOpp(crosses.size(), SIZE_MAX); for (size_t k = 0; k + 1 < crosses.size(); ++k) nextOpp[k] = crosses[k + 1].first;
  size_t ci = 0; e9 = e20 = NAN;
  for (size_t i = 0; i < s.size(); ++i) {
    const int di = s.day_of[i]; const double c = s.c[i];
    if (std::isnan(e9)) { e9 = e20 = c; } else { e9 += 2.0 / 10 * (c - e9); e20 += 2.0 / 21 * (c - e20); } e20h.push_back(e20);
    if (di < 0) continue; const Day& d = s.days[di]; const int tm = tday_min(s.t[i]);
    const int sess = tm < 540 ? 0 : tm < 930 ? 1 : tm < 1320 ? 2 : -1;
    if (di != curDay || sess != curSess) { curDay = di; curSess = sess; so = s.o[i]; shi = s.h[i]; slo = s.l[i]; pv = 0; vv = 0; }
    shi = std::max(shi, (double)s.h[i]); slo = std::min(slo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + c) / 3 * s.v[i]; vv += s.v[i];
    while (ci < crosses.size() && crosses[ci].first < i) ++ci;
    if (ci >= crosses.size() || crosses[ci].first != i) continue;
    const int dir = crosses[ci].second; const bool clean = s.t[i] - lastCross > 30 * 60; lastCross = s.t[i];
    if (sess < 0 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue;
    const int w = tm < 540 ? 0 : tm < 930 ? 1 : tm < 990 ? 2 : tm < 1080 ? 3 : tm < 1290 ? 4 : -1; if (w < 0) continue;
    nCross++; const double A = d.atr, vw = vv > 0 ? pv / vv : c, path = shi > slo ? std::fabs(c - so) / (shi - slo) : 0;
    const bool f1 = dir * (c - vw) > 0, f2 = dir * (c - so) > 0 && path >= 0.55, f4 = e20h.size() > 6 && dir * (e20 - e20h[e20h.size() - 6]) > 0;
    const bool f[NF] = {true, f1, f2, clean, f4, f1 && f2 && f4}; const int endTm = sessEnd(tm);
    const int64_t limUntil = s.t[i] + 15 * 60, oppT = nextOpp[ci] == SIZE_MAX ? INT64_MAX : s.t[nextOpp[ci]];
    const int hold = oppT == INT64_MAX ? 100000 : (int)((oppT - s.t[i]) / 60);
    for (int mo = 0; mo < NM; ++mo) for (int sd = 0; sd < NSIDE; ++sd) { const int td = sd == 0 ? dir : -dir;
      // a limit rests on the trade's favourable side only: buy limit below the close (follow up-cross at EMA9 below), else skip
      double lim = NAN; if (mo) { lim = mo == 1 ? e9 : e20; if (td * (c - lim) <= 0.25) continue; }
      const double limA = mo ? (c - lim) / A : 0;
      for (int x = 0; x < NX; ++x) {
        int mins = 0; const double stA = x == 2 ? 0.05 : NAN, tgA = x == 2 ? 0.10 : NAN;
        const double p = trade(i, td, lim, std::min(limUntil, oppT), x, stA, tgA, std::max(hold, 1), endTm, mins, A);
        if (std::isnan(p)) continue; const double net = p / A * Anow - cost; const int64_t exitT = s.t[i] + (int64_t)std::max(mins, 1) * 60;
        for (int ff = 0; ff < NF; ++ff) { if (!f[ff] || s.t[i] < busy[mo][sd][x][w][ff]) continue; busy[mo][sd][x][w][ff] = exitT;
          Acc& a = acc[mo][sd][x][w][ff]; a.s += net; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net); a.m[era(d.date / 10000)] += net; a.c[era(d.date / 10000)]++;
          if (ff == 0 || ff == 1 || ff == 5) {   // control: same minute, direction, limit distance (ATR) and exit on random other days
            double tw = 0; int nt = 0; for (int z = 0; z < 8 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0) continue;
              const double A2 = s.days[od].atr; int m2; const double lim2 = mo ? s.c[j] - limA * A2 : NAN;
              const double q = trade(j, td, lim2, s.t[j] + 15 * 60, x, stA, tgA, std::max(hold, 1), endTm, m2, A2);
              nt++; if (!std::isnan(q)) tw += q / A2 * Anow - cost; }   // an unfilled control limit counts as 0
            if (nt) { a.tw += net - tw / nt; a.ny++; } } } } }
  }
  printf("1-minute EMA9 / EMA20 cross as a trade — %s, %ld crosses in the windows (to 2026-10-16). Cost %.1f pts. Points scaled to today's ATR (%.0f).\n", sym.c_str(), nCross, cost, Anow);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 mean pts (n) · all: pts/trade, win %%, PF, trades/yr · twin = trade minus the same minute / direction / exit on random other days · periods > 0.\n");
  for (int mo = 0; mo < NM; ++mo) for (int sd = 0; sd < NSIDE; ++sd) for (int x = 0; x < NX; ++x) { printf("\n######## %s · %s · %s\n", MN[mo], SIDEN[sd], XN[x]);
    for (int w = 0; w < NW; ++w) for (int ff = 0; ff < NF; ++ff) { const Acc& a = acc[mo][sd][x][w][ff]; if (a.n < 50) continue; int pos = 0, have = 0;
      printf("  %-8s %-22s", WN[w], FN[ff]); for (int e = 0; e < 4; ++e) { if (a.c[e] < 20) printf("   —    (%5.0f) |", a.c[e]); else { printf(" %+6.2f (%5.0f) |", a.m[e] / a.c[e], a.c[e]); have++; pos += a.m[e] > 0; } }
      printf(" all %+6.2f win %2.0f%% PF %.2f %4.0f/yr", a.s / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
      if (a.ny) printf(" · twin %+6.2f", a.tw / a.ny); else printf("              "); printf(" · %d/%d\n", pos, have); } }
  return 0;
}
