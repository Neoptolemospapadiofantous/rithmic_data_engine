// 2026-10-20 prop_pipeline.cpp (founder: "do more tests", "so basically we have to treat the account like a full port"). The whole
// path — buy an evaluation, pass it aggressively, then run the funded account small like a portfolio — on the two firms whose
// funded side looked best in prop_one_day.cpp, with their OFFICIAL rules (read 2026-10-20):
//  LUCIDFLEX 50K (support.lucidtrading.com LucidFlex articles): $146 one-time eval (third-party price, checked 2026-09-05), no
//   activation fee. Eval: +$3,000, $2,000 MLL trailing the end-of-day balance (breach when equity touches it), locks at $50,100
//   once the balance passes $52,100, best day <= 40 % of the profit, >= 3 days, no daily loss limit (optional, off), max 40 MNQ.
//   Funded: same MLL; payout when 5 days >= $150 since the last payout and the cycle is positive; 50 % of the profit, capped
//   $2,000, minimum $500, 90 % to the trader; AFTER A PAYOUT REQUEST THE MLL MOVES TO $50,100; 5 payouts, then the account goes
//   live (treated here as the end of the account — conservative).
//  TOPSTEP 50K (help.topstep.com payout policy + pricing): No Activation Fee path $95 a month while in the Combine. Combine: +$3,000,
//   $2,000 MLL trailing end-of-day, locks at the start, best day <= 50 % of the profit, >= 2 days. XFA Standard path: payout when
//   5 days >= $150 (positive since the last payout, the first exempt); up to 50 % of the balance above the start, capped $2,000,
//   minimum $125, 90 %; AFTER EACH PAYOUT THE MLL RESETS TO THE START ($0) PERMANENTLY.
// Trade: at 10:30 ET go with the first hour, hold to 15:55, no stop; the eval also exits the moment it passes (target hit with the
// best-day rule met). Eval size 1 / 2 / 3 / 4 / 10 MNQ (20 / 40 dropped: 0 % pass under the best-day caps) every day; funded size 1 / 2 / 3 MNQ, every day (F1) or only a straight first
// hour (F2); control: a coin flip for the direction in BOTH phases. Points at today's ATR, cost 1.5 pts + $1.82 per MNQ.
// Programme: from every month 2014-01 … 2025-10, 12 months, K = 1 / 5 accounts on the same trades (hedging banned), a failed eval or
// a breached / finished funded account replaced by a new eval the next day. Per calendar month: payouts − fees.
// Usage: prop_pipeline
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <numeric>
#include <random>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct Dd { int date; double k; int dir, coin; bool straight; std::vector<double> lo, hi; double last; };   // signed points vs the 10:30 entry (long view)
  std::vector<Dd> D; double atrNow = 0; std::mt19937 rng(20261020);
  for (auto& d : s.days) if (d.ok && d.atr > 0 && d.date <= 20261016) atrNow = d.atr;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20131201 || d.date > 20261016) continue;
    double o = NAN, h1 = -1e18, l1 = 1e18, c1 = NAN; Dd z; z.last = NAN;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930) continue; if (std::isnan(o)) o = s.o[i];
      if (tm < 990) { h1 = std::max(h1, (double)s.h[i]); l1 = std::min(l1, (double)s.l[i]); c1 = s.c[i]; continue; }
      if (tm >= 1315) { z.last = s.o[i] - c1; break; } z.lo.push_back(s.l[i] - c1); z.hi.push_back(s.h[i] - c1); }
    if (std::isnan(c1) || std::isnan(z.last) || z.lo.size() < 100) continue;
    z.date = d.date; z.k = atrNow / d.atr; z.dir = c1 > o ? 1 : -1; z.coin = rng() % 2 ? 1 : -1;
    z.straight = std::fabs(c1 - o) >= 0.55 * (h1 - l1) && std::fabs(c1 - o) >= 0.25 * d.atr; D.push_back(std::move(z)); }
  enum { LUCID, TOPSTEP };
  struct Acct { bool funded = false, dead = false, done = false; double bal = 0, peak = 0, floor = -2000, best = 0, cyc = 0; int days = 0, wins = 0, payouts = 0; };
  // one trading day for an account; returns the payout (after the 90 % split) paid at that day's close
  auto day = [&](int firm, Acct& a, const Dd& z, int dir, int N, bool trade) -> double {
    const double cost = N * (2 * 1.5 + 1.82), u = N * 2.0 * z.k; double dp = 0;
    if (trade) {
      dp = -cost; bool closed = false;
      for (size_t j = 0; j < z.lo.size(); ++j) { const double lo = -cost + u * (dir > 0 ? z.lo[j] : -z.hi[j]), hi = -cost + u * (dir > 0 ? z.hi[j] : -z.lo[j]);
        if (a.bal + lo <= a.floor) { a.dead = true; return 0; }
        if (!a.funded) { const double P = a.bal + hi, c = firm == LUCID ? 0.40 : 0.50; const int need = firm == LUCID ? 3 : 2;
          if (P >= 3000 && std::max(a.best, hi) <= c * P && a.days + 1 >= need) { dp = 3000 - a.bal; closed = true; break; } } }
      if (!closed) dp = -cost + u * dir * z.last;
      a.days++; }
    a.bal += dp; a.best = std::max(a.best, dp); a.cyc += dp; if (trade && dp >= 150) a.wins++;
    // end-of-day trail: Lucid locks at +100 once the balance passed +2,100; Topstep locks at the start (0)
    a.peak = std::max(a.peak, a.bal); const double lockAt = firm == LUCID ? 100 : 0; a.floor = std::max(a.floor, std::min(a.peak - 2000, lockAt));
    if (a.bal <= a.floor) { a.dead = true; return 0; }
    if (!a.funded) { const double c = firm == LUCID ? 0.40 : 0.50; const int need = firm == LUCID ? 3 : 2;
      if (a.bal >= 3000 && a.best <= c * a.bal && a.days >= need) { a = Acct{}; a.funded = true; } return 0; }
    // funded payout
    const bool cycleOk = firm == LUCID ? a.cyc > 0 : (a.payouts == 0 || a.cyc > 0);
    if (a.wins >= 5 && cycleOk && a.bal > 0) { double amt = std::min(2000.0, 0.5 * a.bal);
      if (amt >= (firm == LUCID ? 500 : 125)) { a.bal -= amt; a.payouts++; a.wins = 0; a.cyc = 0; a.best = 0;
        a.floor = firm == LUCID ? std::max(a.floor, 100.0) : std::max(a.floor, 0.0); a.peak = a.bal;
        if (a.bal <= a.floor) a.dead = true;   // a payout that leaves the balance at the new floor ends it at the next touch
        if (firm == LUCID && a.payouts >= 5) a.done = true; return amt * 0.9; } }
    return 0; };
  const char* FN[2] = {"LUCIDFLEX 50K ($146 eval)", "TOPSTEP 50K ($95 a month in the Combine)"};
  for (int firm = 0; firm < 2; ++firm) { printf("\n%s — 12-month programmes from every month 2014-01 … 2025-10\n", FN[firm]);
    for (int ctl = 0; ctl < 2; ++ctl) for (int fr = 1; fr <= 2; ++fr) { if (ctl && fr == 2) continue;
      for (int Ne : {1, 2, 3, 4, 10}) for (int Nf : {1, 2, 3}) for (int K : {1, 5}) { if (K == 5 && Nf != 1) continue;
        std::vector<double> monthNet; std::vector<double> yrNet; int evals = 0, passes = 0, payN = 0; double payS = 0;
        for (int y = 2014; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break;
          size_t s0 = std::lower_bound(D.begin(), D.end(), d0, [](const Dd& a, int v) { return a.date < v; }) - D.begin();
          std::vector<Acct> A(K); std::map<int, double> mon; int sinceFee = 0;
          for (int k = 0; k < K; ++k) { mon[d0 / 100] -= firm == LUCID ? 146 : 95; evals++; }
          for (size_t i = s0; i < D.size() && D[i].date < d0 + 10000; ++i) { const Dd& z = D[i]; const int mk = z.date / 100; sinceFee++;
            for (int k = 0; k < K; ++k) { Acct& a = A[k];
              if (a.dead || a.done) { a = Acct{}; mon[mk] -= firm == LUCID ? 146 : 95; evals++; continue; }
              if (firm == TOPSTEP && !a.funded && sinceFee % 21 == 0) mon[mk] -= 95;   // the monthly Combine fee
              const int dir = ctl ? z.coin : z.dir; const bool f = a.funded;
              const bool trade = !f || fr == 1 || z.straight;
              const double pay = day(firm, a, z, dir, f ? Nf : Ne, trade);
              if (!f && a.funded) passes++; if (pay > 0) { payN++; payS += pay; } mon[mk] += pay; } }
          double tot = 0; for (auto& [mm, v] : mon) { monthNet.push_back(v); tot += v; } yrNet.push_back(tot); }
        std::sort(monthNet.begin(), monthNet.end()); std::vector<double> sy = yrNet; std::sort(sy.begin(), sy.end());
        int m500 = 0, m1k = 0, mpos = 0; for (double v : monthNet) { m500 += v >= 500 * K; m1k += v >= 1000 * K; mpos += v > 0; }
        const size_t nm = monthNet.size(), ny = yrNet.size(); int ypos = 0; for (double v : yrNet) ypos += v > 0;
        printf("  %-26s eval %2d MNQ, funded %d MNQ, %d acct: evals passed %3.0f%% · avg payout $%4.0f · year net mean $%+6.0f, median $%+6.0f, 10th $%+6.0f, %3.0f%% of years > 0 · months: mean $%+5.0f, %3.0f%% > 0, %3.0f%% >= $500/acct, %3.0f%% >= $1,000/acct\n",
               ctl ? "COIN FLIP both phases" : fr == 1 ? "first hour, funded every day" : "first hour, funded straight only", Ne, Nf, K, 100.0 * passes / evals, payN ? payS / payN : 0,
               std::accumulate(yrNet.begin(), yrNet.end(), 0.0) / ny, sy[ny / 2], sy[ny / 10], 100.0 * ypos / ny, std::accumulate(monthNet.begin(), monthNet.end(), 0.0) / nm, 100.0 * mpos / nm, 100.0 * m500 / nm, 100.0 * m1k / nm); } } }
  return 0;
}
