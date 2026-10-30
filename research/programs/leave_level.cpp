// 2026-10-29 leave_level.cpp (founder: "we need to be enter after leaving a level"). Price comes TO a VWAP level, then LEAVES it; enter
// on the leave, in the direction it leaves, only when the 1m EMA 7/9 points the same way. Fixed before the run:
//  LEVELS  VWAP − 2 SD, − 1 SD, VWAP, + 1 SD, + 2 SD (VWAP from typical price × volume, volume-weighted SD). Each is its own cell.
//  AT      a bar is AT the level when its range comes within 0.1 SD of it (low ≤ L + 0.1 SD and high ≥ L − 0.1 SD).
//  LEAVE   the first close ≥ K SD beyond the level (K = 0.25 or 0.5), up for a BUY / down for a SELL, within 10 bars of the last bar AT
//          the level, with EMA 7 > EMA 9 for a BUY (< for a SELL). Market at that close. One entry per visit (a new AT bar re-arms).
//  FILTER  F0 none · F1 VWAP sloping the trade's way (VWAP now − 30 min ago > +0.1 SD for a BUY, < −0.1 SD for a SELL).
//  STOP    S1 the far extreme while at the level ∓ 1 tick (min 2 pts) · S2 the level ∓ 0.25 SD.
//  TARGET  T1 the next level in the trade's direction (live; +2 SD → +3 SD) · T2 2R · T3 none (session end), stop to break-even at 1R.
//  ANCHOR  A0 VWAP from 09:30 carried overnight · A2 each session's own open (Asia 18:00, London 03:00, NY 09:30). Armed 30 min after.
//  SESSION Asia 18:00-03:00 · London 03:00-09:30 · NY 09:30-15:55, flat at the end, no entry in the last 15 min. One position per cell.
//  COST    NQ 1.5 / ES 0.5 pts, points at today's ATR. Control = 5 random OTHER days, same minute / side / stop + target distance.
//  PASS    n ≥ 150, net > 0 in 2010-15, 2016-20, 2021-24 AND 2025-26, beats its control, same cell net > 0 on ES.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NA = 2, NV = 5, NK = 2, NF = 2, NS = 2, NT = 3, NW = 3 };
static const char* AN[NA] = {"A0 09:30 carried", "A2 session open"};
static const int LV[NV] = {-2, -1, 0, 1, 2};
static const char* VN[NV] = {"-2SD", "-1SD", "VWAP", "+1SD", "+2SD"};
static const double KK[NK] = {0.25, 0.5};
static const char* FN[NF] = {"F0 none", "F1 VWAP slope agrees"};
static const char* SN[NS] = {"S1 extreme at level", "S2 level 0.25SD"};
static const char* TN[NT] = {"T1 next level", "T2 2R", "T3 none BE1R"};
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
  // tk 0: live band VW + bk·SD; 1: none. fx > 0 fixed target. be: break-even at 1R
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
  FILE* tradesf = getenv("TRADES") ? fopen(getenv("TRADES"), "w") : nullptr; int dc[7] = {1, 2, 0, 0, 0, 0, 2};
  if (getenv("CELL")) sscanf(getenv("CELL"), "%d,%d,%d,%d,%d,%d,%d", &dc[0], &dc[1], &dc[2], &dc[3], &dc[4], &dc[5], &dc[6]);
  static Acc acc[NA][NV][NK][NF][NS][NT][NW]; std::mt19937 rng(20261029); long nSig = 0;
  for (int an = 0; an < NA; ++an) {
    std::vector<float> VW(s.size(), NAN), SD(s.size(), NAN); std::vector<int> since(s.size(), -1);
    { double pv = 0, pq = 0, vv = 0; int64_t t0 = 0; bool on = false; int prevTm = -1, prevD = -2;
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) { on = false; continue; } const int tm = tday_min(s.t[i]);
        const bool reset = an == 0 ? (tm >= 930 && (prevTm < 930 || di != prevD)) : (di != prevD || (tm >= 540 && prevTm < 540) || (tm >= 930 && prevTm < 930));
        prevTm = tm; prevD = di;
        if (reset) { pv = pq = vv = 0; t0 = s.t[i]; on = true; } if (!on) continue;
        const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
        VW[i] = (float)(pv / vv); SD[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); since[i] = (int)((s.t[i] - t0) / 60); } }
    static int64_t busy[NV][NK][NF][NS][NT][NW]; memset(busy, 0, sizeof busy);
    size_t lastAt[NV] = {}; double lo[NV] = {}, hi[NV] = {}; bool done[NV][NK][2] = {}; int lastW = -1, lastD = -1;
    for (size_t i = 31; i + 1 < s.size(); ++i) {
      const int di = s.day_of[i]; if (di < 0 || s.day_of[i - 1] != di || s.day_of[i + 1] != di) continue; const Day& d = s.days[di];
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const int tm = tday_min(s.t[i]), w = win(tm);
      if (lastW != w || lastD != di) { memset(lastAt, 0, sizeof lastAt); lastW = w; lastD = di; }
      if (since[i] < 30 || !(SD[i] > 0) || std::isnan(VW[i])) continue;
      const bool slopeOk = s.day_of[i - 30] == di && since[i - 30] >= 0 && since[i - 30] < since[i];
      const double slope = slopeOk ? (VW[i] - VW[i - 30]) / SD[i] : 0; const double A = d.atr;
      for (int v = 0; v < NV; ++v) {
        const double L = VW[i] + LV[v] * SD[i], tol = 0.1 * SD[i];
        if (s.l[i] <= L + tol && s.h[i] >= L - tol) {   // AT the level: (re)start the visit
          if (!lastAt[v] || i - lastAt[v] > 10) { lo[v] = s.l[i]; hi[v] = s.h[i]; } else { lo[v] = std::min(lo[v], (double)s.l[i]); hi[v] = std::max(hi[v], (double)s.h[i]); }
          lastAt[v] = i; memset(done[v], 0, sizeof done[v]); continue; }
        if (!lastAt[v] || i - lastAt[v] > 10 || tm >= W1[w] - 15) continue;
        for (int k = 0; k < NK; ++k) for (int q = 0; q < 2; ++q) { const int dir = q == 0 ? 1 : -1;
          if (done[v][k][q]) continue;
          const double trig = L + dir * KK[k] * SD[i];
          if (!(dir * (s.c[i] - trig) >= 0 && dir * (s.c[i - 1] - (VW[i - 1] + LV[v] * SD[i - 1] + dir * KK[k] * SD[i - 1])) < 0)) continue;
          done[v][k][q] = true;
          if (dir * (E7[i] - E9[i]) <= 0) continue;
          const double E = s.c[i]; nSig++;
          const double st[NS] = {std::max(2.0, dir * (E - (dir > 0 ? lo[v] : hi[v])) + tick), std::max(2.0, dir * (E - (L - dir * 0.25 * SD[i])))};
          const double bk = LV[v] + dir;
          for (int f = 0; f < NF; ++f) { if (f == 1 && !(slopeOk && dir * slope > 0.1)) continue;
            for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) {
              if (s.t[i] < busy[v][k][f][si][ti][w]) continue;
              const double fx = ti == 1 ? 2 * st[si] : 0;
              const double r = sim(VW, SD, i + 1, E, dir, st[si], ti == 0 ? 0 : 1, bk, fx, ti == 2, W1[w]); if (std::isnan(r)) continue;
              busy[v][k][f][si][ti][w] = s.t[xj];
              if (tradesf && an == dc[0] && v == dc[1] && k == dc[2] && f == dc[3] && si == dc[4] && ti == dc[5] && w == dc[6])
                fprintf(tradesf, "%d,%lld,%d,%.2f,%.2f,%.2f,%.2f,%lld,%.2f,%.2f,%.1f,%lld,%.2f\n", d.date, (long long)s.t[i], dir, E, VW[i], SD[i], E - dir * st[si],
                        (long long)s.t[xj], xp, r, A, (long long)s.t[lastAt[v]], L);
              const double tgD = ti == 1 ? fx : ti == 0 ? std::max(0.0, dir * (VW[i] + bk * SD[i] - E)) : 0;
              double tw = 0; int nt = 0;
              for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0 || j + 1 >= (long)s.size()) continue;
                const double A2 = s.days[od].atr; const double u = sim(VW, SD, j + 1, s.c[j], dir, std::max(st[si] / A * A2, 1.0), 1, 0, tgD > 0 ? std::max(tgD / A * A2, 0.5) : 0, ti == 2, W1[w]);
                if (std::isnan(u)) continue; nt++; tw += u / A2 * Anow - cost; }
              const double net = r / A * Anow - cost; Acc& a = acc[an][v][k][f][si][ti][w];
              a.s += net; a.raw += r / A * Anow; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
              const int er = era(d.date / 10000); a.m[er] += net; a.c[er]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } } }
  printf("Leave-a-level entry (VWAP / ±1 / ±2 SD, EMA 7/9 agrees) — %s 1m, 2010-07 → 2026-10-23. Cost %.2f. ATR %.0f. signals %ld\n", sym.c_str(), cost, Anow, nSig);
  printf("CSV: anchor,level,leave,filter,stop,target,session,n,net,raw,win,pf,per_yr,e1015,e1620,e2124,e2526,twin\n");
  for (int an = 0; an < NA; ++an) for (int v = 0; v < NV; ++v) for (int k = 0; k < NK; ++k) for (int f = 0; f < NF; ++f) for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int w = 0; w < NW; ++w) {
    const Acc& a = acc[an][v][k][f][si][ti][w]; if (a.n < 1) continue;
    printf("%s,%s,%.2f SD,%s,%s,%s,%s,%.0f,%.3f,%.3f,%.1f,%.2f,%.0f", AN[an], VN[v], KK[k], FN[f], SN[si], TN[ti], WN[w], a.n, a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
    for (int z = 0; z < 4; ++z) printf(",%s", a.c[z] >= 15 ? std::to_string(a.m[z] / a.c[z]).c_str() : "");
    printf(",%.3f\n", a.ny ? a.tw / a.ny : NAN); }
  if (tradesf) fclose(tradesf);
  return 0;
}
