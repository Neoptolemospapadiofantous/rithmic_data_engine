// 2026-10-29 fade2sd.cpp (founder: "test all sessions with 2SD fade. you are doing something wrong"). A clean re-test of the plain
// VWAP ± 2 SD fade (no EMA) that varies every place the earlier tests could have differed from the founder's chart. Fixed before the run:
//  ANCHOR  A0 VWAP from 09:30 carried overnight until the next 09:30 · A1 from 18:00 (the trading day) · A2 each session's own open
//          (Asia 18:00, London 03:00, NY 09:30). Typical price × volume of 1m bars, volume-weighted SD. Armed 30 min after the anchor.
//  ENTRY   E1 market at the close of the first 1m bar that closes beyond the 2 SD band · E2 resting LIMIT at the 2 SD band of the last
//          closed bar, filled only when price trades 1 tick through it · E3 market at the close of the first bar back inside after a
//          close beyond. Fresh signal only (the previous bar was not already beyond / through).
//  STOP    S1 the 3 SD band at entry (min 2 pts) · S2 0.1 × the daily ATR · S3 none (out at the session end).
//  TARGET  T1 VWAP (live, the last closed bar's) · T2 the 1 SD band on the same side (live).
//  SESSION Asia 18:00-03:00 · London 03:00-09:30 · NY 09:30-15:55; flat at the session end at the next bar's open; no entry in the
//          session's last 15 min. One position at a time per cell. Stop checked first when one bar touches both.
//  COST    E2 (limit in, limit target) NQ 0.5 / ES 0.25 pts; E1/E3 (market in) NQ 1.5 / ES 0.5. Raw (before cost) also shown.
//          Points scaled to today's ATR. Control = 5 random OTHER days, same minute / direction / stop + target distance in ATR.
//  PASS    n ≥ 150, net > 0 in 2010-15, 2016-20, 2021-24 AND 2025-26 (never tuned on), beats its control, same cell net > 0 on ES.
// Usage: fade2sd [NQ|ES]. tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NA = 3, NE = 3, NS = 3, NT = 2, NW = 3 };
static const char* AN[NA] = {"A0 09:30 carried", "A1 18:00", "A2 session open"};
static const char* EN[NE] = {"E1 close beyond", "E2 limit at band", "E3 close back in"};
static const char* SN[NS] = {"S1 3SD stop", "S2 0.1 ATR stop", "S3 no stop"};
static const char* TN[NT] = {"T1 VWAP", "T2 1SD"};
static const char* WN[NW] = {"Asia", "London", "NY"};
static const int W0[NW] = {0, 540, 930}, W1[NW] = {540, 930, 1315};
struct Acc { double s = 0, raw = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, ny = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const bool es = sym == "ES";
  const double costLim = es ? 0.25 : 0.5, costMkt = es ? 0.5 : 1.5, tick = 0.25;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  auto win = [](int tm) { return tm < 540 ? 0 : tm < 930 ? 1 : 2; };
  // simulate from bar j0 (first bar after entry) with entry price e, direction dir; stop distance stD (<=0 = none); target either live
  // (band of the last closed bar: VW - dir*k*SD with k = 0 or 1) or fixed distance fx (> 0). Exit at the window end.
  size_t xj = 0; double xp = 0;   // exit bar / price of the last sim() call (for the TRADES dump)
  auto sim = [&](const std::vector<float>& VW, const std::vector<float>& SD, size_t j0, double e, int dir, double stD, int tk, double fx, int wend) -> double {
    const int di = s.day_of[j0 - 1];
    for (size_t j = j0; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]); xj = j;
      if (tm >= wend) { xp = s.o[j]; return dir * (s.o[j] - e); }
      const double adv = dir > 0 ? e - s.l[j] : s.h[j] - e;
      if (stD > 0 && adv >= stD) { xp = e - dir * stD; return -stD; }
      double tgP;
      if (fx > 0) tgP = e + dir * fx; else { if (std::isnan(VW[j - 1])) continue; tgP = VW[j - 1] - dir * tk * SD[j - 1]; if (dir * (tgP - e) <= 0) { xp = s.o[j]; return dir * (s.o[j] - e); } }
      if (dir > 0 ? s.h[j] >= tgP : s.l[j] <= tgP) { xp = tgP; return dir * (tgP - e); } }
    return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  // TRADES=<file> CELL=a,e,s,t,w (indices) dumps that cell's trades: date,entry epoch,dir,entry,vwap,sd,stop,target@entry,exit epoch,exit,pts,atr
  FILE* tradesf = getenv("TRADES") ? fopen(getenv("TRADES"), "w") : nullptr; int dumpCell[5] = {0, 0, 2, 0, 1};
  if (getenv("CELL")) sscanf(getenv("CELL"), "%d,%d,%d,%d,%d", &dumpCell[0], &dumpCell[1], &dumpCell[2], &dumpCell[3], &dumpCell[4]);
  static Acc acc[NA][NE][NS][NT][NW]; std::mt19937 rng(20261029); long nSig = 0;
  for (int an = 0; an < NA; ++an) {
    std::vector<float> VW(s.size(), NAN), SD(s.size(), NAN); std::vector<int> since(s.size(), -1);
    { double pv = 0, pq = 0, vv = 0; int64_t t0 = 0; bool on = false; int prevTm = -1, prevD = -2;
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) { on = false; continue; } const int tm = tday_min(s.t[i]);
        bool reset = false;
        if (an == 0) reset = (tm >= 930 && (prevTm < 930 || di != prevD));
        else if (an == 1) reset = di != prevD;
        else reset = di != prevD || (tm >= 540 && prevTm < 540) || (tm >= 930 && prevTm < 930);
        prevTm = tm; prevD = di;
        if (reset) { pv = pq = vv = 0; t0 = s.t[i]; on = true; } if (!on) continue;
        const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
        VW[i] = (float)(pv / vv); SD[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); since[i] = (int)((s.t[i] - t0) / 60); } }
    static int64_t busy[NE][NS][NT][NW]; for (auto& a : busy) for (auto& b : a) for (auto& c : b) for (auto& x : c) x = 0;
    for (size_t i = 2; i + 1 < s.size(); ++i) {
      const int di = s.day_of[i]; if (di < 0 || s.day_of[i - 1] != di || s.day_of[i + 1] != di) continue; const Day& d = s.days[di];
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const int tm = tday_min(s.t[i]), w = win(tm); if (tm >= W1[w] - 15) continue;
      if (since[i] < 30 || since[i - 1] < 29 || !(SD[i] > 0) || !(SD[i - 1] > 0) || !(SD[i - 2] > 0) || std::isnan(VW[i - 2])) continue;
      const double A = d.atr;
      for (int dir : {1, -1}) {   // dir = trade direction: +1 BUY below the lower band, −1 SELL above the upper band
        auto band = [&](size_t k, double kk) { return VW[k] - dir * kk * SD[k]; };
        auto beyondC = [&](size_t k) { return dir * (s.c[k] - band(k, 2)) < 0; };
        double ent[NE]; bool sig[NE];
        sig[0] = beyondC(i) && !beyondC(i - 1); ent[0] = s.c[i];
        { const double lv = band(i - 1, 2), lvp = band(i - 2, 2);
          const bool thru = dir > 0 ? s.l[i] <= lv - tick : s.h[i] >= lv + tick, thruP = dir > 0 ? s.l[i - 1] <= lvp - tick : s.h[i - 1] >= lvp + tick;
          sig[1] = thru && !thruP; ent[1] = lv; }
        sig[2] = beyondC(i - 1) && !beyondC(i); ent[2] = s.c[i];
        for (int e = 0; e < NE; ++e) { if (!sig[e]) continue; nSig++;
          const double E = ent[e], cost = e == 1 ? costLim : costMkt;
          const double st[NS] = {std::max(2.0, dir * (E - band(i, 3))), 0.1 * A, 0};
          for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) {
            if (s.t[i] < busy[e][si][ti][w]) continue;
            const double p = sim(VW, SD, i + 1, E, dir, st[si], ti, 0, W1[w]); if (std::isnan(p)) continue;
            if (tradesf && an == dumpCell[0] && e == dumpCell[1] && si == dumpCell[2] && ti == dumpCell[3] && w == dumpCell[4] && s.t[i] >= busy[e][si][ti][w])
              fprintf(tradesf, "%d,%lld,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%lld,%.2f,%.2f,%.1f\n", d.date, (long long)s.t[i], dir, E, VW[i], SD[i], st[si] > 0 ? E - dir * st[si] : NAN,
                      VW[i] - dir * ti * SD[i], (long long)s.t[xj], xp, p, A);
            // exit time for one-position-at-a-time: re-run to find it cheaply = assume busy until the window end or exit; find by scanning
            int64_t tEnd = s.t[i];
            { const double tgD = dir * ((VW[i] - dir * ti * SD[i]) - E);   // control target distance (fixed at entry)
              double tw = 0; int nt = 0;
              for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0 || j + 1 >= (long)s.size()) continue;
                const double A2 = s.days[od].atr; const double q = sim(VW, SD, j + 1, s.c[j], dir, st[si] > 0 ? std::max(st[si] / A * A2, 1.0) : 0, ti, std::max(tgD / A * A2, 0.5), W1[w]);
                if (std::isnan(q)) continue; nt++; tw += q / A2 * Anow - cost; }
              // busy: scan forward for the exit bar (same logic, cheap)
              for (size_t j = i + 1; j < s.size() && s.day_of[j] == di; ++j) { const int t2 = tday_min(s.t[j]); tEnd = s.t[j]; if (t2 >= W1[w]) break;
                const double adv = dir > 0 ? E - s.l[j] : s.h[j] - E; if (st[si] > 0 && adv >= st[si]) break;
                const double tgP = VW[j - 1] - dir * ti * SD[j - 1]; if (dir * (tgP - E) <= 0 || (dir > 0 ? s.h[j] >= tgP : s.l[j] <= tgP)) break; }
              busy[e][si][ti][w] = tEnd;
              const double net = p / A * Anow - cost; Acc& a = acc[an][e][si][ti][w];
              a.s += net; a.raw += p / A * Anow; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
              const int er = era(d.date / 10000); a.m[er] += net; a.c[er]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } } }
  printf("VWAP 2 SD fade, no EMA — %s 1m, all sessions, 2010-07 → 2026-10-23. Cost E2 %.2f / E1,E3 %.2f pts. Points at today's ATR (%.0f). signals %ld\n", sym.c_str(), costLim, costMkt, Anow, nSig);
  printf("CSV: anchor,entry,stop,target,session,n,net,raw,win,pf,per_yr,e1015,e1620,e2124,e2526,twin\n");
  for (int an = 0; an < NA; ++an) for (int e = 0; e < NE; ++e) for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int w = 0; w < NW; ++w) {
    const Acc& a = acc[an][e][si][ti][w]; if (a.n < 1) continue;
    printf("%s,%s,%s,%s,%s,%.0f,%.3f,%.3f,%.1f,%.2f,%.0f", AN[an], EN[e], SN[si], TN[ti], WN[w], a.n, a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
    for (int k = 0; k < 4; ++k) printf(",%s", a.c[k] >= 15 ? std::to_string(a.m[k] / a.c[k]).c_str() : "");
    printf(",%.3f\n", a.ny ? a.tw / a.ny : NAN); }
  if (tradesf) fclose(tradesf);
  return 0;
}
