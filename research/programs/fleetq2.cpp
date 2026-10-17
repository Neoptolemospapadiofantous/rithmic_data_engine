// 2026-10-17 follow-up to fleetq: "the entries are fine, the exits kill the edge" — tested the strict way
// (feedback_exit_resim_traps: one position at a time + random-timing twins).
// Per strategy id (≥300 trades): walk its entries in time order, ONE position at a time, hold H minutes (60, 120, or to 15:55 for
// RTH entries), cost 1.6 NQ pts (measured commission + slippage). Twins, same count, same direction rule:
//   OPEN-DIR twin = same moment, direction = sign(price − today's 09:30 open), RTH only (does the signal beat 'go with the day'?) [a ±90-min random-time twin was dropped: entering before the signal in the signal's direction is look-ahead]
//   DAY twin   = same time of day, same direction, a random other day in the same month (does today matter?)
// Reported per family (median over its ids) and over all ids, by year.
#include "research/bars.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Tr { int64_t te; int dir; };
int main(int argc, char** argv) {
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1;
  auto idx = [&](int64_t T) -> long { auto it = std::lower_bound(s.t.begin(), s.t.end(), T); if (it == s.t.end() || *it - T > 120) return -1; return it - s.t.begin(); };
  std::map<std::string, std::vector<Tr>> ids; { std::ifstream in(argv[1]); std::string l;
    while (std::getline(in, l)) { std::stringstream q(l); std::string id, a, b, d; std::getline(q, id, ','); std::getline(q, a, ','); std::getline(q, b, ','); std::getline(q, d, ',');
      ids[id].push_back({(std::stoll(a) / 60 + 1) * 60, std::stoi(d)}); } }
  std::vector<int64_t> days; for (size_t i = 0; i < s.size(); ++i) { int64_t d = s.t[i] / 86400 * 86400; if (days.empty() || days.back() != d) days.push_back(d); }
  std::mt19937 rng(7); const double COST = 1.6;
  const int HS[3] = {60, 120, -1}; const char* HN[3] = {"hold 60m", "hold 120m", "hold to 15:55"};
  auto fam_of = [](const std::string& id) { std::string base = id.substr(0, id.find("__")), f; std::stringstream q(base); std::string tok;
    while (std::getline(q, tok, '_')) { if (std::any_of(tok.begin(), tok.end(), ::isdigit)) break; f += (f.empty() ? "" : "_") + tok; } return f.empty() ? base : f; };
  for (int h = 0; h < 3; ++h) {
    // outcome of a position entered at T (wall-clock ET epoch) in direction dir; NAN if unpriced
    auto out = [&](int64_t T, int dir) -> double { long i = idx(T); if (i < 0) return NAN; int64_t E;
      if (HS[h] > 0) E = T + HS[h] * 60; else { int tm = tday_min(T); if (tm < hm_to_tday(9, 30) || tm >= hm_to_tday(15, 55)) return NAN; E = T / 86400 * 86400 + (15 * 60 + 55) * 60; }
      long j = idx(E); if (j < 0) return NAN; return dir * ((double)s.o[j] - (double)s.o[i]) - COST; };
    std::map<std::string, std::vector<double>> famv[3]; std::map<int, std::array<double, 6>> byyear;   // real/time/day sums+counts
    std::vector<double> allv[3];
    for (auto& [id, v] : ids) { if (v.size() < 300) continue; std::sort(v.begin(), v.end(), [](const Tr& a, const Tr& b) { return a.te < b.te; });
      double sr = 0, st = 0, sd = 0; int nr = 0, nt = 0, nd = 0; int64_t busy = 0;
      for (auto& t : v) { if (t.te < busy) continue; double r = out(t.te, t.dir); if (std::isnan(r)) continue;
        busy = HS[h] > 0 ? t.te + HS[h] * 60 : t.te / 86400 * 86400 + 86400;
        sr += r; ++nr; int y = ymd_of(t.te) / 10000; auto& Y = byyear[y]; Y[0] += r; Y[1] += 1;
        { int tm = tday_min(t.te); long io = idx(t.te / 86400 * 86400 + 570 * 60), ie = idx(t.te); if (tm >= hm_to_tday(9, 31) && tm < hm_to_tday(15, 55) && io >= 0 && ie >= 0 && s.o[ie] != s.o[io]) {
            const int d2 = s.o[ie] > s.o[io] ? 1 : -1; double x = out(t.te, d2); if (!std::isnan(x)) { st += x; ++nt; Y[2] += x; Y[3] += 1; } } }
        { auto it = std::lower_bound(days.begin(), days.end(), t.te / 86400 * 86400); long k = it - days.begin(), k2 = k + std::uniform_int_distribution<int>(-10, 10)(rng);
          if (k2 >= 0 && k2 < (long)days.size() && k2 != k) { double x = out(days[k2] + t.te % 86400, t.dir); if (!std::isnan(x)) { sd += x; ++nd; Y[4] += x; Y[5] += 1; } } } }
      if (nr < 100 || !nt || !nd) continue; const std::string f = fam_of(id);
      famv[0][f].push_back(sr / nr); famv[1][f].push_back(st / nt); famv[2][f].push_back(sd / nd); allv[0].push_back(sr / nr); allv[1].push_back(st / nt); allv[2].push_back(sd / nd); }
    auto med = [](std::vector<double> x) { if (x.empty()) return (double)NAN; std::sort(x.begin(), x.end()); return x[x.size() / 2]; };
    auto pos = [](const std::vector<double>& x) { int p = 0; for (double v : x) p += v > 0; return 100.0 * p / x.size(); };
    printf("\n===== %s, one position at a time, after 1.6 pts cost (NQ pts/trade; median over strategy ids) =====\n", HN[h]);
    printf("ALL %zu ids: fleet entries %+.2f (%.0f%% of ids > 0) | open-dir twin %+.2f (%.0f%%) | day twin %+.2f (%.0f%%)\n", allv[0].size(), med(allv[0]), pos(allv[0]), med(allv[1]), pos(allv[1]), med(allv[2]), pos(allv[2]));
    printf("  by year (all trades pooled): "); for (auto& [y, Y] : byyear) printf("%d real %+.2f time %+.2f day %+.2f | ", y, Y[0] / Y[1], Y[2] / Y[3], Y[4] / Y[5]); printf("\n");
    std::vector<std::pair<size_t, std::string>> fs; for (auto& [f, x] : famv[0]) fs.push_back({x.size(), f}); std::sort(fs.rbegin(), fs.rend());
    for (auto& [n, f] : fs) if (n >= 15) printf("  %-22s ids %4zu  real %+6.2f (%3.0f%% >0)  open-dir twin %+6.2f  day twin %+6.2f  → signal vs open-dir %+5.2f, today's direction adds %+5.2f\n",
                                                  f.c_str(), n, med(famv[0][f]), pos(famv[0][f]), med(famv[1][f]), med(famv[2][f]), med(famv[0][f]) - med(famv[1][f]), med(famv[0][f]) - med(famv[2][f]));
  }
}
