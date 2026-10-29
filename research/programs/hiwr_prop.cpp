// 2026-10-21 hiwr_prop.cpp (founder: "the thing is that we can get payouts fast" → "different strategies like this one" → "yes test
// them all"). High-win-rate setups, each judged as a trade AND through the Tradeify payout rules, so a 90 % win rate is weighed against
// the rare big loss. NQ (or ES) 1m, NY session 09:30-15:55, entries 09:45-15:00 at a 1m close, all rules fixed before the run:
//  S1 VWAP 2 SD FADE       close beyond session VWAP ± 2 SD (volume-weighted SD of the typical price from 09:30) → fade it
//  S2 VWAP 2.5 SD FADE     the same at 2.5 SD
//  S3 FAILED OR BREAK      after 10:00 a bar trades beyond the 09:30-10:00 range and closes back inside → fade it; once per side a day
//  S4 GAP FIRST PULLBACK   gap >= 0.2 ATR (09:30 open vs prior close); the first bar from 09:35 that touches the session VWAP and closes
//                          on the gap's side of it → enter with the gap; once a day
//  S5 RSI-2 DIP IN TREND   price above the 09:30 open and VWAP (below both = short) and the 5m RSI(2) closes < 10 (> 90) → with trend
//  S6 STRETCH FADE         close more than 0.5 ATR from the 09:30 open, the last 5 bars all in that direction → fade it
// EXITS  X1 TP 0.03 ATR, SL 10× (0.3 ATR)  X2 TP 0.05 ATR, SL 10×  X3 TP 0.05 ATR, SL 5×  X4 the setup's natural target (VWAP for S1/S2/S6,
//        the range middle for S3, the session high / low so far for S4, the high / low of the last 5 bars for S5) with a stop the same
//        distance away (1:1; skipped when the target is < 0.02 ATR away). Flat 15:55. At most 3 trades a day, one at a time, per cell.
// Points scaled to today's ATR, cost NQ 1.5 / ES 0.5 pts at today's size. Control: the same minute, direction and distances on 5 random
// other days. PROP: Tradeify Select Flex 50K / Select Daily 50K (payout_sim.hpp plans), 1 / 2 / 3 / 5 micros ($2 / pt NQ, $5 / pt ES
// micro = MES), cost per micro = cost pts × $ per pt, each trade's worst open loss tested against the floor, $165 per eval, 2014-2025:
//   one account from every 5th day: eval pass %, median days to pass, funded accounts paid within 20 trading days;
//   5 accounts (all on the same trades — hedging banned) opened one a week, 12 months from every month start: mean net, % of years > 0.
// Periods 2010-15 | 2016-20 | 2021-24 | 2025-26. Usage: hiwr_prop [NQ|ES]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:35 = 935, 09:45 = 945, 10:00 = 960, 15:00 = 1260, 15:55 = 1315.
#include "research/meta.hpp"
#include "paper/payout_sim.hpp"
#include <cstdio>
#include <map>
#include <numeric>
#include <random>
using namespace lab;
enum { NS = 6, NX = 4 };
static const char* SN[NS] = {"S1 VWAP 2SD fade", "S2 VWAP 2.5SD fade", "S3 failed OR break", "S4 gap 1st pullback", "S5 RSI-2 dip in trend", "S6 stretch fade"};
static const char* XN[NX] = {"X1 TP.03 SL10x", "X2 TP.05 SL10x", "X3 TP.05 SL5x", "X4 natural 1:1"};
struct Tr { int date, year; double atr, pts, mae, mfe, twin; bool tw; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double cost = sym == "ES" ? 0.5 : 1.5, usdPt = sym == "ES" ? 5.0 : 2.0;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DayB { int date; double atr, pc; std::vector<double> O, H, L, C, V; std::vector<int> TM; };
  std::vector<DayB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DayB z{d.date, d.atr, d.pc, {}, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr;
  // trade from bar k's close of day z, direction dir, stop / target distances (points); flat 15:55; returns gross pts, mae, mfe (>= 0), exit bar
  auto sim = [&](const DayB& z, size_t k, int dir, double stD, double tgD, double& mae, double& mfe, size_t& ko) -> double {
    const double e = z.C[k]; mae = mfe = 0; ko = z.C.size() - 1;
    for (size_t j = k + 1; j < z.C.size(); ++j) { if (z.TM[j] >= 1315) { ko = j; return dir * (z.O[j] - e); }
      const double adv = dir > 0 ? e - z.L[j] : z.H[j] - e, fav = dir > 0 ? z.H[j] - e : e - z.L[j];
      if (adv >= stD) { mae = stD; ko = j; return -stD; } mae = std::max(mae, adv);
      if (fav >= tgD) { mfe = tgD; ko = j; return tgD; } mfe = std::max(mfe, fav); }
    return dir * (z.C.back() - e); };
  auto minuteIdx = [&](const DayB& z, int tm) -> long { for (size_t k = 0; k < z.TM.size(); ++k) if (z.TM[k] >= tm) return z.TM[k] <= tm + 2 ? (long)k : -1; return -1; };
  static std::vector<Tr> T[NS][NX]; std::mt19937 rng(20261021);
  for (size_t di = 0; di < days.size(); ++di) { const DayB& z = days[di]; const size_t n = z.C.size(); const double A = z.atr;
    std::vector<double> vw(n), sd(n); { double pv = 0, vv = 0, p2 = 0; for (size_t k = 0; k < n; ++k) { const double tp = (z.H[k] + z.L[k] + z.C[k]) / 3, v = std::max(z.V[k], 1.0);
        pv += tp * v; vv += v; p2 += tp * tp * v; vw[k] = pv / vv; sd[k] = std::sqrt(std::max(0.0, p2 / vv - vw[k] * vw[k])); } }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && z.TM[k] < 960; ++k) { orh = std::max(orh, z.H[k]); orl = std::min(orl, z.L[k]); }
    const double gap = std::isnan(z.pc) ? 0 : (z.O[0] - z.pc) / A; const int gdir = gap >= 0.2 ? 1 : gap <= -0.2 ? -1 : 0;
    // 5m RSI(2) at each 5m close (index of the last 1m bar of the 5m bar)
    std::vector<double> rsi(n, NAN); { double ag = NAN, al = NAN, pc5 = NAN, c5 = NAN; for (size_t k = 0; k < n; ++k) { c5 = z.C[k]; if ((z.TM[k] - 930) % 5 != 4) continue;
        if (!std::isnan(pc5)) { const double ch = c5 - pc5, g = std::max(ch, 0.0), l = std::max(-ch, 0.0); ag = std::isnan(ag) ? g : (ag + g) / 2; al = std::isnan(al) ? l : (al + l) / 2; rsi[k] = al > 0 ? 100 - 100 / (1 + ag / al) : 100; }
        pc5 = c5; } }
    // signals: per setup, a list of (bar, dir, natural target price)
    struct Sig { size_t k; int dir; double nat; }; std::vector<Sig> sig[NS];
    bool orDone[2] = {false, false}, gapDone = false; double hiSo = -1e18, loSo = 1e18;
    for (size_t k = 0; k < n; ++k) { hiSo = std::max(hiSo, z.H[k]); loSo = std::min(loSo, z.L[k]); const int tm = z.TM[k]; if (tm < 935 || tm >= 1260) continue; const double c = z.C[k];
      if (tm >= 945 && sd[k] > 0) { if (c > vw[k] + 2 * sd[k]) sig[0].push_back({k, -1, vw[k]}); if (c < vw[k] - 2 * sd[k]) sig[0].push_back({k, 1, vw[k]});
        if (c > vw[k] + 2.5 * sd[k]) sig[1].push_back({k, -1, vw[k]}); if (c < vw[k] - 2.5 * sd[k]) sig[1].push_back({k, 1, vw[k]}); }
      if (tm >= 960) { if (!orDone[0] && z.H[k] > orh && c < orh) { orDone[0] = true; sig[2].push_back({k, -1, (orh + orl) / 2}); }
        if (!orDone[1] && z.L[k] < orl && c > orl) { orDone[1] = true; sig[2].push_back({k, 1, (orh + orl) / 2}); } }
      if (gdir && !gapDone && (gdir > 0 ? z.L[k] <= vw[k] && c > vw[k] : z.H[k] >= vw[k] && c < vw[k])) { gapDone = true; sig[3].push_back({k, gdir, gdir > 0 ? hiSo : loSo}); }
      if (tm >= 945 && !std::isnan(rsi[k])) { double h5 = -1e18, l5 = 1e18; for (size_t j = k >= 4 ? k - 4 : 0; j <= k; ++j) { h5 = std::max(h5, z.H[j]); l5 = std::min(l5, z.L[j]); }
        if (c > z.O[0] && c > vw[k] && rsi[k] < 10) sig[4].push_back({k, 1, h5}); if (c < z.O[0] && c < vw[k] && rsi[k] > 90) sig[4].push_back({k, -1, l5}); }
      if (tm >= 945 && k >= 5 && std::fabs(c - z.O[0]) > 0.5 * A) { const int d = c > z.O[0] ? 1 : -1; bool run = true; for (size_t j = k - 4; j <= k; ++j) if (d * (z.C[j] - z.O[j]) <= 0) run = false;
        if (run) sig[5].push_back({k, -d, vw[k]}); } }
    for (int st = 0; st < NS; ++st) for (int x = 0; x < NX; ++x) { size_t busyTo = 0; int nDay = 0;
      for (auto& g : sig[st]) { if (g.k <= busyTo && busyTo) continue; if (nDay >= 3) break;
        double tgD, stD; if (x == 0) { tgD = 0.03 * A; stD = 10 * tgD; } else if (x == 1) { tgD = 0.05 * A; stD = 10 * tgD; } else if (x == 2) { tgD = 0.05 * A; stD = 5 * tgD; }
        else { tgD = g.dir * (g.nat - z.C[g.k]); if (tgD < 0.02 * A) continue; stD = tgD; }
        double mae, mfe; size_t ko; const double p = sim(z, g.k, g.dir, stD, tgD, mae, mfe, ko); busyTo = ko; nDay++;
        Tr t{z.date, z.date / 10000, A, p, mae, mfe, 0, false};
        double tw = 0; int nt = 0; for (int q = 0; q < 10 && nt < 5; ++q) { const size_t od = rng() % days.size(); if (od == di) continue; const long j = minuteIdx(days[od], z.TM[g.k]); if (j < 0) continue;
          const double A2 = days[od].atr, f = A2 / A; double m2, f2; size_t k2; const double q2 = sim(days[od], j, g.dir, stD * f, tgD * f, m2, f2, k2); tw += q2 / A2 * Anow - cost; nt++; }
        if (nt) { t.twin = tw / nt; t.tw = true; }
        T[st][x].push_back(t); } } }
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  printf("High-win-rate setups as trades and through Tradeify payouts — %s, %zu NY days. Cost %.1f pts. Points at today's ATR (%.0f).\n", sym.c_str(), days.size(), cost, Anow);
  printf("TRADES: 2010-15 | 2016-20 | 2021-24 | 2025-26 mean pts (n) · all pts/trade, win %%, PF, trades/yr, worst drawdown (pts, 1 contract) · vs control (random other day, same minute / direction / distances)\n");
  for (int st = 0; st < NS; ++st) { printf("\n######## %s\n", SN[st]); for (int x = 0; x < NX; ++x) { auto& v = T[st][x]; if (v.size() < 30) { printf("  %-16s n %zu\n", XN[x], v.size()); continue; }
      double m[4] = {0}, c[4] = {0}, sum = 0, w = 0, gw = 0, gl = 0, tw = 0, ntw = 0, cum = 0, pk = 0, dd = 0; int pos = 0, have = 0;
      for (auto& t : v) { const double x2 = t.pts / t.atr * Anow - cost; m[era(t.year)] += x2; c[era(t.year)]++; sum += x2; w += x2 > 0; (x2 > 0 ? gw : gl) += std::fabs(x2); if (t.tw) { tw += x2 - t.twin; ntw++; } cum += x2; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); }
      printf("  %-16s", XN[x]); for (int e = 0; e < 4; ++e) { if (c[e] < 20) { printf("   —    (%5.0f) |", c[e]); continue; } printf(" %+6.2f (%5.0f) |", m[e] / c[e], c[e]); have++; pos += m[e] > 0; }
      printf(" all %+6.2f win %4.1f%% PF %.2f %4.0f/yr dd %6.0f · vs control %+5.2f · %d/%d\n", sum / v.size(), 100 * w / v.size(), gl > 0 ? gw / gl : 99.0, v.size() / 16.3, dd, ntw ? tw / ntw : NAN, pos, have); } }
  // ── PROP ──
  printf("\n######## PROP (2014-2025): one account from every 5th day · 5 accounts opened one a week, 12 months from each month start\n");
  const payout::Plan *PF = nullptr, *PD = nullptr; for (auto& p : payout::plans()) { if (p.name == "select_flex_50k") PF = &p; if (p.name == "select_daily_50k") PD = &p; }
  std::vector<int> cal; for (auto& z : days) cal.push_back(z.date); std::map<int, size_t> ci; for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = i;
  struct Acct { bool funded = false, dead = false; double bal = 0, peak = 0, floor = 0, best = 0, cyc = 0; int wins = 0, days = 0, payouts = 0; };
  auto fresh = [&](const payout::Plan& p) { Acct a; a.bal = a.peak = p.start; a.floor = p.start - p.drawdown; return a; };
  auto step = [&](const payout::Plan& p, Acct& a, const std::vector<payout::Trade>& tr) -> double {
    if (!a.funded) { double cut = p.eval_daily_loss, dp = 0; if (tr.empty()) return 0;
      for (auto& t : tr) { const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; }
      a.bal += dp; a.days++; a.best = std::max(a.best, dp); a.peak = std::max(a.peak, a.bal); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, p.start + p.lock_above_start));
      if (a.bal <= a.floor) { a.dead = true; return 0; } const double profit = a.bal - p.start;
      if (profit >= p.eval_target && a.days >= p.eval_min_days && (p.eval_consistency <= 0 || a.best <= p.eval_consistency * profit)) { a = fresh(p); a.funded = true; }
      return 0; }
    const double cut = p.daily_loss, lock = p.start + p.lock_above_start; double dp = 0;
    auto trail = [&](double eq) { a.peak = std::max(a.peak, eq); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, lock)); };
    for (auto& t : tr) { if (p.intraday_trail && t.mfe > 0) trail(a.bal + dp + t.mfe); const double w = std::min(t.mae, t.pnl);
      if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; if (p.intraday_trail) trail(a.bal + dp); }
    a.bal += dp; if (dp >= p.win_day_min && !tr.empty()) a.wins++; a.cyc += dp; a.best = std::max(a.best, dp); trail(a.bal);
    const double profit = a.bal - p.start; const bool cons = p.consistency <= 0 || (a.cyc > 0 && a.best <= p.consistency * a.cyc);
    if (a.wins >= p.win_days_needed && profit > 0 && cons && (a.payouts == 0 || a.cyc > 0)) {
      const double cap = p.caps[std::min<size_t>(a.payouts, p.caps.size() - 1)]; double amt = 0;
      if (p.payout_mult > 0) { if (profit > p.buffer && a.cyc > 0) amt = std::min({cap, p.payout_mult * a.cyc, profit - p.buffer}); if (amt < p.min_payout) amt = 0; }
      else if (profit >= p.min_profit) amt = std::min(cap, p.profit_share * profit);
      if (amt > 0) { a.bal -= amt; a.payouts++; a.wins = 0; a.best = 0; a.cyc = 0; a.peak = a.bal; return amt * 0.9; } }
    return 0; };
  const double FEE = 165;
  for (int st = 0; st < NS; ++st) for (int x = 0; x < NX; ++x) for (int q : {1, 2, 3, 5}) {
    std::vector<std::vector<payout::Trade>> D(cal.size()); long ntr = 0;
    for (auto& t : T[st][x]) { if (t.year < 2014) continue; const double f = Anow / t.atr; D[ci[t.date]].push_back({q * usdPt * (t.pts * f - cost), -q * usdPt * (t.mae * f + cost), q * usdPt * t.mfe * f}); ntr++; }
    if (ntr < 30) continue;
    for (int pk = 0; pk < 2; ++pk) { const payout::Plan& P = pk ? *PD : *PF;
      int pass = 0, fail = 0, nF = 0, paid20 = 0; std::vector<int> dtp; double payF = 0;
      for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; s0 += 5) { if (cal[s0] < 20140101) continue; Acct a = fresh(P); size_t i = s0;
        for (; i < cal.size() && !a.funded && !a.dead; ++i) step(P, a, D[i]); if (a.dead) { fail++; continue; } if (!a.funded) continue; pass++; dtp.push_back((int)(i - s0)); nF++;
        double pay = 0; int first = -1; for (size_t j = i; j < cal.size() && !a.dead; ++j) { const double y = step(P, a, D[j]); if (y > 0 && first < 0) first = (int)(j - i + 1); pay += y; } payF += pay; paid20 += first > 0 && first <= 20; }
      std::sort(dtp.begin(), dtp.end()); const double pp = pass + fail ? (double)pass / (pass + fail) : 0;
      std::vector<double> nets; int win = 0;
      for (int y = 2014; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break; const int d1 = d0 + 10000;
        size_t s0 = std::lower_bound(cal.begin(), cal.end(), d0) - cal.begin(); if (s0 >= cal.size()) break; std::vector<Acct> Ac(5); std::vector<char> open(5, 0); double net = 0;
        for (size_t i = s0; i < cal.size() && cal[i] < d1; ++i) for (int k = 0; k < 5; ++k) {
          if (!open[k]) { if ((int)(i - s0) < 5 * k) continue; open[k] = 1; Ac[k] = fresh(P); net -= FEE; }
          else if (Ac[k].dead) { Ac[k] = fresh(P); net -= FEE; continue; }
          net += step(P, Ac[k], D[i]); }
        nets.push_back(net); win += net > 0; }
      const double mean = std::accumulate(nets.begin(), nets.end(), 0.0) / nets.size();
      printf("  %-22s %-16s %d micro %-16s eval pass %3.0f%% median %4d days · funded paid within 20 days %3.0f%% · $%6.0f per funded acct · 5 accts/yr mean $%+7.0f, %3.0f%% of years > 0\n",
             SN[st], XN[x], q, P.name.c_str(), 100 * pp, dtp.empty() ? -1 : dtp[dtp.size() / 2], nF ? 100.0 * paid20 / nF : 0, nF ? payF / nF : 0, mean, 100.0 * win / nets.size()); } }
  return 0;
}
