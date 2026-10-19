// 2026-10-19 (founder: "improve the structure of the flags, validate the formation of each bear and bull flag, what filter can we add,
// feature selection to see what affects bear and bull flags the best; with no filters how many trades a day on 1-2-3-4-5-15 m").
// PART A — trades a day with NO filters: every flag on each chart, each session (pole 6 bars >= 0.10 ATR, flag 5 bars <= 0.8 x pole,
// close beyond the flag), every signal counted.
// PART B — feature selection on NY flags (same loose definition, all six charts, every signal, 1.5R held to 15:55, stop beyond the flag,
// 2 pts cost). 24 features (correlated with the net R multiple) describe the formation (pole, flag, breakout) and the context. For each feature and side: Spearman rank
// correlation with the trade's net result in 2010-18 (IS) and 2019-26 (OOS), and win % / pts by IS-defined quintiles. A feature is
// CONSISTENT when both correlations have the same sign and |rho| >= 0.02. PART C — a quality score built on IS only (sum of z-scored
// consistent features, signed by their IS correlation), read by score quintile in 2019-26 and 2025-26.
#include "research/meta.hpp"
#include <cstdio>
#include <algorithm>
using namespace lab;
static const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {535, 925, 1315}; static const char* SN[3] = {"Asia", "London", "NY"};
enum { F_TF, F_POLE, F_POLEER, F_POLEDIR, F_FLAGRATIO, F_RETRACE, F_SLOPE, F_CHAN, F_VOLR, F_BRKPOS, F_BRKBODY, F_BRKDIST, F_RISK, F_ST, F_MOVE, F_WITH, F_VWSIDE, F_VWDIST, F_TIME, F_GAP, F_PREV, F_VIX, F_D10, F_NTH, NF };
static const char* FN[NF] = {"chart minutes", "pole size (ATR)", "pole straightness (net / path)", "pole bars closing with the move", "flag range / pole", "flag pullback depth / pole",
  "flag drift against the pole (+ = counter-trend)", "flag lower highs (bull) / higher lows (bear)", "flag volume / pole volume", "breakout close position in its bar", "breakout bar body / range",
  "breakout distance beyond flag (R)", "stop size (ATR)", "session straightness", "session move (ATR)", "with the day's side of the open", "on the trade's side of VWAP", "distance from VWAP (ATR, signed)",
  "minutes since 09:30", "gap agrees", "prior day agrees", "VIX (prior close)", "with the 10:00 direction (after 10:00)", "flags already seen today"};
