// 2026-10-19 (founder: "more tests" after flag_trail): the flags we would trade — NY bull AND bear flags WITH the day on a straight
// path (price on the day's side of the 09:30 open and VWAP, straightness >= 0.55), on the 2/3/4/5-min charts, one PRE-SET setting for
// every chart (pole 6 bars >= 0.20 ATR, flag 5 bars <= 0.6 x pole — the flag5_trend shape, not re-picked), close beyond the flag, stop
// beyond the flag + 0.25, 3R target HELD TO 15:55 (the best exit in flag_trail), entries 09:45–15:00, one position at a time, ≤ 2 a day.
// Matched control: on the same days, the same number of trades at RANDOM minutes where the same context holds, same direction rule,
// same stop distance as the paired flag trade, same exit. If the flag adds nothing, the control earns the same.
// Writes research/programs/inputs_20261019/{flagx_k,flagctl_k}.csv (_k format). Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; };
static double flagStop(const std::vector<FB>& b, int N, int F, double m, double tight, int D, double A) {
  int n = b.size(); if (n < N + F + 1 || !D) return NAN; int p0 = n - 1 - F - N, p1 = n - 2 - F;
  double pole = D * (b[p1].c - b[p0].o); if (pole < m * A) return NAN; double fh = -1e18, fl = 1e18;
  for (int k = n - 1 - F; k < n - 1; ++k) { fh = std::max(fh, b[k].h); fl = std::min(fl, b[k].l); }
  if (fh - fl > tight * pole) return NAN; double c = b[n - 1].c; if (D > 0 ? !(c > fh) : !(c < fl)) return NAN; return D > 0 ? fl - 0.25 : fh + 0.25; }
struct Tr { int date; long e, x; int dir; double g, mae, mfe, R, A; int tf; };
struct St { double n = 0, s = 0, q = 0, w = 0, big = 0; void add(double p, bool b3) { n++; s += p; q += p * p; if (p > 0) w++; if (b3) big++; }
  double m() const { return n ? s / n : NAN; } double t() const { if (n < 3) return NAN; double mu = m(), sd = std::sqrt(std::max(1e-12, q / n - mu * mu)); return mu / (sd / std::sqrt(n)); } };
