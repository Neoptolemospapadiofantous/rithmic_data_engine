// 2026-10-26 ema721_ms_vwap.cpp (founder, third pass of the 7/21 + VWAP SD idea, after ema721_vwap_sd.cpp was the wrong reading:
// "create market structure and when we break the structure and ema crosses we go", fade, "24h, VWAP from 18:00", "test profit locks
// to see which one is better", "check 1m"). Fixed before the run:
//  BANDS   VWAP anchored 18:00 ET (the trading day), typical price × volume of the 1m bars, ± k × the volume-weighted SD. k = 1, 2;
//          k = 0 baseline = the same setup off VWAP itself (does the band add anything?).
//  SETUP   (SELL; BUY is the mirror) a 1m high reaches the upper band (within 0.25 SD of it, or through it) → armed. top = the highest
//          high since arming; structure = the last CONFIRMED swing low at the time the top was made (the higher low). Swings = fractal
//          pivots, N bars each side (N = 3 and 5), confirmed only N bars later (no look-ahead). A new high resets top + structure.
//          Disarmed 60 min after the last band touch.
//  ENTRY   a 1m close below the structure low AND EMA7 < EMA21 with the down-cross made after the top (either order) → sell at that
//          close. Stop = top + 1 tick (min 2 pts). R = stop distance. Entries 18:15-16:30 ET (VWAP needs 15 min), flat 16:50.
//  LOCKS   P0 target 2R, no lock · P1 break-even at +1R, target 2R · P2 BE at +1R, target 3R · P3 lock +0.5R at +1R, target 2R ·
//          P4 after +1R trail 1R behind the best price, no target · P5 after +1R trail 0.5R behind · P6 step lock (each +1R moves the
//          stop to +(n−1)R), no target · P7 target VWAP, no lock · P8 target VWAP, BE at +1R. Per 1m bar: stop checked first (with the
//          stop in force at the bar's open), then target, then the lock updates from that bar's extreme for the next bar.
//  WAVE    (founder, 4th: "test also when we pass the SD level but close then reverse with the ema cross over like a wave. we wanna sell
//          top and buy bottom but with the ema confirmation") S2: a 1m bar trades THROUGH the band but closes back inside → armed
//          (top = the highest high of such bars); a close beyond the band disarms until the next close-back; sell when EMA7 crosses
//          below EMA21 after the first close-back bar (enter at that cross close, stop = top + 1 tick). No structure needed.
//          top = the extreme of the WHOLE excursion past the band (fixed 2026-10-26 after the founder's chart: v1 restarted it at
//          every close beyond the band, so stops sat inside the wave — e.g. 10:18 buy stop 29,068.75 vs the 29,024 low).
//          S3 = S2 + the structure break (a close through the last swing low at the top) as well. S1 = the structure setup above.
// One position at a time per cell. Cost NQ 1.5 / ES 0.5 pts per round trip. Points scaled to today's ATR (pnl / ATR that day × the
// latest ATR), cost at today's size. CONTROL: 5 random OTHER days, same minute, same direction, same stop in ATR, same lock rule (VWAP
// target = that trade's distance to VWAP at entry) — twin = trade minus control. Windows by entry: Asia 18:00-03:00 · London
// 03:00-09:30 · NY 09:30-16:30. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26. Usage: ema721_ms_vwap [NQ|ES].
// tday_min = minutes since 18:00 ET: 18:15 = 15, 03:00 = 540, 09:30 = 930, 16:30 = 1350, 16:50 = 1370.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NS = 3, NN = 2, NK = 3, NP = 9, NW = 4 };
static const char* SN[NS] = {"S1 structure+EMA", "S2 pierce+close back, EMA", "S3 pierce+close back, EMA+structure"};
static const int PIV[NN] = {3, 5};
static const char* KN[NK] = {"k=0 VWAP", "1 SD", "2 SD"};
static const char* PN[NP] = {"P0 2R, no lock", "P1 BE@1R, 2R", "P2 BE@1R, 3R", "P3 +0.5R@1R, 2R", "P4 trail 1R after 1R", "P5 trail 0.5R after 1R",
                             "P6 step lock 1R", "P7 target VWAP", "P8 VWAP, BE@1R"};
