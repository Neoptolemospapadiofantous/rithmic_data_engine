// 2026-10-17 direction + permission for the founder's hand trades (research/preregistered/2026-10-17_ema_permission.md, extended
// the same day to the live ORB and mtf_london_v5__sg on the founder's word, conditions listed before the run in that file).
//   perm MTF_CSV
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Acc { double n = 0, s = 0; void add(double x) { n++; s += x; } double m() const { return n ? s / n : NAN; } };
struct Split { Acc a[2][3]; Acc all[2]; void add(bool yes, int era, double x) { a[yes][era].add(x); all[yes].add(x); } };
static void show(const char* nm, const Split& sp, const char* yes, const char* no, const char* e0, const char* e1, const char* e2) {
  printf("    %-34s %-12s %+6.2f (n %5.0f) [%s %+.2f · %s %+.2f · %s %+.2f]   %-12s %+6.2f (n %5.0f) [%+.2f · %+.2f · %+.2f]\n", nm,
         yes, sp.all[1].m(), sp.all[1].n, e0, sp.a[1][0].m(), e1, sp.a[1][1].m(), e2, sp.a[1][2].m(), no, sp.all[0].m(), sp.all[0].n, sp.a[0][0].m(), sp.a[0][1].m(), sp.a[0][2].m()); }
