// 2026-10-20 prop_one_day.cpp (founder: "what about entering on trades we think will follow the trend and have no stop loss … I can
// buy 10 accounts … 1 trade we buy or sell … no stop loss and wait until we pass it", "there are accounts you pass in 1 day").
// Evaluations with no best-day cap (payout_sim.hpp: Tradeify Growth 50K — $3,000 target, $2,000 trail on INTRADAY equity, soft
// $1,250 daily loss; Apex 50K EOD — $3,000 target, $2,000 trail on the end-of-day balance, no daily loss) traded with ONE trade a
// day, no stop: at 10:30 ET go with the first hour (long if 10:30 > 09:30 open), hold minute by minute until the account reaches the
// target (PASS, out at that price), touches its floor (FAIL), the daily loss closes it, or 15:55 (flat, carry the balance). Fixed rules:
//  D1 every day, with the first hour · D2 only a straight first hour (|move| >= 55 % of its range and >= 0.25 × ATR14), else no trade
//  D0 control: a coin flip for the direction every day (seeded)
// Size 10 / 20 / 30 / 40 MNQ (Growth caps 4 minis). Points scaled to today's ATR, cost 1.5 pts + $1.82 per MNQ. Starts on every
// trading day 2014-01 … 2025-10, at most 60 trading days. 10 accounts started together take the same trades = the same result 10×.
// Then the FUNDED side (founder: "not tradeify"): a passed account on each firm's funded rules in payout_sim.hpp (Tradeify Growth,
// Apex EOD / intraday, MFF Rapid, Lucid Flex, Topstep 50K) trading the same daily trade (exit 15:55, no target) at 1 / 2 / 4 / 10
// MNQ: share paid within 60 trading days, average payout, share blown first, $ paid per funded account.
// Usage: prop_one_day
#include "paper/payout_sim.hpp"
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct Dd { int date; double k; int dir, coin; bool straight; std::vector<double> path; };   // path = signed points from the 10:30 entry, per minute to 15:55 (lo-first, hi-second pairs)
  std::vector<Dd> D; double atrNow = 0; std::mt19937 rng(20261020);
  for (auto& d : s.days) if (d.ok && d.atr > 0 && d.date <= 20261016) atrNow = d.atr;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20131201 || d.date > 20261016) continue;
    double o = NAN, h1 = -1e18, l1 = 1e18, c1 = NAN; std::vector<std::array<double, 3>> m;   // l, h, c after 10:30
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930) continue; if (std::isnan(o)) o = s.o[i];
      if (tm < 990) { h1 = std::max(h1, (double)s.h[i]); l1 = std::min(l1, (double)s.l[i]); c1 = s.c[i]; continue; }
      if (tm >= 1315) { m.push_back({(double)s.o[i], (double)s.o[i], (double)s.o[i]}); break; } m.push_back({(double)s.l[i], (double)s.h[i], (double)s.c[i]}); }
    if (std::isnan(c1) || m.size() < 100) continue;
    Dd z; z.date = d.date; z.k = atrNow / d.atr; z.dir = c1 > o ? 1 : -1; z.coin = rng() % 2 ? 1 : -1;
    z.straight = std::fabs(c1 - o) >= 0.55 * (h1 - l1) && std::fabs(c1 - o) >= 0.25 * d.atr;
    for (auto& b : m) { z.path.push_back(b[0] - c1); z.path.push_back(b[1] - c1); z.path.push_back(b[2] - c1); } D.push_back(std::move(z)); }
  printf("ONE TRADE A DAY, NO STOP, HOLD UNTIL PASS — NQ, 10:30 entry, points at today's ATR (%.0f), cost 1.5 pts + $1.82 / MNQ\n", atrNow);
  const char* RN[3] = {"D0 coin flip, every day", "D1 first hour, every day", "D2 straight first hour only"};
  // one day of an eval: returns +1 pass, −1 fail, 0 open; bal / peak / floor relative to the start (0)
  auto day = [&](const Dd& z, int dir, int N, bool intraday, double dailyLoss, double& bal, double& peak, double& floor) -> int {
    const double cost = N * (2 * 1.5 + 1.82), u = N * 2.0 * z.k; double eq = bal - cost;
    for (size_t j = 0; j + 2 < z.path.size(); j += 3) {
      const double lo = bal - cost + u * (dir > 0 ? z.path[j] : -z.path[j + 1]), hi = bal - cost + u * (dir > 0 ? z.path[j + 1] : -z.path[j]);
      // worst first within the minute (conservative): the floor, then the daily loss, then the target
      if (lo <= floor) { bal = floor; return -1; }
      if (dailyLoss > 0 && lo - bal <= -dailyLoss) { bal -= dailyLoss; break; }
      if (hi >= 3000) { bal = 3000; return +1; }
      if (intraday) { peak = std::max(peak, hi); floor = std::max(floor, std::min(peak - 2000, 100.0)); }
      eq = bal - cost + u * dir * z.path[j + 2]; }
    if (eq != bal - cost || true) { const double last = z.path[z.path.size() - 1]; bal = bal - cost + u * dir * last; }
    peak = std::max(peak, bal); floor = std::max(floor, std::min(peak - 2000, 100.0));
    return bal <= floor ? -1 : bal >= 3000 ? +1 : 0; };
  for (int plan = 0; plan < 2; ++plan) {
    printf("\n%s\n", plan == 0 ? "TRADEIFY GROWTH 50K eval (intraday trail, $1,250 daily loss)" : "APEX 50K EOD eval (end-of-day trail, no daily loss)");
    for (int rule = 0; rule < 3; ++rule) for (int N : {10, 20, 30, 40}) {
      int starts = 0, pass = 0, fail = 0, pass1 = 0; std::vector<int> dtp;
      for (size_t s0 = 0; s0 < D.size(); ++s0) { if (D[s0].date < 20140101 || D[s0].date > 20251031) continue; starts++;
        double bal = 0, peak = 0, floor = -2000; int res = 0, td = 0;
        for (size_t i = s0; i < D.size() && i < s0 + 60 && res == 0; ++i) { const auto& z = D[i]; if (rule == 2 && !z.straight) continue;
          const int dir = rule == 0 ? z.coin : z.dir; td++; res = day(z, dir, N, plan == 0, plan == 0 ? 1250 : 0, bal, peak, floor); }
        if (res > 0) { pass++; dtp.push_back(td); pass1 += td == 1; } else if (res < 0) fail++; }
      std::sort(dtp.begin(), dtp.end());
      printf("  %-30s %2d MNQ: PASS %3.0f%% (on the first trade %3.0f%%) · FAIL %3.0f%% · still open after 60 days %3.0f%% · median %d trades to pass\n",
             RN[rule], N, 100.0 * pass / starts, 100.0 * pass1 / starts, 100.0 * fail / starts, 100.0 * (starts - pass - fail) / starts, dtp.empty() ? -1 : dtp[dtp.size() / 2]); } }
  // funded: each firm's funded rules (payout_sim.hpp plans), the same daily trade held to 15:55
  for (const char* pn : {"growth_50k", "apex_eod_50k", "apex_intraday_50k", "mff_rapid_50k", "lucid_flex_50k", "topstep_50k"}) {
  const payout::Plan* G = nullptr; for (auto& p : payout::plans()) if (p.name == pn) G = &p;
  printf("\nFUNDED %s (payout_sim.hpp rules), same trade held to 15:55, 60 trading days:\n", pn);
  for (int rule = 0; rule < 3; ++rule) for (int N : {1, 2, 4, 10}) {
    std::vector<std::vector<payout::Trade>> T(D.size()); std::vector<const std::vector<payout::Trade>*> ptr(D.size(), nullptr);
    for (size_t i = 0; i < D.size(); ++i) { const auto& z = D[i]; if (rule == 2 && !z.straight) continue; const double u = N * 2.0 * z.k, cost = N * (2 * 1.5 + 1.82); const int dd = rule == 0 ? z.coin : z.dir;
      double mae = 0, mfe = 0; for (size_t j = 0; j + 2 < z.path.size(); j += 3) { mae = std::min(mae, u * (dd > 0 ? z.path[j] : -z.path[j + 1])); mfe = std::max(mfe, u * (dd > 0 ? z.path[j + 1] : -z.path[j])); }
      T[i].push_back({u * dd * z.path.back() - cost, mae - cost, mfe}); ptr[i] = &T[i]; }
    int starts = 0, paid = 0, blown = 0; double first = 0, tot = 0;
    for (size_t s0 = 0; s0 < D.size(); ++s0) { if (D[s0].date < 20140101 || D[s0].date > 20251031) continue; starts++;
      const auto o = payout::run_account(*G, ptr, (int)s0, 60); tot += o.paid_sum; if (o.paid) { paid++; first += o.paid_sum / o.payouts; } else if (o.breach) blown++; }
    printf("  %-30s %2d MNQ: paid within 60 days %3.0f%% (avg payout $%.0f) · blown before a payout %3.0f%% · paid per funded account $%.0f\n", RN[rule], N, 100.0 * paid / starts, paid ? first / paid : 0, 100.0 * blown / starts, tot / starts); } }
  return 0;
}
