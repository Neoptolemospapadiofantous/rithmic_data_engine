// 2026-10-22 flag_adapt.cpp (founder: "maybe because the way to find this reversal changes by market conditions? how can build a system to keep up
// with market conditions and how to find the best possible bull flags bear flags. for normal trend and reversals"). Does letting the flag rule ADAPT
// pick better flags than one rule for all years? Two kinds of flag, against the 5-minute opening range (09:30-09:35):
//   TREND     a flag on the break side, registered after the break (orb_flag.cpp F1)
//   REVERSAL  a flag on the other side (orb_flag_rev.cpp A)
// The same 1,152-cell grid as orb_rev_opt.cpp (how far the break ran × ATR, registration time, first of the day, grade, 5m structure, gap side,
// bull only). Selection schemes, all walk-forward, test years 2017-2026, each fixed before the run:
//   FIXED     every flag of the kind (no selection)
//   EXPAND    best cell over ALL earlier years (as orb_rev_opt.cpp)
//   ROLL1/2/3 best cell over only the last 1 / 2 / 3 years (keeps up with recent conditions)
//   VIX       best cell picked separately for days whose prior VIX close is < 20 and >= 20 (expanding), each regime trades its own cell
//   TREND5    the same split on the last 5 sessions' direction (r5 > 0 / <= 0)
//   ATRX      the same split on the volatility trend: today's ATR above / below its level 20 sessions earlier
// A cell needs >= 25 trades in its training window (>= 12 for ROLL1). One position at a time; flag-mid · S2 trade. Usage: flag_adapt [NQ|ES] FLAGS.csv.
// Read-only. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct F { int date, side, tf, reg, end, pole, grade, hh5; double plo, phi, flo; int ok, em, xm; double e, st, tg, x, r, pts; };
struct Rv { int date, year, em, xm, side, ext, tm, first, grade, hh5, gap, rg[3]; double r, pts; };
enum { NE = 4, NT = 4, NF = 2, NG = 3, NH = 2, NP = 3, NS = 2, NCELL = NE * NT * NF * NG * NH * NP * NS };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_adapt [NQ|ES] FLAGS.csv\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5;
  std::map<int, std::vector<F>> fl;
  { std::ifstream f(argv[2]); std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { F z; if (sscanf(ln.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf", &z.date, &z.side, &z.tf, &z.reg, &z.end, &z.pole, &z.grade, &z.hh5,
                             &z.plo, &z.phi, &z.flo, &z.ok, &z.em, &z.xm, &z.e, &z.st, &z.tg, &z.x, &z.r, &z.pts) == 20 && z.ok && z.date >= 20140101) fl[z.date].push_back(z); } }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Rv> rv[2]; long nd = 0; std::vector<double> atrs;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; atrs.push_back(d.atr); if (d.date < 20140101) continue;
    std::vector<double> H, L, C; std::vector<int> TM; double O0 = NAN;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; if (std::isnan(O0)) O0 = s.o[i]; H.push_back(s.h[i]); L.push_back(s.l[i]); C.push_back(s.c[i]); TM.push_back(tm); }
    if (H.size() < 370) continue; nd++;
    const int rgV = !std::isnan(d.vix) && d.vix >= 20, rgT = !std::isnan(d.r5) && d.r5 > 0, rgA = atrs.size() > 20 && d.atr > atrs[atrs.size() - 21];
    double hi = -1e18, lo = 1e18; size_t k = 0; for (; k < C.size() && TM[k] < 935; ++k) { hi = std::max(hi, H[k]); lo = std::min(lo, L[k]); }
    int dir = 0, brk = 0; for (; k < C.size() && TM[k] < 1200; ++k) if (C[k] > hi || C[k] < lo) { dir = C[k] > hi ? 1 : -1; brk = TM[k] + 1; break; }
    auto it = fl.find(d.date); if (!dir || it == fl.end()) continue;
    const double gap = std::isnan(d.pc) ? 0 : O0 - d.pc;
    for (int kind = 0; kind < 2; ++kind) { std::vector<const F*> v; for (auto& f : it->second) if (f.side == (kind ? -dir : dir) && f.reg >= brk) v.push_back(&f);
      std::sort(v.begin(), v.end(), [](const F* a, const F* b) { return a->em < b->em; }); bool seen = false;
      for (const F* f : v) { double xt = 0; for (size_t j = 0; j < C.size() && TM[j] < f->reg; ++j) if (TM[j] >= brk - 1) xt = std::max(xt, dir > 0 ? H[j] - hi : lo - L[j]);
        const double xa = xt / d.atr; Rv r; r.date = d.date; r.year = d.date / 10000; r.em = f->em; r.xm = f->xm; r.side = f->side;
        r.ext = xa < 0.15 ? 1 : xa <= 0.40 ? 2 : 3; r.tm = f->reg < 990 ? 1 : f->reg < 1080 ? 2 : 3; r.first = !seen; seen = true;
        r.grade = f->grade; r.hh5 = f->hh5; r.gap = std::fabs(gap) < 1e-9 ? 0 : (gap * f->side > 0 ? 1 : 2); r.rg[0] = rgV; r.rg[1] = rgT; r.rg[2] = rgA; r.r = f->r; r.pts = f->pts;
        rv[kind].push_back(r); } } }
  auto inCell = [](const Rv& r, int c) { int x = c; const int e = x % NE; x /= NE; const int t = x % NT; x /= NT; const int fi = x % NF; x /= NF; const int g = x % NG; x /= NG;
    const int h = x % NH; x /= NH; const int p = x % NP; x /= NP; const int sd = x % NS;
    return (e == 0 || r.ext == e) && (t == 0 || r.tm == t) && (fi == 0 || r.first) && (g == 0 || (g == 1 ? (r.grade == 1 || r.grade == 2) : r.grade == 1)) && (h == 0 || r.hh5) &&
           (p == 0 || r.gap == p) && (sd == 0 || r.side > 0); };
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, cum = 0, pk = 0, dd = 0; void add(double x) { n++; s += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); cum += x; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); } };
  // trades of one selection, one position at a time, within [y0, y1]
  auto take = [&](const std::vector<Rv>& R, std::function<bool(const Rv&)> sel, int y0, int y1, St& st) { int lastD = 0, busy = -1;
    for (auto& r : R) { if (r.year < y0 || r.year > y1 || !sel(r)) continue; if (r.date != lastD) { lastD = r.date; busy = -1; } if (r.em <= busy) continue; busy = r.xm; st.add(r.r); } };
  auto best = [&](const std::vector<Rv>& R, std::function<bool(const Rv&)> pre, int y0, int y1, int minN) { int bc = -1; double bv = -1e9;
    for (int c = 0; c < NCELL; ++c) { St a; take(R, [&](const Rv& r) { return pre(r) && inCell(r, c); }, y0, y1, a); if (a.n >= minN && a.s / a.n > bv) { bv = a.s / a.n; bc = c; } } return bc; };
  printf("Adaptive flag selection — %s, %ld NY days 2014-01 → 2026-10, 5-minute opening range. TREND flags %zu, REVERSAL flags %zu. Test years 2017-2026, walk-forward.\n", sym.c_str(), nd, rv[0].size(), rv[1].size());
  printf("Each scheme picks one cell of the same 1,152-cell grid from its training window and trades it blind the next year. R per trade, one position at a time.\n");
  const char* KN[2] = {"TREND flags (with the break)", "REVERSAL flags (against the break)"};
  for (int kind = 0; kind < 2; ++kind) { const auto& R = rv[kind]; printf("\n######## %s\n", KN[kind]);
    printf("    %-34s | 2017-19 | 2020-22 | 2023-26 | all 2017-26: trades/yr, R, win %%, PF, DD R, years > 0\n", "scheme");
    auto runScheme = [&](const char* name, std::function<void(int, St&)> trade) { St tot, p3[3]; int py = 0, ny = 0;
      for (int Y = 2017; Y <= 2026; ++Y) { St a; trade(Y, a); const int ix = Y <= 2019 ? 0 : Y <= 2022 ? 1 : 2; p3[ix].n += a.n; p3[ix].s += a.s; tot.n += a.n; tot.s += a.s; tot.w += a.w; tot.gw += a.gw; tot.gl += a.gl;
        tot.dd = std::max(tot.dd, a.dd); if (a.n >= 3) { ny++; py += a.s > 0; } }
      printf("    %-34s |", name); for (int e = 0; e < 3; ++e) printf(" %+6.2f (%3.0f) |", p3[e].n ? p3[e].s / p3[e].n : 0, p3[e].n);
      printf(" %5.1f/yr %+6.3fR win %4.1f%% PF %.2f · worst-year DD %4.1fR · %d/%d\n", tot.n / 10, tot.n ? tot.s / tot.n : 0, tot.n ? 100 * tot.w / tot.n : 0, tot.gl > 0 ? tot.gw / tot.gl : 99.0, tot.dd, py, ny); };
    auto all = [](const Rv&) { return true; };
    runScheme("FIXED every flag", [&](int Y, St& a) { take(R, all, Y, Y, a); });
    runScheme("FIXED A-list + 5m HH/HL", [&](int Y, St& a) { take(R, [](const Rv& r) { return r.grade == 1 && r.hh5; }, Y, Y, a); });
    runScheme("EXPAND best cell, all earlier yrs", [&](int Y, St& a) { const int c = best(R, all, 2014, Y - 1, 25); if (c >= 0) take(R, [&](const Rv& r) { return inCell(r, c); }, Y, Y, a); });
    for (int w : {1, 2, 3}) { char nm[64]; snprintf(nm, sizeof nm, "ROLL%d best cell, last %d yr%s", w, w, w > 1 ? "s" : "");
      runScheme(nm, [&, w](int Y, St& a) { const int c = best(R, all, std::max(2014, Y - w), Y - 1, w == 1 ? 12 : 25); if (c >= 0) take(R, [&](const Rv& r) { return inCell(r, c); }, Y, Y, a); }); }
    const char* RN[3] = {"VIX split (< 20 / >= 20)", "TREND5 split (last 5 days up/down)", "ATRX split (ATR rising / falling)"};
    for (int g = 0; g < 3; ++g) runScheme(RN[g], [&, g](int Y, St& a) { int c[2]; for (int q = 0; q < 2; ++q) c[q] = best(R, [&](const Rv& r) { return r.rg[g] == q; }, 2014, Y - 1, 25);
      take(R, [&](const Rv& r) { const int q = r.rg[g]; return c[q] >= 0 && inCell(r, c[q]); }, Y, Y, a); });
    // what each regime does to every flag of the kind (no selection), 2014-26
    printf("    every flag by regime, 2014-26 (no selection):");
    const char* RG[3][2] = {{"VIX<20", "VIX>=20"}, {"5d down", "5d up"}, {"ATR falling", "ATR rising"}};
    for (int g = 0; g < 3; ++g) for (int q = 0; q < 2; ++q) { St a; take(R, [&](const Rv& r) { return r.rg[g] == q; }, 2014, 2026, a); printf(" %s %+.2f (%.0f)", RG[g][q], a.n ? a.s / a.n : 0, a.n); }
    printf("\n"); }
  return 0;
}
