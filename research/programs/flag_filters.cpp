// 2026-10-19 (founder: "add the best filter that would give us a higher WR … on 1 min 2 min 3 min 4 min … on all basically").
// Base = flags WITH the session's day on a straight path (price on the session's side of its open and VWAP, straightness >= 0.55),
// pole 6 bars >= 0.20 ATR, flag 5 bars <= 0.6 x pole, close beyond the flag, stop beyond the flag + 0.25, target 1.5R held to the session
// end, entries from session open + 15 min to session end − 60 min. Charts 1/2/3/4/5/15 min, sessions Asia / London / NY. Every signal
// is scored on its own (no one-at-a-time) so a filter does not change which other trades exist.
// Pre-declared filters (none tuned): session third; straightness >= 0.70; session move >= 0.5 ATR; pole >= 0.30 ATR; flag <= 0.4 x pole;
// pullback <= 38 % of the pole; flag volume < pole volume; breakout-bar volume > 1.5 x flag average; prior day's direction agrees; gap
// agrees (NY); price on the trade's side of a 60-minute EMA; within 0.3 ATR of VWAP; VIX >= 20; VIX < 20; first flag of the session.
// A filter COUNTS when it raises the win rate by >= 2 points in BOTH 2010-18 and 2019-24. Best pair chosen on 2010-18 only.
// WIN = net > 0 after 2 pts cost. Control: random minutes with the same context, same stop size, same filters where they apply.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
#include <algorithm>
using namespace lab;
static const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {535, 925, 1315};
static const char* SN[3] = {"Asia", "London", "NY"};
enum { F_T1, F_T2, F_T3, F_ST70, F_MOVE, F_POLE, F_TIGHT, F_SHALLOW, F_VDRY, F_VBRK, F_PREV, F_GAP, F_EMA, F_NEARVW, F_VIXHI, F_VIXLO, F_FIRST, NF };
static const char* FN[NF] = {"first third of the session", "middle third", "last third", "straightness >= 0.70", "session move >= 0.5 ATR", "pole >= 0.30 ATR",
  "flag <= 0.4 x pole", "pullback <= 38 % of pole", "flag volume < pole volume", "breakout volume > 1.5x flag", "prior day agrees", "gap agrees",
  "60-min EMA on the trade's side", "within 0.3 ATR of VWAP", "VIX >= 20", "VIX < 20", "first flag of the session"};
