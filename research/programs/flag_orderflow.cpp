// flag_orderflow.cpp — order flow INSIDE each flag's candles from the Databento MBO files (2026-10-24,
// research/preregistered/2026-10-24_flag_orderflow_sweep.md). Read-only on bot/data/raw/mbo.
//
// usage: flag_orderflow WINDOWS.csv OUT.csv MBOFILE…   (one month file per run is fine; flags whose cut-off is
//        outside the file's span are skipped and can be picked up by another run)
// WINDOWS.csv (geometry.py): fid,date,side,tf,grade,book2,x2,x4,r,t_base,t_pole,t_peak,t_cut,t_fill,pole_hi,pole_lo,
//        flag_x,entry,… (times = UTC µs). Segments: B [t_base,t_pole) baseline, P [t_pole,t_peak) pole,
//        F [t_peak,t_cut) flag, E [t_cut,t_fill) registration → fill minute. Every feature is on OUR side
//        (short flags mirrored). Nothing at or after t_cut enters a P_/F_/S_ feature.
#include "history/mbo_feed.hpp"
#include "history/binance_feed.hpp"
#include <memory>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

struct T { int64_t ts; double px; int64_t sz; bool buy; };
struct QA { long n = 0; double imb = 0, spr = 0; double last_imb = NAN, last_spr = NAN, last_micro = NAN; };
struct Flag {
  long fid; int date, side, tf, grade, book2; double x2, x4, r; int64_t tb, tp, tk, tc, tf_; double H, L, FX;
  std::vector<T> ticks; QA q[4]; bool seen_base = false; int64_t first_ts = 0;
  double sess_pv = NAN, sess_v = NAN, sess_d = NAN; int64_t topen = 0;
  std::map<double, double> fexec, fdisp;   // Round 6 B: per price in the flag, volume executed at the best bid (long) / ask (short), largest size displayed there
};
static int seg(const Flag& f, int64_t ts) { return ts < f.tp ? 0 : ts < f.tk ? 1 : ts < f.tc ? 2 : ts < f.tf_ ? 3 : -1; }

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_orderflow WINDOWS.csv OUT.csv MBOFILE...\n"); return 2; }
  std::vector<Flag> fl; {
    std::ifstream in(argv[1]); std::string ln; std::getline(in, ln);
    while (std::getline(in, ln)) { std::stringstream ss(ln); std::string c; std::vector<std::string> a; while (std::getline(ss, c, ',')) a.push_back(c);
      Flag f{}; f.fid = std::stol(a[0]); f.date = std::stoi(a[1]); f.side = std::stoi(a[2]); f.tf = std::stoi(a[3]); f.grade = std::stoi(a[4]);
      f.book2 = std::stoi(a[5]); f.x2 = std::stod(a[6]); f.x4 = std::stod(a[7]); f.r = std::stod(a[8]);
      f.tb = std::stoll(a[9]); f.tp = std::stoll(a[10]); f.tk = std::stoll(a[11]); f.tc = std::stoll(a[12]); f.tf_ = std::max<int64_t>(std::stoll(a[13]), f.tc);
      f.H = std::stod(a[14]); f.L = std::stod(a[15]); f.FX = std::stod(a[16]); f.topen = std::stoll(a[20]); fl.push_back(std::move(f)); } }
  std::sort(fl.begin(), fl.end(), [](const Flag& a, const Flag& b) { return a.tb < b.tb; });
  std::vector<std::string> files(argv + 3, argv + argc);
  // session VWAP / delta reset at each flag's 09:30 ET open (t_open, from geometry.py — DST handled there)
  // Binance aggTrades (.zip) → trades with aggressor side, no quotes (book features stay NaN); otherwise the CME MBO files
  std::unique_ptr<hist::TickFeed> feedp;
  if (files[0].size() > 4 && files[0].substr(files[0].size() - 4) == ".zip") feedp = std::make_unique<hist::AggTradesFeed>(files, fl.front().tb - 1, fl.back().tf_ + 60'000'000LL);
  else feedp = std::make_unique<hist::MboFeed>(files, fl.front().tb - 1, fl.back().tf_ + 60'000'000LL);
  hist::TickFeed& feed = *feedp;
  std::vector<hist::Tick> ticks; std::deque<hist::Quote> quotes;
  size_t lo = 0;   // flags before lo are finished
  std::vector<char> done(fl.size(), 0);
  FILE* fo = fopen(argv[2], "w");
  fprintf(fo, "fid,date,side,tf,grade,book2,x2,x4,r,complete,"
              "P_vol_rel,P_delta,P_big_share,P_big_delta,P_avgsize_rel,P_rate_rel,P_late_delta,P_top_share,P_eff,P_imb,P_spr,P_qrate_rel,"
              "F_dry,F_delta,F_absorb,F_big_share,F_big_delta,F_imb,F_imb_last,F_micro_last,F_spr_last,F_spr,F_qrate_rel,F_ext_share,F_tests,"
              "F_poc_pos,F_vwap,F_last1_delta,F_last1_vol,F_size_asym,F_late_delta,F_undo,F_minutes,F_speed,"
              "S_vwap,S_cumdelta,E_delta,E_vol_rel,E_imb_last,E_wait,F_hidden,F_hidden_vol\n");
  std::vector<int64_t> opens; for (auto& f : fl) opens.push_back(f.topen); std::sort(opens.begin(), opens.end()); opens.erase(std::unique(opens.begin(), opens.end()), opens.end()); size_t oi = 0; bool s_ok = false;
  double s_pv = 0, s_v = 0, s_d = 0;
  auto finish = [&](Flag& f) {
    const int d = f.side;
    double v[4] = {0}, b[4] = {0}, n[4] = {0}; for (auto& t : f.ticks) { int s = seg(f, t.ts); if (s < 0) continue; v[s] += t.sz; if (t.buy) b[s] += t.sz; n[s]++; }
    const double mB = (f.tp - f.tb) / 6e7, mP = (f.tk - f.tp) / 6e7, mF = (f.tc - f.tk) / 6e7, mE = (f.tf_ - f.tc) / 6e7;
    auto dr = [&](double bb, double vv) { return vv > 0 ? d * (2 * bb - vv) / vv : NAN; };
    std::vector<int64_t> bs; for (auto& t : f.ticks) if (seg(f, t.ts) == 0) bs.push_back(t.sz); std::sort(bs.begin(), bs.end());
    const double thr = bs.empty() ? 10 : std::max<double>(2, bs[(size_t)(0.95 * (bs.size() - 1))]);
    double bigv[4] = {0}, bigb[4] = {0}; for (auto& t : f.ticks) { int s = seg(f, t.ts); if (s < 0 || t.sz < thr) continue; bigv[s] += t.sz; if (t.buy) bigb[s] += t.sz; }
    const double rng = f.H - f.L; const bool complete = f.seen_base && f.first_ts <= f.tb + 60'000'000LL && v[0] > 0 && v[1] > 0 && v[2] > 0;
    // pole: halves, top quarter, efficiency
    const int64_t pmid = (f.tp + f.tk) / 2; double h1v = 0, h1b = 0, h2v = 0, h2b = 0, top = 0;
    for (auto& t : f.ticks) { int s = seg(f, t.ts); if (s != 1) continue; if (t.ts < pmid) { h1v += t.sz; h1b += t.buy ? t.sz : 0; } else { h2v += t.sz; h2b += t.buy ? t.sz : 0; }
      if (d > 0 ? t.px >= f.H - 0.25 * rng : t.px <= f.L + 0.25 * rng) top += t.sz; }
    const double Pnet = 2 * b[1] - v[1], Fnet = 2 * b[2] - v[2], Enet = 2 * b[3] - v[3];
    // flag: extreme, tests, POC, VWAP, last minute, halves, sizes
    double ext = d > 0 ? 1e18 : -1e18, lastpx = NAN, fpv = 0; std::map<double, double> vap;
    for (auto& t : f.ticks) if (seg(f, t.ts) == 2) { ext = d > 0 ? std::min(ext, t.px) : std::max(ext, t.px); vap[t.px] += t.sz; fpv += t.px * t.sz; lastpx = t.px; }
    double extv = 0; int tests = 0; bool near = false;
    for (auto& t : f.ticks) if (seg(f, t.ts) == 2) { const double dist = d * (t.px - ext); if (dist <= 0.25 + 1e-9) { extv += t.sz; if (!near) { tests++; near = true; } } else if (dist >= 0.5 - 1e-9) near = false; }
    double poc = NAN, pocv = -1; for (auto& [p, q] : vap) if (q > pocv) { pocv = q; poc = p; }
    const double startpx = d > 0 ? f.H : f.L, pb = std::fabs(startpx - ext);
    const int64_t fmid = (f.tk + f.tc) / 2, l1 = f.tc - 60'000'000LL; double g1v = 0, g1b = 0, g2v = 0, g2b = 0, lv = 0, lb = 0, ourv = 0, ourn = 0, cv = 0, cn = 0;
    for (auto& t : f.ticks) { if (seg(f, t.ts) != 2) continue; if (t.ts < fmid) { g1v += t.sz; g1b += t.buy ? t.sz : 0; } else { g2v += t.sz; g2b += t.buy ? t.sz : 0; }
      if (t.ts >= l1) { lv += t.sz; lb += t.buy ? t.sz : 0; } const bool ours = (t.buy == (d > 0)); if (ours) { ourv += t.sz; ourn++; } else { cv += t.sz; cn++; } }
    const double counter = d > 0 ? v[2] - b[2] : b[2];
    auto rate = [](double x, double m) { return m > 0 ? x / m : NAN; };
    auto rel = [](double a, double bb) { return (std::isfinite(a) && std::isfinite(bb) && bb > 0) ? a / bb : NAN; };
    const double sv = std::isfinite(f.sess_v) && f.sess_v > 0 ? f.sess_pv / f.sess_v : NAN;
    fprintf(fo, "%ld,%d,%d,%d,%d,%d,%.5g,%.5g,%.5g,%d", f.fid, f.date, f.side, f.tf, f.grade, f.book2, f.x2, f.x4, f.r, (int)complete);
    const double P[] = {rel(rate(v[1], mP), rate(v[0], mB)), dr(b[1], v[1]), v[1] > 0 ? bigv[1] / v[1] : NAN, dr(bigb[1], bigv[1]),
      rel(n[1] > 0 ? v[1] / n[1] : NAN, n[0] > 0 ? v[0] / n[0] : NAN), rel(rate(n[1], mP), rate(n[0], mB)), dr(h2b, h2v) - dr(h1b, h1v),
      v[1] > 0 ? top / v[1] : NAN, std::fabs(Pnet) > 0 ? rng / std::fabs(Pnet) * 100 : NAN,
      f.q[1].n ? f.q[1].imb / f.q[1].n : NAN, f.q[1].n ? f.q[1].spr / f.q[1].n : NAN, rel(rate(f.q[1].n, mP), rate(f.q[0].n, mB))};
    const double F[] = {rel(rate(v[2], mF), rate(v[1], mP)), dr(b[2], v[2]), (pb > 0 && rng > 0 && v[1] > 0) ? (counter / pb) / (v[1] / rng) : NAN,
      v[2] > 0 ? bigv[2] / v[2] : NAN, dr(bigb[2], bigv[2]), f.q[2].n ? f.q[2].imb / f.q[2].n : NAN, f.q[2].last_imb, f.q[2].last_micro, f.q[2].last_spr,
      f.q[2].n ? f.q[2].spr / f.q[2].n : NAN, rel(rate(f.q[2].n, mF), rate(f.q[1].n, mP)), v[2] > 0 ? extv / v[2] : NAN, (double)tests,
      pb > 0 ? std::fabs(poc - ext) / pb : NAN, (v[2] > 0 && pb > 0) ? d * (lastpx - fpv / v[2]) / pb : NAN, dr(lb, lv), rel(lv, rate(v[2], mF)),
      rel(ourn > 0 ? ourv / ourn : NAN, cn > 0 ? cv / cn : NAN), dr(g2b, g2v) - dr(g1b, g1v), (d * Pnet) > 0 ? -d * Fnet / (d * Pnet) : NAN, mF,
      (mF > 0 && mP > 0 && rng > 0) ? (pb / mF) / (rng / mP) : NAN};
    const double S[] = {(std::isfinite(sv) && rng > 0 && std::isfinite(lastpx)) ? d * (lastpx - sv) / rng : NAN,
      (std::isfinite(f.sess_v) && f.sess_v > 0) ? d * f.sess_d / f.sess_v : NAN};
    const double E[] = {dr(b[3], v[3]), rel(rate(v[3], mE), rate(v[2], mF)), f.q[3].n ? f.q[3].last_imb : f.q[2].last_imb, mE};
    for (double x : P) fprintf(fo, ",%.6g", x); for (double x : F) fprintf(fo, ",%.6g", x); for (double x : S) fprintf(fo, ",%.6g", x); for (double x : E) fprintf(fo, ",%.6g", x);
    { const double ex = f.fexec.count(ext) ? f.fexec[ext] : 0.0, dp = f.fdisp.count(ext) ? f.fdisp[ext] : 0.0;
      fprintf(fo, ",%.6g,%.6g", dp > 0 ? ex / dp : NAN, v[2] > 0 ? ex / v[2] : NAN); }
    fprintf(fo, "\n"); f.ticks.clear(); f.ticks.shrink_to_fit(); };
  int64_t first_ts = 0; long nt = 0, nq = 0, out = 0; double gbid = NAN, gask = NAN;
  auto on_quote = [&](const hist::Quote& q) {
    if (q.bid <= 0 || q.ask <= q.bid) return; nq++; gbid = q.bid; gask = q.ask;
    for (size_t i = lo; i < fl.size() && fl[i].tb <= q.ts_us; ++i) { Flag& f = fl[i]; if (done[i]) continue; const int s = seg(f, q.ts_us); if (s < 0) continue;
      const int tot = q.bid_sz + q.ask_sz; if (tot <= 0) continue; double im = (double)q.bid_sz / tot; if (f.side < 0) im = 1 - im;
      const double mid = (q.bid + q.ask) / 2, micro = (q.bid * q.ask_sz + q.ask * q.bid_sz) / tot, spr = (q.ask - q.bid) / 0.25;
      if (s == 2) { const double lvl = f.side > 0 ? q.bid : q.ask; const double sz = f.side > 0 ? q.bid_sz : q.ask_sz; double& m = f.fdisp[lvl]; m = std::max(m, sz); }
      QA& a = f.q[s]; a.n++; a.imb += im; a.spr += spr; a.last_imb = im; a.last_spr = spr; a.last_micro = f.side * (micro - mid) / 0.25; } };
  auto on_tick = [&](const hist::Tick& t) {
    nt++; if (!first_ts) first_ts = t.ts_us;
    while (oi < opens.size() && t.ts_us >= opens[oi]) { s_pv = s_v = s_d = 0; s_ok = true; oi++; }
    s_pv += t.price * t.size; s_v += t.size; s_d += t.is_buy ? t.size : -t.size;
    for (size_t i = lo; i < fl.size() && fl[i].tb <= t.ts_us; ++i) { Flag& f = fl[i]; if (done[i]) continue;
      if (!f.seen_base) { f.seen_base = true; f.first_ts = t.ts_us; }
      if (s_ok && t.ts_us >= f.tc && !std::isfinite(f.sess_v)) { f.sess_pv = s_pv; f.sess_v = s_v; f.sess_d = s_d; }
      if (t.ts_us >= f.tf_) { if (s_ok && !std::isfinite(f.sess_v)) { f.sess_pv = s_pv; f.sess_v = s_v; f.sess_d = s_d; } finish(f); done[i] = 1; out++; continue; }
      const int sg = seg(f, t.ts_us);
      if (sg == 2 && ((f.side > 0 && !t.is_buy && t.price == gbid) || (f.side < 0 && t.is_buy && t.price == gask))) f.fexec[t.price] += t.size;
      if (sg >= 0) f.ticks.push_back({t.ts_us, t.price, t.size, t.is_buy}); }
    while (lo < fl.size() && done[lo]) lo++; };
  while (feed.next_batch(ticks, quotes, 200000)) {
    size_t qi = 0;
    for (auto& t : ticks) { while (qi < quotes.size() && quotes[qi].ts_us <= t.ts_us) on_quote(quotes[qi++]); on_tick(t); }
    while (qi < quotes.size()) on_quote(quotes[qi++]);
    ticks.clear(); quotes.clear(); }
  fclose(fo);
  fprintf(stderr, "%s: ticks %ld quotes %ld flags written %ld of %zu (first tick %lld)\n", argv[2], nt, nq, out, fl.size(), (long long)first_ts);
  return 0;
}
