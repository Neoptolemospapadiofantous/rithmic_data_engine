// 2026-10-22 flag_bars.cpp (founder: "can we analyse the formation of the flag bars. add some conditions or analyse behaivor"). EXPLORATORY map
// of how the bars inside the flag and the pole look, and what goes with better trades. For every flag (flag_hhhl export, 3-5m, flag-mid trade)
// the flag's own chart bars are rebuilt from the 1-minute cache (09:30 grid). Pole = the bar holding the pole low → the pole-high bar; flag =
// the bars after the pole-high bar that closed before the entry minute. Mirrored for bear flags (so "up" = with the trade). Measurements:
//   flag bars: count · avg range ÷ pole's avg range (contraction) · range of the flag ÷ (bars × avg range) (overlap; low = tight) · share of
//   down bars · share of bars with a lower high than the bar before · avg lower-wick share (buying tails) · avg close position in the bar ·
//   last bar up (y/n) · last bar inside the one before (y/n) · volume of the later half ÷ the earlier half
//   pole bars: biggest bar ÷ pole height · share of up bars · avg close position · pole height ÷ sum of bar ranges (straightness) ·
//   pole-high bar's upper-wick share (rejection at the top) · number of pole bars
// Table per measurement: avg R of the bottom fifth vs the top fifth (no vs yes), NQ 2014-19, NQ 2020-26, ES; CONSISTENT = same sign in all
// three. Sets: every flag, and A-list + 5m HH/HL + with the break. Anything used later needs its own pre-registered walk-forward test.
// Usage: flag_bars NQ_EXPORT.csv ES_EXPORT.csv. Read-only. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Row { int year; double r; bool rule; std::vector<double> x; };
static const char* XN[16] = {"flag: number of bars", "flag: bar size ÷ pole bar size", "flag: overlap (low = tight)", "flag: share of down bars", "flag: share of lower highs",
  "flag: buying tails (lower wick)", "flag: close position in bar", "flag: last bar up", "flag: last bar inside", "flag: volume later ÷ earlier half",
  "pole: biggest bar ÷ pole", "pole: share of up bars", "pole: close position in bar", "pole: straightness", "pole: top bar upper wick", "pole: number of bars"};
