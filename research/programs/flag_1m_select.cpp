// 2026-10-20 flag_1m_select.cpp (founder: "1m are the good ones — just need to select the good ones"). The /flags page's 1-MINUTE
// flags (researched + inner tiers; parity-checked detection), NY / Asia / London, bull and bear. Main cost = a FIXED 1.5 NQ pts a round trip
// (1m stops are small — the volatility-scaled cost of the other programs understates it before 2019); entry = the pole-top stop order,
// stop = flag low, exit = the measured move. Candidate traits (all known at the entry): 1st pullback, pole >= 5x bar, pole broke a level,
// gap with it (NY), before 12:30 (NY), 60-min move with it > 0.1 / > 0.3 ATR, beyond the 15m EMA20, beyond the prior close, flag low near
// VWAP, researched tier (pole >= 4x and a straight session) vs inner, stop >= 10x / 20x the cost, pole >= 0.15 / 0.25 ATR, shallow pullback
// (<= 30 % of the pole), the 5-minute trend agreeing (close beyond a rising 5m EMA20). Selection, WALK-FORWARD (each test year 2016-2026
// uses only earlier years): (1) the best rule of up to 3 traits (>= 150 training trades), (2) a weighted grade (Σ trait edges; A = top
// third of training scores), (3) each trait alone with / without. Then the selected 1m trades go into the Vest Monte Carlo with the plan.
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
struct Row { int date, year, sess, side, tf, slot, tEntry; double poleAtr, pull, m60a, Rf; bool ema5, research; long long epIn, epOut; bool inner, f[NF]; double R, Rb, twin, risk, flowShare; bool flow;
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
        std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), BV(n), VW(n), EMA15(n), EMA5(n), EMA5p(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
        for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; BV[k] = side > 0 ? s.bv[i] : s.v[i] - s.bv[i];
          if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
        { const double so = O[0]; double pv = 0, vv = 0, hi = -1e18, lo = 1e18, ema = NAN, e5 = NAN, e5p = NAN;
          for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
            VW[k] = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > VW[k]; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55;
            if ((TM[k] - S0[sessIx]) % 15 == 14) ema = std::isnan(ema) ? Cc[k] : ema + (2.0 / 21.0) * (Cc[k] - ema); EMA15[k] = ema;
            if ((TM[k] - S0[sessIx]) % 5 == 4) { e5p = e5; e5 = std::isnan(e5) ? Cc[k] : e5 + (2.0 / 21.0) * (Cc[k] - e5); } EMA5[k] = e5; EMA5p[k] = e5p; } }
        const double pcM = side * d.pc, gapA = std::isnan(d.pc) ? 0 : (O[0] - pcM) / A;
        auto sim = [&](size_t k0, double e, double st, double tg, bool fillMin, double cost, int& kOut, int* min1 = nullptr, double* rAt1 = nullptr) -> double {
          double px = NAN; kOut = n - 1; const double risk = e - st; bool hit = false;
          if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
          for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; kOut = k; break; }
            if (!hit && Hh[k] >= e + risk) { hit = true; if (min1) *min1 = (int)(k - k0); if (rAt1) *rAt1 = 1.0; }
            if (Hh[k] >= tg) { px = tg; kOut = k; break; } if (TM[k] >= FLAT[sessIx]) { px = O[k]; kOut = k; break; } }
          if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / risk; };
        std::vector<size_t> twinPool; for (size_t k = 0; k < n; ++k) if (TM[k] >= RG0[sessIx] && TM[k] < RG1[sessIx]) twinPool.push_back(k);
        for (int tf = 1; tf <= 1; ++tf) for (int inner = 0; inner <= 1; ++inner) {
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
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.sess = sess3; r.side = side; r.tf = tf; r.inner = inner; r.risk = e - st; r.tEntry = TM[kE]; r.epIn = s.t[I[kE]]; r.poleAtr = M / A; r.pull = pull; r.m60a = (Cc[kc] - Cc[kc >= 60 ? kc - 60 : 0]) / A; r.research = !inner;
            r.ema5 = !std::isnan(EMA5[kc]) && !std::isnan(EMA5p[kc]) && Cc[kc] > EMA5[kc] && EMA5[kc] > EMA5p[kc];
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
            r.R = sim(kE, e, st, H + M, !use2m, costA, ko, &m1, &at1); r.Rb = sim(kE, e, st, H + M, !use2m, costB, ko); r.Rf = r.Rb; r.epOut = s.t[I[ko]];
            r.hit1 = m1 >= 0; r.min1 = m1; r.Rhold1 = r.R; r.Rbank1 = (r.risk - costA) / r.risk;
            { double tw = 0; for (int z = 0; z < 5; ++z) { const size_t k = twinPool[rng() % twinPool.size()]; int k2; const double ce = Cc[k]; tw += sim(k, ce, ce - (e - st), ce + (H + M - e), false, costA, k2); } r.twin = tw / 5; }
            lastOut = ko; rows.push_back(r); } } } } }
  // ── 1m selection ──────────────────────────────────────────────────────────
  enum { T_PB1, T_POLE5, T_BROKE, T_GAPW, T_B1230, T_M60, T_M60S, T_EMA15, T_PCSIDE, T_VWAP, T_RES, T_RISK10, T_RISK20, T_PATR15, T_PATR25, T_SHALLOW, T_EMA5, NT };
  static const char* TN[NT] = {"1st pullback", "pole >= 5x bar", "pole broke a level", "gap with it (NY)", "before 12:30 (NY)", "60-min move > 0.1 ATR", "60-min move > 0.3 ATR",
    "beyond the 15m EMA20", "beyond the prior close", "flag low near VWAP", "researched tier (not inner)", "stop >= 10x cost", "stop >= 20x cost", "pole >= 0.15 ATR", "pole >= 0.25 ATR",
    "shallow pullback (<= 30 %)", "5m trend agrees (rising 5m EMA20)"};
  auto tr = [&](const Row& r, int t) -> bool { switch (t) {
    case T_PB1: return r.f[F_PB1]; case T_POLE5: return r.f[F_POLE5]; case T_BROKE: return r.f[F_BROKE]; case T_GAPW: return r.f[F_GAPW]; case T_B1230: return r.f[F_B1230];
    case T_M60: return r.m60a > 0.1; case T_M60S: return r.m60a > 0.3; case T_EMA15: return r.f[F_EMA15]; case T_PCSIDE: return r.f[F_PCSIDE]; case T_VWAP: return r.f[F_VWAP];
    case T_RES: return r.research; case T_RISK10: return r.risk >= 15.0; case T_RISK20: return r.risk >= 30.0; case T_PATR15: return r.poleAtr >= 0.15; case T_PATR25: return r.poleAtr >= 0.25;
    case T_SHALLOW: return r.pull <= 0.30; case T_EMA5: return r.ema5; } return false; };
  auto era = [](int y) { return y < 2016 ? -1 : y <= 2020 ? 0 : y <= 2024 ? 1 : 2; };
  static const char* SN[3] = {"NY", "ASIA", "LONDON"};
  printf("1-MINUTE flag selection — NQ, %ld days, %zu 1m flag trades. R after a FIXED 1.5-pt cost (ATR-scaled cost in brackets where shown).\n", nDays, rows.size());
  printf("Cells: R (trades) 2016-20 | 2021-24 | 2025-26 · trades a year over the 10.8 test years.\n");
  // one position at a time per session/side (both tiers share the account): skip a trade entering before the previous one exited
  auto oneAtATime = [&](std::vector<const Row*> v) { std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->date != b->date ? a->date < b->date : a->epIn < b->epIn; });
    std::vector<const Row*> out; int lastDate = 0; long long lastOut = 0; for (auto* r : v) { if (r->date == lastDate && r->epIn < lastOut) continue; out.push_back(r); lastDate = r->date; lastOut = r->epOut; } return out; };
  auto pr = [&](const char* name, std::vector<const Row*> v, bool one = true) { if (one) v = oneAtATime(v); double m[3] = {0, 0, 0}, c[3] = {0, 0, 0}, a2 = 0, w = 0, N = 0;
    for (auto* r : v) { const int e = era(r->year); if (e < 0) continue; m[e] += r->Rf; c[e]++; N++; w += r->Rf > 0; if (e == 2) a2 += r->R; }
    if (N < 20) { printf("    %-50s (%.0f trades)\n", name, N); return; }
    printf("    %-50s %+.3f (%5.0f) | %+.3f (%5.0f) | %+.3f (%4.0f) [ATR-cost 2025-26 %+.3f] · win %2.0f%% · %.0f/yr\n", name, c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2], c[2] ? a2 / c[2] : NAN, 100 * w / N, N / 10.8); };
  std::map<std::string, std::vector<const Row*>> chosenSets;
  for (int side : {1, -1}) for (int sess = 0; sess < 3; ++sess) {
    auto base = [=](const Row& r) { return r.side == side && r.sess == sess; };
    std::vector<const Row*> all; for (auto& r : rows) if (base(r)) all.push_back(&r);
    printf("\n######## 1m %s %s\n", side > 0 ? "BULL" : "BEAR", SN[sess]);
    pr("ALL 1m flags", all);
    printf("  traits alone (with):\n");
    for (int t = 0; t < NT; ++t) { if ((t == T_GAPW || t == T_B1230) && sess != 0) continue; std::vector<const Row*> v; for (auto* r : all) if (tr(*r, t)) v.push_back(r); pr(TN[t], v); }
    // (1) walk-forward best rule of <= 3 traits
    std::vector<std::vector<int>> rules; for (int a = 0; a < NT; ++a) { rules.push_back({a}); for (int b = a + 1; b < NT; ++b) { rules.push_back({a, b}); for (int c = b + 1; c < NT; ++c) rules.push_back({a, b, c}); } }
    std::vector<const Row*> wf; std::string last;
    for (int Y = 2016; Y <= 2026; ++Y) { double bm = 0; int best = -1;
      for (size_t ri = 0; ri < rules.size(); ++ri) { double sm = 0, nn = 0; for (auto* r : all) if (r->year < Y) { bool ok = true; for (int t : rules[ri]) if (!tr(*r, t)) { ok = false; break; } if (ok) { sm += r->Rf; nn++; } }
        if (nn >= 150 && sm / nn > bm) { bm = sm / nn; best = (int)ri; } }
      if (best < 0) continue; for (auto* r : all) if (r->year == Y) { bool ok = true; for (int t : rules[best]) if (!tr(*r, t)) { ok = false; break; } if (ok) wf.push_back(r); }
      std::string nm; for (int t : rules[best]) { if (!nm.empty()) nm += " + "; nm += TN[t]; } char yb[16]; snprintf(yb, 16, "%d", Y); if (nm != last) { printf("      rule from %s: %s (train %+.3fR)\n", yb, nm.c_str(), bm); last = nm; } }
    pr("(1) WALK-FORWARD best rule (<= 3 traits)", wf);
    // (2) walk-forward grade
    std::vector<const Row*> gA, gB, gC;
    for (int Y = 2016; Y <= 2026; ++Y) { double w[NT]; for (int t = 0; t < NT; ++t) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (auto* r : all) if (r->year < Y) { if (tr(*r, t)) { s1 += r->Rf; n1++; } else { s0 += r->Rf; n0++; } } w[t] = (n1 >= 100 && n0 >= 100) ? s1 / n1 - s0 / n0 : 0; }
      auto sc = [&](const Row* r) { double x = 0; for (int t = 0; t < NT; ++t) if (tr(*r, t)) x += w[t]; return x; };
      std::vector<double> trn; for (auto* r : all) if (r->year < Y) trn.push_back(sc(r)); if (trn.size() < 300) continue; std::sort(trn.begin(), trn.end()); const double c1 = trn[trn.size() / 3], c2 = trn[2 * trn.size() / 3], c3 = trn[9 * trn.size() / 10];
      for (auto* r : all) if (r->year == Y) { const double x = sc(r); (x > c2 ? gA : x > c1 ? gB : gC).push_back(r); if (x > c3) chosenSets[std::string(side > 0 ? "bull" : "bear") + SN[sess] + "_top10"].push_back(r); }
      if (Y == 2026) { std::vector<std::pair<double, int>> ws; for (int t = 0; t < NT; ++t) ws.push_back({w[t], t}); std::sort(ws.rbegin(), ws.rend()); printf("      2026 trait weights (top 5): "); for (int k = 0; k < 5; ++k) printf("%s %+.2f · ", TN[ws[k].second], ws[k].first); printf("\n"); } }
    pr("(2) grade A (top third of scores)", gA); pr("    grade B", gB); pr("    grade C", gC); pr("    top 10 % of scores", chosenSets[std::string(side > 0 ? "bull" : "bear") + SN[sess] + "_top10"]);
    chosenSets[std::string(side > 0 ? "bull" : "bear") + SN[sess] + "_A"] = gA; chosenSets[std::string(side > 0 ? "bull" : "bear") + SN[sess] + "_wf"] = wf;
  }
  return 0;
}
