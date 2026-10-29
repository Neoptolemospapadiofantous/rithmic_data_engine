// 2026-10-22 flag_more_trades.cpp (founder: "too few", "we want to get at least 1-3 set ups a day", "1-2-3-4-5m", "best ones"). The flag rules on the
// 1, 2, 3, 4 and 5 minute charts (flag_hhhl.cpp `export FILE tf15`: flag_tiny_tp rules on every chart, grades fitted on all five), ranked by how many
// setups a day they give and what each one earns. Trade = flag-mid limit, stop flag low − 0.1 ATR, measured move, flat 15:55, fixed cost per trade
// (1.5 NQ / 0.5 ES pts — a bigger share of the risk on the small charts). One position at a time per market, across its charts. Grid fixed before the run:
//   CHARTS  1-5m · 3-5m · 1-2m · each chart alone
//   QUALITY every flag · A or B · A · A + 5m HH/HL · A or B + 5m HH/HL · A + 5m HH/HL + with the 5m opening-range break · A or B + HH/HL + break ·
//           A + HH/HL, trend or reversal (any break state)
//   SIDE    bull only · both sides
// Then NQ + ES together (one position per market). Setups a day = trades / NY trading days 2014-01 → 2026-10. $ per 1 MNQ / 1 MES at today's ATR.
// Usage: flag_more_trades NQ.csv ES.csv. Read-only. tday = minutes since 18:00 ET.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Fl { int date, year, side, tf, em, xm, grade, hh5, orb; double r, u; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_more_trades NQ.csv ES.csv\n"); return 1; }
  std::vector<Fl> M[2]; long nd = 0; const char* SY[2] = {"NQ", "ES"};
  for (int mk = 0; mk < 2; ++mk) { const double cost = mk ? 0.5 : 1.5, ppt = mk ? 5 : 2;
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", SY[mk], s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    build_days(s, fomc, vix); std::map<int, double> atr; double Anow = 1; long n = 0;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; atr[d.date] = d.atr; Anow = d.atr; n++; }
    if (mk == 0) nd = n;
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; const int dt = (int)a[0]; auto it = atr.find(dt); if (it == atr.end()) continue;
      M[mk].push_back({dt, dt / 10000, (int)a[1], (int)a[2], (int)a[12], (int)a[13], (int)a[6], (int)a[7], (int)a[47], a[18], ((a[19] + cost) * Anow / it->second - cost) * ppt}); }
    std::sort(M[mk].begin(), M[mk].end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em != b.em ? a.em < b.em : a.tf > b.tf; }); }
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, u = 0, cum = 0, pk = 0, dd = 0, yr[13] = {0}, yc[13] = {0}, r25 = 0, n25 = 0;
    void add(const Fl& f) { n++; s += f.r; w += f.r > 0; (f.r > 0 ? gw : gl) += std::fabs(f.r); u += f.u; cum += f.u; pk = std::max(pk, cum); dd = std::max(dd, pk - cum);
      yr[f.year - 2014] += f.r; yc[f.year - 2014]++; if (f.year >= 2025) { r25 += f.r; n25++; } } };
  auto take = [](const std::vector<Fl>& v, std::function<bool(const Fl&)> sel, St& a) { int lastD = 0, busy = -1;
    for (auto& f : v) { if (!sel(f)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm; a.add(f); } };
  auto line = [&](const std::string& name, const St& a, const char* unit) { if (a.n < 20) { printf("  %-62s fewer than 20 trades\n", name.c_str()); return; }
    int py = 0, ny = 0; for (int k = 0; k < 13; ++k) if (a.yc[k] >= 5) { ny++; py += a.yr[k] > 0; }
    printf("  %-62s %5.2f/day %+6.3fR win %4.1f%% PF %.2f · %+6.1f $/trade %+6.1f $/day per %s · DD $%6.0f · 2025-26 %+6.3fR · years > 0 %2d/%d\n", name.c_str(), a.n / nd, a.s / a.n, 100 * a.w / a.n,
           a.gl > 0 ? a.gw / a.gl : 99.0, a.u / a.n, a.u / nd, unit, a.dd, a.n25 ? a.r25 / a.n25 : 0, py, ny); };
  struct Ch { const char* n; int lo, hi; }; const Ch CH[] = {{"1-5m", 1, 5}, {"3-5m", 3, 5}, {"1-2m", 1, 2}};
  struct Q { const char* n; std::function<bool(const Fl&)> f; };
  const Q QS[] = {{"every flag", [](const Fl&) { return true; }}, {"A or B", [](const Fl& f) { return f.grade == 1 || f.grade == 2; }}, {"A", [](const Fl& f) { return f.grade == 1; }},
                  {"A + HH/HL", [](const Fl& f) { return f.grade == 1 && f.hh5; }}, {"A or B + HH/HL", [](const Fl& f) { return (f.grade == 1 || f.grade == 2) && f.hh5; }},
                  {"A + HH/HL + with break", [](const Fl& f) { return f.grade == 1 && f.hh5 && f.orb > 0; }}, {"A or B + HH/HL + with break", [](const Fl& f) { return (f.grade == 1 || f.grade == 2) && f.hh5 && f.orb > 0; }}};
  printf("Flags on the 1-5 minute charts — setups a day and what they earn. NQ %zu flags with a trade, ES %zu; %ld NY days 2014-01 → 2026-10. One position at a time per market.\n", M[0].size(), M[1].size(), nd);
  printf("Columns: setups a day, avg R, win %%, PF, $ per trade and per day (1 MNQ / 1 MES at today's ATR, after cost), worst drawdown $, 2025-26 avg R, years > 0.\n");
  for (int mk = 0; mk < 2; ++mk) { const char* unit = mk ? "MES" : "MNQ";
    printf("\n################ %s\n", SY[mk]);
    for (int sd = 0; sd < 2; ++sd) { printf("\n  ---- %s ----\n", sd ? "both sides" : "bull only");
      for (auto& c : CH) for (auto& q : QS) { St a; take(M[mk], [&](const Fl& f) { return f.tf >= c.lo && f.tf <= c.hi && (sd || f.side > 0) && q.f(f); }, a);
        line(std::string(c.n) + " · " + q.n, a, unit); } }
    printf("\n  ---- each chart alone, both sides, A + HH/HL / A or B + HH/HL + with break ----\n");
    for (int tf = 1; tf <= 5; ++tf) { St a, b; take(M[mk], [&](const Fl& f) { return f.tf == tf && f.grade == 1 && f.hh5; }, a); take(M[mk], [&](const Fl& f) { return f.tf == tf && (f.grade == 1 || f.grade == 2) && f.hh5 && f.orb > 0; }, b);
      line(std::to_string(tf) + "m · A + HH/HL", a, unit); line(std::to_string(tf) + "m · A or B + HH/HL + with break", b, unit); } }
  // both markets, one position per market
  printf("\n################ NQ + ES together (one position per market; $ = 1 MNQ + 1 MES)\n");
  for (auto& c : CH) for (int qi : {2, 3, 4, 6}) for (int sd = 0; sd < 2; ++sd) { std::vector<Fl> both;
    for (int mk = 0; mk < 2; ++mk) { int lastD = 0, busy = -1; for (auto& f : M[mk]) { if (!(f.tf >= c.lo && f.tf <= c.hi && (sd || f.side > 0) && QS[qi].f(f))) continue;
        if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm; both.push_back(f); } }
    std::sort(both.begin(), both.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); St a; for (auto& f : both) a.add(f);
    line(std::string(c.n) + " · " + QS[qi].n + (sd ? " · both sides" : " · bull only"), a, "MNQ+MES"); }
  return 0;
}
