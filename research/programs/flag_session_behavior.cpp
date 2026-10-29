// 2026-10-20 flag_session_behavior.cpp (founder: "can we validate each session to identify possible behaviors per session to play
// around that"). The /flags page's flags (flag_exits.cpp detection: 1-5m, researched + inner tier, NY + Globex) split by the window the
// flag REGISTERED in: Asia 18:00-03:00 | London 03:00-09:30 | NY open 09:30-10:30 | NY mid 10:30-12:00 | lunch 12:00-13:30 |
// NY pm 13:30-14:00 registration (held to 15:55). Behaviours, all fixed before the run, R after a fixed cost (NQ 1.5 / ES 0.5 pts):
//  break %       share of flags whose pole top is broken (else the flag fails or expires)
//  held / failed after the pole-top break, does price reach +1R first (held) or the flag low first (failed break)
//  FOLLOW MM     the page's trade (2m trigger or pole-top stop, stop flag low, measured-move target)
//  FOLLOW 1R     same entry, 1R target
//  FADE          short the pole-top break (H + 1 tick), stop and target 1 × the same risk — profits when breaks fail
//  each against 5 same-day twins: a random minute in the 60 min after registration, same direction and distances.
// SESSION CONTEXT per window and day (no flags): trend share (|close − open| / range >= 0.55), continues the previous window's direction,
// first-15-min range: which side breaks first and whether the window CLOSES beyond it (holds) or not.
// PATH / DIRECTION (founder: "if path and direction helps … based on features"): FOLLOW MM split by — flag with the window's move so far
// (close at registration vs the window open), a straight path (that move / the window's range so far >= 0.55), flag with the previous
// window's direction (Asia has none: its previous window is the prior NY afternoon). In Asia the flag context (above the 18:00
// open, straight path) already implies "with the window move" and "straight", so those splits are empty there. FADE also shown for flags against the window move.
// Periods 2010-15 | 2016-20 | 2021-24 | 2025-26. Usage: flag_session_behavior [NQ|ES]. Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 10:30 = 990, 12:00 = 1080, 13:30 = 1170, 15:55 = 1315, 16:00 = 1320.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
static const char* WN[6] = {"Asia 18:00-03:00", "London 03:00-09:30", "NY open 09:30-10:30", "NY mid 10:30-12:00", "NY lunch 12:00-13:30", "NY pm 13:30-14:00"};
static int winOf(int tm) { return tm < 540 ? 0 : tm < 930 ? 1 : tm < 990 ? 2 : tm < 1080 ? 3 : tm < 1170 ? 4 : 5; }
static const int WB[7] = {0, 540, 930, 990, 1080, 1170, 1315};   // window bounds for the session context
struct Row { int date, era, side, tf, win; bool inner, broke, hasF, hasX, fail, held, aWin, straight, aPrev, hasPrev; double fMM, f1, tMM, t1, xR, tX; };
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const double COST = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(20261020);
  // session context: per day and window — open, high, low, close, first-15-min range, close
  struct Ctx { int era, n = 0; double trend = 0, cont = 0, contN = 0, orUp = 0, orDn = 0, orUpHold = 0, orDnHold = 0; };
  std::vector<std::array<Ctx, 6>> ctx(4);
  for (auto& d : s.days) { if (!d.ok || d.rollwin || d.date > 20261016) continue; const int era = d.date < 20160101 ? 0 : d.date < 20210101 ? 1 : d.date < 20250101 ? 2 : 3;
    double prevMove = NAN;
    for (int w = 0; w < 6; ++w) { double o = NAN, h = -1e18, l = 1e18, c = NAN, oh = -1e18, ol = 1e18; int cnt = 0; std::vector<int> idx;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < WB[w] || tm >= WB[w + 1]) continue; if (std::isnan(o)) o = s.o[i]; h = std::max(h, (double)s.h[i]); l = std::min(l, (double)s.l[i]); c = s.c[i]; cnt++; idx.push_back(i);
        if (tm < WB[w] + 15) { oh = std::max(oh, (double)s.h[i]); ol = std::min(ol, (double)s.l[i]); } }
      if (cnt < (WB[w + 1] - WB[w]) * 0.8) { prevMove = NAN; continue; }
      Ctx& z = ctx[era][w]; z.n++; const double mv = c - o; z.trend += std::fabs(mv) / std::max(h - l, 0.25) >= 0.55;
      if (!std::isnan(prevMove) && prevMove != 0 && mv != 0) { z.contN++; z.cont += (mv > 0) == (prevMove > 0); } prevMove = mv;
      for (int i : idx) { if (tday_min(s.t[i]) < WB[w] + 15) continue; const bool up = s.h[i] > oh, dn = s.l[i] < ol;
        if (up && !dn) { z.orUp++; z.orUpHold += c > oh; break; } if (dn && !up) { z.orDn++; z.orDnHold += c < ol; break; } if (up && dn) break; } } }
  std::vector<Row> rows; long nDays = 0; std::mt19937 tw_rng(20261020);
  const int S0[2] = {930, 0}, S1[2] = {1320, 930}, RG0[2] = {945, 15}, RG1[2] = {1200, 900}, FLAT[2] = {1315, 925};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; nDays++;
    const double A = d.atr; const int era = d.date < 20160101 ? 0 : d.date < 20210101 ? 1 : d.date < 20250101 ? 2 : 3;
    const double cost = COST; (void)A;
    double wo[6], wc[6]; for (int w = 0; w < 6; ++w) { wo[w] = wc[w] = NAN; }
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm >= 1315) continue; const int w = winOf(tm); if (std::isnan(wo[w])) wo[w] = s.o[i]; wc[w] = s.c[i]; }
    for (int sess = 0; sess < 2; ++sess) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[sess] && tm < S1[sess]) I.push_back(i); }
      if (I.size() < (sess == 0 ? 370u : 800u)) continue;
      const size_t n = I.size();
      for (int side = 1; side >= -1; side -= 2) {
        std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
        for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
          if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
        { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18;
          for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
            const double vw = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > vw; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
        size_t simExit = 0;   // exit minute of the last simL call (the one-position rule waits for the exit, not the entry)
        auto simL = [&](size_t k0, double e, double st, double tg, bool fillMin, double cost) -> double {   // long on the mirrored series
          const double risk = e - st; double px = NAN; simExit = n - 1; if (fillMin && Lw[k0] <= st) { px = st; simExit = k0; }
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; simExit = k; break; } if (Hh[k] >= tg) { px = tg; simExit = k; break; } if (TM[k] >= FLAT[sess]) { px = O[k]; simExit = k; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / risk; };
        auto simS = [&](size_t k0, double e, double R0, bool fillMin, double cost) -> double {   // short (fade), stop e + R0, target e − R0
          const double st = e + R0, tg = e - R0; double px = NAN; if (fillMin && Hh[k0] >= st) px = st;
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Hh[k] >= st) { px = st; break; } if (Lw[k] <= tg) { px = tg; break; } if (TM[k] >= FLAT[sess]) { px = O[k]; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; return (e - px - cost) / R0; };
        for (int tf = 1; tf <= 5; ++tf) for (int inner = 0; inner <= (tf <= 2 ? 1 : 0); ++inner) {
          std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - S0[sess]) / tf != (TM[b.back().k0] - S0[sess]) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } }
          int busy = -1, lastOut = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
          for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
            const int tmc = TM[b[q].k] + 1; if (tmc < RG0[sess] || tmc >= RG1[sess]) continue;
            const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
            int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
            bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
            double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
            const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
            const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
            int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
            // the page's follow (busy) and entries
            int nb = 0; size_t kPole = 0; double sPole = NAN, flo = fl0;
            for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; break; } flo = std::min(flo, Lw[kk]);
              if ((TM[kk] - S0[sess]) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
            busy = q + nb;
            size_t k2m = 0; double e2m = NAN, s2m = NAN;
            if (tf >= 3) { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
              for (size_t k = kc + 1; k < n; ++k) { const int mm = TM[k] - S0[sess]; if (TM[k] + 1 >= RG1[sess] + 60) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
                if (mm % 2 == 1) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
                if (mm % tf == tf - 1) { nb2++; fl = std::min(fl, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
            const bool use2m = k2m && (!kPole || k2m <= kPole);
            const int win = winOf(tmc);
            Row r{}; r.date = d.date; r.era = era; r.side = side; r.tf = tf; r.inner = inner; r.win = win; r.broke = kPole != 0;
            // path / direction at registration (mirrored: the flag's direction is up): window move so far, its straightness, previous window
            { size_t kw = 0; while (kw < (size_t)kc && TM[kw] < WB[win]) ++kw; double hi = -1e18, lo = 1e18; for (size_t k = kw; k <= (size_t)kc; ++k) { hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); }
              const double mv = Cc[kc] - O[kw]; r.aWin = mv > 0; r.straight = mv > 0 && mv / std::max(hi - lo, 0.25) >= 0.55;
              r.hasPrev = win > 0 && !std::isnan(wo[win - 1]); r.aPrev = r.hasPrev && side * (wc[win - 1] - wo[win - 1]) > 0; }
            // follow: the page's trade (2m trigger or pole-top stop order, stop flag low, target measured move) and the same with a 1R target
            const size_t kE = use2m ? k2m : kPole;
            if (kE && (int)kE > lastOut) { const double e = use2m ? e2m : H + 0.25, st = use2m ? s2m : sPole;
              if (e - st > 0.25) { r.hasF = true; r.fMM = simL(kE, e, st, H + M, !use2m, cost); const size_t kExitF = simExit; r.f1 = simL(kE, e, st, e + (e - st), !use2m, cost);
                double t1 = 0, t2 = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); const double ce = Cc[k];
                  t1 += simL(k, ce, ce - (e - st), ce + (H + M - e), false, cost); t2 += simL(k, ce, ce - (e - st), ce + (e - st), false, cost); }
                r.tMM = t1 / 5; r.t1 = t2 / 5; lastOut = (int)kExitF;   /* 2026-10-20 audit: was kE + 1 — trades overlapped on a chart */ } }
            // fade: short at the pole-top break (H + 1 tick), stop / target 1 × the pole-top risk; failed break = flag low before +1R
            if (kPole) { const double e = H + 0.25, R0 = e - sPole;
              if (R0 > 0.25) { r.hasX = true; r.xR = simS(kPole, e, R0, true, cost);
                double t = 0; for (int z = 0; z < 5; ++z) { const size_t k = std::min(n - 2, (size_t)kc + rng() % 60); t += simS(k, Cc[k], R0, false, cost); } r.tX = t / 5;
                int res = 0; for (size_t k = kPole; k < n && !res; ++k) { if (Lw[k] <= sPole) res = -1; else if (Hh[k] >= e + R0) res = 1; if (TM[k] >= FLAT[sess]) break; } r.fail = res == -1; r.held = res == 1; } }
            rows.push_back(r); } } } } }
  printf("Flag behaviour per session window — %s 2010-07 → 2026-10, %ld days, %zu flags (1-5m, researched + inner tiers). Fixed cost %.1f pts.\n", sym.c_str(), nDays, rows.size(), COST);
  printf("Cells: 2010-15 | 2016-20 | 2021-24 | 2025-26 avg R (n) · all: avg R, PF, vs same-day twin. Twin = random minute in the hour after registration, same distances.\n");
  auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
  auto cell = [&](const char* name, std::function<bool(const Row&)> sel, std::function<bool(const Row&)> has, std::function<double(const Row&)> R, std::function<double(const Row&)> T) {
    double m[4] = {0}, c[4] = {0}, sum = 0, N = 0, tw = 0, gw = 0, gl = 0;
    for (auto& r : rows) if (sel(r) && has(r)) { const double x = R(r); m[r.era] += x; c[r.era]++; sum += x; N++; tw += x - T(r); (x > 0 ? gw : gl) += std::fabs(x); }
    if (N < 40) { printf("      %-22s (%.0f trades)\n", name, N); return; }
    printf("      %-22s", name); for (int e = 0; e < 4; ++e) { if (c[e] < 10) printf("   —    (%4.0f) |", c[e]); else printf(" %+.3f (%4.0f) |", m[e] / c[e], c[e]); }
    int pos = 0, have = 0; for (int e = 0; e < 4; ++e) if (c[e] >= 10) { have++; pos += m[e] > 0; }
    printf(" all %+.3f PF %.2f · vs twin %+.3f · %d/%d periods > 0\n", sum / N, pf(gw, gl), tw / N, pos, have); };
  for (int w = 0; w < 6; ++w) {
    printf("\n######## %s\n", WN[w]);
    { double n = 0, tr = 0, co = 0, cn = 0, uu = 0, uh = 0, dd = 0, dh = 0; for (int e = 0; e < 4; ++e) { auto& z = ctx[e][w]; n += z.n; tr += z.trend; co += z.cont; cn += z.contN; uu += z.orUp; uh += z.orUpHold; dd += z.orDn; dh += z.orDnHold; }
      char cs[16]; if (cn > 0) snprintf(cs, 16, "%.0f %%", 100 * co / cn); else snprintf(cs, 16, "n/a");
      printf("  session: %.0f days · trend %.0f %% · continues the previous window %s · first-15-min break holds to the close: up %.0f %%, down %.0f %%\n", n, 100 * tr / std::max(n, 1.0), cs, 100 * uh / std::max(uu, 1.0), 100 * dh / std::max(dd, 1.0));
      printf("           by period trend %%:"); for (int e = 0; e < 4; ++e) printf(" %.0f", 100 * ctx[e][w].trend / std::max(1, ctx[e][w].n)); printf(" · continues %%:"); for (int e = 0; e < 4; ++e) printf(" %.0f", 100 * ctx[e][w].cont / std::max(1.0, ctx[e][w].contN)); printf("\n"); }
    for (int side = 1; side >= -1; side -= 2) for (int g = 0; g < 2; ++g) {
      auto sel = [&, side, g, w](const Row& r) { return r.win == w && r.side == side && (g == 0 ? r.tf >= 3 : r.tf <= 2); };
      double n = 0, br = 0, he = 0, fa = 0, bx = 0; for (auto& r : rows) if (sel(r)) { n++; br += r.broke; if (r.hasX) { bx++; he += r.held; fa += r.fail; } }
      if (n < 40) continue;
      printf("  %s %s · %.0f flags (%.0f/yr) · break %.0f %% · after the break: held to +1R %.0f %%, failed to the flag low %.0f %%\n", side > 0 ? "BULL" : "BEAR", g == 0 ? "3-5m" : "1-2m", n, n / 16.3, 100 * br / n, 100 * he / std::max(bx, 1.0), 100 * fa / std::max(bx, 1.0));
      cell("FOLLOW MM", sel, [](const Row& r) { return r.hasF; }, [](const Row& r) { return r.fMM; }, [](const Row& r) { return r.tMM; });
      cell("FOLLOW 1R", sel, [](const Row& r) { return r.hasF; }, [](const Row& r) { return r.f1; }, [](const Row& r) { return r.t1; });
      cell("FADE 1:1", sel, [](const Row& r) { return r.hasX; }, [](const Row& r) { return r.xR; }, [](const Row& r) { return r.tX; });
      struct F { const char* y; const char* no; std::function<bool(const Row&)> f; std::function<bool(const Row&)> ok; };
      const F fs[3] = {{"MM with window move", "MM against window", [](const Row& r) { return r.aWin; }, [](const Row&) { return true; }},
                       {"MM straight path", "MM not straight", [](const Row& r) { return r.straight; }, [](const Row&) { return true; }},
                       {"MM with prev window", "MM against prev", [](const Row& r) { return r.aPrev; }, [](const Row& r) { return r.hasPrev; }}};
      for (auto& f : fs) { cell(f.y, [&](const Row& r) { return sel(r) && f.ok(r) && f.f(r); }, [](const Row& r) { return r.hasF; }, [](const Row& r) { return r.fMM; }, [](const Row& r) { return r.tMM; });
        cell(f.no, [&](const Row& r) { return sel(r) && f.ok(r) && !f.f(r); }, [](const Row& r) { return r.hasF; }, [](const Row& r) { return r.fMM; }, [](const Row& r) { return r.tMM; }); }
      cell("FADE against window", [&](const Row& r) { return sel(r) && !r.aWin; }, [](const Row& r) { return r.hasX; }, [](const Row& r) { return r.xR; }, [](const Row& r) { return r.tX; }); } }
  return 0;
}
