// flag_exitflow.cpp — BOOK2 trades replayed tick by tick from the MBO files with order-flow EXIT rules (2026-10-24,
// research/preregistered/2026-10-24_flag_orderflow_sweep.md "Exits by order flow"). Read-only on bot/data/raw/mbo.
// usage: flag_exitflow TRADES.csv OUT.csv MBOFILE…
// TRADES.csv: fid,date,side,t_fill,t_flat,entry,stop,target,r_export,pts_export (t_* = UTC µs; t_fill = start of the fill minute).
// Fill = first print through the flag-mid limit inside the fill minute. Every rule keeps stop / target / 15:55 flat;
// rules are judged at each clock-minute close and act on the next print. Cost 1.5 NQ pts per trade (as the export).
// OUT: per trade base R, export R, R + exit minute for every rule, and the minute-close path (pts) for random-timing twins.
#include "history/mbo_feed.hpp"
#include "history/binance_feed.hpp"
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct Rule { const char* name; int kind; double a, b; };   // kind 0 base, 1 FLIP_LOSS(m,x), 2 FLIP_ANY(m,x), 3 STALL(m), 4 BE_FLOW(k), 5 BOOK_LOSS
static const Rule RULES[] = {{"base", 0, 0, 0},
  {"flip_loss_m2_x15", 1, 2, 0.15}, {"flip_loss_m2_x30", 1, 2, 0.30}, {"flip_loss_m5_x15", 1, 5, 0.15}, {"flip_loss_m5_x30", 1, 5, 0.30},
  {"flip_any_m2_x15", 2, 2, 0.15}, {"flip_any_m2_x30", 2, 2, 0.30}, {"flip_any_m5_x15", 2, 5, 0.15}, {"flip_any_m5_x30", 2, 5, 0.30},
  {"stall_m10", 3, 10, 0}, {"stall_m20", 3, 20, 0}, {"be_flow_k05", 4, 0.5, 0}, {"be_flow_k10", 4, 1.0, 0}, {"book_loss_40", 5, 0.40, 0}};
constexpr int NR = sizeof(RULES) / sizeof(RULES[0]);
// cost per round trip: 1.5 NQ pts, or env EXIT_COST_PCT × entry price (crypto maker fee, e.g. 0.0004)
static const double COSTPCT = std::getenv("EXIT_COST_PCT") ? std::atof(std::getenv("EXIT_COST_PCT")) : 0.0;

