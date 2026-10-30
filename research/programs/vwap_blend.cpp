// 2026-10-29 vwap_blend.cpp (founder: "vwap is the structure with SD and the ema can show direction but how can I blend everything" →
// "there has to be a way"). ONE rulebook: VWAP decides the day type, the SD bands decide the location, the 1m EMA 7/9 times the entry.
// Every threshold fixed before the run, no tuning after:
//  REGIME  (re-read every closed 1m bar) slope = (VWAP now − VWAP 30 min ago) / SD now; share = closes above VWAP in the last 30 bars.
//          UP: slope > 0.3 and share ≥ 80 % · DOWN: slope < −0.3 and share ≤ 20 % · ROTATION: |slope| < 0.15 · else no trade.
//  TREND   (UP buys only, DOWN sells only) arm: a close between VWAP and the ±1 SD band on the trend side AND below/above EMA 9 (a
//          pullback); the arm drops when a bar closes through VWAP or the regime stops being UP/DOWN. Entry ≤ 15 min after the last
//          pullback bar on X1 EMA 7/9 cross back / X2 close back over EMA 9, regime still UP/DOWN, close still on the VWAP side.
//          Stops S1 the pullback extreme ∓ 1 tick (min 2 pts) · S2 0.5 SD through VWAP. Targets T1 the 2 SD band (live) · T2 none · T3 2R.
//  ROTATE  (ROTATION only) arm: a close beyond ±2 SD while the regime is ROTATION; drops if the regime turns UP/DOWN. Entry ≤ 15 min
//          later on the EMA turn back toward VWAP after a close back inside 2 SD. Stops S1 the stretch extreme ± 1 tick · S2 the 3 SD band.
//          Targets T1 VWAP (live) · T2 none · T3 2R.
//  EXITS   L0 as above · L1 stop to break-even once 1R up (closed bars). A bar opening through the stop fills at the open. Flat at the
//          session end. One position at a time per cell and setup. Sessions Asia 18:00-03:00 / London 03:00-09:30 / NY 09:30-15:55,
//          anchor A0 09:30 carried or A2 the session's own open (identical in NY). Armed 45 min after the anchor. No entry in the last 15 min.
//  COST    NQ 1.5 / ES 0.5 pts per round trip, points at today's ATR. Control = 5 random OTHER days, same minute / side / stop + target.
//  PASS    n ≥ 150, net > 0 in 2010-15, 2016-20, 2021-24 AND 2025-26, beats its control, same cell net > 0 on ES. Sums are printed so
//          TREND + ROTATE can be added into one book.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NA = 2, NP = 2, NX = 2, NS = 2, NT = 3, NL = 2, NW = 3 };
static const char* AN[NA] = {"A0 09:30 carried", "A2 session open"};
static const char* PN[NP] = {"TREND pullback", "ROTATE fade 2SD"};
static const char* XN[NX] = {"X1 EMA cross", "X2 close over EMA9"};
static const char* SN[NS] = {"S1 swing", "S2 structure"};
static const char* TN[NT] = {"T1 band", "T2 none", "T3 2R"};
static const char* LN[NL] = {"L0", "L1 BE 1R"};
static const char* WN[NW] = {"Asia", "London", "NY"};
static const int W1[NW] = {540, 930, 1315};
struct Acc { double s = 0, raw = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, ny = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5, tick = 0.25;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  auto win = [](int tm) { return tm < 540 ? 0 : tm < 930 ? 1 : 2; };
  std::vector<float> E7(s.size()), E9(s.size());
  for (size_t i = 0; i < s.size(); ++i) { const bool rs = i == 0 || s.t[i] - s.t[i - 1] > 1800;
    E7[i] = rs ? s.c[i] : E7[i - 1] + (s.c[i] - E7[i - 1]) * (2.0 / 8); E9[i] = rs ? s.c[i] : E9[i - 1] + (s.c[i] - E9[i - 1]) * (2.0 / 10); }
  size_t xj = 0; double xp = 0;
  // tk: 0 live band (VW + bk·SD), 1 none; fx > 0 fixed target distance; be = break-even at 1R
  auto sim = [&](const std::vector<float>& VW, const std::vector<float>& SD, size_t j0, double e, int dir, double stD, int tk, double bk, double fx, bool be, int wend) -> double {
    const int di = s.day_of[j0 - 1]; double stP = e - dir * stD, best = e;
    for (size_t j = j0; j < s.size() && s.day_of[j] == di; ++j) { xj = j;
      if (tday_min(s.t[j]) >= wend || dir * (s.o[j] - stP) <= 0) { xp = s.o[j]; return dir * (s.o[j] - e); }
      if (dir > 0 ? s.l[j] <= stP : s.h[j] >= stP) { xp = stP; return dir * (stP - e); }
      double tgP = NAN;
      if (fx > 0) tgP = e + dir * fx; else if (tk == 0 && !std::isnan(VW[j - 1])) { tgP = VW[j - 1] + bk * SD[j - 1]; if (dir * (tgP - e) <= 0) { xp = s.o[j]; return dir * (s.o[j] - e); } }
      if (!std::isnan(tgP) && (dir > 0 ? s.h[j] >= tgP : s.l[j] <= tgP)) { xp = tgP; return dir * (tgP - e); }
      best = dir > 0 ? std::max(best, (double)s.h[j]) : std::min(best, (double)s.l[j]);
      if (be && dir * (best - e) >= stD && dir * (e - stP) > 0) stP = e; }
    return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  FILE* tradesf = getenv("TRADES") ? fopen(getenv("TRADES"), "w") : nullptr; int dc[7] = {1, 0, 0, 0, 0, 0, 2};
  if (getenv("CELL")) sscanf(getenv("CELL"), "%d,%d,%d,%d,%d,%d,%d", &dc[0], &dc[1], &dc[2], &dc[3], &dc[4], &dc[5], &dc[6]);
  static Acc acc[NA][NP][NX][NS][NT][NL][NW]; std::mt19937 rng(20261029); long nSig = 0, regN[4] = {0, 0, 0, 0};
  for (int an = 0; an < NA; ++an) {
    std::vector<float> VW(s.size(), NAN), SD(s.size(), NAN); std::vector<int> since(s.size(), -1);
    { double pv = 0, pq = 0, vv = 0; int64_t t0 = 0; bool on = false; int prevTm = -1, prevD = -2;
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) { on = false; continue; } const int tm = tday_min(s.t[i]);
        const bool reset = an == 0 ? (tm >= 930 && (prevTm < 930 || di != prevD)) : (di != prevD || (tm >= 540 && prevTm < 540) || (tm >= 930 && prevTm < 930));
        prevTm = tm; prevD = di;
        if (reset) { pv = pq = vv = 0; t0 = s.t[i]; on = true; } if (!on) continue;
        const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
        VW[i] = (float)(pv / vv); SD[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); since[i] = (int)((s.t[i] - t0) / 60); } }
    static int64_t busy[NP][NX][NS][NT][NL][NW]; memset(busy, 0, sizeof busy);
    int64_t LBa[NP][2] = {}; double EXa[NP][2] = {}; bool used[NP][NX][2] = {}; int lastW = -1, lastD = -1;
    for (size_t i = 31; i + 1 < s.size(); ++i) {
      const int di = s.day_of[i]; if (di < 0 || s.day_of[i - 1] != di || s.day_of[i + 1] != di) continue; const Day& d = s.days[di];
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const int tm = tday_min(s.t[i]), w = win(tm);
      if (lastW != w || lastD != di) { memset(LBa, 0, sizeof LBa); memset(used, 0, sizeof used); lastW = w; lastD = di; }
      if (since[i] < 45 || !(SD[i] > 0) || std::isnan(VW[i]) || s.day_of[i - 30] != di || since[i - 30] < 0 || since[i - 30] > since[i]) continue;
      const double slope = (VW[i] - VW[i - 30]) / SD[i]; int above = 0; for (int q = 0; q < 30; ++q) above += s.c[i - q] > VW[i - q];
      const int reg = slope > 0.3 && above >= 24 ? 1 : slope < -0.3 && above <= 6 ? -1 : std::fabs(slope) < 0.15 ? 2 : 0;   // 1 up, −1 down, 2 rotation
      if (an == 1) regN[reg == -1 ? 3 : reg]++;
      const double A = d.atr;
      for (int p = 0; p < NP; ++p) for (int k = 0; k < 2; ++k) { const int dir = k == 0 ? 1 : -1;
        int64_t& LB = LBa[p][k]; double& EX = EXa[p][k];
        bool okReg, stretched;
        if (p == 0) { okReg = reg == dir && dir * (s.c[i] - VW[i]) > 0; stretched = okReg && dir * (s.c[i] - (VW[i] + dir * SD[i])) <= 0 && dir * (s.c[i] - E9[i]) < 0; }
        else { okReg = reg == 2 || (LB && reg == 0); stretched = reg == 2 && dir * (s.c[i] - (VW[i] - dir * 2 * SD[i])) < 0; }
        if (!okReg) { LB = 0; continue; }
        if (LB) EX = dir > 0 ? std::min(EX, (double)s.l[i]) : std::max(EX, (double)s.h[i]);
        if (stretched) { if (!LB) EX = dir > 0 ? s.l[i] : s.h[i]; LB = s.t[i]; used[p][0][k] = used[p][1][k] = false; continue; }
        if (!LB || tm >= W1[w] - 15 || (s.t[i] - LB) / 60 > 15) continue;
        if (p == 1 && dir * (s.c[i] - (VW[i] - dir * 2 * SD[i])) < 0) continue;
        const bool sx[NX] = {dir * (E7[i] - E9[i]) > 0 && dir * (E7[i - 1] - E9[i - 1]) <= 0, dir * (s.c[i] - E9[i]) > 0 && dir * (s.c[i - 1] - E9[i - 1]) <= 0};
        const double E = s.c[i];
        for (int x = 0; x < NX; ++x) { if (!sx[x] || used[p][x][k]) continue; used[p][x][k] = true; nSig++;
          const double st[NS] = {std::max(2.0, dir * (E - EX) + tick),
                                 std::max(2.0, p == 0 ? dir * (E - (VW[i] - dir * 0.5 * SD[i])) : dir * (E - (VW[i] - dir * 3 * SD[i])))};
          const double bk = p == 0 ? dir * 2.0 : 0.0;   // natural target: trend → the 2 SD band ahead, rotation → VWAP
          for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int L = 0; L < NL; ++L) {
            if (s.t[i] < busy[p][x][si][ti][L][w]) continue;
            const double fx = ti == 2 ? 2 * st[si] : 0;
            const double r = sim(VW, SD, i + 1, E, dir, st[si], ti == 0 ? 0 : 1, bk, fx, L == 1, W1[w]); if (std::isnan(r)) continue;
            busy[p][x][si][ti][L][w] = s.t[xj];
            if (tradesf && an == dc[0] && p == dc[1] && x == dc[2] && si == dc[3] && ti == dc[4] && L == dc[5] && w == dc[6])
              fprintf(tradesf, "%d,%lld,%d,%.2f,%.2f,%.2f,%.2f,%lld,%.2f,%.2f,%.1f,%lld,%d,%.2f\n", d.date, (long long)s.t[i], dir, E, VW[i], SD[i], E - dir * st[si],
                      (long long)s.t[xj], xp, r, A, (long long)LB, p, slope);
            const double tgD = ti == 2 ? fx : ti == 0 ? std::max(0.0, dir * (VW[i] + bk * SD[i] - E)) : 0;
            double tw = 0; int nt = 0;
            for (int q = 0; q < 10 && nt < 5; ++q) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0 || j + 1 >= (long)s.size()) continue;
              const double A2 = s.days[od].atr; const double v = sim(VW, SD, j + 1, s.c[j], dir, std::max(st[si] / A * A2, 1.0), 1, 0, tgD > 0 ? std::max(tgD / A * A2, 0.5) : 0, L == 1, W1[w]);
              if (std::isnan(v)) continue; nt++; tw += v / A2 * Anow - cost; }
            const double net = r / A * Anow - cost; Acc& a = acc[an][p][x][si][ti][L][w];
            a.s += net; a.raw += r / A * Anow; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
            const int er = era(d.date / 10000); a.m[er] += net; a.c[er]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } }
  const double rt = regN[0] + regN[1] + regN[2] + regN[3];
  printf("VWAP blend (regime from VWAP, location from SD, trigger EMA 7/9) — %s 1m, 2010-07 → 2026-10-23. Cost %.2f. ATR %.0f. signals %ld. "
         "bars: up %.0f%% down %.0f%% rotation %.0f%% none %.0f%%\n", sym.c_str(), cost, Anow, nSig, 100 * regN[1] / rt, 100 * regN[3] / rt, 100 * regN[2] / rt, 100 * regN[0] / rt);
  printf("CSV: anchor,setup,trigger,stop,target,exit,session,n,net,raw,win,pf,per_yr,e1015,e1620,e2124,e2526,twin,sum,c1015,c1620,c2124,c2526,s1015,s1620,s2124,s2526\n");
  for (int an = 0; an < NA; ++an) for (int p = 0; p < NP; ++p) for (int x = 0; x < NX; ++x) for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int L = 0; L < NL; ++L) for (int w = 0; w < NW; ++w) {
    const Acc& a = acc[an][p][x][si][ti][L][w]; if (a.n < 1) continue;
    printf("%s,%s,%s,%s,%s,%s,%s,%.0f,%.3f,%.3f,%.1f,%.2f,%.0f", AN[an], PN[p], XN[x], SN[si], TN[ti], LN[L], WN[w], a.n, a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
    for (int k = 0; k < 4; ++k) printf(",%s", a.c[k] >= 15 ? std::to_string(a.m[k] / a.c[k]).c_str() : "");
    printf(",%.3f,%.2f", a.ny ? a.tw / a.ny : NAN, a.s);
    for (int k = 0; k < 4; ++k) printf(",%.0f", a.c[k]);
    for (int k = 0; k < 4; ++k) printf(",%.2f", a.m[k]);
    printf("\n"); }
  if (tradesf) fclose(tradesf);
  return 0;
}
