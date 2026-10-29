// 2026-10-22 flag_matrix_best.cpp (founder: "give me the best scenario on the matrix"). Searches the Flag Matrix (flag_hhhl `export wide15`:
// 1-5m charts, pole >= 2×, pullback 5-70 %) for the best filter combination: charts × pole ÷ average bar range × pullback range × grade × 5m
// structure × opening-range break × side (9,216 combinations). Two views:
//   ALL YEARS: rank by the WORSE of NQ and ES avg R, needing >= 60 trades on each market and both halves (2014-19, 2020-26) > 0 on both.
//   PICK EARLY, TEST LATE: rank on 2014-2021 only (>= 40 trades a market, both > 0), then show the top picks' 2022-2026 results — years the
//   pick never saw. That is the honest number; the all-years ranking is the best case and is inflated by the search itself.
// One position at a time per market. Usage: flag_matrix_best W15_NQ.csv W15_ES.csv. Read-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>
struct Fl { int date, em, xm, tf, side, grade, hh5, orb; double r, mult, pull; };
struct C { int ch, plo, phi, pb, g, h, o, s; };
static const int CH[8][2] = {{1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {3, 3}, {4, 4}, {2, 2}};
static const double PLO[4] = {2, 3, 4, 5}, PHI[2] = {7, 1e9};
static const double PB[6][2] = {{5, 25}, {15, 25}, {5, 38}, {15, 38}, {15, 50}, {5, 50}};
static std::string name(const C& c) { char b[220]; snprintf(b, sizeof b, "%s · pole %g-%s× · pullback %g-%g %% · %s · %s · %s · %s", CH[c.ch][0] == CH[c.ch][1] ? (std::to_string(CH[c.ch][0]) + "m").c_str() : (std::to_string(CH[c.ch][0]) + "-" + std::to_string(CH[c.ch][1]) + "m").c_str(),
    PLO[c.plo], c.phi ? "∞" : "7", PB[c.pb][0], PB[c.pb][1], c.g == 0 ? "any grade" : c.g == 1 ? "A or B" : "A-list", c.h ? "5m HH/HL" : "any structure", c.o ? "with the break" : "any break", c.s ? "bull only" : "both sides"); return b; }
static bool in(const Fl& f, const C& c) { return f.tf >= CH[c.ch][0] && f.tf <= CH[c.ch][1] && f.mult >= PLO[c.plo] && f.mult < PHI[c.phi] && f.pull >= PB[c.pb][0] && f.pull < PB[c.pb][1] &&
    (c.g == 0 || (c.g == 1 ? (f.grade == 1 || f.grade == 2) : f.grade == 1)) && (!c.h || f.hh5) && (!c.o || f.orb > 0) && (!c.s || f.side > 0); }
struct St { double n = 0, s = 0, gw = 0, gl = 0, w = 0, h[2] = {0, 0}, hn[2] = {0, 0}; double m() const { return n ? s / n : -9; } };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_matrix_best W15_NQ.csv W15_ES.csv\n"); return 1; }
  std::vector<Fl> M[2];
  for (int mk = 0; mk < 2; ++mk) { std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; M[mk].push_back({(int)a[0], (int)a[12], (int)a[13], (int)a[2], (int)a[1], (int)a[6], (int)a[7], (int)a[47], a[18], a[22], a[24] * 100}); }
    std::sort(M[mk].begin(), M[mk].end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); }
  auto run = [&](int mk, const C& c, int d0, int d1) { St a; int lastD = 0, busy = -1;
    for (auto& f : M[mk]) { if (f.date < d0 || f.date > d1 || !in(f, c)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm;
      a.n++; a.s += f.r; a.w += f.r > 0; (f.r > 0 ? a.gw : a.gl) += std::fabs(f.r); const int k = f.date >= 20200101; a.h[k] += f.r; a.hn[k]++; } return a; };
  std::vector<C> all; for (int ch = 0; ch < 8; ++ch) for (int plo = 0; plo < 4; ++plo) for (int phi = 0; phi < 2; ++phi) for (int pb = 0; pb < 6; ++pb) for (int g = 0; g < 3; ++g) for (int h = 0; h < 2; ++h) for (int o = 0; o < 2; ++o) for (int s = 0; s < 2; ++s) all.push_back({ch, plo, phi, pb, g, h, o, s});
  struct Sc { double score; C c; St a[2]; }; std::vector<Sc> full, early;
  for (auto& c : all) { St a[2] = {run(0, c, 20140101, 20261031), run(1, c, 20140101, 20261031)};
    if (a[0].n >= 60 && a[1].n >= 60 && a[0].hn[0] && a[0].hn[1] && a[1].hn[0] && a[1].hn[1] && a[0].h[0] > 0 && a[0].h[1] > 0 && a[1].h[0] > 0 && a[1].h[1] > 0) full.push_back({std::min(a[0].m(), a[1].m()), c, {a[0], a[1]}});
    St b[2] = {run(0, c, 20140101, 20211231), run(1, c, 20140101, 20211231)}; if (b[0].n >= 40 && b[1].n >= 40) early.push_back({std::min(b[0].m(), b[1].m()), c, {b[0], b[1]}}); }
  std::sort(full.begin(), full.end(), [](const Sc& a, const Sc& b) { return a.score > b.score; }); std::sort(early.begin(), early.end(), [](const Sc& a, const Sc& b) { return a.score > b.score; });
  auto pf = [](const St& a) { return a.gl > 0 ? a.gw / a.gl : 99.0; };
  printf("Best scenarios on the Flag Matrix — %zu combinations searched (charts × pole × pullback × grade × structure × break × side), NQ and ES 2014-01 → 2026-10.\n", all.size());
  printf("\n######## ALL YEARS — ranked by the worse market's avg R; >= 60 trades on each market, both halves > 0 on both (best case: picked with hindsight)\n");
  for (size_t q = 0; q < full.size() && q < 15; ++q) { const auto& x = full[q];
    printf("  %2zu. %s\n      NQ %4.0f tr (%4.1f/yr) %+6.3fR win %4.1f%% PF %.2f · ES %4.0f tr (%4.1f/yr) %+6.3fR win %4.1f%% PF %.2f\n", q + 1, name(x.c).c_str(), x.a[0].n, x.a[0].n / 12.8, x.a[0].m(), 100 * x.a[0].w / x.a[0].n, pf(x.a[0]),
           x.a[1].n, x.a[1].n / 12.8, x.a[1].m(), 100 * x.a[1].w / x.a[1].n, pf(x.a[1])); }
  printf("  (%zu combinations meet the conditions)\n", full.size());
  printf("\n######## PICK ON 2014-2021, TEST ON 2022-2026 (the honest view) — top 10 by the worse market on 2014-21, then their unseen 2022-26 results\n");
  double sumLate = 0; int nl = 0;
  for (size_t q = 0; q < early.size() && q < 10; ++q) { const auto& x = early[q]; const St l[2] = {run(0, x.c, 20220101, 20261031), run(1, x.c, 20220101, 20261031)};
    printf("  %2zu. %s\n      picked on 2014-21: NQ %+6.3fR (%3.0f) · ES %+6.3fR (%3.0f)   →   2022-26: NQ %+6.3fR (%3.0f, PF %.2f) · ES %+6.3fR (%3.0f, PF %.2f)\n", q + 1, name(x.c).c_str(), x.a[0].m(), x.a[0].n, x.a[1].m(), x.a[1].n,
           l[0].m(), l[0].n, pf(l[0]), l[1].m(), l[1].n, pf(l[1])); if (l[0].n) { sumLate += l[0].m() + l[1].m(); nl += 2; } }
  const C rule{2, 2, 1, 4, 2, 1, 1, 0}, bull{2, 2, 1, 4, 2, 1, 1, 1};
  for (auto [nm, c] : {std::pair<const char*, C>{"today's rule (3-5m, pole >= 4×, 15-50 %, A, HH/HL, break, both sides)", rule}, {"bull rule", bull}}) { const St l[2] = {run(0, c, 20220101, 20261031), run(1, c, 20220101, 20261031)}, e[2] = {run(0, c, 20140101, 20211231), run(1, c, 20140101, 20211231)};
    printf("  reference — %s\n      2014-21: NQ %+6.3fR (%3.0f) · ES %+6.3fR (%3.0f)   →   2022-26: NQ %+6.3fR (%3.0f) · ES %+6.3fR (%3.0f)\n", nm, e[0].m(), e[0].n, e[1].m(), e[1].n, l[0].m(), l[0].n, l[1].m(), l[1].n); }
  printf("  average 2022-26 R of the top 10 early picks: %+6.3fR\n", nl ? sumLate / nl : 0);
  return 0;
}
