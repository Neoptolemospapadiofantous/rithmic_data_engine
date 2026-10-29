// 2026-10-27 wave_1m_hours.cpp (founder: "what about the 1m cross over on specific times"). The wave setup of
// ema721_wave_search.cpp on the 1-MINUTE chart only (pierce 18:00 VWAP ± k SD, close back inside, EMA7/21 cross toward VWAP
// while still on the band side, stop = excursion extreme + 1 tick, min 2 pts), no filter, broken down by the HALF HOUR of entry
// (ET) across the whole 18:00 → 16:30 day, so "specific times" are read directly instead of through five wide windows.
// k = 1 / 2 SD × exit 1R / 2R. One position at a time per (k, exit, half hour). Flat 16:40 ET. Cost NQ 1.5 / ES 0.5 pts round
// trip, points at today's ATR. Twin = 5 random OTHER days, same minute / direction / stop + target in ATR.
// Periods: 2010-20 (look here first) · 2021-24 · 2025-01 → 2026-10-23. 46 half hours × 4 = 184 cells: expect ~9 to look good in
// all three periods by chance alone (5 % each way is not a filter at this count) — a time counts only with ES agreeing.
// Output out/wave_1m_hours_<SYM>.csv + stdout. Usage: wave_1m_hours [NQ|ES]. tday_min = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NK = 2, NX = 2, NB = 45, NP = 3 };
static const double KS[NK] = {1, 2};
static const double RM[NX] = {1, 2};
struct Acc { double s[NP] = {}, n[NP] = {}, gw[NP] = {}, gl[NP] = {}, tw[NP] = {}, ny[NP] = {}; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr; const int FLAT = 1360;
  auto per = [](int y) { return y <= 2020 ? 0 : y <= 2024 ? 1 : 2; };
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; if (di != cur) { cur = di; pv = pq = vv = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  auto trade = [&](size_t ie, int dir, double R, double tg, int& minsOut) -> double {
    const double e = s.c[ie]; const int di = s.day_of[ie]; const int64_t t0 = s.t[ie];
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      auto out = [&](double v) { minsOut = (int)((s.t[j] - t0) / 60); return v; };
      if (tm >= FLAT) return out(dir * (s.o[j] - e));
      const double adv = dir > 0 ? s.l[j] - e : e - s.h[j], fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv <= -R) return out(std::min(-R, dir * (s.o[j] - e)));
      if (fav >= tg) return out(tg); }
    minsOut = 0; return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  // 1m EMA7 / EMA21 sign, continuous across days (as the search)
  std::vector<int8_t> G(s.size(), 0); { double e7 = s.c[0], e21 = s.c[0]; for (size_t i = 0; i < s.size(); ++i) { e7 += 2.0 / 8 * (s.c[i] - e7); e21 += 2.0 / 22 * (s.c[i] - e21); G[i] = e7 > e21 ? 1 : -1; } }
  static Acc acc[NK][NX][NB]; std::mt19937 rng(20261027);
  for (int ki = 0; ki < NK; ++ki) { const double kk = KS[ki];
    struct Sg { size_t i; int dir; double R; double A; int tm, di; }; std::vector<Sg> sigs;
    for (int side = 0; side < 2; ++side) { const int dir = side == 0 ? -1 : 1;
      bool ex = false, on = false; double exTop = 0; size_t exLast = 0; int curDay = -1;
      for (size_t i = 1; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue;
        if (di != curDay) { curDay = di; ex = on = false; }
        const double vw = VW[i], sd = SDV[i]; const int tm = tday_min(s.t[i]); if (std::isnan(vw) || !(sd > 0)) continue;
        if (ex && i - exLast > 60) ex = on = false;
        const double band = vw - dir * kk * sd, ext = side == 0 ? s.h[i] : s.l[i], c = s.c[i];
        if (-dir * (ext - band) > 0) { if (!ex || -dir * (ext - exTop) > 0) exTop = ext; ex = true; exLast = i;
          if (-dir * (c - band) >= 0) on = false; else on = true; }
        if (!on || !(G[i] == dir && G[i - 1] != dir)) continue;
        on = false; ex = false;
        const Day& d = s.days[di]; if (tm < 15 || tm >= 1350 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
        if (dir * (vw - c) <= 0) continue;
        sigs.push_back({i, dir, std::max(-dir * (exTop - c) + 0.25, 2.0), d.atr, tm, di}); } }
    std::sort(sigs.begin(), sigs.end(), [](const Sg& a, const Sg& b) { return a.i < b.i; });
    for (int x = 0; x < NX; ++x) { std::vector<int64_t> busy(NB, 0);
      for (const Sg& g : sigs) { const int b = g.tm / 30; if (b >= NB || s.t[g.i] < busy[b]) continue;
        int mins = 0; const double pts = trade(g.i, g.dir, g.R, RM[x] * g.R, mins); if (std::isnan(pts)) continue;
        busy[b] = s.t[g.i] + (int64_t)std::max(mins, 1) * 60;
        const double net = pts / g.A * Anow - cost; const int p = per(s.days[g.di].date / 10000);
        double tw = 0; int nt = 0; const double rA = g.R / g.A;
        for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == g.di) continue; const long j = barAt(od, g.tm); if (j < 0) continue;
          const double A2 = s.days[od].atr, R2 = std::max(rA * A2, 2.0); int m2; const double q = trade(j, g.dir, R2, RM[x] * R2, m2);
          if (std::isnan(q)) continue; nt++; tw += q / A2 * Anow - cost; }
        Acc& a = acc[ki][x][b]; a.s[p] += net; a.n[p]++; (net > 0 ? a.gw[p] : a.gl[p]) += std::fabs(net); if (nt) { a.tw[p] += net - tw / nt; a.ny[p]++; } } } }
  FILE* o = std::fopen(("research/programs/out/wave_1m_hours_" + sym + ".csv").c_str(), "w");
  fprintf(o, "k,exit,et_from,n1,pts1,pf1,n2,pts2,pf2,n3,pts3,pf3,twin_all\n");
  auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
  printf("1m wave by half hour of entry — %s. Net pts/trade at today's ATR (%.0f), cost %.1f. Periods 2010-20 | 2021-24 | 2025-26, twin = edge over random other days (all years).\n", sym.c_str(), Anow, cost);
  for (int ki = 0; ki < NK; ++ki) for (int x = 0; x < NX; ++x) {
    printf("\n== k %.0f SD, target %.0fR ==\n%-6s %-24s %-24s %-24s %s\n", KS[ki], RM[x], "ET", "2010-20 n pts PF", "2021-24 n pts PF", "2025-26 n pts PF", "twin");
    for (int b = 0; b < NB; ++b) { const Acc& a = acc[ki][x][b]; if (a.n[0] + a.n[1] + a.n[2] < 30) continue;
      const int m = (b * 30 + 1080) % 1440; const double twn = a.ny[0] + a.ny[1] + a.ny[2], tws = a.tw[0] + a.tw[1] + a.tw[2];
      const bool all3 = a.s[0] > 0 && a.s[1] > 0 && a.s[2] > 0;
      printf("%02d:%02d  ", m / 60, m % 60);
      for (int p = 0; p < NP; ++p) printf("%5.0f %+6.2f %4.2f        ", a.n[p], a.n[p] ? a.s[p] / a.n[p] : 0, pf(a.gw[p], a.gl[p]));
      printf("%+6.2f %s\n", twn ? tws / twn : 0, all3 ? "<< + in all three" : "");
      fprintf(o, "%.0f,%.0fR,%02d:%02d", KS[ki], RM[x], m / 60, m % 60);
      for (int p = 0; p < NP; ++p) fprintf(o, ",%.0f,%.3f,%.3f", a.n[p], a.n[p] ? a.s[p] / a.n[p] : 0, pf(a.gw[p], a.gl[p]));
      fprintf(o, ",%.3f\n", twn ? tws / twn : 0); } }
  std::fclose(o); return 0;
}
