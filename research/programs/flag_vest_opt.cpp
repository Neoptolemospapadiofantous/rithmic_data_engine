// 2026-10-20 flag_vest_opt.cpp (founder: "and can we optimize this even more"): the ACCOUNT side of book + TAKE on Vest $5k, chosen
// walk-forward: every parameter set is ranked on evaluations STARTED 2016-01 … 2021-12 and judged on starts 2022-01 … 2026-09.
// Grid: risk a trade for the book {30, 40, 50, 60} × for TAKE {40, 50, 75, 100}; max open positions {1, 2, unlimited}; cushion sizing
// (risk × 1.5 once the balance is >= +$150 / +$250, or off); daily stop {none, after the first win, after a −$100 day}. Score = pass % −
// fail % (an evaluation that never finishes counts neither). Trades and limits as flag_book_vest.cpp (below).
// (header of flag_book_vest.cpp follows)
// 2026-10-20 flag_book_vest.cpp (founder: "commit and run the book + TAKE test"): the morning BOOK (book F = cascade + gap retest +
// 10:00 gap rule side by side, 0.35 ATR stop, 2R — inputs_20261019/book_F_k.csv; and its first trade of the day, book_first_trade_k.csv)
// together with the /flags TAKE rule (NY bull 4/5m: 1st pullback + pole broke a level + gap with it + pole >= 5x bar; also + the 60-min move
// filter from flag_all_kpi), on a Vest $5k account: static $300 drawdown, $150 daily loss (no new entry once the day's realized P&L
// is <= −$150), +$500 target. Started on every month's first trading day 2016-01 … 2026-09 (129 starts), positions may overlap, P&L is
// realized at each exit and the limits are checked then; variants: $50 / $100 risk a trade, with / without the daily stop (no new entry
// after the day's first win or second loss). Book R = net pts (1 pt cost, edge_lab) / (0.35 × the edge_lab ATR); TAKE R after 2 pts ×
// ATR/320. Also: how often TAKE trades fall on book days and in the same direction, and the daily P&L correlation.
// The detection below is flag_all_kpi.cpp's (parity-checked page rules); everything after it is new.
// (header of flag_all_kpi.cpp follows)
// 2026-10-20 flag_all_kpi.cpp (founder: "what else can we analyse on each chart on each session last 10 years" → "do all test and lets
// optimize"). The /flags page's flags (parity-checked detection: flag_parity.cpp / scripts/flag_parity.sh), NY 09:30-16:00 (registration
// 09:45-14:00) and the overnight session split by registration time into ASIA (18:15-03:00) and LONDON (03:00-09:00); 1-5m charts,
// researched + inner tiers, both sides. Entry = the page's (2m trigger on 3-5m if first, else the pole-top stop), stop = flag low,
// exit = measured move (flag_exits.cpp: no exit beats it), flat 15:55 / 09:25. R after 2 pts × ATR/320; one position at a time per
// chart / tier / side. Reported on the LAST 10 YEARS: 2016-20 | 2021-24 | 2025-26 (earlier years only feed the walk-forward learning).
//  1 TIME OF DAY      flag R by 30-minute registration slot
//  2 VOLATILITY       prior VIX percentile (terciles), ATR percentile vs the last 250 days (terciles), VIX up yesterday
//  3 HIGHER TIMEFRAME 60-minute move with the flag (> +0.1 ATR / against < −0.1 ATR), close beyond the 15m EMA20 of the session, close
//                     beyond the prior RTH close (all mirrored for bears)
//  4 CALENDAR         weekday, month-end (last 2 trading days), option-expiry week, FOMC / NFP / CPI days
//  5 THE +1R DECISION trades that reach +1R: hold to the measured move vs bank 1R, split by how fast +1R came (≤ / > the median minutes)
//  6 VALUE            flag low within 0.1 ATR of the session VWAP; flag low inside yesterday's value area
//  7 ORDER FLOW       (aggressor data, 2023-03+) buy share of the flag bars' volume with the flag (terciles)
//  8 FAILED FLAGS     a flag that closes below 50 % of its pole traded the other way at that close, stop beyond the pole top, target
//                     the pole start
//  9 PROP             Vest $5k (static $300 drawdown, $150 daily loss, +$500 target, $50 risk a trade, daily stop: first win or second
//                     loss) started on every month's first trading day 2016-26: pass %, fail %, median trading days to pass
//  OPT walk-forward GRADE per session × chart group: each test year, every feature's edge (mean R with − without) on EARLIER years becomes
//      its weight; A = top third of training scores. Grade A per session and chart vs the plain rule.
// tday_min = minutes since 18:00 ET: 09:30 = 930, 09:45 = 945, 14:00 = 1200, 15:55 = 1315, 16:00 = 1320; 03:00 = 540, 09:00 = 900, 09:25 = 925.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k0, k; };
enum Feat { F_PB1, F_POLE5, F_BROKE, F_GAPW, F_B1230, F_M60W, F_M60A, F_EMA15, F_PCSIDE, F_VIXLO, F_VIXHI, F_ATRLO, F_ATRHI, F_VIXUP,
            F_MON, F_FRI, F_MEND, F_OPEX, F_EVENT, F_VWAP, F_INVA, NF };
