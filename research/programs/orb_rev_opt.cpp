// 2026-10-22 orb_rev_opt.cpp (founder, after orb_flag_rev.cpp: "obviously now we must optimize to find the reversals"). Reversal flags = the
// 5-minute opening range (09:30-09:35) broke one way, then a flag forms the OTHER way (orb_flag_rev.cpp A); the flag's own flag-mid trade.
// A grid of reversal definitions, fixed before the run (4 × 4 × 2 × 3 × 2 × 3 × 2 = 1,152 cells):
//   EXT    how far the break ran past the range before the turn (to the flag's registration), × ATR: any · < 0.15 · 0.15-0.40 · > 0.40
//   TIME   flag registered: any · before 10:30 · 10:30-12:00 · 12:00-14:00
//   FIRST  any reversal flag · only the first one of the day
//   GRADE  any · A or B · A
//   HH5    any · 5m HH/HL (LH/LL for a bear flag)
//   GAP    any · the overnight gap on the flag's side · against it
//   SIDE   both · bull only (range broke down, turned up)
// WALK-FORWARD: for each test year 2017-2026 the cell with the best average R over ALL EARLIER years (>= 30 trades there) is traded blind in that
// year; also "top 5": a flag is taken if any of the 5 best earlier cells holds it. Compared with every reversal flag in the same years (blind) and
// with the best cell picked on all years (hindsight — never tradable). One position at a time, flag-mid · S2 (stop flag low − 0.1 ATR, measured move,
// flat 15:55). Last block (added after reading the one-condition table, so NOT pre-registered): the two conditions that held on BOTH markets
// and both halves — bull reversal, gap on the flag's side — and simple combos of them. Usage: orb_rev_opt [NQ|ES] FLAGS.csv (flag_hhhl.cpp <sym> export). Read-only. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
struct F { int date, side, tf, reg, end, pole, grade, hh5; double plo, phi, flo; int ok, em, xm; double e, st, tg, x, r, pts; };
struct Rv { int date, year, em, xm, side, ext, tm, first, grade, hh5, gap; double r, pts; };
enum { NE = 4, NT = 4, NF = 2, NG = 3, NH = 2, NP = 3, NS = 2, NCELL = NE * NT * NF * NG * NH * NP * NS };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: orb_rev_opt [NQ|ES] FLAGS.csv\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::map<int, std::vector<F>> fl;
  { std::ifstream f(argv[2]); std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { F z; if (sscanf(ln.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf", &z.date, &z.side, &z.tf, &z.reg, &z.end, &z.pole, &z.grade, &z.hh5,
                             &z.plo, &z.phi, &z.flo, &z.ok, &z.em, &z.xm, &z.e, &z.st, &z.tg, &z.x, &z.r, &z.pts) == 20 && z.ok && z.date >= 20140101) fl[z.date].push_back(z); } }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Rv> rv; long nd = 0; double Anow = 1;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue;
    std::vector<double> H, L, C; std::vector<int> TM; double O0 = NAN;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; if (std::isnan(O0)) O0 = s.o[i]; H.push_back(s.h[i]); L.push_back(s.l[i]); C.push_back(s.c[i]); TM.push_back(tm); }
    if (H.size() < 370) continue; nd++; Anow = d.atr;
    double hi = -1e18, lo = 1e18; size_t k = 0; for (; k < C.size() && TM[k] < 935; ++k) { hi = std::max(hi, H[k]); lo = std::min(lo, L[k]); }
    int dir = 0, brk = 0; for (; k < C.size() && TM[k] < 1200; ++k) if (C[k] > hi || C[k] < lo) { dir = C[k] > hi ? 1 : -1; brk = TM[k] + 1; break; }
    auto it = fl.find(d.date); if (!dir || it == fl.end()) continue;
    std::vector<const F*> v; for (auto& f : it->second) if (f.side == -dir && f.reg >= brk) v.push_back(&f);
    std::sort(v.begin(), v.end(), [](const F* a, const F* b) { return a->em < b->em; });
    const double gap = std::isnan(d.pc) ? 0 : O0 - d.pc; bool seen = false;
    for (const F* f : v) { double xt = 0; for (size_t j = 0; j < C.size() && TM[j] < f->reg; ++j) if (TM[j] >= brk - 1) xt = std::max(xt, dir > 0 ? H[j] - hi : lo - L[j]);
      const double xa = xt / d.atr; Rv r; r.date = d.date; r.year = d.date / 10000; r.em = f->em; r.xm = f->xm; r.side = f->side;
      r.ext = xa < 0.15 ? 1 : xa <= 0.40 ? 2 : 3; r.tm = f->reg < 990 ? 1 : f->reg < 1080 ? 2 : 3; r.first = !seen; seen = true;
      r.grade = f->grade; r.hh5 = f->hh5; r.gap = std::fabs(gap) < 1e-9 ? 0 : (gap * f->side > 0 ? 1 : 2); r.r = f->r; r.pts = f->pts;
      r.pts = (f->pts + cost) * 1.0 / d.atr; rv.push_back(r); } }
  for (auto& r : rv) r.pts = r.pts * Anow - cost;   // points at today's ATR
  auto inCell = [](const Rv& r, int c) { int x = c; const int e = x % NE; x /= NE; const int t = x % NT; x /= NT; const int fi = x % NF; x /= NF; const int g = x % NG; x /= NG;
    const int h = x % NH; x /= NH; const int p = x % NP; x /= NP; const int sd = x % NS;
    return (e == 0 || r.ext == e) && (t == 0 || r.tm == t) && (fi == 0 || r.first) && (g == 0 || (g == 1 ? (r.grade == 1 || r.grade == 2) : r.grade == 1)) && (h == 0 || r.hh5) &&
           (p == 0 || r.gap == p) && (sd == 0 || r.side > 0); };
  auto cname = [](int c) { static const char* E[NE] = {"ext any", "ext<0.15", "ext 0.15-0.4", "ext>0.4"}; static const char* T[NT] = {"any time", "<10:30", "10:30-12", "12-14"};
    static const char* G[NG] = {"any grade", "A or B", "A"}; static const char* P[NP] = {"any gap", "gap with", "gap against"}; int x = c; std::string o = E[x % NE]; x /= NE; o += std::string(" · ") + T[x % NT]; x /= NT;
    o += x % NF ? " · first" : ""; x /= NF; o += std::string(" · ") + G[x % NG]; x /= NG; o += x % NH ? " · 5m HH/HL" : ""; x /= NH; o += std::string(" · ") + P[x % NP]; x /= NP; o += x % NS ? " · bull only" : " · both sides"; return o; };
  // one position at a time over a selection
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, pts = 0, cum = 0, pk = 0, dd = 0; void add(const Rv& r) { n++; s += r.r; w += r.r > 0; (r.r > 0 ? gw : gl) += std::fabs(r.r); pts += r.pts; cum += r.r; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); } };
  auto take = [&](auto sel, int y0, int y1, St& st) { int lastD = 0, busy = -1; for (auto& r : rv) { if (r.year < y0 || r.year > y1 || !sel(r)) continue; if (r.date != lastD) { lastD = r.date; busy = -1; } if (r.em <= busy) continue; busy = r.xm; st.add(r); } };
  printf("Optimising the reversal flag — %s, %ld NY days 2014-01 → 2026-10, %zu reversal flags (5-minute range broke one way, a flag formed the other way), %d cells.\n", sym.c_str(), nd, rv.size(), NCELL);
  printf("Each test year trades the cell that was best over all earlier years (>= 30 trades there). R per trade, points at today's ATR (%.0f), cost %.1f pts.\n", Anow, cost);
  printf("\n  year | walk-forward pick (trained on earlier years)                                    | its trades R | top-5 union R | every reversal flag R\n");
  St wf, wf5, blind;
  for (int Y = 2017; Y <= 2026; ++Y) {
    std::vector<std::pair<double, int>> sc; for (int c = 0; c < NCELL; ++c) { St a; take([&](const Rv& r) { return inCell(r, c); }, 2014, Y - 1, a); if (a.n >= 30) sc.push_back({a.s / a.n, c}); }
    std::sort(sc.rbegin(), sc.rend()); if (sc.empty()) continue; const int best = sc[0].second;
    St a, a5, b; take([&](const Rv& r) { return inCell(r, best); }, Y, Y, a); take([&](const Rv& r) { for (size_t q = 0; q < sc.size() && q < 5; ++q) if (inCell(r, sc[q].second)) return true; return false; }, Y, Y, a5);
    take([](const Rv&) { return true; }, Y, Y, b);
    take([&](const Rv& r) { return inCell(r, best); }, Y, Y, wf); take([&](const Rv& r) { for (size_t q = 0; q < sc.size() && q < 5; ++q) if (inCell(r, sc[q].second)) return true; return false; }, Y, Y, wf5); take([](const Rv&) { return true; }, Y, Y, blind);
    printf("  %d | %-80s | %+6.2f (%3.0f) | %+6.2f (%3.0f) | %+6.2f (%3.0f)\n", Y, cname(best).c_str(), a.n ? a.s / a.n : 0, a.n, a5.n ? a5.s / a5.n : 0, a5.n, b.n ? b.s / b.n : 0, b.n); }
  auto line = [&](const char* n, const St& a) { printf("  %-44s %4.0f trades (%4.1f/yr) · %+6.3fR · win %4.1f%% · PF %.2f · %+6.1f pts · DD %5.1fR\n", n, a.n, a.n / 10.0, a.n ? a.s / a.n : 0, a.n ? 100 * a.w / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, a.n ? a.pts / a.n : 0, a.dd); };
  printf("\n2017-2026 out of sample:\n"); line("walk-forward best cell", wf); line("walk-forward top-5 union", wf5); line("every reversal flag (blind)", blind);
  // hindsight: the best cells on 2014-26 with >= 60 trades — for reading only; how they did in 2014-19 vs 2020-26
  std::vector<std::pair<double, int>> all; for (int c = 0; c < NCELL; ++c) { St a; take([&](const Rv& r) { return inCell(r, c); }, 2014, 2026, a); if (a.n >= 60) all.push_back({a.s / a.n, c}); }
  std::sort(all.rbegin(), all.rend());
  printf("\nHindsight (picked on all years — NOT tradable, for reading): best cells with >= 60 trades, and the two halves\n");
  for (size_t q = 0; q < all.size() && q < 12; ++q) { const int c = all[q].second; St a, h1, h2; take([&](const Rv& r) { return inCell(r, c); }, 2014, 2026, a); take([&](const Rv& r) { return inCell(r, c); }, 2014, 2019, h1); take([&](const Rv& r) { return inCell(r, c); }, 2020, 2026, h2);
    printf("  %-80s %4.0f tr %+6.3fR PF %.2f · 2014-19 %+6.2f (%3.0f) · 2020-26 %+6.2f (%3.0f)\n", cname(c).c_str(), a.n, a.s / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, h1.n ? h1.s / h1.n : 0, h1.n, h2.n ? h2.s / h2.n : 0, h2.n); }
  // single-knob view: each condition alone vs the rest
  printf("\nOne condition at a time (every reversal flag 2014-26, one at a time):\n");
  struct K { const char* n; bool (*f)(const Rv&); };
  const K ks[] = {{"all", [](const Rv&) { return true; }}, {"ext < 0.15 ATR (a fake break)", [](const Rv& r) { return r.ext == 1; }}, {"ext 0.15-0.40", [](const Rv& r) { return r.ext == 2; }},
                  {"ext > 0.40 (a real move, then turned)", [](const Rv& r) { return r.ext == 3; }}, {"registered before 10:30", [](const Rv& r) { return r.tm == 1; }}, {"10:30-12:00", [](const Rv& r) { return r.tm == 2; }},
                  {"12:00-14:00", [](const Rv& r) { return r.tm == 3; }}, {"first reversal flag of the day", [](const Rv& r) { return (bool)r.first; }}, {"A-list", [](const Rv& r) { return r.grade == 1; }},
                  {"5m HH/HL", [](const Rv& r) { return (bool)r.hh5; }}, {"gap on the flag's side", [](const Rv& r) { return r.gap == 1; }}, {"gap against the flag", [](const Rv& r) { return r.gap == 2; }},
                  {"bull (broke down, turned up)", [](const Rv& r) { return r.side > 0; }}, {"bear (broke up, turned down)", [](const Rv& r) { return r.side < 0; }}};
  for (auto& kk : ks) { St a, h1, h2; take(kk.f, 2014, 2026, a); take(kk.f, 2014, 2019, h1); take(kk.f, 2020, 2026, h2);
    printf("  %-40s %4.0f tr %+6.3fR win %4.1f%% PF %.2f · 2014-19 %+6.2f (%3.0f) · 2020-26 %+6.2f (%3.0f)\n", kk.n, a.n, a.n ? a.s / a.n : 0, a.n ? 100 * a.w / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, h1.n ? h1.s / h1.n : 0, h1.n, h2.n ? h2.s / h2.n : 0, h2.n); }
  printf("\nTwo conditions that held on both NQ and ES in both halves (chosen AFTER the table above — a hypothesis, not a pre-registered result):\n");
  const K k2[] = {{"bull reversal + gap up (gap on the flag's side)", [](const Rv& r) { return r.side > 0 && r.gap == 1; }},
                  {"  … + registered 10:30-12:00", [](const Rv& r) { return r.side > 0 && r.gap == 1 && r.tm == 2; }},
                  {"  … + A or B grade", [](const Rv& r) { return r.side > 0 && r.gap == 1 && (r.grade == 1 || r.grade == 2); }},
                  {"  … + first reversal flag of the day", [](const Rv& r) { return r.side > 0 && r.gap == 1 && r.first; }},
                  {"bull reversal + gap DOWN (control)", [](const Rv& r) { return r.side > 0 && r.gap == 2; }},
                  {"bear reversal + gap down", [](const Rv& r) { return r.side < 0 && r.gap == 1; }}};
  for (auto& kk : k2) { St a, h1, h2; take(kk.f, 2014, 2026, a); take(kk.f, 2014, 2019, h1); take(kk.f, 2020, 2026, h2);
    int py = 0, ny = 0; for (int Y = 2014; Y <= 2026; ++Y) { St b; take(kk.f, Y, Y, b); if (b.n >= 3) { ny++; py += b.s > 0; } }
    printf("  %-48s %4.0f tr (%4.1f/yr) %+6.3fR win %4.1f%% PF %.2f · %+6.1f pts · DD %5.1fR · 2014-19 %+6.2f (%3.0f) · 2020-26 %+6.2f (%3.0f) · years > 0: %d/%d\n", kk.n, a.n, a.n / 12.8, a.n ? a.s / a.n : 0,
           a.n ? 100 * a.w / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, a.n ? a.pts / a.n : 0, a.dd, h1.n ? h1.s / h1.n : 0, h1.n, h2.n ? h2.s / h2.n : 0, h2.n, py, ny); }
  return 0;
}