int main(int argc, char** argv) {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  const int NM = 386, LAST = 265; std::mt19937 rng(5);
  std::map<int, int> day_of; for (size_t k = 0; k < s.days.size(); ++k) day_of[s.days[k].date] = k;
  // ---------- per-day RTH minute arrays
  struct DayM { std::array<double, NM> o, h, l, c, vw; double op, atr, gap; bool ok = false; };
  auto fill = [&](const Day& D, DayM& M) {
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) return false;
    double pv = 0, vv = 0, last = s.o[i0]; int j = i0;
    for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
      if (j < D.b1 && tday_min(s.t[j]) == tm) { M.o[m] = s.o[j]; M.h[m] = s.h[j]; M.l[m] = s.l[j]; M.c[m] = s.c[j]; pv += (M.h[m] + M.l[m] + M.c[m]) / 3 * std::max(1.0f, s.v[j]); vv += std::max(1.0f, s.v[j]); }
      else M.o[m] = M.h[m] = M.l[m] = M.c[m] = last;
      last = M.c[m]; M.vw[m] = vv > 0 ? pv / vv : last; }
    M.op = M.o[0]; M.atr = D.atr; M.gap = (M.op - D.pc) / D.atr; M.ok = true; return true; };
  auto era_of = [](int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; };
  // plan direction: first cascade check reached / gap-retest trigger → (minute, dir)
  auto plan_dir = [&](const DayM& M, int& when) { when = 1 << 30; int dir = 0; auto px = [&](int m) { return M.c[m - 1]; };
    const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
    for (int q = 0; q < 4; ++q) { double mv = px(CM[q]) - M.op; if (std::fabs(mv) >= CK[q] * M.atr) { when = CM[q]; dir = mv > 0 ? 1 : -1; break; } }
    const int g = M.gap >= 0.1 ? 1 : M.gap <= -0.1 ? -1 : 0;
    if (g) { double orh = -1e18, orl = 1e18; for (int m = 0; m < 15; ++m) { orh = std::max(orh, M.h[m]); orl = std::min(orl, M.l[m]); }
      for (int k = 3; 5 * k + 5 <= LAST; ++k) { double H = -1e18, L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, M.h[m]); L = std::min(L, M.l[m]); } double C = M.c[5 * k + 4];
        if (g > 0 ? (L <= orh && C > orh) : (H >= orl && C < orl)) { if (5 * k + 5 < when) { when = 5 * k + 5; dir = g; } break; } } }
    return dir; };
  auto run = [&](const DayM& M, int me, int dir, double st, double rr) {   // entry at price c[me-1]... returns pts after 1.0, exit minute
    const double p = M.c[me - 1]; for (int m = me; m < NM - 1; ++m) { if (dir > 0 ? M.l[m] <= p - st : M.h[m] >= p + st) return std::make_pair(-st - 1, m + 1);
      if (dir > 0 ? M.h[m] >= p + rr * st : M.l[m] <= p - rr * st) return std::make_pair(rr * st - 1, m + 1); }
    return std::make_pair(dir * (M.c[NM - 2] - p) - 1, NM - 1); };

  // ================= A. EMA-14 pullbacks on 5-minute bars (2010-07 → 2024-12)
  const char* DN[7] = {"D0 EMA side (no plan)", "D1 plan direction", "D2 side of the 09:30 open", "D3 side of VWAP", "D4 gap side", "D1 twin (random 5-min bar)", "PANEL RULE: D1 + VWAP + open + <=13:55, max 3, stop after a loss"};
  Acc pol[7][3], polall[7]; Acc stopsz[3]; std::map<int, Acc> panel_year; Split p1, p2, p3a, p3b, p4n, p4w;
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || D.date > 20241231 || !(D.atr > 0) || std::isnan(D.pc)) continue; DayM M; if (!fill(D, M)) continue; const int era = era_of(D.date);
    // EMA14 of 5-min closes from 18:00, value at the start of each RTH 5-min bar k (k = 0 → 09:30)
    std::vector<double> ema_at(78, NAN); { double e = NAN; const double a = 2.0 / 15; int64_t bucket = -1; double lastc = NAN; int k = 0;
      for (int i = D.b0; i < D.b1; ++i) { const int tm = tday_min(s.t[i]); const int64_t b = tm / 5;
        if (b != bucket) { if (!std::isnan(lastc)) e = std::isnan(e) ? lastc : e + a * (lastc - e); bucket = b; const int rk = (tm - hm_to_tday(9, 30)); if (rk >= 0 && rk % 5 == 0 && rk / 5 < 78) ema_at[rk / 5] = e; }
        lastc = s.c[i]; if (tm >= hm_to_tday(16, 0)) break; }
      (void)k; }
    int pwhen; const int pd = plan_dir(M, pwhen);
    for (int pol_i = 0; pol_i < 7; ++pol_i) { int busy = 0, ntr = 0; bool lastwin = true;
      for (int k = 3; 5 * k < 330; ++k) {   // bars starting 09:45 .. 14:55
        const double e = ema_at[k]; if (std::isnan(e)) continue; double H = -1e18, L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, M.h[m]); L = std::min(L, M.l[m]); }
        const double C = M.c[5 * k + 4]; const int me = 5 * k + 5; if (me < busy) continue;
        int sig = (L <= e && C > e) ? 1 : (H >= e && C < e) ? -1 : 0; if (pol_i == 5) sig = 0;
        int d = 0;
        if (pol_i == 0) d = sig;
        else if (pol_i == 1) d = (me >= pwhen && sig == pd) ? sig : 0;
        else if (pol_i == 2) d = (sig && sig * (C - M.op) > 0) ? sig : 0;
        else if (pol_i == 3) d = (sig && sig * (C - M.vw[me - 1]) > 0) ? sig : 0;
        else if (pol_i == 4) { const int g = M.gap >= 0.1 ? 1 : M.gap <= -0.1 ? -1 : 0; d = (sig && sig == g) ? sig : 0; }
        else if (pol_i == 6) d = (me >= pwhen && sig == pd && me <= LAST && ntr < 3 && lastwin && sig * (C - M.vw[me - 1]) > 0 && sig * (C - M.op) > 0) ? sig : 0;
        else if (pol_i == 5 && pd && me >= pwhen && std::uniform_real_distribution<>(0, 1)(rng) < 0.12) d = pd;   // ~ the D1 trade rate
        if (!d) continue;
        auto [pts, xm] = run(M, me, d, 0.25 * M.atr, 2.0); pol[pol_i][era].add(pts); if (pol_i == 6) { stopsz[era].add(0.25 * M.atr); panel_year[D.date / 10000].add(pts); } polall[pol_i].add(pts); busy = xm; ++ntr;
        if (pol_i == 1) { p1.add(d * (C - M.op) > 0, era, pts); p2.add(d * (C - M.vw[me - 1]) > 0, era, pts); p3a.add(me <= 120, era, pts); p3b.add(me <= LAST, era, pts);
          p4n.add(ntr == 1, era, pts); if (ntr > 1) p4w.add(lastwin, era, pts); }
        lastwin = pts > 0; } } }
  printf("A. EMA-14 pullback (5-min touch + close back), stop 0.25 ATR, 2R, one at a time, entries 09:50–15:00, NQ pts/trade after 1.0 cost\n");
  for (int i = 0; i < 7; ++i) printf("    %-34s %+6.2f (n %5.0f)  eras 2010-18 %+.2f · 2019-22 %+.2f · 2023-24 %+.2f\n", DN[i], polall[i].m(), polall[i].n, pol[i][0].m(), pol[i][1].m(), pol[i][2].m());
  printf("  panel rule stop size (0.25 ATR): %.1f / %.1f / %.1f pts by era; by year:", stopsz[0].m(), stopsz[1].m(), stopsz[2].m());
  for (auto& [y, a] : panel_year) printf(" %d %+.1f(%.0f)", y, a.m(), a.n); printf("\n");
  printf("  inside D1 (plan direction):\n");
  show("P1 price on D's side of the open", p1, "yes", "no", "10-18", "19-22", "23-24"); show("P2 price on D's side of VWAP", p2, "yes", "no", "10-18", "19-22", "23-24");
  show("P3 entry by 11:30", p3a, "by 11:30", "after", "10-18", "19-22", "23-24"); show("P3 entry by 13:55", p3b, "by 13:55", "13:55-15:00", "10-18", "19-22", "23-24");
  show("P4 first EMA trade of the day", p4n, "1st", "2nd+", "10-18", "19-22", "23-24"); show("P4 2nd+ trade, previous won", p4w, "after win", "after loss", "10-18", "19-22", "23-24");

  // ================= B. the live ORB (5-min range, stop orders 2 pts beyond, 15-pt stop, BE +1 after 5 min once +3, trail 10, ≤3, to 10:30)
  Split bg, bon, bpd, bsma, bw, bvix, bn, bpw; Acc ball, bera[3];
  for (size_t di = 1; di < s.days.size(); ++di) { const Day& D = s.days[di]; const Day& P = s.days[di - 1];
    if (!D.ok || D.rollwin || D.date > 20241231 || !(D.atr > 0) || std::isnan(D.pc)) continue; DayM M; if (!fill(D, M)) continue; const int era = era_of(D.date);
    double orh = -1e18, orl = 1e18; for (int m = 0; m < 5; ++m) { orh = std::max(orh, M.h[m]); orl = std::min(orl, M.l[m]); }
    const double up = orh + 2, dn = orl - 2, wid = (orh - orl) / M.atr; const double gl = s.o[D.b0];   // 18:00 Globex open
    int m = 5, ntr = 0; bool lastwin = true;
    while (ntr < 3 && m <= 60) {
      int d = 0; for (; m <= 60; ++m) { const bool hu = M.h[m] >= up, hd = M.l[m] <= dn; if (hu && hd) { m++; continue; } if (hu) { d = 1; break; } if (hd) { d = -1; break; } }
      if (!d) break; const double p = d > 0 ? std::max(up, M.o[m]) : std::min(dn, M.o[m]);   /* a stop order fills at the market when price is already beyond it */ double stop = p - d * 15, best = p; int x = NM - 1; double pts = NAN;
      for (int k = m; k < NM - 1; ++k) { if (d > 0 ? M.l[k] <= stop : M.h[k] >= stop) { pts = d * (stop - p); x = k + 1; break; }
        best = d > 0 ? std::max(best, M.h[k]) : std::min(best, M.l[k]);
        if (k - m >= 5) { if (d * (best - p) >= 3) stop = d > 0 ? std::max(stop, p + 1) : std::min(stop, p - 1); if (d * (best - p) >= 3) stop = d > 0 ? std::max(stop, best - 10) : std::min(stop, best + 10); } }
      if (std::isnan(pts)) pts = d * (M.c[NM - 2] - p); pts -= 1.0; ++ntr;
      const double pre = M.op; ball.add(pts); bera[era].add(pts);
      bg.add(d * M.gap >= 0.1, era, pts); bon.add(d * (pre - gl) > 0, era, pts); bpd.add(!std::isnan(P.ro) && d * (P.rc - P.ro) > 0, era, pts);
      bsma.add(!std::isnan(D.sma50) && d * (D.pc - D.sma50) > 0, era, pts); bw.add(wid <= 0.12, era, pts); bvix.add(D.vix >= 20, era, pts);
      bn.add(ntr == 1, era, pts); if (ntr > 1) bpw.add(lastwin, era, pts); lastwin = pts > 0; m = x; }
  }
  printf("\nB. live ORB rules (NQ pts/trade after 1.0 cost): %+.2f (n %.0f)  eras %+.2f · %+.2f · %+.2f\n", ball.m(), ball.n, bera[0].m(), bera[1].m(), bera[2].m());
  show("gap agrees (≥0.1 ATR)", bg, "agrees", "not", "10-18", "19-22", "23-24"); show("overnight move agrees (18:00→09:30)", bon, "agrees", "against", "10-18", "19-22", "23-24");
  show("yesterday's RTH direction agrees", bpd, "agrees", "against", "10-18", "19-22", "23-24"); show("prior close vs 50-day avg agrees", bsma, "agrees", "against", "10-18", "19-22", "23-24");
  show("5-min range ≤ 0.12 ATR (narrow)", bw, "narrow", "wide", "10-18", "19-22", "23-24"); show("VIX ≥ 20", bvix, "VIX≥20", "VIX<20", "10-18", "19-22", "23-24");
  show("1st trade of the day", bn, "1st", "2nd/3rd", "10-18", "19-22", "23-24"); show("2nd/3rd: previous won", bpw, "after win", "after loss", "10-18", "19-22", "23-24");

  // ================= C. mtf_london_v5(__sg) trades: 2022-01→2023-02 bar replay (nq_bars) and 2025-01→2026-10 (nq_hist + forward)
  Split cpc, cgl, cvw, cpd, csma, ct, cn, cpw; Acc call[3];
  { std::ifstream in(argv[1]); std::string l; std::map<int, std::pair<int, bool>> perday;
    while (std::getline(in, l)) { std::stringstream q(l); std::string lab, a, d, p; std::getline(q, lab, ','); std::getline(q, a, ','); std::getline(q, d, ','); std::getline(q, p, ',');
      const int64_t te = std::stoll(a); const int dir = std::stoi(d); const double pts = std::stod(p); const int date = ymd_of(te);
      auto it = day_of.find(date); if (it == day_of.end()) continue; const Day& D = s.days[it->second]; if (std::isnan(D.pc) || it->second == 0) continue; const Day& P = s.days[it->second - 1];
      auto ie = std::lower_bound(s.t.begin(), s.t.end(), te) - s.t.begin(); if (ie <= D.b0 || ie >= D.b1) continue; const double px = s.c[ie - 1];
      double pv = 0, vv = 0; for (long i = D.b0; i < ie; ++i) { pv += (s.h[i] + s.l[i] + s.c[i]) / 3 * std::max(1.0f, s.v[i]); vv += std::max(1.0f, s.v[i]); }
      const int per = lab == "nq_bars" ? 0 : lab == "nq_hist" ? 1 : 2; call[per].add(pts);
      auto& pd = perday[date]; ++pd.first; const int ntr = pd.first;
      cpc.add(dir * (px - D.pc) > 0, per, pts); cgl.add(dir * (px - s.o[D.b0]) > 0, per, pts); cvw.add(dir * (px - pv / vv) > 0, per, pts);
      cpd.add(!std::isnan(P.ro) && dir * (P.rc - P.ro) > 0, per, pts); csma.add(!std::isnan(D.sma50) && dir * (D.pc - D.sma50) > 0, per, pts);
      ct.add(tday_min(te) >= hm_to_tday(3, 0), per, pts); cn.add(ntr == 1, per, pts); if (ntr > 1) cpw.add(pd.second, per, pts); pd.second = pts > 0; } }
  printf("\nC. mtf_london_v5 trades, pts/trade after commission: 2022-23 replay %+.2f (n %.0f) · 2025-26 history %+.2f (n %.0f) · forward 09-24→10-16 %+.2f (n %.0f)\n",
         call[0].m(), call[0].n, call[1].m(), call[1].n, call[2].m(), call[2].n);
  show("overnight move agrees (vs prior close)", cpc, "agrees", "against", "22-23", "25-26", "fwd"); show("move since 18:00 agrees", cgl, "agrees", "against", "22-23", "25-26", "fwd");
  show("on its side of VWAP (from 18:00)", cvw, "yes", "no", "22-23", "25-26", "fwd"); show("yesterday's RTH direction agrees", cpd, "agrees", "against", "22-23", "25-26", "fwd");
  show("prior close vs 50-day avg agrees", csma, "agrees", "against", "22-23", "25-26", "fwd"); show("entry at/after 03:00", ct, "≥03:00", "02:00-03:00", "22-23", "25-26", "fwd");
  show("1st trade of the night", cn, "1st", "2nd+", "22-23", "25-26", "fwd"); show("2nd+: previous won", cpw, "after win", "after loss", "22-23", "25-26", "fwd");
}