static double RRT = 3.0; static bool TOCLOSE = true;   // argv: target R (default 3), "1" = hold to 15:55 else 120-min time exit
int main(int argc, char** argv) {
  if (argc > 1) RRT = atof(argv[1]); if (argc > 2) TOCLOSE = atoi(argv[2]) != 0;
  const std::string sfx = argc > 1 ? std::string("_") + argv[1] + (TOCLOSE ? "" : "t") : "";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(31); std::vector<Tr> fx, ct; const int TFS[4] = {5, 4, 3, 2};
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue;
    std::vector<signed char> D(I.size()); std::vector<char> ctx(I.size());
    { double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < I.size(); ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; int dd = c > so ? 1 : c < so ? -1 : 0; D[k] = dd; int tm = tday_min(s.t[i]);
        ctx[k] = dd && (c - vw) * dd > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55 && tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0); } }
    auto run = [&](size_t k0, int dir, double R, int tf, std::vector<Tr>& out) -> size_t {   // returns exit position
      double e = s.c[I[k0]], st = e - dir * R, tg = e + dir * RRT * R, mae = 0, mfe = 0, px = NAN; size_t k = k0;
      for (k = k0 + 1; k < I.size(); ++k) { int i = I[k]; mae = std::min(mae, dir * ((dir > 0 ? s.l[i] : s.h[i]) - e)); mfe = std::max(mfe, dir * ((dir > 0 ? s.h[i] : s.l[i]) - e));
        if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55) || (!TOCLOSE && s.t[i] >= s.t[I[k0]] + 7200)) { px = s.c[i]; break; } }
      if (k >= I.size()) { k = I.size() - 1; px = s.c[I[k]]; }
      out.push_back({d.date, (long)s.t[I[k0]], (long)s.t[I[k]], dir, dir * (px - e), mae, mfe, R, A, tf}); return k; };
    std::vector<FB> b[4]; size_t busy = 0; bool have = false; int nd = 0; std::vector<double> Rs;
    for (size_t k = 0; k < I.size(); ++k) { int i = I[k], tm = tday_min(s.t[i]);
      for (int t = 0; t < 4; ++t) { int tf = TFS[t]; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
        else { FB& x = b[t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } }
      if (!ctx[k] || (have && k <= busy) || nd >= 2) continue;
      for (int t = 0; t < 4; ++t) { int tf = TFS[t]; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; double sp = flagStop(b[t], 6, 5, 0.20, 0.6, D[k], A); if (std::isnan(sp)) continue;
        double R = D[k] * (s.c[i] - sp); if (!(R > 0.25) || R > 0.6 * A) continue; busy = run(k, D[k], R, tf, fx); have = true; nd++; Rs.push_back(R); break; } }
    // matched control: same count, random context minutes, same R
    std::vector<size_t> cand; for (size_t k = 0; k < I.size(); ++k) if (ctx[k]) cand.push_back(k);
    size_t after = 0; for (double R : Rs) { std::vector<size_t> ok; for (size_t k : cand) if (k > after) ok.push_back(k); if (ok.empty()) break;
      size_t k = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)]; after = run(k, D[k], R, 0, ct); } }
  for (int f = 0; f < 2; ++f) { FILE* o = fopen(((f ? std::string("research/programs/inputs_20261019/flagctl_k") : std::string("research/programs/inputs_20261019/flagx_k")) + sfx + ".csv").c_str(), "w");
    for (auto& t : f ? ct : fx) fprintf(o, "%d,%ld,%ld,%d,%.4f,%.4f,%.2f,%.2f,%d,%.5f\n", t.date, t.e, t.x, t.dir, t.g, t.g - 1, t.mae, t.mfe, t.tf, t.R / t.A); fclose(o); }
  printf("NY flags with the day on a straight path (2/3/4/5m, pole 6 >= 0.20 ATR, flag 5 <= 0.6), target %.1fR, %s, <= 2 a day — vs a matched random-timing control.\n"
         "pts = net NQ points per trade at today's range (2 pts cost); 3R column = share reaching the full target.\n\n", RRT, TOCLOSE ? "held to 15:55" : "120-min time exit");
  auto rep = [&](const char* lab, const std::vector<Tr>& v, auto keep) { St a; for (auto& t : v) if (keep(t)) a.add((t.g - 2.0 * t.A / 320) * 320 / t.A, t.g >= RRT * t.R - 0.01);
    printf("  %-34s n %5.0f (%4.1f/yr)  win %4.1f%%  3R %4.1f%%  pts %+6.2f  (t %+4.1f)\n", lab, a.n, a.n / 16.2, 100 * a.w / std::max(1.0, a.n), 100 * a.big / std::max(1.0, a.n), a.m(), a.t()); };
  for (int f = 0; f < 2; ++f) { const auto& v = f ? ct : fx; printf("%s\n", f ? "== CONTROL (same days, same stop, random timing, with the day)" : "== FLAGS");
    rep("all", v, [](const Tr&) { return true; }); rep("bull", v, [](const Tr& t) { return t.dir > 0; }); rep("bear", v, [](const Tr& t) { return t.dir < 0; });
    rep("2010-2018", v, [](const Tr& t) { return t.date < 20190101; }); rep("2019-2024", v, [](const Tr& t) { return t.date >= 20190101 && t.date < 20250101; }); rep("2025-2026", v, [](const Tr& t) { return t.date >= 20250101; });
    if (!f) for (int tf : {5, 4, 3, 2}) { char l[32]; snprintf(l, sizeof l, "%dm flags", tf); rep(l, v, [tf](const Tr& t) { return t.tf == tf; }); }
    if (!f) for (int tf : {5, 4, 3, 2}) { char l[40]; snprintf(l, sizeof l, "%dm bull", tf); rep(l, v, [tf](const Tr& t) { return t.tf == tf && t.dir > 0; }); }
    rep("bull 2010-2018", v, [](const Tr& t) { return t.dir > 0 && t.date < 20190101; }); rep("bull 2019-2024", v, [](const Tr& t) { return t.dir > 0 && t.date >= 20190101 && t.date < 20250101; });
    rep("bull 2025-2026", v, [](const Tr& t) { return t.dir > 0 && t.date >= 20250101; });
    if (!f) for (int tf : {5, 4, 3, 2}) for (int e = 0; e < 3; ++e) { const int lo = e == 0 ? 0 : e == 1 ? 20190101 : 20250101, hi = e == 0 ? 20190101 : e == 1 ? 20250101 : 99999999; char l[48];
      snprintf(l, sizeof l, "%dm bull %s", tf, e == 0 ? "2010-2018" : e == 1 ? "2019-2024" : "2025-2026"); rep(l, v, [tf, lo, hi](const Tr& t) { return t.tf == tf && t.dir > 0 && t.date >= lo && t.date < hi; }); } }
  return 0;
}
