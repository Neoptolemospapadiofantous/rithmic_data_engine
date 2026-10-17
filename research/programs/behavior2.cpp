// 2026-10-17 behaviour map, part 2: intraday structure, volatility, cross-market, news days (real BLS / FOMC dates).
// NQ 1m 2010-07 → 2026-10, RTH = 09:30-16:00 ET, roll windows excluded. Moves in ATR (14 prior RTH ranges) unless noted.
#include "research/bars.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;

static int era(int date) { return date < 20190101 ? 0 : date < 20230101 ? 1 : 2; }
struct St { double m = NAN, t = NAN, sd = NAN; int n = 0; };
static St stat(const std::vector<double>& x) {
  St s; s.n = x.size(); if (s.n < 3) return s; double a = 0, b = 0; for (double v : x) { a += v; b += v * v; }
  s.m = a / s.n; s.sd = std::sqrt(std::max(0.0, b / s.n - s.m * s.m)); s.t = s.m / (s.sd / std::sqrt((double)s.n)); return s; }
struct Row { std::vector<double> all, e[3]; void add(int date, double v) { if (std::isnan(v)) return; all.push_back(v); e[era(date)].push_back(v); } };
static void line(const char* label, const Row& r, const char* unit = "") {
  St a = stat(r.all), x = stat(r.e[0]), y = stat(r.e[1]), z = stat(r.e[2]);
  bool same = x.n > 2 && y.n > 2 && z.n > 2 && (x.m > 0) == (y.m > 0) && (y.m > 0) == (z.m > 0);
  printf("  %-46s n %5d  mean %+8.3f%s  t %+6.2f | eras %+8.3f %+8.3f %+8.3f%s\n", label, a.n, a.m, unit, a.t, x.m, y.m, z.m,
         std::fabs(a.t) >= 3 && same ? "  ***" : std::fabs(a.t) >= 2 && same ? "  *" : ""); }
static double corr(const std::vector<double>& a, const std::vector<double>& b) {
  int n = a.size(); if (n < 10) return NAN; double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
  return sab / std::sqrt(saa * sbb); }
struct Pair { std::vector<double> a[4], b[4]; void add(int date, double x, double y) { if (std::isnan(x) || std::isnan(y)) return; for (int e : {era(date), 3}) { a[e].push_back(x); b[e].push_back(y); } } };
static void cline(const char* label, const Pair& p) {
  double c = corr(p.a[3], p.b[3]);
  printf("  %-52s corr %+.3f (n %zu, t %+.1f) | eras %+.3f %+.3f %+.3f\n", label, c, p.a[3].size(), c * std::sqrt((double)p.a[3].size()), corr(p.a[0], p.b[0]), corr(p.a[1], p.b[1]), corr(p.a[2], p.b[2])); }
// price at ET hh:mm of day D = close of the last bar before it (open of the first bar if none before)
static double px_at(const Series& s, const Day& D, int h, int m) {
  int i = first_bar_at(s, D, hm_to_tday(h, m), D.b0); if (i >= D.b1) return s.c[D.b1 - 1]; return i > D.b0 ? s.c[i - 1] : s.o[i]; }
static double pct(std::vector<double> v, double q) { if (v.empty()) return NAN; std::sort(v.begin(), v.end()); return v[(size_t)std::min<double>(v.size() - 1, q * v.size())]; }

