// 2026-10-19 (founder: "show me on a page each chart the full dates of all trades, at least 3 years, I wanna manually test them").
// Every trade of the flag-grid rule "with the day + straight path, trail previous bar", one fixed setting so each trade can be checked on a
// chart: pole 6 bars in the session's direction (any size), flag 5 bars <= 0.6 x pole, close beyond the flag; context = the session's own
// open / VWAP / straightness (price on the trade's side of both, straightness >= 0.55); entries from session open + 15 min to session end −
// 30 min; stop 1 tick beyond the flag; once +1R (1-min close) the stop trails the previous completed bar of that chart; flat at session end.
// One position at a time per chart and session, <= 5 a day. Charts 1/2/3/4/5/15 min; sessions Asia 18:00-03:00, London 03:00-09:30,
// NY 09:30-15:55 (ET). NQ continuous front month (Databento NQ.c.0 + the live collector), raw prices, New York time.
// Output CSV (stdout): session,chart,side,trade_day,entry_date,entry_time,entry,stop,risk_pts,exit_date,exit_time,exit,pts,R,exit_reason,
// pole_start,flag_start,flag_high,flag_low,wick_candle. With argv[2] = "wick" only flags where one of the last 2 flag candles has a wick AGAINST
// the trade of at least half its range (bull: lower wick) — founder 2026-10-19, see flag_mid_wick.cpp. argv: first trading day (default 20231016).
#include "research/meta.hpp"
#include <cstdio>
#include <cstdlib>
#include <ctime>
using namespace lab;
struct FB { double o, h, l, c; int k; };
// bar times are stored as New York wall clock (see tday_min), so they are formatted with gmtime, not converted
static void et(long epoch, char* d, char* t) { time_t e = epoch; struct tm tmv; gmtime_r(&e, &tmv); strftime(d, 16, "%Y-%m-%d", &tmv); strftime(t, 8, "%H:%M", &tmv); }
int main(int argc, char** argv) {
  const int from = argc > 1 ? atoi(argv[1]) : 20231016; const bool WICK = argc > 2 && std::string(argv[2]) == "wick";   // argv[2] "wick": only flags with a rejection wick (see below)
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {540, 930, 1315}; const char* SN[3] = {"Asia", "London", "NY"};
  printf("session,chart,side,trade_day,entry_date,entry_time,entry,stop,risk_pts,exit_date,exit_time,exit,pts,R,exit_reason,pole_start,flag_start,flag_high,flag_low,wick_candle\n");
  for (auto& d : s.days) { if (!d.ok || d.date < from || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    for (int se = 0; se < 3; ++se) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) I.push_back(i); } if (I.size() < 60) continue; const size_t n = I.size();
      std::vector<int> D(n); std::vector<char> ctx(n); double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < n; ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
        D[k] = c > so ? 1 : c < so ? -1 : 0; int tm = tday_min(s.t[i]); ctx[k] = D[k] && (c - vw) * D[k] > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55 && tm >= S0[se] + 15 && tm < SE[se] - 30; }
      for (int tf : TFS) { std::vector<FB> b; size_t busy = 0; bool have = false; int nd = 0;
        for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
          if ((tm - S0[se]) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
          if ((tm + 1 - S0[se]) % tf || !ctx[k] || (have && k <= busy) || nd >= 5) continue; int m = b.size(); if (m < 12) continue;
          int Dd = D[k]; double pole = Dd * (b[m - 7].c - b[m - 12].o); if (pole <= 0) continue;
          double fh = -1e18, fl = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, b[q].h); fl = std::min(fl, b[q].l); } if (fh - fl > 0.6 * pole) continue;
          double c = b[m - 1].c; if (Dd > 0 ? !(c > fh) : !(c < fl)) continue;
          int wq = -1; for (int q = m - 3; q <= m - 2; ++q) { double rg = b[q].h - b[q].l, wk = Dd > 0 ? std::min(b[q].o, b[q].c) - b[q].l : b[q].h - std::max(b[q].o, b[q].c); if (rg > 0 && wk >= 0.5 * rg) { wq = q; break; } }
          if (WICK && wq < 0) continue; double stop = Dd > 0 ? fl - 0.25 : fh + 0.25, R = Dd * (c - stop); if (!(R > 0.25)) continue;
          // exit: trail the previous completed bar of this chart once +1R
          double st = stop, px = NAN, bh = -1e18, bl = 1e18, ph = NAN, pl = NAN; size_t kx = n - 1; bool armed = false; const char* why = "session end";
          for (size_t kk = k + 1; kk < n; ++kk) { int j = I[kk], tj = tday_min(s.t[j]);
            if (Dd > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = kk; why = armed ? "trailing stop" : "stop"; break; }
            bh = std::max(bh, (double)s.h[j]); bl = std::min(bl, (double)s.l[j]);
            if ((tj + 1 - S0[se]) % tf == 0) { ph = bh; pl = bl; bh = -1e18; bl = 1e18; if (armed) { double ns = Dd > 0 ? pl - 0.25 : ph + 0.25; st = Dd > 0 ? std::max(st, ns) : std::min(st, ns); } }
            if (!armed && Dd * (s.c[j] - c) >= R) { armed = true; if (!std::isnan(pl)) { double ns = Dd > 0 ? pl - 0.25 : ph + 0.25; st = Dd > 0 ? std::max(st, ns) : std::min(st, ns); } } }
          if (std::isnan(px)) { px = s.c[I[n - 1]]; kx = n - 1; }
          busy = kx; have = true; nd++; double pts = Dd * (px - c);
          char ed[16], etm[8], xd[16], xt[8], pd[16], pt[8], fd[16], ft[8]; char wd[16] = "", wt[8] = ""; if (wq >= 0) et(s.t[I[b[wq].k]] - 60 * (tf - 1), wd, wt); long te = s.t[I[k]] + 60, tx = s.t[I[kx]] + 60;
          et(te, ed, etm); et(tx, xd, xt); et(s.t[I[b[m - 12].k]] - 60 * (tf - 1), pd, pt); et(s.t[I[b[m - 6].k]] - 60 * (tf - 1), fd, ft);
          printf("%s,%d,%s,%d,%s,%s,%.2f,%.2f,%.2f,%s,%s,%.2f,%.2f,%.2f,%s,%s %s,%s %s,%.2f,%.2f,%s%s%s\n", SN[se], tf, Dd > 0 ? "bull" : "bear", d.date, ed, etm, c, stop, R, xd, xt, px, pts, pts / R, why, pd, pt, fd, ft, fh, fl, wd, wq >= 0 ? " " : "", wt); } } } }
  return 0;
}
