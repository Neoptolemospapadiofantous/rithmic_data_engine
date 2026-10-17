// 2026-10-17 calibration for the live regime chart (bot /chart overlay).
// Per 1-minute bar m (NQ, 2019-01 → 2026-10-16, only data up to m):
//   ER30 = |c_m − c_{m−30}| / Σ|Δc| over the last 30 one-minute moves   (1 = straight line, 0 = went nowhere)
//   AC60 = lag-1 autocorrelation of the last 60 one-minute moves         (< 0 = moves reverse each other)
// Label: TREND if ER30 ≥ its session's 67th pct; else MEAN-REVERT if AC60 ≤ its session's 33rd pct; else CHOP.
// Session buckets (ET): Asia 18:00-03:00, London 03:00-09:30, NY 09:30-16:00 (16:00-18:00 counted with NY).
// Cut points from 2019-2024; stats below on 2025-01 → 2026-10-16 (not used for the cut points).
// Reported per label: share of minutes, P(same label 60 min later, non-overlapping windows) vs the label's base share,
// and what the next 30 minutes did (trend: continued the 30-min direction %, avg pts; MR/chop: next-30 efficiency).
#include "research/bars.hpp"
#include <algorithm>
#include <cstdio>
using namespace lab;
int main() {
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  const size_t N = s.size();
  std::vector<float> er(N, NAN), ac(N, NAN); std::vector<int> ses(N, 0);
  for (size_t i = 61; i < N; ++i) {
    if (s.t[i] - s.t[i - 60] > 3900) continue;
    double path = 0; for (size_t j = i - 29; j <= i; ++j) path += std::fabs(s.c[j] - s.c[j - 1]);
    er[i] = path > 0 ? std::fabs(s.c[i] - s.c[i - 30]) / path : 0;
    double num = 0, den = 0; for (size_t j = i - 59; j <= i; ++j) { double r = s.c[j] - s.c[j - 1], q = s.c[j - 1] - s.c[j - 2]; den += r * r; num += r * q; }
    ac[i] = den > 0 ? num / den : 0;
    const int tm = tday_min(s.t[i]); ses[i] = tm < hm_to_tday(3, 0) ? 0 : tm < hm_to_tday(9, 30) ? 1 : 2;
  }
  const char* SN[3] = {"Asia", "London", "New York"};
  float q67[3], q33[3];
  for (int k = 0; k < 3; ++k) { std::vector<float> a, b; for (size_t i = 0; i < N; ++i) { int y = ymd_of(s.t[i]) / 10000; if (y < 2019 || y > 2024 || std::isnan(er[i]) || ses[i] != k) continue; a.push_back(er[i]); b.push_back(ac[i]); }
    std::sort(a.begin(), a.end()); std::sort(b.begin(), b.end()); q67[k] = a[a.size() * 2 / 3]; q33[k] = b[b.size() / 3];
    printf("CUT %s er30_q67=%.4f ac60_q33=%.4f (n %zu)\n", SN[k], q67[k], q33[k], a.size()); }
  auto lab = [&](size_t i) { if (std::isnan(er[i])) return -1; int k = ses[i]; return er[i] >= q67[k] ? 0 : ac[i] <= q33[k] ? 1 : 2; };
  const char* LN[3] = {"TREND", "MEAN-REVERT", "CHOP"};
  for (int per = 0; per < 2; ++per) {
    printf("\n== %s\n", per == 0 ? "2019-2024 (calibration years)" : "2025-01 → 2026-10-16 (not used to set the cuts)");
    for (int k = 0; k < 3; ++k) { long cnt[3] = {0}, tot = 0, same[3] = {0}, pn[3] = {0}, cont = 0, cn = 0; double pts = 0, eff[3] = {0}; long effn[3] = {0};
      for (size_t i = 0; i + 60 < N; ++i) { int y = ymd_of(s.t[i]) / 10000; if ((per == 0) != (y <= 2024) || y < 2019 || ses[i] != k) continue; int L = lab(i); if (L < 0) continue;
        ++cnt[L]; ++tot;
        if (s.t[i + 60] - s.t[i] <= 3900) { int L2 = lab(i + 60); if (L2 >= 0) { ++pn[L]; same[L] += L2 == L; } }
        if (s.t[i + 30] - s.t[i] <= 1950) { double nxt = s.c[i + 30] - s.c[i]; double path = 0; for (size_t j = i + 1; j <= i + 30; ++j) path += std::fabs(s.c[j] - s.c[j - 1]);
          if (path > 0) { eff[L] += std::fabs(nxt) / path; ++effn[L]; }
          if (L == 0) { int d = s.c[i] > s.c[i - 30] ? 1 : -1; ++cn; cont += d * nxt > 0; pts += d * nxt; } } }
      printf("  %-9s", SN[k]);
      for (int L = 0; L < 3; ++L) printf(" | %s %4.1f%% of minutes, same 60 min later %4.1f%%, next-30 efficiency %.3f", LN[L], 100.0 * cnt[L] / tot, 100.0 * same[L] / pn[L], eff[L] / effn[L]);
      printf(" | trend next 30 min continues %.1f%%, %+.2f pts\n", 100.0 * cont / cn, pts / cn); }
  }
}