struct TB { double lo = 0, hi = 0, nlo = 0, nhi = 0; bool bin = false; };
static TB topBottom(const std::vector<Row>& v, int j) { TB t; std::vector<double> xs; std::set<double> u; for (auto& w : v) { xs.push_back(w.x[j]); if (u.size() < 3) u.insert(w.x[j]); }
  if (v.size() < 50) return t; t.bin = u.size() <= 2; std::sort(xs.begin(), xs.end());
  if (t.bin) { const double a = *u.begin(), b = *u.rbegin(); for (auto& w : v) { if (w.x[j] == a) { t.lo += w.r; t.nlo++; } else if (w.x[j] == b) { t.hi += w.r; t.nhi++; } } }
  else { const double c1 = xs[xs.size() / 5], c2 = xs[xs.size() * 4 / 5]; if (c1 == c2) return t; for (auto& w : v) { if (w.x[j] <= c1 && w.x[j] < c2) { t.lo += w.r; t.nlo++; } if (w.x[j] >= c2 && w.x[j] > c1) { t.hi += w.r; t.nhi++; } } }
  if (t.nlo) t.lo /= t.nlo; if (t.nhi) t.hi /= t.nhi; return t; }
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_bars NQ_EXPORT.csv ES_EXPORT.csv\n"); return 1; }
  std::vector<Row> R[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    struct DB { std::vector<int> TM; std::vector<double> O, H, L, C, V; }; std::map<int, DB> days;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || d.date < 20140101 || d.date > 20261016) continue; DB z;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); }
      if (z.TM.size() >= 370) days[d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; auto it = days.find((int)a[0]); if (it == days.end()) continue; const DB& z = it->second;
      const int side = (int)a[1], tf = (int)a[2], reg = (int)a[3], em = (int)a[12]; const double plo = a[8], phi = a[9];
      // the chart's bars (mirrored): start minute, o h l c v
      struct B { int t0; double o, h, l, c, v; }; std::vector<B> b;
      for (size_t k = 0; k < z.TM.size(); ++k) { const int t0 = 930 + (z.TM[k] - 930) / tf * tf; const double o = side * z.O[k], h = side > 0 ? z.H[k] : -z.L[k], l = side > 0 ? z.L[k] : -z.H[k], c = side * z.C[k];
        if (b.empty() || b.back().t0 != t0) b.push_back({t0, o, h, l, c, z.V[k]}); else { B& x = b.back(); x.h = std::max(x.h, h); x.l = std::min(x.l, l); x.c = c; x.v += z.V[k]; } }
      const int hbT = reg - 3 * tf; int hi = -1; for (int k = 0; k < (int)b.size(); ++k) if (b[k].t0 == hbT) { hi = k; break; } if (hi < 2) continue;
      const double poleLowM = side > 0 ? plo : -phi; int lo = -1; for (int k = hi - 2; k >= std::max(0, hi - 15); --k) if (std::fabs(b[k].l - poleLowM) < 0.01) { lo = k; break; } if (lo < 0) continue;
      std::vector<int> fl; for (int k = hi + 1; k < (int)b.size() && b[k].t0 + tf <= em; ++k) fl.push_back(k); if (fl.size() < 2) continue;
      auto rng = [&](int k) { return std::max(b[k].h - b[k].l, 0.25); }; auto pos = [&](int k) { return (b[k].c - b[k].l) / rng(k); };
      double pr = 0, pmax = 0, pup = 0, ppos = 0; for (int k = lo; k <= hi; ++k) { pr += rng(k); pmax = std::max(pmax, rng(k)); pup += b[k].c > b[k].o; ppos += pos(k); } const int np = hi - lo + 1;
      const double poleH = b[hi].h - b[lo].l; if (!(poleH > 0)) continue;
      double fr = 0, fdn = 0, flh = 0, fwick = 0, fpos = 0, fhi = -1e18, flo = 1e18, v1 = 0, v2 = 0; const size_t nf = fl.size();
      for (size_t q = 0; q < nf; ++q) { const int k = fl[q]; fr += rng(k); fdn += b[k].c < b[k].o; flh += b[k].h < b[k - 1].h; fwick += (std::min(b[k].o, b[k].c) - b[k].l) / rng(k); fpos += pos(k);
        fhi = std::max(fhi, b[k].h); flo = std::min(flo, b[k].l); (q < nf / 2 ? v1 : v2) += b[k].v; }
      const int last = fl.back(); const double fAvg = fr / nf, pAvg = pr / np;
      Row w; w.year = (int)a[0] / 10000; w.r = std::max(-1.5, std::min(5.0, a[18])); w.rule = a[6] == 1 && a[7] > 0.5 && a[47] > 0.5;
      w.x = {(double)nf, fAvg / pAvg, (fhi - flo) / (nf * fAvg), fdn / nf, flh / nf, fwick / nf, fpos / nf, b[last].c > b[last].o ? 1.0 : 0.0, b[last].h <= b[last - 1].h && b[last].l >= b[last - 1].l ? 1.0 : 0.0,
             v1 > 0 ? (v2 / (nf - nf / 2)) / (v1 / (nf / 2)) : 1.0, pmax / poleH, pup / np, ppos / np, poleH / pr, (b[hi].h - std::max(b[hi].o, b[hi].c)) / rng(hi), (double)np};
      R[mk].push_back(w); } }
  printf("How the flag and pole bars look, and what goes with better trades — EXPLORATORY. NQ %zu flags, ES %zu, 2014-01 → 2026-10, flag-mid trade, R clipped to [−1.5, 5].\n", R[0].size(), R[1].size());
  printf("bottom fifth → top fifth (no → yes for y/n) avg R; consistent = same sign in NQ 2014-19, NQ 2020-26 and ES.\n");
  for (int set = 0; set < 2; ++set) { std::vector<Row> nq[2], es; for (auto& w : R[0]) if (!set || w.rule) nq[w.year >= 2020].push_back(w); for (auto& w : R[1]) if (!set || w.rule) es.push_back(w);
    printf("\n######## %s — NQ %zu, ES %zu\n    %-36s | NQ 2014-19                 | NQ 2020-26                 | ES                         | consistent\n", set ? "A-list + 5m HH/HL + with the break" : "every flag", nq[0].size() + nq[1].size(), es.size(), "measurement");
    struct O { double sc; std::string s; }; std::vector<O> out;
    for (int j = 0; j < 16; ++j) { const TB a = topBottom(nq[0], j), b = topBottom(nq[1], j), c = topBottom(es, j); const double da = a.hi - a.lo, db = b.hi - b.lo, dc = c.hi - c.lo;
      const int mn = set ? 10 : 25; const bool ok = a.nlo >= mn && a.nhi >= mn && b.nlo >= mn && b.nhi >= mn && c.nlo >= mn && c.nhi >= mn && da * db > 0 && da * dc > 0;
      char buf[400]; snprintf(buf, sizeof buf, "    %-36s | %+5.2f → %+5.2f (%+5.2f) %s | %+5.2f → %+5.2f (%+5.2f)     | %+5.2f → %+5.2f (%+5.2f)     | %s", XN[j], a.lo, a.hi, da, a.bin ? "y/n" : "   ", b.lo, b.hi, db, c.lo, c.hi, dc,
               ok ? (da > 0 ? "YES, higher is better" : "YES, lower is better") : "no");
      out.push_back({ok ? std::min(std::fabs(da), std::min(std::fabs(db), std::fabs(dc))) : -1, buf}); }
    std::sort(out.begin(), out.end(), [](const O& a, const O& b) { return a.sc > b.sc; }); for (auto& o : out) printf("%s\n", o.s.c_str()); }
  return 0;
}
