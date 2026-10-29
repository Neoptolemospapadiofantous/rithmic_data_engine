// 2026-10-22 flag_regime.cpp (founder: "ok can we add correlation with market conditions on the flags?"). EXPLORATORY map of market conditions
// known before the session vs the flag trade's result, for every flag and for A-list + 5m HH/HL + with the break (flag_hhhl export, 3-5m,
// flag-mid trade). Directional conditions are taken on the trade's side (higher = with the trade). Conditions: VIX level, VIX 1-day change,
// VIX percentile, VIX ÷ VIX3M, ATR ÷ ATR 20 sessions ago, price vs the 50- and 200-day average (in ATR), last 5 / 20 sessions' move (ATR),
// 10- and 20-session path straightness, overnight gap (ATR), overnight range (ATR), yesterday's close position in its range, weekday,
// trading day of the month, month-to-date move, and the day types FOMC / CPI / NFP / options expiry / day before a holiday (y/n).
// Table: avg R of the bottom vs top fifth (no vs yes), NQ 2014-19, NQ 2020-26, ES; CONSISTENT = same sign in all three. Anything used later
// needs its own pre-registered walk-forward test. Usage: flag_regime NQ_EXPORT.csv ES_EXPORT.csv. Read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
struct Row { int year; double r; bool rule; std::vector<double> x; };
static const char* XN[22] = {"VIX level", "VIX 1-day change", "VIX percentile", "VIX ÷ VIX3M", "ATR ÷ ATR 20 sessions ago", "price vs 50-day avg (side, ATR)", "price vs 200-day avg (side, ATR)",
  "last 5 sessions (side, ATR)", "last 20 sessions (side, ATR)", "10-session straightness", "20-session straightness", "gap (side, ATR)", "overnight range (ATR)", "yesterday close in range (side)",
  "weekday (Mon 1 → Fri 5)", "trading day of month", "month to date (side)", "FOMC day", "CPI day", "NFP day", "options expiry day", "day before a holiday"};
struct TB { double lo = 0, hi = 0, nlo = 0, nhi = 0; bool bin = false; };
static TB topBottom(const std::vector<Row>& v, int j) { TB t; std::vector<double> xs; std::set<double> u; for (auto& w : v) { if (std::isnan(w.x[j])) continue; xs.push_back(w.x[j]); if (u.size() < 3) u.insert(w.x[j]); }
  if (xs.size() < 50) return t; t.bin = u.size() <= 2; std::sort(xs.begin(), xs.end());
  if (t.bin) { const double a = *u.begin(), b = *u.rbegin(); for (auto& w : v) { if (w.x[j] == a) { t.lo += w.r; t.nlo++; } else if (w.x[j] == b) { t.hi += w.r; t.nhi++; } } }
  else { const double c1 = xs[xs.size() / 5], c2 = xs[xs.size() * 4 / 5]; if (c1 == c2) return t; for (auto& w : v) { if (std::isnan(w.x[j])) continue; if (w.x[j] <= c1) { t.lo += w.r; t.nlo++; } if (w.x[j] >= c2) { t.hi += w.r; t.nhi++; } } }
  if (t.nlo) t.lo /= t.nlo; if (t.nhi) t.hi /= t.nhi; return t; }
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_regime NQ_EXPORT.csv ES_EXPORT.csv\n"); return 1; }
  std::vector<Row> R[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix); load_events("data/edge_lab/cache", s);
    std::map<int, std::vector<double>> dx; std::vector<double> atrs;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0)) continue; atrs.push_back(d.atr); if (d.date < 20140101 || d.date > 20261016) continue;
      double onh = -1e18, onl = 1e18, o0 = NAN; for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930) { onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); } else if (std::isnan(o0)) o0 = s.o[i]; }
      const double A = d.atr; dx[d.date] = {d.vix, d.vixc1, d.vixpct, d.vts, atrs.size() > 20 ? A / atrs[atrs.size() - 21] : NAN, (d.pc - d.sma50) / A, (d.pc - d.sma200) / A, d.r5, d.r20, d.e10, d.e20,
        std::isnan(d.pc) || std::isnan(o0) ? NAN : (o0 - d.pc) / A, onh > onl ? (onh - onl) / A : NAN, d.pibs, (double)d.dow, (double)d.tdom, d.mtd, (double)d.fomc, (double)d.cpi, (double)d.nfp, (double)d.opex, (double)d.prehol}; }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; auto it = dx.find((int)a[0]); if (it == dx.end()) continue; const int sd = (int)a[1];
      Row w; w.year = (int)a[0] / 10000; w.r = std::max(-1.5, std::min(5.0, a[18])); w.rule = a[6] == 1 && a[7] > 0.5 && a[47] > 0.5; w.x = it->second;
      for (int j : {5, 6, 7, 8, 11, 16}) w.x[j] *= sd; if (sd < 0 && !std::isnan(w.x[13])) w.x[13] = 1 - w.x[13]; R[mk].push_back(w); } }
  printf("Market conditions vs the flag trade — EXPLORATORY. NQ %zu flags, ES %zu, 2014-01 → 2026-10, flag-mid trade, R clipped to [−1.5, 5]. Conditions known before the session.\n", R[0].size(), R[1].size());
  printf("bottom fifth → top fifth (no → yes for y/n) avg R; consistent = same sign in NQ 2014-19, NQ 2020-26 and ES. Directional ones on the trade's side.\n");
  for (int set = 0; set < 2; ++set) { std::vector<Row> nq[2], es; for (auto& w : R[0]) if (!set || w.rule) nq[w.year >= 2020].push_back(w); for (auto& w : R[1]) if (!set || w.rule) es.push_back(w);
    printf("\n######## %s — NQ %zu, ES %zu\n    %-34s | NQ 2014-19                | NQ 2020-26                | ES                        | consistent\n", set ? "A-list + 5m HH/HL + with the break" : "every flag", nq[0].size() + nq[1].size(), es.size(), "condition");
    struct O { double sc; std::string s; }; std::vector<O> out;
    for (int j = 0; j < 22; ++j) { const TB a = topBottom(nq[0], j), b = topBottom(nq[1], j), c = topBottom(es, j); const double da = a.hi - a.lo, db = b.hi - b.lo, dc = c.hi - c.lo;
      const int mn = set ? 10 : 25; const bool ok = a.nlo >= mn && a.nhi >= mn && b.nlo >= mn && b.nhi >= mn && c.nlo >= mn && c.nhi >= mn && da * db > 0 && da * dc > 0;
      char buf[400]; snprintf(buf, sizeof buf, "    %-34s | %+5.2f → %+5.2f (%+5.2f) %s | %+5.2f → %+5.2f (%+5.2f)    | %+5.2f → %+5.2f (%+5.2f)    | %s%s", XN[j], a.lo, a.hi, da, a.bin ? "y/n" : "   ", b.lo, b.hi, db, c.lo, c.hi, dc,
               ok ? (da > 0 ? "YES, higher is better" : "YES, lower is better") : "no", a.bin && ok ? "" : (a.bin ? (" (yes: " + std::to_string((int)(a.nhi + b.nhi)) + " NQ)").c_str() : ""));
      out.push_back({ok ? std::min(std::fabs(da), std::min(std::fabs(db), std::fabs(dc))) : -1, buf}); }
    std::sort(out.begin(), out.end(), [](const O& a, const O& b) { return a.sc > b.sc; }); for (auto& o : out) printf("%s\n", o.s.c_str()); }
  return 0;
}
