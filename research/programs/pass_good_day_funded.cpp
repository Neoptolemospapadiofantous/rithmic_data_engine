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
using namespace lab;
int main(int argc, char** argv) {
  const std::string inDir = argc > 1 ? argv[1] : "research/programs/inputs_20261021"; const double cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z{d.date, d.atr, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
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
  struct FT { int date, top; double g, mae, mfe; }; std::vector<FT> fl;
  { std::ifstream f(inDir + "/flagmid_trades.csv"); if (!f) { fprintf(stderr, "missing %s/flagmid_trades.csv (run flag_tiny_tp NQ export)\n", inDir.c_str()); return 1; }
    std::string line; std::getline(f, line); while (std::getline(f, line)) { FT t; if (sscanf(line.c_str(), "%d,%d,%lf,%lf,%lf", &t.date, &t.top, &t.g, &t.mae, &t.mfe) >= 5) fl.push_back(t); } }
  const char* FIRMS[3] = {"apex_eod_50k", "apex_intraday_50k", "growth_50k"};
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
    const char* SN[3] = {"FG good-day trade", "FF flag-mid, all bull flags", "FT flag-mid, top third (A-like)"};
    for (int st = 0; st < 3; ++st) for (int q : {1, 2, 3, 5, 10}) {
      std::vector<std::vector<payout::Trade>> D(days.size());
      if (st == 0) for (size_t di = 0; di < days.size(); ++di) { if (!gdir[di]) continue; double mae, mfe; const double g = fgTrade(di, 37.5, mae, mfe); D[di].push_back({q * 2 * (g - cost), -q * 2 * (mae + cost), q * 2 * mfe}); }
      else for (auto& t : fl) { if (st == 2 && !t.top) continue; auto it = ci.find(t.date); if (it == ci.end()) continue; D[it->second].push_back({q * 2 * (t.g - cost), -q * 2 * (t.mae + cost), q * 2 * t.mfe}); }
      std::vector<const std::vector<payout::Trade>*> ptr(days.size()); for (size_t i = 0; i < days.size(); ++i) ptr[i] = D[i].empty() ? nullptr : &D[i];
      int nF = 0, paid = 0, paid20 = 0, blown = 0; double sum = 0, npay = 0;
      for (size_t s0 = 0; s0 < days.size(); ++s0) { if (passDay[s0] < 0) continue; const int f0 = passDay[s0] + 1; if (f0 >= (int)days.size()) continue; nF++;
        const payout::RunOut o = payout::run_account(*P, ptr, f0, 120); paid += o.paid; paid20 += o.paid && o.first_pay <= 20; blown += o.breach && !o.paid; sum += o.paid_sum; npay += o.payouts; }
      if (!nF) continue; const double perF = sum / nF, perEval = perF * pass / starts;
      printf("  %-32s %2d MNQ: funded accounts paid %3.0f%% (within 20 days %3.0f%%) · blown before a payout %3.0f%% · %.1f payouts, $%5.0f per funded account in 120 days · $%5.0f per eval bought (break-even fee)\n",
             SN[st], q, 100.0 * paid / nF, 100.0 * paid20 / nF, 100.0 * blown / nF, npay / nF, perF, perEval); } }
  return 0;
}
