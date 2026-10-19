// 2026-10-19 (founder: "we want 5 a day" → "more tests"): can repeated WITH-THE-DAY pullback entries on a straight NY session give ~5
// trades a day with an edge? One position at a time per rule, re-entry after each exit. Context: 09:45–15:00 ET, path straightness
// |px − open| / range ≥ thr, price on the day's side of the 09:30 open AND of VWAP; direction = the day's side. Entries:
//  A 1m EMA20 touch-and-close-back, B VWAP touch-and-close-back, C 5m EMA9 touch-and-close-back, D 1m dip ≥0.05 ATR then new 10-bar extreme,
//  ALL = any of A–D. Controls: R15 = enter every 15 min whenever flat in context (same direction, no pattern); RDIR = rule ALL with a coin-flip
//  direction. Exit: stop 0.10 ATR, target 1.5R, 60-min time exit, flat 15:55. Cost = 2 NQ pts at today's cost-to-range (0.00625 ATR).
// argv: symbol (NQ|ES). Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Acc { double s[4] = {0}, q[4] = {0}; long n[4] = {0}, w[4] = {0};
  void add(int d, double v) { for (int e : {era(d), 3}) { s[e] += v; q[e] += v * v; n[e]++; if (v > 0) w[e]++; } }
  double m(int e) const { return n[e] ? s[e] / n[e] : NAN; }
  double t(int e) const { if (n[e] < 3) return NAN; double mu = m(e), sd = std::sqrt(std::max(1e-12, q[e] / n[e] - mu * mu)); return mu / (sd / std::sqrt((double)n[e])); } };
enum { RA, RB, RC, RD, RALL, R15, RDIR, NR };
static const char* RN[NR] = {"A 1m EMA20 pullback", "B VWAP pullback", "C 5m EMA9 pullback", "D 1m dip then new high/low", "ALL (any of A-D)",
                             "control R15: every 15 min in context", "control: ALL with coin-flip direction"};
static const double COST = 2.0 / 320.0, STOP = 0.10, RR = 1.5;

