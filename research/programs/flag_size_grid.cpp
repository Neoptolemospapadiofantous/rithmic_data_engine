// 2026-10-22 flag_size_grid.cpp (founder: "so test smaller/biger poles and smaller/bigger flags"). Pole size × flag size on one grid, from
// flag_hhhl.cpp `export FILE wide` (pole >= 2× the average bar, pullback 5-70 % of the pole; context and everything else as the rule; grades
// refit on this wider set). Pole = pole height / the session's average bar (2-3 / 3-4 / 4-5 / 5-7 / >= 7) and pole / daily ATR (< 0.15 /
// 0.15-0.25 / 0.25-0.40 / >= 0.40); flag = pullback depth as a share of the pole (5-15 / 15-25 / 25-38 / 38-50 / 50-62 / 62-70 %). Today's
// rule = pole >= 4× and pullback 15-50 %. Each cell: avg R (trades) and a mark when both halves (2014-19, 2020-26) are > 0; one position at a
// time within each cell. Sets: every flag, and A-list + 5m HH/HL + with the break, both sides. Usage: flag_size_grid WIDE_NQ WIDE_ES. Read-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
struct Fl { int date, em, xm, grade, hh5, orb; double r, mult, poleAtr, pull; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_size_grid WIDE_NQ WIDE_ES\n"); return 1; }
  const double PB[7] = {0.05, 0.15, 0.25, 0.38, 0.50, 0.62, 0.70001}; static const char* PN[6] = {"5-15%", "15-25%", "25-38%", "38-50%", "50-62%", "62-70%"};
  const double MB[6] = {2, 3, 4, 5, 7, 1e9}; static const char* MN[5] = {"2-3×", "3-4×", "4-5×", "5-7×", ">=7×"};
  const double AB[5] = {0, 0.15, 0.25, 0.40, 1e9}; static const char* AN[4] = {"<0.15 ATR", "0.15-0.25", "0.25-0.40", ">=0.40 ATR"};
  printf("Pole size × flag size — NQ and ES 2014-01 → 2026-10, flag-mid trade (stop flag low − 0.1 ATR, measured move, flat 15:55), R after cost.\n");
  printf("Cell: avg R (trades); * = both halves 2014-19 and 2020-26 > 0. Today's rule = pole >= 4× and pullback 15-50 %%. One position at a time within a cell.\n");
  for (int mk = 0; mk < 2; ++mk) { std::vector<Fl> v; std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; v.push_back({(int)a[0], (int)a[12], (int)a[13], (int)a[6], (int)a[7], (int)a[47], a[18], a[22], a[23], a[24]}); }
    std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    auto cell = [&](std::function<bool(const Fl&)> sel) { double n = 0, s = 0, h[2] = {0, 0}, hn[2] = {0, 0}; int lastD = 0, busy = -1;
      for (auto& x : v) { if (!sel(x)) continue; if (x.date != lastD) { lastD = x.date; busy = -1; } if (x.em <= busy) continue; busy = x.xm; n++; s += x.r; const int k = x.date >= 20200101; h[k] += x.r; hn[k]++; }
      char b[48]; if (n < 15) snprintf(b, sizeof b, "   —  (%3.0f)  ", n); else snprintf(b, sizeof b, "%+6.2f (%4.0f)%s", s / n, n, hn[0] && hn[1] && h[0] > 0 && h[1] > 0 ? "*" : " "); return std::string(b); };
    struct Set { const char* n; std::function<bool(const Fl&)> f; };
    const Set sets[] = {{"every flag", [](const Fl&) { return true; }}, {"A-list + 5m HH/HL + with the break", [](const Fl& x) { return x.grade == 1 && x.hh5 && x.orb > 0; }}};
    for (auto& S0 : sets) {
      printf("\n######## %s — %s — pole / average bar (rows) × pullback (columns)\n    %-10s", mk ? "ES" : "NQ", S0.n, ""); for (int j = 0; j < 6; ++j) printf(" | %-14s", PN[j]); printf(" | all pullbacks\n");
      for (int i = 0; i < 5; ++i) { printf("    %-10s", MN[i]); for (int j = 0; j < 6; ++j) printf(" | %s", cell([&](const Fl& x) { return S0.f(x) && x.mult >= MB[i] && x.mult < MB[i + 1] && x.pull >= PB[j] && x.pull < PB[j + 1]; }).c_str());
        printf(" | %s\n", cell([&](const Fl& x) { return S0.f(x) && x.mult >= MB[i] && x.mult < MB[i + 1]; }).c_str()); }
      printf("    %-10s", "all poles"); for (int j = 0; j < 6; ++j) printf(" | %s", cell([&](const Fl& x) { return S0.f(x) && x.pull >= PB[j] && x.pull < PB[j + 1]; }).c_str());
      printf(" | %s\n", cell(S0.f).c_str());
      printf("    today's rule (pole >= 4×, pullback 15-50 %%): %s\n", cell([&](const Fl& x) { return S0.f(x) && x.mult >= 4 && x.pull >= 0.15 && x.pull < 0.50; }).c_str());
      printf("\n    %s — %s — pole / daily ATR (rows) × pullback (columns)\n    %-10s", mk ? "ES" : "NQ", S0.n, ""); for (int j = 0; j < 6; ++j) printf(" | %-14s", PN[j]); printf(" | all pullbacks\n");
      for (int i = 0; i < 4; ++i) { printf("    %-10s", AN[i]); for (int j = 0; j < 6; ++j) printf(" | %s", cell([&](const Fl& x) { return S0.f(x) && x.poleAtr >= AB[i] && x.poleAtr < AB[i + 1] && x.pull >= PB[j] && x.pull < PB[j + 1]; }).c_str());
        printf(" | %s\n", cell([&](const Fl& x) { return S0.f(x) && x.poleAtr >= AB[i] && x.poleAtr < AB[i + 1]; }).c_str()); } } }
  return 0;
}
