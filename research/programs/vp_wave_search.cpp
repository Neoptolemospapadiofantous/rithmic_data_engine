// 2026-10-27 vp_wave_search.cpp (founder: "the mean reverse I have is this. vwap with 1-2 SD and when the 7 and 21 ema cross at one of
// those levels I trade. I also have a POC / volume profile — test these … just volume profile could be better"). The WAVE setup of
// ema721_wave_search.cpp (pierce a level, close back inside, EMA7/21 cross toward the mean, stop beyond the excursion extreme) run on
// VOLUME-PROFILE levels next to the VWAP band, everything else identical, so the levels are compared like for like:
//  LEVELS  vwap2  18:00 VWAP ± 2 SD (typical price × volume, VW SD) — the baseline (the paper id ww_ldn5_2sd), mean = VWAP
//          pVA    yesterday's RTH value area: sell VAH, buy VAL (Day::pvah / pval, 70 %), mean = yesterday's POC
//          dVA    today's developing profile from 18:00 (1-pt bins NQ, 0.25 ES, volume spread evenly over each 1m bar's range,
//                 70 % value area grown from the POC), sell VAH / buy VAL, mean = the developing POC; armed only after 60 min
//          pPOC   yesterday's POC as a level: rejected from below → sell, from above → buy; mean = VWAP
//          nPOC   the nearest naked POC above / below the PREVIOUS chart bar's close: POCs of the last 20 ok RTH sessions not
//                 traded through by a later session (the list as known at 18:00 — NOT Day::npoca / npocb, which pick the side of
//                 the 09:30 open and so look ahead for every trade before 09:30; the first run used them and was void); mean = VWAP
//          conf   the VWAP ± 2 SD band, armed only when yesterday's VAH (upper) / VAL (lower) is within 0.5 SD of it
//  TRIGGER ema    the wave: on or after the close-back bar, the chart bar where EMA7 crosses EMA21 toward the mean
//          close  enter at the close of the close-back bar itself (no EMA)
//  Both: the entry close must still be on the level's side of the mean; stop = the excursion extreme + 1 tick (min 2 pts);
//  armed state expires 60 min after the last bar past the level. TF 1/3/5. Window by entry time: Asia 18:00-03:00, London
//  03:00-09:30, NY open 09:30-10:30, NY mid 10:30-12:00, NY pm 12:00-16:30. Exit 1R / 2R / 3R / the mean (live). Flat 16:40.
//  One position at a time per config. Cost NQ 1.5 / ES 0.5 pts round trip, points at today's ATR. Twin = 5 random OTHER days,
//  same minute / direction / stop + target in ATR. 6 × 2 × 3 × 5 × 4 = 720 configs.
//  SELECT (fixed before the run, same as ema721_wave_search) on 2010-2020: n ≥ 150, PF ≥ 1.15, net > 0 in 2010-15 AND 2016-20;
//  JUDGE validation 2021-24 (net > 0, PF ≥ 1.05) → holdout 2025-01..2026-10-23 (net > 0) → ES (join the two CSVs).
// Output out/vp_wave_search_<SYM>.csv + stdout. Usage: vp_wave_search [NQ|ES]. tday_min = minutes since 18:00 ET (03:00 = 540, 16:40 = 1360).
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NL = 6, NG = 2, NTF = 3, NW = 5, NX = 4, NE = 4 };
static const char* LN[NL] = {"vwap2", "pVA", "dVA", "pPOC", "nPOC", "conf"};
static const char* GN[NG] = {"ema", "close"};
static const int TFS[NTF] = {1, 3, 5};
static const char* WN[NW] = {"Asia", "London", "NYopen", "NYmid", "NYpm"};
static const char* XN[NX] = {"1R", "2R", "3R", "mean"};
struct Acc { double s[NE] = {}, n[NE] = {}, gw[NE] = {}, gl[NE] = {}, tw = 0, ny = 0; };
struct Sig { size_t i; int dir, L; double R, mean, A; int tm, di; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5, BIN = sym == "ES" ? 0.25 : 1.0;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr; const int FLAT = 1360;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  // 18:00 VWAP + SD, and the developing 18:00 volume profile (POC / VAH / VAL after each 1m bar)
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN), DP(s.size(), NAN), DH(s.size(), NAN), DL(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0; std::vector<double> hist; long base = 0; int lo = 0, hi = -1, poc = -1; double tot = 0;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue;
      if (di != cur) { cur = di; pv = pq = vv = 0; hist.assign(16000, 0.0); base = (long)std::floor(s.o[i] / BIN) - 8000; lo = 16000; hi = -1; poc = -1; tot = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv)));
      const int a = (int)std::clamp((long)std::floor(s.l[i] / BIN) - base, 0L, 15999L), b = (int)std::clamp((long)std::floor(s.h[i] / BIN) - base, 0L, 15999L);
      const double per = v / (b - a + 1); for (int k = a; k <= b; ++k) { hist[k] += per; if (poc < 0 || hist[k] > hist[poc]) poc = k; }
      lo = std::min(lo, a); hi = std::max(hi, b); tot += v;
      int x0 = poc, x1 = poc; double in = hist[poc];
      while (in < 0.7 * tot && (x0 > lo || x1 < hi)) { const double up = x1 < hi ? hist[x1 + 1] : -1, dn = x0 > lo ? hist[x0 - 1] : -1; if (up >= dn) in += hist[++x1]; else in += hist[--x0]; }
      DP[i] = (float)((poc + base + 0.5) * BIN); DH[i] = (float)((x1 + base + 1) * BIN); DL[i] = (float)((x0 + base) * BIN); } }
  // naked POCs known at 18:00 of each day (bars.hpp logic without the 09:30-open side split)
  std::vector<std::vector<double>> NK(s.days.size());
  { std::vector<int> recent; int prev = -1; std::vector<double> dh(s.days.size(), -1e18), dl(s.days.size(), 1e18);
    for (size_t k = 0; k < s.days.size(); ++k) for (int i = s.days[k].b0; i < s.days[k].b1; ++i) { dh[k] = std::max(dh[k], (double)s.h[i]); dl[k] = std::min(dl[k], (double)s.l[i]); }
    for (size_t k = 0; k < s.days.size(); ++k) { const Day& d = s.days[k]; if (!d.ok) continue;
      if (prev >= 0 && (timegm_ymd(d.date) - timegm_ymd(s.days[prev].date)) / 86400 > 7) recent.clear();
      for (size_t r = recent.size() > 20 ? recent.size() - 20 : 0; r < recent.size(); ++r) { const double p = s.days[recent[r]].poc; if (std::isnan(p)) continue;
        bool naked = true; for (size_t q = r + 1; q < recent.size() && naked; ++q) naked = !(dl[recent[q]] <= p && p <= dh[recent[q]]); if (naked) NK[k].push_back(p); }
      recent.push_back((int)k); prev = (int)k; } }
  // the level on a side (dir −1 = upper / sell, +1 = lower / buy) and the mean, at 1m bar i; NAN = no level
  auto level = [&](int L, int dir, size_t i, double pc, double& mean) -> double {
    const Day& d = s.days[s.day_of[i]]; const double vw = VW[i], sd = SDV[i]; mean = vw;
    switch (L) {
      case 0: return vw - dir * 2 * sd;
      case 1: mean = d.ppoc; return dir < 0 ? d.pvah : d.pval;
      case 2: mean = DP[i]; return tday_min(s.t[i]) < 60 ? NAN : (dir < 0 ? DH[i] : DL[i]);
      case 3: return d.ppoc;
      case 4: { double best = NAN; for (double p : NK[s.day_of[i]]) if (dir < 0 ? p > pc && !(p >= best) : p < pc && !(p <= best)) best = p; return best; }
      default: { const double band = vw - dir * 2 * sd, va = dir < 0 ? d.pvah : d.pval; return std::fabs(band - va) <= 0.5 * sd ? band : NAN; } } };
  auto meanAt = [&](int L, size_t j) -> double { if (L == 1) return s.days[s.day_of[j]].ppoc; if (L == 2) return DP[j]; return VW[j]; };
  const double RM[3] = {1, 2, 3};
  auto trade = [&](size_t ie, int dir, double R, int x, int L, double fixedTg, int& minsOut) -> double {
    const double e = s.c[ie]; const int di = s.day_of[ie]; const int64_t t0 = s.t[ie];
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      auto out = [&](double v) { minsOut = (int)((s.t[j] - t0) / 60); return v; };
      if (tm >= FLAT) return out(dir * (s.o[j] - e));
      const double adv = dir > 0 ? s.l[j] - e : e - s.h[j], fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv <= -R) return out(std::min(-R, dir * (s.o[j] - e)));
      double tg = x < 3 ? RM[x] * R : fixedTg;
      if (x == 3 && fixedTg <= 0) { const double m = meanAt(L, j - 1); if (std::isnan(m)) continue; tg = dir * (m - e); if (tg <= 0) return out(dir * (s.o[j] - e)); }
      if (fav >= tg) return out(tg); }
    minsOut = 0; return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  static Acc acc[NL][NG][NTF][NW][NX]; std::mt19937 rng(20261027); long nsig = 0;
  for (int ti = 0; ti < NTF; ++ti) { const int tf = TFS[ti];
    struct CB { size_t i1; double h, l, c; }; std::vector<CB> cb;
    { long curB = -1; int curD = -2; CB b{0, 0, 0, 0};
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const long bk = tday_min(s.t[i]) / tf;
        if (di != curD || bk != curB) { if (curB >= 0) cb.push_back(b); curD = di; curB = bk; b = {i, s.h[i], s.l[i], s.c[i]}; }
        else { b.i1 = i; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; } }
      if (curB >= 0) cb.push_back(b); }
    std::vector<int8_t> G(cb.size()); { double e7 = cb[0].c, e21 = cb[0].c; for (size_t q = 0; q < cb.size(); ++q) { e7 += 2.0 / 8 * (cb[q].c - e7); e21 += 2.0 / 22 * (cb[q].c - e21); G[q] = e7 > e21 ? 1 : -1; } }
    const size_t expire = (size_t)(60 / tf);
    for (int L = 0; L < NL; ++L) for (int g = 0; g < NG; ++g) { std::vector<Sig> sigs;
      for (int side = 0; side < 2; ++side) { const int dir = side == 0 ? -1 : 1;
        bool ex = false, on = false; double exTop = 0; size_t exLast = 0; int curDay = -1;
        for (size_t q = 1; q < cb.size(); ++q) { const size_t i = cb[q].i1; const int di = s.day_of[i];
          if (di != curDay) { curDay = di; ex = on = false; }
          double mean; const double band = level(L, dir, i, cb[q - 1].c, mean); const int tm = tday_min(s.t[i]);
          if (std::isnan(band) || std::isnan(mean) || std::isnan(VW[i]) || !(SDV[i] > 0)) continue;
          if (ex && q - exLast > expire) ex = on = false;
          const double ext = side == 0 ? cb[q].h : cb[q].l, c = cb[q].c; bool closedBack = false;
          if (-dir * (ext - band) > 0) { if (!ex || -dir * (ext - exTop) > 0) exTop = ext; ex = true; exLast = q;
            if (-dir * (c - band) >= 0) on = false; else { closedBack = !on || g == 1; on = true; } }
          const bool fire = g == 0 ? on && G[q] == dir && G[q - 1] != dir : closedBack;
          if (!fire) continue;
          on = false; ex = false;
          const Day& d = s.days[di]; if (tm < 15 || tm >= 1350 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
          if (dir * (mean - c) <= 0) continue;   // already through the mean — not a fade any more
          sigs.push_back({i, dir, L, std::max(-dir * (exTop - c) + 0.25, 2.0), mean, d.atr, tm, di}); } }
      std::sort(sigs.begin(), sigs.end(), [](const Sig& a, const Sig& b) { return a.i < b.i; }); nsig += sigs.size();
      int64_t busy[NW][NX] = {};
      for (const Sig& sg : sigs) { const Day& d = s.days[sg.di]; const int w = sg.tm < 540 ? 0 : sg.tm < 930 ? 1 : sg.tm < 990 ? 2 : sg.tm < 1080 ? 3 : 4; const int e = era(d.date / 10000);
        for (int x = 0; x < NX; ++x) { if (s.t[sg.i] < busy[w][x]) continue;
          int mins = 0; const double pts = trade(sg.i, sg.dir, sg.R, x, L, 0, mins); if (std::isnan(pts)) continue;
          const double net = pts / sg.A * Anow - cost; busy[w][x] = s.t[sg.i] + (int64_t)std::max(mins, 1) * 60;
          double tw = 0; int nt = 0; const double rA = sg.R / sg.A, tgA = sg.dir * (sg.mean - s.c[sg.i]) / sg.A;
          for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == sg.di) continue; const long j = barAt(od, sg.tm); if (j < 0) continue;
            const double A2 = s.days[od].atr; int m2; const double qv = trade(j, sg.dir, std::max(rA * A2, 2.0), x, L, x == 3 ? std::max(tgA * A2, 0.25) : 0, m2);
            if (std::isnan(qv)) continue; nt++; tw += qv / A2 * Anow - cost; }
          Acc& a = acc[L][g][ti][w][x]; a.s[e] += net; a.n[e]++; (net > 0 ? a.gw[e] : a.gl[e]) += std::fabs(net); if (nt) { a.tw += net - tw / nt; a.ny++; } } } } }
  const std::string csv = "research/programs/out/vp_wave_search_" + sym + ".csv"; FILE* o = std::fopen(csv.c_str(), "w");
  fprintf(o, "level,trigger,tf,window,exit,is_n,is_pts,is_pf,is_a,is_b,val_n,val_pts,val_pf,ho_n,ho_pts,ho_pf,twin,per_yr\n");
  auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
  printf("Volume-profile wave search — %s, %ld signals. Cost %.1f. Points at today's ATR (%.0f). Selected on 2010-2020 (n >= 150, PF >= 1.15, both halves > 0):\n", sym.c_str(), nsig, cost, Anow);
  printf("%-6s %-6s %-3s %-7s %-5s | IS n   pts   PF  | VAL21-24 n  pts   PF | HO25-26 n  pts   PF | twin\n", "level", "trig", "tf", "window", "exit");
  int nsel[NL] = {}, nval[NL] = {}, nho[NL] = {}; double allIS[NL] = {}, allISn[NL] = {}, allHO[NL] = {}, allHOn[NL] = {};
  for (int L = 0; L < NL; ++L) for (int g = 0; g < NG; ++g) for (int ti = 0; ti < NTF; ++ti) for (int w = 0; w < NW; ++w) for (int x = 0; x < NX; ++x) {
    const Acc& a = acc[L][g][ti][w][x]; const double isn = a.n[0] + a.n[1], iss = a.s[0] + a.s[1], isgw = a.gw[0] + a.gw[1], isgl = a.gl[0] + a.gl[1];
    allIS[L] += iss; allISn[L] += isn; allHO[L] += a.s[2] + a.s[3]; allHOn[L] += a.n[2] + a.n[3];
    fprintf(o, "%s,%s,%d,%s,%s,%.0f,%.3f,%.3f,%.3f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.3f,%.1f\n", LN[L], GN[g], TFS[ti], WN[w], XN[x], isn, isn ? iss / isn : 0, pf(isgw, isgl),
            a.n[0] ? a.s[0] / a.n[0] : 0, a.n[1] ? a.s[1] / a.n[1] : 0, a.n[2], a.n[2] ? a.s[2] / a.n[2] : 0, pf(a.gw[2], a.gl[2]), a.n[3], a.n[3] ? a.s[3] / a.n[3] : 0, pf(a.gw[3], a.gl[3]),
            a.ny ? a.tw / a.ny : 0, (isn + a.n[2] + a.n[3]) / 16.3);
    if (!(isn >= 150 && pf(isgw, isgl) >= 1.15 && a.s[0] > 0 && a.s[1] > 0)) continue; nsel[L]++;
    const bool v = a.s[2] > 0 && pf(a.gw[2], a.gl[2]) >= 1.05, h = a.s[3] > 0; nval[L] += v; nho[L] += v && h;
    printf("%-6s %-6s %-3d %-7s %-5s | %5.0f %+6.2f %4.2f | %5.0f %+6.2f %4.2f %s | %4.0f %+6.2f %4.2f %s | %+6.2f\n", LN[L], GN[g], TFS[ti], WN[w], XN[x], isn, iss / isn, pf(isgw, isgl),
           a.n[2], a.n[2] ? a.s[2] / a.n[2] : 0, pf(a.gw[2], a.gl[2]), v ? "ok" : "--", a.n[3], a.n[3] ? a.s[3] / a.n[3] : 0, pf(a.gw[3], a.gl[3]), h ? "ok" : "--", a.ny ? a.tw / a.ny : 0); }
  std::fclose(o);
  printf("\nPer level (120 configs each): selected on 2010-20 / also pass 2021-24 / also positive 2025-26 · average net pts/trade over ALL its configs 2010-20 | 2021-26\n");
  for (int L = 0; L < NL; ++L) printf("  %-6s %3d / %3d / %3d   · %+6.2f | %+6.2f\n", LN[L], nsel[L], nval[L], nho[L], allISn[L] ? allIS[L] / allISn[L] : 0, allHOn[L] ? allHO[L] / allHOn[L] : 0);
  return 0;
}