struct Sig { int date, side, era; double x[NF], pts, r; bool win; };
struct B { double o, h, l, c, v; int k; };
static double spearman(std::vector<std::pair<double, double>> v) { size_t n = v.size(); if (n < 30) return NAN;
  auto rank = [&](int which) { std::vector<size_t> id(n); for (size_t i = 0; i < n; ++i) id[i] = i; std::sort(id.begin(), id.end(), [&](size_t a, size_t b) { return (which ? v[a].second : v[a].first) < (which ? v[b].second : v[b].first); });
    std::vector<double> r(n); for (size_t i = 0; i < n;) { size_t j = i; double val = which ? v[id[i]].second : v[id[i]].first; while (j < n && (which ? v[id[j]].second : v[id[j]].first) == val) ++j; double avg = (i + j - 1) / 2.0; for (size_t q = i; q < j; ++q) r[id[q]] = avg; i = j; } return r; };
  auto a = rank(0), b = rank(1); double ma = (n - 1) / 2.0, sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - ma); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - ma) * (b[i] - ma); }
  return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : NAN; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  double cnt[3][6][2] = {}; double days = 0; std::vector<Sig> sig;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; days++; const double A = d.atr; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    const int prevDir = (!std::isnan(d.pc) && !std::isnan(d.pc2)) ? (d.pc > d.pc2 ? 1 : d.pc < d.pc2 ? -1 : 0) : 0;
    for (int se = 0; se < 3; ++se) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) I.push_back(i); } if (I.size() < 60) continue;
      const size_t n = I.size(); std::vector<double> ST(n), MV(n), VW(n); std::vector<int> D(n); double so = s.o[I[0]];
      { double hi = -1e18, lo = 1e18, pv = 0, vv = 0; for (size_t k = 0; k < n; ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
          VW[k] = vv > 0 ? pv / vv : s.c[i]; ST[k] = hi > lo ? std::fabs(s.c[i] - so) / (hi - lo) : 0; MV[k] = (s.c[i] - so) / A; D[k] = s.c[i] > so ? 1 : s.c[i] < so ? -1 : 0; } }
      const int gapDir = se == 2 && !std::isnan(d.pc) ? (so > d.pc ? 1 : so < d.pc ? -1 : 0) : 0;
      int d10 = 0; if (se == 2) for (size_t k = 0; k < n; ++k) if (tday_min(s.t[I[k]]) == hm_to_tday(9, 59)) d10 = D[k];
      int nth = 0;
      for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<B> b;
        for (size_t k = 0; k < n; ++k) { int i = I[k], g = (tday_min(s.t[i]) - S0[se]) / tf;
          if (b.empty() || (tday_min(s.t[I[b.back().k]]) - S0[se]) / tf != g) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (double)s.v[i], (int)k});
          else { B& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.v += s.v[i]; x.k = k; } }
        for (int q = 11; q < (int)b.size(); ++q) { int p0 = q - 11, p1 = q - 6; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < 0.10 * A) continue;
          double fh = -1e18, fl = 1e18, fv = 0, pv2 = 0; for (int r = q - 5; r < q; ++r) { fh = std::max(fh, b[r].h); fl = std::min(fl, b[r].l); fv += b[r].v; } if (fh - fl > 0.8 * ap) continue;
          double c = b[q].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue; size_t k = b[q].k; int tm = tday_min(s.t[I[k]]);
          if (tm < S0[se] + 15 || tm >= SE[se] - 30) continue;
          cnt[se][ti][0]++; if (ap >= 0.20 * A && fh - fl <= 0.6 * ap) cnt[se][ti][1]++;
          if (se != 2) continue;
          double stop = fd > 0 ? fl - 0.25 : fh + 0.25, R = fd * (c - stop); if (R <= 0.25 || R > 0.6 * A) continue;
          Sig z; z.date = d.date; z.side = fd > 0 ? 0 : 1; z.era = era;
          double path = 0; int dirBars = 0; for (int r = p0; r <= p1; ++r) { path += std::fabs(b[r].c - b[r].o); if (fd * (b[r].c - b[r].o) > 0) dirBars++; pv2 += b[r].v; }
          int chan = 0; for (int r = q - 4; r < q; ++r) if (fd > 0 ? b[r].h < b[r - 1].h : b[r].l > b[r - 1].l) chan++;
          double br = b[q].h - b[q].l;
          z.x[F_TF] = tf; z.x[F_POLE] = ap / A; z.x[F_POLEER] = path > 0 ? ap / path : 0; z.x[F_POLEDIR] = dirBars / 6.0; z.x[F_FLAGRATIO] = (fh - fl) / ap;
          z.x[F_RETRACE] = (fd > 0 ? b[p1].c - fl : fh - b[p1].c) / ap; z.x[F_SLOPE] = -fd * (b[q - 1].c - b[q - 5].o) / ap; z.x[F_CHAN] = chan / 4.0; z.x[F_VOLR] = pv2 > 0 ? (fv / 5) / (pv2 / 6) : 1;
          z.x[F_BRKPOS] = br > 0 ? (fd > 0 ? (c - b[q].l) / br : (b[q].h - c) / br) : 0.5; z.x[F_BRKBODY] = br > 0 ? fd * (c - b[q].o) / br : 0; z.x[F_BRKDIST] = (fd * (c - (fd > 0 ? fh : fl))) / R;
          z.x[F_RISK] = R / A; z.x[F_ST] = ST[k]; z.x[F_MOVE] = fd * MV[k]; z.x[F_WITH] = D[k] == fd; z.x[F_VWSIDE] = fd * (c - VW[k]) > 0; z.x[F_VWDIST] = fd * (c - VW[k]) / A;
          z.x[F_TIME] = tm - hm_to_tday(9, 30); z.x[F_GAP] = gapDir == fd; z.x[F_PREV] = prevDir == fd; z.x[F_VIX] = std::isnan(d.vix) ? 18 : d.vix; z.x[F_D10] = tm >= hm_to_tday(10, 0) && d10 == fd; z.x[F_NTH] = nth++;
          double e = c, st = stop, tg = e + fd * 1.5 * R, px = NAN; for (size_t kk = k + 1; kk < n; ++kk) { int j = I[kk]; if (fd > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; break; } if (fd > 0 ? s.h[j] >= tg : s.l[j] <= tg) { px = tg; break; } }
          if (std::isnan(px)) px = s.c[I[n - 1]]; double net = fd * (px - e) - 2.0 * A / 320; z.pts = net * 320 / A; z.r = net / R; z.win = net > 0; sig.push_back(z); } } } }
  printf("PART A — flags with NO filters, trades a day (every signal; 'loose' = pole 6 bars >= 0.10 ATR, flag 5 bars <= 0.8 x pole; 'standard' = pole >= 0.20, flag <= 0.6). %.0f days.\n", days);
  printf("  %-7s", "session"); for (int t : TFS) printf("   %2dm loose / std", t); printf("   all charts loose / std\n");
  for (int se = 2; se >= 0; --se) { printf("  %-7s", SN[se]); double a = 0, b2 = 0; for (int ti = 0; ti < 6; ++ti) { printf("   %5.2f / %5.2f   ", cnt[se][ti][0] / days, cnt[se][ti][1] / days); a += cnt[se][ti][0]; b2 += cnt[se][ti][1]; } printf("   %5.2f / %5.2f\n", a / days, b2 / days); }
  printf("\nPART B — feature selection, NY flags (loose, all charts, every signal, 1.5R to close). rho = Spearman with the net R multiple (profit per unit of risk; sizing is by risk, so R is what pays). Quintiles show win %% and mean net R. IS 2010-18, OOS 2019-26.\n");
  std::vector<int> keep[2]; std::vector<double> rhoIS[2];
  for (int side = 0; side < 2; ++side) {
    std::vector<Sig*> is, oos; for (auto& z : sig) if (z.side == side) (z.era == 0 ? is : oos).push_back(&z);
    double wI = 0, pI = 0, wO = 0, pO = 0; for (auto* z : is) { wI += z->win; pI += z->pts; } for (auto* z : oos) { wO += z->win; pO += z->pts; }
    printf("\n== %s flags: IS n %zu (WR %.1f%%, %+.2f pts) · OOS n %zu (WR %.1f%%, %+.2f pts) — pts = NQ points at today's range\n", side ? "BEAR" : "BULL", is.size(), 100 * wI / is.size(), pI / is.size(), oos.size(), 100 * wO / oos.size(), pO / oos.size());
    struct Rk { int f; double ri, ro; }; std::vector<Rk> rk; rhoIS[side].assign(NF, 0);
    for (int f = 0; f < NF; ++f) { std::vector<std::pair<double, double>> a, b2; for (auto* z : is) a.push_back({z->x[f], z->r}); for (auto* z : oos) b2.push_back({z->x[f], z->r}); double ri = spearman(a), ro = spearman(b2); rk.push_back({f, ri, ro}); rhoIS[side][f] = ri; }
    std::sort(rk.begin(), rk.end(), [](const Rk& a, const Rk& b) { auto sc = [](const Rk& r) { return (r.ri > 0) == (r.ro > 0) ? std::min(std::fabs(r.ri), std::fabs(r.ro)) : -std::max(std::fabs(r.ri), std::fabs(r.ro)); }; return sc(a) > sc(b); });
    printf("  %-48s %8s %8s  %s\n", "feature (ranked by consistent strength)", "rho IS", "rho OOS", "verdict");
    for (auto& r : rk) { bool cons = (r.ri > 0) == (r.ro > 0) && std::fabs(r.ri) >= 0.02 && std::fabs(r.ro) >= 0.02; if (cons) keep[side].push_back(r.f);
      printf("  %-48s %+8.3f %+8.3f  %s\n", FN[r.f], r.ri, r.ro, cons ? (r.ri > 0 ? "CONSISTENT: higher is better" : "CONSISTENT: lower is better") : ""); }
    // quintile detail for the top 6 consistent features
    printf("  quintiles (cut on IS) — win %% and pts in IS | OOS, lowest → highest:\n");
    for (size_t t = 0; t < std::min<size_t>(6, keep[side].size()); ++t) { int f = keep[side][t]; std::vector<double> v; for (auto* z : is) v.push_back(z->x[f]); std::sort(v.begin(), v.end());
      double cut[4]; for (int q = 0; q < 4; ++q) cut[q] = v[(q + 1) * v.size() / 5]; auto bin = [&](double x) { int q = 0; while (q < 4 && x > cut[q]) q++; return q; };
      double n1[5] = {}, w1[5] = {}, p1[5] = {}, n2[5] = {}, w2[5] = {}, p2[5] = {}; for (auto* z : is) { int q = bin(z->x[f]); n1[q]++; w1[q] += z->win; p1[q] += z->r; } for (auto* z : oos) { int q = bin(z->x[f]); n2[q]++; w2[q] += z->win; p2[q] += z->r; }
      printf("    %-46s", FN[f]); for (int q = 0; q < 5; ++q) printf(" | %4.1f%% %+5.2fR / %4.1f%% %+5.2fR", n1[q] ? 100 * w1[q] / n1[q] : 0, n1[q] ? p1[q] / n1[q] : 0, n2[q] ? 100 * w2[q] / n2[q] : 0, n2[q] ? p2[q] / n2[q] : 0); printf("\n"); }
    // PART C score
    std::vector<double> mu(NF, 0), sd(NF, 1); for (int f : keep[side]) { double m = 0, q2 = 0; for (auto* z : is) { m += z->x[f]; q2 += z->x[f] * z->x[f]; } m /= is.size(); mu[f] = m; sd[f] = std::sqrt(std::max(1e-12, q2 / is.size() - m * m)); }
    auto score = [&](const Sig* z) { double sc = 0; for (int f : keep[side]) sc += rhoIS[side][f] * (z->x[f] - mu[f]) / sd[f]; return sc; };
    std::vector<double> sv; for (auto* z : is) sv.push_back(score(z)); std::sort(sv.begin(), sv.end()); double cut[4]; for (int q = 0; q < 4; ++q) cut[q] = sv[(q + 1) * sv.size() / 5];
    double n2[5] = {}, w2[5] = {}, p2[5] = {}, n3[5] = {}, w3[5] = {}, p3[5] = {}, nI[5] = {}, pIq[5] = {};
    for (auto* z : is) { double scv = score(z); int q = 0; while (q < 4 && scv > cut[q]) q++; nI[q]++; pIq[q] += z->r; }
    for (auto* z : oos) { double scv = score(z); int q = 0; while (q < 4 && scv > cut[q]) q++; n2[q]++; w2[q] += z->win; p2[q] += z->r; if (z->date >= 20250101) { n3[q]++; w3[q] += z->win; p3[q] += z->r; } }
    printf("  PART C quality score (%zu consistent features, built on 2010-18) — by score quintile, low → high:\n", keep[side].size());
    printf("    2010-18 R  :"); for (int q = 0; q < 5; ++q) printf("  %+6.3f", pIq[q] / std::max(1.0, nI[q])); printf("\n");
    printf("    2019-26    :"); for (int q = 0; q < 5; ++q) printf("  %4.1f%% %+6.3fR (%4.2f/day)", 100 * w2[q] / std::max(1.0, n2[q]), p2[q] / std::max(1.0, n2[q]), n2[q] / (days * 0.45)); printf("\n");
    printf("    2025-26    :"); for (int q = 0; q < 5; ++q) printf("  %4.1f%% %+6.3fR (n %3.0f)     ", 100 * w3[q] / std::max(1.0, n3[q]), p3[q] / std::max(1.0, n3[q]), n3[q]); printf("\n"); }
  return 0;
}
