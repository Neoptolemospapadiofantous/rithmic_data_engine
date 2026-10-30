// 2026-10-29 ema79_vwap_sd.cpp (founder: "vwap 1-2 SD from 09:30, ema 7/9 on 1m. all sessions test"). Fixed before the run:
//  VWAP   anchored 09:30 ET (typical price x volume of 1m bars) and CARRIED through the evening, Asia and London until the next
//         09:30 (assumption: the founder did not say what the overnight uses). SD = volume-weighted SD of price around it.
//  SIGNAL EMA7 / EMA9 of 1m closes. BUY = EMA7 crosses above EMA9 under condition C below VWAP; SELL = mirror above VWAP.
//         Enter at the cross bar's close.
//  COND   C0 cross on the far side of VWAP (no band) - baseline · B1 close beyond 1 SD · B2 close beyond 2 SD ·
//         T1 1 SD band touched in the last 10 bars · T2 2 SD touched in the last 10 bars · Z12 close between 1 and 2 SD.
//  EXIT   stop = 10-bar swing extreme + 1 tick (min 1 pt). X1 target VWAP (live) · X2 1R · X3 2R · X4 opposite EMA cross.
//         Flat 15:55. Stop first when one bar touches both.
//  WINDOW entry time: Asia 18:00-03:00 · London 03:00-09:30 · NY open 09:45-10:30 · NY mid 10:30-12:00 · NY pm 12:00-15:30.
// One position at a time per cell. Cost NQ 1.5 / ES 0.5 pts per round trip, points scaled to today's ATR. Control = 5 random OTHER
// days, same minute / direction / stop+target in ATR. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26 (2025-26 = never tuned on).
// PASS (fixed before the run): n >= 150, net > 0 in all three 2010-24 periods AND in 2025-26, twin > 0, and the same cell net > 0 on ES.
// DUMP=<file> also writes every B1/B2/T2 trade (cond,exit,window,date,tday_min,net,control) for ema79_daily_stop.py.
// Usage: ema79_vwap_sd [NQ|ES]. tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 15:30 = 1290, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NTF = 1, NC = 6, NX = 4, NW = 6 };
static const int TFS[NTF] = {1};
static const char* CN[NC] = {"C0 VWAP side", "B1 beyond 1SD", "B2 beyond 2SD", "T1 1SD touched", "T2 2SD touched", "Z12 between 1-2SD"};
static const char* XN[NX] = {"X1 target VWAP, swing stop", "X2 swing stop, 1R", "X3 swing stop, 2R", "X4 to opposite cross"};
static const char* WN[NW] = {"all sessions", "Asia 18-03", "London 03-09:30", "NY open 09:45-10:30", "NY mid 10:30-12", "NY pm 12-15:30"};
struct Acc { double s = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, ny = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}, raw = 0; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  const int END = 1315;
  // session VWAP / SD per 1m bar (NAN outside RTH)
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0;
    bool started = false; (void)cur;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; const int tm = tday_min(s.t[i]); if (di < 0) continue;
      if (tm >= 930 && cur != di) { cur = di; pv = pq = vv = 0; started = true; }
      if (!started) continue;
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  // trade on 1m bars from entry bar ie (entered at its close). x: 0 VWAP target, 1/2 R targets, 3 opposite-cross time exit.
  // fixedTg > 0 replaces the live VWAP target (controls). Returns gross pts, minutes held via minsOut; NAN = no exit found.
  auto trade = [&](size_t ie, int dir, int x, double stD, double fixedTg, int64_t tExit, int& minsOut) -> double {
    const double e = s.c[ie]; const int di = s.day_of[ie]; const int64_t t0 = s.t[ie];
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      if (tm >= END) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.o[j] - e); }
      if (x == 3) { if (s.t[j] >= tExit) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.c[j] - e); } continue; }
      const double adv = dir > 0 ? e - s.l[j] : s.h[j] - e, fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv >= stD) { minsOut = (int)((s.t[j] - t0) / 60); return -stD; }
      double tg = x == 1 ? stD : x == 2 ? 2 * stD : fixedTg;
      if (x == 0 && fixedTg <= 0) { const double vw = VW[j - 1]; if (std::isnan(vw)) continue; tg = dir * (vw - e); if (tg <= 0) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.o[j] - e); } }
      if (fav >= tg) { minsOut = (int)((s.t[j] - t0) / 60); return tg; } }
    minsOut = 0; return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  FILE* dumpf = getenv("DUMP") ? fopen(getenv("DUMP"), "w") : nullptr;   // per-trade CSV for the daily-stop study
  static Acc acc[NTF][NC][NX][NW]; std::mt19937 rng(20261029); long nSig[NTF] = {0};
  for (int ti = 0; ti < NTF; ++ti) { const int tf = TFS[ti];
    // build chart bars: bucket = floor(tday_min / tf) within a trading day; a bar closes at the last 1m bar of its bucket
    struct CB { size_t i1; double h, l, c; }; std::vector<CB> cb; std::vector<double> e7v, e21v;
    { long curB = -1; int curD = -2; CB b{0, 0, 0, 0};
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const long bk = tday_min(s.t[i]) / tf;
        if (di != curD || bk != curB) { if (curB >= 0) cb.push_back(b); curD = di; curB = bk; b = {i, s.h[i], s.l[i], s.c[i]}; }
        else { b.i1 = i; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; } }
      if (curB >= 0) cb.push_back(b); }
    double e7 = cb[0].c, e21 = cb[0].c; e7v.resize(cb.size()); e21v.resize(cb.size());
    for (size_t k = 0; k < cb.size(); ++k) { e7 += 2.0 / 8 * (cb[k].c - e7); e21 += 2.0 / 10 * (cb[k].c - e21); e7v[k] = e7; e21v[k] = e21; }
    // next opposite cross (chart-bar index) for X4
    std::vector<long> nextX(cb.size(), -1); { long nx = -1; int sgNext = 0;
      for (long k = (long)cb.size() - 1; k > 0; --k) { const int a = e7v[k] > e21v[k] ? 1 : -1, b = e7v[k - 1] > e21v[k - 1] ? 1 : -1; nextX[k] = nx; (void)sgNext; if (a != b) nx = k; } }
    static int64_t busy[NC][NX][NW]; for (auto& a : busy) for (auto& b : a) for (auto& x : b) x = 0;
    for (size_t k = 11; k < cb.size(); ++k) {
      const int sg = e7v[k] > e21v[k] ? 1 : -1, sp = e7v[k - 1] > e21v[k - 1] ? 1 : -1; if (sg == sp) continue;
      const size_t i = cb[k].i1; const int di = s.day_of[i]; const Day& d = s.days[di]; const int tm = tday_min(s.t[i]);
      if ((tm >= 930 && tm < 945) || tm >= 1290 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const double vw = VW[i], sd = SDV[i]; if (std::isnan(vw) || !(sd > 0)) continue;
      const int dir = sg;   // the cross direction: up-cross = BUY (needs rollover from BELOW), down-cross = SELL (from ABOVE)
      const double c = s.c[i], A = d.atr; double ext = dir > 0 ? 1e18 : -1e18;   // extreme of the last 10 chart bars (incl. this one)
      for (size_t q = k - 9; q <= k; ++q) ext = dir > 0 ? std::min(ext, cb[q].l) : std::max(ext, cb[q].h);
      // band touched in the last 10 bars, using the band at each bar's close
      auto touched = [&](double kk) { for (size_t q = k - 9; q <= k; ++q) { const size_t iq = cb[q].i1; if (s.day_of[iq] != di || std::isnan(VW[iq])) continue;
          const double band = VW[iq] - dir * kk * SDV[iq]; if (dir > 0 ? cb[q].l <= band : cb[q].h >= band) return true; } return false; };
      auto beyond = [&](double kk) { return dir * (c - (vw - dir * kk * sd)) <= 0; };
      const bool f[NC] = {dir * (c - vw) < 0, beyond(1), beyond(2), touched(1), touched(2), beyond(1) && !beyond(2)};
      bool any = false; for (bool b : f) any |= b; if (!any) continue; nSig[ti]++;
      const int w = tm < 540 ? 1 : tm < 930 ? 2 : tm < 990 ? 3 : tm < 1080 ? 4 : 5;
      const double stD = std::max(dir > 0 ? c - ext + 0.25 : ext - c + 0.25, 1.0);   // swing stop, min 1 pt
      const int64_t tOpp = nextX[k] >= 0 ? s.t[cb[nextX[k]].i1] : INT64_MAX;
      for (int x = 0; x < NX; ++x) { int mins = 0; const double p = trade(i, dir, x, stD, 0, tOpp, mins); if (std::isnan(p)) continue;
        const double net = p / A * Anow - cost; const int64_t exitT = s.t[i] + (int64_t)std::max(mins, 1) * 60;
        // control once per (signal, exit)
        double tw = 0; int nt = 0; const double stA = stD / A, tgA = dir * (vw - c) / A;
        for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0) continue;
          const double A2 = s.days[od].atr; int m2; const double q = trade(j, dir, x, std::max(stA * A2, 1.0), x == 0 ? std::max(tgA * A2, 0.25) : 0, s.t[j] + (int64_t)mins * 60, m2);
          if (std::isnan(q)) continue; nt++; tw += q / A2 * Anow - cost; }
        for (int cc = 0; cc < NC; ++cc) { if (!f[cc]) continue; for (int ww : {0, w}) { if (s.t[i] < busy[cc][x][ww]) continue; busy[cc][x][ww] = exitT;
            if (dumpf && (cc == 1 || cc == 2 || cc == 4)) fprintf(dumpf, "%d,%d,%d,%d,%d,%.3f,%.3f\n", cc, x, ww, d.date, tm, net, nt ? tw / nt : 0.0);
            Acc& a = acc[ti][cc][x][ww]; a.s += net; a.raw += p; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
            a.m[era(d.date / 10000)] += net; a.c[era(d.date / 10000)]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } }
  printf("EMA 7/9 1m cross off the 09:30 VWAP SD bands — %s, all sessions, 2010-07 → 2026-10-23. Cost %.1f pts. Points scaled to today's ATR (%.0f).\n", sym.c_str(), cost, Anow);
  printf("signals (any condition): 1m %ld\n", nSig[0]);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 mean pts (n) · all: pts/trade net, raw pts, win %%, PF, trades/yr · twin = minus random other-day control · periods > 0.\n");
  for (int ti = 0; ti < NTF; ++ti) for (int x = 0; x < NX; ++x) { printf("\n######## %dm · %s\n", TFS[ti], XN[x]);
    for (int cc = 0; cc < NC; ++cc) for (int w = 0; w < NW; ++w) { const Acc& a = acc[ti][cc][x][w]; if (a.n < 30) continue; int pos = 0, have = 0;
      printf("  %-20s %-17s", CN[cc], WN[w]); for (int e = 0; e < 4; ++e) { if (a.c[e] < 15) printf("   —    (%4.0f) |", a.c[e]); else { printf(" %+6.2f (%4.0f) |", a.m[e] / a.c[e], a.c[e]); have++; pos += a.m[e] > 0; } }
      printf(" all %+6.2f raw %+6.2f win %2.0f%% PF %.2f %4.0f/yr", a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
      if (a.ny) printf(" · twin %+6.2f", a.tw / a.ny); printf(" · %d/%d\n", pos, have); } }
  if (dumpf) fclose(dumpf);
  return 0;
}
