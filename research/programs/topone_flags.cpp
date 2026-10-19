// 2026-10-19 (founder: "lets do the math for a 50k, which is better, trail or no trail"): the NY wick-break flags (flag_wickentry.cpp,
// list 2010 → 2026, inputs_20261019/flag_wickentry_full.csv) on a TopOne 50K, trailing exit vs no trail (measured move).
// Each trade is sized to $risk at its own stop in micros ($2 a point, max 30 = 3 minis), cost 2 NQ pts scaled to the day's range
// (2 x ATR / 320). Eval as pasted: +$3,000, $2,000 EOD trailing, 21 trading days, no daily limit. Funded: 5 days >= $250, balance >=
// +$2,500, $500-1,500 a payout at 90 %, drawdown locks at +$100 after +$2,100 (unconfirmed), consistency off. Reported per chart set and
// risk: eval pass %, median days to pass, share of evals that reach a first payout and the days to it, and a one-account chain (eval →
// funded → payouts → new eval when blown, $85 an eval, $189 activation) as $ per year. argv: inputs dir.
#include <cstdio>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
struct Tr { int chart, side, hhmm; double pts, risk; };
int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : "research/programs/inputs_20261019"; std::string listFile = argc > 2 ? argv[2] : "flag_wickentry_full.csv";   // argv[2]: trade list (W2 default; flag_wickentry_w1_full.csv = entry at the wick candle close)
  std::map<int, double> atr; std::vector<int> ses;
  { std::ifstream f(dir + "/nq_atr_edge_lab.csv"); std::string l; while (std::getline(f, l)) { int d; double a; if (sscanf(l.c_str(), "%d,%lf", &d, &a) == 2) atr[d] = a; } }
  { std::ifstream f(dir + "/sessions_all.csv"); std::string l; while (std::getline(f, l)) { int d; if (sscanf(l.c_str(), "%d", &d) == 1 && atr.count(d) && d >= 20100722 && d <= 20261016) ses.push_back(d); } }
  std::map<int, std::vector<Tr>> byMode[2];
  { std::ifstream f(dir + "/" + listFile); std::string l; std::getline(f, l);
    while (std::getline(f, l)) { std::vector<std::string> c; std::stringstream ss(l); std::string x; while (std::getline(ss, x, ',')) c.push_back(x); if (c.size() < 21 || c[0] != "NY") continue;
      int day = std::stoi(c[3]); if (!atr.count(day)) continue; int mode = c[20] == "trail" ? 0 : 1; byMode[mode][day].push_back({std::stoi(c[1]), c[2] == "bull" ? 1 : -1, std::stoi(c[5].substr(0, 2)) * 100 + std::stoi(c[5].substr(3, 2)), std::stod(c[12]), std::stod(c[8])}); } }
  struct Set { const char* n; std::set<int> charts; int side; bool core = false; int lastEntry = 2400; }; std::vector<Set> sets = {{"4m", {4}, 0}, {"5m", {5}, 0}, {"4m + 5m", {4, 5}, 0}, {"2m + 3m + 4m + 5m", {2, 3, 4, 5}, 0}, {"BULL ONLY 2m + 3m + 4m + 5m", {2, 3, 4, 5}, 1}, {"BULL ONLY 4m + 5m", {4, 5}, 1}, {"BULL ONLY 4m", {4}, 1}, {"BULL ONLY 5m", {5}, 1}, {"BULL ONLY 4m + 5m, entries before 14:00", {4, 5}, 1, false, 1400},
    {"MAIN PLAN ONLY (book F + weakness long)", {}, 0, true}, {"MAIN PLAN + bull 4m + 5m", {4, 5}, 1, true}, {"MAIN PLAN + bull 4m + 5m before 14:00", {4, 5}, 1, true, 1400}};
  // the main plan's day trades (same _k files as topone_sim): sized to $risk at their own stop
  std::map<int, std::vector<std::pair<double, double>>> core; for (const char* fn : {"book_F_k.csv", "weakness_long_1p5atr_k.csv"}) { std::ifstream f(dir + "/" + fn); std::string l;
    while (std::getline(f, l)) { int d; long e, x; int di; double g, nn, mae, mfe, sr, kk; if (sscanf(l.c_str(), "%d,%ld,%ld,%d,%lf,%lf,%lf,%lf,%lf,%lf", &d, &e, &x, &di, &g, &nn, &mae, &mfe, &sr, &kk) != 10 || !atr.count(d)) continue; core[d].push_back({g, kk * atr[d]}); } }
  auto dayPL = [&](int d, int mode, const Set& st, double risk) { double pl = 0; const std::set<int>& ch = st.charts; const int side = st.side; double A = atr[d];
    if (st.core && core.count(d)) for (auto& t : core[d]) { double mnq = std::min(30.0, risk / (t.second * 2.0)); pl += mnq * 2.0 * (t.first - 2.0 * A / 320.0); }
    auto it = byMode[mode].find(d); if (it == byMode[mode].end()) return pl;
    for (auto& t : it->second) { if (!ch.count(t.chart) || t.risk <= 0 || (side && t.side != side) || t.hhmm >= st.lastEntry) continue; double mnq = std::min(30.0, risk / (t.risk * 2.0)); pl += mnq * 2.0 * (t.pts - 2.0 * A / 320.0); } return pl; };
  printf("TopOne 50K with the NY wick flags (%s) — trail vs no trail. Evals started on every NY day %d → %d (%zu days).\n", listFile.c_str(), ses.front(), ses.back(), ses.size());
  for (auto& st : sets) for (int mode = 0; mode < 2; ++mode) {
    printf("\n== %s, %s\n  risk  | eval pass %%  median days | reach 1st payout %%  median days | one account chain: $ per year  losing years | by era 2010-18 / 2019-24 / 2025-26 ($/yr)\n", st.n, mode ? "NO TRAIL (measured move)" : "TRAIL (previous bar after +1R)");
    for (double risk : {250.0, 500.0, 750.0, 1000.0, 1500.0}) {
      long tot = 0, pass = 0, paid = 0; std::vector<int> dP, dF;
      for (size_t s0 = 0; s0 + 21 < ses.size(); ++s0) { tot++; double bal = 0, peak = 0; int res = 0; size_t k;
        for (k = 0; k < 21 && !res; ++k) { bal += dayPL(ses[s0 + k], mode, st, risk); peak = std::max(peak, bal); if (bal >= 3000) res = 1; else if (bal <= peak - 2000) res = -1; }
        if (res != 1) continue; pass++; dP.push_back(k); size_t f0 = s0 + k; bal = 0; peak = 0; double floor = -2000; bool locked = false; int pd = 0;
        for (size_t i = f0; i < ses.size() && i < f0 + 250; ++i) { double pl = dayPL(ses[i], mode, st, risk); bal += pl; if (pl >= 250) pd++;
          if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (peak >= 2100) { locked = true; floor = 100; } } if (bal <= floor) break;
          if (pd >= 5 && bal >= 2500) { paid++; dF.push_back(i - s0 + 1); break; } } }
      // one-account chain over the whole history, per calendar year
      std::map<int, double> yr; double cash = 0; bool funded = false, first = true, locked = false; double bal = 0, peak = 0, floor = -2000; int pd = 0, evalDays = 0;
      auto newEval = [&](int y) { yr[y] -= first ? 39 : 85; first = false; funded = false; bal = 0; peak = 0; evalDays = 0; };
      newEval(ses[0] / 10000);
      for (int d : ses) { int y = d / 10000; double pl = dayPL(d, mode, st, risk);
        if (!funded) { evalDays++; bal += pl; peak = std::max(peak, bal); if (bal >= 3000) { funded = true; yr[y] -= 189; bal = 0; peak = 0; floor = -2000; locked = false; pd = 0; } else if (bal <= peak - 2000 || evalDays >= 21) newEval(y); continue; }
        bal += pl; if (pl >= 250) pd++; if (!locked) { peak = std::max(peak, bal); floor = peak - 2000; if (peak >= 2100) { locked = true; floor = 100; } }
        if (bal <= floor) { newEval(y); continue; }
        if (pd >= 5 && bal >= 2500) { double w = std::min(1500.0, bal - 2000); if (w >= 500) { yr[y] += 0.9 * w; bal -= w; locked = true; floor = 100; pd = 0; } } }
      double sum = 0, e[3] = {0}; int ne[3] = {0}, lose = 0; for (auto& [y, v] : yr) { sum += v; if (v < 0) lose++; int er = y < 2019 ? 0 : y < 2025 ? 1 : 2; e[er] += v; ne[er]++; }
      std::sort(dP.begin(), dP.end()); std::sort(dF.begin(), dF.end());
      printf("  $%5.0f |  %5.1f        %3d        |  %5.1f            %3d          |   %+8.0f          %2d of %2zu    | %+7.0f / %+7.0f / %+7.0f\n", risk, 100.0 * pass / tot, dP.empty() ? 0 : dP[dP.size() / 2], 100.0 * paid / tot, dF.empty() ? 0 : dF[dF.size() / 2],
             sum / yr.size(), lose, yr.size(), e[0] / std::max(1, ne[0]), e[1] / std::max(1, ne[1]), e[2] / std::max(1, ne[2])); } }
  return 0;
}
