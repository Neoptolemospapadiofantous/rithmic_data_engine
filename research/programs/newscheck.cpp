// verify BLS release dates against NQ's own 08:30 volume spike
#include "research/bars.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
static std::set<int> rd(const char* p) { std::set<int> s; std::ifstream f(p); std::string l; while (std::getline(f, l)) { int y, m, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &m, &d) == 3) s.insert(y * 10000 + m * 100 + d); } return s; }
int main(int, char** av) {
  std::string S = av[1];
  auto cpi = rd((S + "/news/cpi.txt").c_str()), nfp = rd((S + "/news/nfp.txt").c_str()), ppi = rd((S + "/news/ppi.txt").c_str());
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); paper::VixRegime v; build_days(s, {}, v);
  std::map<int, double> ratio;
  for (auto& d : s.days) { if (!d.ok) continue; double pre = 0, ev = 0; int np = 0;
    for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(8, 0) && tm < hm_to_tday(8, 30)) { pre += s.v[i]; ++np; } else if (tm >= hm_to_tday(8, 30) && tm < hm_to_tday(8, 33)) ev += s.v[i]; }
    if (np >= 20 && pre > 0) ratio[d.date] = (ev / 3) / (pre / np); }
  auto rep = [&](const char* n, const std::set<int>& e) { int tot = 0, hit = 0; std::vector<int> miss;
    for (int x : e) { auto it = ratio.find(x); if (it == ratio.end()) continue; ++tot; if (it->second >= 3) ++hit; else miss.push_back(x); }
    printf("%-4s dates with NQ data %3d, 08:30 spike >= 3x %3d (%.0f%%)  low:", n, tot, hit, 100.0 * hit / tot);
    for (int x : miss) printf(" %d(%.1f)", x, ratio[x]); printf("\n"); };
  rep("CPI", cpi); rep("NFP", nfp); rep("PPI", ppi);
  int n = 0, h = 0; for (auto& [d, r] : ratio) if (!cpi.count(d) && !nfp.count(d) && !ppi.count(d) && weekday_of(d) != 4) { ++n; h += r >= 3; }
  printf("other non-Thursday days: %d, spike >= 3x %d (%.0f%%)  (retail sales, GDP, PCE etc. also print at 08:30)\n", n, h, 100.0 * h / n);
}