int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s, es; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err) || !load_raw("data/edge_lab/cache", "ES", es, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); build_days(es, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  std::map<int, int> esd; for (int k = 0; k < (int)es.days.size(); ++k) if (es.days[k].ok) esd[es.days[k].date] = k;
  std::vector<int> D; for (int k = 0; k < (int)s.days.size(); ++k) { const Day& d = s.days[k]; if (d.ok && !d.rollwin && d.atr > 0 && d.date <= 20261016) D.push_back(k); }
  printf("NQ behaviour map part 2 — %zu sessions %d → %d, ATR units unless noted. *** / * = |t| >= 3 / 2 and same sign in all eras (2010-18 / 2019-22 / 2023-26)\n",
         D.size(), s.days[D.front()].date, s.days[D.back()].date);

  // ───────────────────────── A. intraday structure
  printf("\n== A1. When the RTH high and low are made (share of days, 30-min bucket of the bar that sets it)\n");
  { int hb[13] = {0}, lb[13] = {0}, hb19[13] = {0}, lb19[13] = {0}, n = 0, n19 = 0;
    for (int k : D) { const Day& d = s.days[k]; int ih = d.rth0, il = d.rth0;
      for (int i = d.rth0; i < d.rth1; ++i) { if (s.h[i] > s.h[ih]) ih = i; if (s.l[i] < s.l[il]) il = i; }
      int bh = (tday_min(s.t[ih]) - kRthOpen) / 30, bl = (tday_min(s.t[il]) - kRthOpen) / 30; bh = std::min(bh, 12); bl = std::min(bl, 12);
      ++hb[bh]; ++lb[bl]; ++n; if (d.date >= 20190101) { ++hb19[bh]; ++lb19[bl]; ++n19; } }
    for (int b = 0; b < 13; ++b) { int m = 570 + b * 30; printf("  %02d:%02d  high %4.1f%%  low %4.1f%%   (2019+: %4.1f%% / %4.1f%%)\n", m / 60, m % 60, 100.0 * hb[b] / n, 100.0 * lb[b] / n, 100.0 * hb19[b] / n19, 100.0 * lb19[b] / n19); } }

  printf("\n== A2. Has the day's RTH extreme already been made? (share of days, by time)\n");
  for (auto [h, m] : std::vector<std::pair<int, int>>{{10, 0}, {10, 30}, {11, 0}, {12, 0}, {13, 0}, {14, 0}, {15, 0}}) {
    int both = 0, one = 0, n = 0; int tm = hm_to_tday(h, m);
    for (int k : D) { const Day& d = s.days[k]; double hi = -1e18, lo = 1e18, hi2 = -1e18, lo2 = 1e18;
      for (int i = d.rth0; i < d.rth1; ++i) { if (tday_min(s.t[i]) < tm) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); } else { hi2 = std::max(hi2, (double)s.h[i]); lo2 = std::min(lo2, (double)s.l[i]); } }
      bool hh = hi2 <= hi, ll = lo2 >= lo; ++n; both += hh && ll; one += hh || ll; }
    printf("  by %02d:%02d  at least one extreme final %4.1f%%   both final (range done) %4.1f%%\n", h, m, 100.0 * one / n, 100.0 * both / n); }

  printf("\n== A3. A trending morning (|10:30 - open| > k ATR): what happens next, in the direction of the move\n");
  for (double k : {0.3, 0.5, 0.8}) {
    Row rest, mfe, holds; int n = 0, held = 0, t1 = 0, t2 = 0, t3 = 0, st1 = 0;
    for (int k2 : D) { const Day& d = s.days[k2]; double p = px_at(s, d, 10, 30), mv = (p - d.ro) / d.atr; if (std::fabs(mv) <= k) continue;
      int dir = mv > 0 ? 1 : -1; double R = 0.25 * d.atr; ++n;
      double adv_ext = dir > 0 ? 1e18 : -1e18;   // morning's adverse extreme (low on an up morning)
      int i0 = first_bar_at(s, d, hm_to_tday(10, 30), d.b0);
      for (int i = d.rth0; i < i0; ++i) adv_ext = dir > 0 ? std::min(adv_ext, (double)s.l[i]) : std::max(adv_ext, (double)s.h[i]);
      double best = 0; bool broke = false; int hit = 0; bool stopped = false;
      int i1 = first_bar_at(s, d, hm_to_tday(15, 55), d.b0);
      for (int i = i0; i < i1; ++i) { double fav = dir * ((dir > 0 ? s.h[i] : s.l[i]) - p), adv = dir * ((dir > 0 ? s.l[i] : s.h[i]) - p);
        if (!stopped && adv <= -R) stopped = true;
        if (!stopped) { if (fav >= R) hit = std::max(hit, 1); if (fav >= 2 * R) hit = std::max(hit, 2); if (fav >= 3 * R) hit = 3; }
        best = std::max(best, fav); if (dir > 0 ? s.l[i] < adv_ext : s.h[i] > adv_ext) broke = true; }
      double cl = px_at(s, d, 15, 55);
      rest.add(d.date, dir * (cl - p) / d.atr); mfe.add(d.date, best / d.atr); holds.add(d.date, broke ? 0 : 1);
      held += !broke; t1 += hit >= 1; t2 += hit >= 2; t3 += hit >= 3; st1 += stopped && hit == 0; }
    char lb[64]; snprintf(lb, sizeof lb, "k %.1f (%d days): 10:30 -> 15:55 in its direction", k, n); line(lb, rest, " atr");
    snprintf(lb, sizeof lb, "k %.1f: best further move after 10:30", k); line(lb, mfe, " atr");
    printf("      morning's opposite extreme never broken %4.1f%% | from 10:30, R = 0.25 ATR stop: +1R first %4.1f%%  +2R %4.1f%%  +3R %4.1f%%  stopped before +1R %4.1f%%\n",
           100.0 * held / n, 100.0 * t1 / n, 100.0 * t2 / n, 100.0 * t3 / n, 100.0 * st1 / n); }

  printf("\n== A4. First hour range -> rest of day\n");
  { Pair p; int nb = 0, nbig = 0, ns = 0, nsbig = 0;
    for (int k : D) { const Day& d = s.days[k]; double hi = -1e18, lo = 1e18, hi2 = -1e18, lo2 = 1e18; int tm = hm_to_tday(10, 30);
      for (int i = d.rth0; i < d.rth1; ++i) if (tday_min(s.t[i]) < tm) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); } else { hi2 = std::max(hi2, (double)s.h[i]); lo2 = std::min(lo2, (double)s.l[i]); }
      double r1 = (hi - lo) / d.atr, r2 = (hi2 - lo2) / d.atr, day = (d.rh - d.rl) / d.atr; p.add(d.date, r1, r2);
      if (r1 > 0.7) { ++nb; nbig += day > 1.3; } else if (r1 < 0.4) { ++ns; nsbig += day > 1.3; } }
    cline("first-hour range -> 10:30-16:00 range", p);
    printf("  day range > 1.3 ATR: after a first hour > 0.7 ATR %4.1f%% (n %d), after < 0.4 ATR %4.1f%% (n %d)\n", 100.0 * nbig / nb, nb, 100.0 * nsbig / ns, ns); }

  // ───────────────────────── B. volatility
  printf("\n== B1. Volatility memory: does today's range predict tomorrow's?\n");
  { Pair p, p5; std::vector<double> lr; std::vector<int> dd;
    for (int k : D) { const Day& d = s.days[k]; lr.push_back(std::log((d.rh - d.rl) / d.atr)); dd.push_back(d.date); }
    for (size_t i = 1; i < lr.size(); ++i) p.add(dd[i], lr[i - 1], lr[i]);
    for (size_t i = 5; i + 5 <= lr.size(); i += 5) { double a = 0, b = 0; for (int j = 0; j < 5; ++j) { a += lr[i - 5 + j]; b += lr[i + j]; } p5.add(dd[i], a, b); }
    cline("log(range/ATR) today -> tomorrow", p); cline("5-day mean log range -> next 5 days", p5);
    int hv = 0, hv2 = 0, n = 0, base = 0;
    for (size_t i = 1; i < lr.size(); ++i) { bool a = std::exp(lr[i - 1]) > 1.5, b = std::exp(lr[i]) > 1.5; ++n; base += b; if (a) { ++hv; hv2 += b; } }
    printf("  P(range > 1.5 ATR) = %.1f%% unconditional, %.1f%% the day after one (n %d)\n", 100.0 * base / n, 100.0 * hv2 / hv, hv); }

  printf("\n== B2. What predicts today's RTH range (range / ATR, mean)\n");
  { std::map<std::string, Row> r;
    for (int k : D) { const Day& d = s.days[k]; double rg = (d.rh - d.rl) / d.atr;
      double onh = -1e18, onl = 1e18; for (int i = d.b0; i < d.rth0; ++i) { onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
      double onr = (onh - onl) / d.atr, g = std::isnan(d.pc) ? NAN : std::fabs(d.ro - d.pc) / d.atr;
      r["all days"].add(d.date, rg);
      r[onr > 0.8 ? "overnight range > 0.8 ATR" : onr < 0.4 ? "overnight range < 0.4 ATR" : "overnight range 0.4-0.8"].add(d.date, rg);
      if (!std::isnan(g)) r[g > 0.5 ? "|gap| > 0.5 ATR" : g < 0.15 ? "|gap| < 0.15 ATR" : "|gap| 0.15-0.5"].add(d.date, rg);
      if (!std::isnan(d.vixc1)) r[d.vixc1 > 0.10 ? "VIX rose > 10% yesterday" : d.vixc1 < -0.10 ? "VIX fell > 10% yesterday" : "VIX change within 10%"].add(d.date, rg);
      r[d.cpi ? "CPI day" : d.nfp ? "NFP day" : d.fomc ? "FOMC day" : d.ppi ? "PPI day" : "no CPI/NFP/FOMC/PPI"].add(d.date, rg);
      if (d.dow == 1) r["Monday"].add(d.date, rg); if (d.dow == 5) r["Friday"].add(d.date, rg); }
    for (auto& [k, v] : r) line(k.c_str(), v, " atr"); }

  printf("\n== B3. Tails: RTH open->close move in ATR\n");
  { std::vector<double> x[4]; for (int k : D) { const Day& d = s.days[k]; double v = (d.rc - d.ro) / d.atr; x[era(d.date)].push_back(v); x[3].push_back(v); }
    const char* nm[] = {"2010-18", "2019-22", "2023-26", "all"};
    for (int e = 0; e < 4; ++e) { double m2 = 0, m4 = 0, mu = stat(x[e]).m; int o1 = 0, o2 = 0; for (double v : x[e]) { m2 += (v - mu) * (v - mu); m4 += std::pow(v - mu, 4); o1 += std::fabs(v) > 1; o2 += std::fabs(v) > 2; }
      m2 /= x[e].size(); m4 /= x[e].size();
      printf("  %-8s sd %.2f  kurtosis %.1f (normal 3)  |move| > 1 ATR %4.1f%%  > 2 ATR %4.2f%%  1st pct %+.2f  99th pct %+.2f\n", nm[e], std::sqrt(m2), m4 / (m2 * m2), 100.0 * o1 / x[e].size(), 100.0 * o2 / x[e].size(), pct(x[e], 0.01), pct(x[e], 0.99)); } }

  // ───────────────────────── C. cross-market
  printf("\n== C1. NQ vs ES, 5-minute RTH returns: who leads?\n");
  { Pair same, es_nq, nq_es, sprd_m, sprd_nq;
    for (int k : D) { const Day& d = s.days[k]; auto it = esd.find(d.date); if (it == esd.end()) continue; const Day& e = es.days[it->second];
      std::vector<double> rn, re; for (int b = 0; b < 78; ++b) { int m0 = 570 + b * 5, m1 = m0 + 5;
        double a0 = px_at(s, d, m0 / 60, m0 % 60), a1 = px_at(s, d, m1 / 60, m1 % 60), b0 = px_at(es, e, m0 / 60, m0 % 60), b1 = px_at(es, e, m1 / 60, m1 % 60);
        rn.push_back(1e4 * std::log(a1 / a0)); re.push_back(1e4 * std::log(b1 / b0)); }
      for (int b = 0; b < 78; ++b) { same.add(d.date, rn[b], re[b]); if (b + 1 < 78) { es_nq.add(d.date, re[b], rn[b + 1]); nq_es.add(d.date, rn[b], re[b + 1]); } }
      // relative strength: NQ minus ES (bp) 09:30-10:30 → 10:30-16:00, and → NQ's own 10:30-16:00
      double s1 = 1e4 * (std::log(px_at(s, d, 10, 30) / d.ro) - std::log(px_at(es, e, 10, 30) / e.ro));
      double s2 = 1e4 * (std::log(d.rc / px_at(s, d, 10, 30)) - std::log(e.rc / px_at(es, e, 10, 30)));
      sprd_m.add(d.date, s1, s2); sprd_nq.add(d.date, s1, 1e4 * std::log(d.rc / px_at(s, d, 10, 30))); }
    cline("same 5 minutes (how tied they are)", same); cline("ES 5-min -> NQ next 5-min", es_nq); cline("NQ 5-min -> ES next 5-min", nq_es);
    cline("NQ-ES spread 09:30-10:30 -> spread 10:30-16:00", sprd_m); cline("NQ-ES spread 09:30-10:30 -> NQ 10:30-16:00", sprd_nq); }

  printf("\n== C2. Yesterday's macro moves -> today's NQ RTH (bp)\n");
  { std::map<std::string, Row> r;
    for (int k : D) { const Day& d = s.days[k]; double v = 1e4 * std::log(d.rc / d.ro);
      if (!std::isnan(d.y10c)) r[d.y10c > 7 ? "10y yield up > 7 bp" : d.y10c < -7 ? "10y yield down > 7 bp" : "10y yield within 7 bp"].add(d.date, v);
      if (!std::isnan(d.usdc)) r[d.usdc > 0.4 ? "dollar up > 0.4%" : d.usdc < -0.4 ? "dollar down > 0.4%" : "dollar within 0.4%"].add(d.date, v);
      if (!std::isnan(d.vixc1)) r[d.vixc1 > 0.10 ? "VIX up > 10%" : d.vixc1 < -0.10 ? "VIX down > 10%" : "VIX within 10%"].add(d.date, v);
      if (!std::isnan(d.vts)) r[d.vts > 1.0 ? "VIX term structure inverted (>1)" : "VIX term structure normal"].add(d.date, v); }
    for (auto& [k, v] : r) line(k.c_str(), v, " bp"); }

  // ───────────────────────── D. news days
  printf("\n== D1. News days (real BLS / FOMC dates): the 08:30 reaction and the session after it (bp unless noted)\n");
  auto ev = [&](const char* nm, std::function<bool(const Day&)> f) {
    Row rng, rth, on, pre, react; Pair cont, fol; int n = 0;
    for (int k : D) { const Day& d = s.days[k]; if (!f(d)) continue; ++n;
      double p0828 = px_at(s, d, 8, 29), p0845 = px_at(s, d, 8, 45), p10 = px_at(s, d, 10, 0), p1030 = px_at(s, d, 10, 30);
      rng.add(d.date, (d.rh - d.rl) / d.atr); rth.add(d.date, 1e4 * std::log(d.rc / d.ro));
      react.add(d.date, 1e4 * std::fabs(std::log(p0845 / p0828)));
      cont.add(d.date, 1e4 * std::log(p0845 / p0828), 1e4 * std::log(d.rc / d.ro));
      fol.add(d.date, 1e4 * std::log(p10 / d.ro), 1e4 * std::log(d.rc / p10));
      (void)p1030; }
    printf("  %s (%d days)\n", nm, n);
    line("   RTH range", rng, " atr"); line("   RTH open->close", rth, " bp"); line("   |08:29->08:45 move|", react, " bp");
    cline("   08:29->08:45 move -> RTH open->close", cont); cline("   09:30->10:00 move -> 10:00->16:00", fol); };
  ev("all days", [](const Day&) { return true; });
  ev("CPI 2010-2020", [](const Day& d) { return d.cpi && d.date < 20210101; });
  ev("CPI 2021+", [](const Day& d) { return d.cpi && d.date >= 20210101; });
  ev("NFP", [](const Day& d) { return d.nfp; });
  ev("PPI", [](const Day& d) { return d.ppi; });
  ev("no CPI / NFP / PPI / FOMC", [](const Day& d) { return !d.cpi && !d.nfp && !d.ppi && !d.fomc; });

  printf("\n== D2. FOMC days: before vs after the 14:00 decision\n");
  { Row pre, react, after, rngpm, rngpm_n; Pair fol; for (int k : D) { const Day& d = s.days[k];
      double p14 = px_at(s, d, 14, 0), p1430 = px_at(s, d, 14, 30), p1555 = px_at(s, d, 15, 55);
      double hi = -1e18, lo = 1e18; int i0 = first_bar_at(s, d, hm_to_tday(14, 0), d.b0); for (int i = i0; i < d.rth1; ++i) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); }
      if (!d.fomc) { rngpm_n.add(d.date, (hi - lo) / d.atr); continue; }
      pre.add(d.date, 1e4 * std::log(p14 / d.ro)); react.add(d.date, 1e4 * std::log(p1430 / p14)); after.add(d.date, 1e4 * std::log(p1555 / p1430));
      rngpm.add(d.date, (hi - lo) / d.atr); fol.add(d.date, 1e4 * std::log(p1430 / p14), 1e4 * std::log(p1555 / p1430)); }
    line("09:30 -> 14:00 (before)", pre, " bp"); line("14:00 -> 14:30 (reaction)", react, " bp"); line("14:30 -> 15:55 (after)", after, " bp");
    line("14:00-16:00 range, FOMC days", rngpm, " atr"); line("14:00-16:00 range, other days", rngpm_n, " atr");
    cline("14:00->14:30 reaction -> 14:30->15:55", fol); }

  printf("\n== D3. The strong-open follow-through (A3 k 0.3) on news days vs other days\n");
  { std::map<std::string, Row> r;
    for (int k : D) { const Day& d = s.days[k]; double p = px_at(s, d, 10, 30), mv = (p - d.ro) / d.atr; if (std::fabs(mv) <= 0.3) continue;
      double v = (mv > 0 ? 1 : -1) * (px_at(s, d, 15, 55) - p) / d.atr;
      r[d.cpi ? (d.date >= 20210101 ? "CPI 2021+" : "CPI 2010-20") : d.nfp ? "NFP" : d.fomc ? "FOMC" : d.ppi ? "PPI" : "no news"].add(d.date, v); }
    for (auto& [k, v] : r) line(k.c_str(), v, " atr"); }
}
