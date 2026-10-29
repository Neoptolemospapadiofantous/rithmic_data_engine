// flag_candidates.cpp — every minute close of a BOOK2 trade as a candidate ADD (while the base trade is open) or a candidate
// ENTRY (registration → flag end), each simulated tick by tick to stop / target / 15:55 flat (2026-10-24,
// research/preregistered/2026-10-24_flag_orderflow_sweep.md "Round 3"). Python picks the order-flow rule and random twins.
// usage: flag_candidates TRADES2.csv OUT.csv MBOFILE…   (read-only on bot/data/raw/mbo)
// OUT rows: fid,date,side,family(B base · A add · E entry),minute,px,loc,dr,hadneg,R   (R in units of the trade's
// original risk |mid − stop|, cost 1.5 NQ pts; minute = clock minutes since the fill (A) or registration (E))
#include "history/mbo_feed.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

constexpr double COST = 1.5;
struct Sub { char fam; int minute; double px = NAN, loc = 0, dr = 0; int hadneg = 0; bool pending = true, open = true; double R = NAN; };
struct Tr {
  long fid; int date, side; int64_t tfill, tflat, tcut, tend; double mid, stop, target;
  bool filled = false, base_open = true, done = false; double baseR = NAN;
  int64_t emin = -1, amin = -1; int eidx = 0, aidx = 0; double emb = 0, emv = 0, amb = 0, amv = 0, last = NAN; int hadneg = 0;
  std::vector<Sub> subs;
};
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_candidates TRADES2.csv OUT.csv MBOFILE...\n"); return 2; }
  std::vector<Tr> tr; { std::ifstream in(argv[1]); std::string ln; std::getline(in, ln);
    while (std::getline(in, ln)) { std::stringstream ss(ln); std::string c; std::vector<std::string> a; while (std::getline(ss, c, ',')) a.push_back(c);
      Tr t; t.fid = std::stol(a[0]); t.date = std::stoi(a[1]); t.side = std::stoi(a[2]); t.tfill = std::stoll(a[3]); t.tflat = std::stoll(a[4]);
      t.stop = std::stod(a[6]); t.target = std::stod(a[7]); t.tcut = std::stoll(a[10]); t.tend = std::stoll(a[11]); t.mid = std::stod(a[12]); tr.push_back(t); } }
  std::sort(tr.begin(), tr.end(), [](const Tr& a, const Tr& b) { return std::min(a.tcut, a.tfill) < std::min(b.tcut, b.tfill); });
  int64_t t0 = INT64_MAX, t1 = 0; for (auto& t : tr) { t0 = std::min({t0, t.tcut, t.tfill}); t1 = std::max(t1, t.tflat); }
  hist::MboFeed feed(std::vector<std::string>(argv + 3, argv + argc), t0 - 1, t1 + 60'000'000LL);
  std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes;
  auto R1 = [](const Tr& t) { return std::fabs(t.mid - t.stop); };
  auto onTick = [&](Tr& t, const hist::Tick& x) {
    const int d = t.side; const int64_t mi = x.ts_us / 60'000'000LL;
    // pending subs take this print as their entry; invalid if already through the stop or the target
    for (auto& s : t.subs) if (s.pending) { s.pending = false; s.px = x.price;
      if (d * (x.price - t.stop) <= 0 || d * (x.price - t.target) >= 0) { s.open = false; s.R = NAN; } }
    // ENTRY family: minute closes inside [tcut, tend)
    if (x.ts_us >= t.tcut) {
      if (t.emin < 0) t.emin = mi;
      else if (mi != t.emin) { if (t.emin * 60'000'000LL < t.tend && std::isfinite(t.last)) {
          const double dr = t.emv > 0 ? d * (2 * t.emb - t.emv) / t.emv : 0; Sub s; s.fam = 'E'; s.minute = t.eidx; s.loc = d * (t.mid - t.last) >= 0; s.dr = dr; s.hadneg = t.hadneg; t.subs.push_back(s);
          if (dr < 0) t.hadneg = 1; }
        t.eidx++; t.emin = mi; t.emb = t.emv = 0; }
      t.emv += x.size; if (x.is_buy) t.emb += x.size; }
    // base trade: fill in the fill minute at the mid
    if (!t.filled && x.ts_us >= t.tfill && x.ts_us < t.tfill + 60'000'000LL && d * (t.mid - x.price) >= 0) { t.filled = true; t.amin = mi; }
    if (t.filled && t.base_open) {
      if (mi != t.amin) { const double dr = t.amv > 0 ? d * (2 * t.amb - t.amv) / t.amv : 0; Sub s; s.fam = 'A'; s.minute = t.aidx++; s.dr = dr; s.loc = d * (t.last - t.mid); t.subs.push_back(s); t.amin = mi; t.amb = t.amv = 0; }
      t.amv += x.size; if (x.is_buy) t.amb += x.size;
      double px = NAN; if (x.ts_us >= t.tflat) px = x.price; else if (d * (x.price - t.stop) <= 0) px = t.stop; else if (d * (x.price - t.target) >= 0) px = t.target;
      if (std::isfinite(px)) { t.base_open = false; t.baseR = (d * (px - t.mid) - COST) / R1(t);
        for (auto& s : t.subs) if (s.fam == 'A' && s.pending) { s.pending = false; s.open = false; } } }   // an add signalled on the exit print never fills
    t.last = x.price;
    for (auto& s : t.subs) if (!s.pending && s.open) { double px = NAN;
      if (x.ts_us >= t.tflat) px = x.price; else if (d * (x.price - t.stop) <= 0) px = t.stop; else if (d * (x.price - t.target) >= 0) px = t.target;
      if (std::isfinite(px)) { s.open = false; s.R = (d * (px - s.px) - COST) / R1(t); } }
    if (x.ts_us >= t.tflat + 1) t.done = true; };
  size_t lo = 0;
  while (feed.next_batch(ticks, quotes, 200000)) {
    for (auto& x : ticks) { for (size_t i = lo; i < tr.size() && std::min(tr[i].tcut, tr[i].tfill) <= x.ts_us; ++i) if (!tr[i].done) onTick(tr[i], x);
      while (lo < tr.size() && tr[lo].done) lo++; }
    ticks.clear(); quotes.clear(); }
  FILE* fo = fopen(argv[2], "w"); fprintf(fo, "fid,date,side,family,minute,px,loc,dr,hadneg,R\n");
  for (auto& t : tr) { fprintf(fo, "%ld,%d,%d,B,0,%.2f,0,0,0,%.4f\n", t.fid, t.date, t.side, t.mid, t.filled ? t.baseR : NAN);
    for (auto& s : t.subs) if (std::isfinite(s.R)) fprintf(fo, "%ld,%d,%d,%c,%d,%.2f,%.4g,%.4f,%d,%.4f\n", t.fid, t.date, t.side, s.fam, s.minute, s.px, s.loc, s.dr, s.hadneg, s.R); }
  fclose(fo); fprintf(stderr, "%s done (%zu trades)\n", argv[2], tr.size()); return 0; }