static const bool CTXF[NF] = {1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0};   // applies to the random-timing control too
struct Sig { int date, era; bool win; double pts; bool f[NF]; };
struct St { double n = 0, w = 0, p = 0; void add(const Sig& s) { n++; if (s.win) w++; p += s.pts; } double wr() const { return n ? 100 * w / n : NAN; } double pt() const { return n ? p / n : NAN; } };
struct B { double o, h, l, c, v; int k; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(57); static std::vector<Sig> SG[3][6], CT[3][6]; double nDays[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; const double A = d.atr; nDays[0]++;
    const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    const int prevDir = (!std::isnan(d.pc) && !std::isnan(d.pc2)) ? (d.pc > d.pc2 ? 1 : d.pc < d.pc2 ? -1 : 0) : 0;
    for (int se = 0; se < 3; ++se) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) I.push_back(i); } if (I.size() < 60) continue;
      const int gapDir = se == 2 && !std::isnan(d.pc) ? (s.o[I[0]] > d.pc ? 1 : s.o[I[0]] < d.pc ? -1 : 0) : 0;
      std::vector<float> ST(I.size()), MV(I.size()), VWD(I.size()); std::vector<signed char> D(I.size()), VS(I.size()), ES(I.size());
      double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0, ema = s.c[I[0]];
      for (size_t k = 0; k < I.size(); ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; ema += 2.0 / 61 * (c - ema); ST[k] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; MV[k] = std::fabs(c - so) / A; VWD[k] = std::fabs(c - vw) / A;
        D[k] = c > so ? 1 : c < so ? -1 : 0; VS[k] = c > vw ? 1 : c < vw ? -1 : 0; ES[k] = c > ema ? 1 : c < ema ? -1 : 0; }
      const int len = SE[se] - S0[se]; auto third = [&](int tm) { int x = (tm - S0[se]) * 3 / len; return std::min(2, std::max(0, x)); };
      auto exitR = [&](size_t k0, int dir, double R, bool& win, double& pts) { double e = s.c[I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN;
        for (size_t k = k0 + 1; k < I.size(); ++k) { int i = I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } }
        if (std::isnan(px)) px = s.c[I.back()]; double net = dir * (px - e) - 2.0 * A / 320; win = net > 0; pts = net * 320 / A; };
      auto ctxOK = [&](size_t k) { int tm = tday_min(s.t[I[k]]); return D[k] && VS[k] == D[k] && ST[k] >= 0.55 && tm >= S0[se] + 15 && tm < SE[se] - 60; };
      auto ctxF = [&](size_t k, Sig& z) { int tm = tday_min(s.t[I[k]]), dd = D[k]; int th = third(tm);
        z.f[F_T1] = th == 0; z.f[F_T2] = th == 1; z.f[F_T3] = th == 2; z.f[F_ST70] = ST[k] >= 0.70; z.f[F_MOVE] = MV[k] >= 0.5; z.f[F_PREV] = prevDir == dd; z.f[F_GAP] = gapDir == dd;
        z.f[F_EMA] = ES[k] == dd; z.f[F_NEARVW] = VWD[k] <= 0.3; z.f[F_VIXHI] = d.vix >= 20; z.f[F_VIXLO] = d.vix < 20; };
      for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<B> b;
        for (size_t k = 0; k < I.size(); ++k) { int i = I[k], g = (tday_min(s.t[i]) - S0[se]) / tf;
          if (b.empty() || (tday_min(s.t[I[b.back().k]]) - S0[se]) / tf != g) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (double)s.v[i], (int)k});
          else { B& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.v += s.v[i]; x.k = k; } }
        int nflags = 0; std::vector<double> Rs;
        for (int q = 11; q < (int)b.size(); ++q) { int p0 = q - 11, p1 = q - 6; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < 0.20 * A) continue;
          double fh = -1e18, fl = 1e18, fv = 0, pvv = 0; for (int r = q - 5; r < q; ++r) { fh = std::max(fh, b[r].h); fl = std::min(fl, b[r].l); fv += b[r].v; } for (int r = p0; r <= p1; ++r) pvv += b[r].v;
          if (fh - fl > 0.6 * ap) continue; double c = b[q].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue;
          size_t k = b[q].k; if (!ctxOK(k) || D[k] != fd) continue; double stop = fd > 0 ? fl - 0.25 : fh + 0.25, R = fd * (c - stop); if (R <= 0.25 || R > 0.6 * A) continue;
          Sig z; z.date = d.date; z.era = era; ctxF(k, z); exitR(k, fd, R, z.win, z.pts);
          double retr = (fd > 0 ? b[p1].c - fl : fh - b[p1].c) / ap;
          z.f[F_POLE] = ap >= 0.30 * A; z.f[F_TIGHT] = fh - fl <= 0.4 * ap; z.f[F_SHALLOW] = retr <= 0.38; z.f[F_VDRY] = fv / 5 < pvv / 6; z.f[F_VBRK] = b[q].v > 1.5 * fv / 5; z.f[F_FIRST] = nflags == 0;
          nflags++; Rs.push_back(R); SG[se][ti].push_back(z); }
        // control: one random context minute per flag found today, same R
        std::vector<size_t> cand; for (size_t k = 0; k < I.size(); ++k) if (ctxOK(k)) cand.push_back(k);
        for (double R : Rs) { if (cand.empty()) break; size_t k = cand[std::uniform_int_distribution<size_t>(0, cand.size() - 1)(rng)]; Sig z; z.date = d.date; z.era = era; ctxF(k, z);
          for (int f = 0; f < NF; ++f) if (!CTXF[f]) z.f[f] = true; exitR(k, D[k], R, z.win, z.pts); CT[se][ti].push_back(z); } } } }
  const double yrs = nDays[0] / 252.0;
  printf("Flag filters for a higher win rate — NQ %.1f yrs, base = flags WITH the session's day on a straight path, 1.5R to session end.\n"
         "WR = win rate (net > 0 after 2 pts). Columns: WR 2010-18 / 2019-24 / 2025-26 | trades/day | net pts 2019-26 | control WR 2019-26 (random timing, same filter) .\n"
         "✓ = raises WR by >= 2 points in BOTH 2010-18 and 2019-24.\n", yrs);
  for (int se = 2; se >= 0; --se) for (int ti = 0; ti < 6; ++ti) { auto& v = SG[se][ti]; auto& c = CT[se][ti]; if (v.size() < 150) continue;
    auto stat = [&](const std::vector<Sig>& x, int f1, int f2, St* e) { for (auto& z : x) { if (f1 >= 0 && !z.f[f1]) continue; if (f2 >= 0 && !z.f[f2]) continue; e[z.era].add(z); } };
    St b0[3]; stat(v, -1, -1, b0); St cb[3]; stat(c, -1, -1, cb);
    printf("\n== %s %dm — base: WR %4.1f / %4.1f / %4.1f  | %.2f/day | pts %+5.1f | control WR %4.1f\n", SN[se], TFS[ti], b0[0].wr(), b0[1].wr(), b0[2].wr(), (b0[0].n + b0[1].n + b0[2].n) / nDays[0],
           (b0[1].p + b0[2].p) / std::max(1.0, b0[1].n + b0[2].n), (cb[1].w + cb[2].w) * 100 / std::max(1.0, cb[1].n + cb[2].n));
    std::vector<int> good; for (int f = 0; f < NF; ++f) { St e[3]; stat(v, f, -1, e); if (e[0].n < 40 || e[1].n < 30) continue; St ce[3]; stat(c, f, -1, ce);
      bool ok = e[0].wr() >= b0[0].wr() + 2 && e[1].wr() >= b0[1].wr() + 2; if (ok) good.push_back(f);
      printf("   %s %-32s %4.1f / %4.1f / %4.1f  | %.2f/day | pts %+5.1f | control %s\n", ok ? "✓" : " ", FN[f], e[0].wr(), e[1].wr(), e[2].n >= 10 ? e[2].wr() : NAN, (e[0].n + e[1].n + e[2].n) / nDays[0],
             (e[1].p + e[2].p) / std::max(1.0, e[1].n + e[2].n), CTXF[f] ? (std::to_string((int)std::lround((ce[1].w + ce[2].w) * 100 / std::max(1.0, ce[1].n + ce[2].n))) + "%").c_str() : "n/a"); }
    // best pair chosen on 2010-18 win rate (both members must be ✓ filters, n >= 40 in 2010-18)
    int bf1 = -1, bf2 = -1; double bw = -1; for (size_t a = 0; a < good.size(); ++a) for (size_t bb = a + 1; bb < good.size(); ++bb) { St e[3]; stat(v, good[a], good[bb], e); if (e[0].n >= 40 && e[0].wr() > bw) { bw = e[0].wr(); bf1 = good[a]; bf2 = good[bb]; } }
    if (bf1 >= 0) { St e[3]; stat(v, bf1, bf2, e); St ce[3]; stat(c, bf1, bf2, ce);
      printf("   ★ best pair (picked on 2010-18): %s + %s → WR %4.1f / %4.1f / %4.1f | %.2f/day | pts 2019-26 %+5.1f | control %4.1f%%\n", FN[bf1], FN[bf2], e[0].wr(), e[1].wr(), e[2].n >= 10 ? e[2].wr() : NAN,
             (e[0].n + e[1].n + e[2].n) / nDays[0], (e[1].p + e[2].p) / std::max(1.0, e[1].n + e[2].n), (ce[1].w + ce[2].w) * 100 / std::max(1.0, ce[1].n + ce[2].n)); } }
  return 0;
}
