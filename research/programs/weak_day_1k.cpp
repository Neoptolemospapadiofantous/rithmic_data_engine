// 2026-10-20 weak_day_1k.cpp (founder, after flag_setforget.cpp: "its just not enough", "I want to make at least 5k a month",
// "there must be something", "even 1k is fine"). Can the one intraday trade that survived the 2026-10-11 search (ledger S1,
// docs/intraday_search_2026-10-11.md: yesterday closed weak AND VIX >= 20 → buy the 09:30 open, sell 15:55, no stop, +1 unit at
// VIX >= 25) pay $1,000 a month on Tradeify? Fixed before the run, no parameter chosen on these results:
//  weak = prior RTH close in the bottom 30 % of its range (IBS < 0.3) OR 2-day RSI < 20 OR two lower closes OR a fear day (close <=
//  open − 1 × ATR14); VIX = the prior CBOE close. Points scaled to today's ATR (× ATR now / ATR then) so 2011 days trade like today.
//  Cost: 1.5 pts + $1.82 a round trip per MNQ (realistic) and 3 pts (stress). The trade's worst open loss (MAE) is applied first.
//  Accounts (payout_sim.hpp plans, read 2026-10-11): Select Daily 50K and Select Flex 50K, $165 an eval, 90 % of every payout,
//  a filler 1-MNQ trade (−$5) in each eval week without a trade; size N MNQ (+N at VIX >= 25), N = 2 / 4 / 6 / 8 / 10 (max 40 MNQ).
//  W1 the trade alone (per MNQ) · W2 one account: pass %, days to pass, payouts a funded account
//  W3 1 / 3 / 5 accounts on the same trades, a dead one replaced the next day, 12 months from every month 2012-01 … 2025-10:
//     net a month (payouts − fees) mean / median / 10th pct, % of windows >= $500 / $1,000 a month per account ("5k can be 5 accounts", "at least 500"), % > 0, worst running loss
// Usage: weak_day_1k
#include "paper/payout_sim.hpp"
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <numeric>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct D { int date; double pts, mae, atr, vix; bool weak; };
  std::vector<D> days; double atrNow = 0; int dNow = 0;
  const Day* p1 = nullptr; const Day* p2 = nullptr;
  for (auto& d : s.days) { if (!d.ok) continue;
    if (d.atr > 0 && d.date <= 20261016) { atrNow = d.atr; dNow = d.date; }
    if (p1 && p2 && !d.rollwin && d.atr > 0 && !std::isnan(d.vix) && d.date >= 20110101 && d.date <= 20261016) {
      const double ibs = p1->rh > p1->rl ? (p1->rc - p1->rl) / (p1->rh - p1->rl) : 0.5;
      double e = NAN, x = NAN, lo = 1e18;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930) continue; if (std::isnan(e)) e = s.o[i]; if (tm >= 1315) { x = s.o[i]; break; } lo = std::min(lo, (double)s.l[i]); }
      if (!std::isnan(e) && !std::isnan(x)) days.push_back({d.date, x - e, std::min(0.0, lo - e), d.atr, d.vix, ibs < 0.3 || (!std::isnan(d.rsi2) && d.rsi2 < 20) || p1->rc <= p1->ro - d.atr}); }
    p2 = p1; p1 = &d; }
  // two lower closes needs three closes: recompute with a rolling window of RTH closes
  { std::vector<std::pair<int, double>> rc; for (auto& d : s.days) if (d.ok) rc.push_back({d.date, d.rc});
    std::map<int, size_t> ix; for (size_t i = 0; i < rc.size(); ++i) ix[rc[i].first] = i;
    for (auto& z : days) { auto it = ix.find(z.date); if (it == ix.end() || it->second < 3) continue; const size_t i = it->second; if (rc[i - 1].second < rc[i - 2].second && rc[i - 2].second < rc[i - 3].second) z.weak = true; } }
  std::vector<D> T; for (auto& z : days) if (z.weak && z.vix >= 20) T.push_back(z);
  printf("WEAK DAY on Tradeify — yesterday weak AND VIX >= 20 → long 09:30 → 15:55 ET, NQ 2011-01 … 2026-10-16, points scaled to ATR now %.0f (%d)\n", atrNow, dNow);
  auto perMNQ = [&](const D& z, double cost) { const double k = atrNow / z.atr; return 2 * (z.pts * k - cost) - 1.82; };
  for (int fx = 0; fx < 2; ++fx) { const double cost = fx ? 3.0 : 1.5; double sum = 0, gw = 0, gl = 0, w = 0, eq = 0, pk = 0, dd = 0; std::map<int, double> yr; std::map<int, double> mon;
    for (auto& z : T) { const double p = perMNQ(z, cost); sum += p; w += p > 0; (p > 0 ? gw : gl) += std::fabs(p); eq += p; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); yr[z.date / 10000] += p; mon[z.date / 100] += p; }
    int yp = 0; for (auto& [y, v] : yr) yp += v > 0; int mp = 0; for (auto& [m, v] : mon) mp += v > 0;
    printf("\n  W1 %s: %zu trades (%.1f a year, %.1f a month) · $%+.0f per MNQ a trade · win %.0f%% · PF %.2f · %d/%zu years > 0 · %d/%zu trading months > 0 · max drawdown $%.0f per MNQ\n     by year $/MNQ:",
           fx ? "STRESS cost 3 pts" : "realistic cost 1.5 pts", T.size(), T.size() / 15.8, T.size() / 189.5, sum / T.size(), 100 * w / T.size(), gw / gl, yp, yr.size(), mp, mon.size(), dd);
    for (auto& [y, v] : yr) printf(" %d %+.0f", y, v); printf("\n"); }
  // calendar of trading days and the trades on them
  std::vector<int> cal; for (auto& z : days) cal.push_back(z.date); std::map<int, int> ci; for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = (int)i;
  auto dayNum = [](int dt) { std::tm a{}; a.tm_year = dt / 10000 - 1900; a.tm_mon = dt / 100 % 100 - 1; a.tm_mday = dt % 100; a.tm_hour = 12; return (int)(timegm(&a) / 86400); };
  std::vector<char> lastOfWeek(cal.size(), 0); for (size_t i = 0; i < cal.size(); ++i) lastOfWeek[i] = i + 1 == cal.size() || (dayNum(cal[i + 1]) + 3) / 7 != (dayNum(cal[i]) + 3) / 7;
  struct Acct { bool funded = false, dead = false; double bal = 0, peak = 0, floor = 0, best = 0, cyc = 0; int wins = 0, days = 0, payouts = 0; bool traded = false; };
  const payout::Plan* PL[2] = {nullptr, nullptr}; for (auto& p : payout::plans()) { if (p.name == "select_daily_50k") PL[0] = &p; if (p.name == "select_flex_50k") PL[1] = &p; }
  auto fresh = [&](int kind) { Acct a; a.bal = a.peak = PL[kind]->start; a.floor = a.bal - PL[kind]->drawdown; return a; };
  // (same day logic as flag_setforget.cpp: eval = EOD trail at each trade's MAE, soft daily loss, target + consistency; funded = run_account)
  auto step = [&](int kind, Acct& a, const std::vector<payout::Trade>& tr, size_t ix) -> double {
    const payout::Plan& p = *PL[kind];
    if (!a.funded) { double cut = p.eval_daily_loss, dp = 0; bool any = false;
      for (auto& t : tr) { any = true; const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; }
      a.traded |= any; if (lastOfWeek[ix] && !a.traded) { dp -= 5; any = true; } if (lastOfWeek[ix]) a.traded = false;
      if (!any) return 0;
      a.bal += dp; a.days++; a.best = std::max(a.best, dp); a.peak = std::max(a.peak, a.bal); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, p.start + p.lock_above_start));
      if (a.bal <= a.floor) { a.dead = true; return 0; }
      const double profit = a.bal - p.start;
      if (profit >= p.eval_target && a.days >= p.eval_min_days && (p.eval_consistency <= 0 || a.best <= p.eval_consistency * profit)) { a = fresh(kind); a.funded = true; }
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
  const char* KN[2] = {"Select Daily 50K", "Select Flex 50K"};
  for (int fx = 0; fx < 2; ++fx) { const double cost = fx ? 3.0 : 1.5;
    printf("\n%s\n", fx ? "STRESS (cost 3 pts)" : "REALISTIC (cost 1.5 pts)");
    for (int kind = 0; kind < 2; ++kind) for (int N : {2, 4, 6, 8, 10}) {
      std::vector<std::vector<payout::Trade>> DT(cal.size());
      for (auto& z : T) { const int q = N * (z.vix >= 25 ? 2 : 1); const double k = atrNow / z.atr; DT[ci[z.date]].push_back({q * perMNQ(z, cost), q * 2 * (z.mae * k - cost), 0}); }
      int starts = 0, pass = 0, fail = 0; std::vector<int> dtp; double payF = 0; int nF = 0, life = 0;
      for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; s0 += 5) { if (cal[s0] < 20120101) continue; Acct a = fresh(kind); starts++; size_t i = s0;
        for (; i < cal.size() && !a.funded && !a.dead; ++i) step(kind, a, DT[i], i);
        if (a.dead) { fail++; continue; } if (!a.funded) continue; pass++; dtp.push_back((int)(i - s0));
        double pay = 0; size_t j = i; for (; j < cal.size() && !a.dead; ++j) pay += step(kind, a, DT[j], j); payF += pay; nF++; life += (int)(j - i); }
      std::sort(dtp.begin(), dtp.end());
      printf("  W2 %-16s %2d MNQ (+%d at VIX>=25): pass %3.0f%% · median %3d trading days to pass · funded pays $%5.0f over %3.0f days alive\n", KN[kind], N, N,
             100.0 * pass / std::max(1, pass + fail), dtp.empty() ? -1 : dtp[dtp.size() / 2], nF ? payF / nF : 0, nF ? (double)life / nF : 0.0);
      for (int K : {1, 3, 5}) { std::vector<double> nets, worst; int nw = 0, k1 = 0, k5 = 0;
        for (int y = 2012; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break;
          size_t s0 = std::lower_bound(cal.begin(), cal.end(), d0) - cal.begin(); if (s0 >= cal.size()) break;
          std::vector<Acct> A(K); for (auto& a : A) a = fresh(kind); double net = -165.0 * K, low = net;
          for (size_t i = s0; i < cal.size() && cal[i] < d0 + 10000; ++i) for (int k = 0; k < K; ++k) {
            if (A[k].dead) { A[k] = fresh(kind); net -= 165; low = std::min(low, net); continue; }
            net += step(kind, A[k], DT[i], i); low = std::min(low, net); }
          nets.push_back(net / 12); worst.push_back(low); nw++; k1 += net / 12 >= 1000 * K; k5 += net / 12 >= 500 * K; }
        std::vector<double> sn = nets; std::sort(sn.begin(), sn.end()); std::sort(worst.begin(), worst.end()); int pos = 0; for (double v : nets) pos += v > 0;
        printf("     W3 %d account%s: net a month mean $%+5.0f · median $%+5.0f · 10th pct $%+5.0f · >= $500/month per account in %3.0f%% · >= $1,000/month per account in %3.0f%% · > 0 in %3.0f%% · worst running loss $%6.0f\n",
               K, K > 1 ? "s" : " ", std::accumulate(nets.begin(), nets.end(), 0.0) / nw, sn[nw / 2], sn[nw / 10], 100.0 * k5 / nw, 100.0 * k1 / nw, 100.0 * pos / nw, worst.front()); } } }
  return 0;
}
