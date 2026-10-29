// 2026-10-28 wave_days.cpp (founder: "best days to trade"). Reads one setup's trade list dumped by ema721_wave_search
// (`ema721_wave_search SYM tf k window filter exit` → out/ema721_wave_trades_<SYM>.csv) and splits it by the day it was taken:
// weekday, and the calendar days known in advance (FOMC, NFP, CPI, PPI, monthly opex, big-tech earnings, the day before a
// holiday, first / last 3 trading days of the month). Net pts/trade are already cost-paid and scaled to today's ATR by the
// dumper. Periods 2010-20 | 2021-24 | 2025-26. A day "counts" only if it is better in all three periods AND on ES too.
// Usage: wave_days SYM trades.csv [label]. Output stdout + out/wave_days_<label>_<SYM>.csv.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace lab;
struct A { double s[3] = {}, n[3] = {}, gw[3] = {}, gl[3] = {}; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: wave_days SYM trades.csv [label]\n"); return 2; }
  const std::string sym = argv[1], label = argc > 3 ? argv[3] : "setup";
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); load_events("data/edge_lab/cache", s);
  std::map<int, const Day*> byDate; for (const Day& d : s.days) byDate[d.date] = &d;
  static const char* NM[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "FOMC", "NFP", "CPI", "PPI", "opex", "earnings", "pre-holiday",
                             "month first 3", "month last 3", "no event", "ALL"};
  enum { NB = 16 }; A a[NB];
  std::ifstream in(argv[2]); std::string line; std::getline(in, line); long miss = 0;
  while (std::getline(in, line)) { std::stringstream ss(line); std::string f[9]; for (auto& x : f) std::getline(ss, x, ',');
    const int date = std::stoi(f[0]); const double net = std::stod(f[6]); auto it = byDate.find(date);
    if (it == byDate.end()) { ++miss; continue; } const Day& d = *it->second;
    const int y = date / 10000, p = y <= 2020 ? 0 : y <= 2024 ? 1 : 2;
    auto add = [&](int b) { a[b].s[p] += net; a[b].n[p]++; (net > 0 ? a[b].gw[p] : a[b].gl[p]) += std::fabs(net); };
    if (d.dow >= 1 && d.dow <= 5) add(d.dow - 1);
    const bool ev = d.fomc || d.nfp || d.cpi || d.ppi || d.opex || d.earn || d.prehol;
    if (d.fomc) add(5); if (d.nfp) add(6); if (d.cpi) add(7); if (d.ppi) add(8); if (d.opex) add(9); if (d.earn) add(10);
    if (d.prehol) add(11); if (d.tdom >= 1 && d.tdom <= 3) add(12); if (d.tdleft >= 0 && d.tdleft <= 2) add(13); if (!ev) add(14); add(15); }
  auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
  FILE* o = std::fopen(("research/programs/out/wave_days_" + label + "_" + sym + ".csv").c_str(), "w");
  fprintf(o, "bucket,n1,pts1,pf1,n2,pts2,pf2,n3,pts3,pf3,n_all,pts_all\n");
  printf("%s %s — net pts/trade (cost paid, today's ATR) by day. %ld trades had no day row.\n%-14s %-20s %-20s %-20s %s\n", label.c_str(), sym.c_str(), miss,
         "day", "2010-20 n pts PF", "2021-24 n pts PF", "2025-26 n pts PF", "all n pts");
  for (int b = 0; b < NB; ++b) { const A& x = a[b]; const double n = x.n[0] + x.n[1] + x.n[2], t = x.s[0] + x.s[1] + x.s[2];
    printf("%-14s ", NM[b]); fprintf(o, "%s", NM[b]);
    for (int p = 0; p < 3; ++p) { const double av = x.n[p] ? x.s[p] / x.n[p] : 0;
      printf("%4.0f %+6.2f %4.2f     ", x.n[p], av, pf(x.gw[p], x.gl[p])); fprintf(o, ",%.0f,%.3f,%.3f", x.n[p], av, pf(x.gw[p], x.gl[p])); }
    printf("%5.0f %+6.2f\n", n, n ? t / n : 0); fprintf(o, ",%.0f,%.3f\n", n, n ? t / n : 0); }
  std::fclose(o); return 0;
}
