// 2026-10-24 rotation_groups.cpp (preregistered/2026-10-24_rotation_groups.md; founder: "rotate accounts 2 weeks apart in groups").
// Lucid Pro 50K accounts in groups started GAP sessions apart; inside a group one account trades at a time (lock +$750 / day stop
// −$1,000 hands over); eval → funded → first payout. Trades: candidate files with live fills (column 72 = size).
// Usage: rotation_groups CAND_NQ.csv CAND_ES.csv. Read-only on data/edge_lab/cache (trading calendar). tday = minutes since 18:00.
#include "research/meta.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace lab;
struct Tr { int day, em, xm, mk; double usd; };   // day = calendar index
struct Acct { int stage = 0; double bal = 0, peakEod = 0, floor = -2000, dayPnl = 0, fundStart = 0, bestDay = 0; bool locked = false, dead = false, paid = false; int paidDay = -1; double paidAmt = 0; int evalDays = 0, q150 = 0; double evalBest = 0; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: rotation_groups CAND_NQ.csv CAND_ES.csv\n"); return 1; }
  // calendar and $ scaling exactly as flag_prop_ev: NQ days ok, not roll window, >= 370 RTH bars, 2014-01 → 2026-10-16; points scaled
  // by today's ATR / the day's ATR (each market), cost 1.5 NQ / 0.5 ES pts, $2 / $5 per point
  std::vector<int> cal; std::map<int, double> atr[2]; double Anow[2] = {1, 1};
  for (int mk = 0; mk < 2; ++mk) { std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", mk ? "ES" : "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; int nb = 0;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) nb++; }
      if (nb >= 370) { Anow[mk] = d.atr; atr[mk][d.date] = d.atr; if (mk == 0) cal.push_back(d.date); } } }
  std::sort(cal.begin(), cal.end()); cal.erase(std::unique(cal.begin(), cal.end()), cal.end());
  std::map<int, int> di; for (size_t i = 0; i < cal.size(); ++i) di[cal[i]] = (int)i;
  std::vector<Tr> all;
  for (int mk = 0; mk < 2; ++mk) { std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) a.push_back(strtod(t.c_str(), nullptr)); if (a.size() < 72) continue;
      const double mult = a[22], pull = a[24] * 100; const int g = (int)a[6];
      if (!(a[11] > 0.5 && (int)a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2))) continue;
      auto it = di.find((int)a[0]); auto za = atr[mk].find((int)a[0]); if (it == di.end() || za == atr[mk].end()) continue;
      const double cost = mk ? 0.5 : 1.5, ppt = mk ? 5.0 : 2.0, fct = Anow[mk] / za->second;
      all.push_back({it->second, (int)a[12], (int)a[13], mk, ((a[19] + cost) * fct - cost) * ppt * std::max(1.0, a[71])}); } }
  // at most 2 open per market (entry order)
  std::sort(all.begin(), all.end(), [](const Tr& x, const Tr& y) { return x.day != y.day ? x.day < y.day : x.em != y.em ? x.em < y.em : x.mk < y.mk; });
  std::vector<std::vector<Tr>> byDay(cal.size()); { std::vector<Tr> kept;
    for (size_t i = 0; i < all.size(); ++i) { int open = 0; for (const Tr& k : kept) if (k.day == all[i].day && k.mk == all[i].mk && k.xm > all[i].em && k.em <= all[i].em) open++;
      if (open < 2) kept.push_back(all[i]); }
    for (auto& t : kept) byDay[t.day].push_back(t); printf("trades after the 2-open rule: %zu of %zu\n", kept.size(), all.size()); }
  // 2026-10-25 (preregistered/2026-10-25_speed_streams_firms.md): extra NQ strategy streams, env RG_STREAMS = comma list of edge_lab
  // --trades files (date,entry_epoch,exit_epoch,dir,gross_pts,net_pts,...); points scaled to today's NQ ATR, cost 1.5, $2 per point
  if (const char* st = std::getenv("RG_STREAMS")) { std::stringstream sl(st); std::string fn;
    while (std::getline(sl, fn, ',')) { std::ifstream f(fn); if (!f) { perror(fn.c_str()); return 1; } std::string ln; std::getline(f, ln); int n = 0, used = 0;
      while (std::getline(f, ln)) { int d; long long ep, xp; int dir; double g, net; if (sscanf(ln.c_str(), "%d,%lld,%lld,%d,%lf,%lf", &d, &ep, &xp, &dir, &g, &net) != 6) continue; n++;
        auto it = di.find(d); auto za = atr[0].find(d); if (it == di.end() || za == atr[0].end()) continue;
        auto tday = [](long long e) { return (int)(((e % 86400) / 60 - 1080 + 1440) % 1440); };
        byDay[it->second].push_back({it->second, tday(ep), tday(xp), 0, (g * Anow[0] / za->second - 1.5) * 2.0}); used++; }
      printf("stream %s: %d of %d trades on the calendar\n", fn.c_str(), used, n); } }
  const int FIRM = std::getenv("RG_FIRM") ? std::atoi(std::getenv("RG_FIRM")) : 0;   // 0 Lucid Pro 50K · 1 Lucid Flex 50K
  const double LOCK = 750, DSTOP = 1000, FEE = 50;
  struct Cfg { int G, k, gap; }; std::vector<Cfg> cfgs = std::getenv("RG_SHORT") ? std::vector<Cfg>{{4, 1, 10}, {6, 1, 10}} : std::vector<Cfg>{{4, 1, 10}, {2, 2, 10}, {1, 4, 10}, {6, 1, 10}, {3, 2, 10}, {2, 3, 10}, {1, 6, 10}, {4, 2, 10}, {2, 4, 10}, {4, 1, 5}, {2, 2, 5}, {6, 1, 5}, {3, 2, 5}};
  printf("%s · streams %s · ", FIRM ? "Lucid Flex 50K" : "Lucid Pro 50K", std::getenv("RG_STREAMS") ? "flags + extra" : "flags only"); printf("Lucid rules · live fills · $ at today's ATR · lock $%.0f · day stop $%.0f · fee $%.0f · 160 sessions · starts every 5th session 2014-01 → 2025-04\n", LOCK, DSTOP, FEE);
  printf("%-24s | %-6s | P(paid) ≤30 ≤45 ≤60 ≤90 | median 1st | paid/prog | E[payouts] fees  net | 2020-25: P(paid) net\n", "config (groups × size, gap)", "starts");
  for (const Cfg& c : cfgs) {
    double nP = 0, any = 0, w[4] = {}, paidN = 0, payS = 0, nP2 = 0, any2 = 0, net2 = 0; std::vector<int> firsts;
    for (size_t s0 = 0; s0 < cal.size(); s0 += 5) { if (cal[s0] < 20140101 || cal[s0] > 20250430) continue; const int end = (int)s0 + 160; if (end + (c.G - 1) * c.gap >= (int)cal.size()) break;
      const int N = c.G * c.k; std::vector<Acct> A(N); std::vector<int> act(c.G, 0);   // active index inside each group
      int first = -1; double pay = 0; int paidCnt = 0;
      for (int d = (int)s0; d < end + (c.G - 1) * c.gap && d < (int)cal.size(); ++d) {
        for (auto& a : A) { a.dayPnl = 0; a.locked = false; }
        // events in time order: entries assign to the group's active account, exits book P&L and may hand over
        struct Ev { int tm, typ; size_t i; }; std::vector<Ev> ev; for (size_t i = 0; i < byDay[d].size(); ++i) { ev.push_back({byDay[d][i].em, 1, i}); ev.push_back({byDay[d][i].xm, 0, i}); }
        std::sort(ev.begin(), ev.end(), [](const Ev& x, const Ev& y) { return x.tm != y.tm ? x.tm < y.tm : x.typ < y.typ; });
        std::vector<std::vector<int>> owner(byDay[d].size(), std::vector<int>(c.G, -1));
        auto liveIn = [&](int g, int j) { const int idx = g * c.k + j; return d >= (int)s0 + g * c.gap && !A[idx].dead && !A[idx].paid && !A[idx].locked; };
        auto advance = [&](int g) { for (int t = 0; t < c.k; ++t) { if (liveIn(g, act[g])) return; act[g] = (act[g] + 1) % c.k; } };
        for (auto& e : ev) for (int g = 0; g < c.G; ++g) {
          if (e.typ == 1) { advance(g); if (liveIn(g, act[g])) owner[e.i][g] = g * c.k + act[g]; }
          else { const int o = owner[e.i][g]; if (o < 0) continue; Acct& a = A[o]; a.dayPnl += byDay[d][e.i].usd; a.bal += byDay[d][e.i].usd;
            if (!a.locked && (a.dayPnl >= LOCK || a.dayPnl <= -DSTOP)) { a.locked = true; if (act[g] == o - g * c.k) { act[g] = (act[g] + 1) % c.k; advance(g); } } } }
        // end of day: eval pass / fail, funded trail, payout
        for (int idx = 0; idx < N; ++idx) { Acct& a = A[idx]; if (a.dead || a.paid || d < (int)s0 + (idx / c.k) * c.gap) continue;
          if (a.stage == 0) { if (a.bal <= a.floor) { a.dead = true; continue; } if (a.dayPnl != 0) a.evalDays++; a.evalBest = std::max(a.evalBest, a.dayPnl);
            const bool passNow = a.bal >= 3000 && (FIRM == 0 || (a.evalDays >= 3 && a.evalBest <= 0.4 * a.bal));
            if (passNow) { a.stage = 1; a.bal = 0; a.peakEod = 0; a.floor = -2000; a.bestDay = 0; a.q150 = 0; continue; }
            a.peakEod = std::max(a.peakEod, a.bal); a.floor = a.peakEod - 2000; }
          else { if (a.bal <= a.floor) { a.dead = true; continue; } a.bestDay = std::max(a.bestDay, a.dayPnl); a.peakEod = std::max(a.peakEod, a.bal);
            a.floor = a.peakEod >= 2100 ? 100 : a.peakEod - 2000;
            if (a.dayPnl >= 150) a.q150++;
            const bool payNow = FIRM == 0 ? (a.bal >= 2600 && a.bestDay <= 0.4 * a.bal) : (a.q150 >= 5 && a.bal >= 1000);
            if (payNow) { a.paid = true; a.paidDay = d - (int)s0; a.paidAmt = 0.9 * (FIRM == 0 ? std::min(2000.0, a.bal - 2100) : std::min(2000.0, 0.5 * a.bal)); paidCnt++; pay += a.paidAmt; if (first < 0) first = a.paidDay; } } } }
      nP++; const bool p = paidCnt > 0; any += p; if (p) { firsts.push_back(first); const int L[4] = {30, 45, 60, 90}; for (int q = 0; q < 4; ++q) w[q] += first <= L[q]; }
      paidN += paidCnt; payS += pay; if (cal[s0] >= 20200101) { nP2++; any2 += p; net2 += pay - FEE * c.G * c.k; } }
    std::sort(firsts.begin(), firsts.end()); const int med = firsts.empty() ? -1 : firsts[firsts.size() / 2];
    char nm[64]; snprintf(nm, sizeof nm, "%d × %d, %d sessions apart", c.G, c.k, c.gap);
    printf("%-24s | %6.0f | %5.1f%% %3.0f%% %3.0f%% %3.0f%% %3.0f%% | %10d | %9.2f | %8.0f %4d %5.0f | %5.1f%% %6.0f\n", nm, nP, 100 * any / nP, 100 * w[0] / nP, 100 * w[1] / nP, 100 * w[2] / nP, 100 * w[3] / nP, med,
      paidN / nP, payS / nP, (int)(FEE * c.G * c.k), payS / nP - FEE * c.G * c.k, 100 * any2 / std::max(1.0, nP2), net2 / std::max(1.0, nP2)); }
  return 0;
}
