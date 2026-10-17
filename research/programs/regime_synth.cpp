// Synthetic NQ with PLANTED regimes, to prove the regime measurement can see regimes when they exist.
// Each trading day is randomly a trending day (1-minute returns autocorrelated +phi) or a mean-reverting day (−phi),
// the same all day across Asia, London and New York. phi = 0 → a plain random walk.
//   regime_synth OUT_DIR phi [seed]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>
#include <string>
static int64_t wall(int y, int m, int d, int h, int mi) { struct tm g{}; g.tm_year = y - 1900; g.tm_mon = m - 1; g.tm_mday = d; g.tm_hour = h; g.tm_min = mi; return (int64_t)timegm(&g); }
int main(int argc, char** argv) {
  if (argc < 3) { std::fprintf(stderr, "usage: regime_synth OUT_DIR phi [seed]\n"); return 2; }
  const std::string out = argv[1]; const double phi = std::atof(argv[2]); const unsigned seed = argc > 3 ? std::atoi(argv[3]) : 11;
  const int BLOCK = argc > 4 ? std::atoi(argv[4]) : 1;   // regime planted on BLOCK-minute moves (1 = every minute)
  std::mt19937_64 rng(seed); std::normal_distribution<double> n01(0, 1); std::bernoulli_distribution coin(0.5);
  FILE* f = std::fopen((out + "/NQ.csv").c_str(), "w"); if (!f) return 1;
  FILE* lab = std::fopen((out + "/regime_labels.csv").c_str(), "w");
  double px = 10000;
  for (int64_t day = wall(2010, 7, 1, 12, 0); day <= wall(2026, 10, 16, 12, 0); day += 86400) {
    time_t tt = (time_t)day; struct tm g; gmtime_r(&tt, &g); if (g.tm_wday == 0 || g.tm_wday == 6) continue;
    const int r = coin(rng) ? 1 : -1; const double a = phi * r; double prev = 0;
    std::fprintf(lab, "%04d%02d%02d,%d\n", g.tm_year + 1900, g.tm_mon + 1, g.tm_mday, r);
    const int64_t start = day - 18 * 3600;
    for (int64_t t = start; t < start + 23 * 3600; t += 60) {
      const int hm = (int)((t % 86400) / 60); const bool rth = hm >= 570 && hm < 960; const double sd = rth ? 3.0 : 1.35;
      double ret;
      if (BLOCK <= 1) { ret = a * prev + sd * std::sqrt(1 - a * a) * n01(rng); prev = ret; }
      else {                                                  // AR on the BLOCK-minute move, spread evenly over its minutes
        static double blk = 0; const double bs = sd * std::sqrt((double)BLOCK);
        if (hm % BLOCK == 0) { blk = a * prev + bs * std::sqrt(1 - a * a) * n01(rng); prev = blk; }
        ret = blk / BLOCK + 0.3 * sd * n01(rng);
      }
      const double o = px, c = std::round((o + ret) * 4) / 4;
      const double h = std::max(o, c) + std::round(std::fabs(n01(rng)) * sd * 0.5 * 4) / 4, l = std::min(o, c) - std::round(std::fabs(n01(rng)) * sd * 0.5 * 4) / 4;
      std::fprintf(f, "%lld,%.2f,%.2f,%.2f,%.2f,%d,0\n", (long long)t, o, h, l, c, rth ? 1000 : 100); px = c;
    }
  }
  std::fclose(f); std::fclose(lab);
  if (FILE* g2 = std::fopen((out + "/fomc.txt").c_str(), "w")) std::fclose(g2);
  std::printf("regime_synth: phi %.3f written to %s\n", phi, out.c_str());
}
