// 2026-10-22 flag_traits.cpp (founder: "also what about a smaller pole", "how can study the behaivor of good flags and find correlation if that
// can help with the overal perfromanec. for reversals to"). EXPLORATORY — a map of what goes with better trades, not a rule; anything used later
// needs its own pre-registered test.
//   A  POLE SIZE: flag_hhhl.cpp `export FILE pole2` (pole >= 2× the average bar instead of 4×, all else unchanged; grades refit on that set):
//      buckets 2-3 / 3-4 / 4-5 / 5-7 / >= 7 × the average bar, for every flag / A-list / A-list + 5m HH/HL / … + with the break, both sides.
//   B  FLAG TRAITS: the 35 measurements of the standard export (pole >= 4×) — for each, the average
//      R of the bottom vs top fifth (yes vs no for a yes/no trait), in 2014-19 and 2020-26 separately; a trait is CONSISTENT when the top-minus-
//      bottom difference has the same sign in both halves AND on ES (ES file given).
//   C  REVERSAL TRAITS: the reversal signals (reversal_plan / reversal_double `SIGNALS` exports: R1, R2, D1, exit X2) with traits computed from
//      the bars: signal time, minutes from the low to the signal, stop size / ATR, drop into the low / ATR (from the session high so far), VWAP
//      distance at the signal / ATR, where the signal sits in the day's range so far, the overnight gap with the trade / ATR, last 5 sessions
//      with the trade / ATR, prior VIX, 5m opening range broken the other way first — same table and the same consistency test.
// Trades here are every flag / signal (no one-at-a-time) so each trait sees all of them. Usage:
//   flag_traits POLE2_NQ.csv POLE2_ES.csv FLAGS_NQ.csv FLAGS_ES.csv RP_NQ.csv RD_NQ.csv RP_ES.csv RD_ES.csv. Read-only. tday = minutes since 18:00 ET.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
static std::vector<std::vector<double>> readCsv(const char* fn) { std::vector<std::vector<double>> o; std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln);
  while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; } o.push_back(a); } return o; }
struct Row { int year; double r; std::vector<double> x; };
// rank correlation (Spearman) of trait j with R — NOT printed: with a fixed cost per trade, R of a stopped trade depends on the stop's size in
// points (−1.3R in a quiet 2014, −1.05R today), so rank correlation tracks the era and volatility (VIX, order-flow years) more than flag quality
[[maybe_unused]] static double spearman(const std::vector<Row>& v, int j) { const size_t n = v.size(); if (n < 30) return NAN;
  auto rk = [&](std::function<double(const Row&)> g) { std::vector<size_t> ix(n); for (size_t i = 0; i < n; ++i) ix[i] = i; std::sort(ix.begin(), ix.end(), [&](size_t a, size_t b) { return g(v[a]) < g(v[b]); });
    std::vector<double> r(n); for (size_t i = 0; i < n;) { size_t k = i; while (k + 1 < n && g(v[ix[k + 1]]) == g(v[ix[i]])) ++k; for (size_t q = i; q <= k; ++q) r[ix[q]] = (i + k) / 2.0; i = k + 1; } return r; };
  const auto a = rk([j](const Row& w) { return w.x[j]; }), b = rk([](const Row& w) { return w.r; }); double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); } return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : NAN; }
// top-minus-bottom: binary → avg R yes − no; else top fifth − bottom fifth (by the trait's own cut points in this set)
struct TB { double lo, hi, nlo, nhi; bool bin; };
static TB topBottom(const std::vector<Row>& v, int j) { std::vector<double> xs; std::set<double> uniq; for (auto& w : v) { xs.push_back(w.x[j]); if (uniq.size() < 3) uniq.insert(w.x[j]); }
  TB t{0, 0, 0, 0, uniq.size() <= 2}; if (v.size() < 30) return t; std::sort(xs.begin(), xs.end()); double c1, c2;
  if (t.bin) { c1 = *uniq.begin(); c2 = *uniq.rbegin(); for (auto& w : v) { if (w.x[j] == c1) { t.lo += w.r; t.nlo++; } else if (w.x[j] == c2) { t.hi += w.r; t.nhi++; } } }
  else { c1 = xs[xs.size() / 5]; c2 = xs[xs.size() * 4 / 5]; for (auto& w : v) { if (w.x[j] <= c1) { t.lo += w.r; t.nlo++; } if (w.x[j] >= c2) { t.hi += w.r; t.nhi++; } } }
  if (t.nlo) t.lo /= t.nlo; if (t.nhi) t.hi /= t.nhi; return t; }
