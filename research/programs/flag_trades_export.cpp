// 2026-10-19: export flag trades WITH their stop (for prop-account simulators that size each trade to its stop, e.g. topone_sim).
// FL5  = the 5m flag with the day on a straight NY session (flag5_trend rules: pole 6 bars >= 0.20 ATR, flag 5 bars <= 0.6 x pole,
//        close beyond the flag, price on the day's side of the 09:30 open and VWAP, straightness >= 0.55, 09:45–15:00, stop beyond the
//        flag + 0.25, 1.5R, flat 15:55, ≤ 2 a day, one at a time).
// FLMIX = any 1m (pole 10 >= 0.10, flag 5 <= 0.6) / 3m (3 >= 0.10, 2 <= 0.8) / 5m (4 >= 0.10, 3 <= 0.6, or the FL5 flag) flag with the day,
//        same context, ≤ 5 a day.  Output: research/programs/inputs_20261019/{flag5_k,flagmix_k}.csv in the _k format (10th col = stop / ATR,
//        ATR = edge_lab's). gross_pts before cost; net = gross − 1. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; };
static double flagStop(const std::vector<FB>& b, int N, int F, double m, double tight, int D, double A) {
  int n = b.size(); if (n < N + F + 1 || !D) return NAN; int p0 = n - 1 - F - N, p1 = n - 2 - F;
  double pole = D * (b[p1].c - b[p0].o); if (pole < m * A) return NAN; double fh = -1e18, fl = 1e18;
  for (int k = n - 1 - F; k < n - 1; ++k) { fh = std::max(fh, b[k].h); fl = std::min(fl, b[k].l); }
  if (fh - fl > tight * pole) return NAN; double c = b[n - 1].c; if (D > 0 ? !(c > fh) : !(c < fl)) return NAN; return D > 0 ? fl - 0.25 : fh + 0.25; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  FILE* out[2] = {fopen("research/programs/inputs_20261019/flag5_k.csv", "w"), fopen("research/programs/inputs_20261019/flagmix_k.csv", "w")};
  const int MAXD[2] = {2, 5}; long cnt[2] = {0}; double sum[2] = {0};
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr;
    struct Pos { int dir = 0, ei = 0; double e = 0, st = 0, tg = 0, R = 0, mae = 0, mfe = 0; int64_t tEnd = 0; } pos[2]; int nd[2] = {0, 0};
    std::vector<FB> b1, b3, b5; double so = s.o[d.rth0], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
    for (int i = d.rth0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(16, 0)) break;
      for (int k = 0; k < 2; ++k) { Pos& p = pos[k]; if (!p.dir) continue;
        p.mae = std::min(p.mae, p.dir * ((p.dir > 0 ? s.l[i] : s.h[i]) - p.e)); p.mfe = std::max(p.mfe, p.dir * ((p.dir > 0 ? s.h[i] : s.l[i]) - p.e));
        double px = NAN; bool hs = p.dir > 0 ? s.l[i] <= p.st : s.h[i] >= p.st, ht = p.dir > 0 ? s.h[i] >= p.tg : s.l[i] <= p.tg;
        if (hs) px = p.st; else if (ht) px = p.tg; else if (tm >= hm_to_tday(15, 55)) px = s.c[i];
        if (!std::isnan(px)) { double g = p.dir * (px - p.e); fprintf(out[k], "%d,%lld,%lld,%d,%.4f,%.4f,%.2f,%.2f,0,%.5f\n", d.date, (long long)s.t[p.ei], (long long)s.t[i], p.dir, g, g - 1, p.mae, p.mfe, p.R / A);
          cnt[k]++; sum[k] += g - 2.0 * A / 320.0; p.dir = 0; } }
      hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
      auto upd = [&](std::vector<FB>& b, int tf) { if ((tm - hm_to_tday(9, 30)) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
        else { FB& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } };
      upd(b1, 1); upd(b3, 3); upd(b5, 5); const bool c3 = (tm + 1 - hm_to_tday(9, 30)) % 3 == 0, c5 = (tm + 1 - hm_to_tday(9, 30)) % 5 == 0;
      const double c = s.c[i], vw = vv > 0 ? pv / vv : c; const int D = c > so ? 1 : c < so ? -1 : 0; double st = hi > lo ? std::fabs(c - so) / (hi - lo) : 0;
      if (!(tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0) && D && (c - vw) * D > 0 && st >= 0.55)) continue;
      double a5 = c5 ? flagStop(b5, 6, 5, 0.20, 0.6, D, A) : NAN, m5 = c5 ? flagStop(b5, 4, 3, 0.10, 0.6, D, A) : NAN, m3 = c3 ? flagStop(b3, 3, 2, 0.10, 0.8, D, A) : NAN, m1 = flagStop(b1, 10, 5, 0.10, 0.6, D, A);
      double fs[2] = {a5, NAN}; for (double x : {a5, m5, m3, m1}) if (std::isnan(fs[1]) && !std::isnan(x)) fs[1] = x;
      for (int k = 0; k < 2; ++k) { if (std::isnan(fs[k]) || pos[k].dir || nd[k] >= MAXD[k]) continue; double R = D * (c - fs[k]); if (!(R > 0.25) || R > 0.6 * A) continue;
        Pos& p = pos[k]; p.dir = D; p.ei = i; p.e = c; p.R = R; p.st = c - D * R; p.tg = c + D * 1.5 * R; p.mae = p.mfe = 0; nd[k]++; } } }
  for (int k = 0; k < 2; ++k) { fclose(out[k]); printf("%s: %ld trades, mean %+.2f NQ pts net of 2 pts at today's cost-to-range (in-era points)\n", k ? "flagmix_k.csv" : "flag5_k.csv", cnt[k], sum[k] / cnt[k]); }
  return 0;
}
