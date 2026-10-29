// 2026-10-22 flag_rule_check.cpp (founder: "more tests", after flag_model.cpp). Stress tests of the strongest flag rule, fixed before the run:
//   RULE B  bull flag (flag_tiny_tp rules), A-list grade, 5m HH/HL at registration, the 5m opening range already broken UP
//   RULE 2  the same on both sides (bear = A-list, 5m LH/LL, range broken DOWN)
// Trade = flag-mid limit, stop flag low − 0.1 ATR, measured-move target, flat 15:55 (flag_hhhl.cpp export). Tests:
//   T1 year by year, in R and $ per 1 MNQ at today's ATR      T2 cost: + 1× and + 2× the base cost per trade (1.5 NQ / 0.5 ES pts each)
//   T3 the limit fills only if price trades 0.25 THROUGH the mid (flag_hhhl export … thru)
//   T4 OTHER-DAY TWIN: the same entry minute, risk and target distance (scaled by ATR) on 5 random other sessions, same side — what time of
//      day and direction alone earn (post-registration entry, no same-day look-ahead)
//   T5 other entries × stops (first rule flag of the day, so cells compare on the same days): entry break of the pole high / retest / flag-mid /
//      chart close above the high × stop flag low − 0.25 / flag low − 0.1 ATR / pole low / entry − 0.2 ATR
//   T6 time of registration, weekday, FOMC days, VIX >= 20, gap with the trade
//   T7 money at 1 MNQ and 1 NQ: months positive, worst month, worst losing streak, worst drawdown, best month share of the total
// Usage: flag_rule_check SYM EXPORT.csv THRU.csv. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
using namespace lab;
struct Fl { int date, year, side, tf, reg, em, xm, grade, hh5; double e, st, tg, r, pts, x[35], c[16]; };
static std::vector<Fl> readX(const char* fn) { std::vector<Fl> v; std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln);
  while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
    if (a.size() < 71 || a[11] < 0.5) continue; Fl z; z.date = (int)a[0]; z.year = z.date / 10000; z.side = (int)a[1]; z.tf = (int)a[2]; z.reg = (int)a[3]; z.grade = (int)a[6]; z.hh5 = (int)a[7];
    z.em = (int)a[12]; z.xm = (int)a[13]; z.e = a[14]; z.st = a[15]; z.tg = a[16]; z.r = a[18]; z.pts = a[19]; for (int j = 0; j < 35; ++j) z.x[j] = a[20 + j]; for (int j = 0; j < 16; ++j) z.c[j] = a[55 + j];
    if (z.date >= 20140101) v.push_back(z); }
  std::sort(v.begin(), v.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); return v; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_rule_check SYM EXPORT.csv THRU.csv\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5, mnq = sym == "ES" ? 5 : 2;   // $ per point: MNQ 2, MES 5
  const char* micro = sym == "ES" ? "MES" : "MNQ"; const char* full = sym == "ES" ? "ES" : "NQ";
  std::vector<Fl> X = readX(argv[2]), XT = readX(argv[3]);
  std::set<int> fomc; { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date, dow; double atr, vix, gap; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days; std::map<int, size_t> di;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.date, d.dow, d.atr, d.vix, 0, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() < 370) continue; z.gap = std::isnan(d.pc) ? 0 : z.O[0] - d.pc; di[z.date] = days.size(); days.push_back(std::move(z)); }
  const double Anow = days.back().atr;
  auto ruleB = [](const Fl& f) { return f.side > 0 && f.grade == 1 && f.hh5 && f.x[27] > 0.5; };
  auto rule2 = [](const Fl& f) { return f.grade == 1 && f.hh5 && f.x[27] > 0.5; };
  // one position at a time over a selection, returning the taken flags
  auto taken = [](const std::vector<Fl>& v, std::function<bool(const Fl&)> sel) { std::vector<const Fl*> o; int lastD = 0, busy = -1;
    for (auto& f : v) { if (!sel(f)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm; o.push_back(&f); } return o; };
  auto usd = [&](const Fl& f) { auto it = di.find(f.date); const double a = it == di.end() ? Anow : days[it->second].atr; return ((f.pts + cost) * Anow / a - cost) * mnq; };
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0; void add(double x) { n++; s += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); }
    std::string str() const { char b[160]; snprintf(b, sizeof b, "%4.0f tr %+6.3fR win %4.1f%% PF %.2f", n, n ? s / n : 0, n ? 100 * w / n : 0, gl > 0 ? gw / gl : 99.0); return b; } };
  printf("Stress tests of the strongest flag rule — %s 2014-01 → 2026-10. Trade: flag-mid limit, stop flag low − 0.1 ATR, measured move, flat 15:55, cost %.1f pts.\n", sym.c_str(), cost);
  printf("RULE B = bull flag, A-list, 5m HH/HL, 5m opening range broken up. RULE 2 = the same on both sides. Money at today's ATR (%.0f): 1 %s = $%.0f a point, 1 %s = 10×.\n", Anow, micro, mnq, full);
  for (int rr = 0; rr < 2; ++rr) { auto sel = rr == 0 ? std::function<bool(const Fl&)>(ruleB) : std::function<bool(const Fl&)>(rule2); const auto T = taken(X, sel);
    printf("\n################ %s — %zu trades (%.1f a year)\n", rr == 0 ? "RULE B (bull only)" : "RULE 2 (both sides)", T.size(), T.size() / 12.8);
    // T1
    printf("\n  T1 by year\n");
    for (int y = 2014; y <= 2026; ++y) { St a; double u = 0; for (auto* f : T) if (f->year == y) { a.add(f->r); u += usd(*f); } if (a.n) printf("    %d  %s  $%+7.0f per %s\n", y, a.str().c_str(), u, micro); }
    // T2
    { printf("\n  T2 cost (extra points per trade, taken off each trade's R through its own risk)\n");
      for (double ex : {0.0, cost, 2 * cost}) { St a; for (auto* f : T) a.add(f->r - ex / std::fabs(f->e - f->st)); printf("    + %.1f pts  %s\n", ex, a.str().c_str()); } }
    // T3
    { const auto TT = taken(XT, sel); St a, b; for (auto* f : T) a.add(f->r); for (auto* f : TT) b.add(f->r);
      printf("\n  T3 limit fill\n    touch the mid            %s\n    trade 0.25 THROUGH it    %s\n", a.str().c_str(), b.str().c_str()); }
    // T4 other-day twin
    { std::mt19937 rng(20261022); St a, tw; double tws = 0, twn = 0;
      for (auto* f : T) { a.add(f->r); auto it = di.find(f->date); if (it == di.end()) continue; const double aF = days[it->second].atr, risk = std::fabs(f->e - f->st) / aF, tgd = std::fabs(f->tg - f->e) / aF;
        for (int z = 0; z < 5; ++z) { size_t j; do { j = rng() % days.size(); } while (j == it->second); const DB& o = days[j]; size_t k0 = 0; while (k0 < o.TM.size() && o.TM[k0] < f->em) ++k0;
          if (k0 + 1 >= o.TM.size()) continue; const int sd = f->side; const double e = o.C[k0], R = risk * o.atr, tg = tgd * o.atr; double g = NAN;
          for (size_t k = k0 + 1; k < o.TM.size(); ++k) { if (o.TM[k] >= 1315) { g = sd * (o.O[k] - e); break; } if ((sd > 0 ? e - o.L[k] : o.H[k] - e) >= R) { g = -R; break; } if ((sd > 0 ? o.H[k] - e : e - o.L[k]) >= tg) { g = tg; break; } }
          if (std::isnan(g)) g = sd * (o.C.back() - e); const double x = (g - cost * o.atr / aF) / R; tw.add(x); tws += x; twn++; } }
      printf("\n  T4 other-day twin (same entry minute, side, risk and target in ATR, 5 random other sessions)\n    the rule    %s\n    its twins   %s  → edge over the twin %+.3fR\n", a.str().c_str(), tw.str().c_str(), a.s / a.n - tws / twn); }
    // T5 entries × stops on the first rule flag of each day
    { static const char* EN[4] = {"break of the pole high", "retest after the break", "flag-mid limit", "chart close above high"}; static const char* SN[4] = {"flag low − 0.25", "flag low − 0.1 ATR", "pole low", "entry − 0.2 ATR"};
      printf("\n  T5 entry × stop (first rule flag of each day; target the measured move)\n    %-24s", ""); for (int sn = 0; sn < 4; ++sn) printf(" | %-30s", SN[sn]); printf("\n");
      std::map<int, const Fl*> first; for (auto& f : X) if (sel(f) && !first.count(f.date)) first[f.date] = &f;
      for (int en = 0; en < 4; ++en) { printf("    %-24s", EN[en]); for (int sn = 0; sn < 4; ++sn) { St a; for (auto& kv : first) { const double x = kv.second->c[en * 4 + sn]; if (std::isfinite(x)) a.add(x); }
          char b[64]; snprintf(b, sizeof b, "%+6.3fR PF %.2f (%4.0f)", a.n ? a.s / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, a.n); printf(" | %-30s", b); } printf("\n"); } }
    // T6 slices
    { printf("\n  T6 slices\n"); struct Sl { const char* n; std::function<bool(const Fl&, const DB&)> f; };
      const Sl sl[] = {{"registered before 10:30", [](const Fl& f, const DB&) { return f.reg < 990; }}, {"10:30-12:00", [](const Fl& f, const DB&) { return f.reg >= 990 && f.reg < 1080; }},
                       {"12:00-14:00", [](const Fl& f, const DB&) { return f.reg >= 1080; }}, {"Monday", [](const Fl&, const DB& d) { return d.dow == 1; }}, {"Tuesday", [](const Fl&, const DB& d) { return d.dow == 2; }},
                       {"Wednesday", [](const Fl&, const DB& d) { return d.dow == 3; }}, {"Thursday", [](const Fl&, const DB& d) { return d.dow == 4; }}, {"Friday", [](const Fl&, const DB& d) { return d.dow == 5; }},
                       {"FOMC day", [&](const Fl& f, const DB&) { return fomc.count(f.date) > 0; }}, {"not FOMC", [&](const Fl& f, const DB&) { return !fomc.count(f.date); }},
                       {"VIX >= 20 (prior close)", [](const Fl&, const DB& d) { return !std::isnan(d.vix) && d.vix >= 20; }}, {"VIX < 20", [](const Fl&, const DB& d) { return std::isnan(d.vix) || d.vix < 20; }},
                       {"gap in the trade's direction", [](const Fl& f, const DB& d) { return d.gap * f.side > 0; }}, {"gap against", [](const Fl& f, const DB& d) { return d.gap * f.side < 0; }},
                       {"3m chart", [](const Fl& f, const DB&) { return f.tf == 3; }}, {"4m chart", [](const Fl& f, const DB&) { return f.tf == 4; }}, {"5m chart", [](const Fl& f, const DB&) { return f.tf == 5; }}};
      for (auto& q : sl) { St a; for (auto* f : T) { auto it = di.find(f->date); if (it != di.end() && q.f(*f, days[it->second])) a.add(f->r); } printf("    %-30s %s\n", q.n, a.str().c_str()); } }
    // T7 money
    { std::map<int, double> mon; std::vector<double> seq; for (auto* f : T) { const double u = usd(*f); mon[f->date / 100] += u; seq.push_back(u); }
      double tot = 0, cum = 0, pk = 0, dd = 0, best = -1e18, worst = 1e18; int pos = 0, streak = 0, ws = 0; for (double u : seq) { tot += u; cum += u; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); streak = u < 0 ? streak + 1 : 0; ws = std::max(ws, streak); }
      int nm = 0; for (int y = 2014; y <= 2026; ++y) for (int m = 1; m <= 12; ++m) { const int k = y * 100 + m; if (k > 202610) break; nm++; const double v = mon.count(k) ? mon[k] : 0; pos += v > 0; best = std::max(best, v); worst = std::min(worst, v); }
      printf("\n  T7 money at today's ATR (1 %s; × 10 for 1 %s)\n    total $%.0f over %d months = $%.0f a month · months > 0: %d of %d (months with no trade count as 0) · best month $%.0f (%.0f%% of the total) · worst month $%.0f\n",
             micro, full, tot, nm, tot / nm, pos, nm, best, 100 * best / tot, worst);
      printf("    worst losing streak %d trades · worst drawdown $%.0f per %s ($%.0f per %s) · average trade $%.1f per %s\n", ws, dd, micro, 10 * dd, full, tot / seq.size(), micro); } }
  return 0;
}