static const char* FN[NF] = {"1st pullback", "pole >= 5x bar", "pole broke a level", "gap with it (NY)", "before 12:30 (NY)", "60-min move with it > 0.1 ATR",
  "60-min move against < -0.1 ATR", "close beyond the 15m EMA20", "beyond the prior RTH close", "VIX pct < 0.33", "VIX pct > 0.66", "ATR pct < 0.33", "ATR pct > 0.66",
  "VIX up yesterday (bull) / down (bear)", "Monday", "Friday", "month-end (last 2 days)", "option-expiry week", "FOMC / NFP / CPI day", "flag low near VWAP (0.1 ATR)",
  "flag low inside yesterday's value area"};
struct Row { int date, year, sess, side, tf, slot, tEntry; long long epIn, epOut; bool inner, f[NF]; double R, Rb, twin, risk, flowShare; bool flow;
             bool hit1; int min1; double Rhold1, Rbank1; };
struct Fail { int date, year, sess, side, tf; double R; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  // ATR percentile vs the previous 250 days
  std::map<int, double> atrPct; { std::deque<double> win; for (auto& d : s.days) { if (!(d.atr > 0)) continue; if (win.size() >= 60) { int below = 0; for (double a : win) below += a < d.atr; atrPct[d.date] = below / (double)win.size(); } win.push_back(d.atr); if (win.size() > 250) win.pop_front(); } }
  std::vector<Row> rows; std::vector<Fail> fails; long nDays = 0; std::mt19937 rng(20261020);
  const int S0[2] = {930, 0}, S1[2] = {1320, 930}, RG0[2] = {945, 15}, RG1[2] = {1200, 900}, FLAT[2] = {1315, 925};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; nDays++;
    const double A = d.atr, costA = 2 * A / 320.0, costB = 1.5;
    const double ap = atrPct.count(d.date) ? atrPct[d.date] : NAN;
    for (int sessIx = 0; sessIx < 2; ++sessIx) {
      std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[sessIx] && tm < S1[sessIx]) I.push_back(i); }
      if (I.size() < (sessIx == 0 ? 370u : 800u)) continue;
      const size_t n = I.size();
      double onh = -1e18, onl = 1e18; if (sessIx == 0) for (int i = d.b0; i < d.b1; ++i) { if (tday_min(s.t[i]) >= 930) break; onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
      double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < S0[sessIx] + 30; ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
      for (int side = 1; side >= -1; side -= 2) {
        std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), BV(n), VW(n), EMA15(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
        for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; BV[k] = side > 0 ? s.bv[i] : s.v[i] - s.bv[i];
          if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
        { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18, ema = NAN;
          for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
            VW[k] = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > VW[k]; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55;
            if ((TM[k] - S0[sessIx]) % 15 == 14) ema = std::isnan(ema) ? Cc[k] : ema + (2.0 / 21.0) * (Cc[k] - ema); EMA15[k] = ema; } }
        const double pcM = side * d.pc, gapA = std::isnan(d.pc) ? 0 : (O[0] - pcM) / A;
        auto sim = [&](size_t k0, double e, double st, double tg, bool fillMin, double cost, int& kOut, int* min1 = nullptr, double* rAt1 = nullptr) -> double {
          double px = NAN; kOut = n - 1; const double risk = e - st; bool hit = false;
          if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; kOut = k; break; }
            if (!hit && Hh[k] >= e + risk) { hit = true; if (min1) *min1 = (int)(k - k0); if (rAt1) *rAt1 = 1.0; }
            if (Hh[k] >= tg) { px = tg; kOut = k; break; } if (TM[k] >= FLAT[sessIx]) { px = O[k]; kOut = k; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / risk; };
        std::vector<size_t> twinPool; for (size_t k = 0; k < n; ++k) if (TM[k] >= RG0[sessIx] && TM[k] < RG1[sessIx]) twinPool.push_back(k);
        for (int tf = 1; tf <= 5; ++tf) for (int inner = 0; inner <= (tf <= 2 ? 1 : 0); ++inner) {
          std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - S0[sessIx]) / tf != (TM[b.back().k0] - S0[sessIx]) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], BV[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.bv += BV[k]; z.k = k; } }
          int busy = -1, lastOut = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
          for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
            const int tmc = TM[b[q].k] + 1; if (tmc < RG0[sessIx] || tmc >= RG1[sessIx]) continue;
            const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
            int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
            bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
            double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
            const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
            const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
            int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
            int nb = 0; size_t kPole = 0; double sPole = NAN, flo = fl0; bool failed = false; size_t kFail = 0;
            for (size_t kk = kc + 1; kk < n; ++kk) { if (Hh[kk] > H) { kPole = kk; sPole = flo - 0.25; break; } flo = std::min(flo, Lw[kk]);
              if ((TM[kk] - S0[sessIx]) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M) { failed = true; kFail = kk; break; } if (nb >= 12) break; } }
            busy = q + nb;
            const int sessMin = TM[kc] + 1;   // registration clock (tday_min)
            const int sess3 = sessIx == 0 ? 0 : (sessMin < 540 ? 1 : 2);
            // 8 failed flag the other way (mirrored: short in this side's units): entry at the failing close, stop pole top + 1 tick, target pole start
            if (failed && kFail) { const double e = Cc[kFail], stp = H + 0.25, tgt = L; double px = NAN;
              for (size_t k = kFail + 1; k < n && std::isnan(px); ++k) { if (Hh[k] >= stp) { px = stp; break; } if (Lw[k] <= tgt) { px = tgt; break; } if (TM[k] >= FLAT[sessIx]) { px = O[k]; break; } }
              if (std::isnan(px)) px = Cc[n - 1]; if (stp - e > 0.25) fails.push_back({d.date, d.date / 10000, sess3, side, tf, (e - px - costA) / (stp - e)}); }
            size_t k2m = 0; double e2m = NAN, s2m = NAN;
            if (tf >= 3) { double fl = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
              for (size_t k = kc + 1; k < n; ++k) { const int mm = TM[k] - S0[sessIx]; if (TM[k] + 1 >= RG1[sessIx] + 60) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
                if (mm % 2 == 1) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
                if (mm % tf == tf - 1) { nb2++; fl = std::min(fl, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
            const bool use2m = k2m && (!kPole || k2m <= kPole);
            const size_t kE = use2m ? k2m : kPole; if (!kE) continue; const double e = use2m ? e2m : H + 0.25, st = use2m ? s2m : sPole;
            if (!(e - st > 0.25) || (int)kE <= lastOut) continue;
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.sess = sess3; r.side = side; r.tf = tf; r.inner = inner; r.risk = e - st; r.tEntry = TM[kE]; r.epIn = s.t[I[kE]];
            { const int clock = (sessMin + 18 * 60) % (24 * 60); r.slot = clock / 30; }   // 30-min slot of the ET clock
            bool broke = std::floor(std::max(side * H, side * L) / 100) * 100 > std::min(side * H, side * L);
            for (double x : {side > 0 ? d.ph : -d.pl, side > 0 ? d.pl : -d.ph}) if (!std::isnan(x) && x > L && x <= H) broke = true;
            if (sessIx == 0) { for (double x : {side > 0 ? onh : -onl, side > 0 ? onl : -onh}) if (x > L && x <= H) broke = true;
              if (sessMin >= 960) for (double x : {side > 0 ? orh : -orl, side > 0 ? orl : -orh}) if (x > L && x <= H) broke = true; }
            const size_t k60 = kc >= 60 ? kc - 60 : 0; const double m60 = (Cc[kc] - Cc[k60]) / A;
            const double vixU = side > 0 ? -d.vixc1 : d.vixc1;
            r.f[F_PB1] = pb == 1; r.f[F_POLE5] = M >= 5 * ar; r.f[F_BROKE] = broke; r.f[F_GAPW] = sessIx == 0 && gapA > 0.1; r.f[F_B1230] = sessIx == 0 && sessMin < 1110;
            r.f[F_M60W] = m60 > 0.1; r.f[F_M60A] = m60 < -0.1; r.f[F_EMA15] = !std::isnan(EMA15[kc]) && Cc[kc] > EMA15[kc]; r.f[F_PCSIDE] = !std::isnan(pcM) && Cc[kc] > pcM;
            r.f[F_VIXLO] = !std::isnan(d.vixpct) && d.vixpct < 0.33; r.f[F_VIXHI] = !std::isnan(d.vixpct) && d.vixpct > 0.66;
            r.f[F_ATRLO] = !std::isnan(ap) && ap < 0.33; r.f[F_ATRHI] = !std::isnan(ap) && ap > 0.66; r.f[F_VIXUP] = !std::isnan(vixU) && vixU > 0;
            r.f[F_MON] = d.dow == 1; r.f[F_FRI] = d.dow == 5; r.f[F_MEND] = d.tdleft <= 1; r.f[F_OPEX] = d.opex; r.f[F_EVENT] = d.fomc || d.nfp || d.cpi;
            r.f[F_VWAP] = std::fabs(fl0 - VW[kc]) <= 0.1 * A;
            { const double va = side > 0 ? d.pval : -d.pvah, vb = side > 0 ? d.pvah : -d.pval; r.f[F_INVA] = !std::isnan(va) && fl0 >= std::min(va, vb) && fl0 <= std::max(va, vb); }
            { double fv = 0, fb = 0; for (int x = h + 1; x <= q; ++x) { fv += b[x].v; fb += b[x].bv; } r.flow = d.flow && fv > 0; r.flowShare = fv > 0 ? fb / fv : NAN; }
            int ko = kE, m1 = -1; double at1 = 0;
            r.R = sim(kE, e, st, H + M, !use2m, costA, ko, &m1, &at1); r.Rb = sim(kE, e, st, H + M, !use2m, costB, ko);
            r.hit1 = m1 >= 0; r.min1 = m1; r.Rhold1 = r.R; r.Rbank1 = (r.risk - costA) / r.risk;
            { double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = twinPool[rng() % twinPool.size()]; int k2; const double ce = Cc[k]; tw += sim(k, ce, ce - (e - st), ce + (H + M - e), false, costA, k2); } r.twin = tw / 5; }
            r.epOut = s.t[I[ko]]; lastOut = ko; rows.push_back(r); } } } } }
  // ── the book ───────────────────────────────────────────────────────────────
  struct T { int date; long long in, out; int dir; double R; int src; };   // src 0 book, 1 TAKE
  std::map<int, double> atrE; { std::ifstream f("research/programs/inputs_20261019/nq_atr_edge_lab.csv"); std::string ln; while (std::getline(f, ln)) { int d; double a; if (std::sscanf(ln.c_str(), "%d,%lf", &d, &a) == 2) atrE[d] = a; } }
  auto loadBook = [&](const char* path) { std::vector<T> v; std::ifstream f(path); std::string ln;
    while (std::getline(f, ln)) { int d, dir, srcx; long long a, b; double g, net, mae, mfe, k; if (std::sscanf(ln.c_str(), "%d,%lld,%lld,%d,%lf,%lf,%lf,%lf,%d,%lf", &d, &a, &b, &dir, &g, &net, &mae, &mfe, &srcx, &k) != 10) continue;
      if (d < 20160101 || !atrE.count(d)) continue; v.push_back({d, a, b, dir, net / (k * atrE[d]), 0}); }
    return v; };
  const auto bookF = loadBook("research/programs/inputs_20261019/book_F_k.csv"), bookFirst = loadBook("research/programs/inputs_20261019/book_first_trade_k.csv");
  auto takeRows = [&](bool m60) { std::vector<T> v; for (auto& r : rows) if (r.year >= 2016 && r.date <= 20261016 && r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.f[F_PB1] && r.f[F_BROKE] && r.f[F_GAPW] && r.f[F_POLE5] && (!m60 || r.f[F_M60W]))
      v.push_back({r.date, r.epIn, r.epOut, +1, r.R, 1}); return v; };
  const auto take = takeRows(false), takeM = takeRows(true);
  std::vector<int> days; for (auto& d : s.days) if (d.ok && d.date >= 20160101 && d.date <= 20261016) days.push_back(d.date);
  auto merge = [](std::vector<T> a, const std::vector<T>& b) { a.insert(a.end(), b.begin(), b.end()); return a; };
  printf("Book + TAKE on a Vest $5k account — started on the first trading day of each month 2016-01 … 2026-09. Since 2016: book F %zu trades, book first-trade %zu, TAKE %zu (+60-min filter %zu).\n",
         bookF.size(), bookFirst.size(), take.size(), takeM.size());
  // overlap
  { std::map<int, std::set<int>> bookDir; for (auto& t : bookF) bookDir[t.date].insert(t.dir); int onBook = 0, same = 0; for (auto& t : take) if (bookDir.count(t.date)) { ++onBook; if (bookDir[t.date].count(t.dir)) ++same; }
    printf("Overlap: %zu TAKE trades, %d on a book-F day (%.0f%%), %d of those with a book trade in the same direction.\n", take.size(), onBook, take.empty() ? 0 : 100.0 * onBook / take.size(), same);
    std::map<int, double> pb, pt; for (auto& t : bookF) pb[t.date] += t.R; for (auto& t : take) pt[t.date] += t.R; double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0, n = 0;
    for (int d : days) { const double x = pb.count(d) ? pb[d] : 0, y = pt.count(d) ? pt[d] : 0; sx += x; sy += y; sxx += x * x; syy += y * y; sxy += x * y; n++; }
    const double cor = (n * sxy - sx * sy) / std::sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
    printf("Daily R correlation, book F vs TAKE (all days 2016-26): %+.3f\n", cor); }
  // per-year R totals
  auto yearly = [&](const char* name, const std::vector<T>& v) { std::map<int, double> y; std::map<int, int> c; for (auto& t : v) { y[t.date / 10000] += t.R; c[t.date / 10000]++; }
    printf("  %-34s", name); for (int Y = 2016; Y <= 2026; ++Y) printf(" %d %+5.1fR(%3d)", Y % 100, y[Y], c[Y]); printf("\n"); };
  printf("\nR a year (trades):\n"); yearly("book F", bookF); yearly("book first trade", bookFirst); yearly("TAKE", take); yearly("TAKE + 60-min filter", takeM); yearly("book F + TAKE", merge(bookF, take)); yearly("book first + TAKE", merge(bookFirst, take));
  // ── account-side grid, walk-forward on start dates ─────────────────────────
  struct P { double rBook, rTake; int maxOpen; double cushion; int dstop; };   // dstop 0 none, 1 first win, 2 day <= −100
  auto vestP = [&](const std::vector<T>& v0, const P& p, int fromStart, int toStart, double& passPct, double& failPct, int& medDays, double& worst) {
    std::vector<T> v = v0; std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.in < b.in; });
    std::map<int, std::vector<const T*>> byDay; for (auto& t : v) byDay[t.date].push_back(&t);
    int starts = 0, pass = 0, fail = 0; std::vector<int> tdays; int lastMonth = -1; worst = 0;
    for (size_t si = 0; si < days.size(); ++si) { const int mo = days[si] / 100; if (mo == lastMonth) continue; lastMonth = mo; if (days[si] < fromStart || days[si] > toStart) continue; ++starts;
      double bal = 0; int res = 0, td = 0;
      for (size_t j = si; j < days.size() && !res; ++j) { ++td; auto it = byDay.find(days[j]); if (it == byDay.end()) continue;
        double day = 0; int wins = 0; struct O { const T* t; double usd; }; std::vector<O> open;
        auto realize = [&](long long upto) { for (size_t k = 0; k < open.size();) { if (open[k].t->out <= upto) { const double pnl = open[k].usd * open[k].t->R; bal += pnl; day += pnl; if (pnl > 0) ++wins; open.erase(open.begin() + k);
                if (bal <= -300) { res = -1; return; } if (bal >= 500) { res = 1; return; } } else ++k; } };
        for (auto* t : it->second) { realize(t->in); if (res) break;
          if (day <= -150) continue; if (p.dstop == 1 && wins >= 1) continue; if (p.dstop == 2 && day <= -100) continue;
          if (p.maxOpen > 0 && (int)open.size() >= p.maxOpen) continue;
          double usd = t->src == 1 ? p.rTake : p.rBook; if (p.cushion > 0 && bal >= p.cushion) usd *= 1.5;
          open.push_back({t, usd}); }
        if (!res) realize(LLONG_MAX); worst = std::min(worst, day); }
      if (res > 0) { ++pass; tdays.push_back(td); } else if (res < 0) ++fail; }
    std::sort(tdays.begin(), tdays.end()); passPct = starts ? 100.0 * pass / starts : 0; failPct = starts ? 100.0 * fail / starts : 0; medDays = tdays.empty() ? 0 : tdays[tdays.size() / 2]; return starts; };
  std::vector<P> grid; for (double rb : {30.0, 40.0, 50.0, 60.0}) for (double rt : {40.0, 50.0, 75.0, 100.0}) for (int mo : {1, 2, 0}) for (double cu : {0.0, 150.0, 250.0}) for (int ds : {0, 1, 2}) grid.push_back({rb, rt, mo, cu, ds});
  auto pname = [](const P& p) { char b[160]; std::snprintf(b, sizeof b, "book $%.0f · TAKE $%.0f · max open %s · cushion %s · daily stop %s", p.rBook, p.rTake, p.maxOpen ? (p.maxOpen == 1 ? "1" : "2") : "∞",
      p.cushion > 0 ? (p.cushion == 150 ? "×1.5 from +$150" : "×1.5 from +$250") : "off", p.dstop == 0 ? "none" : p.dstop == 1 ? "first win" : "−$100 day"); return std::string(b); };
  for (int which = 0; which < 2; ++which) {
    const auto v = which == 0 ? merge(bookFirst, take) : merge(bookF, take); const char* nm = which == 0 ? "BOOK FIRST TRADE + TAKE" : "BOOK F + TAKE";
    printf("\n================ %s — %zu parameter sets, ranked on starts 2016-21, judged on starts 2022-26 ================\n", nm, grid.size());
    struct Res { double score; size_t gi; double tp, tf; int td; double tw; };
    std::vector<Res> res; for (size_t gi = 0; gi < grid.size(); ++gi) { double pp, ff, w; int md; vestP(v, grid[gi], 20160101, 20211231, pp, ff, md, w); res.push_back({pp - ff, gi, pp, ff, md, w}); }
    std::sort(res.begin(), res.end(), [](const Res& a, const Res& b) { return a.score > b.score; });
    auto show = [&](const char* tag, const P& p) { double pp, ff, w, p2, f2, w2; int md, md2; vestP(v, p, 20160101, 20211231, pp, ff, md, w); vestP(v, p, 20220101, 20260930, p2, f2, md2, w2);
      printf("  %-9s %-92s | 2016-21: pass %3.0f%% fail %3.0f%% %3dd | 2022-26: pass %3.0f%% fail %3.0f%% %3dd · worst day $%.0f\n", tag, pname(p).c_str(), pp, ff, md, p2, f2, md2, std::min(w, w2)); };
    show("baseline", P{50, 50, 0, 0, 0});
    for (int k = 0; k < 8; ++k) { char tag[16]; std::snprintf(tag, 16, "#%d", k + 1); show(tag, grid[res[k].gi]); }
    // robustness: how the 2022-26 result of the top-20 (by 2016-21) compares with the median set
    std::vector<double> all2; for (auto& r : res) { double p2, f2, w2; int m2; vestP(v, grid[r.gi], 20220101, 20260930, p2, f2, m2, w2); all2.push_back(p2 - f2); }
    double top = 0; for (int k = 0; k < 20; ++k) top += all2[k]; std::vector<double> srt = all2; std::sort(srt.begin(), srt.end());
    printf("  2022-26 score (pass − fail): top-20 sets by 2016-21 average %+.1f · median of all %zu sets %+.1f · best possible %+.1f\n", top / 20, all2.size(), srt[srt.size() / 2], srt.back());
  }
  return 0;
}
