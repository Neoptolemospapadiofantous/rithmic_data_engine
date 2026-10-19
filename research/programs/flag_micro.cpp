// 2026-10-19 (founder: "the bull flag we have and we generate are more macro"): MICRO flags — a short sharp burst and a short tight
// pause, as a trader marks them by eye — instead of the 3-15-bar poles of flag_opt_all.cpp. NY 1-5m, bull + bear (bears on the
// price-mirrored session), NQ 2010-07 → 2026-10.
//  pole  P = 1-4 consecutive candles, each closing in the trend direction with a body ≥ 50 % of its range; pole = from the first
//        pole candle's low to the last one's high; ≥ k = 2 / 3 / 4 × the session's average chart bar
//  flag  F = 1-4 candles after it: no new high, every candle's range ≤ 60 % of the average pole candle, lowest low above the pole's
//        midpoint (pullback ≤ 50 %)
//  ctx   none, or with the session direction (above the 09:30 open and VWAP, path straightness ≥ 0.55)
//  entry M1 stop order 1 tick above the last flag candle's high, live for the next 2 bars · M2 1 tick above the pole top, same window
//  exit  measured move (entry + pole) · 2R · hold to 15:55;  stop 1 tick under the flag low.  2 pts cost scaled to the day.
// Entries 09:45-14:00. Selection 2010-18, test 2019-24, holdout 2025-26. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; int k; };
constexpr int NX = 3;
struct Cand { int tf, side, P, F, era; double kpole; bool ctx; float R[2][NX]; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const char* XN[NX] = {"measured", "2R", "hold"}; const char* EN[2] = {"M1 flag-high stop", "M2 pole-top stop"};
  std::vector<Cand> cs; double nd[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; nd[era]++; const double cost = 2 * d.atr / 320.0; const size_t n = I.size();
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3 * s.v[I[k]]; vv += s.v[I[k]]; vw[k] = vv > 0 ? pv / vv : Cc[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); hiS[k] = hi; loS[k] = lo; } }
      const double so = O[0];
      auto exits = [&](size_t k0, double e, double st, double M, float out[NX]) { const double risk = e - st;
        for (int x = 0; x < NX; ++x) { double tg = x == 0 ? e + M : x == 1 ? e + 2 * risk : 1e18, px = NAN; if (Lw[k0] <= st) px = st;
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; out[x] = (px - e - cost) / risk; } };
      for (int tf = 1; tf <= 5; ++tf) { std::vector<FB> b; std::vector<int> busy(17, -1);
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          if ((tm - 930) % tf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.k = k; }
          if ((tm + 1 - 930) % tf) continue; const int q = b.size() - 1; if (q < 10 || tm < 944 || tm >= 1260) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1;
          for (int F = 1; F <= 4; ++F) for (int P = 1; P <= 4; ++P) { const int slot = (F - 1) * 4 + (P - 1); if (q <= busy[slot]) continue;
            const int h = q - F, p0 = h - P + 1; if (p0 < 1) continue; bool ok = true; double prs = 0;
            for (int r = p0; r <= h; ++r) { double rg = b[r].h - b[r].l; if (!(b[r].c > b[r].o) || rg <= 0 || (b[r].c - b[r].o) < 0.5 * rg) { ok = false; break; } prs += rg; } if (!ok) continue;
            if (h + 1 <= q && b[h].h < b[h - 1].h) continue;   // the last pole candle makes the high
            const double H = b[h].h, L = b[p0].l, M = H - L; if (M < 2 * ar) continue; prs /= P;
            double FL = 1e18, FHl = b[q].h; for (int r = h + 1; r <= q; ++r) { if (b[r].h > H || b[r].h - b[r].l > 0.6 * prs) ok = false; FL = std::min(FL, b[r].l); } if (!ok || FL <= L + 0.5 * M) continue;
            const size_t kq = b[q].k; const double cl = b[q].c;
            Cand c{}; c.tf = tf; c.side = side; c.P = P; c.F = F; c.era = era; c.kpole = M / ar; c.ctx = cl > so && cl > vw[kq] && hiS[kq] > loS[kq] && (cl - so) / (hiS[kq] - loS[kq]) >= 0.55;
            for (int e = 0; e < 2; ++e) for (int x = 0; x < NX; ++x) c.R[e][x] = NAN;
            // the next 2 chart bars: stop orders above the last flag candle's high (M1) and above the pole top (M2); flag low as stop
            const double t1 = FHl + 0.25, t2 = H + 0.25, st = FL - 0.25; int nb = 0; size_t kend = kq;
            for (size_t kk = kq + 1; kk < n && nb < 2; ++kk) { kend = kk; if (Lw[kk] <= st) break;
              if (std::isnan(c.R[0][0]) && Hh[kk] >= t1) exits(kk, t1, st, M, c.R[0]);
              if (std::isnan(c.R[1][0]) && Hh[kk] >= t2) exits(kk, t2, st, M, c.R[1]);
              if ((TM[kk] + 1 - 930) % tf == 0) nb++; }
            for (int sl = 0; sl < 16; ++sl) busy[sl] = std::max(busy[sl], (int)b.size() - 1 + 2);   // one micro flag at a time per chart
            (void)kend; cs.push_back(c); goto next_bar; }
          next_bar:; } } } }
  // the grid
  struct Cfg { int tf, side, P, F, ki, ctx, e, x; double n[3], s[3], w[3]; }; const double KP[3] = {2, 3, 4}; std::vector<Cfg> G;
  for (int tf = 1; tf <= 5; ++tf) for (int side = 1; side >= -1; side -= 2) for (int P = 1; P <= 4; ++P) for (int F = 1; F <= 4; ++F) for (int ki = 0; ki < 3; ++ki) for (int cx = 0; cx < 2; ++cx) for (int e = 0; e < 2; ++e) for (int x = 0; x < NX; ++x)
    G.push_back({tf, side, P, F, ki, cx, e, x, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}});
  for (auto& g : G) for (auto& c : cs) { if (c.tf != g.tf || c.side != g.side || c.P != g.P || c.F != g.F || c.kpole < KP[g.ki] || (g.ctx && !c.ctx)) continue; float r = c.R[g.e][g.x]; if (std::isnan(r)) continue; g.n[c.era]++; g.s[c.era] += r; g.w[c.era] += r > 0; }
  const double ND = nd[0] + nd[1] + nd[2];
  printf("Micro flags (1-4 strong candles + 1-4 small candles) — NY 1-5m bull + bear, %zu candidates, %zu configs. R after 2 pts cost.\nSelection 2010-18 (%.0f days), test 2019-24 (%.0f), holdout 2025-26 (%.0f).\n", cs.size(), G.size(), nd[0], nd[1], nd[2]);
  auto mean = [](const Cfg& g, int e) { return g.n[e] ? g.s[e] / g.n[e] : NAN; };
  { printf("\n== How many micro flags a day (any size, no context) per chart and side:\n"); for (int tf = 1; tf <= 5; ++tf) { printf("  %dm", tf); for (int side = 1; side >= -1; side -= 2) { double n = 0, nc = 0; for (auto& c : cs) if (c.tf == tf && c.side == side) { n++; nc += c.ctx; } printf("  %s %.2f/day (with context %.2f)", side > 0 ? "bull" : "bear", n / ND, nc / ND); } printf("\n"); } }
  std::vector<const Cfg*> el; for (auto& g : G) if (g.n[0] >= 150) el.push_back(&g); std::sort(el.begin(), el.end(), [&](auto a, auto b) { return mean(*a, 0) > mean(*b, 0); });
  printf("\n== Does the 2010-18 ranking carry forward? (%zu configs with >= 150 trades 2010-18)\n", el.size());
  for (double top : {1.0, 0.10, 0.05, 0.01}) { size_t m = std::max<size_t>(1, el.size() * top); double p1 = 0, m1 = 0, p2 = 0, m2 = 0, c2 = 0; for (size_t i = 0; i < m; ++i) { double a = mean(*el[i], 1), b = mean(*el[i], 2); p1 += a > 0; m1 += a; if (!std::isnan(b)) { p2 += b > 0; m2 += b; c2++; } }
    printf("  top %5.1f%% (%4zu): 2019-24 positive %5.1f%%, mean %+.3fR | 2025-26 positive %5.1f%%, mean %+.3fR\n", 100 * top, m, 100 * p1 / m, m1 / m, c2 ? 100 * p2 / c2 : 0, c2 ? m2 / c2 : 0); }
  auto line = [&](const Cfg& g) { printf("  %dm %s pole %d bar(s) >=%.0fx flag %d bar(s) %s %-18s %-8s | 2010-18 %5.0f %+.3f (%2.0f%% win) | 2019-24 %5.0f %+.3f | 2025-26 %4.0f %+.3f | %.2f/day\n", g.tf, g.side > 0 ? "bull" : "bear", g.P, KP[g.ki], g.F, g.ctx ? "ctx" : "   ", EN[g.e], XN[g.x], g.n[0], mean(g, 0), g.n[0] ? 100 * g.w[0] / g.n[0] : 0, g.n[1], mean(g, 1), g.n[2], mean(g, 2), (g.n[0] + g.n[1] + g.n[2]) / ND); };
  printf("\n== Top 20 by 2010-18\n"); for (size_t i = 0; i < 20 && i < el.size(); ++i) line(*el[i]);
  printf("\n== Best per chart × side (2010-18)\n"); for (int tf = 1; tf <= 5; ++tf) for (int side = 1; side >= -1; side -= 2) for (auto* g : el) if (g->tf == tf && g->side == side) { line(*g); break; }
  printf("\n== Each choice on its own (mean 2019-24 R of configs using it minus the mean of all; 2025-26 mean in brackets)\n");
  double all1 = 0; int na = 0; for (auto* g : el) if (!std::isnan(mean(*g, 1))) { all1 += mean(*g, 1); na++; } all1 /= std::max(1, na);
  auto marg = [&](const char* lab, std::function<bool(const Cfg&)> f) { double s1 = 0, s2 = 0; int n1 = 0, n2 = 0; for (auto* g : el) if (f(*g)) { double a = mean(*g, 1), b = mean(*g, 2); if (!std::isnan(a)) { s1 += a; n1++; } if (!std::isnan(b)) { s2 += b; n2++; } } printf("  %-22s %+.3f (%+.3f) [%d]\n", lab, n1 ? s1 / n1 - all1 : 0, n2 ? s2 / n2 : 0, n1); };
  printf("  all configs mean 2019-24 %+.3f\n", all1);
  for (int tf = 1; tf <= 5; ++tf) { char b[24]; snprintf(b, 24, "chart %dm", tf); marg(b, [tf](const Cfg& g) { return g.tf == tf; }); }
  marg("bull", [](const Cfg& g) { return g.side > 0; }); marg("bear", [](const Cfg& g) { return g.side < 0; });
  for (int P = 1; P <= 4; ++P) { char b[24]; snprintf(b, 24, "pole %d bar(s)", P); marg(b, [P](const Cfg& g) { return g.P == P; }); }
  for (int F = 1; F <= 4; ++F) { char b[24]; snprintf(b, 24, "flag %d bar(s)", F); marg(b, [F](const Cfg& g) { return g.F == F; }); }
  for (int i = 0; i < 3; ++i) { char b[24]; snprintf(b, 24, "pole >= %.0fx bar", KP[i]); marg(b, [i](const Cfg& g) { return g.ki == i; }); }
  marg("with context", [](const Cfg& g) { return g.ctx == 1; }); marg("no context", [](const Cfg& g) { return g.ctx == 0; });
  for (int e = 0; e < 2; ++e) marg(EN[e], [e](const Cfg& g) { return g.e == e; }); for (int x = 0; x < NX; ++x) { char b[24]; snprintf(b, 24, "exit %s", XN[x]); marg(b, [x](const Cfg& g) { return g.x == x; }); }
  return 0;
}
