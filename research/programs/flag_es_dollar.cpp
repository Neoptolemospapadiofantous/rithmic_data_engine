// 2026-10-20 flag_es_dollar.cpp (founder: "test these correlated with es or uncorrelated with dollar?"). No new parameters on the rule:
//  A NQ flags conditioned on ES and macro, NY only: ES confirms (ES above its own 09:30 open and VWAP with a path >= 0.55 straight at
//    the flag's registration minute, mirrored), ES's 60-min move with / against the flag (0.1 ES ATR), NQ stronger / weaker than ES since
//    the open (in each market's ATR, ±0.1), and yesterday's broad-dollar change (usdc, > +0.2 % / < −0.2 % / flat) and 10-year yield
//    change (y10c, > +5 bp / < −5 bp / flat) — daily data, known before the open. Groups: NY bull 3-5m, its 1st pullbacks, the TAKE rule,
//    NY bear 3-5m. R 2010-18 | 2019-24 | 2025-26.
//  B the same detection and TAKE rule run unchanged on ES (cost 2 pts × ES ATR / 320, ES's own levels and gap) — does the edge exist
//    in a market the rule was never fitted to?
// Detection = flag_all_kpi.cpp's (parity-checked page rules), wrapped to run per market.
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
struct Row { int date, year, sess, side, tf, slot, tEntry, mk = 0; bool esHave = false, esConfirm = false; double es60 = NAN, rel = NAN, usdc = NAN, y10c = NAN; bool inner, f[NF]; double R, Rb, twin, risk, flowShare; bool flow;
             bool hit1; int min1; double Rhold1, Rbank1; };
