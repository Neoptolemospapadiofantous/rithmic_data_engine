// 2026-10-19 (founder, on the /flags page's nested view: "this is golden" → "do the test"): does the number of smaller 1-minute flags
// already inside a 3/4/5m flag's pole predict how that flag does? Question asked BEFORE looking: 0 boxes in the pole (a young move)
// should beat 3+ (a late move).
// Flags exactly as the /flags page detects them (bot/frontend/src/app/flags/page.tsx): NY 3/4/5m, bull + bear on the mirrored session,
// pole = lowest low of the 2-15 bars before the top, >= 4x the session's average bar on that chart, 2 flag bars with no new high,
// pullback 15-50 %, registered 09:45-14:00, context at registration (above the 09:30 open and VWAP, straightness >= 0.55), one at a
// time per chart and side. 1-minute flags for the count: the page's two passes — researched (same rules on 1m) and inner (pole >= 2x
// the 1m average bar, above the open and VWAP only, only candidates the researched rules reject) — each one at a time; a 1m flag
// counts when its pole-top minute lies inside the bigger flag's pole (from the pole-start bar's first minute to the top bar's first
// minute, as on the page). Entry: the 2m trigger (first 2m close above the previous 2m high after a lower high inside the flag, stop =
// flag low), else the pole-top stop order; target pole top + pole length; flat 13:15 (as flag_review2); 2 pts cost scaled to the day.
// Pullback # = the page's: researched flags of this chart and side since the low (high) of day + 1. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
struct Row { int date, era, tf, side, pauses, pbno; double poleAtr, R; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Row> rows; long n1 = 0, nInner = 0, nDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    nDays++; const double A = d.atr, cost = 2 * A / 320.0; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18;   // the page's contextAt (mirrored units)
        for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
          const double vw = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > vw; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
      auto exitR = [&](size_t k0, double e, double st, double tg, bool fillMin) -> double { double px = NAN; if (fillMin && Lw[k0] <= st) px = st;
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / (e - st); };
      // chart bars on the 09:30 grid
      auto bars = [&](int tf) { std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - 930) / tf != (TM[b.back().k0] - 930) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } } return b; };
      // the page's detect(): returns per flag {pole-start first minute, top first minute, flag bar index q, ...}
      struct Det { int kPole, kTop, q, l, h; double H, L, M, ar; };
      auto detect = [&](const std::vector<FB>& b, int tf, bool inner) { std::vector<Det> out; int busy = -1;
        for (int q = 6; q < (int)b.size(); ++q) { if (q <= busy) continue; const int minute = (TM[b[q].k0] - 930) + tf; if (minute < 15 || minute >= 270) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
          // follow minute by minute until breakout / failure / 12 chart bars (sets busy, as the page)
          int nb = 0; for (size_t kk = b[q].k + 1; kk < n; ++kk) { if (Hh[kk] > H) break; if ((TM[kk] - 930) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
          out.push_back({b[l].k0, b[h].k0, q, l, h, H, L, M, ar}); busy = q + nb; }
        return out; };
      const auto b1 = bars(1); auto r1 = detect(b1, 1, false), i1 = detect(b1, 1, true); n1 += r1.size(); nInner += i1.size();
      std::vector<int> tops1; for (auto& x : r1) tops1.push_back(x.kTop); for (auto& x : i1) tops1.push_back(x.kTop);
      for (int tf = 3; tf <= 5; ++tf) { const auto b = bars(tf); const auto fl = detect(b, tf, false);
        double lod = 1e18; size_t lk = 0; std::vector<int> regK; size_t scan = 0;
        for (auto& f : fl) { const size_t kq = b[f.q].k;
          for (; scan <= kq; ++scan) if (Lw[scan] < lod) { lod = Lw[scan]; lk = scan; }   // low of day up to the registration bar
          Row r{}; r.date = d.date; r.era = era; r.tf = tf; r.side = side; r.poleAtr = f.M / A; r.R = NAN;
          for (int t : tops1) if (t >= f.kPole && t < f.kTop) r.pauses++;
          int c = 0; for (int rk : regK) if (rk > (int)lk) c++; r.pbno = c + 1; regK.push_back(kq);
          // entry: 2m trigger inside the flag, else the pole-top stop order
          const double H = f.H, M = f.M; double flo = std::min(b[f.h + 1].l, b[f.q].l), hbl = 1e18, p2h = NAN, b2h = -1e18, Rp = NAN; bool lowerHigh = false; int nb = 0;
          for (size_t kk = kq + 1; kk < n; ++kk) { const int t2 = TM[kk]; if (t2 >= 1260) break;
            if (std::isnan(Rp) && Hh[kk] > H) Rp = exitR(kk, H + 0.25, std::min(flo, hbl) - 0.25, H + M, true);
            b2h = std::max(b2h, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
            if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[kk]; if (!std::isnan(p2h) && b2h <= H && std::isnan(r.R)) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(flo, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) r.R = exitR(kk, c2, fln - 0.25, H + M, false); } p2h = b2h; b2h = -1e18; }
            if ((t2 + 1 - 930) % tf == 0) { nb++; flo = std::min(flo, hbl); hbl = 1e18; if (Hh[kk] > H || Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
          if (std::isnan(r.R)) r.R = Rp;
          rows.push_back(r); } } } }
  printf("Flags with smaller 1m flags inside the pole — NQ NY 2010-07 → 2026-10, %ld days. %zu 3/4/5m flags (page rules); 1m flags for the count: %.2f researched + %.2f inner a day per side.\n",
         nDays, rows.size(), n1 / (2.0 * nDays), nInner / (2.0 * nDays));
  printf("R after 2 pts cost (2m trigger, else pole-top stop; target pole top + pole; flat 13:15). Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26; NaN R = never entered.\n");
  auto line = [&](const char* name, std::function<bool(const Row&)> f) { double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, pa = 0, k = 0; for (auto& r : rows) if (f(r)) { pa += r.poleAtr; k++; if (!std::isnan(r.R)) { m[r.era] += r.R; nn[r.era]++; } }
    printf("  %-40s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)   pole %.2f ATR\n", name, nn[0] ? m[0] / nn[0] : NAN, nn[0], nn[1] ? m[1] / nn[1] : NAN, nn[1], nn[2] ? m[2] / nn[2] : NAN, nn[2], k ? pa / k : 0); };
  for (int side : {1, -1}) for (int sub = 0; sub < 2; ++sub) {
    printf("\n== %s flags, %s\n", side > 0 ? "BULL" : "BEAR", sub ? "1st pullback only (the page's main check)" : "all");
    auto base = [=](const Row& r) { return r.side == side && (!sub || r.pbno == 1); };
    line("ALL", base);
    line("0 smaller 1m flags in the pole", [=](const Row& r) { return base(r) && r.pauses == 0; });
    line("1", [=](const Row& r) { return base(r) && r.pauses == 1; });
    line("2", [=](const Row& r) { return base(r) && r.pauses == 2; });
    line("3+", [=](const Row& r) { return base(r) && r.pauses >= 3; });
    line("0-1", [=](const Row& r) { return base(r) && r.pauses <= 1; });
    line("2+", [=](const Row& r) { return base(r) && r.pauses >= 2; });
  }
  // longer poles hold more 1m flags — compare inside pole-size halves (median pole/ATR per side)
  printf("\n== Same count inside pole-size halves (pole size is the obvious confound: a longer pole has room for more boxes)\n");
  for (int side : {1, -1}) { std::vector<double> v; for (auto& r : rows) if (r.side == side) v.push_back(r.poleAtr); std::sort(v.begin(), v.end()); const double med = v[v.size() / 2];
    for (int half = 0; half < 2; ++half) { printf("  -- %s, pole %s %.2f ATR\n", side > 0 ? "bull" : "bear", half ? ">=" : "<", med);
      auto base = [=](const Row& r) { return r.side == side && ((r.poleAtr >= med) == (bool)half); };
      line("0-1 smaller 1m flags", [=](const Row& r) { return base(r) && r.pauses <= 1; });
      line("2+", [=](const Row& r) { return base(r) && r.pauses >= 2; }); } }
  printf("\n== By chart (bull + bear): 0-1 vs 2+\n");
  for (int tf = 3; tf <= 5; ++tf) { char a[64], c[64]; snprintf(a, 64, "%dm 0-1", tf); snprintf(c, 64, "%dm 2+", tf);
    line(a, [=](const Row& r) { return r.tf == tf && r.pauses <= 1; }); line(c, [=](const Row& r) { return r.tf == tf && r.pauses >= 2; }); }
  return 0;
}
