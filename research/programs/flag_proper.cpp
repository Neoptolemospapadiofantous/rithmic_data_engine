// 2026-10-19 (founder: "we are not calculating flags correctly" — a screenshot shows the fixed 6+5-bar rule labelling a few tiny bars at
// the bottom of a big move as pole and flag). A flag built from structure, on each chart (bars from the session open), bull (bear mirror):
//   POLE  = one impulse leg from the swing low L (bar l) to the swing high H (bar h), 3-15 bars; H − L >= 4 x the average bar range of the
//           session so far on that chart; at least 60 % of the pole's bars close up; L is the lowest low of the pole.
//   FLAG  = the 3-12 bars after h: no bar makes a new high above H; the pullback (H − flag low) is 15-50 % of the pole; the flag's range is
//           <= 50 % of the pole; the flag drifts sideways or against the pole (least-squares slope of the flag's closes <= 0 for a bull flag,
//           measured per bar, with a tolerance of +5 % of the pole across the whole flag).
//   BREAKOUT = a bar closes above the flag's high. Entry at that close; stop 1 tick below the flag low.
// Context (as the trade log): the session's direction on a straight path — price on its side of the session open and VWAP, straightness
// >= 0.55 — or no context. Exits: trail the previous bar after +1R, or the measured move (pole length) from the entry. One at a time,
// <= 5 a day, 2 pts cost (scaled to the day's range). argv[1] = "list" prints the trade list (context on, trailing exit, 2023-10-16 on).
#include "research/meta.hpp"
#include <cstdio>
#include <cstring>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; int k; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int er, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[er] += pts; ne[er]++; } };
static void hm(long ep, char* d, char* t) { time_t e = ep; struct tm x; gmtime_r(&e, &x); strftime(d, 16, "%Y-%m-%d", &x); strftime(t, 8, "%H:%M", &x); }
int main(int argc, char** argv) {
  const bool LIST = argc > 1 && !strcmp(argv[1], "list");
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {540, 930, 1315}; const char* SN[3] = {"Asia", "London", "NY"};
  struct DI { std::vector<int> I, D; std::vector<char> ctx; int era, se, date; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    for (int se = 0; se < 3; ++se) { DI x; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) x.I.push_back(i); } if (x.I.size() < 60) continue;
      const size_t n = x.I.size(); x.D.resize(n); x.ctx.resize(n); double so = s.o[x.I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < n; ++k) { int i = x.I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
        x.D[k] = c > so ? 1 : c < so ? -1 : 0; int tm = tday_min(s.t[i]); x.ctx[k] = x.D[k] && (c - vw) * x.D[k] > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55; }
      x.era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; x.se = se; x.date = d.date; x.A = d.atr; days.push_back(std::move(x)); } }
  auto exitF = [&](const DI& x, size_t k0, double e, int D, double R, double tgtPts, int tf, int mode, double& pts, int& why) -> size_t { const auto& I = x.I; const size_t n = I.size(); double st = e - D * R, tg = e + D * tgtPts, px = NAN, bh = -1e18, bl = 1e18, ph = NAN, pl = NAN; size_t kx = n - 1; bool armed = false; why = 2;
    for (size_t k = k0 + 1; k < n; ++k) { int j = I[k], tm = tday_min(s.t[j]); if (D > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = k; why = armed ? 1 : 0; break; }
      if (mode == 1 && (D > 0 ? s.h[j] >= tg : s.l[j] <= tg)) { px = tg; kx = k; why = 3; break; }
      bh = std::max(bh, (double)s.h[j]); bl = std::min(bl, (double)s.l[j]);
      if ((tm + 1 - S0[x.se]) % tf == 0) { ph = bh; pl = bl; bh = -1e18; bl = 1e18; if (mode == 0 && armed) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } }
      if (mode == 0 && !armed && D * (s.c[j] - e) >= R) { armed = true; if (!std::isnan(pl)) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } } }
    if (std::isnan(px)) px = s.c[I[n - 1]]; pts = D * (px - e); return kx; };
  std::mt19937 rng(137); const char* RSN[4] = {"stop", "trailing stop", "session end", "target"};
  if (LIST) printf("session,chart,side,trade_day,entry_date,entry_time,entry,stop,risk_pts,exit_date,exit_time,exit,pts,R,exit_reason,pole_start,pole_top,flag_start,flag_high,flag_low,pole_pts\n");
  else printf("Structural flags (impulse pole, 3-12 bar flag pulling back 15-50 %%, no new extreme, flat or counter-trend drift, breakout) — NQ 2010-07 → 2026-10.\n"
              "Cells: trades/day | win %% | net pts per trade (raw NQ points, 2 pts cost scaled to the day) (R) | 2025-26 pts (n) | same-context random control pts.\n");
  for (int ctxOn = 1; ctxOn >= 0; --ctxOn) for (int mode = 0; mode < 2; ++mode) { if (LIST && !(ctxOn && mode == 0)) continue;
    if (!LIST) printf("\n######## %s, exit %s\n", ctxOn ? "with the session's direction on a straight path" : "no context (any direction, any time)", mode ? "measured move" : "trail previous bar after +1R");
    for (int se = 2; se >= 0; --se) for (int tf : TFS) { St a; std::vector<double> RA, TA; double ndays = 0;
      for (const DI& x : days) { if (x.se != se) continue; if (LIST && x.date < 20231016) continue; ndays++; const auto& I = x.I; const size_t n = I.size(); std::vector<FB> b; size_t busy = 0; bool have = false; int nd = 0;
        for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
          if ((tm - S0[se]) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
          if ((tm + 1 - S0[se]) % tf || (have && k <= busy) || nd >= 5) continue; if (tm < S0[se] + 15 || tm >= SE[se] - 30) continue;
          if (ctxOn && !x.ctx[k]) continue; int q = b.size() - 1; if (q < 7) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= (q + 1);
          bool done = false;
          for (int D : {1, -1}) { if (done) break; if (ctxOn && D != x.D[k]) continue;
            for (int F = 3; F <= 12 && !done; ++F) { int h = q - 1 - F; if (h < 3) break;
              double H = D > 0 ? b[h].h : b[h].l; bool ok = true; for (int r = h + 1; r < q; ++r) if (D > 0 ? b[r].h > H : b[r].l < H) { ok = false; break; } if (!ok) continue;
              int l = -1; double L = D > 0 ? 1e18 : -1e18; for (int r = std::max(0, h - 15); r <= h - 3; ++r) if (D > 0 ? b[r].l < L : b[r].h > L) { L = D > 0 ? b[r].l : b[r].h; l = r; }
              if (l < 0) continue; for (int r = l; r <= h; ++r) if (D > 0 ? (b[r].l < L || b[r].h > H) : (b[r].h > L || b[r].l < H)) ok = false; if (!ok) continue;
              double M = D * (H - L); if (M < 4 * ar || M <= 0) continue;
              int up = 0; for (int r = l; r <= h; ++r) if (D * (b[r].c - b[r].o) > 0) up++; if (up < 0.6 * (h - l + 1)) continue;
              double FH = -1e18, FL = 1e18; for (int r = h + 1; r < q; ++r) { FH = std::max(FH, b[r].h); FL = std::min(FL, b[r].l); }
              double pull = D > 0 ? H - FL : FH - H; pull = std::fabs(pull); if (pull < 0.15 * M || pull > 0.5 * M || FH - FL > 0.5 * M) continue;
              double sx = 0, sy = 0, sxx = 0, sxy = 0; int nn = 0; for (int r = h + 1; r < q; ++r) { double xx = r - h, yy = b[r].c; sx += xx; sy += yy; sxx += xx * xx; sxy += xx * yy; nn++; }
              double slope = (nn * sxy - sx * sy) / std::max(1e-9, nn * sxx - sx * sx); if (D * slope * nn > 0.05 * M) continue;   // flat or against the pole
              double edge = D > 0 ? FH : FL, c = b[q].c; if (D > 0 ? !(c > edge) : !(c < edge)) continue;
              double stop = D > 0 ? FL - 0.25 : FH + 0.25, R = D * (c - stop); if (!(R > 0.25)) continue;
              double pts; int why; size_t kx = exitF(x, k, c, D, R, M, tf, mode, pts, why); double net = pts - 2.0 * x.A / 320; a.add(x.era, net > 0, net, net / R); RA.push_back(R / x.A); TA.push_back(M / x.A); busy = kx; have = true; nd++; done = true;
              if (LIST) { char ed[16], et[8], xd[16], xt[8], pd[16], pt[8], td[16], tt[8], fd[16], ft[8]; hm(s.t[I[k]] + 60, ed, et); hm(s.t[I[kx]] + 60, xd, xt); hm(s.t[I[b[l].k]] - 60 * (tf - 1), pd, pt); hm(s.t[I[b[h].k]] - 60 * (tf - 1), td, tt); hm(s.t[I[b[h + 1].k]] - 60 * (tf - 1), fd, ft);
                printf("%s,%d,%s,%d,%s,%s,%.2f,%.2f,%.2f,%s,%s,%.2f,%.2f,%.2f,%s,%s %s,%s %s,%s %s,%.2f,%.2f,%.2f\n", SN[se], tf, D > 0 ? "bull" : "bear", x.date, ed, et, c, stop, R, xd, xt, c + D * pts, pts, pts / R, RSN[why], pd, pt, td, tt, fd, ft, FH, FL, M); } } } } }
      if (LIST) continue;
      double cp = 0, cn = 0; std::vector<const DI*> pool; for (auto& x : days) if (x.se == se) pool.push_back(&x);
      for (int rep = 0; rep < 5; ++rep) for (size_t ti = 0; ti < RA.size(); ++ti) { const DI& x = *pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)]; std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) { int tm = tday_min(s.t[x.I[k]]); if ((!ctxOn || x.ctx[k]) && x.D[k] && tm >= S0[se] + 15 && tm < SE[se] - 30) cd.push_back(k); } if (cd.empty()) continue;
          size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; int dir = ctxOn ? x.D[k0] : ((rng() & 1) ? 1 : -1); double pts; int why; exitF(x, k0, s.c[x.I[k0]], dir, RA[ti] * x.A, TA[ti] * x.A, tf, mode, pts, why); cp += pts - 2.0 * x.A / 320; cn++; }
      printf("  %-6s %2dm  %5.2f/day | %4.1f%% | %+7.2f (%+.3fR) | 2025-26 %+7.2f (%3.0f) | control %+7.2f\n", SN[se], tf, a.n / ndays, 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n), a.r / std::max(1.0, a.n), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2], cp / std::max(1.0, cn)); } }
  return 0;
}
