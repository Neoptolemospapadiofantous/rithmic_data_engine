// 2026-10-20 flag_flow_check.cpp (founder: "what about order flow to filter trades?"): flag_all_kpi.cpp with only the order-flow
// report — the aggressor-buy share of the flag bars (with the flag), terciles + "drop the buyers' third", on NY bull 3-5m (all / 1st pullback /
// TAKE) and NY bear 3-5m, flow days 2023-03+. The cut points come from the same data (one look, 3.5 years).
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
struct Row { int date, year, sess, side, tf, slot, tEntry; bool inner, f[NF]; double R, Rb, twin, risk, flowShare; bool flow;
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
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.sess = sess3; r.side = side; r.tf = tf; r.inner = inner; r.risk = e - st; r.tEntry = TM[kE];
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
            lastOut = ko; rows.push_back(r); } } } } }
  { // scratch: order flow in the flag bars (buy share with the flag) on NY bull 3-5m and the TAKE rule, flow days 2023-03+
    auto rep = [&](const char* nm, std::function<bool(const Row&)> sel) { std::vector<double> v; for (auto& r : rows) if (sel(r) && r.flow) v.push_back(r.flowShare); if (v.size() < 30) { printf("%s: %zu flow trades\n", nm, v.size()); return; }
      std::sort(v.begin(), v.end()); const double c1 = v[v.size() / 3], c2 = v[2 * v.size() / 3];
      for (int t = 0; t < 4; ++t) { double s2 = 0, n = 0, w = 0; for (auto& r : rows) if (sel(r) && r.flow) { const bool in = t == 0 ? r.flowShare < c1 : t == 1 ? (r.flowShare >= c1 && r.flowShare < c2) : t == 2 ? r.flowShare >= c2 : r.flowShare < c2; if (!in) continue; s2 += r.R; n++; w += r.R > 0; }
        printf("%-34s %-26s n %4.0f  avg %+.3fR  win %2.0f%%\n", nm, t == 0 ? "low buy share (sellers)" : t == 1 ? "mid" : t == 2 ? "high buy share (buyers)" : "FILTER: low+mid (drop high)", n, n ? s2 / n : 0, n ? 100 * w / n : 0); } printf("   cuts %.3f / %.3f\n", c1, c2); };
    rep("NY bull 3-5m, all flags", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3; });
    rep("NY bull 3-5m, 1st pullback", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3 && r.f[F_PB1]; });
    rep("NY bull 4/5m TAKE", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.f[F_PB1] && r.f[F_BROKE] && r.f[F_GAPW] && r.f[F_POLE5]; });
    rep("NY bear 3-5m, all flags", [](const Row& r) { return r.sess == 0 && r.side < 0 && r.tf >= 3; });
    return 0; }
  // ── reporting ─────────────────────────────────────────────────────────────
  auto era = [](int y) { return y < 2016 ? -1 : y <= 2020 ? 0 : y <= 2024 ? 1 : 2; };
  static const char* SN[3] = {"NY", "ASIA", "LONDON"};
  printf("All flag KPIs, per session and chart — NQ, %ld days, %zu flag trades (+ %zu failed flags). Last 10 years shown: 2016-20 | 2021-24 | 2025-26.\n", nDays, rows.size(), fails.size());
  printf("R per trade after 2 pts × ATR/320, page entry / stop / measured move. Cells: R (n). Groups: 1-2m (both tiers) and 3-5m.\n");
  auto cell = [&](std::function<bool(const Row&)> sel, double* out3, double* n3, double* diffTwin = nullptr) { for (int i = 0; i < 3; ++i) { out3[i] = 0; n3[i] = 0; } double tw = 0, nt = 0;
    for (auto& r : rows) { const int e = era(r.year); if (e < 0 || !sel(r)) continue; out3[e] += r.R; n3[e]++; tw += r.R - r.twin; nt++; } for (int i = 0; i < 3; ++i) out3[i] = n3[i] ? out3[i] / n3[i] : NAN; if (diffTwin) *diffTwin = nt ? tw / nt : NAN; };
  auto pr = [&](const char* name, std::function<bool(const Row&)> sel, bool twin = false) { double m[3], c[3], dt; cell(sel, m, c, &dt); if (c[0] + c[1] + c[2] < 40) return;
    printf("    %-44s %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f)", name, m[0], c[0], m[1], c[1], m[2], c[2]); if (twin) printf(" · vs twin %+.3f", dt); printf("\n"); };
  for (int side : {1, -1}) for (int sess = 0; sess < 3; ++sess) for (int g = 0; g < 2; ++g) {
    auto base = [=](const Row& r) { return r.side == side && r.sess == sess && (g == 0 ? r.tf <= 2 : r.tf >= 3); };
    printf("\n######## %s %s %s\n", side > 0 ? "BULL" : "BEAR", SN[sess], g == 0 ? "1-2m" : "3-5m");
    pr("ALL", base, true);
    for (int tf = (g == 0 ? 1 : 3); tf <= (g == 0 ? 2 : 5); ++tf) { char nm[48]; snprintf(nm, 48, "  %dm", tf); pr(nm, [=](const Row& r) { return base(r) && r.tf == tf; }); }
    printf("  1 time of day (30-min slot, ET)\n");
    for (int sl = 0; sl < 48; ++sl) { char nm[48]; snprintf(nm, 48, "    %02d:%02d", sl / 2, sl % 2 * 30); pr(nm, [=](const Row& r) { return base(r) && r.slot == sl; }); }
    printf("  2-4, 6 features: with | without\n");
    for (int f = 0; f < NF; ++f) { if (f == F_PB1 || f == F_POLE5 || f == F_BROKE) continue; if ((f == F_GAPW || f == F_B1230) && sess != 0) continue;
      double m1[3], c1[3], m0[3], c0[3]; cell([=](const Row& r) { return base(r) && r.f[f]; }, m1, c1); cell([=](const Row& r) { return base(r) && !r.f[f]; }, m0, c0);
      if (c1[0] + c1[1] + c1[2] < 40) continue;
      printf("    %-44s %+.3f / %+.3f / %+.3f (%4.0f)  vs without %+.3f / %+.3f / %+.3f\n", FN[f], m1[0], m1[1], m1[2], c1[0] + c1[1] + c1[2], m0[0], m0[1], m0[2]); }
    printf("  5 the +1R decision (trades that reached +1R): hold to the measured move vs bank 1R\n");
    { std::vector<int> mins; for (auto& r : rows) if (base(r) && era(r.year) >= 0 && r.hit1) mins.push_back(r.min1); if (mins.size() > 40) { std::sort(mins.begin(), mins.end()); const int med = mins[mins.size() / 2];
        for (int fast = 1; fast >= 0; --fast) { double h[3] = {0, 0, 0}, bk[3] = {0, 0, 0}, c[3] = {0, 0, 0}; for (auto& r : rows) { const int e = era(r.year); if (e < 0 || !base(r) || !r.hit1 || ((r.min1 <= med) != (bool)fast)) continue; h[e] += r.Rhold1; bk[e] += r.Rbank1; c[e]++; }
          printf("    +1R %s (%s %d min): hold %+.3f / %+.3f / %+.3f · bank %+.3f / %+.3f / %+.3f (%.0f)\n", fast ? "fast" : "slow", fast ? "<=" : ">", med, c[0] ? h[0] / c[0] : NAN, c[1] ? h[1] / c[1] : NAN, c[2] ? h[2] / c[2] : NAN, c[0] ? bk[0] / c[0] : NAN, c[1] ? bk[1] / c[1] : NAN, c[2] ? bk[2] / c[2] : NAN, c[0] + c[1] + c[2]); } } }
    printf("  7 order flow (2023-03+): buy share of the flag bars with the flag\n");
    { std::vector<double> v; for (auto& r : rows) if (base(r) && r.flow && era(r.year) >= 0) v.push_back(r.flowShare); if (v.size() > 90) { std::sort(v.begin(), v.end()); const double c1 = v[v.size() / 3], c2 = v[2 * v.size() / 3];
        for (int t = 0; t < 3; ++t) { char nm[64]; snprintf(nm, 64, "    flow tercile %s (cuts %.2f / %.2f)", t == 0 ? "low" : t == 1 ? "mid" : "high", c1, c2);
          pr(nm, [=](const Row& r) { return base(r) && r.flow && (t == 0 ? r.flowShare < c1 : t == 1 ? (r.flowShare >= c1 && r.flowShare < c2) : r.flowShare >= c2); }); } } }
    printf("  8 failed flags traded the other way\n");
    { double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}; for (auto& f : fails) { const int e = era(f.year); if (e < 0 || f.side != side || f.sess != sess || (g == 0 ? f.tf > 2 : f.tf < 3)) continue; m[e] += f.R; c[e]++; }
      printf("    %-44s %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f)\n", "failed flag → other way", c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2]); }
  }
  // OPT walk-forward grade per session × group × side with every feature
  printf("\n================ OPTIMIZED: walk-forward grade with every feature (weights + cuts on EARLIER years; test 2016-26) ================\n");
  std::map<std::tuple<int, int, int, int>, char> gradeOf;   // (date, sess*10+tf, side, idx) not needed; grade stored per row index
  std::vector<char> grade(rows.size(), 0);
  for (int side : {1, -1}) for (int sess = 0; sess < 3; ++sess) for (int g = 0; g < 2; ++g) {
    auto base = [=](const Row& r) { return r.side == side && r.sess == sess && (g == 0 ? r.tf <= 2 : r.tf >= 3); };
    for (int Y = 2016; Y <= 2026; ++Y) {
      double w[NF]; for (int f = 0; f < NF; ++f) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (auto& r : rows) if (base(r) && r.year < Y) { if (r.f[f]) { s1 += r.R; n1++; } else { s0 += r.R; n0++; } } w[f] = (n1 >= 40 && n0 >= 40) ? s1 / n1 - s0 / n0 : 0; }
      auto score = [&](const Row& r) { double sc = 0; for (int f = 0; f < NF; ++f) if (r.f[f]) sc += w[f]; return sc; };
      std::vector<double> tr; for (auto& r : rows) if (base(r) && r.year < Y) tr.push_back(score(r)); if (tr.size() < 120) continue; std::sort(tr.begin(), tr.end());
      const double c1 = tr[tr.size() / 3], c2 = tr[2 * tr.size() / 3];
      for (size_t i = 0; i < rows.size(); ++i) if (base(rows[i]) && rows[i].year == Y) { const double sc = score(rows[i]); grade[i] = sc > c2 ? 'A' : sc > c1 ? 'B' : 'C'; }
      if (Y == 2026) { printf("  %s %s %s 2026 weights (top 6):", side > 0 ? "bull" : "bear", SN[sess], g == 0 ? "1-2m" : "3-5m"); std::vector<std::pair<double, int>> ws; for (int f = 0; f < NF; ++f) ws.push_back({w[f], f}); std::sort(ws.rbegin(), ws.rend()); for (int k = 0; k < 6; ++k) printf(" %s %+.2f ·", FN[ws[k].second], ws[k].first); printf("\n"); } } }
  for (int side : {1, -1}) for (int sess = 0; sess < 3; ++sess) { printf("\n  -- %s %s\n", side > 0 ? "BULL" : "BEAR", SN[sess]);
    for (int tf = 1; tf <= 5; ++tf) for (char gr : {'A', 'B', 'C'}) { char nm[64]; snprintf(nm, 64, "%dm grade %c", tf, gr);
      double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}, mb = 0, nb2 = 0; for (size_t i = 0; i < rows.size(); ++i) { auto& r = rows[i]; const int e = era(r.year); if (e < 0 || r.side != side || r.sess != sess || r.tf != tf || grade[i] != gr) continue; m[e] += r.R; c[e]++; if (e == 2) { mb += r.Rb; nb2++; } }
      if (c[0] + c[1] + c[2] < 30) continue;
      printf("    %-14s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f) · 2025-26 at a fixed 1.5-pt cost %+.3f · %.0f a year\n", nm, c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2], nb2 ? mb / nb2 : NAN, (c[0] + c[1] + c[2]) / 10.8); } }
  // 9 PROP: Vest $5k on grade-A NY bull 4/5m (walk-forward) and on the fixed TAKE rule, started each month 2016-26
  printf("\n================ 9 PROP: Vest $5k (static $300 DD, $150 daily loss, +$500 target, $50 risk/trade, daily stop first win / 2nd loss) ================\n");
  auto prop = [&](const char* name, std::function<bool(size_t)> sel) {
    std::map<int, std::vector<std::pair<int, double>>> byDay; for (size_t i = 0; i < rows.size(); ++i) if (rows[i].year >= 2016 && sel(i)) byDay[rows[i].date].push_back({rows[i].tEntry, rows[i].R});
    for (auto& [dd, v] : byDay) std::sort(v.begin(), v.end());   // in time order within the trading day
    std::vector<int> days; for (auto& d : s.days) if (d.ok && d.date >= 20160101 && d.date <= 20261016) days.push_back(d.date);
    int starts = 0, pass = 0, fail = 0; std::vector<int> tdays; int lastMonth = -1;
    for (size_t si = 0; si < days.size(); ++si) { const int mo = days[si] / 100; if (mo == lastMonth) continue; lastMonth = mo; ++starts;
      double bal = 0; int res = 0, td = 0;
      for (size_t j = si; j < days.size() && !res; ++j) { ++td; auto it = byDay.find(days[j]); if (it == byDay.end()) continue; double day = 0; int losses = 0;
        for (auto& [tE, R] : it->second) { (void)tE; const double pnl = 50.0 * R; day += pnl; bal += pnl; if (bal <= -300) { res = -1; break; } if (bal >= 500) { res = 1; break; } if (pnl > 0) break; if (++losses >= 2) break; if (day <= -150) break; } }
      if (res > 0) { ++pass; tdays.push_back(td); } else if (res < 0) ++fail; }
    std::sort(tdays.begin(), tdays.end());
    printf("  %-44s starts %d · pass %.0f%% · fail %.0f%% · unresolved %.0f%% · median trading days to pass %d\n", name, starts, 100.0 * pass / starts, 100.0 * fail / starts, 100.0 * (starts - pass - fail) / starts, tdays.empty() ? 0 : tdays[tdays.size() / 2]); };
  prop("fixed TAKE rule (NY bull 4/5m)", [&](size_t i) { auto& r = rows[i]; return r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.f[F_PB1] && r.f[F_BROKE] && r.f[F_GAPW] && r.f[F_POLE5]; });
  prop("grade A, NY bull 4/5m", [&](size_t i) { auto& r = rows[i]; return r.sess == 0 && r.side > 0 && r.tf >= 4 && grade[i] == 'A'; });
  prop("grade A, NY bull 3-5m", [&](size_t i) { auto& r = rows[i]; return r.sess == 0 && r.side > 0 && r.tf >= 3 && grade[i] == 'A'; });
  prop("grade A, NY both sides 3-5m", [&](size_t i) { auto& r = rows[i]; return r.sess == 0 && r.tf >= 3 && grade[i] == 'A'; });
  prop("grade A, every session / chart / side", [&](size_t i) { return grade[i] == 'A'; });
  (void)gradeOf;
  return 0;
}
