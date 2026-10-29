// 2026-10-21 vol_range_bias.cpp (founder: "when there is volume and the range I think on the 1m there is some kind of bias"). A 1-minute
// bar with unusual VOLUME and an unusual RANGE: does price lean one way after it? Fixed before the run:
//  EVENT   relative volume = the bar's volume / the mean volume of the SAME clock minute over the previous 20 sessions (removes the
//          open / close U-shape); relative range the same for high − low. Levels: volume >= 2 / 3 / 5×, range >= 2 / 3×.
//  CLOSE   where the bar closes along its own direction (dir = sign(close − open)): STRONG = top quarter of the bar on its side,
//          MIDDLE, REJECTED = close back in the first half (a wick) — the absorption case
//  WINDOW  Asia 18:00-03:00 · London 03:00-08:25 · releases 08:25-09:30 · NY open 09:30-10:30 · NY 10:30-15:30
//  BIAS    signed move in the bar's direction after 5 / 15 / 30 / 60 min (ATR of the day; > 0 = continues, < 0 = reverses), against the
//          same number for EVERY bar of that window (the ordinary 1-minute follow-through) — the event's bias = the difference
//  TRADE   at the bar's close, FOLLOW and FADE: stop beyond the bar's other extreme + 1 tick (risk = the bar), target 1R / 2R, or out
//          after 30 min (stop kept); flat at the window's session end (03:00 / 09:30 / 15:55). R after cost NQ 1.5 / ES 0.5 pts. One
//          position at a time per cell. Control: same minute, same direction and the same risk in ATR on 5 random other days.
//  REGIME  (founder, mid-run: "and the correct regime. so New York should work") NY windows only, known at the bar: G1 the session path
//          since 09:30 is straight (|close − 09:30 open| / range since 09:30 >= 0.55) and the bar goes WITH the session move · G2 straight,
//          bar AGAINST the move · G3 choppy path (< 0.30) · G4 VIX prior close >= 20 · G5 VIX < 15 (G0 = every event)
// Periods 2010-15 | 2016-20 | 2021-24 | 2025-26. Usage: vol_range_bias [NQ|ES]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 03:00 = 540, 08:25 = 865, 09:30 = 930, 10:30 = 990, 15:30 = 1290, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <deque>
#include <random>
using namespace lab;
enum { NW = 5, NV = 3, NR = 2, NC = 3, NH = 4, NT = 3, NS = 2, NG = 6 };
static const char* GN[NG] = {"G0 any day", "G1 straight, with", "G2 straight, against", "G3 choppy", "G4 VIX>=20", "G5 VIX<15"};
static const char* WN[NW] = {"Asia", "London", "releases", "NY open", "NY 10:30-15:30"};
static const double VL[NV] = {2, 3, 5}, RL[NR] = {2, 3};
static const char* CN[NC] = {"STRONG close", "MIDDLE close", "REJECTED (wick)"};
static const int HM[NH] = {5, 15, 30, 60};
static const char* TN[NT] = {"1R", "2R", "30 min"};
static const char* SN[NS] = {"FOLLOW", "FADE"};
struct M { double s[4] = {0}, n[4] = {0}; void add(int e, double x) { if (!std::isnan(x)) { s[e] += x; n[e]++; } }
  double all() const { double a = 0, b = 0; for (int e = 0; e < 4; ++e) { a += s[e]; b += n[e]; } return b ? a / b : NAN; } double N() const { return n[0] + n[1] + n[2] + n[3]; } };
