// 2026-10-22 orb_flag_rev.cpp (founder, after orb_flag.cpp: "numbers look good however what about take these on reversals?"). Two readings:
//  A  REVERSAL DAY — the 5-minute opening range broke one way and a flag then forms the OTHER way (the market turned); the flag's own flag-mid
//     trade (orb_flag.cpp F3), cut by grade / 5m structure / how far the turn went (back through the 09:30 open, through the far side of the range)
//  B  FAILED FLAG — a flag that fails (a flag-bar close below the middle of the pole before any new pole high, within 12 flag bars) is traded the
//     other way at that close: stop = pole high + 0.25 (bull flag failed → short), targets fixed before the run: 1R · 2R · pole low · hold to 15:55
// Flags = flag_hhhl.cpp export (3-5m, registered 09:45-14:00). One position at a time, cost 1.5 pts, entries before 15:00, flat 15:55, 2014-26.
// Usage: orb_flag_rev FLAGS.csv. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct F { int date, side, tf, reg, end, pole, grade, hh5; double plo, phi, flo; int ok, em, xm; double e, st, tg, x, r, pts; };
struct T { int date, em, xm; double r, pts; };
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: orb_flag_rev FLAGS.csv\n"); return 1; }
  const double cost = 1.5;
  std::map<int, std::vector<F>> fl;
  { std::ifstream f(argv[1]); std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { F z; if (sscanf(ln.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf", &z.date, &z.side, &z.tf, &z.reg, &z.end, &z.pole, &z.grade, &z.hh5,
                             &z.plo, &z.phi, &z.flo, &z.ok, &z.em, &z.xm, &z.e, &z.st, &z.tg, &z.x, &z.r, &z.pts) == 20 && z.date >= 20140101) fl[z.date].push_back(z); } }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.date, d.atr, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; const long nd = days.size();
  auto per = [](int d) { const int y = d / 10000; return y <= 2017 ? 0 : y <= 2021 ? 1 : y <= 2024 ? 2 : 3; };
  // per day: ORB 5m, break direction + minute
  struct Orb { int dir = 0, brk = 0; double hi = 0, lo = 0; };
  std::vector<Orb> ob(days.size());
  for (size_t i = 0; i < days.size(); ++i) { const DB& z = days[i]; Orb& o = ob[i]; o.hi = -1e18; o.lo = 1e18; size_t k = 0;
    for (; k < z.C.size() && z.TM[k] < 935; ++k) { o.hi = std::max(o.hi, z.H[k]); o.lo = std::min(o.lo, z.L[k]); }
    for (; k < z.C.size() && z.TM[k] < 1200; ++k) if (z.C[k] > o.hi || z.C[k] < o.lo) { o.dir = z.C[k] > o.hi ? 1 : -1; o.brk = z.TM[k] + 1; break; } }
  auto report = [&](const char* name, std::vector<T> v) {
    std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    double m[4] = {0}, cn[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, pts = 0, cum = 0, pk = 0, dd = 0; int lastD = 0, busy = -1;
    for (auto& t : v) { if (t.date != lastD) { lastD = t.date; busy = -1; } if (t.em <= busy) continue; busy = t.xm; const int e = per(t.date);
      m[e] += t.r; cn[e]++; sum += t.r; N++; w += t.r > 0; (t.r > 0 ? gw : gl) += std::fabs(t.r); pts += t.pts; cum += t.r; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); }
    if (N < 20) { printf("    %-52s fewer than 20 trades (%.0f)\n", name, N); return; }
    int pos = 0, have = 0; for (int e = 0; e < 4; ++e) if (cn[e] >= 10) { have++; pos += m[e] > 0; }
    printf("    %-52s", name); for (int e = 0; e < 4; ++e) { if (cn[e] < 10) printf("   —    (%4.0f) |", cn[e]); else printf(" %+6.2f (%4.0f) |", m[e] / cn[e], cn[e]); }
    printf(" all %+6.3fR win %4.1f%% PF %.2f · %+6.1f pts · %5.1f/yr · DD %5.1fR · %d/%d\n", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, pts / N, N / nd * 252.0, dd, pos, have); };
  printf("Flags on reversals — NQ, %ld NY days 2014-01 → 2026-10, 5-minute opening range 09:30-09:35, one position at a time, cost %.1f pts, points at today's ATR (%.0f).\n", nd, cost, Anow);
  printf("Cells: 2014-17 | 2018-21 | 2022-24 | 2025-26 avg R (n) · all: R, win %%, PF, pts/trade, trades a year, worst drawdown R, periods > 0.\n");
  // ── A: reversal-day flags ──
  printf("\n######## A  REVERSAL DAY — the range broke one way, a flag forms the other way (the flag's own flag-mid trade)\n");
  { auto collect = [&](std::function<bool(const F&, const Orb&, const DB&)> sel) { std::vector<T> v;
      for (size_t i = 0; i < days.size(); ++i) { auto it = fl.find(days[i].date); if (it == fl.end()) continue; const Orb& o = ob[i];
        for (auto& f : it->second) if (f.ok && o.dir && f.reg >= o.brk && f.side == -o.dir && sel(f, o, days[i]))
          v.push_back({f.date, f.em, f.xm, f.r, (f.pts + cost) * Anow / days[i].atr - cost}); } return v; };
    // how far the turn went by the flag's registration: back through the 09:30 open / through the far side of the range
    auto through = [](const F& f, const Orb& o, const DB& z, int how) { const double lvl = how == 0 ? z.O[0] : (f.side > 0 ? o.hi : o.lo); bool crossed = false;
      for (size_t k = 0; k < z.C.size() && z.TM[k] < f.reg; ++k) if (z.TM[k] >= o.brk && (f.side > 0 ? z.C[k] > lvl : z.C[k] < lvl)) { crossed = true; break; }
      return crossed; };
    report("reversal flag, any", collect([](const F&, const Orb&, const DB&) { return true; }));
    report("reversal flag, A-list", collect([](const F& f, const Orb&, const DB&) { return f.grade == 1; }));
    report("reversal flag, 5m HH/HL (LH/LL)", collect([](const F& f, const Orb&, const DB&) { return f.hh5; }));
    report("reversal flag, A-list + 5m HH/HL", collect([](const F& f, const Orb&, const DB&) { return f.grade == 1 && f.hh5; }));
    report("reversal flag, price closed through the far side of the range", collect([&](const F& f, const Orb& o, const DB& z) { return through(f, o, z, 1); }));
    report("  … + A-list + 5m HH/HL", collect([&](const F& f, const Orb& o, const DB& z) { return through(f, o, z, 1) && f.grade == 1 && f.hh5; }));
    report("bull reversal flag (range broke down, turned up)", collect([](const F& f, const Orb&, const DB&) { return f.side > 0; }));
    report("bear reversal flag (range broke up, turned down)", collect([](const F& f, const Orb&, const DB&) { return f.side < 0; }));
    printf("    reference — with the break, A-list + 5m HH/HL (orb_flag.cpp F6): +0.396R PF 1.55, 44.5/yr, 4/4\n"); }
  // ── B: failed flags traded the other way ──
  printf("\n######## B  FAILED FLAG — the flag closes below the middle of its pole (above, for a bear flag) before breaking out: trade the other way at that close\n");
  printf("    stop = the pole high + 0.25 (pole low − 0.25 for a failed bear flag), out at the target or 15:55\n");
  { const char* TN[4] = {"1R", "2R", "pole start", "hold 15:55"};
    // one failed-flag trade per flag and target: entry minute, exit minute, R, pts
    auto build = [&](int tgtMode, std::function<bool(const F&, const Orb&)> sel) { std::vector<T> v;
      for (size_t i = 0; i < days.size(); ++i) { auto it = fl.find(days[i].date); if (it == fl.end()) continue; const DB& z = days[i]; const Orb& o = ob[i];
        for (auto& f : it->second) { if (!sel(f, o)) continue; const int sd = f.side; const double top = sd > 0 ? f.phi : -f.plo, bot = sd > 0 ? f.plo : -f.phi, mid = top - 0.5 * (top - bot);
          size_t k = 0; while (k < z.C.size() && z.TM[k] < f.reg) ++k; int nb = 0; size_t kE = 0;
          for (; k < z.C.size(); ++k) { const double hh = sd > 0 ? z.H[k] : -z.L[k], cc = sd * z.C[k]; if (hh > top) break;
            if ((z.TM[k] - 930) % f.tf == f.tf - 1) { nb++; if (cc < mid) { kE = k; break; } if (nb >= 12) break; } }
          if (!kE || z.TM[kE] >= 1260) continue;
          // the reverse trade, in the flag's mirrored prices: short = −1
          const double e = sd * z.C[kE], st = top + 0.25, risk = st - e; if (risk < 1) continue;
          const double tg = tgtMode == 0 ? e - risk : tgtMode == 1 ? e - 2 * risk : tgtMode == 2 ? bot : -1e18; if (tg >= e) continue;
          double g = NAN; size_t kx = z.C.size() - 1;
          for (size_t j = kE + 1; j < z.C.size(); ++j) { const double hh = sd > 0 ? z.H[j] : -z.L[j], ll = sd > 0 ? z.L[j] : -z.H[j];
            if (z.TM[j] >= 1315) { g = e - sd * z.O[j]; kx = j; break; } if (hh >= st) { g = -risk; kx = j; break; } if (ll <= tg) { g = e - tg; kx = j; break; } }
          if (std::isnan(g)) g = e - sd * z.C.back();
          v.push_back({f.date, z.TM[kE], z.TM[kx], (g - cost) / risk, g * Anow / z.atr - cost}); } }
      return v; };
    struct S { const char* n; std::function<bool(const F&, const Orb&)> f; };
    const S sets[] = {{"every flag", [](const F&, const Orb&) { return true; }},
                      {"flag with the break", [](const F& f, const Orb& o) { return o.dir && f.reg >= o.brk && f.side == o.dir; }},
                      {"flag with the break, A-list + 5m HH/HL", [](const F& f, const Orb& o) { return o.dir && f.reg >= o.brk && f.side == o.dir && f.grade == 1 && f.hh5; }},
                      {"flag against the break", [](const F& f, const Orb& o) { return o.dir && f.reg >= o.brk && f.side == -o.dir; }},
                      {"bull flags only (failed → short)", [](const F& f, const Orb&) { return f.side > 0; }},
                      {"bear flags only (failed → long)", [](const F& f, const Orb&) { return f.side < 0; }}};
    for (auto& S0 : sets) { printf("  %s\n", S0.n); for (int t = 0; t < 4; ++t) report((std::string("    target ") + TN[t]).c_str(), build(t, S0.f)); } }
  return 0;
}
