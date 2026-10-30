// 2026-10-29 fade2sd_trail.cpp (founder: "what about with trail"). fade2sd_ema.cpp's setup and entry, with trailing exits. Fixed before
// the run (arm 15 min, no 1 SD room filter — fade2sd_ema showed neither mattered):
//  TRAIL   R0 none · R1 break-even at 1R · R2 trail 1R behind the best price once 1R up · R3 trail 0.5R behind the best once 1R up ·
//          R4 exit at the next open after a 1m close back across EMA 9 (against the trade) · R5 trail by EMA 9 once 1R up.
//          Trails move on CLOSED bars only (the new stop applies from the next bar); a bar that opens through the stop fills at the open.
//  TARGET  T1 VWAP (live) · T2 none (runner, trail or session end) · T3 fixed 3R.   STOP S1 reversal extreme ± 1 tick · S2 3 SD band.
//  SETUP   P0 = the 2 SD fade below. P1 (founder: "what about if we are above or below vwap") = trade WITH the VWAP side: price closes
//          above VWAP (buy side) and below EMA 9 = a pullback; the arm drops when a bar closes back below VWAP. Entry = the same EMA
//          turn (X1/X2) while still above VWAP; S1 = the pullback's low − 1 tick; T1 VWAP is not used (it lies behind the trade).
// Original fade2sd_ema header follows.
//  SETUP   a 1m bar CLOSES beyond the 2 SD band (arms the side). The arm lasts ARM minutes after the LAST close beyond (15 or 30).
//  ENTRY   X1 EMA 7 crosses back over EMA 9 in the fade direction · X2 a 1m close back across EMA 9 (price reclaims the EMA) — market at
//          that bar's close. Variant Z: the entry must still be beyond the 1 SD band (room left to VWAP), else skipped.
//  STOP    S1 the reversal extreme (lowest low / highest high since armed) ± 1 tick, min 2 pts · S2 the 3 SD band · S3 none.
//  TARGET  T1 VWAP (live) · T2 the 1 SD band (live) · T3 fixed 2R (S1/S2 only).
//  ANCHOR  A0 VWAP from 09:30 carried overnight · A2 each session's own open (Asia 18:00, London 03:00, NY 09:30). Armed 30 min after.
//  SESSION Asia 18:00-03:00 · London 03:00-09:30 · NY 09:30-15:55; flat at the session end; no entry in the last 15 min.
//          One position at a time per cell. Stop first when one bar touches both.
//  COST    market in: NQ 1.5 / ES 0.5 pts. Points scaled to today's ATR. Control = 5 random OTHER days, same minute / direction /
//          stop + target distance in ATR.
//  PASS    n ≥ 150, net > 0 in 2010-15, 2016-20, 2021-24 AND 2025-26, beats its control, same cell net > 0 on ES.
// Usage: fade2sd_trail [NQ|ES]. TRADES=<file> CELL=a,x,arm,z,s,t,w,L dumps one cell.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NA = 2, NX = 2, NR = 1, NZ = 2, NS = 2, NT = 3, NW = 3, NL = 6 };
static const char* AN[NA] = {"A0 09:30 carried", "A2 session open"};
static const char* XN[NX] = {"X1 EMA7/9 cross", "X2 close back over EMA9"};
static const int ARM[NR] = {15};
static const char* ZN[NZ] = {"P0 fade 2SD", "P1 pullback on the VWAP side"};
static const char* SN[NS] = {"S1 reversal extreme", "S2 3SD"};
static const char* TN[NT] = {"T1 VWAP", "T2 none", "T3 3R"};
static const char* LN[NL] = {"R0 no trail", "R1 BE at 1R", "R2 trail 1R", "R3 trail 0.5R", "R4 EMA9 close", "R5 EMA9 after 1R"};
static const char* WN[NW] = {"Asia", "London", "NY"};
static const int W1[NW] = {540, 930, 1315};
struct Acc { double s = 0, raw = 0, n = 0, w = 0, gw = 0, gl = 0, tw = 0, ny = 0, m[4] = {0, 0, 0, 0}, c[4] = {0, 0, 0, 0}; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const bool es = sym == "ES";
  const double cost = es ? 0.5 : 1.5, tick = 0.25;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  auto win = [](int tm) { return tm < 540 ? 0 : tm < 930 ? 1 : 2; };
  // EMA 7 / 9 on 1m closes, continuous (reset after a data hole > 30 min)
  std::vector<float> E7(s.size()), E9(s.size());
  for (size_t i = 0; i < s.size(); ++i) { const bool rs = i == 0 || s.t[i] - s.t[i - 1] > 1800;
    E7[i] = rs ? s.c[i] : E7[i - 1] + (s.c[i] - E7[i - 1]) * (2.0 / 8); E9[i] = rs ? s.c[i] : E9[i - 1] + (s.c[i] - E9[i - 1]) * (2.0 / 10); }
  size_t xj = 0; double xp = 0;
  // tk: 0 VWAP live / 1 none; fx > 0 fixed target distance; tr = trail mode
  auto sim = [&](const std::vector<float>& VW, size_t j0, double e, int dir, double stD, int tk, double fx, int tr, int wend) -> double {
    const int di = s.day_of[j0 - 1]; double stP = e - dir * stD, best = e; bool flag = false;
    for (size_t j = j0; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]); xj = j;
      if (tm >= wend || flag) { xp = s.o[j]; return dir * (s.o[j] - e); }
      if (dir * (s.o[j] - stP) <= 0) { xp = s.o[j]; return dir * (s.o[j] - e); }
      if (dir > 0 ? s.l[j] <= stP : s.h[j] >= stP) { xp = stP; return dir * (stP - e); }
      double tgP = NAN;
      if (fx > 0) tgP = e + dir * fx; else if (tk == 0 && !std::isnan(VW[j - 1])) { tgP = VW[j - 1]; if (dir * (tgP - e) <= 0) { xp = s.o[j]; return dir * (s.o[j] - e); } }
      if (!std::isnan(tgP) && (dir > 0 ? s.h[j] >= tgP : s.l[j] <= tgP)) { xp = tgP; return dir * (tgP - e); }
      best = dir > 0 ? std::max(best, (double)s.h[j]) : std::min(best, (double)s.l[j]);
      const bool up1 = dir * (best - e) >= stD; auto raise = [&](double p) { if (dir * (p - stP) > 0) stP = p; };
      if (tr == 1 && up1) raise(e);
      if (tr == 2 && up1) raise(best - dir * stD);
      if (tr == 3 && up1) raise(best - dir * 0.5 * stD);
      if ((tr == 4 || (tr == 5 && up1)) && dir * (s.c[j] - E9[j]) < 0) flag = true; }
    return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  FILE* tradesf = getenv("TRADES") ? fopen(getenv("TRADES"), "w") : nullptr; int dc[8] = {0, 0, 0, 0, 0, 0, 2, 0};
  if (getenv("CELL")) sscanf(getenv("CELL"), "%d,%d,%d,%d,%d,%d,%d,%d", &dc[0], &dc[1], &dc[2], &dc[3], &dc[4], &dc[5], &dc[6], &dc[7]);
  static Acc acc[NA][NX][NR][NZ][NS][NT][NW][NL]; std::mt19937 rng(20261029); long nSig = 0;
  for (int an = 0; an < NA; ++an) {
    std::vector<float> VW(s.size(), NAN), SD(s.size(), NAN); std::vector<int> since(s.size(), -1);
    { double pv = 0, pq = 0, vv = 0; int64_t t0 = 0; bool on = false; int prevTm = -1, prevD = -2;
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) { on = false; continue; } const int tm = tday_min(s.t[i]);
        const bool reset = an == 0 ? (tm >= 930 && (prevTm < 930 || di != prevD)) : (di != prevD || (tm >= 540 && prevTm < 540) || (tm >= 930 && prevTm < 930));
        prevTm = tm; prevD = di;
        if (reset) { pv = pq = vv = 0; t0 = s.t[i]; on = true; } if (!on) continue;
        const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
        VW[i] = (float)(pv / vv); SD[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); since[i] = (int)((s.t[i] - t0) / 60); } }
    static int64_t busy[NX][NR][NZ][NS][NT][NW][NL]; memset(busy, 0, sizeof busy);
    // per direction: time of the last close beyond 2 SD and the extreme since then (reset when the window / day changes)
    int64_t lastB[NZ][2] = {}; double ext[NZ][2] = {}; bool used[NZ][NX][2] = {}; int lastW[NZ][2], lastD[NZ][2]; for (auto& q : lastW) q[0] = q[1] = -1; for (auto& q : lastD) q[0] = q[1] = -1;
    for (size_t i = 2; i + 1 < s.size(); ++i) {
      const int di = s.day_of[i]; if (di < 0 || s.day_of[i - 1] != di || s.day_of[i + 1] != di) continue; const Day& d = s.days[di];
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
      const int tm = tday_min(s.t[i]), w = win(tm);
      if (since[i] < 30 || !(SD[i] > 0) || std::isnan(VW[i])) continue;
      const double A = d.atr;
      for (int z = 0; z < NZ; ++z) for (int k = 0; k < 2; ++k) { const int dir = k == 0 ? 1 : -1;   // +1 BUY, −1 SELL
        int64_t& LB = lastB[z][k]; double& EX = ext[z][k]; 
        if (lastW[z][k] != w || lastD[z][k] != di) { LB = 0; lastW[z][k] = w; lastD[z][k] = di; used[z][0][k] = used[z][1][k] = false; }
        if (z == 1 && dir * (s.c[i] - VW[i]) <= 0) { LB = 0; continue; }   // pullback setup: wrong side of VWAP → no arm
        const bool stretched = z == 0 ? dir * (s.c[i] - (VW[i] - dir * 2 * SD[i])) < 0 : dir * (s.c[i] - E9[i]) < 0;
        if (LB) EX = dir > 0 ? std::min(EX, (double)s.l[i]) : std::max(EX, (double)s.h[i]);
        if (stretched) { if (!LB) EX = dir > 0 ? s.l[i] : s.h[i]; LB = s.t[i]; used[z][0][k] = used[z][1][k] = false; continue; }
        if (!LB || tm >= W1[w] - 15) continue;
        const int age = (int)((s.t[i] - LB) / 60);
        const bool sx[NX] = {dir * (E7[i] - E9[i]) > 0 && dir * (E7[i - 1] - E9[i - 1]) <= 0,
                             dir * (s.c[i] - E9[i]) > 0 && dir * (s.c[i - 1] - E9[i - 1]) <= 0};
        const double E = s.c[i];
        for (int x = 0; x < NX; ++x) { if (!sx[x] || used[z][x][k]) continue; used[z][x][k] = true;
          for (int r = 0; r < NR; ++r) { if (age > ARM[r]) continue; nSig++;
              const double st[NS] = {std::max(2.0, dir * (E - EX) + tick), std::max(2.0, dir * (E - (VW[i] - dir * 3 * SD[i])))};
              for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int L = 0; L < NL; ++L) {
                if (z == 1 && ti == 0) continue;
                if (s.t[i] < busy[x][r][z][si][ti][w][L]) continue;
                const double fx = ti == 2 ? 3 * st[si] : 0;
                const double p = sim(VW, i + 1, E, dir, st[si], ti, fx, L, W1[w]); if (std::isnan(p)) continue;
                busy[x][r][z][si][ti][w][L] = s.t[xj]; const size_t xjE = xj; const double xpE = xp;
                if (tradesf && an == dc[0] && x == dc[1] && r == dc[2] && z == dc[3] && si == dc[4] && ti == dc[5] && w == dc[6] && L == dc[7])
                  fprintf(tradesf, "%d,%lld,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%lld,%.2f,%.2f,%.1f,%lld\n", d.date, (long long)s.t[i], dir, E, VW[i], SD[i],
                          E - dir * st[si], ti == 2 ? E + dir * fx : ti == 0 ? VW[i] : NAN, (long long)s.t[xjE], xpE, p, A, (long long)LB);
                const double tgD = ti == 2 ? fx : ti == 0 ? dir * (VW[i] - E) : 0;
                double tw = 0; int nt = 0;
                for (int q = 0; q < 10 && nt < 5; ++q) { const int od = good[rng() % good.size()]; if (od == di) continue; const long j = barAt(od, tm); if (j < 0 || j + 1 >= (long)s.size()) continue;
                  const double A2 = s.days[od].atr; const double v = sim(VW, j + 1, s.c[j], dir, std::max(st[si] / A * A2, 1.0), 1, tgD > 0 ? std::max(tgD / A * A2, 0.5) : 0, L, W1[w]);
                  if (std::isnan(v)) continue; nt++; tw += v / A2 * Anow - cost; }
                const double net = p / A * Anow - cost; Acc& a = acc[an][x][r][z][si][ti][w][L];
                a.s += net; a.raw += p / A * Anow; a.n++; a.w += net > 0; (net > 0 ? a.gw : a.gl) += std::fabs(net);
                const int er = era(d.date / 10000); a.m[er] += net; a.c[er]++; if (nt) { a.tw += net - tw / nt; a.ny++; } } } }
        } } }   // one entry per arm and trigger
  printf("VWAP 2 SD stretch → EMA 7/9 reversal entry + trailing exits, fade + VWAP-side pullback — %s 1m, all sessions, 2010-07 → 2026-10-23. Cost %.2f pts. Points at today's ATR (%.0f). signals %ld\n", sym.c_str(), cost, Anow, nSig);
  printf("CSV: anchor,trigger,arm,setup,stop,target,session,trail,n,net,raw,win,pf,per_yr,e1015,e1620,e2124,e2526,twin\n");
  for (int an = 0; an < NA; ++an) for (int x = 0; x < NX; ++x) for (int r = 0; r < NR; ++r) for (int z = 0; z < NZ; ++z) for (int si = 0; si < NS; ++si) for (int ti = 0; ti < NT; ++ti) for (int w = 0; w < NW; ++w) for (int L = 0; L < NL; ++L) {
    const Acc& a = acc[an][x][r][z][si][ti][w][L]; if (a.n < 1) continue;
    printf("%s,%s,%d,%s,%s,%s,%s,%s,%.0f,%.3f,%.3f,%.1f,%.2f,%.0f", AN[an], XN[x], ARM[r], ZN[z], SN[si], TN[ti], WN[w], LN[L], a.n, a.s / a.n, a.raw / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.n / 16.3);
    for (int k = 0; k < 4; ++k) printf(",%s", a.c[k] >= 15 ? std::to_string(a.m[k] / a.c[k]).c_str() : "");
    printf(",%.3f\n", a.ny ? a.tw / a.ny : NAN); }
  if (tradesf) fclose(tradesf);
  return 0;
}
