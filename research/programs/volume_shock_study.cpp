// 2026-10-19 (founder: "today at globex we had a huge fill"): Monday 06:38 ET — NQ +72 pts in one minute on 2,168 contracts (14× normal),
// ES 35× normal, half given back in 3 min, re-tested, held. Event study: overnight (18:05–09:25 ET) minutes whose volume is ≥ K × the
// previous 60 minutes' average AND whose move is ≥ 0.05 ATR. Direction = the minute's move. Forward from its close: +3 / +15 / +60 /
// +120 min and to 09:30, in ATR, signed; share that give back ≥ half within 5 min; after such a give-back, what happens next.
// By time bucket (08:30 releases vs other), K and era. NQ 1m 2010-07 → 2026-10, read-only.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Acc { double s[4] = {0}, q[4] = {0}; long n[4] = {0};
  void add(int d, double v) { if (std::isnan(v)) return; for (int e : {era(d), 3}) { s[e] += v; q[e] += v * v; n[e]++; } }
  double m(int e) const { return n[e] ? s[e] / n[e] : NAN; }
  double t(int e) const { if (n[e] < 3) return NAN; double mu = m(e), sd = std::sqrt(std::max(1e-12, q[e] / n[e] - mu * mu)); return mu / (sd / std::sqrt((double)n[e])); } };
struct Cell { Acc f3, f15, f60, f120, open, back5, after; };

int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const double Ks[3] = {5, 10, 20}; const char* BN[3] = {"Asia 18:05-03:00", "London 03:00-08:25", "08:25-09:25 (data releases)"};
  static Cell cell[3][3];   // [K][bucket]
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; const double A = d.atr;
    double vsum = 0; int vn = 0; std::vector<double> win;
    for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); double mavg = vn ? vsum / vn : NAN;
      win.push_back(s.v[i]); vsum += s.v[i]; vn++; if (vn > 60) { vsum -= win[win.size() - 61]; vn = 60; }
      if (tm < 5 || tm >= hm_to_tday(9, 25) || std::isnan(mavg) || mavg <= 0) continue;
      double mv = s.c[i] - s.o[i]; if (std::fabs(mv) < 0.05 * A) continue; int dir = mv > 0 ? 1 : -1;
      int b = tm < 540 ? 0 : tm < hm_to_tday(8, 25) ? 1 : 2;
      for (int k = 0; k < 3; ++k) { if (s.v[i] < Ks[k] * mavg) continue; Cell& c = cell[k][b]; const double p = s.c[i];
        auto fwd = [&](int mins) -> double { int64_t T = s.t[i] + 60 * mins; int j = i; while (j + 1 < d.b1 && s.t[j + 1] <= T) ++j; return s.t[j] < T - 120 ? NAN : dir * (s.c[j] - p) / A; };
        c.f3.add(d.date, fwd(3)); c.f15.add(d.date, fwd(15)); c.f60.add(d.date, fwd(60)); c.f120.add(d.date, fwd(120));
        int io = d.rth0; if (io > i) c.open.add(d.date, dir * (s.o[io] - p) / A);
        // gave back >= half of the spike minute within 5 min?
        double half = s.o[i] + mv / 2; int jb = -1; for (int j = i + 1; j < std::min(d.b1, i + 6); ++j) if (dir > 0 ? s.l[j] <= half : s.h[j] >= half) { jb = j; break; }
        c.back5.add(d.date, jb >= 0);
        if (jb >= 0) { int64_t T = s.t[jb] + 3600; int j = jb; while (j + 1 < d.b1 && s.t[j + 1] <= T) ++j; if (s.t[j] >= T - 120) c.after.add(d.date, dir * (s.c[j] - half) / A); } } } }
  printf("Overnight volume shocks — NQ 1m 2010-07 → 2026-10: a minute with volume >= K x the previous 60 min average and a move >= 0.05 ATR.\n"
         "Signed in the shock's direction, ATR units (1 ATR ~ 330 NQ pts today). Eras 2010-18 / 2019-22 / 2023-26.\n");
  for (int k = 0; k < 3; ++k) for (int b = 0; b < 3; ++b) { const Cell& c = cell[k][b]; if (c.f3.n[3] < 20) continue;
    printf("\n== K %2.0fx, %s: %ld shocks (%.1f a year)\n", Ks[k], BN[b], c.f3.n[3], c.f3.n[3] / 16.2);
    auto L = [](const char* l, const Acc& a) { printf("  %-46s %+7.4f (t %+5.1f) | eras %+7.4f %+7.4f %+7.4f\n", l, a.m(3), a.t(3), a.m(0), a.m(1), a.m(2)); };
    L("+3 min", c.f3); L("+15 min", c.f15); L("+60 min", c.f60); L("+120 min", c.f120); L("to the 09:30 open", c.open);
    printf("  %-46s %6.1f%% | eras %5.1f %5.1f %5.1f %%\n", "gives back >= half within 5 min", 100 * c.back5.m(3), 100 * c.back5.m(0), 100 * c.back5.m(1), 100 * c.back5.m(2));
    L("after that give-back: next 60 min from the half", c.after); }
  return 0;
}
