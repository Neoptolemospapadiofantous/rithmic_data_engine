// 2026-10-19 NQ behaviour part 3 + order flow (research/preregistered/2026-10-19_behavior_part3.md): pullback depth, gap fills, sweeps of
// yesterday's high/low, round numbers, the afternoon, early volume, and order flow (aggressor delta, divergence, absorption, book
// imbalance) on 2023-03 → 2026. NQ 1m, RTH, roll windows excluded, ATR units. Read-only. argv[1] = cascade trades CSV (edge_lab --trades).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct St { double m = NAN, t = NAN; int n = 0; };
static St stat(const std::vector<double>& x) { St s; s.n = x.size(); if (s.n < 3) return s; double a = 0, b = 0; for (double v : x) { a += v; b += v * v; }
  s.m = a / s.n; double sd = std::sqrt(std::max(1e-12, b / s.n - s.m * s.m)); s.t = s.m / (sd / std::sqrt((double)s.n)); return s; }
struct Row { std::vector<double> all, e[3]; void add(int d, double v) { if (std::isnan(v)) return; all.push_back(v); e[era(d)].push_back(v); } };
static void line(const char* lab, const Row& r, const char* unit = "") {
  St a = stat(r.all), x = stat(r.e[0]), y = stat(r.e[1]), z = stat(r.e[2]);
  bool same = x.n > 2 && y.n > 2 && z.n > 2 && (x.m > 0) == (y.m > 0) && (y.m > 0) == (z.m > 0);
  printf("  %-58s n %5d mean %+8.3f%s t %+6.2f | eras %+8.3f %+8.3f %+8.3f (n %d/%d/%d)%s\n", lab, a.n, a.m, unit, a.t, x.m, y.m, z.m, x.n, y.n, z.n,
         std::fabs(a.t) >= 2 && same ? "  robust" : ""); }
// share line: values are 0/1
static void share(const char* lab, const Row& r) {
  auto p = [](const std::vector<double>& v) -> double { if (v.empty()) return NAN; double s = 0; for (double x : v) s += x; return 100.0 * s / v.size(); };
  printf("  %-58s %5.1f%% (n %5zu) | eras %5.1f%% %5.1f%% %5.1f%% (n %zu/%zu/%zu)\n", lab, p(r.all), r.all.size(), p(r.e[0]), p(r.e[1]), p(r.e[2]), r.e[0].size(), r.e[1].size(), r.e[2].size()); }
static double px_at(const Series& s, const Day& D, int h, int m) {
  int i = first_bar_at(s, D, hm_to_tday(h, m), D.b0); if (i >= D.b1) return s.c[D.b1 - 1]; return i > D.b0 ? s.c[i - 1] : s.o[i]; }
static int bar_at(const Series& s, const Day& D, int h, int m) { return first_bar_at(s, D, hm_to_tday(h, m), D.b0); }
static double corr(const std::vector<double>& a, const std::vector<double>& b) { int n = a.size(); if (n < 10) return NAN; double ma = 0, mb = 0;
  for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n; double sab = 0, saa = 0, sbb = 0;
  for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); } return sab / std::sqrt(saa * sbb); }

