// 2026-10-19 behaviour per session (research/preregistered/2026-10-19_behavior_per_session.md): VWAP, straightness → trend session,
// pullback depth, sweeps of the prior session's high/low, round numbers, second half, early volume and order flow — the same tests for
// Asia (18:00-03:00), London (03:00-09:30) and NY (09:30-16:00) ET, each with its own anchored VWAP and its own 14-session ATR. Read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <map>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Row { std::vector<double> all, e[3]; void add(int d, double v) { if (std::isnan(v)) return; all.push_back(v); e[era(d)].push_back(v); } };
static double mean(const std::vector<double>& v) { if (v.empty()) return NAN; double s = 0; for (double x : v) s += x; return s / v.size(); }
static void pct(const char* lab, const Row& r) { printf("    %-56s %5.1f%% | eras %5.1f %5.1f %5.1f %% (n %zu/%zu/%zu)\n", lab, 100 * mean(r.all), 100 * mean(r.e[0]), 100 * mean(r.e[1]), 100 * mean(r.e[2]), r.e[0].size(), r.e[1].size(), r.e[2].size()); }
static void avg(const char* lab, const Row& r) { printf("    %-56s %+7.3f | eras %+7.3f %+7.3f %+7.3f (n %zu/%zu/%zu)\n", lab, mean(r.all), mean(r.e[0]), mean(r.e[1]), mean(r.e[2]), r.e[0].size(), r.e[1].size(), r.e[2].size()); }
static double corr(const std::vector<double>& a, const std::vector<double>& b) { int n = a.size(); if (n < 10) return NAN; double ma = 0, mb = 0;
  for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n; double sab = 0, saa = 0, sbb = 0;
  for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); } return sab / std::sqrt(saa * sbb); }

struct Sess { int date; int i0, i1; double o, h, l, c, vol15; bool flow; };

