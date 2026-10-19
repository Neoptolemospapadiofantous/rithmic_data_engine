// 2026-10-19 (research/preregistered/2026-10-19_orderflow_5_a_day.md): order-flow events that fire 5+ times a day in NY — delta bursts,
// absorption, book imbalance, delta divergence — traded with a 0.05 ATR stop, 1R / 2R target, 15-min exit, net of 1 pt. NQ 1m with aggressor
// volume (2023-03 → 2026-03) + per-minute book. Read-only.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;

struct Acc { long n = 0, w = 0, nh[2] = {0, 0}; double s = 0, q = 0, sh[2] = {0, 0}, r = 0;
  void add(int d, double pts, double R) { n++; if (pts > 0) w++; s += pts; q += pts * pts; r += R; int h = d < 20240901 ? 0 : 1; sh[h] += pts; nh[h]++; } };

int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); load_book("data/edge_lab/cache", "NQ", s);
  const char* EN[] = {"A delta burst z1.5 (follow)", "A delta burst z2 (follow)", "A delta burst z3 (follow)", "B absorption (fade)",
                      "C book imbalance >= 0.2 (follow)", "C book imbalance >= 0.3 (follow)", "D delta divergence 15m (fade)", "E delta burst z2 WITH the day"};
  const int NE = 8; static Acc acc[NE][2]; int ndays = 0;
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || !d.flow || d.date < 20230301 || d.date > 20260319) continue; ++ndays; const double A = d.atr;
    int64_t last[NE]; for (auto& x : last) x = -1e18; double pv = 0, vv = 0;
    for (int i = d.rth0; i < d.rth1; ++i) {
      double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i]; double vw = vv > 0 ? pv / vv : s.c[i];
      int tm = tday_min(s.t[i]); if (tm < hm_to_tday(9, 35) || tm > hm_to_tday(15, 30) || i - d.rth0 < 30) continue;
      double dl = 2 * s.bv[i] - s.v[i], mv = s.c[i] - s.o[i];
      double m = 0, q = 0, va = 0; for (int j = i - 30; j < i; ++j) { double x = 2 * s.bv[j] - s.v[j]; m += x; q += x * x; va += s.v[j]; } m /= 30; va /= 30; double sd = std::sqrt(std::max(1e-9, q / 30 - m * m));
      double z = dl / sd; double qi = NAN; { double a = 0; int n = 0; for (int j = i - 2; j <= i; ++j) if (!std::isnan(s.qimb[j])) { a += s.qimb[j]; ++n; } if (n) qi = a / n; }
      double hi15 = -1e18, lo15 = 1e18, dl15 = 0; for (int j = i - 15; j < i; ++j) { hi15 = std::max(hi15, (double)s.h[j]); lo15 = std::min(lo15, (double)s.l[j]); } for (int j = i - 14; j <= i; ++j) dl15 += 2 * s.bv[j] - s.v[j];
      int dirs[NE] = {0};
      for (int k = 0; k < 3; ++k) { double Z = k == 0 ? 1.5 : k == 1 ? 2 : 3; if (std::fabs(z) >= Z && dl * mv > 0) dirs[k] = dl > 0 ? 1 : -1; }
      if (s.v[i] >= 3 * va && std::fabs(mv) <= 0.01 * A && std::fabs(dl) >= 0.3 * s.v[i]) dirs[3] = dl > 0 ? -1 : 1;
      if (!std::isnan(qi)) { if (std::fabs(qi) >= 0.2) dirs[4] = qi > 0 ? 1 : -1; if (std::fabs(qi) >= 0.3) dirs[5] = qi > 0 ? 1 : -1; }
      if (s.h[i] > hi15 && dl15 < 0) dirs[6] = -1; else if (s.l[i] < lo15 && dl15 > 0) dirs[6] = 1;
      if (dirs[1] && dirs[1] * (s.c[i] - d.ro) > 0 && dirs[1] * (s.c[i] - vw) > 0) dirs[7] = dirs[1];
      for (int e = 0; e < NE; ++e) { int D = dirs[e]; if (!D || s.t[i] - last[e] < 300) continue; last[e] = s.t[i];
        double entry = s.c[i], R = 0.05 * A, stop = entry - D * R;
        for (int tg = 0; tg < 2; ++tg) { double tgt = entry + D * (tg ? 2 : 1) * R, out = NAN; int j = i + 1;
          for (; j < d.rth1 && s.t[j] - s.t[i] <= 900; ++j) { bool hs = D > 0 ? s.l[j] <= stop : s.h[j] >= stop, ht = D > 0 ? s.h[j] >= tgt : s.l[j] <= tgt;
            if (hs) { out = -R; break; } if (ht) { out = (tg ? 2 : 1) * R; break; } }
          if (std::isnan(out)) out = D * (s.c[std::min(j, d.rth1) - 1] - entry);
          acc[e][tg].add(d.date, out - 1, (out - 1) / R); } } } }
  printf("Order-flow events, NY 09:35-15:30, NQ 2023-03 → 2026-03-19 (%d days). Stop 0.05 ATR (~15 pts today), 15-min exit, net of 1 pt. Halves 2023-03→2024-08 / 2024-09→2026-03.\n", ndays);
  for (int e = 0; e < NE; ++e) for (int tg = 0; tg < 2; ++tg) { const Acc& a = acc[e][tg]; if (!a.n) continue; double mu = a.s / a.n, sd = std::sqrt(std::max(1e-9, a.q / a.n - mu * mu));
    printf("  %-36s target %dR | %5.1f a day | win %4.1f%% | net %+6.2f pts (%+.3f R, t %+5.1f) | halves %+6.2f / %+6.2f\n", EN[e], tg ? 2 : 1, (double)a.n / ndays,
           100.0 * a.w / a.n, mu, a.r / a.n, mu / (sd / std::sqrt((double)a.n)), a.nh[0] ? a.sh[0] / a.nh[0] : NAN, a.nh[1] ? a.sh[1] / a.nh[1] : NAN); }
  return 0;
}
