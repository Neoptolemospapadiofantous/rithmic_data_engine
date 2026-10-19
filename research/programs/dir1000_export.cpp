// 2026-10-19 (founder: "so basically we wait after 10 for direction then look for flags"): per NY session, the side of the 09:30 open at
// the 10:00 close and the size of that move in ATR, plus straightness at 10:00 → research/programs/inputs_20261019/dir1000.csv
// (date,dir,move_atr,straight,epoch_1000, then the same four for 10:30). Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  FILE* o = fopen("research/programs/inputs_20261019/dir1000.csv", "w"); long n = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0) continue; double so = s.o[d.rth0], hi = -1e18, lo = 1e18, c = NAN; long t10 = 0;
    for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(10, 0); ++i) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); c = s.c[i]; t10 = s.t[i] + 60; }
    if (std::isnan(c)) continue; int dir = c > so ? 1 : c < so ? -1 : 0;
    double h2 = hi, l2 = lo, c2 = c; long t2 = t10; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(10, 30); ++i) if (tday_min(s.t[i]) >= hm_to_tday(10, 0)) { h2 = std::max(h2, (double)s.h[i]); l2 = std::min(l2, (double)s.l[i]); c2 = s.c[i]; t2 = s.t[i] + 60; }
    int dir2 = c2 > so ? 1 : c2 < so ? -1 : 0;
    fprintf(o, "%d,%d,%.4f,%.4f,%ld,%d,%.4f,%.4f,%ld\n", d.date, dir, std::fabs(c - so) / d.atr, hi > lo ? std::fabs(c - so) / (hi - lo) : 0, t10, dir2, std::fabs(c2 - so) / d.atr, h2 > l2 ? std::fabs(c2 - so) / (h2 - l2) : 0, t2); n++; }
  fclose(o); printf("dir1000.csv: %ld sessions\n", n); return 0; }
