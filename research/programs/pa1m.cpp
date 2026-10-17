// 2026-10-17 "from the regime can we follow price action on the 1m": at every RTH minute (09:45–15:30) of NQ 2010-07 → 2026-10,
// the tape regime (acr over the last 30 one-minute moves) and the last 5-minute move; does that move continue over the next
// 5 / 10 / 15 minutes, by how many points, against the ~1 pt round-trip cost? Grouped by regime, and by the size of the move.
#include "research/bars.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  const char* rn[] = {"reverting (acr < -0.1)", "neutral", "trending (acr > +0.1)"};
  // [regime][move bucket][horizon] sums
  double sum[3][3][3] = {}, sumsq[3][3][3] = {}; long n[3][3][3] = {}; long same[3][3][3] = {};
  const int H[3] = {5, 10, 15};
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0)) continue;
    for (int i = d.rth0 + 31; i + 16 < d.rth1; ++i) {
      const int tm = tday_min(s.t[i]); if (tm < hm_to_tday(9, 45) || tm > hm_to_tday(15, 30)) continue;
      double num = 0, den = 0; for (int j = i - 30; j < i; ++j) { const double r = s.c[j] - s.c[j - 1], q = s.c[j - 1] - s.c[j - 2]; num += r * q; den += r * r; }
      if (den <= 0) continue; const double a = num / den; const int rg = a < -0.1 ? 0 : a > 0.1 ? 2 : 1;
      const double mv = s.c[i - 1] - s.c[i - 6];                 // the last 5 completed minutes
      const double m_atr = std::fabs(mv) / d.atr; const int mb = m_atr < 0.03 ? 0 : m_atr < 0.08 ? 1 : 2;
      if (mv == 0) continue; const int dir = mv > 0 ? 1 : -1;
      for (int h = 0; h < 3; ++h) { const double fwd = dir * (s.c[i - 1 + H[h]] - s.o[i]);   // enter at bar i's open, out H minutes later
        sum[rg][mb][h] += fwd; sumsq[rg][mb][h] += fwd * fwd; ++n[rg][mb][h]; same[rg][mb][h] += fwd > 0; }
    }
  }
  const char* mn[] = {"small (< 0.03 ATR)", "medium (0.03–0.08)", "big (≥ 0.08 ATR)"};
  printf("Follow the last 5-minute move, enter next minute's open, exit after 5 / 10 / 15 min. NQ points per trade BEFORE cost (cost ≈ 1.0).\n");
  for (int r = 0; r < 3; ++r) { printf("\n%s\n", rn[r]);
    for (int b = 0; b < 3; ++b) { printf("  %-20s", mn[b]);
      for (int h = 0; h < 3; ++h) { double m = sum[r][b][h] / n[r][b][h], v = sumsq[r][b][h] / n[r][b][h] - m * m;
        printf(" | %2d min: %+5.2f pts (t %+5.1f, continues %4.1f%%, n %7ld)", H[h], m, m / std::sqrt(v / n[r][b][h]), 100.0 * same[r][b][h] / n[r][b][h], n[r][b][h]); }
      printf("\n"); } }
}
