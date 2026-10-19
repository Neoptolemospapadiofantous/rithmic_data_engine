// 2026-10-19 (research/preregistered/2026-10-19_attack_behaviour.md, test 2): re-simulate the book's trades on 1m bars with the 0.35 ATR
// stop and a target chosen by the walk-forward predicted NQ range (quiet two fifths 1.5R, middle 2R, wild two fifths 3R) vs 2R always.
// argv: predicted-range CSV (date,pred,...) then one or more edge_lab --trades CSVs. Read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <map>
using namespace lab;

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: target_by_range PRED.csv TRADES.csv...\n"); return 1; }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::map<int, int> dayIdx; for (int k = 0; k < (int)s.days.size(); ++k) dayIdx[s.days[k].date] = k;
  // predicted range → fifth per era (cut points from that era's own predictions)
  std::map<int, double> pred; { FILE* f = fopen(argv[1], "r"); char ln[256]; fgets(ln, sizeof ln, f); int d; double p; while (fgets(ln, sizeof ln, f)) if (sscanf(ln, "%d,%lf", &d, &p) == 2) pred[d] = p; fclose(f); }
  std::map<int, int> fifth; for (int e = 0; e < 2; ++e) { std::vector<double> v; for (auto& [d, p] : pred) if ((d < 20190101) == (e == 0)) v.push_back(p); std::sort(v.begin(), v.end());
    for (auto& [d, p] : pred) if ((d < 20190101) == (e == 0)) { int q = 0; while (q < 4 && p > v[(q + 1) * v.size() / 5]) ++q; fifth[d] = q; } }
  double sum[2][3] = {{0}}; int n[2][3] = {{0}};   // [schedule][era 0=2014-18 1=2019-24 2=2025-26]
  for (int a = 2; a < argc; ++a) { FILE* f = fopen(argv[a], "r"); char ln[512]; fgets(ln, sizeof ln, f);
    while (fgets(ln, sizeof ln, f)) { int date, dir; long long e; if (sscanf(ln, "%d,%lld,%*lld,%d", &date, &e, &dir) != 3) continue;
      if (!fifth.count(date) || !dayIdx.count(date)) continue; const Day& d = s.days[dayIdx[date]]; if (!(d.atr > 0)) continue;
      auto it = std::lower_bound(s.t.begin() + d.b0, s.t.begin() + d.b1, (int64_t)e); if (it == s.t.begin() + d.b1) continue; int i0 = it - s.t.begin();
      double px = s.o[i0], R = 0.35 * d.atr; int iflat = first_bar_at(s, d, hm_to_tday(15, 55), d.b0);
      int q = fifth[date]; double Ts[2] = {2.0, q <= 1 ? 1.5 : q >= 3 ? 3.0 : 2.0};
      int era = date < 20190101 ? 0 : date < 20250101 ? 1 : 2;
      for (int sc = 0; sc < 2; ++sc) { double stop = px - dir * R, tgt = px + dir * Ts[sc] * R, out = NAN;
        for (int i = i0; i < iflat && i < d.b1; ++i) { bool hs = dir > 0 ? s.l[i] <= stop : s.h[i] >= stop, ht = dir > 0 ? s.h[i] >= tgt : s.l[i] <= tgt;
          if (hs) { out = -R; break; } if (ht) { out = Ts[sc] * R; break; } }
        if (std::isnan(out)) out = dir * (s.c[std::min(iflat, d.b1) - 1] - px);
        sum[sc][era] += (out - 1.0) / R; n[sc][era]++; } }
    fclose(f); }
  const char* EN[3] = {"2014-18", "2019-24", "2025-26"};
  for (int sc = 0; sc < 2; ++sc) { printf("%-38s", sc ? "target by predicted range (1.5/2/3R)" : "target 2R everywhere"); for (int e = 0; e < 3; ++e) printf(" | %s %+.3f R (%d)", EN[e], sum[sc][e] / n[sc][e], n[sc][e]); printf("\n"); }
  return 0;
}
