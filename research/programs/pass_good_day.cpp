// 2026-10-21 pass_good_day.cpp (founder: "lets say I need 3k 2 contracts or 1 contract to pass. can be done 1st day", "so if its a good
// trend day", "we keep passing", "blowing account on a bad day", "and manage that", "we want to be passing accounts on a good day").
// A $3,000 evaluation with a $2,000 end-of-day trailing drawdown and no best-day cap (Apex 50K EOD in payout_sim.hpp: the floor follows
// the end-of-day balance, touching it intraday fails, floor locks at start + $100) attempted ONLY on days that look like trend days,
// one trade a day, with the session's move, held until the account passes (out at +$3,000), the day's loss stop, the floor, or 15:55.
// Fixed before the run:
//  WHEN   go at 10:00 / 10:30 / 11:00 / 12:00 with the move since 09:30 (long if the price is above the 09:30 open)
//  GOOD   G0 every day (no filter) · G1 straight (|move| >= 55 % of the range since 09:30) and moved >= 0.25 ATR · G2 straight >= 70 % and
//         moved >= 0.5 ATR · G3 = G1 + the gap on the same side · G4 = G1 + VIX prior close >= 20 · CF the G1 days with the direction
//         decided by a coin flip (what the direction is worth)
//  DAY    the day's loss stop: none (only the floor) / −$1,000 / −$750 / −$500 — a bad day ends early, the account lives to the next one
//  SIZE   1 NQ ($20 / pt) and 2 NQ; points scaled to today's ATR (a 150-pt day today was a 20-pt day in 2014); cost 1.5 pts / NQ
// From every trading day 2014-01 → 2025-10, at most 60 trading days: pass %, fail %, still open, median trading days to pass,
// passed on the first attempt, average attempts (trades) used, pass % for starts in 2014-19 vs 2020-25. Usage: pass_good_day [NQ]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 10:00 = 960, 10:30 = 990, 11:00 = 1020, 12:00 = 1080, 15:55 = 1315.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NT = 4, NG = 6, NL = 4, NQ = 2 };
static const int TT[NT] = {960, 990, 1020, 1080};
static const char* TN[NT] = {"10:00", "10:30", "11:00", "12:00"};
static const char* GN[NG] = {"G0 every day", "G1 straight+0.25ATR", "G2 straight70+0.5ATR", "G3 G1+gap with", "G4 G1+VIX>=20", "CF G1, coin-flip dir"};
static const double LS[NL] = {0, 1000, 750, 500};
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double ptv = 20, cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr, pc, vix; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z{d.date, d.atr, d.pc, d.vix, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; std::mt19937 rng(20261021);
  // per day, time, group: does it signal, and the direction
  struct Sg { bool on[NT][NG]; int dir[NT][NG]; size_t k[NT]; };
  std::vector<Sg> sg(days.size());
  for (size_t di = 0; di < days.size(); ++di) { const DB& z = days[di]; Sg& g = sg[di]; const double A = z.atr;
    for (int t = 0; t < NT; ++t) { for (int q = 0; q < NG; ++q) { g.on[t][q] = false; g.dir[t][q] = 0; } g.k[t] = 0;
      double hi = -1e18, lo = 1e18; size_t k = 0; for (; k < z.C.size() && z.TM[k] < TT[t]; ++k) { hi = std::max(hi, z.H[k]); lo = std::min(lo, z.L[k]); }
      if (k == 0 || k >= z.C.size()) continue; const size_t ke = k - 1; g.k[t] = ke; const double mv = z.C[ke] - z.O[0], path = hi > lo ? std::fabs(mv) / (hi - lo) : 0;
      const int dir = mv > 0 ? 1 : mv < 0 ? -1 : 0; if (!dir) continue; const double gap = std::isnan(z.pc) ? 0 : z.O[0] - z.pc;
      const bool g1 = path >= 0.55 && std::fabs(mv) >= 0.25 * A, g2 = path >= 0.70 && std::fabs(mv) >= 0.5 * A;
      const bool on[NG] = {true, g1, g2, g1 && dir * gap > 0, g1 && z.vix >= 20, g1};
      for (int q = 0; q < NG; ++q) { g.on[t][q] = on[q]; g.dir[t][q] = q == 5 ? ((rng() & 1) ? 1 : -1) : dir; } } }
  printf("Passing a $3,000 / $2,000 EOD-trailing evaluation on good days only — %s, %zu NY days, starts 2014-01 → 2025-10, <= 60 trading days.\n", sym.c_str(), days.size());
  printf("One trade a day at the given time with the move since 09:30, held to +$3,000 (pass) / the day's loss stop / the floor (fail) / 15:55. Points at today's ATR (%.0f), cost %.1f pts / NQ.\n", Anow, cost);
  for (int qz = 1; qz <= NQ; ++qz) { printf("\n######## %d NQ ($%.0f per point; +$3,000 = %.0f pts, $2,000 = %.0f pts)\n", qz, qz * ptv, 3000 / (qz * ptv), 2000 / (qz * ptv));
    for (int t = 0; t < NT; ++t) for (int q = 0; q < NG; ++q) for (int ls = 0; ls < NL; ++ls) {
      int starts = 0, pass = 0, fail = 0, first = 0, hs[2] = {0, 0}, hp[2] = {0, 0}; std::vector<int> dtp; double att = 0; long sigDays = 0;
      for (size_t di = 0; di < days.size(); ++di) sigDays += sg[di].on[t][q];
      for (size_t s0 = 0; s0 < days.size(); ++s0) { if (days[s0].date < 20140101 || days[s0].date > 20251031) continue; starts++; const int hf = days[s0].date >= 20200101; hs[hf]++;
        double bal = 0, peak = 0, floor = -2000; int tries = 0; bool done = false;
        for (size_t di = s0; di < days.size() && di < s0 + 60 && !done; ++di) { const Sg& g = sg[di]; if (!g.on[t][q]) continue; const DB& z = days[di]; const int dir = g.dir[t][q]; const size_t k0 = g.k[t];
          const double f = Anow / z.atr, e = z.C[k0], per = qz * ptv; tries++; double dp = 0; int out = 0;
          for (size_t j = k0 + 1; j < z.C.size(); ++j) { auto pnl = [&](double px) { return (dir * (px - e) * f - cost) * per; };
            if (z.TM[j] >= 1315) { dp = pnl(z.O[j]); break; }
            const double worst = pnl(dir > 0 ? z.L[j] : z.H[j]), best = pnl(dir > 0 ? z.H[j] : z.L[j]);
            if (bal + worst <= floor) { out = -1; break; }                                    // floor first (conservative)
            if (LS[ls] > 0 && worst <= -LS[ls]) { dp = -LS[ls]; break; }
            if (bal + best >= 3000) { out = 1; break; }
            dp = pnl(z.C[j]); }
          if (out == -1) { fail++; done = true; break; }
          if (out == 1) { pass++; hp[hf]++; first += tries == 1; dtp.push_back((int)(di - s0 + 1)); att += tries; done = true; break; }
          bal += dp; peak = std::max(peak, bal); floor = std::max(floor, std::min(peak - 2000, 100.0)); if (bal <= floor) { fail++; done = true; break; } } }
      std::sort(dtp.begin(), dtp.end()); const int open = starts - pass - fail;
      printf("  %s %-22s day stop %-6s signal on %4.1f%% of days · PASS %3.0f%% (1st try %3.0f%%) · FAIL %3.0f%% · open %3.0f%% · median %3d days to pass · %.1f trades per pass · PASS 2014-19 %3.0f%% / 2020-25 %3.0f%%\n",
             TN[t], GN[q], LS[ls] > 0 ? ("-$" + std::to_string((int)LS[ls])).c_str() : "none", 100.0 * sigDays / days.size(), 100.0 * pass / starts, 100.0 * first / starts, 100.0 * fail / starts, 100.0 * open / starts,
             dtp.empty() ? -1 : dtp[dtp.size() / 2], pass ? att / pass : 0.0, hs[0] ? 100.0 * hp[0] / hs[0] : 0.0, hs[1] ? 100.0 * hp[1] / hs[1] : 0.0); } }
  return 0;
}
