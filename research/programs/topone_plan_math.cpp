// 2026-10-19 (founder: "I pay around 70-100 per 50k account … 40% WR at 1 to 3 … 5 accounts for 1 month … we want to pass at least 2,
// then have 5 winning trade days"). TopOne 50K as the founder pasted it: eval $3,000 target in 30 days (21 trading days), $2,000 trailing
// drawdown (end of day), no daily limit, no consistency; funded: payout needs 5 days of ≥ $250 and balance ≥ +$2,500 ($2,000 buffer stays),
// $500–1,500 per request, 90 %. Funded drawdown assumed to trail EOD and lock at +$100 once +$2,100 is reached (Elite Access, NOT
// confirmed) — a no-lock line is shown too. Synthetic trades: win → +3R, loss → −1R, minus $10 a trade. Accounts started on DIFFERENT
// days are independent; accounts copying the SAME trades pass or fail together (all-or-nothing). 20,000 runs per line.
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
#include <algorithm>
static std::mt19937 g(23);
static double tradeR(double p) { return std::uniform_real_distribution<double>(0, 1)(g) < p ? 3.0 : -1.0; }
static bool evalPass(double p, int n, double risk, int& days) {   // 21 trading days
  double bal = 0, peak = 0; for (days = 1; days <= 21; ++days) { for (int k = 0; k < n; ++k) bal += tradeR(p) * risk - 10; peak = std::max(peak, bal); if (bal >= 3000) return true; if (bal <= peak - 2000) return false; } return false; }
struct F { double paid; int firstPay; bool blown; };
static F funded(double p, int n, double risk, int horizon, bool lock) {
  double bal = 0, peak = 0, floor = -2000, cyc = 0; bool locked = false; int pd = 0; F r{0, -1, false};
  for (int d = 1; d <= horizon; ++d) { double pl = 0; for (int k = 0; k < n; ++k) pl += tradeR(p) * risk - 10; bal += pl; cyc += pl; if (pl >= 250) pd++;
    if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (lock && peak >= 2100) { locked = true; floor = 100; } }
    if (bal <= floor) { r.blown = true; return r; }
    if (pd >= 5 && bal >= 2500) { double w = std::min(1500.0, bal - 2000); if (w >= 500) { r.paid += 0.9 * w; bal -= w; if (lock) { locked = true; floor = 100; } pd = 0; cyc = 0; if (r.firstPay < 0) r.firstPay = d; } } }
  return r; }
int main() {
  const double P[] = {0.40, 0.35, 0.30, 0.27, 0.25, 0.22}; const int NT[] = {1, 3}; const double RISK[] = {250, 500, 750, 1000};
  printf("TopOne 50K — 5 accounts a month at $85 each ($425). Win +3R / loss −1R, $10 cost a trade. 20,000 runs per line.\n"
         "pass = eval passed within 30 days; >=2 of 5 = at least 2 of 5 accounts started on different days; copy = 5 accounts on the same trades.\n"
         "funded: chance of a first payout within 3 months (63 trading days) and mean payouts in that time per funded account; month $ = the\n"
         "month's 5 evals: expected payouts within 3 months minus $425 (activation fee not included).\n\n");
  printf("  WR@1:3  EV/trade  trades/day  risk$ | pass%%  days  >=2of5%%  copy-all%% | funded: 1st payout%%  day  payouts 3mo  blown%% | month $ (lock / no lock)\n");
  for (double p : P) for (int n : NT) for (double r : RISK) {
    const int RUNS = 20000; int pass = 0; long dsum = 0;
    for (int i = 0; i < RUNS; ++i) { int d; if (evalPass(p, n, r, d)) { pass++; dsum += d; } }
    double pp = (double)pass / RUNS, p2 = 1 - std::pow(1 - pp, 5) - 5 * pp * std::pow(1 - pp, 4);
    int fp = 0, bl = 0; long fd = 0; double paid = 0, paidNL = 0;
    for (int i = 0; i < RUNS; ++i) { F f = funded(p, n, r, 63, true); if (f.firstPay >= 0) { fp++; fd += f.firstPay; } if (f.blown) bl++; paid += f.paid; paidNL += funded(p, n, r, 63, false).paid; }
    double perF = paid / RUNS, perFNL = paidNL / RUNS;
    printf("  %4.0f%%   %+5.2fR   %d          %5.0f | %5.1f %5.1f %6.1f   %6.1f    |        %5.1f      %4.0f  %+8.0f     %5.1f  | %+6.0f / %+6.0f\n",
           100 * p, 4 * p - 1, n, r, 100 * pp, pass ? (double)dsum / pass : 0, 100 * p2, 100 * pp, 100.0 * fp / RUNS, fp ? (double)fd / fp : 0, perF, 100.0 * bl / RUNS, 5 * pp * perF - 425, 5 * pp * perFNL - 425); }
  return 0;
}
