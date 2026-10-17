// 2026-10-17: the fleet × tape-regime result (fleetreg.cpp) broken down by strategy family. Same method: the side (trend-only /
// revert-only / no filter) chosen per strategy on January 2026, judged on Mar + Sep + Oct; the same with the regime labels shuffled
// inside each strategy (20 runs) as the luck baseline. 1-minute tape, acr over the 30 moves before entry.
#include "research/edge_lab.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct T { bool test; int r; double p; };
struct Out { bool chosen = false; double unf = 0, filt = 0; int n_test = 0, n_filt = 0; };
static Out pick(std::vector<T> v) {
  double tr_all = 0, tr_t = 0, tr_r = 0; int na = 0, nt = 0, nr = 0; double te_all = 0, te_t = 0, te_r = 0; int ma = 0, mt = 0, mr = 0;
  for (auto& t : v) { if (!t.test) { tr_all += t.p; ++na; if (t.r > 0) { tr_t += t.p; ++nt; } else { tr_r += t.p; ++nr; } }
                      else { te_all += t.p; ++ma; if (t.r > 0) { te_t += t.p; ++mt; } else { te_r += t.p; ++mr; } } }
  Out o; if (na < 20 || ma < 20 || nt < 8 || nr < 8) return o;
  const double base = tr_all / na, a = tr_t / nt, b = tr_r / nr;
  int side = 0; if (a > base && a >= b) side = 1; else if (b > base && b > a) side = -1;
  o.unf = te_all / ma; o.n_test = ma;
  if (side == 0) { o.filt = o.unf; o.n_filt = ma; o.chosen = true; return o; }   // "no filter" is a choice too
  o.chosen = true; o.n_filt = side > 0 ? mt : mr; o.filt = o.n_filt ? (side > 0 ? te_t : te_r) / o.n_filt : 0; return o;
}
int main(int, char** argv) {
  std::set<int> fomc; paper::VixRegime vix; Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  std::map<std::string, std::string> fam; { std::ifstream f(argv[2]); std::string l; while (std::getline(f, l)) { auto c = l.find(','); if (c != std::string::npos) fam[l.substr(0, c)] = l.substr(c + 1); } }
  std::map<std::string, std::vector<T>> by;
  std::ifstream f(argv[1]); std::string line;
  while (std::getline(f, line)) {
    std::stringstream ss(line); std::string id, a, b, c; std::getline(ss, id, ','); std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, c, ',');
    const int64_t te = std::stoll(a); const size_t i = std::lower_bound(s.t.begin(), s.t.end(), te) - s.t.begin(); if (i < 32 || i >= s.size()) continue;
    double num = 0, den = 0; for (size_t j = i - 30; j < i; ++j) { const double r = s.c[j] - s.c[j - 1], q = s.c[j - 1] - s.c[j - 2]; num += r * q; den += r * r; }
    if (den <= 0) continue; time_t tt = (time_t)te; struct tm g; gmtime_r(&tt, &g);
    by[id].push_back({g.tm_mon != 0, num / den > 0 ? 1 : -1, std::stod(c)});
  }
  struct Agg { int n = 0, better = 0, pos_u = 0, pos_f = 0; double gain = 0, unf = 0, filt = 0, sgain = 0; int spos = 0; };
  std::map<std::string, Agg> A; std::mt19937_64 rng(7);
  for (auto& [id, v] : by) {
    Out o = pick(v); if (!o.chosen) continue;
    auto it = fam.find(id); const std::string fm = it == fam.end() ? "?" : it->second;
    Agg& g = A[fm]; ++g.n; g.gain += o.filt - o.unf; g.better += o.filt > o.unf; g.unf += o.unf; g.filt += o.filt; g.pos_u += o.unf > 0; g.pos_f += o.filt > 0;
    for (int k = 0; k < 20; ++k) { std::vector<T> w = v; std::vector<int> lab; for (auto& t : w) lab.push_back(t.r); std::shuffle(lab.begin(), lab.end(), rng);
      for (size_t q = 0; q < w.size(); ++q) w[q].r = lab[q]; Out z = pick(w); g.sgain += (z.filt - z.unf) / 20; g.spos += 0; if (z.filt > 0) g.spos += 1; }
  }
  printf("%-18s %6s %10s %10s %9s %9s %9s %12s %12s\n", "family", "strats", "unfiltered", "filtered", "gain", "shuffled", "edge", "positive", "pos shuffled");
  std::vector<std::pair<std::string, Agg>> rows(A.begin(), A.end());
  std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.second.n > b.second.n; });
  for (auto& [k, g] : rows) { if (g.n < 30) continue;
    printf("%-18s %6d %+10.2f %+10.2f %+9.2f %+9.2f %+9.2f %5d→%-5d %12.0f\n", k.c_str(), g.n, g.unf / g.n, g.filt / g.n, g.gain / g.n, g.sgain / g.n,
           (g.gain - g.sgain) / g.n, g.pos_u, g.pos_f, g.spos / 20.0); }
}
