// 2026-10-19 (research/preregistered/2026-10-19_ema_9_14_cross_study.md, follow-up): 5-minute setups, 1-minute entries. At a 5-min close
// a setup gives a direction; the entry is taken on the 1-min chart within the next 10 minutes (market / limit at the bar's midpoint / small
// pullback limit / 1-min dip-then-resume); stop beyond the 5-min bar, target 2R, exit at 60 min, net of 1 NQ pt. Plus: inside a 5-min bar,
// does its first 1-2 minutes' direction predict its close? NQ 1m 2010-07 → 2026-10, read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;

static int COSTMODE = 0;   // argv[1]: 0 = 1 pt (default), 1 = no cost, 2 = 2 pts at today's cost-to-range (2 × ATR / 320)
static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Acc { double s[4] = {0}, q[4] = {0}; long n[4] = {0}, w[4] = {0};
  void add(int d, double v) { for (int e : {era(d), 3}) { s[e] += v; q[e] += v * v; n[e]++; if (v > 0) w[e]++; } }
  double m(int e) const { return n[e] ? s[e] / n[e] : NAN; }
  double t(int e) const { if (n[e] < 3) return NAN; double mu = m(e), sd = std::sqrt(std::max(1e-12, q[e] / n[e] - mu * mu)); return mu / (sd / std::sqrt((double)n[e])); } };
struct B5 { int i0, i1; double o, h, l, c; int sess; int tm; };

