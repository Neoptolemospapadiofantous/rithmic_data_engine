// 2026-10-19 (founder: "can we try toponefutures", 50K plan pasted: $3,000 target, $2,000 EOD max drawdown, no daily loss limit and
// no consistency on the eval, $39 eval (sale; $189 list), $35 reset, $189 activation, 3 minis / 30 micros, up to 10 accounts).
// Funded rules are the published Elite Access 50K ones (tradetanto.com/learn/top-one-futures-rules, read 2026-10-19 — NOT confirmed
// with TopOne): EOD trailing $2,000 that locks at +$100 once the EOD peak reaches +$2,100 or after the first payout; $1,000 daily loss
// pauses the day; payout needs 5 days of ≥ $250 in the cycle, balance ≥ +$2,500, largest day ≤ 40 % of the cycle's profit; withdraw
// balance − $2,000, $500 min / $1,500 max, 90 % split; no overnight holds; copy trading between own accounts allowed, opposite positions
// banned. Trades: the plan's DAY trades only (book F + gap-down weakness long, 1.5 ATR stop) from research/programs/inputs_20261019,
// each sized to $risk at its stop (fractional micros, capped at 30), cost 2 NQ pts at today's cost-to-range. Copy-traded accounts follow
// identical paths, so one account chain is simulated (eval → funded → blown → new eval) and an operation of N accounts is N x that.
// argv: inputs dir. 12-month windows started each month 2010-07 → 2025-10.
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>

