// 2026-10-19 (founder: "optimize more and check what else we can do" → "try all"): one optimisation over every flag choice we have,
// NY 1-5m, bull AND bear (bears run on the price-mirrored session, so one code path), NQ 2010-07 → 2026-10.
// Fixed in advance (from the sequence study): price on the trend side of the 09:30 open and of VWAP, session path straightness ≥ 0.55,
// entry 09:45-14:00, one candidate at a time per chart × flag length. Free choices (the grid):
//  F     flag length when the candidate is registered: 2 / 3 / 4 bars after the pole top (pullback 15-50 %, no new high)
//  shape any · CHANNEL (flag highs and lows both flat-or-down and roughly parallel) · PENNANT (highs down, lows up)
//  pole  ≥ 3 / 4 / 5 × the session's average chart bar, and ≥ 0 / 0.30 / 0.40 × ATR
//  vol   any · flag volume per bar below the pole's
//  entry E1 close above the flag high · E2 1 tick above the pole top · E3 wick-candle close · E7 close above the flag's upper channel line
//  exit  target pole top + 0.5 / 1.0 / 1.5 × pole · no target (hold to 15:55) · break-even at +1R then target 1.0 × pole
// Stops: flag low (E1, E2, E7), below the wick candle (E3). 2 pts cost scaled to the day; same-minute fill + stop = loss.
// Selection on 2010-18 only; 2019-24 is the test; 2025-26 the holdout. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c, v; int k; };
constexpr int NE = 4, NX = 5;
struct Cand { int tf, side, F, era, shape; double pb, pa, sc; bool vol; float R[NE][NX]; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const char* EN[NE] = {"E1 flag-high close", "E2 pole-top stop", "E3 wick close", "E7 channel-line close"}; const char* XN[NX] = {"T0.5", "T1.0", "T1.5", "hold", "BE+T1"}; const char* SH[3] = {"any", "channel", "pennant"};
  std::vector<Cand> cs; double nd[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; nd[era]++; const double A = d.atr, sc = A / 320.0, cost = 2 * sc; const size_t n = I.size();
    for (int side = 1; side >= -1; side -= 2) {
      // mirrored minute series: bears become bulls
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3 * V[k]; vv += V[k]; vw[k] = vv > 0 ? pv / vv : Cc[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); hiS[k] = hi; loS[k] = lo; } }
      const double so = O[0];
      auto exitAll = [&](size_t k0, double e, double st, double H, double M, bool fillMin, float out[NX]) { const double risk = e - st;
        for (int x = 0; x < NX; ++x) { double tg = x == 0 ? H + 0.5 * M : x == 1 || x == 4 ? H + M : x == 2 ? H + 1.5 * M : 1e18; if (tg <= e) { out[x] = NAN; continue; }
          double stp = st, px = NAN; if (fillMin && Lw[k0] <= stp) px = stp;
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= stp) { px = stp; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } if (x == 4 && Cc[k] - e >= risk) stp = std::max(stp, e); }
          if (std::isnan(px)) px = Cc[n - 1]; out[x] = (px - e - cost) / risk; } };
      for (int tf = 1; tf <= 5; ++tf) for (int F = 2; F <= 4; ++F) { std::vector<FB> b; int busy = -1;
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          if ((tm - 930) % tf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; }
          if ((tm + 1 - 930) % tf) continue; const int q = b.size() - 1; if (q < F + 4 || q <= busy || tm < 944 || tm >= 1260) continue;
          const int h = q - F; const double H = b[h].h; bool ok = true; for (int r = h + 1; r <= q; ++r) if (b[r].h > H) ok = false; if (!ok) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 3 * ar) continue;
          double FH = -1e18, FL = 1e18, fv = 0, pv = 0; for (int r = h + 1; r <= q; ++r) { FH = std::max(FH, b[r].h); FL = std::min(FL, b[r].l); fv += b[r].v; } for (int r = l; r <= h; ++r) pv += b[r].v;
          const double pull = (H - FL) / M; if (pull < 0.15 || pull > 0.5) continue;
          const size_t kq = b[q].k; const double cl = b[q].c; if (!(cl > so && cl > vw[kq] && hiS[kq] > loS[kq] && (cl - so) / (hiS[kq] - loS[kq]) >= 0.55)) continue;
          // flag shape: least-squares slopes of the flag bars' highs and lows (per bar, in pole units); with 2 bars = the 2-point slope
          double sh = 0, sl = 0; { double mx = 0, mh = 0, ml = 0; int nn = q - h; for (int r = h + 1; r <= q; ++r) { mx += r; mh += b[r].h; ml += b[r].l; } mx /= nn; mh /= nn; ml /= nn; double sxx = 0, sxh = 0, sxl = 0; for (int r = h + 1; r <= q; ++r) { sxx += (r - mx) * (r - mx); sxh += (r - mx) * (b[r].h - mh); sxl += (r - mx) * (b[r].l - ml); } sh = sxx > 0 ? sxh / sxx / M : 0; sl = sxx > 0 ? sxl / sxx / M : 0;
            // upper channel line value at bar r: mh + slope*(r - mx)
            Cand c{}; c.tf = tf; c.side = side; c.F = F; c.era = era; c.sc = sc; c.pb = M / ar; c.pa = M / A; c.vol = fv / (q - h) < pv / (h - l + 1);
            c.shape = (sh <= 0.03 && sl <= 0.03 && std::fabs(sh - sl) <= 0.12) ? 1 : (sh < -0.01 && sl > 0.01) ? 2 : 0;
            for (int e = 0; e < NE; ++e) for (int x = 0; x < NX; ++x) c.R[e][x] = NAN;
            const double slopeH = sxx > 0 ? sxh / sxx : 0; double fh = FH, fl = FL; bool done = false, gotW = false; int nb = 0; double bo = 0, bh = -1e18, bl = 1e18;
            for (size_t kk = kq + 1; kk < n && !done; ++kk) { if (TM[kk] >= 1260) break; if (bh < -1e17) bo = O[kk]; bh = std::max(bh, Hh[kk]); bl = std::min(bl, Lw[kk]);
              if (Hh[kk] > H && std::isnan(c.R[1][1])) exitAll(kk, H + 0.25, fl - 0.25, H, M, true, c.R[1]);
              if ((TM[kk] + 1 - 930) % tf == 0) { nb++; const double bc = Cc[kk]; const double line = mh + slopeH * (q + nb - mx);
                if (std::isnan(c.R[0][1]) && bc > fh) exitAll(kk, bc, fl - 0.25, H, M, false, c.R[0]);
                if (std::isnan(c.R[3][1]) && bc > line && bc <= H + 0.25 * M) exitAll(kk, bc, std::min(fl, bl) - 0.25, H, M, false, c.R[3]);
                if (!gotW && bh <= H) { double rg = bh - bl, wk = std::min(bo, bc) - bl; if (rg > 0 && wk >= 0.5 * rg && (bc - bl) / rg >= 0.5) { gotW = true; exitAll(kk, bc, bl - 0.25, H, M, false, c.R[2]); } }
                if (bh > H || bc < H - 0.5 * M || nb >= 12) done = true; fh = std::max(fh, bh); fl = std::min(fl, bl); bh = -1e18; bl = 1e18; } }
            busy = q + nb; cs.push_back(c); } } } } }
  // ---------- grid
  struct Cfg { int tf, side, F, shape, pbi, pai, vol, e, x; double n[3], s[3], w[3]; };
  const double PB[3] = {3, 4, 5}, PA[3] = {0, 0.30, 0.40}; std::vector<Cfg> G;
  for (int tf = 1; tf <= 5; ++tf) for (int side = 1; side >= -1; side -= 2) for (int F = 2; F <= 4; ++F) for (int shp = 0; shp < 3; ++shp) for (int pbi = 0; pbi < 3; ++pbi) for (int pai = 0; pai < 3; ++pai) for (int vol = 0; vol < 2; ++vol)
    for (int e = 0; e < NE; ++e) for (int x = 0; x < NX; ++x) G.push_back({tf, side, F, shp, pbi, pai, vol, e, x, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}});
  std::map<std::tuple<int, int, int>, std::vector<const Cand*>> byKey; for (auto& c : cs) byKey[{c.tf, c.side, c.F}].push_back(&c);
  for (auto& g : G) for (auto* c : byKey[{g.tf, g.side, g.F}]) { if (g.shape && c->shape != g.shape) continue; if (c->pb < PB[g.pbi] || c->pa < PA[g.pai]) continue; if (g.vol && !c->vol) continue; float r = c->R[g.e][g.x]; if (std::isnan(r)) continue; g.n[c->era]++; g.s[c->era] += r; g.w[c->era] += r > 0; }
  printf("Flag optimisation — every choice, NY 1-5m bull + bear, %zu flag candidates, %zu configurations. R after 2 pts cost.\nSelection on 2010-18 (%.0f days); test 2019-24 (%.0f days); holdout 2025-26 (%.0f days).\n", cs.size(), G.size(), nd[0], nd[1], nd[2]);
  auto mean = [](const Cfg& g, int e) { return g.n[e] ? g.s[e] / g.n[e] : NAN; };
  std::vector<const Cfg*> el; for (auto& g : G) if (g.n[0] >= 150) el.push_back(&g);
  std::sort(el.begin(), el.end(), [&](auto a, auto b) { return mean(*a, 0) > mean(*b, 0); });
  printf("\n== Does the 2010-18 ranking carry forward? (configs with >= 150 trades 2010-18)\n");
  for (double top : {1.0, 0.10, 0.05, 0.01}) { size_t m = std::max<size_t>(1, el.size() * top); double p1 = 0, m1 = 0, p2 = 0, m2 = 0, c2 = 0; for (size_t i = 0; i < m; ++i) { double a = mean(*el[i], 1), b = mean(*el[i], 2); p1 += a > 0; m1 += a; if (!std::isnan(b)) { p2 += b > 0; m2 += b; c2++; } }
    printf("  top %5.1f%% (%5zu configs): 2019-24 positive %5.1f%%, mean %+.3fR | 2025-26 positive %5.1f%%, mean %+.3fR\n", 100 * top, m, 100 * p1 / m, m1 / m, c2 ? 100 * p2 / c2 : 0, c2 ? m2 / c2 : 0); }
  auto line = [&](const Cfg& g) { printf("  %dm %s F%d %-7s pole>=%.0fx bar >=%.2f ATR %s %-21s %-5s | 2010-18 %5.0f %+.3f (%2.0f%%) | 2019-24 %5.0f %+.3f | 2025-26 %4.0f %+.3f | %.2f/day\n", g.tf, g.side > 0 ? "bull" : "bear", g.F, SH[g.shape], PB[g.pbi], PA[g.pai], g.vol ? "vol" : "   ", EN[g.e], XN[g.x], g.n[0], mean(g, 0), g.n[0] ? 100 * g.w[0] / g.n[0] : 0, g.n[1], mean(g, 1), g.n[2], mean(g, 2), (g.n[0] + g.n[1] + g.n[2]) / (nd[0] + nd[1] + nd[2])); };
  printf("\n== Top 25 by 2010-18 (the later columns are the test)\n"); for (size_t i = 0; i < 25 && i < el.size(); ++i) line(*el[i]);
  printf("\n== Best 2010-18 config per chart × side (>= 150 trades 2010-18)\n");
  for (int tf = 1; tf <= 5; ++tf) for (int side = 1; side >= -1; side -= 2) for (auto* g : el) if (g->tf == tf && g->side == side) { line(*g); break; }
  printf("\n== Each choice on its own: mean 2019-24 R of the configs that use it, minus the mean of all configs (positive = the choice helps out of sample)\n");
  double all1 = 0; int na = 0; for (auto* g : el) if (!std::isnan(mean(*g, 1))) { all1 += mean(*g, 1); na++; } all1 /= na;
  auto marg = [&](const char* lab, std::function<bool(const Cfg&)> f) { double s1 = 0, s2 = 0; int n1 = 0, n2 = 0; for (auto* g : el) if (f(*g)) { double a = mean(*g, 1), b = mean(*g, 2); if (!std::isnan(a)) { s1 += a; n1++; } if (!std::isnan(b)) { s2 += b; n2++; } } printf("  %-28s %+.3f (2025-26 %+.3f)  [%d configs]\n", lab, n1 ? s1 / n1 - all1 : 0, n2 ? s2 / n2 : 0, n1); };
  for (int tf = 1; tf <= 5; ++tf) { char b[32]; snprintf(b, 32, "chart %dm", tf); marg(b, [tf](const Cfg& g) { return g.tf == tf; }); }
  marg("bull", [](const Cfg& g) { return g.side > 0; }); marg("bear", [](const Cfg& g) { return g.side < 0; });
  for (int F = 2; F <= 4; ++F) { char b[32]; snprintf(b, 32, "flag length %d", F); marg(b, [F](const Cfg& g) { return g.F == F; }); }
  for (int sh = 0; sh < 3; ++sh) { char b[32]; snprintf(b, 32, "shape %s", SH[sh]); marg(b, [sh](const Cfg& g) { return g.shape == sh; }); }
  for (int i = 0; i < 3; ++i) { char b[32]; snprintf(b, 32, "pole >= %.0fx bar", PB[i]); marg(b, [i](const Cfg& g) { return g.pbi == i; }); }
  for (int i = 0; i < 3; ++i) { char b[32]; snprintf(b, 32, "pole >= %.2f ATR", PA[i]); marg(b, [i](const Cfg& g) { return g.pai == i; }); }
  marg("flag volume filter", [](const Cfg& g) { return g.vol == 1; }); marg("no volume filter", [](const Cfg& g) { return g.vol == 0; });
  for (int e = 0; e < NE; ++e) marg(EN[e], [e](const Cfg& g) { return g.e == e; });
  for (int x = 0; x < NX; ++x) { char b[32]; snprintf(b, 32, "exit %s", XN[x]); marg(b, [x](const Cfg& g) { return g.x == x; }); }
  return 0;
}
