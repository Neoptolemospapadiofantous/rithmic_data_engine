// 2026-10-20 (founder: "what about mean reversion? lets test box mean reverse trades as well"). The opening-range box fade, range-day
// VWAP / first-hour / open fades, no-direction-day fades and level sweeps are all closed (README rows 156-407, family sweep 10-16).
// What was never tested: INTRADAY boxes — a sideways rectangle that forms during the session, faded at its edges. Declared before running:
//   BOX    on a 1/3/5m chart (bars on the 09:30 grid), the last 12 closed bars span a height h <= 3x the session's average bar on that
//          chart, with >= 2 bars whose high is within 15 % of h from the top and >= 2 bars whose low is within 15 % of the bottom.
//          The box is fixed at that bar's close and stays live for 30 chart bars, until a 1m close beyond an edge by more than the stop
//          distance, or until it trades (one trade per box).
//   TRADE  limit buy at the box low (short at the high — whichever edge is touched first by a later minute), stop 0.5·h beyond the
//          edge, target the box middle (variant: the opposite edge less 10 % of h; variant stop 0.3·h). Registration 09:45-15:00, flat 15:55.
//   SPLIT  at entry, the session path from the 09:30 open: chop (|close − open| / range < 0.30), middle, trend (>= 0.55); and whether
//          the fade is against or with the day's side of the open.
//   CONTROL each trade's twin: 20 random minutes 10:00-15:00 of the same day, same direction, same stop and target in points.
// One position at a time per chart and day. R after 2 pts cost scaled to the day (ATR/320). Main cell = 3m, middle target, 0.5·h stop.
// Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double h, l, c; int k0, k; };
struct Row { int date, era, tf, var, dir, regime; bool against; double R, ctl; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(20261020); std::vector<Row> rows; long nDays = 0;
  // variants: 0 = mid target / 0.5h stop (main), 1 = opposite edge − 0.1h / 0.5h, 2 = mid / 0.3h
  const double TGT[3] = {0.5, 0.9, 0.5}, STP[3] = {0.5, 0.5, 0.3};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); } if (I.size() < 370) continue;
    nDays++; const double A = d.atr, cost = 2 * A / 320.0; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    std::vector<double> O(n), Hh(n), Lw(n), Cc(n), path(n); std::vector<int> TM(n);
    { double hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); path[k] = hi > lo ? (Cc[k] - O[0]) / (hi - lo) : 0; } }
    // a trade from minute k0 (entry fills at price e on that minute: the minute itself can stop out only if it also traded through the stop
    // AFTER reaching e — unknowable on 1m bars, so a minute that touches both e and the stop counts as stopped)
    auto sim = [&](size_t k0, int dir, double e, double st, double tg, bool sameMin) -> double { double px = NAN;
      if (sameMin && (dir > 0 ? Lw[k0] <= st : Hh[k0] >= st)) px = st;
      for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (dir > 0 ? Lw[k] <= st : Hh[k] >= st) { px = st; break; } if (dir > 0 ? Hh[k] >= tg : Lw[k] <= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
      if (std::isnan(px)) px = Cc[n - 1]; return (dir * (px - e) - cost) / std::fabs(e - st); };
    std::vector<size_t> pool; for (size_t k = 0; k < n; ++k) if (TM[k] >= 960 && TM[k] < 1260) pool.push_back(k);
    for (int tf : {1, 3, 5}) { std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - 930) / tf != (TM[b.back().k0] - 930) / tf) b.push_back({Hh[k], Lw[k], Cc[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.k = k; } }
      for (int var = 0; var < 3; ++var) { int busyUntil = -1; double arSum = 0;
        for (int q = 0; q < (int)b.size(); ++q) { arSum += b[q].h - b[q].l; if (q < 12) continue; const int tmClose = TM[b[q].k] + 1; if (tmClose < 945 || tmClose >= 1260 || b[q].k <= busyUntil) continue;
          double top = -1e18, bot = 1e18; for (int r = q - 11; r <= q; ++r) { top = std::max(top, b[r].h); bot = std::min(bot, b[r].l); } const double h = top - bot, ar = arSum / (q + 1);
          if (!(h > 0) || h > 3 * ar || h < 4 * cost) continue;   // a box narrower than 4 costs cannot pay its target
          int nt = 0, nb = 0; for (int r = q - 11; r <= q; ++r) { if (b[r].h >= top - 0.15 * h) nt++; if (b[r].l <= bot + 0.15 * h) nb++; } if (nt < 2 || nb < 2) continue;
          // live for 30 chart bars: first touch of an edge by a later minute
          const size_t kEnd = std::min(n - 1, (size_t)b[std::min((int)b.size() - 1, q + 30)].k); int dir = 0; size_t kIn = 0;
          for (size_t k = b[q].k + 1; k <= kEnd && TM[k] < 1260; ++k) { const bool lo = Lw[k] <= bot, hi = Hh[k] >= top;
            if (Cc[k] < bot - STP[var] * h || Cc[k] > top + STP[var] * h) break;
            if (lo && !hi) { dir = 1; kIn = k; break; } if (hi && !lo) { dir = -1; kIn = k; break; } if (lo && hi) break; }
          if (!dir) { busyUntil = b[std::min((int)b.size() - 1, q + 1)].k; continue; }
          const double e = dir > 0 ? bot : top, st = e - dir * STP[var] * h, tg = dir > 0 ? bot + TGT[var] * h : top - TGT[var] * h;
          Row r{}; r.date = d.date; r.era = era; r.tf = tf; r.var = var; r.dir = dir;
          const double pth = path[kIn]; r.regime = std::fabs(pth) < 0.30 ? 0 : std::fabs(pth) >= 0.55 ? 2 : 1;
          r.against = dir * (Cc[kIn] - O[0]) > 0 ? false : true;   // long while the day is up = with the day; long while down = against
          r.R = sim(kIn, dir, e, st, tg, true);
          // exit minute → busy
          { size_t k = kIn; for (k = kIn + 1; k < n; ++k) { if (dir > 0 ? (Lw[k] <= st || Hh[k] >= tg) : (Hh[k] >= st || Lw[k] <= tg)) break; if (TM[k] >= 1315) break; } busyUntil = (int)k; }
          double sm = 0; for (int z = 0; z < 20; ++z) { size_t k = pool[rng() % pool.size()]; const double ce = Cc[k]; sm += sim(k, dir, ce, ce - dir * std::fabs(e - st), ce + dir * std::fabs(tg - e), false); } r.ctl = sm / 20;
          rows.push_back(r); } } } }
  printf("Intraday box fades — NQ NY 1/3/5m, 2010-07 → 2026-10, %ld days, %zu trades. R after 2 pts cost (ATR-scaled), flat 15:55, one at a time per chart.\n", nDays, rows.size());
  printf("Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26 · win · box trade minus its random-timing twin (same day, direction, stop, target) · trades a year.\n");
  const double yrs = nDays / 252.0;
  auto line = [&](const char* name, std::function<bool(const Row&)> f) { double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, w = 0, df = 0;
    for (auto& r : rows) if (f(r)) { m[r.era] += r.R; nn[r.era]++; w += r.R > 0; df += r.R - r.ctl; } const double N = nn[0] + nn[1] + nn[2];
    printf("  %-46s %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f) · win %2.0f%% · vs twin %+.3f [%.0f/yr]\n", name, nn[0] ? m[0] / nn[0] : NAN, nn[0], nn[1] ? m[1] / nn[1] : NAN, nn[1], nn[2] ? m[2] / nn[2] : NAN, nn[2], N ? 100 * w / N : 0, N ? df / N : NAN, N / yrs); };
  static const char* VN[3] = {"middle target, stop 0.5h", "opposite edge −0.1h, stop 0.5h", "middle target, stop 0.3h"};
  for (int tf : {3, 1, 5}) for (int var = 0; var < 3; ++var) { printf("\n== %dm, %s%s\n", tf, VN[var], tf == 3 && var == 0 ? "  (MAIN CELL)" : "");
    auto c = [=](const Row& r) { return r.tf == tf && r.var == var; };
    line("all", c);
    line("chop day at entry (path < 0.30)", [=](const Row& r) { return c(r) && r.regime == 0; });
    line("middle (0.30-0.55)", [=](const Row& r) { return c(r) && r.regime == 1; });
    line("trend day (>= 0.55)", [=](const Row& r) { return c(r) && r.regime == 2; });
    line("fade WITH the day's side (buy dips on up days)", [=](const Row& r) { return c(r) && !r.against; });
    line("fade AGAINST the day's side", [=](const Row& r) { return c(r) && r.against; });
    line("trend day + with the day", [=](const Row& r) { return c(r) && r.regime == 2 && !r.against; }); }
  return 0;
}
