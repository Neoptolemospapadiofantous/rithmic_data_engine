// 2026-10-21 trend_day_strats.cpp (founder: "so we only trade on trend days momentum strategies" → "test flags on strong days only. and
// test more strategies for trend only days"). pass_good_day_funded.cpp's eval (its header follows) and funded pipeline on Apex 50K EOD,
// with these funded strategies, all fixed before the run; "strong so far" at a bar = the path since 09:30 >= 70 % of its range and the
// move >= 0.5 ATR, "moderate" = >= 55 % and >= 0.25 ATR, measured AT the entry bar (no look-ahead); direction = the move since 09:30:
//  FLAGS  F0 every flag-mid flag · F1 only when the day is moderate-up at the flag's entry · F2 only when strong-up at entry
//  TREND-DAY ENTRIES (only on days strong at 10:30, with that direction; stop 0.15 ATR; hold to 15:55 unless noted; once a day)
//   T1 enter at 10:30                       T2 = T1 with a trailing stop 0.20 ATR behind the best price
//   T3 first pullback to the session VWAP after 10:30 (limit at VWAP, until 14:00)
//   T4 first 5m bar after 10:30 that touches the 5m EMA20 and closes back on the trend side (enter at that close, until 14:00)
//   T5 breakout: a stop order 1 tick beyond the day's extreme at 10:30, first fill until 14:00
//   T6 enter at 13:00 if the day is still strong at 13:00 (its own check, not the 10:30 one)
//   T7 enter at 10:00 if strong at 10:00 (its own check)
// Trades: points at today's ATR after 1.5 pts cost, win %, PF, periods, trades / yr, vs a control (the same minute on 5 random other days,
// same direction, stop and exit). Combos C1-C3 add the two lists (a flag and the trend trade can overlap in time — the payout sim
// takes them one after the other, so a combo is a slight under-statement of the open risk). Funded: 1 / 2 / 3 / 5 MNQ on Apex 50K EOD after the good-day eval, 120 days. Usage: trend_day_strats [inputs dir]
// (header of pass_good_day_funded.cpp:)
// 2026-10-21 pass_good_day_funded.cpp (founder: "yes run the funded test", after pass_good_day.cpp). From buying the eval to getting paid:
//  EVAL    pass_good_day.cpp's best rule, fixed: 1 NQ, at 10:30 with the move since 09:30 only on a very straight morning (path >= 70 % of
//          the range since 09:30 and moved >= 0.5 ATR), day stop −$750, out at the eval target; each firm's own eval drawdown (end-of-day
//          or intraday trailing), lock and eval daily loss from payout_sim.hpp; at most 60 trading days
//  FUNDED  the next trading day the funded account starts (payout_sim.hpp run_account: the firm's funded drawdown, daily loss, win days,
//          consistency, minimum profit, caps), 120 trading days, traded one of three ways, fixed before the run:
//            FG the same good-day trade, held to 15:55 (no target), day stop 37.5 pts (= −$750 at 1 NQ)
//            FF the flag-mid flags (flag_tiny_tp.cpp export: bull 3-5m, flag-mid limit, stop flag low − 0.1 ATR, measured move, one at a
//               time across charts) — inputs_20261021/flagmid_trades.csv
//            FT only those flags in the walk-forward top third (the A-like list)
//          at 1 / 2 / 3 / 5 / 10 MNQ ($2 / pt each, cost 1.5 pts = $3 per MNQ per trade); points scaled to today's ATR
//  FIRMS   the plans with no best-day cap in the eval: Apex 50K EOD, Apex 50K intraday, Tradeify Growth 50K (payout_sim.hpp — check the
//          firm's current rules before buying)
// From every 5th trading day 2014-01 → 2025-04: pass %, of the funded accounts paid at all / within 20 trading days / blown before a
// payout, payouts in 120 days; $ per eval bought = pass % × payouts per funded account (before the trader's split and before fees —
// the break-even total fee per eval). Usage: pass_good_day_funded [inputs dir]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 10:30 = 990, 15:55 = 1315.
#include "research/meta.hpp"
#include "paper/payout_sim.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <random>
using namespace lab;
int main(int argc, char** argv) {
  const std::string inDir = argc > 1 ? argv[1] : "research/programs/inputs_20261021"; const double cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr; std::vector<double> O, H, L, C, V; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z{d.date, d.atr, {}, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; std::map<int, size_t> ci; for (size_t i = 0; i < days.size(); ++i) ci[days[i].date] = i;
  // the good-day signal: entry bar index and direction (0 = no signal)
  std::vector<int> gdir(days.size(), 0); std::vector<size_t> gk(days.size(), 0);
  for (size_t di = 0; di < days.size(); ++di) { const DB& z = days[di]; double hi = -1e18, lo = 1e18; size_t k = 0;
    for (; k < z.C.size() && z.TM[k] < 990; ++k) { hi = std::max(hi, z.H[k]); lo = std::min(lo, z.L[k]); }
    if (k == 0 || k >= z.C.size()) continue; const size_t ke = k - 1; const double mv = z.C[ke] - z.O[0], path = hi > lo ? std::fabs(mv) / (hi - lo) : 0;
    if (path >= 0.70 && std::fabs(mv) >= 0.5 * z.atr) { gdir[di] = mv > 0 ? 1 : -1; gk[di] = ke; } }
  // the good-day trade held to 15:55 with a points stop: gross pts, mae, mfe (today's ATR)
  auto fgTrade = [&](size_t di, double stopPts, double& mae, double& mfe) -> double { const DB& z = days[di]; const int dir = gdir[di]; const double f = Anow / z.atr, e = z.C[gk[di]]; mae = mfe = 0;
    for (size_t j = gk[di] + 1; j < z.C.size(); ++j) { if (z.TM[j] >= 1315) return dir * (z.O[j] - e) * f;
      const double adv = (dir > 0 ? e - z.L[j] : z.H[j] - e) * f, fav = (dir > 0 ? z.H[j] - e : e - z.L[j]) * f;
      if (adv >= stopPts) { mae = stopPts; return -stopPts; } mae = std::max(mae, adv); mfe = std::max(mfe, fav); }
    return dir * (z.C.back() - e) * f; };
  // flag-mid trades
  struct FT { int date, top, k; double g, mae, mfe; }; std::vector<FT> fl;
  { std::ifstream f(inDir + "/flagmid_trades.csv"); if (!f) { fprintf(stderr, "missing %s/flagmid_trades.csv (run flag_tiny_tp NQ export)\n", inDir.c_str()); return 1; }
    std::string line; std::getline(f, line); while (std::getline(f, line)) { FT t; if (sscanf(line.c_str(), "%d,%d,%lf,%lf,%lf,%d", &t.date, &t.top, &t.g, &t.mae, &t.mfe, &t.k) == 6) fl.push_back(t); } }

  // ── trend-day strategies: per day a list of trades (gross pts, mae, mfe at today's ATR) ──
  struct Tt { int year; double g, mae, mfe, ctl; };
  enum { NSTR = 13 }; const char* STN[NSTR] = {"F0 flag-mid, every flag", "F1 flag-mid, moderate-up day at entry", "F2 flag-mid, strong-up day at entry",
    "T1 10:30 entry, hold", "T2 10:30 entry, trail 0.2 ATR", "T3 1st pullback to VWAP", "T4 5m EMA20 touch+hold", "T5 breakout of the 10:30 extreme", "T6 13:00 still strong", "T7 10:00 strong", "C1 = F0 + T1 (flags + 10:30 trade)", "C2 = F0 + T5 (flags + breakout)", "C3 = F1 + T1"};
  std::vector<std::vector<Tt>> TD[NSTR]; for (auto& v : TD) v.assign(days.size(), {});
  auto pathAt = [&](const DB& z, size_t k, double& mv) { double hi = -1e18, lo = 1e18; for (size_t j = 0; j <= k; ++j) { hi = std::max(hi, z.H[j]); lo = std::min(lo, z.L[j]); } mv = z.C[k] - z.O[0]; return hi > lo ? std::fabs(mv) / (hi - lo) : 0.0; };
  // run from entry price e at bar k (fill during bar k when fillK: the rest of that bar counts against the stop), stop distance st, trail tr (0 = none)
  auto run = [&](const DB& z, size_t k, double e, int dir, double st, double tr, bool fillK, double& mae, double& mfe) -> double {
    const double f = Anow / z.atr; double stop = e - dir * st, best = e; mae = mfe = 0;
    if (fillK) { const double adv = dir > 0 ? e - z.L[k] : z.H[k] - e; if (adv >= st) { mae = st * f; return -st * f; } mae = std::max(0.0, adv) * f; }
    for (size_t j = k + 1; j < z.C.size(); ++j) { if (z.TM[j] >= 1315) return dir * (z.O[j] - e) * f;
      if (dir * ((dir > 0 ? z.L[j] : z.H[j]) - stop) <= 0) { mae = std::max(mae, dir * (e - stop) * f); return dir * (stop - e) * f; }
      mae = std::max(mae, (dir > 0 ? e - z.L[j] : z.H[j] - e) * f); mfe = std::max(mfe, (dir > 0 ? z.H[j] - e : e - z.L[j]) * f);
      if (tr > 0) { best = dir > 0 ? std::max(best, z.H[j]) : std::min(best, z.L[j]); stop = dir > 0 ? std::max(stop, best - tr) : std::min(stop, best + tr); } }
    return dir * (z.C.back() - e) * f; };
  std::mt19937 rng(20261021);
  auto ctl = [&](size_t di, size_t k, int dir, double stA, double trA) { double s2 = 0; int n2 = 0; const int tm = days[di].TM[k];
    for (int q = 0; q < 10 && n2 < 5; ++q) { const size_t od = rng() % days.size(); if (od == di) continue; const DB& o = days[od]; size_t j = 0; while (j < o.TM.size() && o.TM[j] < tm) ++j; if (j >= o.TM.size() || o.TM[j] > tm + 2) continue;
      double a, b; s2 += run(o, j, o.C[j], dir, stA * o.atr, trA * o.atr, false, a, b) - cost; n2++; } return n2 ? s2 / n2 : NAN; };
  for (auto& t : fl) { auto it = ci.find(t.date); if (it == ci.end()) continue; const DB& z = days[it->second]; if (t.k < 0 || t.k >= (int)z.C.size()) continue;
    double mv; const double p = pathAt(z, t.k, mv); const Tt x{t.date / 10000, t.g, t.mae, t.mfe, NAN};
    TD[0][it->second].push_back(x); if (mv > 0 && p >= 0.55 && mv >= 0.25 * z.atr) TD[1][it->second].push_back(x); if (mv > 0 && p >= 0.70 && mv >= 0.5 * z.atr) TD[2][it->second].push_back(x); }
  for (size_t di = 0; di < days.size(); ++di) { const DB& z = days[di]; const double A = z.atr; const int yr = z.date / 10000;
    auto add = [&](int st, size_t k, double e, int dir, double stA, double trA, bool fillK) { double mae, mfe; const double g = run(z, k, e, dir, stA * A, trA * A, fillK, mae, mfe); TD[st][di].push_back({yr, g, mae, mfe, ctl(di, k, dir, stA, trA)}); };
    auto strongAt = [&](int tm, size_t& ke, int& dir) { size_t k = 0; while (k < z.C.size() && z.TM[k] < tm) ++k; if (k == 0 || k >= z.C.size()) return false; ke = k - 1; double mv; const double p = pathAt(z, ke, mv); dir = mv > 0 ? 1 : -1; return p >= 0.70 && std::fabs(mv) >= 0.5 * A; };
    size_t k0; int dir;
    if (strongAt(1140, k0, dir)) add(8, k0, z.C[k0], dir, 0.15, 0, false);   // T6 at 13:00
    if (strongAt(960, k0, dir)) add(9, k0, z.C[k0], dir, 0.15, 0, false);
    if (!gdir[di]) continue; const size_t k = gk[di]; dir = gdir[di];
    add(3, k, z.C[k], dir, 0.15, 0, false); add(4, k, z.C[k], dir, 0.15, 0.20, false);
    // T3 VWAP pullback
    { double pv = 0, vv = 0; std::vector<double> vw(z.C.size()); for (size_t j = 0; j < z.C.size(); ++j) { const double tp = (z.H[j] + z.L[j] + z.C[j]) / 3, v = std::max(z.V[j], 1.0); pv += tp * v; vv += v; vw[j] = pv / vv; }
      for (size_t j = k + 1; j < z.C.size() && z.TM[j] < 1200; ++j) { const double lim = vw[j - 1]; if (dir * ((dir > 0 ? z.L[j] : z.H[j]) - lim) <= 0) { const double e = dir > 0 ? std::min(z.O[j], lim) : std::max(z.O[j], lim); add(5, j, e, dir, 0.15, 0, true); break; } } }
    // T4 5m EMA20 touch+hold
    { double ema = NAN; for (size_t j = 0; j < z.C.size(); ++j) { if ((z.TM[j] - 930) % 5 != 4) continue; double h5 = -1e18, l5 = 1e18; for (size_t q = j >= 4 ? j - 4 : 0; q <= j; ++q) { h5 = std::max(h5, z.H[q]); l5 = std::min(l5, z.L[q]); }
        const double prev = ema; ema = std::isnan(ema) ? z.C[j] : ema + 2.0 / 21 * (z.C[j] - ema);
        if (j > k && z.TM[j] < 1200 && !std::isnan(prev) && (dir > 0 ? l5 <= prev && z.C[j] > ema : h5 >= prev && z.C[j] < ema)) { add(6, j, z.C[j], dir, 0.15, 0, false); break; } } }
    // T5 breakout of the 10:30 extreme
    { double ex = dir > 0 ? -1e18 : 1e18; for (size_t j = 0; j <= k; ++j) ex = dir > 0 ? std::max(ex, z.H[j]) : std::min(ex, z.L[j]); const double lev = ex + dir * 0.25;
      for (size_t j = k + 1; j < z.C.size() && z.TM[j] < 1200; ++j) if (dir * ((dir > 0 ? z.H[j] : z.L[j]) - lev) >= 0) { const double e = dir > 0 ? std::max(z.O[j], lev) : std::min(z.O[j], lev); add(7, j, e, dir, 0.15, 0, true); break; } } }
  for (size_t di = 0; di < days.size(); ++di) { for (int a : {0, 3}) for (auto& t : TD[a][di]) TD[10][di].push_back(t); for (int a : {0, 7}) for (auto& t : TD[a][di]) TD[11][di].push_back(t); for (int a : {1, 3}) for (auto& t : TD[a][di]) TD[12][di].push_back(t); }
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  printf("TRADES (points at today's ATR after %.1f pts cost): 2010-15 | 2016-20 | 2021-24 | 2025-26 mean (n) · all · win %% · PF · trades/yr · vs control\n", cost);
  for (int st = 0; st < NSTR; ++st) { double m[4] = {0}, c[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, tw = 0, nt = 0; int pos = 0, have = 0;
    for (auto& v : TD[st]) for (auto& t : v) { const double x = t.g - cost; m[era(t.year)] += x; c[era(t.year)]++; sum += x; N++; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); if (!std::isnan(t.ctl)) { tw += x - t.ctl; nt++; } }
    if (N < 20) { printf("  %-40s n %.0f\n", STN[st], N); continue; }
    printf("  %-40s", STN[st]); for (int e = 0; e < 4; ++e) { if (c[e] < 10) { printf("   —    (%4.0f) |", c[e]); continue; } printf(" %+6.2f (%4.0f) |", m[e] / c[e], c[e]); have++; pos += m[e] > 0; }
    printf(" all %+6.2f win %2.0f%% PF %.2f %3.0f/yr", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, N / 16.3); if (nt) printf(" · vs control %+6.2f", tw / nt); else printf(" · (flag control: see flag_tiny_tp)"); printf(" · %d/%d\n", pos, have); }
  const char* FIRMS[1] = {"apex_eod_50k"};
  printf("From eval to payout: pass on good days (1 NQ, very straight morning at 10:30, day stop -$750), then trade the funded account — NQ, starts every 5th trading day 2014-01 → 2025-04.\n");
  printf("Points at today's ATR (%.0f). Payouts are the firm's payout amounts BEFORE the trader's split and BEFORE eval / activation fees. Rules from payout_sim.hpp (2026-10 table) — check the firm's current rules.\n", Anow);
  for (const char* fn : FIRMS) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == fn) P = &p; if (!P) continue;
    // ── eval, minute by minute, one account per start ──
    std::vector<int> passDay(days.size(), -1); int starts = 0, pass = 0, fail = 0; std::vector<int> dtp;
    for (size_t s0 = 0; s0 < days.size(); s0 += 5) { if (days[s0].date < 20140101 || days[s0].date > 20250430) continue; starts++;
      double bal = 0, peak = 0, floor = -P->drawdown; int res = 0; size_t di = s0;
      for (; di < days.size() && di < s0 + 60 && !res; ++di) { if (!gdir[di]) continue; const DB& z = days[di]; const int dir = gdir[di]; const double f = Anow / z.atr, e = z.C[gk[di]], per = 20; double dp = 0;
        for (size_t j = gk[di] + 1; j < z.C.size(); ++j) { auto pnl = [&](double px) { return (dir * (px - e) * f - cost) * per; };
          if (z.TM[j] >= 1315) { dp = pnl(z.O[j]); break; }
          const double worst = pnl(dir > 0 ? z.L[j] : z.H[j]), best = pnl(dir > 0 ? z.H[j] : z.L[j]);
          if (P->intraday_trail) { peak = std::max(peak, bal + best); floor = std::max(floor, std::min(peak - P->drawdown, P->lock_above_start)); }
          if (bal + worst <= floor) { res = -1; break; }
          const double cut = P->eval_daily_loss > 0 ? std::min(750.0, P->eval_daily_loss) : 750.0; if (worst <= -cut) { dp = -cut; break; }
          if (bal + best >= P->eval_target) { res = 1; break; }
          dp = pnl(z.C[j]); }
        if (res) break; bal += dp; peak = std::max(peak, bal); floor = std::max(floor, std::min(peak - P->drawdown, P->lock_above_start)); if (bal <= floor) { res = -1; break; } }
      if (res == 1) { pass++; passDay[s0] = (int)di; dtp.push_back((int)(di - s0 + 1)); } else if (res == -1) fail++; }
    std::sort(dtp.begin(), dtp.end());
    printf("\n######## %s — EVAL: pass %.0f%% · fail %.0f%% · still open after 60 days %.0f%% · median %d trading days to pass (%d starts)\n", fn, 100.0 * pass / starts, 100.0 * fail / starts,
           100.0 * (starts - pass - fail) / starts, dtp.empty() ? -1 : dtp[dtp.size() / 2], starts);
    // ── funded ──
    for (int st = 0; st < NSTR; ++st) for (int q : {1, 2, 3, 5}) {
      std::vector<std::vector<payout::Trade>> D(days.size());
      for (size_t di = 0; di < days.size(); ++di) for (auto& t : TD[st][di]) D[di].push_back({q * 2 * (t.g - cost), -q * 2 * (t.mae + cost), q * 2 * t.mfe});
      std::vector<const std::vector<payout::Trade>*> ptr(days.size()); for (size_t i = 0; i < days.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
      int nF = 0, paid = 0, paid20 = 0, blown = 0; double sum = 0, npay = 0;
      for (size_t s0 = 0; s0 < days.size(); ++s0) { if (passDay[s0] < 0) continue; const int f0 = passDay[s0] + 1; if (f0 >= (int)days.size()) continue; nF++;
        const payout::RunOut o = payout::run_account(*P, ptr, f0, 120); paid += o.paid; paid20 += o.paid && o.first_pay <= 20; blown += o.breach && !o.paid; sum += o.paid_sum; npay += o.payouts; }
      if (!nF) continue; const double perF = sum / nF, perEval = perF * pass / starts;
      printf("  %-40s %2d MNQ: funded accounts paid %3.0f%% (within 20 days %3.0f%%) · blown before a payout %3.0f%% · %.1f payouts, $%5.0f per funded account in 120 days · $%5.0f per eval bought (break-even fee)\n",
             STN[st], q, 100.0 * paid / nF, 100.0 * paid20 / nF, 100.0 * blown / nF, npay / nF, perF, perEval); } }
  return 0;
}