static void traitTable(const char* title, const std::vector<std::string>& names, const std::vector<Row>& nq, const std::vector<Row>& es) {
  printf("\n######## %s — NQ %zu, ES %zu\n", title, nq.size(), es.size());
  printf("    %-34s | NQ 2014-19 bottom → top (diff) | NQ 2020-26 bottom → top (diff) | ES all bottom → top (diff) | consistent\n", "trait (bottom fifth → top fifth; no → yes)");
  std::vector<Row> h[2]; for (auto& w : nq) h[w.year >= 2020].push_back(w);
  struct Out { double score; std::string line; }; std::vector<Out> out;
  for (size_t j = 0; j < names.size(); ++j) { const TB a = topBottom(h[0], (int)j), b = topBottom(h[1], (int)j), c = topBottom(es, (int)j);
    const double da = a.hi - a.lo, db = b.hi - b.lo, dc = c.hi - c.lo; const bool cons = a.nlo >= 20 && a.nhi >= 20 && b.nlo >= 20 && b.nhi >= 20 && c.nlo >= 20 && c.nhi >= 20 && da * db > 0 && da * dc > 0;
    char buf[400]; snprintf(buf, sizeof buf, "    %-34s | %+6.2f → %+6.2f (%+6.2f) %s | %+6.2f → %+6.2f (%+6.2f)     | %+6.2f → %+6.2f (%+6.2f) | %s", names[j].c_str(), a.lo, a.hi, da, a.bin ? "y/n" : "   ",
             b.lo, b.hi, db, c.lo, c.hi, dc, cons ? (da > 0 ? "YES, higher is better" : "YES, lower is better") : "no");
    out.push_back({cons ? std::min(std::fabs(da), std::min(std::fabs(db), std::fabs(dc))) : -1, buf}); }
  std::sort(out.begin(), out.end(), [](const Out& a, const Out& b) { return a.score > b.score; }); for (auto& o : out) printf("%s\n", o.line.c_str()); }
