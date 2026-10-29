// 2026-10-26 ema721_vwap_sd.cpp (founder: "vwap with 1-2 SD. I wanna trade 7 and 21 ema cross over but if we roll over 1SD we sell
// if we roll over below we buy. test this as well as the 2SD"). Fixed before the run:
//  SIGNAL  EMA7 / EMA21 of closes on the 1m, 3m and 5m chart (continuous across sessions, like a chart). SELL = EMA7 crosses below
//          EMA21 while price has rolled over from above VWAP + k SD; BUY = EMA7 crosses above EMA21 after rolling over from below
//          VWAP − k SD. Enter at the cross bar's close. Bands = session VWAP (anchored 09:30 ET, typical price × volume of the 1m bars)
//          ± k × the volume-weighted standard deviation of price around it.
//  ROLLED  C0 baseline k = 0 (cross on the far side of VWAP, no band) · "close": the cross bar still closes beyond the band ·
//          "touch": the band was touched in the last 10 chart bars (the cross itself may be back inside) — for k = 1 and k = 2.
//  EXIT    X1 target VWAP (live, moves with the session), stop = the 10-bar swing extreme + 1 tick · X2 swing stop, 1R ·
//          X3 swing stop, 2R · X4 hold to the opposite EMA cross (no stop). All flat 15:55.
//  WINDOW  NY entries 09:45-15:30 (bands need 15 min to form), split open 09:45-10:30 / mid 10:30-12:00 / pm 12:00-15:30.
// One position at a time per cell. Stops/targets checked on 1m bars, stop first when a bar touches both. Cost NQ 1.5 / ES 0.5 pts per
// round trip. Points scaled to today's ATR (pnl / ATR that day × the latest ATR), cost at today's size, so 2010 and 2026 add up.
// CONTROL per trade (other-day twins, never same-day): 5 random OTHER days, same minute, same direction, the same stop / target distance
// in ATR (X1 target = that trade's distance to VWAP at entry), X4 the same holding time. twin = trade minus control.
// Periods 2010-15 | 2016-20 | 2021-24 | 2025-26 (the last = out of sample: nothing here was tuned). Usage: ema721_vwap_sd [NQ|ES].
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 10:30 = 990, 12:00 = 1080, 15:30 = 1290, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NTF = 3, NC = 7, NX = 4, NW = 4 };
static const int TFS[NTF] = {1, 3, 5};
static const char* CN[NC] = {"C0 VWAP side (k=0)", "1SD within 0.25 SD", "1SD within 0.5 SD", "1SD within 0.1 ATR", "2SD within 0.25 SD", "2SD within 0.5 SD", "2SD within 0.1 ATR"};
static const char* XN[NX] = {"X1 target VWAP, swing stop", "X2 swing stop, 1R", "X3 swing stop, 2R", "X4 to opposite cross"};
static const char* WN[NW] = {"all 09:45-15:30", "open 09:45-10:30", "mid 10:30-12:00", "pm 12:00-15:30"};
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
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; const int tm = tday_min(s.t[i]); if (di < 0 || tm < 930 || tm >= 1320) continue;
      if (di != cur) { cur = di; pv = pq = vv = 0; }
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
  static Acc acc[NTF][NC][NX][NW]; std::mt19937 rng(20261026); long nSig[NTF] = {0, 0, 0};
  for (int ti = 0; ti < NTF; ++ti) { const int tf = TFS[ti];
    // build chart bars: bucket = floor(tday_min / tf) within a trading day; a bar closes at the last 1m bar of its bucket
    struct CB { size_t i1; double h, l, c; }; std::vector<CB> cb; std::vector<double> e7v, e21v;
    { long curB = -1; int curD = -2; CB b{0, 0, 0, 0};
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const long bk = tday_min(s.t[i]) / tf;
        if (di != curD || bk != curB) { if (curB >= 0) cb.push_back(b); curD = di; curB = bk; b = {i, s.h[i], s.l[i], s.c[i]}; }
        else { b.i1 = i; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; } }
      if (curB >= 0) cb.push_back(b); }
    double e7 = cb[0].c, e21 = cb[0].c; e7v.resize(cb.size()); e21v.resize(cb.size());
    for (size_t k = 0; k < cb.size(); ++k) { e7 += 2.0 / 8 * (cb[k].c - e7); e21 += 2.0 / 22 * (cb[k].c - e21); e7v[k] = e7; e21v[k] = e21; }
    // next opposite cross (chart-bar index) for X4
    std::vector<long> nextX(cb.size(), -1); { long nx = -1; int sgNext = 0;
      for (long k = (long)cb.size() - 1; k > 0; --k) { const int a = e7v[k] > e21v[k] ? 1 : -1, b = e7v[k - 1] > e21v[k - 1] ? 1 : -1; nextX[k] = nx; (void)sgNext; if (a != b) nx = k; } }
    static int64_t busy[NC][NX][NW]; for (auto& a : busy) for (auto& b : a) for (auto& x : b) x = 0;
    for (size_t k = 11; k < cb.size(); ++k) {
      const int sg = e7v[k] > e21v[k] ? 1 : -1, sp = e7v[k - 1] > e21v[k - 1] ? 1 : -1; if (sg == sp) continue;
      const size_t i = cb[k].i1; const int di = s.day_of[i]; const Day& d = s.days[di]; const int tm = tday_min(s.t[i]);
      if (tm < 945 || tm >= 1290 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const double vw = VW[i], sd = SDV[i]; if (std::isnan(vw) || !(sd > 0)) continue;
      const int dir = sg;   // the cross direction: up-cross = BUY (needs rollover from BELOW), down-cross = SELL (from ABOVE)
      const double c = s.c[i], A = d.atr; double ext = dir > 0 ? 1e18 : -1e18;   // extreme of the last 10 chart bars (incl. this one)
      for (size_t q = k - 9; q <= k; ++q) ext = dir > 0 ? std::min(ext, cb[q].l) : std::max(ext, cb[q].h);
      // band touched in the last 10 bars, using the band at each bar's close
      auto touched = [&](double kk) { for (size_t q = k - 9; q <= k; ++q) { const size_t iq = cb[q].i1; if (s.day_of[iq] != di || std::isnan(VW[iq])) continue;
          const double band = VW[iq] - dir * kk * SDV[iq]; if (dir > 0 ? cb[q].l <= band : cb[q].h >= band) return true; } return false; };
      (void)touched;
      auto near = [&](double kk, double tol) { return std::fabs(c - (vw - dir * kk * sd)) <= tol; };
      const bool f[NC] = {dir * (c - vw) < 0, near(1, 0.25 * sd), near(1, 0.5 * sd), near(1, 0.1 * A), near(2, 0.25 * sd), near(2, 0.5 * sd), near(2, 0.1 * A)};
      bool any = false; for (bool b : f) any |= b; if (!any) continue; nSig[ti]++;
      const int w = tm < 990 ? 1 : tm < 1080 ? 2 : 3;
      const double stD = std::max(dir > 0 ? c - ext + 0.25 : ext - c + 0.25, 1.0);
      const int64_t tOpp = nextX[k] >= 0 ? s.t[cb[nextX[k]].i1] : INT64_MAX;
      for (int x = 0; x < NX; ++x) { int mins = 0; const double p = trade(i, dir, x, stD, 0, tOpp, mins); if (std::isnan(p)) continue;
        const double net = p / A * Anow - cost; const int64_t exitT = s.t[i] + (int64_t)std::max(mins, 1) * 60;
        // control once per (signal, exit)
        double tw = 0; int nt = 0; const double stA = stD / A, tgA = dir * (vw - c) / A;
        for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0) continue;
          const double A2 = s.days[od].atr; int m2; const double q = trade(j, dir, x, std::max(stA * A2, 1.0), x == 0 ? std::max(tgA * A2, 0.25) : 0, s.t[j] + (int64_t)mins * 60, m2);
          if (std::isnan(q)) continue; nt++; tw += q / A2 * Anow - cost; }
        for (int cc = 0; cc < NC; ++cc) { if (!f[cc]) continue; for (int ww : {0, w}) { if (s.t[i] < busy[cc][x][ww]) continue; busy[cc][x][ww] = exitT;
            Acc& a = acc[ti][cc][x][ww]; a.s += net; a.raw += p; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
            a.m[era(d.date / 10000)] += net; a.c[era(d.date / 10000)]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } }
  printf("EMA 7/21 cross faded off the VWAP SD bands — %s, NY 09:45-15:30, 2010-07 → 2026-10-23. Cost %.1f pts. Points scaled to today's ATR (%.0f).\n", sym.c_str(), cost, Anow);
  printf("signals (any condition): 1m %ld · 3m %ld · 5m %ld\n", nSig[0], nSig[1], nSig[2]);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 mean pts (n) · all: pts/trade net, raw pts, win %%, PF, trades/yr · twin = minus random other-day control · periods > 0.\n");
  for (int ti = 0; ti < NTF; ++ti) for (int x = 0; x < NX; ++x) { printf("\n######## %dm · %s\n", TFS[ti], XN[x]);
    for (int cc = 0; cc < NC; ++cc) for (int w = 0; w < NW; ++w) { const Acc& a = acc[ti][cc][x][w]; if (a.n < 30) continue; int pos = 0, have = 0;
      printf("  %-20s %-17s", CN[cc], WN[w]); for (int e = 0; e < 4; ++e) { if (a.c[e] < 15) printf("   —    (%4.0f) |", a.c[e]); else { printf(" %+6.2f (%4.0f) |", a.m[e] / a.c[e], a.c[e]); have++; pos += a.m[e] > 0; } }
      printf(" all %+6.2f raw %+6.2f win %2.0f%% PF %.2f %4.0f/yr", a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
      if (a.ny) printf(" · twin %+6.2f", a.tw / a.ny); printf(" · %d/%d\n", pos, have); } }
  return 0;
}
