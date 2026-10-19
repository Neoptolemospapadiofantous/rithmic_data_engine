// 2026-10-20 (founder, on 2026-10-19's afternoon: "the after downtrend had some short potentials" → "what do the numbers say"):
// flags AFTER THE TURN. The /flags page's context measures the move from the 09:30 open (above the open and VWAP, straightness from the
// open >= 0.55), so a reversal day — up into 11:36, then a clean fall that stayed above the open until 13:30 — shows no bear flags.
// Declared before running:
//   OPEN  the page's context (bull mirrored for bears): close > open, > VWAP, (close - open) / session range >= 0.55
//   TURN  NOT open, and measured from the session's opposite extreme instead (the high of day for a bear flag, the low for a bull):
//         close > VWAP, close at least 0.5 ATR beyond the extreme, and (close - extreme) / (range since the extreme) >= 0.55
//         (variants 0.3 / 0.8 ATR shown, 0.5 is the one judged)
// Flags = the page's rules (NY 3/4/5m, pole = lowest low of the 2-15 bars before the top, >= 4x the session's average bar, 2 flag
// bars, pullback 15-50 %, registered 09:45-14:00, one at a time per chart, side and context). Entry: 2m trigger, else pole-top stop;
// target pole top + pole length; flat 15:55 (later than flag_review2's 13:15 so the afternoon counts); 2 pts cost scaled to the day.
// Pullback # since the extreme (the page's check). CONTROL (same context, random timing): for each flag, 20 random minutes 10:00-14:00
// of the same day where the same context holds, entered at that minute's close with the flag's own risk and reward in points.
// Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
struct Row { int date, era, tf, side, ctx, pbno, minute; double R, ctl; };   // ctx 0 open, 1 turn 0.5, 2 turn 0.3 only, 3 turn 0.8 too
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(20261020); std::vector<Row> rows; long nDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1600) I.push_back(i); } if (I.size() < 370) continue;
    nDays++; const double A = d.atr, cost = 2 * A / 320.0; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), ext(n); std::vector<int> TM(n), ctxK(n), extK(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      // context per minute: 0 none, 1 open, 2 turn (>= 0.3 ATR), bit flags for 0.5 / 0.8 in ext distance
      { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18, lod = 1e18, hiSince = -1e18; int lk = 0;
        for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
          if (Lw[k] < lod) { lod = Lw[k]; lk = k; hiSince = Hh[k]; } else hiSince = std::max(hiSince, Hh[k]);
          const double vw = vv > 0 ? pv / vv : Cc[k], c = Cc[k];
          const bool open = c > so && c > vw && hi > lo && (c - so) / (hi - lo) >= 0.55;
          const bool turn = !open && c > vw && hiSince > lod && (c - lod) / (hiSince - lod) >= 0.55 && (c - lod) / A >= 0.3;
          ctxK[k] = open ? 1 : turn ? 2 : 0; ext[k] = (c - lod) / A; extK[k] = lk; } }
      auto exitR = [&](size_t k0, double e, double st, double tg, bool fillMin) -> double { double px = NAN; if (fillMin && Lw[k0] <= st) px = st;
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1555) { px = O[k]; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / (e - st); };
      for (int tf = 3; tf <= 5; ++tf) { std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if ((TM[k] - 930) % tf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } }
        for (int want = 1; want <= 2; ++want) { int busy = -1; std::vector<std::pair<int, int>> reg;   // (kq, extreme index at registration)
          for (int q = 6; q < (int)b.size(); ++q) { if (q <= busy) continue; const int minute = (TM[b[q].k0] - 930) + tf; if (minute < 15 || minute >= 270) continue;
            const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
            int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
            bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
            double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 4 * ar) continue;
            const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
            const size_t kq = b[q].k; if (ctxK[kq] != want) continue;
            Row r{}; r.date = d.date; r.era = era; r.tf = tf; r.side = side; r.minute = minute; r.R = NAN; r.ctl = NAN;
            r.ctx = want == 1 ? 0 : ext[kq] >= 0.8 ? 3 : ext[kq] >= 0.5 ? 1 : 2;
            int c = 0; for (auto& [rk, ek] : reg) if (rk > extK[kq]) c++; r.pbno = c + 1; reg.push_back({(int)kq, extK[kq]});
            double flo = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18, Rp = NAN, eUsed = NAN, sUsed = NAN; bool lowerHigh = false; int nb = 0;
            for (size_t kk = kq + 1; kk < n; ++kk) { const int t2 = TM[kk]; if (t2 >= 1500) break;
              if (std::isnan(Rp) && Hh[kk] > H) { Rp = exitR(kk, H + 0.25, std::min(flo, hbl) - 0.25, H + M, true); if (std::isnan(eUsed)) { eUsed = H + 0.25; sUsed = std::min(flo, hbl) - 0.25; } }
              b2h = std::max(b2h, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
              if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[kk]; if (!std::isnan(p2h) && b2h <= H && std::isnan(r.R)) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(flo, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { r.R = exitR(kk, c2, fln - 0.25, H + M, false); eUsed = c2; sUsed = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
              if ((t2 + 1 - 930) % tf == 0) { nb++; flo = std::min(flo, hbl); hbl = 1e18; if (Hh[kk] > H || Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
            if (std::isnan(r.R)) r.R = Rp;
            busy = q + nb;
            // control: same context, random minute 10:00-14:00 this day, the flag's own risk and reward in points
            if (!std::isnan(r.R)) { const double risk = eUsed - sUsed, rew = H + M - eUsed; std::vector<size_t> pool; for (size_t k = 0; k < n; ++k) if (TM[k] >= 1000 && TM[k] < 1400 && ctxK[k] == want && (want == 1 || ext[k] >= (r.ctx == 3 ? 0.8 : r.ctx == 1 ? 0.5 : 0.3))) pool.push_back(k);
              if (!pool.empty() && risk > 0) { double sm = 0; for (int z = 0; z < 20; ++z) { size_t k = pool[rng() % pool.size()]; sm += exitR(k, Cc[k], Cc[k] - risk, Cc[k] + rew, false); } r.ctl = sm / 20; } }
            rows.push_back(r); } } } } }
  printf("Flags after the turn — NQ NY 3/4/5m, 2010-07 → 2026-10, %ld days, %zu flags. R after 2 pts cost (2m trigger, else pole-top stop; target pole top + pole; flat 15:55).\n", nDays, rows.size());
  printf("Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26 · then the same-context random-timing control (flag's own risk/reward), all years. Flags a year in brackets.\n");
  const double yrs = nDays / 252.0;
  auto line = [&](const char* name, std::function<bool(const Row&)> f) { double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, cs = 0, cn = 0, rs = 0, w = 0;
    for (auto& r : rows) if (f(r) && !std::isnan(r.R)) { m[r.era] += r.R; nn[r.era]++; w += r.R > 0; if (!std::isnan(r.ctl)) { cs += r.ctl; cn++; rs += r.R; } }
    const double N = nn[0] + nn[1] + nn[2];
    printf("  %-44s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f) · win %2.0f%% · flag %+.3f vs control %+.3f [%.0f/yr]\n", name, nn[0] ? m[0] / nn[0] : NAN, nn[0], nn[1] ? m[1] / nn[1] : NAN, nn[1], nn[2] ? m[2] / nn[2] : NAN, nn[2], N ? 100 * w / N : 0, cn ? rs / cn : NAN, cn ? cs / cn : NAN, N / yrs); };
  for (int side : {1, -1}) { const char* S = side > 0 ? "BULL" : "BEAR"; printf("\n== %s flags%s\n", S, side > 0 ? " (turn = after the low of day)" : " (turn = after the high of day)");
    auto sd = [=](const Row& r) { return r.side == side; };
    line("OPEN context (the page today)", [=](const Row& r) { return sd(r) && r.ctx == 0; });
    line("  1st pullback", [=](const Row& r) { return sd(r) && r.ctx == 0 && r.pbno == 1; });
    line("TURN >= 0.5 ATR (the test)", [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3); });
    line("  1st pullback since the turn", [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3) && r.pbno == 1; });
    line("  2nd+ pullback", [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3) && r.pbno >= 2; });
    line("  registered before 12:00", [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3) && r.minute < 150; });
    line("  registered 12:00-14:00", [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3) && r.minute >= 150; });
    line("TURN 0.3-0.5 ATR only", [=](const Row& r) { return sd(r) && r.ctx == 2; });
    line("TURN >= 0.8 ATR", [=](const Row& r) { return sd(r) && r.ctx == 3; });
    for (int tf = 3; tf <= 5; ++tf) { char nm[64]; snprintf(nm, 64, "TURN >= 0.5, %dm", tf); line(nm, [=](const Row& r) { return sd(r) && (r.ctx == 1 || r.ctx == 3) && r.tf == tf; }); } }
  return 0;
}
