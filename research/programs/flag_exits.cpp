// 2026-10-20 flag_exits.cpp (founder: "is there a way to know to trail close or do 1R or trail lose" — after the 20:13 Asia short ran
// 70 pts and still stopped out under the measured-move target). Same flags, sessions, entries and stops as flag_quick_target.cpp (below);
// EXITS here: E0 measured move | E1 half at +1R, rest to the measured move with the stop at break-even | E2 after +1R trail 1R behind the
// best price (loose) | E3 after +1R trail 0.5R (tight) | E4 after +1R trail under the last 3 one-minute lows | E5 1R chandelier from entry
// | E6 fixed 1R target. Same-minute rule: the stop is checked first, the trail moves on the minute's high for the NEXT minute.
// Each trade also gets 5 random-timing twins (same day / session, same direction, same stop distance, same exit) — an exit that only
// looks good because price trends is caught there. WALK-FORWARD CHOOSER: bucket = session × chart × pole >= 5x bar × 1st pullback;
// each test year 2013-2026 a bucket uses the exit with the best mean R on EARLIER years (>= 50 trades), else the measured move.
// (header of flag_quick_target.cpp follows)
// 2026-10-20 (founder, after Globex 2026-10-19 evening: 8 flag entries, all stopped at the flag low after the measured-move target was
// missed — "the fill or the way this is being played out is incorrect. I see that each entry at least had 5-10 points"; 4 of 6 ran
// +12 to +22 pts first). Question: on the /flags page's flags, does a SMALL target (5-15 pts at today's volatility) beat the measured
// move? Exits were only tested on NY 3-5m flags before (flag_take_opt §2: measured move > 1.5/2/3R); 1-2m and Globex never.
// Flags = the page's exact detection (bot/frontend/src/lib/flags.ts, parity-checked by scripts/flag_parity.sh): researched tier
// (pole >= 4x bar, open + VWAP + straightness >= 0.55) and inner tier (1-2m, pole >= 2x bar, open + VWAP only, rejected by the
// researched rules). Sessions: NY 09:30-16:00 (registration 09:45-14:00, flat 15:55) and Globex 18:00-09:30 (registration 18:15-09:00,
// flat 09:25); the session open / VWAP / low of day are the session's own. Entry: 3-5m the 2m trigger if before the pole-top break,
// else the pole-top stop order (H + 1 tick); 1-2m the pole-top stop order. Stop = the page's (flag low − 1 tick). One position at a
// time per chart, session and side.
// Exits: measured move (H + pole) | fixed target 5 / 8 / 10 / 15 "points at today's volatility" (k × ATR / 356, ATR 356 = 2026-10-19)
// | 0.5R / 1R. Costs: (a) 2 pts × ATR / 320 (as all earlier flag programs), (b) a FIXED 1.5 NQ points per round trip (commission +
// a tick or two — closer to what a contract really pays; much larger than (a) in 2010-18 when NQ was 2,000-7,000).
// Also: share of trades whose best move before the stop reaches +5 / +10 (today-points). Read-only on data/edge_lab/cache.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 14:00 = 1200, 15:55 = 1315, 16:00 = 1320; Globex 18:15 = 15, 09:00 = 900, 09:25 = 925.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
enum { E0, E1, E2, E3, E4, E5, E6, NE };
static const char* EN[NE] = {"E0 measured move (the page)", "E1 half at +1R, rest MM, BE", "E2 trail 1R after +1R (loose)", "E3 trail 0.5R after +1R (tight)", "E4 trail 3 one-min lows after +1R", "E5 1R chandelier from entry", "E6 fixed 1R target"};
struct Row { int date, year, era, sess, side, tf; bool inner, pb1, pole5; double risk, Ra[NE], Rb[NE], Ta[NE]; int kIn, kOut; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<Row> rows; long nDays = 0; std::mt19937 tw_rng(20261020);
  const int S0[2] = {930, 0}, S1[2] = {1320, 930}, RG0[2] = {945, 15}, RG1[2] = {1200, 900}, FLAT[2] = {1315, 925};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; nDays++;
    const double A = d.atr, todayPt = A / 356.0; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    const double costA = 2 * A / 320.0, costB = 1.5;
    for (int sess = 0; sess < 2; ++sess) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[sess] && tm < S1[sess]) I.push_back(i); }
      if (I.size() < (sess == 0 ? 370u : 800u)) continue;
      const size_t n = I.size();
      for (int side = 1; side >= -1; side -= 2) {
        std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
        for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
          if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
        { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18;
          for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
            const double vw = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > vw; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
        // exit engine: entry minute k0 at price e, initial stop st, measured-move target mm; returns R (after `cost`) and the exit minute.
        // fillMin = the entry minute itself can stop (pole-top entries). Each minute: stop first, then the target(s), then the trail moves
        // on that minute's high / lows for the next minute. E1 = two halves: one at +1R, the other on to mm with the stop at break-even.
        auto sim = [&](int ex, size_t k0, double e, double st, double mm, bool fillMin, double cost, int& kOut) -> double {
          const double risk = e - st; double stop = st, best = e, px = NAN, half = NAN; bool armed = false; kOut = n - 1;
          const double tg = ex == E6 ? e + risk : (ex == E1 ? mm : (ex == E0 ? mm : 1e18));
          if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) {
            if (Lw[k] <= stop) { px = stop; kOut = k; break; }
            if (ex == E1 && std::isnan(half) && Hh[k] >= e + risk) { half = e + risk; stop = std::max(stop, e); }
            if (Hh[k] >= tg) { px = tg; kOut = k; break; }
            if (TM[k] >= FLAT[sess]) { px = O[k]; kOut = k; break; }
            best = std::max(best, Hh[k]);
            if (!armed && best >= e + risk) armed = true;
            if (ex == E2 && armed) stop = std::max(stop, best - risk);
            if (ex == E3 && armed) stop = std::max(stop, best - 0.5 * risk);
            if (ex == E4 && armed && k >= 2) stop = std::max(stop, std::min({Lw[k], Lw[k - 1], Lw[k - 2]}) - 0.25);
            if (ex == E5) stop = std::max(stop, best - risk);
          }
          if (std::isnan(px)) px = Cc[n - 1];
          const double full = (px - e - cost) / risk;
          return ex == E1 && !std::isnan(half) ? 0.5 * ((half - e - cost) / risk) + 0.5 * full : full; };
        std::vector<size_t> twinPool; for (size_t k = 0; k < n; ++k) if (TM[k] >= RG0[sess] && TM[k] < RG1[sess]) twinPool.push_back(k);
        for (int tf = 1; tf <= 5; ++tf) for (int inner = 0; inner <= (tf <= 2 ? 1 : 0); ++inner) {
          std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - S0[sess]) / tf != (TM[b.back().k0] - S0[sess]) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } }
          int busy = -1, lastOut = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
          for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
            const int tmc = TM[b[q].k] + 1; if (tmc < RG0[sess] || tmc >= RG1[sess]) continue;
            const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
            int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
            bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
            double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
            const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
            const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
            int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
            // the page's follow (busy) and entries
            int nb = 0; size_t kPole = 0; double sPole = NAN, flo = fl0;
            for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; break; } flo = std::min(flo, Lw[kk]);
              if ((TM[kk] - S0[sess]) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
            busy = q + nb;
            size_t k2m = 0; double e2m = NAN, s2m = NAN;
            if (tf >= 3) { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
              for (size_t k = kc + 1; k < n; ++k) { const int mm = TM[k] - S0[sess]; if (TM[k] + 1 >= RG1[sess] + 60) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
                if (mm % 2 == 1) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
                if (mm % tf == tf - 1) { nb2++; fl = std::min(fl, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
            const bool use2m = k2m && (!kPole || k2m <= kPole);
            const size_t kE = use2m ? k2m : kPole; if (!kE) continue; const double e = use2m ? e2m : H + 0.25, st = use2m ? s2m : sPole;
            if (!(e - st > 0.25) || (int)kE <= lastOut) continue;   // one position at a time per chart / tier
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.era = era; r.sess = sess; r.side = side; r.tf = tf; r.inner = inner; r.pb1 = pb == 1; r.pole5 = M >= 5 * ar; r.risk = e - st; r.kIn = kE;
            int ko = kE;
            for (int x = 0; x < NE; ++x) { r.Ra[x] = sim(x, kE, e, st, H + M, !use2m, costA, ko); if (x == E0) r.kOut = ko; r.Rb[x] = sim(x, kE, e, st, H + M, !use2m, costB, ko);
              double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = twinPool[tw_rng() % twinPool.size()]; int k2; const double ce = Cc[k]; tw += sim(x, k, ce, ce - (e - st), ce + (H + M - e), false, costA, k2); } r.Ta[x] = tw / 5; }
            lastOut = r.kOut; rows.push_back(r); } } } } }
  printf("Exits on the /flags page's flags — NQ 2010-07 → 2026-10, %ld days, %zu trades (NY + Globex, 1-5m, researched + inner tiers).\n", nDays, rows.size());
  printf("Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26 after 2 pts × ATR/320 · 2025-26 at a fixed 1.5-pt cost · win %% · flag minus its random-timing twin (same exit).\n");
  auto line = [&](const char* name, std::function<bool(const Row&)> sel, int x) { double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}, b2 = 0, n2 = 0, w = 0, N = 0, tw = 0;
    for (auto& r : rows) if (sel(r)) { m[r.era] += r.Ra[x]; c[r.era]++; N++; w += r.Ra[x] > 0; tw += r.Ra[x] - r.Ta[x]; if (r.era == 2) { b2 += r.Rb[x]; n2++; } }
    if (N < 30) return; printf("  %-38s %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f) · fixed %+.3f · win %2.0f%% · vs twin %+.3f\n", name, c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2], n2 ? b2 / n2 : NAN, 100 * w / N, tw / N); };
  static const char* SN[2] = {"NY", "GLOBEX/ASIA/LONDON"};
  struct Grp { const char* name; std::function<bool(const Row&)> f; };
  std::vector<Grp> groups = {
    {"NY bull 4-5m TAKE-like (1st pb, pole >= 5x)", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.pb1 && r.pole5; }},
    {"NY bull 3-5m", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3; }},
    {"NY bear 3-5m", [](const Row& r) { return r.sess == 0 && r.side < 0 && r.tf >= 3; }},
    {"NY 1-2m (both sides, both tiers)", [](const Row& r) { return r.sess == 0 && r.tf <= 2; }},
    {"Globex 3-5m (both sides)", [](const Row& r) { return r.sess == 1 && r.tf >= 3; }},
    {"Globex 1-2m (both sides, both tiers) — like 10-19 evening", [](const Row& r) { return r.sess == 1 && r.tf <= 2; }} };
  for (auto& g : groups) { printf("\n== %s\n", g.name); for (int x = 0; x < NE; ++x) line(EN[x], g.f, x); }
  // walk-forward chooser per bucket (session × chart × pole >= 5x × 1st pullback): best exit on earlier years (>= 50 trades), else E0
  printf("\n== WALK-FORWARD EXIT CHOOSER (bucket = session × chart × pole >= 5x × 1st pullback; exit picked on EARLIER years only), test years 2013-26\n");
  auto bucket = [](const Row& r) { return r.sess * 100 + r.tf * 10 + (r.pole5 ? 2 : 0) + (r.pb1 ? 1 : 0); };
  std::map<std::pair<int, int>, int> choice;   // (bucket, year) → exit
  { std::map<int, std::vector<const Row*>> byB; for (auto& r : rows) byB[bucket(r)].push_back(&r);
    for (auto& [bk, v] : byB) for (int Y = 2013; Y <= 2026; ++Y) { int best = E0; double bm = -1e9, nn = 0; double sm[NE] = {0}; for (auto* r : v) if (r->year < Y) { nn++; for (int x = 0; x < NE; ++x) sm[x] += r->Ra[x]; }
      if (nn >= 50) for (int x = 0; x < NE; ++x) if (sm[x] / nn > bm) { bm = sm[x] / nn; best = x; } choice[{bk, Y}] = best; } }
  for (auto& g : groups) { double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}, m0[3] = {0, 0, 0}, b2 = 0, n2 = 0; int used[NE] = {0};
    for (auto& r : rows) if (r.year >= 2013 && g.f(r)) { const int x = choice[{bucket(r), r.year}]; used[x]++; m[r.era] += r.Ra[x]; m0[r.era] += r.Ra[E0]; c[r.era]++; if (r.era == 2) { b2 += r.Rb[x]; n2++; } }
    printf("  %-58s chooser %+.3f | %+.3f | %+.3f (fixed %+.3f)  vs always E0 %+.3f | %+.3f | %+.3f  · exits used:", g.name, c[0] ? m[0] / c[0] : NAN, c[1] ? m[1] / c[1] : NAN, c[2] ? m[2] / c[2] : NAN, n2 ? b2 / n2 : NAN, c[0] ? m0[0] / c[0] : NAN, c[1] ? m0[1] / c[1] : NAN, c[2] ? m0[2] / c[2] : NAN);
    for (int x = 0; x < NE; ++x) if (used[x]) printf(" E%d %d", x, used[x]); printf("\n"); }
  (void)SN;
  return 0;
}
