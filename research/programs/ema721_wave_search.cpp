// 2026-10-26 ema721_wave_search.cpp (founder: "ok find winning set ups", after ema721_ms_vwap.cpp reproduced their chart trades but
// had no edge as one rule). The WAVE setup (S2 of ema721_ms_vwap, with the stop beyond the whole excursion) searched over a grid, with
// the selection rule fixed BEFORE the run:
//  SETUP   on the chart TF (1m / 3m / 5m): a chart bar trades through VWAP ± k SD (18:00-anchored VWAP, typical price × volume, VW SD)
//          and closes back inside → armed; a close beyond the band waits for the next close-back; the excursion's extreme is kept.
//          SELL at the close of the chart bar where EMA7 crosses below EMA21 (on or after the close-back bar); BUY mirrored at the
//          lower band. The entry must still be on the band's side of VWAP (a fade). Stop = the excursion extreme + 1 tick (min 2).
//          Armed state expires 60 min after the last bar past the band.
//  GRID    TF 1/3/5 × k 1/1.5/2/2.5 × window (entry: Asia 18:00-03:00, London 03:00-09:30, NY open 09:30-10:30, NY mid
//          10:30-12:00, NY pm 12:00-16:30) × exit (1R, 1.5R, 2R, 3R, target the OPPOSITE band, target VWAP, trail 1R after +1R)
//          × filter (F0 none · F1 VWAP flat: |VWAP move over the last 30 min| < 0.15 SD · F2 deep: the extreme went ≥ 0.5 SD past the
//          band · F3 quick: the cross came ≤ 10 chart bars after the extreme · F4 small stop: R ≤ 0.1 ATR) = 2,100 configs.
//          All trades flat 16:40 ET. One position at a time per config. Cost NQ 1.5 / ES 0.5 pts round trip; points scaled to today's
//          ATR, cost at today's size. Control per trade: 5 random OTHER days, same minute / direction / stop + target in ATR.
//  SELECT  (fixed before the run) on 2010-2020 only: ≥ 150 trades, PF ≥ 1.15, net > 0 in BOTH 2010-15 and 2016-20.
//  JUDGE   validation 2021-2024 (net > 0, PF ≥ 1.05), then holdout 2025-01 → 2026-10-23 (net > 0), then the same config on ES (net > 0).
// Output: out/ema721_wave_search_<SYM>.csv (every config) + the selected list on stdout. Join NQ + ES with the awk in research/README.md.
// Usage: ema721_wave_search [NQ|ES] [tf k window filter exit → that config's trades to out/ema721_wave_trades_<SYM>.csv]. tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 16:30 = 1350, 16:40 = 1360.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NTF = 3, NK = 4, NW = 5, NX = 7, NF = 5, NE = 4 };
static const int TFS[NTF] = {1, 3, 5};
static const double KS[NK] = {1, 1.5, 2, 2.5};
static const char* WN[NW] = {"Asia", "London", "NYopen", "NYmid", "NYpm"};
static const char* XN[NX] = {"1R", "1.5R", "2R", "3R", "oppBand", "VWAP", "trail1R"};
static const char* FN[NF] = {"none", "vwapFlat", "deep", "quick", "smallStop"};
struct Acc { double s[NE] = {}, n[NE] = {}, gw[NE] = {}, gl[NE] = {}, tw = 0, ny = 0; };
struct Sig { size_t i; int dir; double R, vw, opp, A; int tm, di; bool f[NF]; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  FILE* dump = nullptr; int dT = -1, dK = -1, dW = -1, dF = -1, dX = -1;
  if (argc > 6) { for (int i = 0; i < NTF; ++i) if (std::atoi(argv[2]) == TFS[i]) dT = i; for (int i = 0; i < NK; ++i) if (std::atof(argv[3]) == KS[i]) dK = i;
    for (int i = 0; i < NW; ++i) if (WN[i] == std::string(argv[4])) dW = i; for (int i = 0; i < NF; ++i) if (FN[i] == std::string(argv[5])) dF = i;
    for (int i = 0; i < NX; ++i) if (XN[i] == std::string(argv[6])) dX = i;
    dump = std::fopen(("research/programs/out/ema721_wave_trades_" + sym + ".csv").c_str(), "w"); fprintf(dump, "date,et_hhmm,dir,entry,stop_pts,gross_pts,net_scaled,atr,mins\n"); }
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr; const int FLAT = 1360;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; if (di != cur) { cur = di; pv = pq = vv = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  // x: 0-3 R multiples, 4 opposite band (live, k), 5 VWAP (live), 6 trail. fixedTg > 0 = a fixed target (controls for x 4/5).
  const double RM[4] = {1, 1.5, 2, 3};
  auto trade = [&](size_t ie, int dir, double R, int x, double kk, double fixedTg, int& minsOut) -> double {
    const double e = s.c[ie]; const int di = s.day_of[ie]; const int64_t t0 = s.t[ie]; double stop = -R, best = 0;
    for (size_t j = ie + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      auto out = [&](double v) { minsOut = (int)((s.t[j] - t0) / 60); return v; };
      if (tm >= FLAT) return out(dir * (s.o[j] - e));
      const double adv = dir > 0 ? s.l[j] - e : e - s.h[j], fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv <= stop) return out(std::min(stop, dir * (s.o[j] - e)));
      double tg = x < 4 ? RM[x] * R : 0;
      if (x == 4 || x == 5) { if (fixedTg > 0) tg = fixedTg; else { const double vw = VW[j - 1], sd = SDV[j - 1]; if (std::isnan(vw)) continue;
          tg = dir * ((x == 5 ? vw : vw + dir * kk * sd) - e); if (tg <= 0) return out(dir * (s.o[j] - e)); } }
      if (tg > 0 && fav >= tg) return out(tg);
      best = std::max(best, fav); if (x == 6 && best >= R) stop = std::max(stop, best - R); }
    minsOut = 0; return NAN; };
  auto barAt = [&](int di, int tm) -> long { const Day& d = s.days[di]; for (int i = d.b0; i < d.b1; ++i) if (tday_min(s.t[i]) >= tm) return tday_min(s.t[i]) <= tm + 2 ? i : -1; return -1; };
  static Acc acc[NTF][NK][NW][NF][NX]; std::mt19937 rng(20261026); long nsig = 0;
  for (int ti = 0; ti < NTF; ++ti) { const int tf = TFS[ti];
    struct CB { size_t i1; double h, l, c; }; std::vector<CB> cb;
    { long curB = -1; int curD = -2; CB b{0, 0, 0, 0};
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const long bk = tday_min(s.t[i]) / tf;
        if (di != curD || bk != curB) { if (curB >= 0) cb.push_back(b); curD = di; curB = bk; b = {i, s.h[i], s.l[i], s.c[i]}; }
        else { b.i1 = i; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; } }
      if (curB >= 0) cb.push_back(b); }
    std::vector<int8_t> G(cb.size()); { double e7 = cb[0].c, e21 = cb[0].c; for (size_t q = 0; q < cb.size(); ++q) { e7 += 2.0 / 8 * (cb[q].c - e7); e21 += 2.0 / 22 * (cb[q].c - e21); G[q] = e7 > e21 ? 1 : -1; } }
    const size_t expire = (size_t)(60 / tf);
    for (int ki = 0; ki < NK; ++ki) { const double kk = KS[ki]; std::vector<Sig> sigs;
      for (int side = 0; side < 2; ++side) { const int dir = side == 0 ? -1 : 1;
        bool ex = false, on = false; double exTop = 0; size_t exLast = 0, exBar = 0, armBar = 0; int curDay = -1;
        for (size_t q = 1; q < cb.size(); ++q) { const size_t i = cb[q].i1; const int di = s.day_of[i];
          if (di != curDay) { curDay = di; ex = on = false; }
          const double vw = VW[i], sd = SDV[i]; const int tm = tday_min(s.t[i]); if (std::isnan(vw) || !(sd > 0)) continue;
          if (ex && q - exLast > expire) ex = on = false;
          const double band = vw - dir * kk * sd, ext = side == 0 ? cb[q].h : cb[q].l, c = cb[q].c;
          if (-dir * (ext - band) > 0) { if (!ex || -dir * (ext - exTop) > 0) { exTop = ext; exBar = q; } ex = true; exLast = q;
            if (-dir * (c - band) >= 0) on = false; else if (!on) { on = true; armBar = q; } }
          if (!on || q < armBar || !(G[q] == dir && G[q - 1] != dir)) continue;
          on = false; ex = false;
          const Day& d = s.days[di]; if (tm < 15 || tm >= 1350 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
          if (dir * (vw - c) <= 0) continue;   // already through VWAP — not a fade any more
          Sig g; g.i = i; g.dir = dir; g.A = d.atr; g.R = std::max(-dir * (exTop - c) + 0.25, 2.0); g.vw = vw; g.opp = vw + dir * kk * sd; g.tm = tm; g.di = di;
          const double vw30 = i >= 30 && s.day_of[i - 30] == di ? VW[i - 30] : NAN;
          g.f[0] = true; g.f[1] = !std::isnan(vw30) && std::fabs(vw - vw30) < 0.15 * sd; g.f[2] = -dir * (exTop - band) >= 0.5 * sd;
          g.f[3] = q - exBar <= 10; g.f[4] = g.R <= 0.1 * d.atr; sigs.push_back(g); } }
      std::sort(sigs.begin(), sigs.end(), [](const Sig& a, const Sig& b) { return a.i < b.i; }); nsig += sigs.size();
      static int64_t busy[NW][NF][NX]; for (auto& a : busy) for (auto& b : a) for (auto& x : b) x = 0;
      for (const Sig& g : sigs) { const Day& d = s.days[g.di]; const int w = g.tm < 540 ? 0 : g.tm < 930 ? 1 : g.tm < 990 ? 2 : g.tm < 1080 ? 3 : 4; const int e = era(d.date / 10000);
        for (int x = 0; x < NX; ++x) { bool need = false; for (int f = 0; f < NF; ++f) need |= g.f[f] && s.t[g.i] >= busy[w][f][x]; if (!need) continue;
          int mins = 0; const double pts = trade(g.i, g.dir, g.R, x, kk, 0, mins); if (std::isnan(pts)) continue;
          const double net = pts / g.A * Anow - cost; const int64_t exitT = s.t[g.i] + (int64_t)std::max(mins, 1) * 60;
          double tw = 0; int nt = 0; const double rA = g.R / g.A, tgA = g.dir * ((x == 5 ? g.vw : g.opp) - s.c[g.i]) / g.A;
          for (int z = 0; z < 10 && nt < 5; ++z) { const int od = good[rng() % good.size()]; if (od == g.di) continue; const long j = barAt(od, g.tm); if (j < 0) continue;
            const double A2 = s.days[od].atr; int m2; const double qv = trade(j, g.dir, std::max(rA * A2, 2.0), x, kk, (x == 4 || x == 5) ? std::max(tgA * A2, 0.25) : 0, m2);
            if (std::isnan(qv)) continue; nt++; tw += qv / A2 * Anow - cost; }
          for (int f = 0; f < NF; ++f) { if (!g.f[f] || s.t[g.i] < busy[w][f][x]) continue; busy[w][f][x] = exitT; Acc& a = acc[ti][ki][w][f][x];
            if (dump && ti == dT && ki == dK && w == dW && f == dF && x == dX) { const int hm = (int)((s.t[g.i] % 86400) / 60); fprintf(dump, "%d,%02d%02d,%d,%.2f,%.2f,%.2f,%.2f,%.1f,%d\n", d.date, hm / 60, hm % 60, g.dir, s.c[g.i], g.R, pts, net, g.A, mins); }
            a.s[e] += net; a.n[e]++; (net > 0 ? a.gw[e] : a.gl[e]) += std::fabs(net); if (nt) { a.tw += net - tw / nt; a.ny++; } } } } } }
  const std::string csv = "research/programs/out/ema721_wave_search_" + sym + ".csv"; FILE* o = std::fopen(csv.c_str(), "w");
  fprintf(o, "tf,k,window,filter,exit,is_n,is_pts,is_pf,is_a,is_b,val_n,val_pts,val_pf,ho_n,ho_pts,ho_pf,twin,per_yr\n");
  printf("Wave search — %s, %ld signals over all TF × k. Cost %.1f. Points at today's ATR (%.0f). Selected on 2010-2020 (n >= 150, PF >= 1.15, both halves > 0):\n", sym.c_str(), nsig, cost, Anow);
  printf("%-4s %-4s %-7s %-10s %-8s | IS n   pts   PF  | VAL21-24 n  pts   PF | HO25-26 n  pts   PF | twin\n", "tf", "k", "window", "filter", "exit");
  int nsel = 0, nval = 0, nho = 0;
  for (int ti = 0; ti < NTF; ++ti) for (int ki = 0; ki < NK; ++ki) for (int w = 0; w < NW; ++w) for (int f = 0; f < NF; ++f) for (int x = 0; x < NX; ++x) {
    const Acc& a = acc[ti][ki][w][f][x]; const double isn = a.n[0] + a.n[1], iss = a.s[0] + a.s[1], isgw = a.gw[0] + a.gw[1], isgl = a.gl[0] + a.gl[1];
    auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
    fprintf(o, "%d,%.1f,%s,%s,%s,%.0f,%.3f,%.3f,%.3f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.3f,%.1f\n", TFS[ti], KS[ki], WN[w], FN[f], XN[x], isn, isn ? iss / isn : 0, pf(isgw, isgl),
            a.n[0] ? a.s[0] / a.n[0] : 0, a.n[1] ? a.s[1] / a.n[1] : 0, a.n[2], a.n[2] ? a.s[2] / a.n[2] : 0, pf(a.gw[2], a.gl[2]), a.n[3], a.n[3] ? a.s[3] / a.n[3] : 0, pf(a.gw[3], a.gl[3]),
            a.ny ? a.tw / a.ny : 0, (isn + a.n[2] + a.n[3]) / 16.3);
    if (!(isn >= 150 && pf(isgw, isgl) >= 1.15 && a.s[0] > 0 && a.s[1] > 0)) continue; nsel++;
    const bool v = a.s[2] > 0 && pf(a.gw[2], a.gl[2]) >= 1.05, h = a.s[3] > 0; nval += v; nho += v && h;
    printf("%-4d %-4.1f %-7s %-10s %-8s | %5.0f %+6.2f %4.2f | %5.0f %+6.2f %4.2f %s | %4.0f %+6.2f %4.2f %s | %+6.2f\n", TFS[ti], KS[ki], WN[w], FN[f], XN[x], isn, iss / isn, pf(isgw, isgl),
           a.n[2], a.n[2] ? a.s[2] / a.n[2] : 0, pf(a.gw[2], a.gl[2]), v ? "ok" : "--", a.n[3], a.n[3] ? a.s[3] / a.n[3] : 0, pf(a.gw[3], a.gl[3]), h ? "ok" : "--", a.ny ? a.tw / a.ny : 0); }
  std::fclose(o); if (dump) std::fclose(dump);
  printf("\n%d of %d configs selected on 2010-2020; %d also pass 2021-24; %d of those also positive 2025-26. (Chance alone: expect a few.)\n", nsel, NTF * NK * NW * NF * NX, nval, nho);
  return 0;
}
