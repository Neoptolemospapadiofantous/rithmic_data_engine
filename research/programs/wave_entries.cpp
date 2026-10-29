// 2026-10-28 wave_entries.cpp (founder: "instead of ema cross for entry try other methods" + "possible volume profile or volume or
// volatility or order flow" + "levels possibly? previous day or week or hourly" + "test all"; goal: SD mean reversion, as good an
// entry as possible, 2-3 trades a day). The VWAP-SD wave of ema721_wave_search.cpp with the EMA 7/21 cross replaced by other
// entry triggers, and each trigger crossed with context filters.
//  SETUP   chart TF 1m / 5m, VWAP + volume-weighted SD from 18:00 ET (typical price). A chart bar trades past VWAP ± k SD and a later
//          close is back inside → armed (a close beyond the band waits for the next close-back; the excursion's extreme is kept;
//          armed state expires 60 min after the last bar past the band). The entry must still be on the band's side of VWAP.
//          Stop = the excursion extreme + 1 tick (min 2 pts). All trades flat 16:40 ET. One position at a time per config.
//  TRIGGERS (T)  reclose = the arming close itself · ema = EMA7/21 cross toward VWAP (the baseline) · brk1 = a close beyond the
//          arming bar's low (short) / high (long) · brk3 = a close beyond the lowest low / highest high of the previous 3 chart bars
//          (small structure break) · two = a second close inside, further toward VWAP than the one before · rsi = RSI(2) on chart
//          closes crosses back below 70 (short) / above 30 (long) · retest = LIMIT at the band after arming: fills on the first
//          1-minute bar that trades 1 tick through the band (fill = band, or that minute's open if it opens past it); a stop hit
//          inside the fill minute counts · delta = a chart bar whose aggressor delta (2·buy − volume) points toward VWAP and closes
//          toward VWAP (order-flow days only, NQ 2023-03 →).
//  FILTERS (F) none · lvlPD prior RTH high / low · lvlPW last week's RTH high / low · lvlVP prior day POC / VAH / VAL + the naked
//          POCs · lvlHr the previous clock hour's high / low · lvlRnd a 100-point round number · lvlAny any of those. A level
//          "counts" when the wave reached it (within 0.05 ATR) and the entry is back on the VWAP side of it. · volHi the bar that
//          made the extreme had ≥ 1.5× the average volume of the 20 chart bars before it · volLo ≤ 0.8× · atrHi today's ATR ≥ 1.1×
//          its 60-session average · atrLo ≤ 0.9× · vixHi prior VIX close ≥ 20 · vixLo < 16 · absorb (order flow) the extreme bar's
//          delta pointed AWAY from VWAP (aggressors pushing the wave, and it failed) · exhaust its delta already pointed toward VWAP.
//  EXITS (X) 1R · 2R · VWAP (live, exit at the open when VWAP is behind the entry).
//  WINDOWS (W) entry Asia 18:00-03:00 · London 03:00-09:30 · NYopen 09:30-10:30 · NYmid 10:30-12:00 · NYpm 12:00-16:30 · ALL.
//  GRID    2 TF × 3 k (1 / 1.5 / 2) × 6 W × 8 T × 15 F × 3 X = 38,880 configs. Costs NQ 1.5 / ES 0.5 pts round trip, points at today's ATR.
//  SELECT  (fixed before the run) price/volume filters: 2010-20 n ≥ 150, PF ≥ 1.15, both 2010-15 and 2016-20 > 0 → 2021-24 net > 0,
//          PF ≥ 1.05 → 2025-01 → 2026-10-23 net > 0 → ES the same config net > 0 with PF ≥ 1.05 over 2010-26 (join the two CSVs).
//          Order-flow configs (T delta, F absorb / exhaust; NQ only — ES has no aggressor volume before 2026-09): 2023-03 → 2024 n ≥ 60,
//          PF ≥ 1.15 → 2025-26 net > 0. With this many configs dozens will pass the first gate by chance; ES (and the holdout) decide.
// Output: out/wave_entries_<SYM>.csv (every config) + the NQ-selected list on stdout. Usage: wave_entries [NQ|ES] [shuffle SEED].
// CONTROL "shuffle SEED": every filter column is permuted across the signals of each (TF, k, trigger) group before the trades run —
// the filters keep their pass rates but carry no information, so the counts passing the gates are what chance alone produces.
// Fixes 2026-10-28 (after the first run): the retest stop uses the extreme known BEFORE the fill bar; naked POCs (picked relative
// to the 09:30 open) only count for entries at or after 09:30.
// tday_min = minutes since 18:00 ET: 03:00 = 540, 09:30 = 930, 16:30 = 1350, 16:40 = 1360.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
enum { NTF = 2, NK = 3, NW = 6, NT = 8, NF = 15, NX = 3, NE = 5 };
static const int TFS[NTF] = {1, 5};
static const double KS[NK] = {1, 1.5, 2};
static const char* WN[NW] = {"Asia", "London", "NYopen", "NYmid", "NYpm", "ALL"};
static const char* TN[NT] = {"reclose", "ema", "brk1", "brk3", "two", "rsi", "retest", "delta"};
static const char* FN[NF] = {"none", "lvlPD", "lvlPW", "lvlVP", "lvlHr", "lvlRnd", "lvlAny", "volHi", "volLo", "atrHi", "atrLo", "vixHi", "vixLo", "absorb", "exhaust"};
static const char* XN[NX] = {"1R", "2R", "VWAP"};
// eras: 0 2010-15 · 1 2016-20 · 2 2021-24 · 3 2025-26 · 4 order-flow in-sample 2023-24 (also counted in era 2)
struct Acc { double s[NE] = {}, n[NE] = {}, gw[NE] = {}, gl[NE] = {}; };
static Acc acc[NTF][NK][NW][NT][NF][NX];
int main(int argc, char** argv) {
  const std::string sym = argc > 1 ? argv[1] : "NQ"; const int shuf = argc > 3 && std::string(argv[2]) == "shuffle" ? std::atoi(argv[3]) : 0; std::mt19937 rng(shuf); const double cost = sym == "ES" ? 0.5 : 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<int> good; for (size_t di = 0; di < s.days.size(); ++di) { const Day& d = s.days[di]; if (d.ok && !d.rollwin && d.atr > 0) good.push_back((int)di); }
  const double Anow = s.days[good.back()].atr; const int FLAT = 1360; const double TICK = 0.25;
  // ATR vs its 60-session average (known before the day)
  std::vector<double> atrR(s.days.size(), NAN);
  { double sum = 0; std::vector<double> q; for (int di : good) { if (q.size() >= 60) atrR[di] = s.days[di].atr / (sum / q.size());
      q.push_back(s.days[di].atr); sum += s.days[di].atr; if (q.size() > 60) { sum -= q.front(); q.erase(q.begin()); } } }
  // VWAP / SD from 18:00 on 1m bars; previous clock hour's high / low per 1m bar
  std::vector<float> VW(s.size(), NAN), SDV(s.size(), NAN), HH(s.size(), NAN), HL(s.size(), NAN);
  { int cur = -1; double pv = 0, pq = 0, vv = 0; int64_t hr = -1; double ch = NAN, cl = NAN, ph = NAN, pl = NAN;
    for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i];
      const int64_t h = s.t[i] / 3600; if (h != hr) { if (hr >= 0 && h == hr + 1) { ph = ch; pl = cl; } else ph = pl = NAN; hr = h; ch = s.h[i]; cl = s.l[i]; }
      else { ch = std::max(ch, (double)s.h[i]); cl = std::min(cl, (double)s.l[i]); }
      HH[i] = (float)ph; HL[i] = (float)pl;
      if (di < 0) continue; if (di != cur) { cur = di; pv = pq = vv = 0; }
      const double p = (s.h[i] + s.l[i] + s.c[i]) / 3.0, v = std::max(1.0f, s.v[i]); pv += p * v; pq += p * p * v; vv += v;
      VW[i] = (float)(pv / vv); SDV[i] = (float)std::sqrt(std::max(0.0, pq / vv - (pv / vv) * (pv / vv))); } }
  auto era = [](int date) { const int y = date / 10000; return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  // one trade from 1m bar j0 (first bar checked), entry e. sameBar: j0 is the fill minute — only the stop is checked there.
  auto trade = [&](size_t j0, double e, int dir, double R, int x, bool sameBar) -> double {
    const int di = s.day_of[j0];
    for (size_t j = j0; j < s.size() && s.day_of[j] == di; ++j) { const int tm = tday_min(s.t[j]);
      if (tm >= FLAT) return dir * (s.o[j] - e);
      const double adv = dir > 0 ? s.l[j] - e : e - s.h[j], fav = dir > 0 ? s.h[j] - e : e - s.l[j];
      if (adv <= -R) return (sameBar && j == j0) ? -R : std::min(-R, dir * (s.o[j] - e));
      if (sameBar && j == j0) continue;
      double tg = x == 0 ? R : x == 1 ? 2 * R : 0;
      if (x == 2) { const double vw = VW[j - 1]; if (std::isnan(vw)) continue; tg = dir * (vw - e); if (tg <= 0) return dir * (s.o[j] - e); }
      if (fav >= tg) return tg; }
    return NAN; };
  long nsig = 0;
  for (int ti = 0; ti < NTF; ++ti) { const int tf = TFS[ti];
    struct CB { size_t i0, i1; double o, h, l, c, v, dl; bool flow; };
    std::vector<CB> cb;
    { long curB = -1; int curD = -2; CB b{};
      for (size_t i = 0; i < s.size(); ++i) { const int di = s.day_of[i]; if (di < 0) continue; const long bk = tday_min(s.t[i]) / tf;
        const double dl = 2.0 * s.bv[i] - s.v[i];
        if (di != curD || bk != curB) { if (curB >= 0) cb.push_back(b); curD = di; curB = bk; b = {i, i, s.o[i], s.h[i], s.l[i], s.c[i], s.v[i], dl, s.days[di].flow}; }
        else { b.i1 = i; b.h = std::max(b.h, (double)s.h[i]); b.l = std::min(b.l, (double)s.l[i]); b.c = s.c[i]; b.v += s.v[i]; b.dl += dl; } }
      if (curB >= 0) cb.push_back(b); }
    const size_t nb = cb.size();
    std::vector<int8_t> G(nb); std::vector<float> RS(nb), VA(nb, NAN);
    { double e7 = cb[0].c, e21 = cb[0].c, up = 0, dn = 0, sv = 0;
      for (size_t q = 0; q < nb; ++q) { e7 += 2.0 / 8 * (cb[q].c - e7); e21 += 2.0 / 22 * (cb[q].c - e21); G[q] = e7 > e21 ? 1 : -1;
        if (q) { const double ch = cb[q].c - cb[q - 1].c; up += (std::max(ch, 0.0) - up) / 2; dn += (std::max(-ch, 0.0) - dn) / 2; }
        RS[q] = (float)(up + dn > 0 ? 100 * up / (up + dn) : 50);
        if (q >= 20) VA[q] = (float)(sv / 20); sv += cb[q].v; if (q >= 20) sv -= cb[q - 20].v; } }
    for (int ki = 0; ki < NK; ++ki) { const double kk = KS[ki];
      for (int T = 0; T < NT; ++T) {
        struct Sig { size_t j0; double e; bool same; int dir; double R, A; int w, era, di; bool f[NF]; int64_t t; };
        std::vector<Sig> sigs;
        for (int side = 0; side < 2; ++side) { const int dir = side == 0 ? -1 : 1;   // -1 = short at the upper band
          bool ex = false, on = false; double top = 0, armH = 0, armL = 0, prevIn = 0; size_t last = 0, exQ = 0, armQ = 0; int curDay = -1;
          for (size_t q = 3; q < nb; ++q) { const CB& c = cb[q]; const size_t i = c.i1; const int di = s.day_of[i];
            if (di != curDay) { curDay = di; ex = on = false; }
            const double vw = VW[i], sd = SDV[i]; const int tm = tday_min(s.t[i]); if (std::isnan(vw) || !(sd > 0)) continue;
            if (ex && q - last > (size_t)(60 / tf)) ex = on = false;
            const double band = vw - dir * kk * sd, ext = dir < 0 ? c.h : c.l;
            // the limit-retest trigger reads the band as it stood at the previous chart close
            const double bandPrev = VW[cb[q - 1].i1] - dir * kk * SDV[cb[q - 1].i1];
            bool fired = false; size_t j0 = i + 1; double e = c.c; bool same = false; const double topBefore = top;
            if (T == 6 && on && q > armQ && s.day_of[cb[q - 1].i1] == di && !std::isnan(bandPrev)) {
              for (size_t j = c.i0; j <= c.i1; ++j) { const double thr = bandPrev - dir * TICK;
                if (dir < 0 ? s.h[j] >= thr : s.l[j] <= thr) { e = dir < 0 ? std::max(bandPrev, (double)s.o[j]) : std::min(bandPrev, (double)s.o[j]); j0 = j; same = true; fired = true; break; } } }
            if (-dir * (ext - band) > 0) { if (!ex || -dir * (ext - top) > 0) { top = ext; exQ = q; } ex = true; last = q;
              if (-dir * (c.c - band) >= 0) on = false; else if (!on) { on = true; armQ = q; armH = c.h; armL = c.l; prevIn = c.c; } }
            if (!fired) {
              if (!on) continue;
              const bool cross = G[q] == dir && G[q - 1] != dir;
              switch (T) {
                case 0: fired = q == armQ; break;
                case 1: fired = cross; break;
                case 2: fired = q > armQ && (dir < 0 ? c.c < armL : c.c > armH); break;
                case 3: { double lo = 1e18, hi = -1e18; for (size_t z = q - 3; z < q; ++z) { lo = std::min(lo, cb[z].l); hi = std::max(hi, cb[z].h); }
                  fired = dir < 0 ? c.c < lo : c.c > hi; break; }
                case 4: fired = q > armQ && -dir * (c.c - band) < 0 && dir * (c.c - prevIn) > 0; if (q > armQ && !fired) prevIn = c.c; break;
                case 5: fired = dir < 0 ? (RS[q - 1] >= 70 && RS[q] < 70) : (RS[q - 1] <= 30 && RS[q] > 30); break;
                case 7: fired = c.flow && dir * c.dl > 0 && dir * (c.c - c.o) > 0; break;
                default: break; }
              if (!fired) continue; }
            on = false; ex = false;
            const Day& d = s.days[di]; if (tm < 15 || tm >= 1350 || !d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261023) continue;
            if (dir * (vw - e) <= 0) continue;
            Sig g; g.j0 = j0; g.e = e; g.same = same; g.dir = dir; g.A = d.atr; g.R = std::max(-dir * ((same ? topBefore : top) - e) + TICK, 2.0); g.di = di; g.t = s.t[j0];
            g.w = tm < 540 ? 0 : tm < 930 ? 1 : tm < 990 ? 2 : tm < 1080 ? 3 : 4; g.era = era(d.date);
            // filters: a level counts when the wave reached it (within tol) and the entry is back on the VWAP side of it
            const double tol = 0.05 * d.atr;
            auto lvl = [&](double L) { return !std::isnan(L) && (dir < 0 ? (top >= L - tol && e < L) : (top <= L + tol && e > L)); };
            const size_t ie = cb[exQ].i1;
            const double rnd = dir < 0 ? std::floor(top / 100 + 0.5) * 100 : std::floor(top / 100 + 0.5) * 100;
            g.f[0] = true; g.f[1] = lvl(dir < 0 ? d.ph : d.pl); g.f[2] = lvl(dir < 0 ? d.pwh : d.pwl);
            g.f[3] = lvl(d.ppoc) || lvl(dir < 0 ? d.pvah : d.pval) || (tm >= 930 && (lvl(d.npoca) || lvl(d.npocb)));
            g.f[4] = lvl(dir < 0 ? HH[ie] : HL[ie]); g.f[5] = lvl(rnd);
            g.f[6] = g.f[1] || g.f[2] || g.f[3] || g.f[4] || g.f[5];
            const double va = VA[exQ]; g.f[7] = !std::isnan(va) && cb[exQ].v >= 1.5 * va; g.f[8] = !std::isnan(va) && cb[exQ].v <= 0.8 * va;
            g.f[9] = atrR[di] >= 1.1; g.f[10] = atrR[di] <= 0.9; g.f[11] = d.vix >= 20; g.f[12] = d.vix < 16;
            g.f[13] = cb[exQ].flow && dir * cb[exQ].dl < 0; g.f[14] = cb[exQ].flow && dir * cb[exQ].dl > 0;
            sigs.push_back(g); } }
        std::sort(sigs.begin(), sigs.end(), [](const Sig& a, const Sig& b) { return a.t < b.t; }); nsig += sigs.size();
        // shuffle control: each filter column permuted across this group's signals (keeps every filter's pass rate, breaks its link to the trade)
        if (shuf) for (int f = 1; f < NF; ++f) for (size_t a = sigs.size(); a > 1; --a) { const size_t b = rng() % a; std::swap(sigs[a - 1].f[f], sigs[b].f[f]); }
        static int64_t busy[NW][NF][NX]; for (auto& a : busy) for (auto& b : a) for (auto& x : b) x = 0;
        for (const Sig& g : sigs) { const int yr = s.days[g.di].date / 10000;
          for (int x = 0; x < NX; ++x) { double pts = NAN; int64_t exitT = 0;
            for (int ww = 0; ww < 2; ++ww) { const int w = ww == 0 ? g.w : 5;
              for (int f = 0; f < NF; ++f) { if (!g.f[f] || g.t < busy[w][f][x]) continue;
                if (std::isnan(pts)) { pts = trade(g.j0, g.e, g.dir, g.R, x, g.same); if (std::isnan(pts)) break;
                  // exit time: walk again to find it cheaply — approximate by the R / target scale is not safe, so re-walk
                  exitT = g.t + 60; { const int di = g.di; for (size_t j = g.j0; j < s.size() && s.day_of[j] == di; ++j) { exitT = s.t[j] + 60;
                      if (tday_min(s.t[j]) >= FLAT) break; const double adv = g.dir > 0 ? s.l[j] - g.e : g.e - s.h[j], fav = g.dir > 0 ? s.h[j] - g.e : g.e - s.l[j];
                      if (adv <= -g.R) break; if (g.same && j == g.j0) continue;
                      double tg = x == 0 ? g.R : x == 1 ? 2 * g.R : 0; if (x == 2) { const double vw = VW[j - 1]; if (std::isnan(vw)) continue; tg = g.dir * (vw - g.e); if (tg <= 0) break; }
                      if (fav >= tg) break; } } }
                busy[w][f][x] = exitT; const double net = pts / g.A * Anow - cost; Acc& a = acc[ti][ki][w][T][f][x];
                auto add = [&](int e) { a.s[e] += net; a.n[e]++; (net > 0 ? a.gw[e] : a.gl[e]) += std::fabs(net); };
                add(g.era); if (yr == 2023 || yr == 2024) add(4); } } } } } } }
  const std::string csv = "research/programs/out/wave_entries_" + sym + (shuf ? "_shuf" + std::to_string(shuf) : "") + ".csv"; FILE* o = std::fopen(csv.c_str(), "w");
  fprintf(o, "tf,k,window,trigger,filter,exit,n1,pts1,n2,pts2,is_pf,val_n,val_pts,val_pf,ho_n,ho_pts,ho_pf,of_n,of_pts,of_pf,all_n,all_pts,all_pf,per_day\n");
  auto pf = [](double gw, double gl) { return gl > 0 ? gw / gl : 99.0; };
  const double ndays = good.size();
  printf("Wave entries — %s, %ld signals. Cost %.1f pts, points at today's ATR (%.0f). %.0f sessions.\n", sym.c_str(), nsig, cost, Anow, ndays);
  printf("Selected (price/volume): 2010-20 n>=150 PF>=1.15 both halves>0 → 2021-24 >0 PF>=1.05 → 2025-26 >0 [ES check: join CSVs]\n");
  printf("%-2s %-3s %-7s %-8s %-8s %-4s | IS n   pts   PF  | 21-24 n  pts  PF  | 25-26 n  pts  PF  | /day\n", "tf", "k", "window", "trigger", "filter", "exit");
  int nsel = 0, nval = 0, nho = 0, nof = 0, nofho = 0;
  for (int ti = 0; ti < NTF; ++ti) for (int ki = 0; ki < NK; ++ki) for (int w = 0; w < NW; ++w) for (int T = 0; T < NT; ++T) for (int f = 0; f < NF; ++f) for (int x = 0; x < NX; ++x) {
    const Acc& a = acc[ti][ki][w][T][f][x]; const double isn = a.n[0] + a.n[1], iss = a.s[0] + a.s[1], isgw = a.gw[0] + a.gw[1], isgl = a.gl[0] + a.gl[1];
    const double an = isn + a.n[2] + a.n[3], as = iss + a.s[2] + a.s[3], agw = isgw + a.gw[2] + a.gw[3], agl = isgl + a.gl[2] + a.gl[3];
    if (an == 0) continue;
    fprintf(o, "%d,%.1f,%s,%s,%s,%s,%.0f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.0f,%.3f,%.3f,%.3f\n", TFS[ti], KS[ki], WN[w], TN[T], FN[f], XN[x],
            a.n[0], a.n[0] ? a.s[0] / a.n[0] : 0, a.n[1], a.n[1] ? a.s[1] / a.n[1] : 0, pf(isgw, isgl), a.n[2], a.n[2] ? a.s[2] / a.n[2] : 0, pf(a.gw[2], a.gl[2]),
            a.n[3], a.n[3] ? a.s[3] / a.n[3] : 0, pf(a.gw[3], a.gl[3]), a.n[4], a.n[4] ? a.s[4] / a.n[4] : 0, pf(a.gw[4], a.gl[4]), an, as / an, pf(agw, agl), an / ndays);
    const bool ofc = T == 7 || f == 13 || f == 14;
    if (ofc) { if (a.n[4] >= 60 && pf(a.gw[4], a.gl[4]) >= 1.15 && a.s[4] > 0) { nof++; if (a.s[3] > 0) { nofho++;
          printf("OF %-2d %-3.1f %-7s %-8s %-8s %-4s | 23-24 %4.0f %+6.2f %4.2f | 25-26 %4.0f %+6.2f %4.2f\n", TFS[ti], KS[ki], WN[w], TN[T], FN[f], XN[x],
                 a.n[4], a.s[4] / a.n[4], pf(a.gw[4], a.gl[4]), a.n[3], a.s[3] / a.n[3], pf(a.gw[3], a.gl[3])); } } continue; }
    if (!(isn >= 150 && pf(isgw, isgl) >= 1.15 && a.s[0] > 0 && a.s[1] > 0)) continue; nsel++;
    const bool v = a.s[2] > 0 && pf(a.gw[2], a.gl[2]) >= 1.05, h = a.s[3] > 0; nval += v; nho += v && h;
    if (!(v && h)) continue;
    printf("%-2d %-3.1f %-7s %-8s %-8s %-4s | %5.0f %+6.2f %4.2f | %4.0f %+6.2f %4.2f | %4.0f %+6.2f %4.2f | %.2f\n", TFS[ti], KS[ki], WN[w], TN[T], FN[f], XN[x],
           isn, iss / isn, pf(isgw, isgl), a.n[2], a.s[2] / a.n[2], pf(a.gw[2], a.gl[2]), a.n[3], a.s[3] / a.n[3], pf(a.gw[3], a.gl[3]), an / ndays); }
  std::fclose(o);
  printf("\nprice/volume: %d configs pass 2010-20; %d also 2021-24; %d also 2025-26.  order flow: %d pass 2023-24; %d also 2025-26.\n", nsel, nval, nho, nof, nofho);
  return 0;
}
