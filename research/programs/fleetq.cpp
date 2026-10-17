// 2026-10-17 "are the fleet's entries or its exits the problem?"  fleetq TRADES_CSV
// Every nq_hist fleet trade (2024-03 → 2026-10, 8.3 M MNQ trades). Prices from the NQ 1-minute cache, all measured from the
// open of the first minute after the entry (≤ 60 s late — conservative, same for every horizon):
//   ENTRY edge at +N min = direction × (price N minutes later − price at entry). A random entry averages 0 (random direction)
//   / the drift (random time). If the entries were good, the price would move their way over the next minutes.
//   EXIT value = the trade's own exit replayed on the same prices − the best fixed-time exit (+5/+15/+30/+60) for that family.
// Per family (strategy id before "__", cut at the first token with a digit) and per strategy id.
#include "research/bars.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace lab;
struct A { double n = 0, s = 0, ss = 0; void add(double x) { n++; s += x; ss += x * x; } double m() const { return n ? s / n : NAN; }
  double t() const { if (n < 2) return 0; double mu = s / n, v = ss / n - mu * mu; return v > 0 ? mu / std::sqrt(v / n) : 0; } };
struct F { A gross, replay, e[4], mfe, mae; double longs = 0; };
int main(int argc, char** argv) {
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  auto p_at = [&](int64_t T, double& p) { auto it = std::lower_bound(s.t.begin(), s.t.end(), T); if (it == s.t.end() || *it - T > 120) return false; p = s.o[it - s.t.begin()]; return true; };
  std::map<std::string, F> fam, ids; F all;
  std::ifstream in(argv[1]); std::string line; const int H[4] = {5, 15, 30, 60}; long rows = 0, used = 0;
  while (std::getline(in, line)) { ++rows;
    std::stringstream ss(line); std::string id, a, b, d, pn, mf, ma; std::getline(ss, id, ','); std::getline(ss, a, ','); std::getline(ss, b, ','); std::getline(ss, d, ','); std::getline(ss, pn, ','); std::getline(ss, mf, ','); std::getline(ss, ma, ',');
    const int64_t te = std::stoll(a), tx = std::stoll(b); const int dir = std::stoi(d);
    const int64_t t0 = (te / 60 + 1) * 60; double p0, px; if (!p_at(t0, p0)) continue;
    if (!p_at(std::max(t0, (tx / 60 + 1) * 60), px)) continue; ++used;
    std::string base = id.substr(0, id.find("__")), f; { std::stringstream q(base); std::string tok; while (std::getline(q, tok, '_')) { if (std::any_of(tok.begin(), tok.end(), ::isdigit)) break; f += (f.empty() ? "" : "_") + tok; } if (f.empty()) f = base; }
    for (F* x : {&fam[f], &ids[id], &all}) { x->gross.add(std::stod(pn)); x->replay.add(dir * (px - p0)); x->mfe.add(mf.empty() ? 0 : std::stod(mf)); x->mae.add(ma.empty() ? 0 : std::fabs(std::stod(ma))); x->longs += dir > 0;
      for (int k = 0; k < 4; ++k) { double p; if (p_at(t0 + H[k] * 60, p)) x->e[k].add(dir * (p - p0)); } } }
  auto row = [&](const char* nm, const F& x) { int bk = 0; for (int k = 1; k < 4; ++k) if (x.e[k].m() > x.e[bk].m()) bk = k;
    printf("%-26s %8.0f  %+6.2f  %+6.2f | %+5.2f (t%+5.1f) %+5.2f (t%+5.1f) %+5.2f (t%+5.1f) %+5.2f (t%+5.1f) | best fixed +%-2dm %+5.2f → exit adds %+5.2f | long %3.0f%%\n",
           nm, x.gross.n, x.gross.m(), x.replay.m(), x.e[0].m(), x.e[0].t(), x.e[1].m(), x.e[1].t(), x.e[2].m(), x.e[2].t(), x.e[3].m(), x.e[3].t(), H[bk], x.e[bk].m(), x.replay.m() - x.e[bk].m(), 100 * x.longs / x.gross.n); };
  printf("rows %ld, priced %ld\n%-26s %8s  %6s  %6s | entry edge: dir × move after +5m / +15m / +30m / +60m (NQ pts, t)            | exits\n", rows, used, "family", "trades", "paper", "replay");
  row("ALL FLEET", all);
  std::vector<std::pair<double, std::string>> v; for (auto& [k, x] : fam) v.push_back({x.gross.n, k}); std::sort(v.rbegin(), v.rend());
  for (auto& [n, k] : v) if (n >= 20000) row(k.c_str(), fam[k]);
  // per strategy id: how many have a real entry edge (+15 and +60 min, t > 2) vs the ~2.3 % expected by chance
  int nid = 0, e15 = 0, e60 = 0, both = 0, exitgood = 0, net = 0;
  for (auto& [k, x] : ids) { if (x.gross.n < 300) continue; ++nid; bool a1 = x.e[1].t() > 2, a2 = x.e[3].t() > 2; e15 += a1; e60 += a2; both += a1 && a2;
    double bestm = std::max({x.e[0].m(), x.e[1].m(), x.e[2].m(), x.e[3].m()}); exitgood += x.replay.m() > bestm; net += x.gross.m() > 1.6; }
  printf("\nstrategy ids with ≥300 trades: %d | entry edge t>2 at +15m: %d (%.1f%%), at +60m: %d (%.1f%%), both: %d — chance alone ≈ 2.3%% each\n"
         "  own exit beats the best fixed-time exit: %d (%.1f%%) | average trade > 1.6 pts (≈ the measured round-trip cost): %d (%.1f%%)\n",
         nid, e15, 100.0 * e15 / nid, e60, 100.0 * e60 / nid, both, exitgood, 100.0 * exitgood / nid, net, 100.0 * net / nid);
}
