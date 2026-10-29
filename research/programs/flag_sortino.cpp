// 2026-10-22 flag_sortino.cpp (founder: "the rules is to have the best curve for as many trades as possible with the best PF with as little
// drawdown as possible. the perfect sortino ration of your life"). Searches flag rules for the best DAILY Sortino ratio at each trade frequency,
// honestly: picked on 2014-2021, judged on 2022-2026 (never seen by the pick). Input: flag_hhhl `export wide15` for NQ and ES (1-5m charts,
// pole >= 2×, pullback 5-70 %, flag-mid trade). Grid: charts × pole range × pullback range × grade × 5m structure × break × side × market
// (NQ / ES / both) × open positions per market (1 / 2). Daily series = sum of the day's R (1 unit per trade), every NY session counted
// (no-trade days are 0). Sortino = mean daily R ÷ downside deviation (root mean square of the negative days, over all days) × √252.
// Also PF, avg R, trades a day, max drawdown (R), and Calmar = annual R ÷ max drawdown. Frequency floors: >= 0.1 / 0.2 / 0.5 / 1 / 2 trades a
// day (both markets added). Usage: flag_sortino W15_NQ.csv W15_ES.csv. Read-only. tday = minutes since 18:00 ET.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
struct Fl { int di, year, mk, em, xm, tf, side, grade, hh5, orb; float r, mult, pull; };
struct C { unsigned char ch, plo, phi, pb, g, h, o, s, mk, conc; };
static const int CH[7][2] = {{3, 5}, {4, 5}, {4, 4}, {3, 3}, {5, 5}, {1, 5}, {2, 5}};
static const float PLO[4] = {2, 3, 4, 5}, PHI[2] = {7, 1e9f};
static const float PB[7][2] = {{5, 25}, {5, 38}, {15, 38}, {15, 50}, {5, 50}, {15, 25}, {5, 71}};
struct M { double tpd, avg, pf, sortino, mdd, calmar, n; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_sortino W15_NQ.csv W15_ES.csv\n"); return 1; }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
  std::vector<int> dates; for (auto& d : s.days) if (d.ok && d.date >= 20140101 && d.date <= 20261016) dates.push_back(d.date);
  std::map<int, int> di; for (size_t i = 0; i < dates.size(); ++i) di[dates[i]] = (int)i; int split = 0; while (split < (int)dates.size() && dates[split] < 20220101) ++split;
  std::vector<Fl> v;
  for (int mk = 0; mk < 2; ++mk) { std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; auto it = di.find((int)a[0]); if (it == di.end()) continue;
      v.push_back({it->second, (int)a[0] / 10000, mk, (int)a[12], (int)a[13], (int)a[2], (int)a[1], (int)a[6], (int)a[7], (int)a[47], (float)a[18], (float)a[22], (float)(a[24] * 100)}); } }
  std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.di != b.di ? a.di < b.di : a.em < b.em; });
  const int ND = (int)dates.size();
  auto eval = [&](const C& c, std::vector<double>& day) { day.assign(ND, 0.0); int lastD = -1; std::vector<int> busy[2]; double n = 0;
    for (auto& x : v) { if (c.mk < 2 && x.mk != c.mk) continue; if (x.tf < CH[c.ch][0] || x.tf > CH[c.ch][1] || x.mult < PLO[c.plo] || x.mult >= PHI[c.phi] || x.pull < PB[c.pb][0] || x.pull >= PB[c.pb][1]) continue;
      if (c.g == 1 && !(x.grade == 1 || x.grade == 2)) continue; if (c.g == 2 && x.grade != 1) continue; if (c.h && !x.hh5) continue; if (c.o && x.orb < 1) continue; if (c.s && x.side < 0) continue;
      if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk]; b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end());
      if ((int)b.size() >= c.conc) continue; b.push_back(x.xm); day[x.di] += x.r; n++; } return n; };
  auto metrics = [&](const std::vector<double>& day, int d0, int d1, const std::vector<int>& cnt) { M m{}; double s = 0, dn = 0, gw = 0, gl = 0, cum = 0, pk = 0, n = 0;
    for (int i = d0; i < d1; ++i) { s += day[i]; if (day[i] < 0) dn += day[i] * day[i]; cum += day[i]; pk = std::max(pk, cum); m.mdd = std::max(m.mdd, pk - cum); n += cnt[i]; }
    const double nd = d1 - d0, mean = s / nd, dd = std::sqrt(dn / nd); m.sortino = dd > 0 ? mean / dd * std::sqrt(252.0) : 0; m.tpd = n / nd; m.n = n; m.calmar = m.mdd > 0 ? mean * 252 / m.mdd : 0; m.avg = n ? s / n : 0; (void)gw; (void)gl; return m; };
  // per-trade PF needs trades, not days: recompute with a trade pass
  auto pfOf = [&](const C& c, int y0, int y1) { int lastD = -1; std::vector<int> busy[2]; double gw = 0, gl = 0;
    for (auto& x : v) { if (x.year < y0 || x.year > y1) continue; if (c.mk < 2 && x.mk != c.mk) continue; if (x.tf < CH[c.ch][0] || x.tf > CH[c.ch][1] || x.mult < PLO[c.plo] || x.mult >= PHI[c.phi] || x.pull < PB[c.pb][0] || x.pull >= PB[c.pb][1]) continue;
      if (c.g == 1 && !(x.grade == 1 || x.grade == 2)) continue; if (c.g == 2 && x.grade != 1) continue; if (c.h && !x.hh5) continue; if (c.o && x.orb < 1) continue; if (c.s && x.side < 0) continue;
      if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk]; b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end());
      if ((int)b.size() >= c.conc) continue; b.push_back(x.xm); (x.r > 0 ? gw : gl) += std::fabs(x.r); } return gl > 0 ? gw / gl : 99.0; };
  auto name = [](const C& c) { static const char* G[3] = {"any grade", "A or B", "A-list"}; static const char* MKN[3] = {"NQ", "ES", "NQ + ES"}; char b[260];
    snprintf(b, sizeof b, "%s · %d-%dm · pole %g-%s× · pullback %g-%g %% · %s · %s · %s · %s · %d open/market", MKN[c.mk], CH[c.ch][0], CH[c.ch][1], PLO[c.plo], c.phi ? "∞" : "7", PB[c.pb][0], std::min(70.f, PB[c.pb][1]),
             G[c.g], c.h ? "5m HH/HL" : "any structure", c.o ? "with the break" : "any break", c.s ? "bull only" : "both sides", c.conc); return std::string(b); };
  // per-day trade counts for a config
  auto counts = [&](const C& c) { std::vector<int> cnt(ND, 0); int lastD = -1; std::vector<int> busy[2];
    for (auto& x : v) { if (c.mk < 2 && x.mk != c.mk) continue; if (x.tf < CH[c.ch][0] || x.tf > CH[c.ch][1] || x.mult < PLO[c.plo] || x.mult >= PHI[c.phi] || x.pull < PB[c.pb][0] || x.pull >= PB[c.pb][1]) continue;
      if (c.g == 1 && !(x.grade == 1 || x.grade == 2)) continue; if (c.g == 2 && x.grade != 1) continue; if (c.h && !x.hh5) continue; if (c.o && x.orb < 1) continue; if (c.s && x.side < 0) continue;
      if (x.di != lastD) { lastD = x.di; busy[0].clear(); busy[1].clear(); } auto& b = busy[x.mk]; b.erase(std::remove_if(b.begin(), b.end(), [&](int t) { return t < x.em; }), b.end());
      if ((int)b.size() >= c.conc) continue; b.push_back(x.xm); cnt[x.di]++; } return cnt; };
  struct R { C c; M tr, te, all; }; std::vector<R> res; std::vector<double> day;
  for (int ch = 0; ch < 7; ++ch) for (int plo = 0; plo < 4; ++plo) for (int phi = 0; phi < 2; ++phi) for (int pb = 0; pb < 7; ++pb) for (int g = 0; g < 3; ++g) for (int h = 0; h < 2; ++h)
    for (int o = 0; o < 2; ++o) for (int sd = 0; sd < 2; ++sd) for (int mk = 0; mk < 3; ++mk) for (int conc = 1; conc <= 2; ++conc) {
      const C c{(unsigned char)ch, (unsigned char)plo, (unsigned char)phi, (unsigned char)pb, (unsigned char)g, (unsigned char)h, (unsigned char)o, (unsigned char)sd, (unsigned char)mk, (unsigned char)conc};
      if (eval(c, day) < 100) continue; const auto cnt = counts(c); res.push_back({c, metrics(day, 0, split, cnt), metrics(day, split, ND, cnt), metrics(day, 0, ND, cnt)}); }
  printf("The best daily curve for the number of trades — flag rules, NQ + ES 1-5m (wide export), %zu configurations with >= 100 trades. Picked on 2014-2021 (%d sessions),\n", res.size(), split);
  printf("judged on 2022-2026 (%d sessions, never seen by the pick). Sortino = mean daily R ÷ downside deviation × √252 (no-trade days count as 0). Calmar = R a year ÷ max drawdown.\n", ND - split);
  auto line = [&](const char* tag, const R& r) { printf("  %s %s\n", tag, name(r.c).c_str());
    printf("      picked on 2014-21: %4.2f trades/day · %+6.3fR · Sortino %5.2f · max DD %5.1fR · Calmar %5.2f\n", r.tr.tpd, r.tr.avg, r.tr.sortino, r.tr.mdd, r.tr.calmar);
    printf("      UNSEEN 2022-26:    %4.2f trades/day · %+6.3fR · PF %.2f · Sortino %5.2f · max DD %5.1fR · Calmar %5.2f\n", r.te.tpd, r.te.avg, pfOf(r.c, 2022, 2026), r.te.sortino, r.te.mdd, r.te.calmar); };
  for (double floor : {0.1, 0.2, 0.5, 1.0, 2.0}) { printf("\n######## at least %.1f trades a day (on 2014-21) — the top 3 by 2014-21 Sortino, and how they did after\n", floor);
    std::vector<const R*> cand; for (auto& r : res) if (r.tr.tpd >= floor) cand.push_back(&r);
    std::sort(cand.begin(), cand.end(), [](const R* a, const R* b) { return a->tr.sortino > b->tr.sortino; });
    for (size_t q = 0; q < cand.size() && q < 3; ++q) line(q == 0 ? "PICK" : "    ", *cand[q]);
    // the median unseen Sortino of the top 20 picks: how much of the pick survives in general
    std::vector<double> te; for (size_t q = 0; q < cand.size() && q < 20; ++q) te.push_back(cand[q]->te.sortino); std::sort(te.begin(), te.end());
    if (!te.empty()) printf("      top 20 picks' UNSEEN 2022-26 Sortino: median %.2f (worst %.2f, best %.2f)\n", te[te.size() / 2], te.front(), te.back()); }
  printf("\n######## References (fixed rules, not searched)\n");
  const C refs[3] = {{0, 2, 1, 3, 2, 1, 1, 0, 2, 1}, {0, 2, 1, 3, 1, 1, 0, 0, 2, 2}, {0, 2, 1, 3, 2, 1, 1, 1, 2, 1}};
  const char* rn[3] = {"today's rule, NQ + ES:", "the 1-a-day book (A or B + HH/HL, 2 open):", "bull rule, NQ + ES:"};
  for (int k = 0; k < 3; ++k) { eval(refs[k], day); const auto cnt = counts(refs[k]); R r{refs[k], metrics(day, 0, split, cnt), metrics(day, split, ND, cnt), metrics(day, 0, ND, cnt)}; line(rn[k], r); }
  return 0;
}