struct Fail { int date, year, sess, side, tf; double R; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  Series es; if (!load_raw("data/edge_lab/cache", "ES", es, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(es, fomc, vix); load_macro("data/edge_lab/cache", es);
  std::unordered_map<int64_t, int> esIx; for (size_t i = 0; i < es.t.size(); ++i) esIx[es.t[i]] = (int)i;
  std::map<int, const Day*> esDay; for (auto& d : es.days) esDay[d.date] = &d;
  // ATR percentile vs the previous 250 days
  long nDays = 0; std::mt19937 rng(20261020);
  auto run = [&](Series& s, int mk, std::vector<Row>& rows, std::vector<Fail>& fails) {
  std::map<int, double> atrPct; { std::deque<double> win; for (auto& d : s.days) { if (!(d.atr > 0)) continue; if (win.size() >= 60) { int below = 0; for (double a : win) below += a < d.atr; atrPct[d.date] = below / (double)win.size(); } win.push_back(d.atr); if (win.size() > 250) win.pop_front(); } }

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
            Row r{}; r.date = d.date; r.year = d.date / 10000; r.sess = sess3; r.side = side; r.tf = tf; r.inner = inner; r.risk = e - st; r.tEntry = TM[kE]; r.mk = mk;
            if (mk == 0 && sessIx == 0) {   // ES at the registration minute (mirrored by side)
              auto itE = esIx.find(s.t[I[kc]]); auto itD = esDay.find(d.date);
              if (itE != esIx.end() && itD != esDay.end() && itD->second->ok && itD->second->atr > 0 && itD->second->rth0 >= 0) {
                const Day& ed = *itD->second; const int j1 = itE->second; double pv = 0, vv = 0, hi = -1e18, lo = 1e18; const double eo = es.o[ed.rth0];
                for (int j = ed.rth0; j <= j1 && j < ed.b1; ++j) { pv += (es.h[j] + es.l[j] + es.c[j]) / 3.0 * es.v[j]; vv += es.v[j]; hi = std::max(hi, (double)es.h[j]); lo = std::min(lo, (double)es.l[j]); }
                const double ec = es.c[j1], vw = vv > 0 ? pv / vv : ec, mv = side * (ec - eo), rng2 = hi - lo;
                r.esHave = true; r.esConfirm = mv > 0 && side * (ec - vw) > 0 && rng2 > 0 && mv / rng2 >= 0.55;
                const int j60 = std::max(ed.rth0, j1 - 60); r.es60 = side * (ec - es.c[j60]) / ed.atr;
                r.rel = side * (s.c[I[kc]] - s.o[I[0]]) / A - mv / ed.atr; } }
            r.usdc = d.usdc; r.y10c = d.y10c;
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
  };
  std::vector<Row> nq, esr; std::vector<Fail> nqF, esF;
  run(s, 0, nq, nqF); const long nqDays = nDays; nDays = 0; run(es, 1, esr, esF);
  auto era = [](int y) { return y < 2019 ? 0 : y < 2025 ? 1 : 2; };
  auto pr = [&](const std::vector<Row>& v, const char* name, std::function<bool(const Row&)> sel) { double m[3] = {0, 0, 0}, c[3] = {0, 0, 0};
    for (auto& r : v) if (sel(r)) { m[era(r.year)] += r.R; c[era(r.year)]++; } if (c[0] + c[1] + c[2] < 30) { printf("    %-46s (%.0f trades)\n", name, c[0] + c[1] + c[2]); return; }
    printf("    %-46s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)\n", name, c[0] ? m[0] / c[0] : NAN, c[0], c[1] ? m[1] / c[1] : NAN, c[1], c[2] ? m[2] / c[2] : NAN, c[2]); };
  printf("ES / dollar / yields vs the /flags NQ flags (NY), and the same rules on ES. NQ %ld days, ES %ld days. R after 2 pts × ATR/320 (each market's ATR).\n", nqDays, nDays);
  printf("Cells: R (trades) 2010-18 | 2019-24 | 2025-26.\n");
  struct G { const char* name; std::function<bool(const Row&)> f; };
  std::vector<G> groups = {
    {"NY bull 3-5m", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3; }},
    {"NY bull 3-5m, 1st pullback", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 3 && r.f[F_PB1]; }},
    {"NY bull TAKE (4/5m, 1st pb, broke, gap, pole >= 5x)", [](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf >= 4 && !r.inner && r.f[F_PB1] && r.f[F_BROKE] && r.f[F_GAPW] && r.f[F_POLE5]; }},
    {"NY bear 3-5m", [](const Row& r) { return r.sess == 0 && r.side < 0 && r.tf >= 3; }} };
  printf("\n================ A. NQ flags by ES and macro ================\n");
  for (auto& g : groups) { printf("\n  == %s\n", g.name); auto b = g.f;
    pr(nq, "all", b);
    pr(nq, "ES confirms (above its open + VWAP, straight)", [=](const Row& r) { return b(r) && r.esHave && r.esConfirm; });
    pr(nq, "ES does not confirm", [=](const Row& r) { return b(r) && r.esHave && !r.esConfirm; });
    pr(nq, "ES 60-min move with it > 0.1 ATR", [=](const Row& r) { return b(r) && r.esHave && r.es60 > 0.1; });
    pr(nq, "ES 60-min move against < -0.1 ATR", [=](const Row& r) { return b(r) && r.esHave && r.es60 < -0.1; });
    pr(nq, "NQ stronger than ES (> +0.1 ATR)", [=](const Row& r) { return b(r) && r.esHave && r.rel > 0.1; });
    pr(nq, "NQ about equal to ES", [=](const Row& r) { return b(r) && r.esHave && std::fabs(r.rel) <= 0.1; });
    pr(nq, "NQ weaker than ES (< -0.1 ATR)", [=](const Row& r) { return b(r) && r.esHave && r.rel < -0.1; });
    pr(nq, "dollar UP yesterday (> +0.2 %)", [=](const Row& r) { return b(r) && r.usdc > 0.2; });
    pr(nq, "dollar flat yesterday (|chg| <= 0.2 %)", [=](const Row& r) { return b(r) && !std::isnan(r.usdc) && std::fabs(r.usdc) <= 0.2; });
    pr(nq, "dollar DOWN yesterday (< -0.2 %)", [=](const Row& r) { return b(r) && r.usdc < -0.2; });
    pr(nq, "10y yield UP yesterday (> +5 bp)", [=](const Row& r) { return b(r) && r.y10c > 5; });
    pr(nq, "10y yield flat (|chg| <= 5 bp)", [=](const Row& r) { return b(r) && !std::isnan(r.y10c) && std::fabs(r.y10c) <= 5; });
    pr(nq, "10y yield DOWN yesterday (< -5 bp)", [=](const Row& r) { return b(r) && r.y10c < -5; }); }
  printf("\n================ B. the same rules on ES (never fitted there) ================\n");
  for (auto& g : groups) pr(esr, g.name, g.f);
  for (int tf = 3; tf <= 5; ++tf) { char nm[64]; snprintf(nm, 64, "ES NY bull %dm, 1st pullback", tf); pr(esr, nm, [=](const Row& r) { return r.sess == 0 && r.side > 0 && r.tf == tf && r.f[F_PB1]; }); }
  printf("  NQ for comparison:\n"); for (auto& g : groups) pr(nq, g.name, g.f);
  return 0;
}
