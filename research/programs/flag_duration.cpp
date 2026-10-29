// 2026-10-22 flag_duration.cpp (founder: "also the bull flag cant be consolidating fro 15 m"). How long the flag had been consolidating when the
// trade entered = minutes from the start of the pole-high bar (registration − 3 chart bars) to the entry fill. Rule to test, fixed before the
// run: TAKE only flags entered within 15 minutes of the pole high (consolidation <= 15 min), vs the rest; also buckets <= 10 / 10-15 / 15-20 /
// 20-30 / > 30 min, and per chart. Flag-mid trade as exported (flag_hhhl: stop flag low − 0.1 ATR, measured move, flat 15:55), one position
// at a time, 2014-19 / 2020-26 halves, NQ and ES. Usage: flag_duration NQ_EXPORT.csv ES_EXPORT.csv. Read-only. tday = minutes since 18:00 ET.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
struct Fl { int date, side, tf, em, xm, grade, hh5, orb, dur; double r; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_duration NQ_EXPORT.csv ES_EXPORT.csv\n"); return 1; }
  printf("How long the flag consolidated before the entry (pole-high bar start → entry fill) — NQ and ES 2014-01 → 2026-10, flag-mid trade, R after cost.\n");
  printf("Columns: trades (a year), avg R, win %%, PF, 2014-19 / 2020-26 avg R. One position at a time within each row.\n");
  for (int mk = 0; mk < 2; ++mk) { std::vector<Fl> v; std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; const int tf = (int)a[2], reg = (int)a[3], em = (int)a[12];
      v.push_back({(int)a[0], (int)a[1], tf, em, (int)a[13], (int)a[6], (int)a[7], (int)a[47], em - (reg - 3 * tf), a[18]}); }
    std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    auto line = [&](const std::string& nm, std::function<bool(const Fl&)> sel) { double n = 0, s = 0, w = 0, gw = 0, gl = 0, h[2] = {0, 0}, hn[2] = {0, 0}; int lastD = 0, busy = -1;
      for (auto& x : v) { if (!sel(x)) continue; if (x.date != lastD) { lastD = x.date; busy = -1; } if (x.em <= busy) continue; busy = x.xm; n++; s += x.r; w += x.r > 0; (x.r > 0 ? gw : gl) += std::fabs(x.r);
        const int k = x.date >= 20200101; h[k] += x.r; hn[k]++; }
      if (n < 15) { printf("    %-44s fewer than 15 trades (%.0f)\n", nm.c_str(), n); return; }
      printf("    %-44s %5.0f (%5.1f/yr) %+6.3fR win %4.1f%% PF %.2f · 2014-19 %+6.3f / 2020-26 %+6.3f\n", nm.c_str(), n, n / 12.8, s / n, 100 * w / n, gl > 0 ? gw / gl : 99.0, hn[0] ? h[0] / hn[0] : 0, hn[1] ? h[1] / hn[1] : 0); };
    struct Set { const char* n; std::function<bool(const Fl&)> f; };
    const Set sets[] = {{"BULL, A-list + 5m HH/HL + with the break", [](const Fl& x) { return x.side > 0 && x.grade == 1 && x.hh5 && x.orb > 0; }},
                        {"BOTH SIDES, A-list + 5m HH/HL + with the break", [](const Fl& x) { return x.grade == 1 && x.hh5 && x.orb > 0; }},
                        {"bull, every flag", [](const Fl& x) { return x.side > 0; }}, {"every flag, both sides", [](const Fl&) { return true; }}};
    for (auto& S0 : sets) { printf("\n######## %s — %s\n", mk ? "ES" : "NQ", S0.n);
      line("all", S0.f);
      line("consolidated <= 15 min (TAKE)", [&](const Fl& x) { return S0.f(x) && x.dur <= 15; });
      line("consolidated > 15 min (SKIP)", [&](const Fl& x) { return S0.f(x) && x.dur > 15; });
      const int B[6] = {0, 10, 15, 20, 30, 999}; static const char* BN[5] = {"<= 10 min", "10-15 min", "15-20 min", "20-30 min", "> 30 min"};
      for (int k = 0; k < 5; ++k) line(std::string("   ") + BN[k], [&, k](const Fl& x) { return S0.f(x) && x.dur > B[k] - (k == 0) && x.dur <= B[k + 1]; });
      for (int tf = 3; tf <= 5; ++tf) { line("   " + std::to_string(tf) + "m chart, <= 15 min", [&, tf](const Fl& x) { return S0.f(x) && x.tf == tf && x.dur <= 15; });
        line("   " + std::to_string(tf) + "m chart, > 15 min", [&, tf](const Fl& x) { return S0.f(x) && x.tf == tf && x.dur > 15; }); } } }
  return 0;
}