struct T { M r; double w = 0, gw = 0, gl = 0, tw = 0, ntw = 0; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0 && d.date <= 20261016) good.push_back(di); }
  std::vector<char> isGood(s.days.size(), 0); for (int di : good) isGood[di] = 1;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  auto winOf = [](int tm) { return tm < 540 ? 0 : tm < 865 ? 1 : tm < 930 ? 2 : tm < 990 ? 3 : tm < 1290 ? 4 : -1; };
  auto endOf = [](int tm) { return tm < 540 ? 540 : tm < 930 ? 930 : 1315; };
  // per day: the bar index of each clock minute (or -1)
  std::vector<std::vector<int>> at(s.days.size(), std::vector<int>(1380, -1));
  for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const int tm = tday_min(s.t[i]); if (tm >= 0 && tm < 1380) at[di][tm] = i; }
  // move in direction dir from bar i's close after h minutes (NAN if the session window ends first)
  auto fwd = [&](size_t i, int dir, int h, int endTm) -> double { const int di = s.day_of[i]; const int tm = tday_min(s.t[i]) + h; if (tm >= endTm || tm >= 1380) return NAN;
    for (int k = tm; k >= tm - 2 && k > tday_min(s.t[i]); --k) if (at[di][k] >= 0) return dir * (s.c[at[di][k]] - s.c[i]); return NAN; };
  // a trade from bar i's close: stop distance stD, target tgD (tgD <= 0: none), time exit `mins` (0 = none); flat at endTm
  auto trade = [&](size_t i, int dir, double stD, double tgD, int mins, int endTm, int64_t& tex) -> double { const int di = s.day_of[i]; const double e = s.c[i]; const int64_t t0 = s.t[i];
    for (size_t j = i + 1; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]); tex = s.t[j]; if (tm >= endTm) return dir * (s.o[j] - e);
      const double adv = dir > 0 ? e - s.l[j] : s.h[j] - e, fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv >= stD) return -stD; if (tgD > 0 && fav >= tgD) return tgD; if (mins && s.t[j] - t0 >= mins * 60) return dir * (s.c[j] - e); }
    return NAN; };
  static M base[NW][NH], ev[NW][NG][NV][NR][NC][NH]; static T tr[NW][NG][NV][NR][NC][NS][NT]; static int64_t busy[NW][NG][NV][NR][NC][NS][NT];
  int sesDay = -1; double nyo = NAN, nyh = 0, nyl = 0;
  long nEv = 0; std::mt19937 rng(20261021);
  std::vector<std::deque<double>> hv(1380), hr(1380);   // previous sessions' volume / range of each clock minute
  int lastDay = -1;
  std::vector<std::pair<int, std::pair<double, double>>> today;   // (minute, (vol, range)) of the current day, pushed into hv/hr at day end
  for (size_t i = 0; i < s.size(); ++i) {
    const int di = s.day_of[i]; if (di < 0) continue;
    if (di != lastDay) { for (auto& x : today) { auto& a = hv[x.first]; auto& b = hr[x.first]; a.push_back(x.second.first); b.push_back(x.second.second); if (a.size() > 20) a.pop_front(); if (b.size() > 20) b.pop_front(); }
      today.clear(); lastDay = di; }
    const int tm = tday_min(s.t[i]); if (tm < 0 || tm >= 1380) continue; const double vol = s.v[i], rng1 = s.h[i] - s.l[i];
    today.push_back({tm, {vol, rng1}});
    if (tm >= 930) { if (sesDay != di) { sesDay = di; nyo = s.o[i]; nyh = s.h[i]; nyl = s.l[i]; } nyh = std::max(nyh, (double)s.h[i]); nyl = std::min(nyl, (double)s.l[i]); }
    if (!isGood[di]) continue; const int w = winOf(tm); if (w < 0) continue; const Day& d = s.days[di]; const double A = d.atr; const int endTm = endOf(tm);
    const int dir = s.c[i] > s.o[i] ? 1 : s.c[i] < s.o[i] ? -1 : 0; if (!dir) continue; const int e = era(d.date / 10000);
    for (int h = 0; h < NH; ++h) base[w][h].add(e, fwd(i, dir, HM[h], endTm) / A);
    if (hv[tm].size() < 20) continue; double mv = 0, mr = 0; for (double x : hv[tm]) mv += x; for (double x : hr[tm]) mr += x; mv /= 20; mr /= 20;
    if (!(mv > 0) || !(mr > 0)) continue; const double rv = vol / mv, rr = rng1 / mr; if (rv < VL[0] || rr < RL[0] || rng1 < 1.0) continue;
    nEv++; const double loc = dir > 0 ? (s.c[i] - s.l[i]) / rng1 : (s.h[i] - s.c[i]) / rng1; const int cl = loc >= 0.75 ? 0 : loc >= 0.5 ? 1 : 2;
    double f[NH]; for (int h = 0; h < NH; ++h) f[h] = fwd(i, dir, HM[h], endTm) / A;
    bool g[NG] = {true, false, false, false, false, false};
    if (w >= 3 && sesDay == di && nyh > nyl) { const double path = std::fabs(s.c[i] - nyo) / (nyh - nyl); const bool with = dir * (s.c[i] - nyo) > 0;
      g[1] = path >= 0.55 && with; g[2] = path >= 0.55 && !with; g[3] = path < 0.30; g[4] = d.vix >= 20; g[5] = d.vix < 15; }
    for (int gg = 0; gg < NG; ++gg) if (g[gg]) for (int v = 0; v < NV; ++v) if (rv >= VL[v]) for (int r = 0; r < NR; ++r) if (rr >= RL[r]) {
      for (int h = 0; h < NH; ++h) ev[w][gg][v][r][cl][h].add(e, f[h]);
      for (int sd = 0; sd < NS; ++sd) { const int td = sd == 0 ? dir : -dir; const double stD = rng1 + 0.25;
        for (int t = 0; t < NT; ++t) { if (s.t[i] < busy[w][gg][v][r][cl][sd][t]) continue;
          const double tg = t == 0 ? stD : t == 1 ? 2 * stD : 0; const int mins = t == 2 ? 30 : 0; int64_t ex = s.t[i] + 60, ex2;
          const double p = trade(i, td, stD, tg, mins, endTm, ex); if (std::isnan(p)) continue; const double R = (p - cost) / stD;
          busy[w][gg][v][r][cl][sd][t] = ex;
          T& x = tr[w][gg][v][r][cl][sd][t]; x.r.add(e, R); x.w += R > 0; (R > 0 ? x.gw : x.gl) += std::fabs(R);
          if (v == 0 || gg == 0) { double cs = 0; int nc = 0; for (int z = 0; z < 10 && nc < 5; ++z) { const int od = good[rng() % good.size()]; if (od == di || at[od][tm] < 0) continue; const size_t j = at[od][tm]; const double A2 = s.days[od].atr, st2 = stD / A * A2;
            const double q = trade(j, td, st2, t == 2 ? 0 : tg / A * A2, mins, endTm, ex2); if (std::isnan(q)) continue; cs += (q - cost * A2 / A) / st2; nc++; }
          if (nc) { x.tw += R - cs / nc; x.ntw++; } } } } } }
  printf("1-minute bars with unusual volume AND range — %s, %ld events (volume >= 2x and range >= 2x the same minute's 20-session mean). Cost %.1f pts.\n", sym.c_str(), nEv, cost);
  printf("BIAS: mean move in the bar's direction (ATR) after 5 / 15 / 30 / 60 min, minus the same for every bar of the window. Periods 2010-15 | 2016-20 | 2021-24 | 2025-26.\n");
  for (int w = 0; w < NW; ++w) { printf("\n######## %s — every bar: 5m %+.4f  15m %+.4f  30m %+.4f  60m %+.4f ATR (n %.0f)\n", WN[w], base[w][0].all(), base[w][1].all(), base[w][2].all(), base[w][3].all(), base[w][0].N());
    for (int gg = 0; gg < NG; ++gg) for (int v = 0; v < NV; ++v) for (int r = 0; r < NR; ++r) for (int c = 0; c < NC; ++c) { const M* m = ev[w][gg][v][r][c]; if (m[0].N() < 40) continue;
      printf("  %-20s vol>=%.0fx range>=%.0fx %-16s n %5.0f  bias", GN[gg], VL[v], RL[r], CN[c], m[0].N()); for (int h = 0; h < NH; ++h) printf("  %2dm %+.4f", HM[h], m[h].all() - base[w][h].all());
      printf("  | 30m by period"); int pos = 0, have = 0; for (int e = 0; e < 4; ++e) { if (m[2].n[e] < 15) { printf("    —   "); continue; } const double b = base[w][2].n[e] ? base[w][2].s[e] / base[w][2].n[e] : 0; const double x = m[2].s[e] / m[2].n[e] - b; printf(" %+.4f", x); have++; pos += x > 0; }
      printf("  (%d/%d continue)\n", pos, have); } }
  printf("\n######## TRADES (R after cost; stop beyond the bar, risk = the bar's range) — cells with >= 60 trades\n");
  for (int w = 0; w < NW; ++w) for (int gg = 0; gg < NG; ++gg) for (int v = 0; v < NV; ++v) for (int r = 0; r < NR; ++r) for (int c = 0; c < NC; ++c) for (int sd = 0; sd < NS; ++sd) for (int t = 0; t < NT; ++t) {
    const T& x = tr[w][gg][v][r][c][sd][t]; const double N = x.r.N(); if (N < 60) continue; int pos = 0, have = 0;
    printf("  %-14s %-20s vol>=%.0fx rng>=%.0fx %-16s %-6s %-6s", WN[w], GN[gg], VL[v], RL[r], CN[c], SN[sd], TN[t]);
    for (int e = 0; e < 4; ++e) { if (x.r.n[e] < 15) { printf("   —   (%4.0f) |", x.r.n[e]); continue; } const double m = x.r.s[e] / x.r.n[e]; printf(" %+.2f (%4.0f) |", m, x.r.n[e]); have++; pos += m > 0; }
    printf(" all %+.3f win %2.0f%% PF %.2f %4.0f/yr · vs control %+.3f · %d/%d\n", x.r.all(), 100 * x.w / N, x.gl > 0 ? x.gw / x.gl : 99.0, N / 16.3, x.ntw ? x.tw / x.ntw : NAN, pos, have); }
  { long n = 0; for (auto& a : tr) for (auto& b : a) for (auto& c : b) for (auto& d2 : c) for (auto& e2 : d2) for (auto& f2 : e2) for (auto& g2 : f2) if (g2.r.N() >= 60) n++; printf("\n(trade cells shown: %ld — read any single cell against that many looks)\n", n); }
  return 0;
}
