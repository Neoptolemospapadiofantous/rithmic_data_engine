// 2026-10-17 momentum + EMA, optimised by walk-forward (research/preregistered/2026-10-17_momentum_optimize.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <map>
#include <unordered_map>
using namespace lab;
constexpr int Y0 = 2010, NY = 17, ND = 1380, R0 = 930;
const int WS[5] = {15, 20, 30, 45, 60}; const double QS[5] = {0.2, 0.25, 0.3, 0.35, 0.4}; const int LS[5] = {9, 14, 20, 30, 50};
const double TS[3] = {0.0, 0.03, 0.06}; const double SS[4] = {0.15, 0.2, 0.25, 0.35}; const double BS[4] = {0.75, 1.0, 1.5, 0.0}; const int ES_[2] = {1050, 1195};
constexpr int NC = 5 * 5 * 5 * 3 * 4 * 4 * 2;
struct Set { int w, q, l, t, s, b, e; };
static int cid(const Set& x) { return (((((x.w * 5 + x.q) * 5 + x.l) * 3 + x.t) * 4 + x.s) * 4 + x.b) * 2 + x.e; }
static Set dec(int c) { Set x; x.e = c % 2; c /= 2; x.b = c % 4; c /= 4; x.s = c % 4; c /= 4; x.t = c % 3; c /= 3; x.l = c % 5; c /= 5; x.q = c % 5; c /= 5; x.w = c; return x; }
struct YA { float n = 0, r = 0, pts = 0; };
// run every setting in `cells` over one series; out[cell][year]
static void run(const Series& s, double COST, const std::vector<int>& cells, std::vector<std::array<YA, NY>>& out) {
  out.assign(cells.size(), {});
  std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND), vw(ND);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20261016) continue; const int yi = D.date / 10000 - Y0; if (yi < 0 || yi >= NY) continue;
    { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = 0; } last = c[m]; } }
    { double pv = 0, vv = 0; for (int m = R0; m < ND; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
    const double op = o[R0], atr = D.atr;
    const int NB = ND / 5; std::vector<double> BH(NB), BL(NB), BC(NB); for (int k = 0; k < NB; ++k) { BH[k] = -1e18; BL[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { BH[k] = std::max(BH[k], h[m]); BL[k] = std::min(BL[k], l[m]); } BC[k] = c[5 * k + 4]; }
    std::array<std::vector<double>, 5> E; for (int q = 0; q < 5; ++q) { E[q].resize(NB); double x = BC[0]; for (int k = 0; k < NB; ++k) { x += 2.0 / (LS[q] + 1) * (BC[k] - x); E[q][k] = x; } }
    // momentum direction per (window, checkpoint): eff and sign
    const int K0 = (R0 + 15) / 5, K1 = 1195 / 5;   // bars whose END is 09:50 … 13:55 → bar index k = end/5 - 1
    std::array<std::vector<std::pair<double, int>>, 5> MOM; for (int wi = 0; wi < 5; ++wi) { MOM[wi].assign(NB, {0.0, 0});
      for (int k = K0 - 1; k < K1; ++k) { const int mm = 5 * k + 4, W = WS[wi]; double path = 0; for (int q = mm - W + 1; q <= mm; ++q) path += std::fabs(c[q] - c[q - 1]); const double net = c[mm] - c[mm - W];
        MOM[wi][k] = {path > 0 ? std::fabs(net) / path : 0.0, net > 0 ? 1 : -1}; } }
    std::unordered_map<int, std::pair<double, int>> memo;   // (k, dir, s, b) → (pts net, exit minute)
    auto exitsim = [&](int k, int d, int si, int bi) { const int key = ((k * 2 + (d > 0)) * 4 + si) * 4 + bi; auto it = memo.find(key); if (it != memo.end()) return it->second;
      const double C = BC[k], st = SS[si] * atr; double stop = C - d * st, best = C; bool be = false; double pts = NAN; int xm = R0 + 385;
      for (int m = 5 * k + 5; m < R0 + 385; ++m) { if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - C); xm = m + 1; break; }
        best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (BS[bi] > 0 && !be && d * (best - C) >= BS[bi] * st) { be = true; stop = C + d * COST; } }
      if (std::isnan(pts)) pts = d * (c[R0 + 384] - C); auto r = std::make_pair(pts - COST, xm); memo[key] = r; return r; };
    for (size_t ci = 0; ci < cells.size(); ++ci) { const Set x = dec(cells[ci]); auto& Y = out[ci][yi];
      int busy = 0, ntr = 0; bool lastwin = true; const double st = SS[x.s] * atr;
      for (int k = K0 - 1; k < K1; ++k) { const int end = 5 * k + 5; if (end > ES_[x.e]) break; if (ntr >= 3 || !lastwin) break; if (5 * k < busy) continue;
        auto [eff, d] = MOM[x.w][k]; if (eff < QS[x.q]) continue; const double C = BC[k];
        if (d * (C - vw[end - 1]) <= 0 || d * (C - op) <= 0) continue;
        const double ep = E[x.l][k - 1], tol = TS[x.t] * atr; if (!(d > 0 ? (BL[k] <= ep + tol && C > ep) : (BH[k] >= ep - tol && C < ep))) continue;
        auto [pts, xm] = exitsim(k, d, x.s, x.b); Y.n++; Y.r += pts / st; Y.pts += pts; busy = xm; ++ntr; lastwin = pts > 0; } } }
}
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series nq; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", nq, err)) return 1; build_days(nq, fomc, vix);
  std::vector<int> all(NC); for (int i = 0; i < NC; ++i) all[i] = i;
  std::vector<std::array<YA, NY>> A; run(nq, 1.6, all, A);
  auto sum = [&](const std::array<YA, NY>& y, int y0, int y1) { YA t; for (int k = y0; k <= y1; ++k) { t.n += y[k - Y0].n; t.r += y[k - Y0].r; t.pts += y[k - Y0].pts; } return t; };
  auto nm = [](int c) { Set x = dec(c); char b[160]; snprintf(b, 160, "W%-2d Q%.2f EMA%-2d tol%.2f stop%.2f BE%s last %s", WS[x.w], QS[x.q], LS[x.l], TS[x.t], SS[x.s], BS[x.b] > 0 ? (std::to_string(BS[x.b]).substr(0, 4) + "R").c_str() : "none", x.e ? "13:55" : "11:30"); return std::string(b); };
  auto line = [&](const char* tag, const std::array<YA, NY>& y, const std::string& n) { YA a = sum(y, 2010, 2024), e0 = sum(y, 2010, 2018), e1 = sum(y, 2019, 2022), e2 = sum(y, 2023, 2024), hh = sum(y, 2025, 2026);
    printf("  %-10s %-56s n %5.0f %+.3f R (%+.3f / %+.3f / %+.3f) %+6.2f pts | 25-26 %+.3f (n %.0f)\n", tag, n.c_str(), a.n, a.r / a.n, e0.r / e0.n, e1.r / e1.n, e2.r / e2.n, a.pts / a.n, hh.r / hh.n, hh.n); };
  const int BASE = cid({2, 2, 2, 1, 2, 1, 1});
  printf("Momentum + EMA optimisation, %d settings, NQ New York, cost 1.6 pts. R/trade 2010-24 (eras 10-18 / 19-22 / 23-24)\n", NC);
  line("BASE", A[BASE], nm(BASE));
  // walk-forward
  YA wf, bs; std::map<int, int> picks; printf("Walk-forward (each year: best total R of 2010..year-1, ≥ 100 trades):\n");
  for (int y = 2013; y <= 2024; ++y) { int best = BASE; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(A[ci], 2010, y - 1); if (a.n >= 100 && a.r > br) { br = a.r; best = ci; } }
    picks[best]++; auto& a = A[best][y - Y0]; auto& b = A[BASE][y - Y0]; wf.n += a.n; wf.r += a.r; wf.pts += a.pts; bs.n += b.n; bs.r += b.r; bs.pts += b.pts;
    printf("    %d %-56s %+.3f R (n %3.0f) | base %+.3f (n %.0f)\n", y, nm(best).c_str(), a.r / a.n, a.n, b.r / b.n, b.n); }
  printf("  WF 2013-24: %+.3f R/trade, %+.2f pts (n %.0f, total %+.0f R) | base %+.3f R, %+.2f pts (n %.0f, total %+.0f R)\n", wf.r / wf.n, wf.pts / wf.n, wf.n, wf.r, bs.r / bs.n, bs.pts / bs.n, bs.n, bs.r);
  // in-sample best + plateau (one step on any one parameter)
  int best = BASE; double br = -1e18; for (int ci = 0; ci < NC; ++ci) { YA a = sum(A[ci], 2010, 2024); if (a.n >= 150 && a.r > br) { br = a.r; best = ci; } }
  auto plateau = [&](int c0) { Set x = dec(c0); double r = 0, n = 0; int cnt = 0; int* f[7] = {&x.w, &x.q, &x.l, &x.t, &x.s, &x.b, &x.e}; const int lim[7] = {5, 5, 5, 3, 4, 4, 2};
    for (int d = 0; d < 7; ++d) for (int st : {-1, 1}) { Set y = x; int* g[7] = {&y.w, &y.q, &y.l, &y.t, &y.s, &y.b, &y.e}; *g[d] += st; if (*g[d] < 0 || *g[d] >= lim[d]) continue; YA a = sum(A[cid(y)], 2010, 2024); r += a.r; n += a.n; ++cnt; }
    (void)f; return std::make_tuple(r / n, cnt, n); };
  printf("In-sample best (total R, ≥ 150 trades) and its neighbourhood:\n"); line("IS-best", A[best], nm(best));
  { auto [pr, cnt, pn] = plateau(best); printf("    neighbourhood (%d settings one step away, n %.0f): %+.3f R/trade\n", cnt, pn, pr); }
  { auto [pr, cnt, pn] = plateau(BASE); printf("    base's neighbourhood (%d settings, n %.0f): %+.3f R/trade\n", cnt, pn, pr); }
  // top 10 by R/trade with n ≥ 300, and how many settings beat the base in all three eras
  std::vector<std::pair<double, int>> rk; for (int ci = 0; ci < NC; ++ci) { YA a = sum(A[ci], 2010, 2024); if (a.n >= 300) rk.push_back({a.r / a.n, ci}); } std::sort(rk.rbegin(), rk.rend());
  printf("Top 10 by R/trade (n ≥ 300):\n"); for (int q = 0; q < 10 && q < (int)rk.size(); ++q) line("", A[rk[q].second], nm(rk[q].second));
  int allp = 0; for (auto& [r, ci] : rk) allp += sum(A[ci], 2010, 2018).r > 0 && sum(A[ci], 2019, 2022).r > 0 && sum(A[ci], 2023, 2024).r > 0;
  printf("settings with n ≥ 300 positive in all three eras: %d of %zu\n", allp, rk.size());
  // dimension marginals (pooled R/trade over settings with n ≥ 300)
  const char* DN[7] = {"window", "efficiency", "EMA", "tolerance", "stop", "break-even", "last entry"}; const int lim[7] = {5, 5, 5, 3, 4, 4, 2};
  for (int d = 0; d < 7; ++d) { printf("  %-10s", DN[d]); for (int vv = 0; vv < lim[d]; ++vv) { double r = 0, n = 0; for (auto& [x, ci] : rk) { Set s = dec(ci); int val[7] = {s.w, s.q, s.l, s.t, s.s, s.b, s.e}; if (val[d] == vv) { YA a = sum(A[ci], 2010, 2024); r += a.r; n += a.n; } }
      const char* lab[7][5] = {{"15", "20", "30", "45", "60"}, {"0.20", "0.25", "0.30", "0.35", "0.40"}, {"9", "14", "20", "30", "50"}, {"0", "0.03", "0.06"}, {"0.15", "0.20", "0.25", "0.35"}, {"0.75R", "1R", "1.5R", "none"}, {"11:30", "13:55"}};
      printf(" %s %+.3f |", lab[d][vv], r / n); } printf("\n"); }
  // ES check: the walk-forward's final setting (best on 2010-24) and the base
  Series es; if (load_raw("data/edge_lab/cache", "ES", es, err)) { build_days(es, fomc, vix); std::vector<std::array<YA, NY>> B; run(es, 0.65, {best, BASE}, B);
    printf("ES (cost 0.65):\n"); line("final", B[0], nm(best)); line("BASE", B[1], nm(BASE)); }
}
