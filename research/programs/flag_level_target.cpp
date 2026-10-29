// 2026-10-22 flag_level_target.cpp (founder: "what about targeting session highs and lows"). The strongest flag rule (flag_rule_check.cpp RULE B =
// bull flag, A-list, 5m HH/HL, 5m opening range broken up; RULE 2 = both sides), same entry (flag-mid limit) and stop (flag low − 0.1 ATR), with the
// target moved to a session level instead of the measured move. Levels known before the entry, beyond the entry in the trade's direction
// (bull: highs above, bear: lows below): the overnight high (18:00-09:30), Asia (18:00-02:00), London (02:00-08:00), yesterday's RTH high, last
// week's and last month's high. Targets fixed before the run:
//   MM    measured move (today's rule — re-simulated here; must match the export)
//   L.5   the nearest level at least 0.5 R away (no level → measured move)        L1   … at least 1 R away
//   NEAR  the CLOSER of the measured move and the nearest level >= 0.5 R          FAR  the FARTHER of the two
//   BEYOND the first level beyond the measured move (no level → hold)             HOLD no target, out at 15:55
//   SPLIT half at the nearest level >= 0.5 R, half at the measured move
// One position at a time (the export's exit minute), cost per trade as exported. Usage: flag_level_target SYM EXPORT.csv. Read-only.
// tday = minutes since 18:00 ET (02:00 = 480, 08:00 = 840, 09:30 = 930, 15:55 = 1315).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Fl { int date, side, em, xm, grade, hh5; double e, st, tg, r, orb; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_level_target SYM EXPORT.csv\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::vector<Fl> X; { std::ifstream f(argv[2]); if (!f) { perror(argv[2]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; X.push_back({(int)a[0], (int)a[1], (int)a[12], (int)a[13], (int)a[6], (int)a[7], a[14], a[15], a[16], a[18], a[47]}); } }
  std::sort(X.begin(), X.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { double lv[2][6]; std::vector<double> O, H, L, C; std::vector<int> TM; };   // lv[0] highs, lv[1] lows: ON, Asia, London, PD, PW, PM
  std::map<int, DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || d.date < 20140101 || d.date > 20261016) continue; DB z;
    double h[3] = {-1e18, -1e18, -1e18}, l[3] = {1e18, 1e18, 1e18};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]);
      if (tm < 930) { h[0] = std::max(h[0], (double)s.h[i]); l[0] = std::min(l[0], (double)s.l[i]); const int w = tm < 480 ? 1 : tm < 840 ? 2 : -1;
        if (w > 0) { h[w] = std::max(h[w], (double)s.h[i]); l[w] = std::min(l[w], (double)s.l[i]); } }
      else if (tm < 1320) { z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); } }
    const double ph[6] = {h[0], h[1], h[2], d.ph, d.pwh, d.pmh}, pl[6] = {l[0], l[1], l[2], d.pl, d.pwl, d.pml};
    for (int j = 0; j < 6; ++j) { z.lv[0][j] = std::isfinite(ph[j]) && ph[j] > -1e17 ? ph[j] : NAN; z.lv[1][j] = std::isfinite(pl[j]) && pl[j] < 1e17 ? pl[j] : NAN; }
    if (z.O.size() >= 370) days[d.date] = std::move(z); }
  // simulate a long (side +1) or short from the entry minute: stop, one target (NAN = none), flat 15:55; returns gross points and exit minute
  auto sim = [](const DB& z, const Fl& f, double tg, int& xm) -> double { const int sd = f.side; size_t k0 = 0; while (k0 < z.TM.size() && z.TM[k0] < f.em) ++k0; xm = 1315;
    if (k0 >= z.TM.size()) return 0; if (sd > 0 ? z.L[k0] <= f.st : z.H[k0] >= f.st) { xm = z.TM[k0]; return -std::fabs(f.e - f.st); }
    for (size_t k = k0 + 1; k < z.TM.size(); ++k) { if (z.TM[k] >= 1315) { xm = z.TM[k]; return sd * (z.O[k] - f.e); }
      if (sd > 0 ? z.L[k] <= f.st : z.H[k] >= f.st) { xm = z.TM[k]; return -std::fabs(f.e - f.st); }
      if (!std::isnan(tg) && (sd > 0 ? z.H[k] >= tg : z.L[k] <= tg)) { xm = z.TM[k]; return std::fabs(tg - f.e); } }
    return sd * (z.C.back() - f.e); };
  // nearest level beyond `from` in the trade's direction (NAN = none)
  auto nearest = [](const DB& z, const Fl& f, double from) { double b = NAN; for (int j = 0; j < 6; ++j) { const double v = z.lv[f.side > 0 ? 0 : 1][j]; if (std::isnan(v)) continue;
      if (f.side > 0 ? v > from : v < from) if (std::isnan(b) || std::fabs(v - from) < std::fabs(b - from)) b = v; } return b; };
  static const char* TN[9] = {"MM measured move (today)", "L.5 nearest level >= 0.5R", "L1 nearest level >= 1R", "NEAR closer of MM / level", "FAR farther of MM / level",
                               "BEYOND first level past MM", "HOLD to 15:55", "SPLIT half level, half MM", "share with a level >= 0.5R"};
  printf("Targeting session highs and lows — %s 2014-01 → 2026-10. Entry flag-mid limit, stop flag low − 0.1 ATR, flat 15:55, cost %.1f pts. Levels: overnight, Asia, London, yesterday, last week, last month.\n", sym.c_str(), cost);
  for (int rr = 0; rr < 2; ++rr) { auto sel = [rr](const Fl& f) { return (rr == 1 || f.side > 0) && f.grade == 1 && f.hh5 && f.orb > 0.5; };
    printf("\n######## %s\n", rr == 0 ? "RULE B — bull flag, A-list, 5m HH/HL, range broken up" : "RULE 2 — both sides");
    printf("    %-30s | 2014-17 | 2018-21 | 2022-24 | 2025-26 | all: trades, R, win %%, PF, avg hold min\n", "target");
    for (int t = 0; t < 8; ++t) { double m[4] = {0}, cn[4] = {0}, n = 0, sR = 0, w = 0, gw = 0, gl = 0, hold = 0, withLv = 0; int lastD = 0, busy = -1; double diffExp = 0;
      for (auto& f : X) { if (!sel(f)) continue; auto it = days.find(f.date); if (it == days.end()) continue; const DB& z = it->second;
        if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue;
        const double R = std::fabs(f.e - f.st), sd = f.side, mm = f.tg; const double l5 = nearest(z, f, f.e + sd * 0.5 * R), l1 = nearest(z, f, f.e + sd * R), lb = nearest(z, f, mm);
        withLv += !std::isnan(l5); double g = 0; int xm = 1315;
        if (t == 7) { int x1, x2; const double g1 = sim(z, f, std::isnan(l5) ? mm : l5, x1), g2 = sim(z, f, mm, x2); g = 0.5 * (g1 + g2); xm = std::max(x1, x2); }
        else { double tg = mm; if (t == 1) tg = std::isnan(l5) ? mm : l5; if (t == 2) tg = std::isnan(l1) ? mm : l1;
          if (t == 3) tg = std::isnan(l5) ? mm : (std::fabs(l5 - f.e) < std::fabs(mm - f.e) ? l5 : mm); if (t == 4) tg = std::isnan(l5) ? mm : (std::fabs(l5 - f.e) > std::fabs(mm - f.e) ? l5 : mm);
          if (t == 5) tg = lb; if (t == 6) tg = NAN; g = sim(z, f, tg, xm); }
        busy = f.xm;   // the same trades under every target
        const double x = (g - cost) / R; if (t == 0) diffExp += std::fabs(x - f.r);
        const int y = f.date / 10000, e = y <= 2017 ? 0 : y <= 2021 ? 1 : y <= 2024 ? 2 : 3; m[e] += x; cn[e]++; n++; sR += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); hold += xm - f.em; }
      printf("    %-30s |", TN[t]); for (int e = 0; e < 4; ++e) printf(" %+6.2f (%3.0f) |", cn[e] ? m[e] / cn[e] : 0, cn[e]);
      printf(" %4.0f tr %+6.3fR win %4.1f%% PF %.2f · %3.0f min", n, sR / n, 100 * w / n, gl > 0 ? gw / gl : 99.0, hold / n);
      if (t == 0) printf(" · mean |R − export| %.3f", diffExp / n); if (t == 1) printf(" · a level >= 0.5R existed on %.0f%%", 100 * withLv / n); printf("\n"); } }
  return 0;
}