struct T { long e; double r; double kpts; };   // R multiple net, stop in points
static double COSTPTS = 2.0;   // argv[2]: cost per round trip in NQ pts at today's cost-to-range
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "research/programs/inputs_20261019"; if (argc > 2) COSTPTS = atof(argv[2]);
  std::map<int, double> atr; { std::ifstream f(dir + "/nq_atr_edge_lab.csv"); std::string l; while (std::getline(f, l)) { int d; double a; if (sscanf(l.c_str(), "%d,%lf", &d, &a) == 2) atr[d] = a; } }
  std::vector<int> ses; { std::ifstream f(dir + "/sessions_all.csv"); std::string l; while (std::getline(f, l)) { int d; if (sscanf(l.c_str(), "%d", &d) == 1 && atr.count(d)) ses.push_back(d); } }
  std::map<int, std::vector<T>> src[10];   // 0 book F, 1 weakness long, 2 FL5 (5m flag with the day, straight path), 3 FLMIX (any 1m/3m/5m flag with the day)
  auto load = [&](const char* fn, std::map<int, std::vector<T>>& m) { std::ifstream f(dir + "/" + fn); std::string l;
    while (std::getline(f, l)) { int d; long e, x; int di; double g, n, mae, mfe, src, k; if (sscanf(l.c_str(), "%d,%ld,%ld,%d,%lf,%lf,%lf,%lf,%lf,%lf", &d, &e, &x, &di, &g, &n, &mae, &mfe, &src, &k) != 10 || !atr.count(d)) continue;
      double A = atr[d], stop = k * A; m[d].push_back({e, (g - COSTPTS * A / 320.0) / stop, stop}); } };
  load("book_F_k.csv", src[0]); load("weakness_long_1p5atr_k.csv", src[1]); load("flag5_k.csv", src[2]); load("flagmix_k.csv", src[3]); load("flagx_k_1.5.csv", src[4]); load("flagref_k.csv", src[5]); load("flagdaily40_k.csv", src[6]); load("flagr7_k.csv", src[7]); load("flagr12_k.csv", src[8]); load("flagtrail5_k.csv", src[9]);
  auto dayPL = [&](int d, double risk, int mask, double dll) { std::vector<T> v; for (int k = 0; k < 10; ++k) if ((mask >> k & 1) && src[k].count(d)) for (auto& t : src[k][d]) v.push_back(t);
    std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.e < b.e; }); double pl = 0;
    for (auto& t : v) { if (pl <= -dll) break; double contracts = std::min(30.0, risk / (t.kpts * 2.0)); pl += t.r * contracts * t.kpts * 2.0; } return pl; };
  printf("TopOne 50K (Elite Access rules for funded) on the plan's day trades, cost %.0f pts — per ONE copy-traded account chain; N accounts = N x.\n"
         "eval fee $39 first, $35 per reset; activation $189; payout 90 %% of withdrawals.\n", COSTPTS);
  const double ER[] = {250, 500, 750, 1000, 1500}, FR[] = {250, 500, 750, 1000};
  const int MASKS[17] = {1, 3, 4, 8, 7, 19, 16, 35, 32, 67, 64, 131, 128, 259, 256, 515, 512}; const char* MN[17] = {"book F only", "book F + weakness long (day trades)", "FL5: 5m flag with the day on a straight path, alone", "FLMIX: any 1m/3m/5m flag with the day, alone (~1.5 a day)", "book F + weakness long + FL5", "book F + weakness long + 2-5m flags with the day (1.5R, to close)", "2-5m flags with the day alone (1.5R, to close)", "book F + weakness long + refined flags (10:00 direction, 3-5m, strong breakout)", "refined flags alone", "book F + weakness long + 1-2 flags a day (top 40 %% by score)", "1-2 flags a day alone (top 40 %% by score)", "book F + weakness long + up-to-5 flags (10:00 dir, 3-5m, no ATR rules, to 15:00)", "up-to-5 flags alone", "book F + weakness long + morning flags (same, 10:00-12:00)", "morning flags alone (10:00-12:00)", "book F + weakness long + 5m trailing flag (with the day, straight, no ATR rule)", "5m trailing flag alone"};
  for (int mi = 0; mi < 17; ++mi) { const int withWeak = MASKS[mi];
    printf("\n== %s\n  eval$ fund$ | eval: pass%% fail%% med days | per year: net $  median  losing%%  evals  fundeds  payouts$ | net 2010-18 / 2019-22 / 2023-25\n", MN[mi]);
    for (double er : ER) {
      // eval statistics: start on every session, run up to 120 sessions
      long pass = 0, fail = 0, tot = 0; std::vector<int> dd;
      for (size_t s = 0; s + 120 < ses.size(); ++s) { double bal = 0, peak = 0; int res = 0, k; for (k = 0; k < 120 && !res; ++k) { bal += dayPL(ses[s + k], er, withWeak, 1e9); peak = std::max(peak, bal); if (bal >= 3000) res = 1; else if (bal <= peak - 2000) res = -1; }
        tot++; if (res == 1) { pass++; dd.push_back(k); } else if (res == -1) fail++; }
      std::sort(dd.begin(), dd.end());
      for (double fr : FR) {
        std::vector<double> nets; double es[3] = {0}; long en[3] = {0}; double sE = 0, sF = 0, sP = 0;
        for (size_t s = 0; s < ses.size(); ++s) { if (s > 0 && ses[s] / 100 == ses[s - 1] / 100) continue; if (ses[s] > 20251016) break;
          size_t end = s; while (end < ses.size() && ses[end] < ses[s] + 10000) ++end; if (end >= ses.size()) break;
          double cash = 0, paid = 0; int evals = 0, fundeds = 0; bool funded = false, firstEval = true; double bal = 0, peak = 0, floor = -2000; bool locked = false;
          int pdays = 0; double cyc = 0, big = 0;
          auto newEval = [&]() { cash -= firstEval ? 39 : 35; firstEval = false; evals++; funded = false; bal = 0; peak = 0; };
          newEval();
          for (size_t i = s; i < end; ++i) { int d = ses[i];
            if (!funded) { bal += dayPL(d, er, withWeak, 1e9); peak = std::max(peak, bal);
              if (bal >= 3000) { funded = true; fundeds++; cash -= 189; bal = 0; peak = 0; floor = -2000; locked = false; pdays = 0; cyc = 0; big = 0; }
              else if (bal <= peak - 2000) newEval(); continue; }
            double pl = dayPL(d, fr, withWeak, 1000); bal += pl; cyc += pl; if (pl >= 250) pdays++; big = std::max(big, pl);
            if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (peak >= 2100) { locked = true; floor = 100; } }
            if (bal <= floor) { firstEval = false; newEval(); continue; }
            if (pdays >= 5 && bal >= 2500 && cyc > 0 && big <= 0.4 * cyc) { double w = std::min(1500.0, bal - 2000); if (w >= 500) { paid += 0.9 * w; bal -= w; locked = true; floor = 100; pdays = 0; cyc = 0; big = 0; } } }
          double net = paid + cash; nets.push_back(net); sE += evals; sF += fundeds; sP += paid; int e = ses[s] < 20190101 ? 0 : ses[s] < 20230101 ? 1 : 2; es[e] += net; en[e]++; }
        std::vector<double> so = nets; std::sort(so.begin(), so.end()); long lose = 0; double sm = 0; for (double x : nets) { sm += x; if (x < 0) lose++; } double W = nets.size();
        printf("  %5.0f %5.0f | %5.1f %5.1f %5d     | %+7.0f %+7.0f %6.1f   %5.1f  %5.1f  %8.0f  | %+6.0f %+6.0f %+6.0f\n", er, fr, 100.0 * pass / tot, 100.0 * fail / tot, dd.empty() ? 0 : dd[dd.size() / 2],
               sm / W, so[nets.size() / 2], 100.0 * lose / W, sE / W, sF / W, sP / W, es[0] / en[0], es[1] / en[1], es[2] / en[2]); } } }
  return 0;
}