int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const double THR[3] = {0.40, 0.55, 0.70};
  std::mt19937 rng(5);
  for (int ti = 0; ti < 3; ++ti) {
    const double thr = THR[ti];
    Acc acc[NR], early[NR]; long nDays = 0; std::vector<double> dayR[NR]; long daysWith[NR] = {0};
    for (auto& d : s.days) {
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue;
      const double A = d.atr; nDays++;
      double ds[NR] = {0}; int dn[NR] = {0};
      // per-rule open position
      struct Pos { int dir = 0; double e = 0, st = 0, tg = 0; int64_t tEnd = 0; bool early = false; } pos[NR];
      double so = s.o[d.rth0], hi = s.h[d.rth0], lo = s.l[d.rth0], pv = 0, vv = 0, e20 = s.c[d.rth0], e9_5 = NAN, h5 = -1e18, l5 = 1e18;
      double ext = so; int lastDir = 0;   // running extreme in the day's direction for rule D
      for (int i = d.rth0; i < d.b1; ++i) {
        int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(16, 0)) break;
        // manage open positions on this bar first (bar i's range)
        for (int r = 0; r < NR; ++r) { Pos& p = pos[r]; if (!p.dir) continue; double out = NAN;
          bool hs = p.dir > 0 ? s.l[i] <= p.st : s.h[i] >= p.st, ht = p.dir > 0 ? s.h[i] >= p.tg : s.l[i] <= p.tg;
          if (hs) out = -1; else if (ht) out = RR; else if (s.t[i] >= p.tEnd || tm >= hm_to_tday(15, 55)) out = p.dir * (s.c[i] - p.e) / (STOP * A);
          if (!std::isnan(out)) { double net = out - COST / STOP; acc[r].add(d.date, net); if (p.early) early[r].add(d.date, net); ds[r] += net; dn[r]++; p.dir = 0; } }
        hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]);
        double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i]; double vw = vv > 0 ? pv / vv : s.c[i];
        double pe20 = e20; e20 += 2.0 / 21 * (s.c[i] - e20);
        bool close5 = (tm + 1) % 5 == 0; h5 = std::max(h5, (double)s.h[i]); l5 = std::min(l5, (double)s.l[i]);
        double pe9 = e9_5; bool c5 = false;
        if (close5) { if (std::isnan(e9_5)) e9_5 = s.c[i]; else e9_5 += 0.2 * (s.c[i] - e9_5); c5 = true; }
        const double c = s.c[i]; const int D = c > so ? 1 : c < so ? -1 : 0;
        if (D != lastDir) { ext = c; lastDir = D; } double prevExt = ext; if (D > 0) ext = std::max(ext, (double)s.h[i]); else if (D < 0) ext = std::min(ext, (double)s.l[i]);
        double st = hi > lo ? std::fabs(c - so) / (hi - lo) : 0;
        bool ctx = tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0) && D && st >= thr && (c - vw) * D > 0;
        bool sig[NR] = {false};
        if (ctx) {
          sig[RA] = D > 0 ? (s.l[i] <= pe20 && c > e20) : (s.h[i] >= pe20 && c < e20);
          sig[RB] = D > 0 ? (s.l[i] <= vw + 0.02 * A && c > vw) : (s.h[i] >= vw - 0.02 * A && c < vw);
          sig[RC] = c5 && !std::isnan(pe9) && (D > 0 ? (l5 <= pe9 && c > e9_5) : (h5 >= pe9 && c < e9_5));
          // D: price had pulled back >= 0.05 ATR from the running extreme and this bar makes a new extreme on the close
          { double back = D * (prevExt - (D > 0 ? s.l[i] : s.h[i])); (void)back; }
          sig[RD] = D * (c - prevExt) > 0 && i > d.rth0 + 10 && [&] { double m = D > 0 ? 1e18 : -1e18; for (int k = i - 10; k < i; ++k) m = D > 0 ? std::min(m, (double)s.l[k]) : std::max(m, (double)s.h[k]); return D * (prevExt - m) >= 0.05 * A; }();
          sig[RALL] = sig[RA] || sig[RB] || sig[RC] || sig[RD];
          sig[R15] = (tm + 1) % 15 == 0;
          sig[RDIR] = sig[RALL];
        }
        if (c5) { h5 = -1e18; l5 = 1e18; }
        for (int r = 0; r < NR; ++r) { if (!sig[r] || pos[r].dir) continue; int dir = r == RDIR ? ((rng() & 1) ? 1 : -1) : D;
          Pos& p = pos[r]; p.dir = dir; p.e = c; p.st = c - dir * STOP * A; p.tg = c + dir * RR * STOP * A; p.tEnd = s.t[i] + 3600; p.early = tm < hm_to_tday(11, 30); }
      }
      for (int r = 0; r < NR; ++r) { if (dn[r]) { daysWith[r]++; dayR[r].push_back(ds[r]); } }
    }
    const double yrs = nDays / 252.0;
    printf("\n== %s, straightness >= %.2f — %ld NY days (%.1f yrs). Net R per trade (1R = 0.10 ATR ≈ 32 NQ pts today; cost 2 pts at today's scale)\n", sym, thr, nDays, yrs);
    printf("  %-40s %6s %6s %8s %6s %24s %6s %8s %7s %9s\n", "rule", "/day", "/yr", "net R", "t", "eras 10-18/19-22/23-26", "win%", "pts/tr", "days%", "bad days");
    for (int r = 0; r < NR; ++r) { const Acc& a = acc[r]; if (!a.n[3]) continue; long bad = 0; for (double x : dayR[r]) if (x <= -4.5) bad++;
      printf("  %-40s %6.2f %6.0f %+8.3f %+6.1f %+7.3f %+7.3f %+7.3f %6.1f %+8.2f %6.1f%% %8.1f%%\n", RN[r], a.n[3] / (double)nDays, a.n[3] / yrs, a.m(3), a.t(3), a.m(0), a.m(1), a.m(2),
             100.0 * a.w[3] / a.n[3], a.m(3) * 32, 100.0 * daysWith[r] / nDays, dayR[r].empty() ? 0 : 100.0 * bad / dayR[r].size()); }
    printf("  entries before 11:30 only:\n");
    for (int r = 0; r < NR; ++r) { const Acc& a = early[r]; if (!a.n[3]) continue;
      printf("  %-40s %6.2f %6.0f %+8.3f %+6.1f %+7.3f %+7.3f %+7.3f\n", RN[r], a.n[3] / (double)nDays, a.n[3] / yrs, a.m(3), a.t(3), a.m(0), a.m(1), a.m(2)); }
  }
  return 0;
}
