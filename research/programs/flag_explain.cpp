// 2026-10-19 (founder: "show me how we calculate flags"): prints one flag step by step — the 12 bars on its chart, the pole, the flag,
// the breakout check, the stop and the target — exactly as flag_refined.cpp computes them. argv: YYYYMMDD entry_epoch chart_minutes.
#include "research/meta.hpp"
#include <cstdio>
#include <cstdlib>
using namespace lab;
struct FB { double o, h, l, c; int t0, t1; };
static std::string hm(int tday) { int m = tday + 18 * 60; m %= 1440; char b[8]; snprintf(b, sizeof b, "%02d:%02d", m / 60, m % 60); return b; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_explain YYYYMMDD entry_epoch chart_minutes\n"); return 1; }
  int date = atoi(argv[1]); long te = atol(argv[2]); int tf = atoi(argv[3]);
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  for (auto& d : s.days) { if (d.date != date) continue; const double A = d.atr; std::vector<FB> b; double so = s.o[d.rth0], c10 = NAN;
    for (int i = d.rth0; i < d.b1 && s.t[i] <= te; ++i) { int tm = tday_min(s.t[i]); if (tm == hm_to_tday(9, 59)) c10 = s.c[i];
      if (b.empty() || (tm - hm_to_tday(9, 30)) % tf == 0) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], tm, tm});
      else { FB& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.t1 = tm; } }
    int m = b.size(); if (m < 12) { printf("not enough bars\n"); return 1; }
    printf("NQ %d, %d-minute chart. ATR (14-day average daily range) = %.1f pts.\n", date, tf, A);
    printf("Step 0 — direction: 09:30 open %.2f, price at 10:00 %.2f → %s day.\n\n", so, c10, c10 > so ? "UP (look for BULL flags)" : "DOWN (look for BEAR flags)");
    printf("  bar   time         open      high       low     close   role\n");
    for (int q = m - 12; q < m; ++q) { const char* role = q <= m - 7 ? "POLE" : q < m - 1 ? "FLAG" : "BREAKOUT";
      printf("  %3d   %s-%s  %8.2f  %8.2f  %8.2f  %8.2f   %s\n", q - (m - 12) + 1, hm(b[q].t0).c_str(), hm(b[q].t1 + 1).c_str(), b[q].o, b[q].h, b[q].l, b[q].c, role); }
    int p0 = m - 12, p1 = m - 7; double pole = b[p1].c - b[p0].o; int D = pole > 0 ? 1 : -1; double fh = -1e18, fl = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, b[q].h); fl = std::min(fl, b[q].l); }
    const FB& bk = b[m - 1]; double br = bk.h - bk.l, pos = br > 0 ? (D > 0 ? (bk.c - bk.l) / br : (bk.h - bk.c) / br) : 0, stop = D > 0 ? fl - 0.25 : fh + 0.25, R = D * (bk.c - stop);
    printf("\nStep 1 — POLE (bars 1-6): close of bar 6 − open of bar 1 = %.2f − %.2f = %+.2f pts = %.2f ATR  → need >= 0.20 ATR (%.1f pts): %s\n", b[p1].c, b[p0].o, pole, std::fabs(pole) / A, 0.20 * A, std::fabs(pole) >= 0.20 * A ? "YES" : "no");
    printf("Step 2 — FLAG (bars 7-11): high %.2f, low %.2f, range %.2f pts = %.2f x the pole → need <= 0.6: %s\n", fh, fl, fh - fl, (fh - fl) / std::fabs(pole), fh - fl <= 0.6 * std::fabs(pole) ? "YES" : "no");
    printf("Step 3 — BREAKOUT (bar 12): close %.2f %s the flag %s %.2f: %s\n", bk.c, D > 0 ? "above" : "below", D > 0 ? "high" : "low", D > 0 ? fh : fl, (D > 0 ? bk.c > fh : bk.c < fl) ? "YES" : "no");
    printf("Step 4 — STRONG BREAKOUT BAR: close sits at %.0f %% of the bar's range toward the trade → need >= 70 %%: %s\n", 100 * pos, pos >= 0.7 ? "YES" : "no");
    printf("Step 5 — same direction as 10:00 and after 10:00: %s\n", ((c10 > so) == (D > 0)) ? "YES" : "no");
    printf("\nTRADE: %s at %.2f. Stop 1 tick beyond the flag = %.2f (risk %.2f pts = %.2f ATR). Target 1.5R = %.2f. Flat by 15:55 if neither is hit.\n", D > 0 ? "BUY" : "SELL", bk.c, stop, R, R / A, bk.c + D * 1.5 * R);
    double px = NAN; int tx = 0; for (int i = d.rth0; i < d.b1; ++i) { if (s.t[i] <= te) continue; int tm = tday_min(s.t[i]); if (D > 0 ? s.l[i] <= stop : s.h[i] >= stop) { px = stop; tx = tm; break; }
      if (D > 0 ? s.h[i] >= bk.c + 1.5 * R : s.l[i] <= bk.c - 1.5 * R) { px = bk.c + D * 1.5 * R; tx = tm; break; } if (tm >= hm_to_tday(15, 55)) { px = s.c[i]; tx = tm; break; } }
    printf("RESULT: exit %.2f at %s → %+.2f pts (%+.2fR) before costs.\n", px, hm(tx).c_str(), D * (px - bk.c), D * (px - bk.c) / R); return 0; }
  printf("date not found\n"); return 1; }