struct RS { bool open = true; double stop = 0, R = NAN; int xmin = -1; };
struct Tr {
  long fid; int date, side; int64_t tfill, tflat; double entry, stop, target, rexp;
  bool filled = false, closed = false, nofill = false; int64_t t0 = 0, curmin = -1; int elapsed = 0;
  double mb = 0, mv = 0, cumd = 0, mfe = 0, last_px = 0, qimb = 0; long qn = 0; RS rs[NR]; std::string path;
};

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_exitflow TRADES.csv OUT.csv MBOFILE...\n"); return 2; }
  std::vector<Tr> tr; { std::ifstream in(argv[1]); std::string ln; std::getline(in, ln);
    while (std::getline(in, ln)) { std::stringstream ss(ln); std::string c; std::vector<std::string> a; while (std::getline(ss, c, ',')) a.push_back(c);
      Tr t; t.fid = std::stol(a[0]); t.date = std::stoi(a[1]); t.side = std::stoi(a[2]); t.tfill = std::stoll(a[3]); t.tflat = std::stoll(a[4]);
      t.entry = std::stod(a[5]); t.stop = std::stod(a[6]); t.target = std::stod(a[7]); t.rexp = std::stod(a[8]);
      for (auto& r : t.rs) r.stop = t.stop; tr.push_back(t); } }
  std::sort(tr.begin(), tr.end(), [](const Tr& a, const Tr& b) { return a.tfill < b.tfill; });
  const std::vector<std::string> files(argv + 3, argv + argc); std::unique_ptr<hist::TickFeed> fp;   // .zip = Binance aggTrades (no quotes)
  if (files[0].size() > 4 && files[0].substr(files[0].size() - 4) == ".zip") fp = std::make_unique<hist::AggTradesFeed>(files, tr.front().tfill - 1, tr.back().tflat + 60'000'000LL);
  else fp = std::make_unique<hist::MboFeed>(files, tr.front().tfill - 1, tr.back().tflat + 60'000'000LL);
  hist::TickFeed& feed = *fp;
  std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes; size_t lo = 0;
  auto risk = [](const Tr& t) { return std::fabs(t.entry - t.stop); };
  auto close_rule = [&](Tr& t, int k, double px) { RS& r = t.rs[k]; if (!r.open) return; r.open = false; r.R = (t.side * (px - t.entry) - (COSTPCT > 0 ? COSTPCT * t.entry : 1.5)) / risk(t); r.xmin = t.elapsed; };
  auto all_closed = [&](Tr& t) { for (auto& r : t.rs) if (r.open) return false; return true; };
  auto minute_close = [&](Tr& t, double next_px) {    // evaluate rules on the minute that just ended, act at next_px
    const double dr = t.mv > 0 ? t.side * (2 * t.mb - t.mv) / t.mv : 0.0, pts = t.side * (t.last_px - t.entry), R1 = risk(t);
    const double im = t.qn ? t.qimb / t.qn : 0.5;
    char b[24]; snprintf(b, sizeof b, "%s%.2f", t.path.empty() ? "" : ";", pts); t.path += b;
    t.elapsed++;
    for (int k = 1; k < NR; ++k) { RS& r = t.rs[k]; if (!r.open) continue; const Rule& u = RULES[k];
      if (u.kind == 1 && t.elapsed >= u.a && dr <= -u.b && pts < 0) close_rule(t, k, next_px);
      else if (u.kind == 2 && t.elapsed >= u.a && dr <= -u.b) close_rule(t, k, next_px);
      else if (u.kind == 3 && t.elapsed >= u.a && t.mfe < 0.3 * R1 && t.cumd * t.side <= 0) close_rule(t, k, next_px);
      else if (u.kind == 4 && t.mfe >= u.a * R1 && dr >= 0.15 && t.side * (r.stop - t.entry) < 0) r.stop = t.entry;
      else if (u.kind == 5 && t.elapsed >= 2 && im < u.a && pts < 0) close_rule(t, k, next_px); }
    t.mb = t.mv = 0; t.qimb = 0; t.qn = 0; };
  auto on_tick = [&](const hist::Tick& x) {
    for (size_t i = lo; i < tr.size() && tr[i].tfill <= x.ts_us; ++i) { Tr& t = tr[i]; if (t.closed) continue;
      if (!t.filled) {
        if (x.ts_us >= t.tfill + 60'000'000LL) { t.nofill = true; t.closed = true; continue; }
        if (t.side * (t.entry - x.price) >= 0) { t.filled = true; t.t0 = x.ts_us; t.curmin = x.ts_us / 60'000'000LL; t.last_px = x.price; }
        continue; }
      const int64_t mi = x.ts_us / 60'000'000LL;
      if (mi != t.curmin) { minute_close(t, x.price); t.curmin = mi; }
      t.mv += x.size; if (x.is_buy) t.mb += x.size; t.cumd += x.is_buy ? x.size : -x.size; t.last_px = x.price;
      t.mfe = std::max(t.mfe, t.side * (x.price - t.entry));
      for (int k = 0; k < NR; ++k) { RS& r = t.rs[k]; if (!r.open) continue;
        if (x.ts_us >= t.tflat) { close_rule(t, k, x.price); continue; }
        if (t.side * (x.price - r.stop) <= 0) close_rule(t, k, r.stop);
        else if (t.side * (x.price - t.target) >= 0) close_rule(t, k, t.target); }
      if (all_closed(t)) t.closed = true; }
    while (lo < tr.size() && tr[lo].closed) lo++; };
  auto on_quote = [&](const hist::Quote& q) {
    const int tot = q.bid_sz + q.ask_sz; if (tot <= 0 || q.ask <= q.bid) return;
    for (size_t i = lo; i < tr.size() && tr[i].tfill <= q.ts_us; ++i) { Tr& t = tr[i]; if (t.closed || !t.filled) continue;
      double im = (double)q.bid_sz / tot; if (t.side < 0) im = 1 - im; t.qimb += im; t.qn++; } };
  while (feed.next_batch(ticks, quotes, 200000)) { size_t qi = 0;
    for (auto& t : ticks) { while (qi < quotes.size() && quotes[qi].ts_us <= t.ts_us) on_quote(quotes[qi++]); on_tick(t); }
    ticks.clear(); quotes.clear(); }
  FILE* fo = fopen(argv[2], "w"); fprintf(fo, "fid,date,side,status,r_export"); for (auto& u : RULES) fprintf(fo, ",%s,%s_min", u.name, u.name); fprintf(fo, ",path\n");
  long nf = 0, nc = 0;
  for (auto& t : tr) { const char* st = t.nofill ? "nofill" : !t.filled ? "nodata" : all_closed(t) ? "ok" : "open"; nf += t.nofill; nc += all_closed(t) && t.filled;
    fprintf(fo, "%ld,%d,%d,%s,%.4f", t.fid, t.date, t.side, st, t.rexp); for (auto& r : t.rs) fprintf(fo, ",%.4f,%d", r.R, r.xmin); fprintf(fo, ",%s\n", t.path.c_str()); }
  fclose(fo); fprintf(stderr, "%s: %zu trades, %ld simulated, %ld no fill\n", argv[2], tr.size(), nc, nf); return 0; }
