// 2026-10-19 (founder: "if it's not NY we only take 10-20 points max"): Asia and London wick-close flags (W1 entries from
// inputs_20261019/flag_wickentry_w1_full.csv, the measured-move rows) re-run from the fill on 1-minute data with a POINT target:
//  MM   measured move (pole length), the reference · FIX T  target T points · CAP T  target = the smaller of the measured move and T.
//  T = 10 / 15 / 20 NQ points at TODAY's range (T × ATR / 320 on older days, the same scaling as the 2-point cost), no trail.
// Stop = the flag stop; "stop ≤ T" also caps the stop at T points (risk never above the target). Flat at the session end
// (Asia 03:00, London 09:30 ET). Net of 2 pts cost. Trades scored on their own. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::map<int, int> dayIx; for (size_t i = 0; i < s.days.size(); ++i) dayIx[s.days[i].date] = i;
  struct E { int se, chart, dir, date; long t; double e, stop, pole; }; std::vector<E> ents; std::set<int> alldays;
  { std::ifstream f("research/programs/inputs_20261019/flag_wickentry_w1_full.csv"); std::string l; std::getline(f, l);
    while (std::getline(f, l)) { std::vector<std::string> c; std::stringstream ss(l); std::string x; while (std::getline(ss, x, ',')) c.push_back(x); if (c.size() < 21 || c[20] != "measured move") continue;
      alldays.insert(std::stoi(c[3])); if (c[0] == "NY") continue;
      struct tm tmv = {}; sscanf((c[4] + " " + c[5]).c_str(), "%d-%d-%d %d:%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday, &tmv.tm_hour, &tmv.tm_min); tmv.tm_year -= 1900; tmv.tm_mon -= 1;
      ents.push_back({c[0] == "Asia" ? 0 : 1, std::stoi(c[1]), c[2] == "bull" ? 1 : -1, std::stoi(c[3]), timegm(&tmv) - 60, std::stod(c[6]), std::stod(c[7]), std::stod(c[19])}); } }
  const int S0[2] = {0, 540}, SE[2] = {540, 930}; const char* SN[2] = {"Asia", "London"};
  const int NX = 13; const char* XN[NX] = {"MM", "FIX10", "FIX15", "FIX20", "CAP10", "CAP15", "CAP20", "FIX10 stop<=10", "FIX15 stop<=15", "FIX20 stop<=20", "CAP10 stop<=10", "CAP15 stop<=15", "CAP20 stop<=20"};
  struct Acc { double n = 0, s = 0, w = 0, n2 = 0, s2 = 0, rr = 0; }; std::map<std::string, std::vector<Acc>> acc;
  for (auto& en : ents) { auto it = dayIx.find(en.date); if (it == dayIx.end()) continue; const Day& d = s.days[it->second]; const double A = d.atr; if (!(A > 0)) continue; int k0 = -1;
    for (int i = d.b0; i < d.b1; ++i) if (s.t[i] == en.t) { k0 = i; break; } if (k0 < 0) continue;
    const int D = en.dir; const double sc = A / 320.0, cost = 2.0 * sc, R0 = D * (en.e - en.stop); if (R0 <= 0) continue;
    char kb[64]; snprintf(kb, sizeof kb, "%-6s %2dm %s", SN[en.se], en.chart, D > 0 ? "bull" : "bear"); auto& V = acc[kb]; if (V.empty()) V.resize(NX);
    for (int x = 0; x < NX; ++x) { int base = x <= 6 ? x : x - 6; double T = base == 0 ? 0 : (base - 1) % 3 == 0 ? 10 : (base - 1) % 3 == 1 ? 15 : 20; T *= sc;
      double tgtDist = base == 0 ? en.pole : base <= 3 ? T : std::min(en.pole, T); double st = en.stop; if (x >= 7) st = D > 0 ? std::max(st, en.e - T) : std::min(st, en.e + T);
      double R = D * (en.e - st), tg = en.e + D * tgtDist, px = NAN;
      for (int i = k0 + 1; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= SE[en.se]) { px = s.o[i]; break; }
        if (D > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (D > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } }
      if (std::isnan(px)) continue; double net = (D * (px - en.e) - cost) / sc;   // points at today's range
      Acc& a = V[x]; a.n++; a.s += net; a.w += net > 0; a.rr += net * sc / R; if (en.date >= 20250101) { a.n2++; a.s2 += net; } } (void)S0; }
  const double ND = alldays.size();
  printf("Asia + London wick-close flags with a 10-20 point target (%zu entries, %.0f days 2010-07 → 2026-10). Points at today's range, net of 2 pts.\n", ents.size(), ND);
  printf("Each cell: net pts per trade / win %% / 2025-26 net pts. FIX = fixed target, CAP = min(measured move, T), stop<=T = stop capped at T.\n");
  for (auto& [key, V] : acc) { printf("\n== %s  (%.2f a day)\n", key.c_str(), V[0].n / ND);
    for (int x = 0; x < NX; ++x) { const Acc& a = V[x]; if (!a.n) continue; printf("  %-16s %+6.2f pts | %+.3fR | %4.1f%% win | 2025-26 %+6.2f (%.0f)\n", XN[x], a.s / a.n, a.rr / a.n, 100 * a.w / a.n, a.n2 ? a.s2 / a.n2 : 0, a.n2); } }
  return 0;
}