int main(int argc, char** argv) {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); size_t nb = load_book("data/edge_lab/cache", "NQ", s);
  std::vector<int> D; for (int k = 0; k < (int)s.days.size(); ++k) { const Day& d = s.days[k]; if (d.ok && !d.rollwin && d.atr > 0 && !std::isnan(d.pc) && d.date <= 20261016) D.push_back(k); }
  printf("NQ behaviour part 3 — %zu sessions %d → %d, ATR units. 'robust' = same sign in all eras (2010-18 / 2019-22 / 2023-26) and |t| >= 2. Book minutes: %zu\n",
         D.size(), s.days[D.front()].date, s.days[D.back()].date, nb);

  // ── 2. pullback depth
  printf("\n== 2a. Cascade trades: how far against before the result (MAE in R, R = 0.35 ATR)\n");
  std::map<int, double> atrOf; for (auto& d : s.days) if (d.atr > 0) atrOf[d.date] = d.atr;
  struct Tr { int date; int64_t e; int dir; double net, mae, mfe; };
  std::vector<Tr> tr;
  if (argc > 1) { FILE* f = fopen(argv[1], "r"); char ln[512]; if (f) { fgets(ln, sizeof ln, f); while (fgets(ln, sizeof ln, f)) { Tr t; long long e, x; double g; int src;
      if (sscanf(ln, "%d,%lld,%lld,%d,%lf,%lf,%lf,%lf,%d", &t.date, &e, &x, &t.dir, &g, &t.net, &t.mae, &t.mfe, &src) >= 8) { t.e = e; tr.push_back(t); } } fclose(f); } }
  { Row maeW, mae2R, deep5, deep75, maeL; for (auto& t : tr) { auto it = atrOf.find(t.date); if (it == atrOf.end()) continue; double R = 0.35 * it->second, m = -t.mae / R;
      if (t.net > 0) { maeW.add(t.date, m); deep5.add(t.date, m > 0.5); deep75.add(t.date, m > 0.75); } else maeL.add(t.date, m);
      if (t.mfe >= 2 * R - 1e-9 && t.net > 0) mae2R.add(t.date, m); }
    line("winners: MAE (R)", maeW); line("2R winners: MAE (R)", mae2R); line("losers: MAE (R)", maeL);
    share("winners that went more than 0.5R against first", deep5); share("winners that went more than 0.75R against first", deep75); }
  printf("\n== 2b. Trend days (|C-O| >= 0.5 range): deepest pullback after 10:00 against the day's direction (ATR), and when it ended\n");
  { Row pb; int hrs[7] = {0}; int nt = 0;
    for (int k : D) { const Day& d = s.days[k]; double r = d.rh - d.rl; if (r <= 0 || std::fabs(d.rc - d.ro) < 0.5 * r) continue; int dir = d.rc > d.ro ? 1 : -1;
      int i0 = bar_at(s, d, 10, 0); double ext = dir > 0 ? -1e18 : 1e18, best = 0; int when = i0;
      for (int i = d.rth0; i < d.rth1; ++i) { if (dir > 0) ext = std::max(ext, (double)s.h[i]); else ext = std::min(ext, (double)s.l[i]);
        if (i < i0) continue; double pbk = dir > 0 ? ext - s.l[i] : s.h[i] - ext; if (pbk > best) { best = pbk; when = i; } }
      pb.add(d.date, best / d.atr); int h = (tday_min(s.t[when]) - kRthOpen) / 60; hrs[std::min(std::max(h, 0), 6)]++; ++nt; }
    line("deepest pullback after 10:00 on trend days", pb, " atr");
    printf("  when it ended (hour from 09:30): "); for (int h = 0; h < 7; ++h) printf("%02d:30 %4.1f%%  ", 9 + h, 100.0 * hrs[h] / nt); printf("\n"); }

  // ── 3. gap fills
  printf("\n== 3. Gap fills (gap = 09:30 open - prior RTH close; filled = RTH touches the prior close)\n");
  { struct B { const char* n; double lo, hi; }; B bk[4] = {{"0.10-0.25 ATR", 0.1, 0.25}, {"0.25-0.50 ATR", 0.25, 0.5}, {"0.50-1.00 ATR", 0.5, 1}, {"> 1.00 ATR", 1, 99}};
    for (int up = 1; up >= 0; --up) for (auto& b : bk) for (int vx = 0; vx < 2; ++vx) {
      Row f, f10, f11; std::vector<double> mins;
      for (int k : D) { const Day& d = s.days[k]; double g = (d.ro - d.pc) / d.atr; if ((up && g < b.lo) || (up && g >= b.hi) || (!up && -g < b.lo) || (!up && -g >= b.hi)) continue;
        if (std::isnan(d.vix) || (vx == 0) != (d.vix < 20)) continue;
        int when = -1; for (int i = d.rth0; i < d.rth1; ++i) if (up ? s.l[i] <= d.pc : s.h[i] >= d.pc) { when = i; break; }
        f.add(d.date, when >= 0); int m = when >= 0 ? tday_min(s.t[when]) - kRthOpen : 999; f10.add(d.date, m < 30); f11.add(d.date, m < 90); if (when >= 0) mins.push_back(m); }
      std::sort(mins.begin(), mins.end()); char lb[96];
      snprintf(lb, sizeof lb, "gap %s %s, VIX %s: filled (median %d min)", up ? "UP" : "DOWN", b.n, vx ? ">=20" : "<20", mins.empty() ? -1 : (int)mins[mins.size() / 2]); share(lb, f);
      snprintf(lb, sizeof lb, "   ... filled by 10:00"); share(lb, f10); } }

  // ── 4. sweeps of yesterday's high / low
  printf("\n== 4. Yesterday's high / low taken in RTH (from an open inside): back through within 30 min = sweep, else hold\n");
  for (int side = 1; side >= -1; side -= 2) { Row sw, swClose, holdClose;
    for (int k : D) { const Day& d = s.days[k]; double L = side > 0 ? d.ph : d.pl; if (side > 0 ? d.ro >= L : d.ro <= L) continue;
      int i0 = -1; for (int i = d.rth0; i < d.rth1; ++i) if (side > 0 ? s.h[i] > L : s.l[i] < L) { i0 = i; break; } if (i0 < 0) continue;
      bool back = false; for (int i = i0 + 1; i < std::min(d.rth1, i0 + 31); ++i) if (side > 0 ? s.c[i] < L : s.c[i] > L) { back = true; break; }
      sw.add(d.date, back); bool beyond = side > 0 ? d.rc > L : d.rc < L; (back ? swClose : holdClose).add(d.date, beyond); }
    printf(" %s\n", side > 0 ? "yesterday's HIGH" : "yesterday's LOW");
    share("   taken, then back through within 30 min (sweep)", sw); share("   sweep: day still closes beyond the level", swClose); share("   hold (no close back within 30 min): closes beyond", holdClose); }

  // ── 5. round numbers
  printf("\n== 5. Round numbers: first RTH touch, next 30 min — through by 0.15 ATR first (vs rejected by 0.15 ATR first)\n");
  for (int off : {0, 50, 25}) { Row thr; for (int k : D) { const Day& d = s.days[k]; double A = d.atr, q = 0.15 * A; std::set<long> seen;
      for (int i = d.rth0 + 1; i < d.rth1; ++i) { long lo = (long)std::ceil((s.l[i] - off) / 100.0), hi = (long)std::floor((s.h[i] - off) / 100.0);
        for (long m = lo; m <= hi; ++m) { double L = m * 100.0 + off; if (seen.count(m)) continue; seen.insert(m); int from = s.c[i - 1] < L ? 1 : s.c[i - 1] > L ? -1 : 0; if (!from) continue;
          int res = 0; for (int j = i; j < std::min(d.rth1, i + 30) && !res; ++j) { double fav = from > 0 ? s.h[j] - L : L - s.l[j], adv = from > 0 ? L - s.l[j] : s.h[j] - L;
            if (fav >= q && adv >= q) res = s.c[j] > L ? (from > 0 ? 1 : -1) : (from > 0 ? -1 : 1); else if (fav >= q) res = 1; else if (adv >= q) res = -1; }
          if (res) thr.add(d.date, res > 0); } } }
    char lb[64]; snprintf(lb, sizeof lb, "levels at x%02d (every 100 pts): broke through", off); share(lb, thr); }

  // ── 6. afternoon
  printf("\n== 6. The afternoon after a trend morning (12:00 straight >= 0.55 and >= 0.4 ATR from the open), in the morning's direction\n");
  { Row a1, a2, rev; for (int k : D) { const Day& d = s.days[k]; int i12 = bar_at(s, d, 12, 0); double hi = -1e18, lo = 1e18;
      for (int i = d.rth0; i < i12; ++i) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); }
      double p12 = px_at(s, d, 12, 0), mv = p12 - d.ro; if (hi <= lo || std::fabs(mv) < 0.4 * d.atr || std::fabs(mv) / (hi - lo) < 0.55) continue; int dir = mv > 0 ? 1 : -1;
      double p14 = px_at(s, d, 14, 0), p1555 = px_at(s, d, 15, 55);
      a1.add(d.date, dir * (p14 - p12) / d.atr); a2.add(d.date, dir * (p1555 - p14) / d.atr); rev.add(d.date, dir * (p1555 - p12) <= -0.5 * std::fabs(mv)); }
    line("12:00 -> 14:00", a1, " atr"); line("14:00 -> 15:55", a2, " atr"); share("afternoon gives back >= half the morning move", rev); }

  // ── 7. early volume
  printf("\n== 7. Early volume: RVOL = 09:30-09:45 volume / mean of the prior 20 days; trend-day rate by RVOL fifth\n");
  { std::vector<std::pair<double, int>> rv; std::vector<int> rvd, strongs; std::vector<double> hist;
    for (int k : D) { const Day& d = s.days[k]; int i15 = bar_at(s, d, 9, 45); double v = 0; for (int i = d.rth0; i < i15; ++i) v += s.v[i];
      double r = d.rh - d.rl; int trend = r > 0 && std::fabs(d.rc - d.ro) >= 0.5 * r;
      if (hist.size() >= 20) { double m = 0; for (size_t j = hist.size() - 20; j < hist.size(); ++j) m += hist[j]; m /= 20;
        if (m > 0) { rv.push_back({v / m, trend}); rvd.push_back(d.date);
          int i30 = bar_at(s, d, 10, 30); double hi = -1e18, lo = 1e18; for (int i = d.rth0; i < i30; ++i) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); }
          double p = px_at(s, d, 10, 30); strongs.push_back(hi > lo && std::fabs(p - d.ro) / (hi - lo) >= 0.55 && std::fabs(p - d.ro) >= 0.25 * d.atr); } }
      hist.push_back(v); }
    std::vector<double> srt; for (auto& x : rv) srt.push_back(x.first); std::sort(srt.begin(), srt.end());
    for (int q = 0; q < 5; ++q) { double lo = srt[q * srt.size() / 5], hi = q == 4 ? 1e18 : srt[(q + 1) * srt.size() / 5]; Row t, ts;
      for (size_t i = 0; i < rv.size(); ++i) if (rv[i].first >= lo && rv[i].first < hi) { t.add(rvd[i], rv[i].second); if (strongs[i]) ts.add(rvd[i], rv[i].second); }
      char lb[96]; snprintf(lb, sizeof lb, "RVOL fifth %d (%.2f-%.2f): trend day", q + 1, lo, q == 4 ? srt.back() : hi); share(lb, t);
      snprintf(lb, sizeof lb, "   ... and 10:30 straight + moved: trend day"); share(lb, ts); } }

  // ── 8. order flow
  printf("\n== 8. Order flow, 2023-03 -> 2026 (aggressor delta = buy - sell volume; book imbalance = (bid - ask)/(bid + ask) within 10 bp)\n");
  { std::vector<double> dA, yA; Row agree, disagree, divR, nodivR, abs1, abs0, bkFirst, bkDay; std::vector<double> bkx, bky;
    for (int k : D) { const Day& d = s.days[k]; if (d.date < 20230301 || !d.flow) continue; double A = d.atr;
      int i10 = bar_at(s, d, 10, 0), i1030 = bar_at(s, d, 10, 30); double del = 0, vol = 0, cum10 = 0, cum1030 = 0, hiPre = -1e18, loPre = 1e18;
      for (int i = d.rth0; i < i1030 && i < d.rth1; ++i) { double dl = 2 * s.bv[i] - s.v[i]; if (i < i10) { del += dl; vol += s.v[i]; hiPre = std::max(hiPre, (double)s.h[i]); loPre = std::min(loPre, (double)s.l[i]); cum10 += dl; } cum1030 += dl; }
      if (vol <= 0) continue; double p10 = px_at(s, d, 10, 0), p1030 = px_at(s, d, 10, 30), p1555 = px_at(s, d, 15, 55);
      double dn = del / vol, y = (p1555 - p10) / A; dA.push_back(dn); yA.push_back(y);
      double mv = p10 - d.ro; if (std::fabs(mv) >= 0.1 * A) { int dir = mv > 0 ? 1 : -1; ((dn > 0) == (dir > 0) ? agree : disagree).add(d.date, dir * y); }
      // divergence at 10:30
      if (p1030 > hiPre) (cum1030 < cum10 ? divR : nodivR).add(d.date, -(p1555 - p1030) / A);   // + = reversal down after a new high
      if (p1030 < loPre) (cum1030 > cum10 ? divR : nodivR).add(d.date, (p1555 - p1030) / A);    // + = reversal up after a new low
      // absorption at PDH / PDL (first RTH touch from inside)
      for (int side = 1; side >= -1; side -= 2) { double L = side > 0 ? d.ph : d.pl; if (side > 0 ? d.ro >= L : d.ro <= L) continue;
        int it = -1; for (int i = d.rth0 + 20; i < d.rth1; ++i) if (side > 0 ? s.h[i] >= L : s.l[i] <= L) { it = i; break; } if (it < 0) continue;
        double mv20 = 0; for (int j = it - 20; j < it; ++j) mv20 += s.v[j]; mv20 /= 20; double dl = 2 * s.bv[it] - s.v[it];
        bool heavy = s.v[it] >= 2 * mv20 && side * dl > 0; bool through = false; for (int j = it; j < std::min(d.rth1, it + 15); ++j) if (side > 0 ? s.h[j] >= L + 0.1 * A : s.l[j] <= L - 0.1 * A) through = true;
        int res = 0; for (int j = it; j < std::min(d.rth1, it + 60) && !res; ++j) { if (side > 0 ? s.l[j] <= L - 0.25 * A : s.h[j] >= L + 0.25 * A) res = 1; else if (side > 0 ? s.h[j] >= L + 0.25 * A : s.l[j] <= L - 0.25 * A) res = -1; }
        if (!res) continue; ((heavy && !through) ? abs1 : abs0).add(d.date, res > 0); }
      // book imbalance just before the open
      if (!s.qimb.empty()) { double qs = 0; int qn = 0; for (int i = std::max(d.b0, d.rth0 - 5); i < d.rth0; ++i) if (!std::isnan(s.qimb[i])) { qs += s.qimb[i]; ++qn; }
        if (qn >= 3) { double q = qs / qn; bkx.push_back(q); bky.push_back((d.rc - d.ro) / A); bkFirst.add(d.date, (q > 0) == (p10 > d.ro)); bkDay.add(d.date, (q > 0) == (d.rc > d.ro)); } } }
    int hit = 0; for (size_t i = 0; i < dA.size(); ++i) hit += (dA[i] > 0) == (yA[i] > 0);
    printf("  a. delta 09:30-10:00 (share of volume) vs 10:00 -> 15:55 move: corr %+.3f, same direction %.1f%% (n %zu)\n", corr(dA, yA), 100.0 * hit / dA.size(), dA.size());
    line("b. 10:00 move >= 0.1 ATR, delta AGREES: rest of day in its direction", agree, " atr");
    line("   delta DISAGREES: rest of day in the move's direction", disagree, " atr");
    line("c. new high/low at 10:30 WITH delta divergence: reversal to 15:55", divR, " atr");
    line("   new high/low at 10:30 without divergence: reversal to 15:55", nodivR, " atr");
    share("d. PDH/PDL first touch, heavy aggression INTO it, not through: reversed 0.25 ATR first", abs1);
    share("   all other first touches: reversed 0.25 ATR first", abs0);
    printf("  e. book imbalance 09:25-09:30 vs the day (open -> close): corr %+.3f (n %zu)\n", corr(bkx, bky), bkx.size());
    share("   imbalance sign = direction of 09:30 -> 10:00", bkFirst); share("   imbalance sign = direction of the day", bkDay);
    // f. cascade trades by 09:50 delta
    Row fa, fd; for (auto& t : tr) { if (t.date < 20230301) continue; auto it = atrOf.find(t.date); if (it == atrOf.end()) continue;
      int k = -1; for (int j = 0; j < (int)s.days.size(); ++j) if (s.days[j].date == t.date) { k = j; break; } if (k < 0 || !s.days[k].flow) continue;
      const Day& d = s.days[k]; double dl = 0; for (int i = d.rth0; i < d.rth1 && s.t[i] < t.e; ++i) dl += 2 * s.bv[i] - s.v[i];
      ((dl > 0) == (t.dir > 0) ? fa : fd).add(t.date, t.net / (0.35 * it->second)); }
    line("f. cascade trades, delta since 09:30 AGREES with the trade: net R", fa); line("   delta DISAGREES: net R", fd); }
  return 0;
}
