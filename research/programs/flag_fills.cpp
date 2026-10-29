// flag_fills.cpp — how BOOK2's orders would really have filled, from the MBO book (2026-10-24, preregistered
// 2026-10-24_flag_orderflow_sweep.md "Round 6 A"). Read-only on bot/data/raw/mbo.
// usage: flag_fills TRADES2.csv OUT.csv MBOFILE…
// Per trade: is the flag-mid limit traded THROUGH (≥ 1 tick) between registration and flag end, or only touched; base exit kind;
// a stop-market's fill at the best bid/ask of the last quote at the trigger print (slippage in ticks); whether the target was traded
// through before price came back to the stop / flat; half the spread at the 15:55 flat.
#include "history/mbo_feed.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Tr {
  long fid; int date, side; int64_t tfill, tflat, tcut, tend; double mid, stop, target;
  bool touch = false, through = false, filled = false, done = false; char kind = '-'; bool tgt_touched = false, tgt_through = false;
  double slip = NAN, half_spread = NAN;
};
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_fills TRADES2.csv OUT.csv MBOFILE...\n"); return 2; }
  std::vector<Tr> tr; { std::ifstream in(argv[1]); std::string ln; std::getline(in, ln);
    while (std::getline(in, ln)) { std::stringstream ss(ln); std::string c; std::vector<std::string> a; while (std::getline(ss, c, ',')) a.push_back(c);
      Tr t; t.fid = std::stol(a[0]); t.date = std::stoi(a[1]); t.side = std::stoi(a[2]); t.tfill = std::stoll(a[3]); t.tflat = std::stoll(a[4]);
      t.stop = std::stod(a[6]); t.target = std::stod(a[7]); t.tcut = std::stoll(a[10]); t.tend = std::stoll(a[11]); t.mid = std::stod(a[12]); tr.push_back(t); } }
  std::sort(tr.begin(), tr.end(), [](const Tr& a, const Tr& b) { return std::min(a.tcut, a.tfill) < std::min(b.tcut, b.tfill); });
  int64_t t0 = INT64_MAX, t1 = 0; for (auto& t : tr) { t0 = std::min({t0, t.tcut, t.tfill}); t1 = std::max(t1, t.tflat); }
  hist::MboFeed feed(std::vector<std::string>(argv + 3, argv + argc), t0 - 1, t1 + 60'000'000LL);
  std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes; double bid = NAN, ask = NAN; size_t lo = 0;
  auto onTick = [&](Tr& t, const hist::Tick& x) {
    const int d = t.side;
    if (x.ts_us >= t.tcut && x.ts_us < t.tend) { if (d * (t.mid - x.price) >= 0) t.touch = true; if (d * (t.mid - x.price) >= 0.25 - 1e-9) t.through = true; }
    if (!t.filled) { if (x.ts_us >= t.tfill && x.ts_us < t.tfill + 60'000'000LL && d * (t.mid - x.price) >= 0) t.filled = true; else { if (x.ts_us >= t.tfill + 60'000'000LL) t.done = true; return; } }
    if (t.kind == '-') {
      if (x.ts_us >= t.tflat) { t.kind = 'F'; t.half_spread = (std::isfinite(ask) && std::isfinite(bid)) ? (ask - bid) / 2 : 0.125; t.done = true; return; }
      if (d * (x.price - t.stop) <= 0) { t.kind = 'S'; const double q = d > 0 ? bid : ask; double f = std::isfinite(q) ? (d > 0 ? std::min(t.stop, q) : std::max(t.stop, q)) : t.stop;
        f = d > 0 ? std::min(f, x.price) : std::max(f, x.price); t.slip = d * (t.stop - f) / 0.25; t.done = true; return; }
      if (d * (x.price - t.target) >= 0) { t.kind = 'T'; t.tgt_touched = true; if (d * (x.price - t.target) >= 0.25 - 1e-9) { t.tgt_through = true; t.done = true; } return; } }
    else if (t.kind == 'T') {   // touched the target: through before the stop / flat?
      if (d * (x.price - t.target) >= 0.25 - 1e-9) { t.tgt_through = true; t.done = true; }
      else if (d * (x.price - t.stop) <= 0 || x.ts_us >= t.tflat) t.done = true; } };
  while (feed.next_batch(ticks, quotes, 200000)) { size_t qi = 0;
    for (auto& x : ticks) { while (qi < quotes.size() && quotes[qi].ts_us <= x.ts_us) { if (quotes[qi].bid > 0 && quotes[qi].ask > quotes[qi].bid) { bid = quotes[qi].bid; ask = quotes[qi].ask; } qi++; }
      for (size_t i = lo; i < tr.size() && std::min(tr[i].tcut, tr[i].tfill) <= x.ts_us; ++i) if (!tr[i].done) onTick(tr[i], x);
      while (lo < tr.size() && tr[lo].done) lo++; }
    ticks.clear(); quotes.clear(); }
  FILE* fo = fopen(argv[2], "w"); fprintf(fo, "fid,date,side,filled,touch,through,kind,slip_ticks,tgt_through,half_spread\n");
  for (auto& t : tr) fprintf(fo, "%ld,%d,%d,%d,%d,%d,%c,%.3f,%d,%.4f\n", t.fid, t.date, t.side, t.filled, t.touch, t.through, t.kind, t.slip, t.tgt_through, t.half_spread);
  fclose(fo); fprintf(stderr, "%s done\n", argv[2]); return 0; }
