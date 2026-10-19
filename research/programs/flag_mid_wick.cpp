// 2026-10-19 (founder: "the flag needs to enter at the half of previous candles and/or have some sort of wicked top candle, test these").
// Flags of the trade log (session direction, straight path, VWAP side; pole 6 bars any size; flag 5 bars <= 0.6 x pole; a bar closes beyond
// the flag), with three entries and three candle filters:
//  entries: A breakout close · B limit at 50 % of the breakout candle (next 3 bars of that chart) · C limit at 50 % of the last flag candle
//           (next 3 bars); a limit is cancelled if the stop trades first
//  filters: none · WICK-IN-FLAG one of the last 2 flag candles has a wick AGAINST the trade >= 50 % of its range (bull: lower wick) ·
//           WICKED BREAKOUT the breakout candle has a wick IN the trade's direction >= 30 % of its range (bull: upper wick)
// Stop 1 tick beyond the flag; exit trail the previous bar after +1R, or fixed 1.5R; flat at session end; one at a time, <= 5 a day,
// 2 pts cost (scaled to each day's range: 2 x ATR / 320). pts = net raw NQ points; R = net / risk.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; int k; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}, sig = 0; void add(int er, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[er] += pts; ne[er]++; } };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TFS[6] = {1, 2, 3, 4, 5, 15}, S0[3] = {0, 540, 930}, SE[3] = {540, 930, 1315}; const char* SN[3] = {"Asia", "London", "NY"};
  struct DI { std::vector<int> I, D; std::vector<char> ctx; int era, se; double A; }; std::vector<DI> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    for (int se = 0; se < 3; ++se) { DI x; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) x.I.push_back(i); } if (x.I.size() < 60) continue;
      const size_t n = x.I.size(); x.D.resize(n); x.ctx.resize(n); double so = s.o[x.I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < n; ++k) { int i = x.I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; double c = s.c[i], vw = vv > 0 ? pv / vv : c;
        x.D[k] = c > so ? 1 : c < so ? -1 : 0; int tm = tday_min(s.t[i]); x.ctx[k] = x.D[k] && (c - vw) * x.D[k] > 0 && hi > lo && std::fabs(c - so) / (hi - lo) >= 0.55 && tm >= S0[se] + 15 && tm < SE[se] - 30; }
      x.era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2; x.se = se; x.A = d.atr; days.push_back(std::move(x)); } }
  auto exitF = [&](const DI& x, size_t k0, double e, int D, double R, int tf, int mode, double& pts) -> size_t { const auto& I = x.I; const size_t n = I.size(); double st = e - D * R, tg = e + D * 1.5 * R, px = NAN, bh = -1e18, bl = 1e18, ph = NAN, pl = NAN; size_t kx = n - 1; bool armed = false;
    for (size_t k = k0 + 1; k < n; ++k) { int j = I[k], tm = tday_min(s.t[j]); if (D > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = k; break; } if (mode == 1 && (D > 0 ? s.h[j] >= tg : s.l[j] <= tg)) { px = tg; kx = k; break; }
      bh = std::max(bh, (double)s.h[j]); bl = std::min(bl, (double)s.l[j]);
      if ((tm + 1 - S0[x.se]) % tf == 0) { ph = bh; pl = bl; bh = -1e18; bl = 1e18; if (mode == 0 && armed) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } }
      if (mode == 0 && !armed && D * (s.c[j] - e) >= R) { armed = true; if (!std::isnan(pl)) { double ns = D > 0 ? pl - 0.25 : ph + 0.25; st = D > 0 ? std::max(st, ns) : std::min(st, ns); } } }
    if (std::isnan(px)) px = s.c[I[n - 1]]; pts = D * (px - e); return kx; };
  const char* EN[3] = {"A breakout close", "B limit 50% of breakout candle", "C limit 50% of last flag candle"}, *FNm[3] = {"no filter", "wick against trend in flag", "wicked breakout candle"};
  printf("Flag entries at half the candle and wick filters — NQ, trade-log flags, 2010-07 → 2026-10. Cells: trades/day | win %% | net pts (R) | 2025-26 pts (n).\n");
  for (int mode = 0; mode < 2; ++mode) { printf("\n######## exit: %s\n", mode ? "fixed 1.5R" : "trail previous bar after +1R");
    for (int se = 2; se >= 0; --se) for (int tf : TFS) { printf("== %s %dm\n", SN[se], tf);
      for (int fl = 0; fl < 3; ++fl) for (int em = 0; em < 3; ++em) { St a; double ndays = 0;
        for (const DI& x : days) { if (x.se != se) continue; ndays++; const auto& I = x.I; const size_t n = I.size(); std::vector<FB> b; size_t busy = 0; bool have = false; int nd = 0;
          for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
            if ((tm - S0[se]) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
            if ((tm + 1 - S0[se]) % tf || !x.ctx[k] || (have && k <= busy) || nd >= 5) continue; int m = b.size(); if (m < 12) continue;
            int D = x.D[k]; double pole = D * (b[m - 7].c - b[m - 12].o); if (pole <= 0) continue;
            double fh = -1e18, flo = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, b[q].h); flo = std::min(flo, b[q].l); } if (fh - flo > 0.6 * pole) continue;
            const FB& bk = b[m - 1]; double c = bk.c; if (D > 0 ? !(c > fh) : !(c < flo)) continue;
            if (fl == 1) { bool ok = false; for (int q = m - 3; q <= m - 2; ++q) { double rg = b[q].h - b[q].l; double wk = D > 0 ? std::min(b[q].o, b[q].c) - b[q].l : b[q].h - std::max(b[q].o, b[q].c); if (rg > 0 && wk >= 0.5 * rg) ok = true; } if (!ok) continue; }
            if (fl == 2) { double rg = bk.h - bk.l, wk = D > 0 ? bk.h - c : c - bk.l; if (!(rg > 0 && wk >= 0.3 * rg)) continue; }
            double stop = D > 0 ? flo - 0.25 : fh + 0.25; if (!(D * (c - stop) > 0.25)) continue; a.sig++;
            double ent = NAN; size_t ke = k;
            if (em == 0) ent = c;
            else { const FB& ref = em == 1 ? bk : b[m - 2]; double lim = (ref.h + ref.l) / 2; if (D * (c - lim) <= 0) { ent = c; } else {
                int barsLeft = 3; long lastT = s.t[i] + 60L * tf * barsLeft;
                for (size_t kk = k + 1; kk < n && s.t[I[kk]] < lastT; ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= stop : s.h[j] >= stop) break; if (D > 0 ? s.l[j] <= lim : s.h[j] >= lim) { ent = lim; ke = kk; break; } } } }
            if (std::isnan(ent)) continue; double R = D * (ent - stop); if (!(R > 0.25)) continue;
            double pts; size_t kx; if (em > 0 && ke > k && (D > 0 ? s.l[I[ke]] <= stop : s.h[I[ke]] >= stop)) { pts = -R; kx = ke; } else kx = exitF(x, ke, ent, D, R, tf, mode, pts);
            double net = pts - 2.0 * x.A / 320; a.add(x.era, net > 0, net, net / R); busy = kx; have = true; nd++; } }
        printf("   %-28s %-34s %5.2f/day | %4.1f%% | %+6.2f (%+.3fR) | 2025-26 %+6.2f (%3.0f)\n", FNm[fl], EN[em], a.n / ndays, 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n), a.r / std::max(1.0, a.n), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2]); } } }
  return 0;
}