int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int W[3][2] = {{0, 540}, {540, 930}, {930, 1320}};   // trading-day minutes (18:00 = 0)
  const char* SN[3] = {"ASIA 18:00-03:00", "LONDON 03:00-09:30", "NY 09:30-16:00"};
  // sessions in time order: [day][session]
  std::vector<std::array<Sess, 3>> S; std::vector<std::array<bool, 3>> ok;
  for (auto& d : s.days) { if (d.rollwin || d.date > 20261016) continue; std::array<Sess, 3> a; std::array<bool, 3> g{};
    for (int k = 0; k < 3; ++k) { Sess x{d.date, -1, -1, NAN, -1e18, 1e18, NAN, 0, false};
      for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < W[k][0] || tm >= W[k][1]) continue;
        if (x.i0 < 0) { x.i0 = i; x.o = s.o[i]; } x.i1 = i + 1; x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i];
        if (tm < W[k][0] + 15) x.vol15 += s.v[i]; if (s.bv[i] > 0) x.flow = true; }
      a[k] = x; g[k] = x.i0 >= 0 && (x.i1 - x.i0) >= (W[k][1] - W[k][0]) * 0.8; }
    S.push_back(a); ok.push_back(g); }
  printf("NQ behaviour per session — %zu trading days. Session ATR = mean range of its previous 14 sessions. Eras 2010-18 / 2019-22 / 2023-26.\n", S.size());

  for (int k = 0; k < 3; ++k) {
    printf("\n================ %s ================\n", SN[k]);
    const int len = W[k][1] - W[k][0], half = len / 2;
    std::vector<double> rng, vhist; int nv = 0;
    Row cross, oneSide, vT[2][3], vSame[2][3], vMove[2][3]; int vN[2][3][3] = {};
    Row stTrend[3], stSide[3], pb, swp, swpBeyond, holdBeyond, rnd[3], h2mv, h2back, rvT[5]; std::vector<double> rvAll; std::vector<std::pair<int, int>> rvRows;
    std::vector<double> ofx, ofy; Row ofAgree, ofDis;
    for (size_t di = 0; di < S.size(); ++di) {
      if (!ok[di][k]) continue; const Sess& x = S[di][k]; double A = NAN;
      if (rng.size() >= 14) { A = 0; for (size_t j = rng.size() - 14; j < rng.size(); ++j) A += rng[j]; A /= 14; }
      double vm = NAN; if (vhist.size() >= 20) { vm = 0; for (size_t j = vhist.size() - 20; j < vhist.size(); ++j) vm += vhist[j]; vm /= 20; }
      rng.push_back(x.h - x.l); vhist.push_back(x.vol15);
      if (!(A > 0) || x.h <= x.l) continue; const int date = x.date;
      // walk the session once: VWAP, path
      double pv = 0, vv = 0, hi = -1e18, lo = 1e18; int crosses = 0, lastSide = 0, sideAfter1h = 0; bool flip1h = false;
      std::vector<double> vw(x.i1 - x.i0), hiSo(x.i1 - x.i0), loSo(x.i1 - x.i0);
      for (int i = x.i0; i < x.i1; ++i) { int j = i - x.i0; double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i]; vw[j] = vv > 0 ? pv / vv : s.c[i];
        hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); hiSo[j] = hi; loSo[j] = lo;
        if ((j + 1) % 5 == 0) { int sd = s.c[i] > vw[j] ? 1 : -1; if (lastSide && sd != lastSide) ++crosses; lastSide = sd;
          if (j >= 60) { if (sideAfter1h && sd != sideAfter1h) flip1h = true; sideAfter1h = sd; } } }
      cross.add(date, crosses); oneSide.add(date, !flip1h);
      int n = x.i1 - x.i0; auto P = [&](int j) { return (double)s.c[x.i0 + std::min(j, n - 1)]; };
      // 1. VWAP revert/extend at 1 h and mid-session
      for (int t = 0; t < 2; ++t) { int j = t == 0 ? 59 : half - 1; if (j >= n - 1) continue; double dist = (P(j) - vw[j]) / A; int dir = dist > 0 ? 1 : -1; double ad = std::fabs(dist);
        int b = ad < 0.1 ? 0 : ad < 0.25 ? 1 : 2; bool touch = false;
        for (int i = x.i0 + j + 1; i < x.i1; ++i) if (dir > 0 ? s.l[i] <= vw[i - x.i0] : s.h[i] >= vw[i - x.i0]) { touch = true; break; }
        vT[t][b].add(date, touch); vSame[t][b].add(date, dir * (x.c - vw[n - 1]) > 0); vMove[t][b].add(date, dir * (x.c - P(j)) / A); vN[t][b][era(date)]++; }
      // 2. straightness at 1 h
      { int j = 59; double p = P(j), r1 = hiSo[j] - loSo[j]; if (r1 > 0) { double st = std::fabs(p - x.o) / r1, mv = std::fabs(p - x.o) / A;
          int g = (st >= 0.55 && mv >= 0.25) ? 0 : st < 0.30 ? 2 : 1; bool trend = std::fabs(x.c - x.o) >= 0.5 * (x.h - x.l);
          stTrend[g].add(date, trend); stSide[g].add(date, (x.c - x.o) * (p - x.o) > 0); } }
      // 3. pullback on trend sessions after the first hour
      if (std::fabs(x.c - x.o) >= 0.5 * (x.h - x.l)) { int dir = x.c > x.o ? 1 : -1; double ext = dir > 0 ? -1e18 : 1e18, best = 0;
        for (int i = x.i0; i < x.i1; ++i) { ext = dir > 0 ? std::max(ext, (double)s.h[i]) : std::min(ext, (double)s.l[i]); if (i - x.i0 < 60) continue;
          best = std::max(best, dir > 0 ? ext - s.l[i] : s.h[i] - ext); } pb.add(date, best / A); }
      // 4. prior session's high / low (the session just before, in time)
      { const Sess* pr = nullptr; if (k > 0 && ok[di][k - 1]) pr = &S[di][k - 1]; else if (k == 0 && di > 0 && ok[di - 1][2]) pr = &S[di - 1][2];
        if (pr) for (int side = 1; side >= -1; side -= 2) { double L = side > 0 ? pr->h : pr->l; if (side > 0 ? x.o >= L : x.o <= L) continue;
          int it = -1; for (int i = x.i0; i < x.i1; ++i) if (side > 0 ? s.h[i] > L : s.l[i] < L) { it = i; break; } if (it < 0) continue;
          bool back = false; for (int i = it + 1; i < std::min(x.i1, it + 31); ++i) if (side > 0 ? s.c[i] < L : s.c[i] > L) { back = true; break; }
          swp.add(date, back); bool beyond = side > 0 ? x.c > L : x.c < L; (back ? swpBeyond : holdBeyond).add(date, beyond); } }
      // 5. round numbers
      for (int oi = 0; oi < 3; ++oi) { int off = oi == 0 ? 0 : oi == 1 ? 50 : 25; double q = 0.15 * A; std::set<long> seen;
        for (int i = x.i0 + 1; i < x.i1; ++i) { long a = (long)std::ceil((s.l[i] - off) / 100.0), b = (long)std::floor((s.h[i] - off) / 100.0);
          for (long m = a; m <= b; ++m) { if (seen.count(m)) continue; seen.insert(m); double L = m * 100.0 + off; int from = s.c[i - 1] < L ? 1 : s.c[i - 1] > L ? -1 : 0; if (!from) continue;
            int res = 0; for (int j = i; j < std::min(x.i1, i + 30) && !res; ++j) { double fav = from > 0 ? s.h[j] - L : L - s.l[j], adv = from > 0 ? L - s.l[j] : s.h[j] - L;
              if (fav >= q && adv < q) res = 1; else if (adv >= q && fav < q) res = -1; else if (fav >= q && adv >= q) res = (from > 0) == (s.c[j] > L) ? 1 : -1; }
            if (res) rnd[oi].add(date, res > 0); } } }
      // 6. second half after a trend first half
      { int j = half - 1; double p = P(j), r1 = hiSo[j] - loSo[j], mv = p - x.o;
        if (r1 > 0 && std::fabs(mv) >= 0.4 * A && std::fabs(mv) / r1 >= 0.55) { int dir = mv > 0 ? 1 : -1; h2mv.add(date, dir * (x.c - p) / A); h2back.add(date, dir * (x.c - p) <= -0.5 * std::fabs(mv)); } }
      // 7. early volume
      if (vm > 0) { rvAll.push_back(x.vol15 / vm); rvRows.push_back({date, std::fabs(x.c - x.o) >= 0.5 * (x.h - x.l)}); }
      // 8. order flow (2023-03+)
      if (date >= 20230301 && x.flow && n > 60) { double dl = 0, vo = 0; for (int i = x.i0; i < x.i0 + 30; ++i) { dl += 2 * s.bv[i] - s.v[i]; vo += s.v[i]; }
        if (vo > 0) { double dn = dl / vo, p30 = P(29), y = (x.c - p30) / A; ofx.push_back(dn); ofy.push_back(y);
          double mv = p30 - x.o; if (std::fabs(mv) >= 0.1 * A) { int dir = mv > 0 ? 1 : -1; ((dn > 0) == (dir > 0) ? ofAgree : ofDis).add(date, dir * y); } } }
    }
    printf("  1. VWAP (anchored at the session open)\n"); avg("5-min closes crossing VWAP per session", cross); pct("no cross after the first hour (one side)", oneSide);
    const char* BN[3] = {"< 0.10 ATR", "0.10-0.25 ATR", ">= 0.25 ATR"};
    for (int t = 0; t < 2; ++t) for (int b = 0; b < 3; ++b) { char l[96];
      snprintf(l, sizeof l, "at %s, %s from VWAP: touches VWAP again", t ? "mid-session" : "1 h", BN[b]); pct(l, vT[t][b]);
      snprintf(l, sizeof l, "   ... closes on the same side of VWAP"); pct(l, vSame[t][b]);
      snprintf(l, sizeof l, "   ... move to the close away from VWAP (ATR)"); avg(l, vMove[t][b]); }
    printf("  2. Straightness at 1 h -> the session\n"); const char* GN[3] = {"straight + moved >= 0.25", "middle", "choppy < 0.30"};
    for (int g = 0; g < 3; ++g) { char l[96]; snprintf(l, sizeof l, "%s: trend session", GN[g]); pct(l, stTrend[g]); snprintf(l, sizeof l, "   ... closes on the first hour's side"); pct(l, stSide[g]); }
    printf("  3. Trend sessions\n"); avg("deepest pullback after the first hour (session ATR)", pb);
    printf("  4. Prior session's high / low taken from inside\n"); pct("back through within 30 min (sweep)", swp); pct("   after a sweep: closes beyond", swpBeyond); pct("   after a 30-min hold: closes beyond", holdBeyond);
    printf("  5. Round numbers: broke through 0.15 ATR first (x00 / x50 / x25)\n"); pct("x00", rnd[0]); pct("x50", rnd[1]); pct("x25", rnd[2]);
    printf("  6. Second half after a trend first half\n"); avg("move to the close in that direction (ATR)", h2mv); pct("gives back >= half the first half's move", h2back);
    printf("  7. Early volume (first 15 min vs the previous 20 sessions): trend-session rate by fifth\n");
    { std::vector<double> srt = rvAll; std::sort(srt.begin(), srt.end());
      for (int q = 0; q < 5; ++q) { double a = srt[q * srt.size() / 5], b = q == 4 ? 1e18 : srt[(q + 1) * srt.size() / 5]; Row t;
        for (size_t i = 0; i < rvAll.size(); ++i) if (rvAll[i] >= a && rvAll[i] < b) t.add(rvRows[i].first, rvRows[i].second); char l[64]; snprintf(l, sizeof l, "RVOL fifth %d", q + 1); pct(l, t); } }
    printf("  8. Order flow 2023-03+: first-30-min delta vs the rest of the session: corr %+.3f (n %zu)\n", corr(ofx, ofy), ofx.size());
    avg("first-30 move >= 0.1 ATR, delta AGREES: rest in its direction (ATR)", ofAgree); avg("   delta DISAGREES (ATR)", ofDis);
  }
  return 0;
}
