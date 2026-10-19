// 2026-10-19 (founder: "the flag entry is so bad, it should come from an ema touch or something"). Flag entry on an EMA touch inside the
// pullback instead of the breakout close. Context as the trade log: the session's direction, price on its side of the session open and
// VWAP, session straightness >= 0.55. On each chart (1/2/3/4/5/15 min, bars from the session open), at a bar close q:
//   POLE   = 6 bars ending at bar h with close[h] − open[h−5] in the trade's direction; H = high of h is the highest high of bars h−5 .. q
//            (no new high since — the market is pulling back), 1 <= q − h <= 8.
//   PULL   = lowest low PL of bars h+1 .. q; pullback (H − PL) <= 0.6 x (H − pole low).
//   TOUCH  = some bar in h+1 .. q has low <= the EMA (9 or 20, of closes on that chart, from the session open).
//   ENTRY  = bar q closes above the previous bar's high and above the EMA (bear: mirror). Enter at that close; stop PL − 1 tick.
// Exits: trail the previous bar of that chart after +1R (no target) or fixed 1.5R; flat at the session end. One at a time, <= 5 a day,
// 2 pts cost. Same-context control = random minutes where the context holds, other days, the trade's stop in ATR, same exit.
// argv[1] = "list" writes the NY/London/Asia trade list (EMA 20, trail) to stdout instead of the summary.
#include "research/meta.hpp"
#include <cstdio>
#include <cstring>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, e; int k; };
struct St { double n = 0, w = 0, p = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int er, bool win, double pts) { n++; if (win) w++; p += pts; pe[er] += pts; ne[er]++; } };
static void hm(long ep, char* d, char* t) { time_t e = ep; struct tm x; gmtime_r(&e, &x); strftime(d, 16, "%Y-%m-%d", &x); strftime(t, 8, "%H:%M", &x); }
int main(int argc, char** argv) {
  const bool LIST = argc > 1 && !strcmp(argv[1], "list");
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {540, 930, 1315}; const char* SN[3] = {"Asia", "London", "NY"};
  struct DI { std::vector<int> I; std::vector<int> D; std::vector<char> ctx; int era, date, se; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    for (int se = 0; se < 3; ++se) { DI x; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) x.I.push_back(i); } if (x.I.size() < 60) continue;
      const size_t n = x.I.size(); x.D.resize(n); x.ctx.resize(n); double so = s.o[x.I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < n; ++k) { int i = x.I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
        x.D[k] = c > so ? 1 : c < so ? -1 : 0; int tm = tday_min(s.t[i]); x.ctx[k] = x.D[k] && (c - vw) * x.D[k] > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55 && tm >= S0[se] + 15 && tm < SE[se] - 30; }
      x.era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; x.date = d.date; x.se = se; x.A = d.atr; days.push_back(std::move(x)); } }
  // exit: mode 0 trail previous bar of chart tf after +1R, mode 1 fixed 1.5R; returns exit index
  auto exitF = [&](const DI& x, size_t k0, int D, double R, int tf, int mode, double& pts, int& why) -> size_t { const auto& I = x.I; const size_t n = I.size(); double e = s.c[I[k0]], st = e - D * R, tg = e + D * 1.5 * R, px = NAN, bh = -1e18, bl = 1e18, ph = NAN, pl = NAN; size_t kx = n - 1; bool armed = false; why = 2;
    for (size_t k = k0 + 1; k < n; ++k) { int j = I[k], tm = tday_min(s.t[j]); if (D > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = k; why = armed ? 1 : 0; break; }
      if (mode == 1 && (D > 0 ? s.h[j] >= tg : s.l[j] <= tg)) { px = tg; kx = k; why = 3; break; }
      bh = std::max(bh, (double)s.h[j]); bl = std::min(bl, (double)s.l[j]);
      if ((tm + 1 - S0[x.se]) % tf == 0) { ph = bh; pl = bl; bh = -1e18; bl = 1e18; if (mode == 0 && armed) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } }
      if (mode == 0 && !armed && D * (s.c[j] - e) >= R) { armed = true; if (!std::isnan(pl)) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } } }
    if (std::isnan(px)) px = s.c[I[n - 1]]; pts = D * (px - e); return kx; };
  std::mt19937 rng(131);
  if (LIST) printf("session,chart,side,trade_day,entry_date,entry_time,entry,stop,risk_pts,exit_date,exit_time,exit,pts,R,exit_reason,pole_start,flag_start,ema,touch_time\n");
  else printf("Flag entry on an EMA touch inside the pullback — NQ, all sessions, one at a time, <= 5 a day, 2 pts cost (pts below = net, raw NQ points).\n"
              "Columns: trades/day | win %% | net pts per trade | 2010-18 / 2019-24 / 2025-26 (n) | same-context random control.\n");
  const char* RS[4] = {"stop", "trailing stop", "session end", "target"};
  for (int ema : {9, 20}) for (int mode = 0; mode < 2; ++mode) { if (LIST && !(ema == 20 && mode == 0)) continue;
    if (!LIST) printf("\n==== EMA %d, exit %s\n", ema, mode ? "fixed 1.5R" : "trail previous bar after +1R");
    for (int se = 2; se >= 0; --se) for (int tf : TFS) { St a; std::vector<double> RA; double ndays = 0;
      for (const DI& x : days) { if (x.se != se) continue; if (LIST && x.date < 20231016) continue; ndays++; const auto& I = x.I; const size_t n = I.size(); std::vector<FB> b; size_t busy = 0; bool have = false; int nd = 0; double em = NAN; const double al = 2.0 / (ema + 1);
        for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
          if ((tm - S0[se]) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], NAN, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
          if ((tm + 1 - S0[se]) % tf) continue;
          FB& cb = b.back(); em = std::isnan(em) ? cb.c : em + al * (cb.c - em); cb.e = em;   // EMA at each completed bar
          if (!x.ctx[k] || (have && k <= busy) || nd >= 5) continue; int q = b.size() - 1, D = x.D[k]; if (q < 8) continue;
          bool done = false;
          for (int h = q - 1; h >= q - 8 && h >= 5 && !done; --h) { if (D * (b[h].c - b[h - 5].o) <= 0) continue;
            double H = D > 0 ? b[h].h : b[h].l; bool ok = true; for (int r = h - 5; r <= q; ++r) if (r != h && (D > 0 ? b[r].h > H : b[r].l < H)) { ok = false; break; } if (!ok) continue;
            double PLo = D > 0 ? 1e18 : -1e18, poleL = D > 0 ? 1e18 : -1e18; bool touch = false; int tq = -1;
            for (int r = h + 1; r <= q; ++r) { PLo = D > 0 ? std::min(PLo, b[r].l) : std::max(PLo, b[r].h); if (r < q && (D > 0 ? b[r].l <= b[r].e : b[r].h >= b[r].e)) { touch = true; if (tq < 0) tq = r; } }
            for (int r = h - 5; r <= h; ++r) poleL = D > 0 ? std::min(poleL, b[r].l) : std::max(poleL, b[r].h);
            double pole = D * (H - poleL); if (pole <= 0 || D * (H - PLo) > 0.6 * pole || !touch) continue;
            double c = cb.c; if (!(D > 0 ? (c > b[q - 1].h && c > em) : (c < b[q - 1].l && c < em))) continue;
            double stop = D > 0 ? PLo - 0.25 : PLo + 0.25, R = D * (c - stop); if (!(R > 0.25)) continue;
            double pts; int why; size_t kx = exitF(x, k, D, R, tf, mode, pts, why); double net = pts - 2.0 * x.A / 320; a.add(x.era, net > 0, net); RA.push_back(R / x.A); busy = kx; have = true; nd++; done = true;
            if (LIST) { char ed[16], et[8], xd[16], xt[8], pd[16], pt[8], fd[16], ft[8], td[16], tt[8]; hm(s.t[I[k]] + 60, ed, et); hm(s.t[I[kx]] + 60, xd, xt); hm(s.t[I[b[h - 5].k]] - 60 * (tf - 1), pd, pt); hm(s.t[I[b[h + 1].k]] - 60 * (tf - 1), fd, ft); hm(s.t[I[b[tq].k]] - 60 * (tf - 1), td, tt);
              printf("%s,%d,%s,%d,%s,%s,%.2f,%.2f,%.2f,%s,%s,%.2f,%.2f,%.2f,%s,%s %s,%s %s,%.2f,%s %s\n", SN[se], tf, D > 0 ? "bull" : "bear", x.date, ed, et, c, stop, R, xd, xt, D * pts + c - c + (D > 0 ? c + pts : c - pts) - c, pts, pts / R, RS[why], pd, pt, fd, ft, em, td, tt); } } } }
      if (LIST) continue;
      double cp = 0, cn = 0; std::vector<const DI*> pool; for (auto& x : days) if (x.se == se) pool.push_back(&x);
      for (int rep = 0; rep < 5; ++rep) for (double ra : RA) { const DI& x = *pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)]; std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) if (x.ctx[k]) cd.push_back(k); if (cd.empty()) continue;
          size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; double pts; int why; exitF(x, k0, x.D[k0], ra * x.A, tf, mode, pts, why); cp += pts - 2.0 * x.A / 320; cn++; }
      printf("  %-6s %2dm  %5.2f/day | win %4.1f%% | %+7.2f | %+7.2f / %+7.2f / %+7.2f (%3.0f) | control %+7.2f\n", SN[se], tf, a.n / ndays, 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n),
             a.pe[0] / std::max(1.0, a.ne[0]), a.pe[1] / std::max(1.0, a.ne[1]), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2], cp / std::max(1.0, cn)); } }
  return 0;
}