int main(int argc, char** argv) {
  if (argc < 9) { fprintf(stderr, "usage: flag_traits POLE2_NQ POLE2_ES FLAGS_NQ FLAGS_ES RP_NQ RD_NQ RP_ES RD_ES\n"); return 1; }
  // ── A: pole size ──
  printf("Flag and reversal traits — EXPLORATORY. NQ and ES 2014-01 → 2026-10, every flag / signal, R after cost (1.5 NQ / 0.5 ES pts).\n");
  for (int mk = 0; mk < 2; ++mk) { auto F = readCsv(argv[1 + mk]); struct P { int date, em, xm, side, grade, hh5, orb; double r, mult; }; std::vector<P> v;
    for (auto& a : F) if (a.size() >= 55 && a[11] > 0.5 && a[0] >= 20140101) v.push_back({(int)a[0], (int)a[12], (int)a[13], (int)a[1], (int)a[6], (int)a[7], (int)a[47], a[18], a[22]});
    std::sort(v.begin(), v.end(), [](const P& a, const P& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    printf("\n######## A  POLE SIZE — %s, pole >= 2× the average bar (grades refit on this set), one position at a time within each cell, both sides\n", mk ? "ES" : "NQ");
    const double B[6] = {2, 3, 4, 5, 7, 1e9}; static const char* SETN[4] = {"every flag", "A-list", "A-list + 5m HH/HL", "A-list + HH/HL + with break"};
    printf("    %-30s", "pole / average bar"); for (int k = 0; k < 5; ++k) printf(" | %-26s", (std::to_string((int)B[k]) + (k < 4 ? "-" + std::to_string((int)B[k + 1]) + "×" : "× and more")).c_str()); printf(" | 4× and more (today's rule)\n");
    for (int st = 0; st < 4; ++st) { printf("    %-30s", SETN[st]);
      for (int k = 0; k < 6; ++k) { const double lo = k < 5 ? B[k] : 4, hi = k < 5 ? B[k + 1] : 1e9; double n = 0, sR = 0, gw = 0, gl = 0; int lastD = 0, busy = -1;
        for (auto& p : v) { if (p.mult < lo || p.mult >= hi) continue; if (st >= 1 && p.grade != 1) continue; if (st >= 2 && !p.hh5) continue; if (st >= 3 && p.orb < 1) continue;
          if (p.date != lastD) { lastD = p.date; busy = -1; } if (p.em <= busy) continue; busy = p.xm; n++; sR += p.r; (p.r > 0 ? gw : gl) += std::fabs(p.r); }
        char b[64]; snprintf(b, sizeof b, "%+6.3fR PF %.2f (%5.0f, %4.1f/yr)", n ? sR / n : 0, gl > 0 ? gw / gl : 99.0, n, n / 12.8); printf(" | %-26s", b); } printf("\n"); } }
  // ── B: flag traits ──
  static const char* XN[35] = {"chart minutes", "registration time", "pole / avg bar", "pole / ATR", "pullback depth", "gap / ATR (with trade)", "last hour / ATR", "5m HH/HL",
    "15m HH/HL", "first pullback", "pullback number", "level inside pole", "straight 09:30-10:00", "sellers in flag", "volume dries up", "buyers in pole",
    "order flow present", "first leg of day", "pullback 38-62 %", "pullback > 62 %", "pole low at VWAP", "pole low at EMA20", "pole makes new high",
    "day position", "VWAP distance / ATR", "pole bars", "pole straightness", "5m range break dir", "last 5 sessions (side)", "last 20 sessions (side)",
    "VIX", "ATR trend (20 sessions)", "side (bull = 1)", "flag / pole volume", "day range / ATR"};
  { std::vector<std::string> nm(XN, XN + 35); std::vector<Row> R[2];
    for (int mk = 0; mk < 2; ++mk) for (auto& a : readCsv(argv[3 + mk])) { if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; Row w; w.year = (int)a[0] / 10000; w.r = std::max(-1.5, std::min(5.0, a[18]));
        w.x.assign(a.begin() + 20, a.begin() + 55); R[mk].push_back(w); }
    traitTable("B  FLAG TRAITS (pole >= 4×, R clipped to [−1.5, 5] so a few runners do not decide it)", nm, R[0], R[1]); }
  // ── C: reversal traits ──
  { static const char* RN[10] = {"signal time", "minutes from low to signal", "stop size / ATR", "drop into the low / ATR", "VWAP distance at signal / ATR", "position in day range", "gap with the trade / ATR",
      "last 5 sessions with trade / ATR", "VIX (prior close)", "range broke the other way first"}; std::vector<std::string> nm(RN, RN + 10);
    std::vector<Row> R[2][3];
    for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
      if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
      struct DB { double atr, pc, vix, r5, O0; std::vector<int> TM; std::vector<double> H, L, C, V; }; std::map<int, DB> days;
      for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.atr, d.pc, d.vix, d.r5, NAN, {}, {}, {}, {}, {}};
        for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; if (std::isnan(z.O0)) z.O0 = s.o[i]; z.TM.push_back(tm); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); }
        if (z.TM.size() >= 370) days[d.date] = std::move(z); }
      for (int f = 0; f < 2; ++f) for (auto& a : readCsv(argv[5 + 2 * mk + f])) { if (a.size() < 15) continue; const int setup = (int)a[2], si = setup == 1 ? 0 : setup == 2 ? 1 : setup == 11 ? 2 : -1; if (si < 0) continue;
        auto it = days.find((int)a[0]); if (it == days.end()) continue; const DB& z = it->second; const int side = (int)a[1], sig = (int)a[3], low = setup == 11 ? (int)a[5] : (int)a[4];
        double hi = -1e18, lo = 1e18, pv = 0, vv = 0, hod = -1e18, c = NAN, orh = -1e18, orl = 1e18; int other = 0;
        for (size_t k = 0; k < z.TM.size() && z.TM[k] < sig; ++k) { hi = std::max(hi, z.H[k]); lo = std::min(lo, z.L[k]); pv += (z.H[k] + z.L[k] + z.C[k]) / 3 * z.V[k]; vv += z.V[k]; c = z.C[k];
          if (z.TM[k] < 935) { orh = std::max(orh, z.H[k]); orl = std::min(orl, z.L[k]); } else if (!other && orh > -1e17) { if (z.C[k] > orh) other = side < 0 ? 1 : -1; else if (z.C[k] < orl) other = side > 0 ? 1 : -1; }
          if (z.TM[k] < low - 5) hod = std::max(hod, side > 0 ? z.H[k] : -z.L[k]); }
        const double lowPx = side * a[8] + 0.1 * z.atr;   // the low (mirrored) = the stop (bottom − 0.1 ATR) + 0.1 ATR
        Row w; w.year = (int)a[0] / 10000; w.r = std::max(-1.5, std::min(5.0, a[10]));
        const double vw = vv > 0 ? pv / vv : c, gap = std::isnan(z.pc) ? 0 : side * (z.O0 - z.pc) / z.atr;
        w.x = {(double)sig, (double)(sig - low), std::fabs(a[7] - a[8]) / z.atr, hod > -1e17 ? (hod - lowPx) / z.atr : 0, side * (c - vw) / z.atr, hi > lo ? (side > 0 ? (c - lo) / (hi - lo) : (hi - c) / (hi - lo)) : 0.5,
               gap, std::isnan(z.r5) ? 0 : side * z.r5, std::isnan(z.vix) ? 18 : z.vix, other == 1 ? 1.0 : 0.0};
        R[mk][si].push_back(w); } }
    static const char* SN[3] = {"C  REVERSAL TRAITS — R1 CHoCH pullback (X2)", "C  REVERSAL TRAITS — R2 sweep and reclaim (X2)", "C  REVERSAL TRAITS — D1 double bottom / top (X2)"};
    for (int si = 0; si < 3; ++si) traitTable(SN[si], nm, R[0][si], R[1][si]); }
  return 0;
}
