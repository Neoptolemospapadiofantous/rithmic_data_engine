// 2026-10-17 regime optimisation, walk-forward (research/preregistered/2026-10-17_regime_optimize.md).
//   regopt [DATA_DIR] [SHUFFLES]
#include "research/bars.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <numeric>
#include <random>
using namespace lab;

constexpr int NF = 13, NB = 3, NO = 16, Y0 = 2010, NY_ = 17;   // years 2010..2026
const char* FN[NF + 1] = {"acr30", "acr60", "acr120", "er5_30", "er5_60", "sess_er", "move_atr", "range_atr", "vwap_side",
                          "flow", "gap_agree", "vix", "on_range", "ANY"};
const char* BN[3] = {"low", "mid", "high"};
struct Sess { const char* name; int open, flat; std::vector<int> T; double stopk; };
struct Sample { int year, ti; float f[NF]; float o[NO]; };
struct Acc { double n = 0, s = 0, ss = 0; void add(double x) { n += 1; s += x; ss += x * x; } void add(const Acc& a) { n += a.n; s += a.s; ss += a.ss; }
  double t() const { if (n < 2) return -1e9; double m = s / n, v = ss / n - m * m; return v > 0 ? m / std::sqrt(v) * std::sqrt(n) : -1e9; } };

static double px_at(const Series& s, const Day& D, int tm) { int i = first_bar_at(s, D, tm, D.b0); if (i >= D.b1) return s.c[D.b1 - 1]; return i > D.b0 ? s.c[i - 1] : s.o[i]; }
static int hm(int h, int m) { return hm_to_tday(h, m); }
static std::string tstr(int tm) { int a = (tm + 1080) % 1440; char b[8]; snprintf(b, 8, "%02d:%02d", a / 60, a % 60); return b; }

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "data/edge_lab/cache"; const int SH = argc > 2 ? atoi(argv[2]) : 200;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw(dir, "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Sess> S = {{"New York", hm(9, 30), hm(15, 55), {hm(10, 0), hm(10, 30), hm(11, 0), hm(11, 30), hm(12, 0), hm(13, 0)}, 0.25},
                         {"London", hm(3, 0), hm(8, 0), {hm(4, 0), hm(5, 0), hm(6, 0), hm(7, 0)}, 0.15},
                         {"Asia", hm(18, 0), hm(2, 30), {hm(20, 0), hm(21, 0), hm(22, 0), hm(23, 0)}, 0.15}};
  for (size_t k = 0; k < S.size(); ++k) {
    const Sess& se = S[k]; const int NT = se.T.size(); std::vector<Sample> X;
    for (auto& D : s.days) {
      if (!D.ok || D.rollwin || D.date > 20261016 || !(D.atr > 0)) continue;
      const int i0 = first_bar_at(s, D, se.open, D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) > se.open + 5) continue;
      const double op = s.o[i0], atr = D.atr;
      for (int ti = 0; ti < NT; ++ti) { const int T = se.T[ti];
        const int iT = first_bar_at(s, D, T, i0); if (iT >= D.b1 || tday_min(s.t[iT]) > T + 2 || iT - i0 < 20) continue;
        const double p = s.c[iT - 1]; const int dir = p > op ? 1 : p < op ? -1 : 0; if (!dir) continue;
        Sample x; x.year = D.date / 10000; x.ti = ti; std::fill(x.f, x.f + NF, NAN);
        auto acr = [&](int n) -> float { if (T - se.open < n) return NAN; double num = 0, den = 0, prev = NAN;
          for (int j = std::max(i0 + 1, iT - n); j < iT; ++j) { double r = s.c[j] - s.c[j - 1]; den += r * r; if (!std::isnan(prev)) num += prev * r; prev = r; } return den > 0 ? num / den : NAN; };
        auto er5 = [&](int n) -> float { if (T - se.open < n) return NAN; double path = 0; for (int m = T - n + 5; m <= T; m += 5) path += std::fabs(px_at(s, D, m) - px_at(s, D, m - 5));
          return path > 0 ? std::fabs(p - px_at(s, D, T - n)) / path : NAN; };
        x.f[0] = acr(30); x.f[1] = acr(60); x.f[2] = acr(120); x.f[3] = er5(30); x.f[4] = er5(60);
        double path = 0, hi = -1e18, lo = 1e18, pv = 0, vv = 0, del = 0, vol = 0; int same = 0, tot = 0; std::vector<double> vw;
        for (int j = i0; j < iT; ++j) { if (j > i0) path += std::fabs(s.c[j] - s.c[j - 1]); hi = std::max(hi, (double)s.h[j]); lo = std::min(lo, (double)s.l[j]);
          const double tp = (s.h[j] + s.l[j] + s.c[j]) / 3.0, w = std::max(1.0f, s.v[j]); pv += tp * w; vv += w; vw.push_back(pv / vv); del += 2.0 * s.bv[j] - s.v[j]; vol += s.v[j]; }
        const double vwT = pv / vv; const int side = p >= vwT ? 1 : -1;
        for (int j = i0; j < iT; ++j) { ++tot; same += ((s.c[j] >= vw[j - i0]) ? 1 : -1) == side; }
        x.f[5] = path > 0 ? std::fabs(p - op) / path : NAN; x.f[6] = std::fabs(p - op) / atr; x.f[7] = (hi - lo) / atr; x.f[8] = (double)same / tot;
        x.f[9] = (D.flow && D.date >= 20230301 && vol > 0) ? dir * del / vol : NAN;
        x.f[10] = std::isnan(D.pc) ? NAN : dir * (op - D.pc) / atr; x.f[11] = D.vix;
        if (k == 0) { int a = first_bar_at(s, D, 0, D.b0); double h2 = -1e18, l2 = 1e18; for (int j = a; j < i0; ++j) { h2 = std::max(h2, (double)s.h[j]); l2 = std::min(l2, (double)s.l[j]); } if (i0 - a > 100) x.f[12] = (h2 - l2) / atr; }
        const int Hs[4] = {30, 60, 120, 100000};
        for (int e = 0; e < 4; ++e) { const int E = std::min(T + Hs[e], se.flat); const double g = dir * (px_at(s, D, E) - p);
          x.o[e] = g - 1.0; x.o[8 + e] = -g - 1.0;
          for (int a = 0; a < 2; ++a) { const int sd = a == 0 ? dir : -dir; const double st = se.stopk * atr; double r = NAN;
            for (int j = iT; j < D.b1 && tday_min(s.t[j]) < E; ++j) { if (sd * (sd > 0 ? s.l[j] - p : s.h[j] - p) <= -st) { r = -st; break; } if (sd * (sd > 0 ? s.h[j] - p : s.l[j] - p) >= 2 * st) { r = 2 * st; break; } }
            if (std::isnan(r)) r = sd * (px_at(s, D, E) - p); x.o[a * 8 + 4 + e] = r - 1.0; } }
        X.push_back(x); } }
    // tercile cut points per (year, ti, f): from years Y-2, Y-1 (2025+ held back: from 2023-24)
    std::vector<std::array<float, 2>> cut(NY_ * NT * NF, {NAN, NAN});
    for (int y = Y0; y < Y0 + NY_; ++y) { const int ya = y >= 2025 ? 2023 : y - 2, yb = y >= 2025 ? 2024 : y - 1;
      for (int ti = 0; ti < NT; ++ti) for (int f = 0; f < NF; ++f) { std::vector<float> v; for (auto& x : X) if (x.ti == ti && x.year >= ya && x.year <= yb && !std::isnan(x.f[f])) v.push_back(x.f[f]);
        if (v.size() < 100) continue; std::sort(v.begin(), v.end()); cut[((y - Y0) * NT + ti) * NF + f] = {v[v.size() / 3], v[2 * v.size() / 3]}; } }
    const int NR = NT * (NF + 1) * NB * NO;
    auto rid = [&](int ti, int f, int b, int o) { return ((ti * (NF + 1) + f) * NB + b) * NO + o; };
    auto accumulate = [&](const std::vector<Sample>& Z, std::vector<Acc>& A) { A.assign(NY_ * NR, Acc());
      for (auto& x : Z) { Acc* a = &A[(x.year - Y0) * NR];
        for (int o = 0; o < NO; ++o) a[rid(x.ti, NF, 0, o)].add(x.o[o]);
        for (int f = 0; f < NF; ++f) { if (std::isnan(x.f[f])) continue; auto c = cut[((x.year - Y0) * NT + x.ti) * NF + f]; if (std::isnan(c[0])) continue;
          const int b = x.f[f] < c[0] ? 0 : x.f[f] < c[1] ? 1 : 2; for (int o = 0; o < NO; ++o) a[rid(x.ti, f, b, o)].add(x.o[o]); } } };
    auto name = [&](int r) { int o = r % NO, b = (r / NO) % NB, f = (r / NO / NB) % (NF + 1), ti = r / NO / NB / (NF + 1);
      char buf[160]; snprintf(buf, 160, "%s %s%s%s %s %s%s", tstr(se.T[ti]).c_str(), FN[f], f == NF ? "" : "=", f == NF ? "" : BN[b], o < 8 ? "FOLLOW" : "FADE",
                                (o % 4) == 3 ? "to flat" : ((o % 4) == 0 ? "30m" : (o % 4) == 1 ? "60m" : "120m"), (o % 8) >= 4 ? " +stop/2R" : ""); return std::string(buf); };
    struct WF { Acc all, era[3]; std::vector<int> picks; };
    auto walk = [&](const std::vector<Acc>& A, bool cond, int topk, int y0 = 2013, int y1 = 2024, int train_end = 2100) { WF w;
      for (int y = y0; y <= y1; ++y) { std::vector<Acc> cum(NR); for (int yy = 2011; yy < y && yy <= train_end; ++yy) for (int r = 0; r < NR; ++r) cum[r].add(A[(yy - Y0) * NR + r]);
        std::vector<std::pair<double, int>> sc; for (int r = 0; r < NR; ++r) { int f = (r / NO / NB) % (NF + 1); if ((f == NF) == cond) continue; if ((f == NF) && (r / NO) % NB) continue; if (cum[r].n < 100) continue; sc.push_back({cum[r].t(), r}); }
        std::partial_sort(sc.begin(), sc.begin() + std::min<size_t>(topk, sc.size()), sc.end(), std::greater<>());
        for (int q = 0; q < topk && q < (int)sc.size(); ++q) { const Acc& a = A[(y - Y0) * NR + sc[q].second]; w.all.add(a); w.era[y <= 2018 ? 0 : y <= 2022 ? 1 : 2].add(a); if (q == 0) w.picks.push_back(sc[q].second); } }
      return w; };
    std::vector<Acc> A; accumulate(X, A);
    printf("\n######## %s — %zu samples (%d decision times) — %d rules\n", se.name, X.size(), NT, NR);
    auto line = [](const char* nm, const Acc& a) { printf("  %-34s %+6.2f pts/trade (n %5.0f)", nm, a.n ? a.s / a.n : NAN, a.n); };
    for (int topk : {1, 10}) {
      WF wc = walk(A, true, topk), wu = walk(A, false, topk);
      // shuffle null: readings permuted across days inside each (year, decision time)
      std::mt19937 rng(12345 + k * 7 + topk); std::vector<double> null;
      for (int r = 0; r < SH; ++r) { std::vector<Sample> Z = X; std::vector<std::vector<int>> grp(NY_ * NT); for (size_t i = 0; i < Z.size(); ++i) grp[(Z[i].year - Y0) * NT + Z[i].ti].push_back(i);
        for (auto& g : grp) { std::vector<int> p = g; std::shuffle(p.begin(), p.end(), rng); for (size_t i = 0; i < g.size(); ++i) std::copy(X[p[i]].f, X[p[i]].f + NF, Z[g[i]].f); }
        std::vector<Acc> B; accumulate(Z, B); WF w = walk(B, true, topk); null.push_back(w.all.s / w.all.n); }
      std::sort(null.begin(), null.end()); const double real = wc.all.s / wc.all.n; const double pct = 100.0 * (std::lower_bound(null.begin(), null.end(), real) - null.begin()) / null.size();
      printf("\n %s — walk-forward 2013-24 (each year's rule chosen on 2011..year-1):\n", topk == 1 ? "BEST RULE" : "TOP-10 RULES");
      line("regime rules", wc.all); printf("   eras %+.2f / %+.2f / %+.2f\n", wc.era[0].s / wc.era[0].n, wc.era[1].s / wc.era[1].n, wc.era[2].s / wc.era[2].n);
      line("no-regime baseline (follow/fade @T)", wu.all); printf("   eras %+.2f / %+.2f / %+.2f\n", wu.era[0].s / wu.era[0].n, wu.era[1].s / wu.era[1].n, wu.era[2].s / wu.era[2].n);
      printf("  shuffled readings: median %+.2f, 95th %+.2f → real is at the %.0fth percentile\n", null[null.size() / 2], null[(size_t)(null.size() * 0.95)], pct);
      const bool pass = real > wu.all.s / wu.all.n && pct >= 95 && real >= 2 && wc.era[0].s > 0 && wc.era[1].s > 0 && wc.era[2].s > 0;
      printf("  VERDICT: %s\n", pass ? "PASS (→ holdout)" : "FAIL");
      if (topk == 1) { printf("  rule picked each year:\n"); for (size_t q = 0; q < wc.picks.size(); ++q) { const Acc& a = A[(2013 + q - Y0) * NR + wc.picks[q]]; printf("    %zu  %-60s → %+6.2f (n %3.0f)\n", 2013 + q, name(wc.picks[q]).c_str(), a.s / a.n, a.n); } }
      // in-sample contrast + holdout (only looked at for a PASS; printed for the record otherwise, marked)
      WF ho = walk(A, true, topk, 2025, 2026, 2024);
      printf("  in-sample best on 2011-24: %s; holdout 2025-26 of the rule(s) chosen on ≤2024: %+.2f (n %.0f)%s\n", name(walk(A, true, 1, 2025, 2025).picks[0]).c_str(), ho.all.s / ho.all.n, ho.all.n, pass ? "" : "  [not a PASS — record only]");
    }
    { std::vector<Acc> cum(NR); for (int yy = 2011; yy <= 2024; ++yy) for (int r = 0; r < NR; ++r) cum[r].add(A[(yy - Y0) * NR + r]);
      std::vector<std::pair<double, int>> sc; for (int r = 0; r < NR; ++r) if (cum[r].n >= 100) sc.push_back({cum[r].t(), r}); std::sort(sc.begin(), sc.end(), std::greater<>());
      printf("\n  in-sample top 8 (2011-24, the 'optimised' view — not tradable evidence):\n");
      for (int q = 0; q < 8; ++q) { const Acc& a = cum[sc[q].second]; printf("    %-60s %+6.2f pts (n %4.0f, t %.1f)\n", name(sc[q].second).c_str(), a.s / a.n, a.n, sc[q].first); } }
  }
}
