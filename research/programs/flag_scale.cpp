// 2026-10-22 flag_scale.cpp (founder: "or we can add to winners?", "or possibly to losers?", "lets test these ideas with the flags"). The flag
// trade (flag_hhhl export: flag-mid limit, stop flag low − 0.1 ATR, measured-move target, flat 15:55) with a SECOND unit added, rules fixed
// before the run (R = the first unit's risk, entry − stop):
//   BASE  one unit (the rule)                                  BASE2 two units from the start (the same trade, double size)
//   W1    add at entry + 1R, then the stop for BOTH units moves to the first entry (the first unit can no longer lose)
//   W2    add at entry + 1R, stop unchanged
//   W3    add when price breaks the pole high (the flag's breakout), stop unchanged
//   L1    add at entry − 0.5R, stop unchanged                  L2   add at entry − 0.75R, stop unchanged
// Same target and 15:55 exit for every unit. Fills: an add fills at its price when a later minute touches it; if the stop or the target is
// also touched in that minute the add fills first (the conservative order for losers, the generous one for winners — stated). Cost per unit
// per trade 1.5 NQ / 0.5 ES pts. Result in units of R (both units added together), and per unit of the most risk the position ever had open
// (R per "max risk", so a bigger position is not rewarded just for being bigger). One position at a time (the scaled trade's own exit).
// Usage: flag_scale SYM EXPORT.csv. Read-only. tday = minutes since 18:00 ET.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Fl { int date, side, em, grade, hh5, orb; double e, st, tg, phi, plo; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_scale SYM EXPORT.csv\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::vector<Fl> X; { std::ifstream f(argv[2]); if (!f) { perror(argv[2]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; X.push_back({(int)a[0], (int)a[1], (int)a[12], (int)a[6], (int)a[7], (int)a[47], a[14], a[15], a[16], a[9], a[8]}); } }
  std::sort(X.begin(), X.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { std::vector<int> TM; std::vector<double> O, H, L, C; }; std::map<int, DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || d.date < 20140101 || d.date > 20261016) continue; DB z;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
    if (z.TM.size() >= 370) days[d.date] = std::move(z); }
  // simulate one plan on one flag, mirrored to a long: returns P&L in R (both units, after cost), the most open risk in R, exit minute
  struct Res { double pnl, maxRisk; int xm; bool added; };
  auto sim = [&](const Fl& f, int plan) -> Res { const DB& z = days.at(f.date); const int sd = f.side; auto m = [sd](double x) { return sd * x; };
    const double e = m(f.e), st0 = m(f.st), tg = m(f.tg), R = e - st0, hiPole = sd > 0 ? m(f.phi) : m(f.plo);
    double addAt = NAN; if (plan == 2 || plan == 3) addAt = e + R; if (plan == 4) addAt = hiPole + 0.25; if (plan == 5) addAt = e - 0.5 * R; if (plan == 6) addAt = e - 0.75 * R;
    const bool up = plan >= 2 && plan <= 4; int units = plan == 1 ? 2 : 1; double e2 = NAN, stop = st0, maxRisk = plan == 1 ? 2.0 : 1.0; bool added = plan == 1;
    size_t k = 0; while (k < z.TM.size() && z.TM[k] < f.em) ++k; if (k >= z.TM.size()) return {0, 1, 1315, false};
    auto pnlAt = [&](double px) { double p = (px - e) - cost; if (units == 2) p += plan == 1 ? (px - e) - cost : (px - e2) - cost; return p / R; };
    auto H = [&](size_t q) { return sd > 0 ? z.H[q] : -z.L[q]; }; auto L = [&](size_t q) { return sd > 0 ? z.L[q] : -z.H[q]; };
    if (L(k) <= stop) return {pnlAt(stop), maxRisk, z.TM[k], added};   // the fill minute also trades the stop
    for (size_t q = k + 1; q < z.TM.size(); ++q) { if (z.TM[q] >= 1315) return {pnlAt(m(z.O[q])), maxRisk, z.TM[q], added};
      if (!added && !std::isnan(addAt) && (up ? H(q) >= addAt : L(q) <= addAt)) { added = true; units = 2; e2 = addAt; if (plan == 2) stop = std::max(stop, e);
        maxRisk = std::max(maxRisk, ((e - stop) + (e2 - stop)) / R); }
      if (L(q) <= stop) return {pnlAt(stop), maxRisk, z.TM[q], added}; if (H(q) >= tg) return {pnlAt(tg), maxRisk, z.TM[q], added}; }
    return {pnlAt(m(z.C.back())), maxRisk, z.TM.back(), added}; };
  static const char* PN[7] = {"BASE one unit (the rule)", "BASE2 two units from the start", "W1 add at +1R, stop to entry", "W2 add at +1R, stop unchanged", "W3 add at the pole-high break",
                              "L1 add at −0.5R", "L2 add at −0.75R"};
  struct Set { const char* n; std::function<bool(const Fl&)> f; };
  const Set sets[] = {{"A-list + 5m HH/HL + with the break, both sides", [](const Fl& f) { return f.grade == 1 && f.hh5 && f.orb > 0; }},
                      {"bull, A-list + 5m HH/HL + with the break", [](const Fl& f) { return f.side > 0 && f.grade == 1 && f.hh5 && f.orb > 0; }},
                      {"every flag", [](const Fl&) { return true; }}};
  printf("Adding to winners and to losers on the flag trade — %s 2014-01 → 2026-10, cost %.1f pts per unit, one position at a time.\n", sym.c_str(), cost);
  printf("Columns: trades, how often the 2nd unit was added, avg R (both units, in the first unit's R), win %%, PF, avg most-risk-open (R), R per unit of that risk, 2014-19 / 2020-26 avg R, years > 0.\n");
  for (auto& S0 : sets) { printf("\n######## %s\n", S0.n);
    for (int plan = 0; plan < 7; ++plan) { double n = 0, sR = 0, w = 0, gw = 0, gl = 0, mr = 0, ad = 0, h[2] = {0, 0}, hn[2] = {0, 0}, yr[13] = {0}, yc[13] = {0}; int lastD = 0, busy = -1;
      for (auto& f : X) { if (!S0.f(f) || !days.count(f.date)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; const Res r = sim(f, plan); busy = r.xm;
        n++; sR += r.pnl; w += r.pnl > 0; (r.pnl > 0 ? gw : gl) += std::fabs(r.pnl); mr += r.maxRisk; ad += r.added; const int y = f.date / 10000; h[y >= 2020] += r.pnl; hn[y >= 2020]++; yr[y - 2014] += r.pnl; yc[y - 2014]++; }
      int py = 0, ny = 0; for (int k = 0; k < 13; ++k) if (yc[k] >= 3) { ny++; py += yr[k] > 0; }
      printf("    %-34s %5.0f tr · added %3.0f%% · %+6.3fR win %4.1f%% PF %.2f · max risk %.2fR · %+6.3fR per risk · 2014-19 %+6.3f / 2020-26 %+6.3f · years > 0 %2d/%d\n", PN[plan], n,
             plan ? 100 * ad / n : 0.0, sR / n, 100 * w / n, gl > 0 ? gw / gl : 99.0, mr / n, sR / mr, hn[0] ? h[0] / hn[0] : 0, hn[1] ? h[1] / hn[1] : 0, py, ny); } }
  return 0;
}