int main(int argc, char** argv) {
  if (argc > 1) COSTMODE = atoi(argv[1]);
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(11); const double yrs = 16.2;
  const char* SETN[4] = {"S1 EMA 9/14 cross (5m)", "S2 strong 5m bar", "S3 5m close beyond prior bar", "S4 S3 + straight session, with its move"};
  const char* ENTN[5] = {"E0 market at 5m close", "E1 limit at 5m midpoint", "E2 limit 0.02 ATR better", "E3 1m dip-then-resume", "random direction (E0)"};
  const char* SN[3] = {"Asia", "London", "NY"};
  static Acc acc[4][5][3]; static long tried[4][5][3];
  Acc in1[3], in2[3], mv2[3];
  double e9 = NAN, e14 = NAN; int lastSg = 0;
  for (auto& d : s.days) {
    if (d.b1 - d.b0 < 300) continue; bool use = d.ok && !d.rollwin && d.atr > 0 && d.date <= 20261016; const double A = d.atr;
    // build this trading day's 5-min bars
    std::vector<B5> bars; int cur = -1;
    for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); int bk = tm / 5;
      if (bk != cur) { cur = bk; bars.push_back({i, i + 1, s.o[i], s.h[i], s.l[i], s.c[i], tm < 540 ? 0 : tm < 930 ? 1 : tm < 1320 ? 2 : -1, bk * 5}); }
      else { B5& b = bars.back(); b.i1 = i + 1; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; } }
    double so = NAN, shi = 0, slo = 0; int sessNow = -2;
    for (size_t k = 0; k < bars.size(); ++k) { B5& b = bars[k];
      if (b.sess != sessNow) { sessNow = b.sess; so = b.o; shi = b.h; slo = b.l; } shi = std::max(shi, b.h); slo = std::min(slo, b.l);
      if (std::isnan(e9)) { e9 = e14 = b.c; continue; }
      e9 += 0.2 * (b.c - e9); e14 += 2.0 / 15 * (b.c - e14); int sg = e9 > e14 ? 1 : e9 < e14 ? -1 : 0; bool cross = sg && lastSg && sg != lastSg; if (sg) lastSg = sg;
      if (!use || b.sess < 0 || k == 0 || (b.i1 - b.i0) < 5) continue;
      int closeTm = b.tm + 5; if (b.sess == 2 && (closeTm < 935 || closeTm > 1260)) continue;   // NY 09:35-15:00 (trading-day minutes: 09:30 = 930)
      // inside the bar (NY): first 1 / 2 minutes vs the close
      if (b.sess == 2) { double m1 = s.c[b.i0], m2 = s.c[b.i0 + 1]; int d1 = m1 > b.o ? 1 : m1 < b.o ? -1 : 0, d2 = m2 > b.o ? 1 : m2 < b.o ? -1 : 0;
        if (d1) in1[2].add(d.date, d1 * (b.c - b.o) > 0); if (d2) { in2[2].add(d.date, d2 * (b.c - m2) > 0); mv2[2].add(d.date, d2 * (b.c - m2) / A); } }
      const B5& p = bars[k - 1]; double body = std::fabs(b.c - b.o), rng5 = b.h - b.l; double st = shi > slo ? std::fabs(b.c - so) / (shi - slo) : 0;
      int dirs[4] = {cross ? sg : 0,
                     (body >= 0.08 * A && rng5 > 0 && ((b.c > b.o && (b.c - b.l) >= 0.75 * rng5) || (b.c < b.o && (b.h - b.c) >= 0.75 * rng5))) ? (b.c > b.o ? 1 : -1) : 0,
                     b.c > p.h ? 1 : b.c < p.l ? -1 : 0, 0};
      if (dirs[2] && st >= 0.55 && (b.c - so) * dirs[2] > 0 && b.sess == 2) dirs[3] = dirs[2];
      for (int su = 0; su < 4; ++su) { if (!dirs[su]) continue;
        for (int en = 0; en < 5; ++en) { int D = en == 4 ? ((rng() & 1) ? 1 : -1) : dirs[su]; tried[su][en][b.sess]++;
          double stop = D > 0 ? b.l - 0.25 : b.h + 0.25; int ci = b.i1 - 1, last = std::min(d.b1 - 1, ci + 10);
          double entry = NAN; int ei = -1;
          if (en == 0 || en == 4) { entry = b.c; ei = ci; }
          else if (en == 1 || en == 2) { double lim = en == 1 ? (b.h + b.l) / 2 : b.c - D * 0.02 * A;
            for (int j = ci + 1; j <= last; ++j) if (D > 0 ? s.l[j] <= lim : s.h[j] >= lim) { entry = lim; ei = j; break; } }
          else { for (int j = ci + 1; j <= last && ei < 0; ++j) if (D * (s.c[j] - s.o[j]) < 0) { double ext = D > 0 ? s.h[j] : s.l[j];
              for (int q = j + 1; q <= last; ++q) if (D * (s.c[q] - ext) > 0) { entry = s.c[q]; ei = q; break; } break; } }
          if (ei < 0) continue; double R = D * (entry - stop); if (R <= 0.25) continue; double tgt = entry + D * 2 * R;
          int64_t tEnd = s.t[ei] + 3600; double out = NAN; int jx = ei;
          if (en == 1 || en == 2) { if (D > 0 ? s.l[ei] <= stop : s.h[ei] >= stop) out = -R; }
          for (int j = ei + 1; std::isnan(out) && j < d.b1 && s.t[j] <= tEnd; ++j) { jx = j; bool hs = D > 0 ? s.l[j] <= stop : s.h[j] >= stop, ht = D > 0 ? s.h[j] >= tgt : s.l[j] <= tgt;
            if (hs) out = -R; else if (ht) out = 2 * R; }
          if (std::isnan(out)) out = D * (s.c[jx] - entry);
          acc[su][en][b.sess].add(d.date, (out - (COSTMODE == 0 ? 1.0 : COSTMODE == 1 ? 0.0 : 2.0 * A / 320.0)) / R); } }
    }
  }
  printf("5-min setups, 1-min entries — NQ %s → %s. Net R per trade (stop beyond the 5-min bar, 2R, 60-min exit, minus 1 pt). Eras 2010-18 / 2019-22 / 2023-26.\n",
         ymd_str(s.days.front().date).c_str(), ymd_str(s.days.back().date).c_str());
  for (int se = 2; se >= 0; --se) for (int su = 0; su < 4; ++su) { if (su == 3 && se != 2) continue; printf("\n== %s — %s\n", SN[se], SETN[su]);
    for (int en = 0; en < 5; ++en) { const Acc& a = acc[su][en][se]; if (a.n[3] < 30) continue;
      bool cons = a.n[0] > 30 && a.n[1] > 30 && a.n[2] > 30 && a.m(0) > 0 && a.m(1) > 0 && a.m(2) > 0 && a.t(3) >= 2;
      printf("  %-28s %6.0f/yr fill %5.1f%% | net %+.3f R (t %+5.1f) | eras %+.3f %+.3f %+.3f | win %4.1f%%%s\n", ENTN[en], a.n[3] / yrs, 100.0 * a.n[3] / tried[su][en][se],
             a.m(3), a.t(3), a.m(0), a.m(1), a.m(2), 100.0 * a.w[3] / a.n[3], cons ? "  POSITIVE ALL ERAS" : ""); } }
  printf("\n== Inside a NY 5-min bar\n");
  printf("  first minute's direction = the bar's close direction:   %.1f%% (n %ld) | eras %.1f %.1f %.1f %%\n", 100 * in1[2].m(3), in1[2].n[3], 100 * in1[2].m(0), 100 * in1[2].m(1), 100 * in1[2].m(2));
  printf("  after 2 minutes, the bar keeps going that way to its close: %.1f%% (n %ld) | eras %.1f %.1f %.1f %%; move minute 2 -> close %+.4f ATR\n",
         100 * in2[2].m(3), in2[2].n[3], 100 * in2[2].m(0), 100 * in2[2].m(1), 100 * in2[2].m(2), mv2[2].m(3));
  return 0;
}
