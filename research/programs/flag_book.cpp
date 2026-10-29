// 2026-10-22 flag_book.cpp (founder: "we want consistent 1-3 trades a day possibly more if we have more PF", "we want the flags"). A flags-only
// book on NQ + ES (flag_hhhl standard exports: 3-5m, pole >= 4×, pullback 15-50 %, flag-mid trade), a LADDER of filters fixed before the run
// from the strictest to the loosest, each with: one or two open positions per market, and with / without a pullback cap of 38 %:
//   L1 A-list + 5m HH/HL + with the break (today's rule)   L2 A-list + with the break   L3 A-list   L4 A or B + 5m HH/HL   L5 A or B   L6 every flag
// both sides. Reported: trades a day, share of days with >= 1 and >= 2 trades, avg R, PF, R a day, worst day (R), worst drawdown (R), longest
// stretch below the high (trading days), years > 0, 2025-26 avg R. R is per trade at 1 unit (both markets add). Usage: flag_book NQ.csv ES.csv.
// Read-only. tday = minutes since 18:00 ET.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <cstdlib>
#include <string>
#include <vector>
struct Fl { int date, mk, em, xm, grade, hh5, orb; double r, pull; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_book NQ.csv ES.csv\n"); return 1; }
  std::vector<Fl> v; std::set<int> days;
  for (int mk = 0; mk < 2; ++mk) { std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[0] < 20140101) continue; days.insert((int)a[0]); if (a[11] < 0.5) continue; v.push_back({(int)a[0], mk, (int)a[12], (int)a[13], (int)a[6], (int)a[7], (int)a[47], a[18], a[24]}); } }
  std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  // trading days: every date that had a flag registered on either market (the exports cover every session's flags) — a close proxy
  const double nd = 2583;   // NY sessions 2014-01 → 2026-10 in the NQ cache (flag_more_trades.cpp)
  struct L { const char* n; std::function<bool(const Fl&)> f; };
  const L lad[] = {{"L1 A-list + 5m HH/HL + with the break", [](const Fl& x) { return x.grade == 1 && x.hh5 && x.orb > 0; }}, {"L2 A-list + with the break", [](const Fl& x) { return x.grade == 1 && x.orb > 0; }},
                   {"L3 A-list", [](const Fl& x) { return x.grade == 1; }}, {"L4 A or B + 5m HH/HL", [](const Fl& x) { return (x.grade == 1 || x.grade == 2) && x.hh5; }},
                   {"L5 A or B", [](const Fl& x) { return x.grade == 1 || x.grade == 2; }}, {"L6 every flag", [](const Fl&) { return true; }}};
  printf("A flags-only book, NQ + ES, 2014-01 → 2026-10 (%g NY sessions). Flag-mid trade, R after cost, 1 unit per trade on each market.\n", nd);
  printf("Columns: trades a day · days with >= 1 / >= 2 trades · avg R · PF · R a day · worst day R · worst drawdown R · longest below the high (days) · years > 0 · 2025-26 avg R\n");
  for (auto& l : lad) { printf("\n######## %s\n", l.n);
    for (int cap = 0; cap < 2; ++cap) for (int conc = 1; conc <= (getenv("FB_CONC") ? atoi(getenv("FB_CONC")) : 2); ++conc) {
      std::map<int, std::pair<double, int>> day;   // date → (R, trades)
      double n = 0, s = 0, gw = 0, gl = 0, r25 = 0, n25 = 0, yr[13] = {0}; int lastD = 0; std::vector<int> busy[2];
      for (auto& x : v) { if (!l.f(x) || (cap && x.pull > 0.38)) continue; if (x.date != lastD) { lastD = x.date; busy[0].clear(); busy[1].clear(); }
        auto& b = busy[x.mk]; b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end()); if ((int)b.size() >= conc) continue; b.push_back(x.xm);
        n++; s += x.r; (x.r > 0 ? gw : gl) += std::fabs(x.r); auto& dd = day[x.date]; dd.first += x.r; dd.second++; yr[x.date / 10000 - 2014] += x.r; if (x.date >= 20250101) { r25 += x.r; n25++; } }
      double cum = 0, pk = 0, mdd = 0, worst = 0; int d1 = 0, d2 = 0, below = 0, longest = 0;
      for (int d : days) { auto it = day.find(d); const double r = it == day.end() ? 0 : it->second.first; const int t = it == day.end() ? 0 : it->second.second; d1 += t >= 1; d2 += t >= 2;
        worst = std::min(worst, r); cum += r; if (cum >= pk) { pk = cum; below = 0; } else { below++; longest = std::max(longest, below); } mdd = std::max(mdd, pk - cum); }
      int py = 0; for (double y : yr) py += y > 0;
      printf("    %-30s %4.2f/day · >=1 %3.0f%% >=2 %3.0f%% · %+6.3fR PF %.2f · %+5.3f R/day · worst day %5.1fR · DD %5.1fR · below high %3d d · years > 0 %2d/13 · 2025-26 %+6.3fR\n",
             (std::string(cap ? "pullback <= 38 %, " : "") + (std::to_string(conc) + (conc == 1 ? " position/market" : " positions/market"))).c_str(), n / nd, 100.0 * d1 / nd, 100.0 * d2 / nd,
             s / n, gl > 0 ? gw / gl : 99.0, s / nd, worst, mdd, longest, py, n25 ? r25 / n25 : 0); } }
  return 0;
}