static const char* WN[NW] = {"all 24h", "Asia", "London", "NY"};
struct Acc { double s = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, ny = 0, raw = 0, hold = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  const int FLAT = 1370;
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue;
      if (di != cur) { cur = di; pv = pq = vv = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  // one trade from entry bar ie at its close, direction dir, stop distance R, lock rule p. fixedTg > 0 = a fixed VWAP-distance target.
  auto trade = [&](size_t ie, int dir, double R, int p, double fixedTg, int& minsOut) -> double {
    const double e = s.c[ie]; const int di = s.day_of[ie]; const int64_t t0 = s.t[ie];
    double stop = -R, best = 0;   // stop as a signed offset from entry in the trade's favour (−R = original stop)
    const double tgR = p == 0 || p == 1 || p == 3 ? 2 : p == 2 ? 3 : 0;
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      if (tm >= FLAT) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.o[j] - e); }
      const double adv = dir > 0 ? s.l[j] - e : e - s.h[j], fav = dir > 0 ? s.h[j] - e : e - s.l[j];   // adv <= 0 usually
      if (adv <= stop) { minsOut = (int)((s.t[j] - t0) / 60); return std::min(stop, dir * (s.o[j] - e)); }   // gap through = the open
      double tg = tgR * R;
      if (p >= 7) { if (fixedTg > 0) tg = fixedTg; else { const double vw = VW[j - 1]; if (std::isnan(vw)) continue; tg = dir * (vw - e);
          if (tg <= 0) { minsOut = (int)((s.t[j] - t0) / 60); return dir * (s.o[j] - e); } } }
      if (tg > 0 && fav >= tg) { minsOut = (int)((s.t[j] - t0) / 60); return tg; }
      best = std::max(best, fav);
      if (best >= R) { if (p == 1 || p == 2 || p == 8) stop = std::max(stop, 0.0); else if (p == 3) stop = std::max(stop, 0.5 * R);
        else if (p == 4) stop = std::max(stop, best - R); else if (p == 5) stop = std::max(stop, best - 0.5 * R);
        else if (p == 6) stop = std::max(stop, (std::floor(best / R) - 1) * R); } }
    minsOut = 0; return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  static Acc acc[NS][NN][NK][NP][NW]; static int64_t busy[NS][NN][NK][NP][NW]; std::mt19937 rng(20261026); long nSig[NS][NN][NK] = {};
  // EMAs (continuous) + last cross bars
  std::vector<int8_t> esg(s.size()); { double e7 = s.c[0], e21 = s.c[0]; for (size_t i = 0; i < s.size(); ++i) { e7 += 2.0 / 8 * (s.c[i] - e7); e21 += 2.0 / 22 * (s.c[i] - e21); esg[i] = e7 > e21 ? 1 : -1; } }
  for (int sm = 0; sm < NS; ++sm) for (int ni = 0; ni < NN; ++ni) { const int N = PIV[ni]; if (sm == 1 && ni > 0) continue;   // S2 has no pivots
    struct Arm { bool on = false; double top = 0, level = NAN; size_t topBar = 0, lastTouch = 0, armBar = 0; bool ex = false; double exTop = 0; size_t exLast = 0; };
    Arm arm[NK][2];   // [k][0 = sell, 1 = buy]
    double swL = NAN, swH = NAN; int curDay = -1; size_t lastXdn = 0, lastXup = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      const int di = s.day_of[i]; if (di < 0) continue;
      if (di != curDay) { curDay = di; for (auto& a : arm) { a[0].on = a[1].on = false; } }
      if (i > 0 && esg[i] != esg[i - 1]) (esg[i] < 0 ? lastXdn : lastXup) = i;
      // confirm a pivot at i − N (same trading day only)
      if (i >= (size_t)(2 * N) && s.day_of[i - 2 * N] == di) { const size_t p = i - N; bool lo = true, hi = true;
        for (size_t q = p - N; q <= p + N; ++q) { if (q == p) continue; if (s.l[q] < s.l[p] || (q < p && s.l[q] == s.l[p])) lo = false; if (s.h[q] > s.h[p] || (q < p && s.h[q] == s.h[p])) hi = false; }
        if (lo) swL = s.l[p]; if (hi) swH = s.h[p]; }
      const Day& d = s.days[di]; const int tm = tday_min(s.t[i]); const double vw = VW[i], sd = SDV[i], c = s.c[i];
      if (std::isnan(vw) || !(sd > 0) || tm < 15 || tm >= 1350) continue;
      for (int k = 0; k < NK; ++k) for (int side = 0; side < 2; ++side) { const int dir = side == 0 ? -1 : 1; Arm& a = arm[k][side];
        const double band = vw - dir * k * sd; const double ext = side == 0 ? s.h[i] : s.l[i];
        bool brk, ema;
        if (sm == 0) {
          const bool touch = k == 0 ? -dir * (ext - band) >= 0 : -dir * (ext - band) >= -0.25 * sd;
          if (touch) { if (!a.on || -dir * (ext - a.top) > 0) { a.top = ext; a.topBar = i; a.level = side == 0 ? swL : swH; } a.on = true; a.lastTouch = i; continue; }
          if (!a.on) continue; if (i - a.lastTouch > 60) { a.on = false; continue; }
          if (std::isnan(a.level) || dir * (a.level - a.top) <= 0) continue;   // structure must sit on the trade's side of the top
          brk = dir * (c - a.level) > 0; ema = esg[i] == dir && (dir < 0 ? lastXdn : lastXup) > a.topBar;
        } else {
          // pierce: the bar trades through the band; closes back inside → arm / extend, closes beyond → disarm
          // the wave's extreme = the most extreme price of the whole excursion past the band (kept across closes beyond it)
          if (a.ex && i - a.exLast > 60) a.ex = false;
          if (-dir * (ext - band) > 0) { if (!a.ex || -dir * (ext - a.exTop) > 0) { a.exTop = ext; a.level = side == 0 ? swL : swH; } a.ex = true; a.exLast = i;
            if (-dir * (c - band) >= 0) { a.on = false; continue; }
            if (!a.on) { a.on = true; a.armBar = i; } a.top = a.exTop; a.lastTouch = i; }
          if (!a.on) continue; if (i - a.lastTouch > 60) { a.on = false; continue; }
          ema = esg[i] == dir && (dir < 0 ? lastXdn : lastXup) > a.armBar && (dir < 0 ? lastXdn : lastXup) == i;   // the cross itself, after the close-back
          brk = true;
          if (sm == 2) { if (std::isnan(a.level) || dir * (a.level - a.top) <= 0) continue; brk = dir * (c - a.level) > 0;
            ema = esg[i] == dir && (dir < 0 ? lastXdn : lastXup) > a.armBar; }
        }
        if (!brk || !ema) continue;
        a.on = false; a.ex = false; if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue; nSig[sm][ni][k]++;
        const double A = d.atr, R = std::max(-dir * (a.top - c) + 0.25, 2.0); const int w = tm < 540 ? 1 : tm < 930 ? 2 : 3; const int ws[2] = {0, w};
        for (int p = 0; p < NP; ++p) { int mins = 0; const double pts = trade(i, dir, R, p, 0, mins); if (std::isnan(pts)) continue;
          const double net = pts / A * Anow - cost; const int64_t exitT = s.t[i] + (int64_t)std::max(mins, 1) * 60;
          bool any = false; for (int ww : ws) any |= s.t[i] >= busy[sm][ni][k][p][ww]; if (!any) continue;
          double tw = 0; int nt = 0; const double rA = R / A, tgA = dir * (vw - c) / A;
          for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0) continue;
            const double A2 = s.days[od].atr; int m2; const double q = trade(j, dir, std::max(rA * A2, 2.0), p, p >= 7 ? std::max(tgA * A2, 0.25) : 0, m2);
            if (std::isnan(q)) continue; nt++; tw += q / A2 * Anow - cost; }
          for (int ww : ws) { if (s.t[i] < busy[sm][ni][k][p][ww]) continue; busy[sm][ni][k][p][ww] = exitT; Acc& x = acc[sm][ni][k][p][ww];
            x.s += net; x.raw += pts; x.n++; x.w += net > 0; (net > 0 ? x.gw : x.gl) += std::fabs(net); x.hold += mins;
            x.m[era(d.date / 10000)] += net; x.c[era(d.date / 10000)]++; if (nt) { x.tw += net - tw / nt; x.ny++; } } } } } }
  printf("1m: fade off the 18:00-anchored VWAP SD bands on a structure break + EMA 7/21 cross — %s, 2010-07 → 2026-10-23, 24h. Cost %.1f pts. Points scaled to today's ATR (%.0f).\n", sym.c_str(), cost, Anow);
  for (int sm = 0; sm < NS; ++sm) for (int ni = 0; ni < NN; ++ni) if (sm != 1 || ni == 0) printf("signals %s, pivots %d: k=0 %ld · 1 SD %ld · 2 SD %ld\n", SN[sm], PIV[ni], nSig[sm][ni][0], nSig[sm][ni][1], nSig[sm][ni][2]);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 mean pts (n) · all: net pts/trade, raw pts, win %%, PF, trades/yr, avg minutes held · twin = minus random other-day control · periods > 0.\n");
  for (int sm = 0; sm < NS; ++sm) for (int ni = 0; ni < NN; ++ni) for (int k = 0; k < NK; ++k) { if (sm == 1 && ni > 0) continue;
    printf("\n######## %s · pivots %d · %s\n", SN[sm], PIV[ni], KN[k]);
    for (int w = 0; w < NW; ++w) for (int p = 0; p < NP; ++p) { const Acc& a = acc[sm][ni][k][p][w]; if (a.n < 30) continue; int pos = 0, have = 0;
      printf("  %-8s %-22s", WN[w], PN[p]); for (int e = 0; e < 4; ++e) { if (a.c[e] < 15) printf("   —    (%4.0f) |", a.c[e]); else { printf(" %+6.2f (%4.0f) |", a.m[e] / a.c[e], a.c[e]); have++; pos += a.m[e] > 0; } }
      printf(" all %+6.2f raw %+6.2f win %2.0f%% PF %.2f %4.0f/yr %3.0fm", a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3, a.hold / a.n);
      if (a.ny) printf(" · twin %+6.2f", a.tw / a.ny); printf(" · %d/%d\n", pos, have); } }
  return 0;
}
