// 2026-10-19 (founder: "pullback with a bull flag", after repeat_entries): on a straight NY session, use a FLAG as the pullback — pole of
// N bars moving >= m ATR with the day, then F bars whose range is <= tight x pole, entry on the close beyond the flag (bull flags on up
// days, bear flags on down days), re-entry allowed after each exit. 1m and 5m charts, several flag sizes, stop beyond the flag (+0.25)
// with 1.5R target, 60-min time exit, flat 15:55, cost 2 NQ pts at today's cost-to-range. Context as repeat_entries (09:45–15:00,
// straightness >= thr, day's side of the open and VWAP). Control: same context, same stop distance, entered every 15 min (no flag).
// argv: symbol. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Acc { double s[4] = {0}, q[4] = {0}, gp = 0, gl = 0; long n[4] = {0}, w[4] = {0};
  void add(int d, double v, double pts) { for (int e : {era(d), 3}) { s[e] += v; q[e] += v * v; n[e]++; if (v > 0) w[e]++; } if (pts > 0) gp += pts; else gl -= pts; }
  double m(int e) const { return n[e] ? s[e] / n[e] : NAN; }
  double t(int e) const { if (n[e] < 3) return NAN; double mu = m(e), sd = std::sqrt(std::max(1e-12, q[e] / n[e] - mu * mu)); return mu / (sd / std::sqrt((double)n[e])); } };
struct Set { int tf, N, F; double m, tight; };
static const Set SETS[] = {{1, 5, 3, 0.05, 0.6}, {1, 10, 3, 0.10, 0.5}, {1, 10, 5, 0.10, 0.6}, {1, 20, 5, 0.15, 0.5},
                           {5, 4, 3, 0.10, 0.6}, {5, 6, 3, 0.10, 0.6}, {5, 6, 5, 0.20, 0.6}};
static const int NS = sizeof(SETS) / sizeof(SETS[0]);
static const double COST = 2.0 / 320.0, RR = 1.5;
struct Bar { double o, h, l, c; int64_t t; int tm; int i; };

int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const double THR[2] = {0.40, 0.55};
  printf("Flag as the pullback on straight NY sessions — %s. 1 pt here = 1 %s pt scaled to today's range (pts x 320 / ATR). Net of cost.\n", sym, sym);
  for (int ti = 0; ti < 2; ++ti) { const double thr = THR[ti];
    for (int si = -1; si < NS; ++si) {   // si = -1: the every-15-min control with a 0.10 ATR stop
      Acc acc, early; long nDays = 0;
      for (auto& d : s.days) {
        if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; nDays++;
        const int tf = si < 0 ? 1 : SETS[si].tf;
        std::vector<Bar> b; double so = s.o[d.rth0], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
        int dir = 0; double e = 0, st = 0, tg = 0, R = 0; int64_t tEnd = 0; bool earlyPos = false;
        for (int i = d.rth0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(16, 0)) break;
          if (dir) { bool hs = dir > 0 ? s.l[i] <= st : s.h[i] >= st, ht = dir > 0 ? s.h[i] >= tg : s.l[i] <= tg; double out = NAN;
            if (hs) out = -R; else if (ht) out = RR * R; else if (s.t[i] >= tEnd || tm >= hm_to_tday(15, 55)) out = dir * (s.c[i] - e);
            if (!std::isnan(out)) { double net = out - COST * A, pts = net * 320 / A; acc.add(d.date, net / R, pts); if (earlyPos) early.add(d.date, net / R, pts); dir = 0; } }
          hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i];
          int bk = (tm - hm_to_tday(9, 30)) / tf;
          if (b.empty() || (b.back().tm - hm_to_tday(9, 30)) / tf != bk) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], s.t[i], tm, i});
          else { Bar& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.i = i; }
          if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue;                    // act on bar closes only
          const double c = s.c[i], vw = vv > 0 ? pv / vv : c; const int D = c > so ? 1 : c < so ? -1 : 0;
          double sx = hi > lo ? std::fabs(c - so) / (hi - lo) : 0;
          bool ctx = tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0) && D && sx >= thr && (c - vw) * D > 0;
          if (!ctx || dir) continue;
          double stop = NAN;
          if (si < 0) { if ((tm + 1) % 15 == 0) stop = c - D * 0.10 * A; }
          else { const Set& S = SETS[si]; int n = b.size(); if (n < S.N + S.F + 1) continue;
            int f1 = n - 2, f0 = n - 1 - S.F, p1 = f0 - 1, p0 = f0 - S.N;   // flag = bars [f0, f1], pole = [p0, p1], breakout bar = n-1
            double pole = D * (b[p1].c - b[p0].o); if (pole < S.m * A) continue;
            double fh = -1e18, fl = 1e18; for (int k = f0; k <= f1; ++k) { fh = std::max(fh, b[k].h); fl = std::min(fl, b[k].l); }
            if (fh - fl > S.tight * pole) continue;
            if (D > 0 ? c > fh : c < fl) stop = D > 0 ? fl - 0.25 : fh + 0.25; }
          if (std::isnan(stop)) continue; double r = D * (c - stop); if (r <= 0.25) continue;
          dir = D; e = c; st = stop; R = r; tg = c + D * RR * r; tEnd = s.t[i] + 3600; earlyPos = tm < hm_to_tday(11, 30);
        }
      }
      char lab[96]; if (si < 0) snprintf(lab, sizeof lab, "control: every 15 min, 0.10 ATR stop");
      else snprintf(lab, sizeof lab, "%dm pole %2d>=%.2f flag %d<=%.1f", SETS[si].tf, SETS[si].N, SETS[si].m, SETS[si].F, SETS[si].tight);
      if (si == -1) printf("\n== straightness >= %.2f — %ld NY days\n  %-38s %6s %6s %8s %6s %7s %22s %6s %8s | before 11:30: /day net R (t)\n", thr, nDays, "rule", "/day", "/yr", "net R", "t", "PF", "eras R", "win%", "pts/tr");
      const double yrs = nDays / 252.0;
      printf("  %-38s %6.2f %6.0f %+8.3f %+6.1f %7.2f %+7.3f%+7.3f%+7.3f %6.1f %+8.2f | %5.2f %+.3f (%+.1f)\n", lab, acc.n[3] / (double)nDays, acc.n[3] / yrs, acc.m(3), acc.t(3),
             acc.gl > 0 ? acc.gp / acc.gl : 0, acc.m(0), acc.m(1), acc.m(2), 100.0 * acc.w[3] / std::max(1L, acc.n[3]), (acc.gp - acc.gl) / std::max(1L, acc.n[3]),
             early.n[3] / (double)nDays, early.m(3), early.t(3));
    } }
  return 0;
}
