// Null market from real NQ 1m bars: inside every trading day the bars' moves (open→close step, high/low offsets,
// volume, aggressor volume) are shuffled, and each whole day's direction is flipped at random (aggressor buy/sell swapped
// with it). Volatility, ranges and volume survive; intraday timing, persistence and day-to-day relations do not.
#include "research/bars.hpp"
#include <cstdio>
#include <random>
using namespace lab;
int main(int argc, char** argv) {
  const int seed = atoi(argv[1]); const std::string out = argv[2];
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { puts(err.c_str()); return 1; }
  paper::VixRegime v; build_days(s, {}, v);
  std::mt19937_64 rng(seed);
  FILE* f = fopen(out.c_str(), "w");
  double px = s.o[0];
  for (auto& d : s.days) {
    const int n = d.b1 - d.b0; std::vector<int> idx(n); for (int k = 0; k < n; ++k) idx[k] = d.b0 + k;
    std::shuffle(idx.begin(), idx.end(), rng);
    const double sg = (rng() & 1) ? 1.0 : -1.0;
    for (int k = 0; k < n; ++k) {
      const int j = idx[k], i = d.b0 + k;
      const double step = sg * (s.c[j] - s.o[j]), up = sg > 0 ? s.h[j] - s.o[j] : s.o[j] - s.l[j], dn = sg > 0 ? s.o[j] - s.l[j] : s.h[j] - s.o[j];
      const double o = px, c = o + step, h = o + up, l = o - dn;
      const double bv = sg > 0 ? s.bv[j] : (s.bv[j] > 0 ? s.v[j] - s.bv[j] : 0);
      fprintf(f, "%lld,%.2f,%.2f,%.2f,%.2f,%.0f,%.0f\n", (long long)s.t[i], o, std::max({h, o, c}), std::min({l, o, c}), c, s.v[j], bv);
      px = c;
      if (px < 100) px += 1000;    // keep prices positive over 16 years of random drift
    }
  }
  fclose(f);
}
