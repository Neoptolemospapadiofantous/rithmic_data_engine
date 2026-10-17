// 2026-10-17: the paper fleet with the tape regime ON, on all 2026 1-minute data we have (Jan, Mar, Sep, Oct — Feb not replayed).
// Every fleet trade gets the tape regime at its entry: acr over the last 30 one-minute moves (1m) and over the last 30
// five-minute moves (5m). Per strategy the regime side (trade only on trending tape, only on reverting tape, or no filter) is
// CHOSEN on January and JUDGED on March + September + October. A shuffled-regime control (labels permuted inside each
// strategy, 100 times) shows how much "improvement" choosing a side produces by luck alone.
#include "research/edge_lab.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct T { bool test; int r1, r5; double p; };
int main(int argc, char** argv) {
  const std::string csv = argv[1];
  std::set<int> fomc; paper::VixRegime vix;
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); const auto& A5 = m.agg(5); const auto& a5 = m.series("acr", 5, 30);
  std::map<std::string, std::vector<T>> by;
  std::ifstream f(csv); std::string line; long rows = 0, miss = 0;
  while (std::getline(f, line)) {
    std::stringstream ss(line); std::string id, a, b, c; std::getline(ss, id, ','); std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, c, ',');
    const int64_t te = std::stoll(a); const double p = std::stod(c); ++rows;
    const size_t i = std::lower_bound(s.t.begin(), s.t.end(), te) - s.t.begin();
    if (i < 32 || i >= s.size()) { ++miss; continue; }
    double num = 0, den = 0;                                  // 1-minute acr over the 30 moves before the entry bar
    for (size_t j = i - 30; j < i; ++j) { const double r = s.c[j] - s.c[j - 1], q = s.c[j - 1] - s.c[j - 2]; num += r * q; den += r * r; }
    const int k = A5.comp[i]; const double v5 = k >= 0 ? a5[k] : NAN;
    if (den <= 0 || std::isnan(v5)) { ++miss; continue; }
    const int month = (int)((te / 86400 / 30.44) ) ; (void)month;
    time_t tt = (time_t)te; struct tm g; gmtime_r(&tt, &g);
    by[id].push_back({g.tm_mon != 0, num / den > 0 ? 1 : -1, v5 > 0 ? 1 : -1, p});
  }
  printf("2026 fleet trades %ld (no regime for %ld), strategies %zu. Choose on January, judge on March + September + October.\n", rows, miss, by.size());
  // pooled: all strategies' test trades by regime
  for (int w = 0; w < 2; ++w) { double st = 0, sr = 0; long nt = 0, nr = 0;
    for (auto& [id, v] : by) for (auto& t : v) if (t.test) { int r = w ? t.r5 : t.r1; if (r > 0) { st += t.p; ++nt; } else { sr += t.p; ++nr; } }
    printf("  pooled test trades, %s tape: trending %+.2f pts/trade (n %ld) · reverting %+.2f (n %ld)\n", w ? "5-minute" : "1-minute", st / nt, nt, sr / nr, nr); }
  std::mt19937_64 rng(42);
  int pos_unf = 0, pos_filt = 0, flipped = 0, good_filt = 0, good_unf = 0;
  auto evaluate = [&](int w, bool shuffle, double& avg_gain, double& share_better, int& chosen, double& total_gain) {
    double gain = 0; int better = 0, ch = 0; total_gain = 0; pos_unf = pos_filt = flipped = good_filt = good_unf = 0;
    for (auto& [id, v0] : by) {
      std::vector<T> v = v0;
      if (shuffle) { std::vector<int> lab; for (auto& t : v) lab.push_back(w ? t.r5 : t.r1); std::shuffle(lab.begin(), lab.end(), rng); for (size_t q = 0; q < v.size(); ++q) (w ? v[q].r5 : v[q].r1) = lab[q]; }
      double tr_all = 0, tr_t = 0, tr_r = 0; int na = 0, nt = 0, nr = 0;
      for (auto& t : v) if (!t.test) { int r = w ? t.r5 : t.r1; tr_all += t.p; ++na; if (r > 0) { tr_t += t.p; ++nt; } else { tr_r += t.p; ++nr; } }
      double te_all = 0, te_t = 0, te_r = 0; int ma = 0, mt = 0, mr = 0;
      for (auto& t : v) if (t.test) { int r = w ? t.r5 : t.r1; te_all += t.p; ++ma; if (r > 0) { te_t += t.p; ++mt; } else { te_r += t.p; ++mr; } }
      if (na < 20 || ma < 20 || nt < 8 || nr < 8) continue;
      const double base = tr_all / na, mt_ = tr_t / nt, mr_ = tr_r / nr;
      int side = 0; if (mt_ > base && mt_ >= mr_) side = 1; else if (mr_ > base && mr_ > mt_) side = -1;
      if (side == 0) continue;
      ++ch;
      const double unf = te_all / ma;
      const double filt_sum = side > 0 ? te_t : te_r; const int filt_n = side > 0 ? mt : mr;
      const double filt = filt_n ? filt_sum / filt_n : 0;
      gain += filt - unf; better += filt > unf;
      pos_unf += unf > 0; pos_filt += filt > 0; flipped += (unf <= 0 && filt > 0);
      if (filt_n >= 20 && filt > 1.0) ++good_filt; if (ma >= 20 && unf > 1.0) ++good_unf;
      total_gain += filt_sum - te_all;                      // test-period net pts with the filter minus without
    }
    avg_gain = ch ? gain / ch : 0; share_better = ch ? (double)better / ch : 0; chosen = ch;
  };
  for (int w = 0; w < 2; ++w) {
    double ag, sb, tg; int ch; evaluate(w, false, ag, sb, ch, tg);
    const int pu = pos_unf, pf = pos_filt, fl = flipped, gf = good_filt, gu = good_unf;
    std::vector<double> nag, nsb, ntg; double spf = 0, sgf = 0, sfl = 0;
    for (int k = 0; k < 100; ++k) { double a, b, c; int d; evaluate(w, true, a, b, d, c); nag.push_back(a); nsb.push_back(b); ntg.push_back(c); spf += pos_filt / 100.0; sgf += good_filt / 100.0; sfl += flipped / 100.0; }
    int beat_ag = 0, beat_sb = 0; for (int k = 0; k < 100; ++k) { beat_ag += ag > nag[k]; beat_sb += sb > nsb[k]; }
    double mag = 0, msb = 0, mtg = 0; for (int k = 0; k < 100; ++k) { mag += nag[k] / 100; msb += nsb[k] / 100; mtg += ntg[k] / 100; }
    printf("\n  %s tape regime: a side chosen for %d strategies\n", w ? "5-minute" : "1-minute", ch);
    printf("    test months, per strategy: filtered vs unfiltered %+.3f pts/trade, better for %.1f%% of strategies; fleet net with filter − without: %+.0f pts\n", ag, 100 * sb, tg);
    printf("    strategies positive in the test months: %d without the filter, %d with it (%d turned from losing to winning); clearly profitable (> +1 pt/trade, >= 20 trades): %d without, %d with\n", pu, pf, fl, gu, gf);
    printf("    shuffled control, same counts: positive with the filter %.0f (turned %.0f), clearly profitable %.0f\n", spf, sfl, sgf);
    printf("    shuffled-regime control (100 runs): %+.3f pts/trade, better for %.1f%%, fleet net %+.0f — the real regime beats it on per-trade gain in %d%% and on share better in %d%% of runs\n",
           mag, 100 * msb, mtg, beat_ag, beat_sb);
  }
}
