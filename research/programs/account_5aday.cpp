// 2026-10-19 (founder: "more tests to achieve 4-5 trades a day. if I make a lot on those trades I stop trading the account"): does a
// 4-5-a-day stream pass a Vest $5k account (+$500 goal, -$300 static drawdown, -$150 daily loss, $40 fee, 80 % of the $500) when the day
// stops after a profit target and the account stops at the goal? Streams (NY, one position at a time, ≤5 a day, stop 0.10 ATR, 1.5R,
// 60-min time exit, cost 2 NQ pts at today's cost-to-range, net R):
//   P55 = with-the-day pullbacks (1m EMA20 / VWAP / 5m EMA9 / dip-then-new-extreme) on straightness ≥ 0.55   (repeat_entries rule ALL)
//   P40 = same on straightness ≥ 0.40
//   T15 = with the day every 15 min, straightness ≥ 0.70, before 11:30  (the only positive one, ~1.3 a day)
//   COIN = P55's entry times with a coin-flip direction (pure chance at the same frequency)
// Account: risk $X a trade, cut to the room left so no single loss can break the daily or total limit (no trade under $20), day stops at
// +$P (profit stop), account stops at +$500 (pass), -$300 (fail) or 60 trading days (timeout). One account started on every NY day
// 2010-07 → 2026-08. EV per attempt = pass x $400 - $40. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
#include <algorithm>
using namespace lab;

struct Tr { double r; };
enum { P55, P40, T15, COIN, FL5, FL5L, FLMIX, FLCOIN, NST };
static const char* STN[NST] = {"P55 pullbacks, straight >= 0.55", "P40 pullbacks, straight >= 0.40", "T15 with-the-day every 15 min, st>=0.70, <11:30", "COIN same times, coin-flip dir", "FL5 best 5m flag with the day (flag5_trend)", "FL5L same, looser pole (>= 0.10 ATR)", "FLMIX any 1m/3m/5m flag with the day, st>=0.55", "FLCOIN FLMIX times, coin-flip dir"};
struct FB { double o, h, l, c; };
// flag on bars b closing now: pole of N bars with the day (>= m ATR), F flag bars within tight x pole, close beyond the flag. Returns the stop or NAN.
static double flagStop(const std::vector<FB>& b, int N, int F, double m, double tight, int D, double A) {
  int n = b.size(); if (n < N + F + 1 || !D) return NAN; int p0 = n - 1 - F - N, p1 = n - 2 - F;
  double pole = D * (b[p1].c - b[p0].o); if (pole < m * A) return NAN; double fh = -1e18, fl = 1e18;
  for (int k = n - 1 - F; k < n - 1; ++k) { fh = std::max(fh, b[k].h); fl = std::min(fl, b[k].l); }
  if (fh - fl > tight * pole) return NAN; double c = b[n - 1].c; if (D > 0 ? !(c > fh) : !(c < fl)) return NAN; return D > 0 ? fl - 0.25 : fh + 0.25; }
static const double COST = 2.0 / 320.0, STOP = 0.10, RR = 1.5;

int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(9);
  std::vector<int> dates; std::vector<std::vector<double>> tr[NST]; double ptsSum[NST] = {0};
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; dates.push_back(d.date);
    for (int k = 0; k < NST; ++k) tr[k].emplace_back();
    struct Pos { int dir = 0; double e = 0, st = 0, tg = 0, R = 0; int64_t tEnd = 0; } pos[NST]; std::vector<FB> b1, b3, b5;
    double so = s.o[d.rth0], hi = s.h[d.rth0], lo = s.l[d.rth0], pv = 0, vv = 0, e20 = s.c[d.rth0], e9 = NAN, h5 = -1e18, l5 = 1e18, ext = so; int lastDir = 0;
    for (int i = d.rth0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= hm_to_tday(16, 0)) break;
      for (int k = 0; k < NST; ++k) { Pos& p = pos[k]; if (!p.dir) continue; double out = NAN;
        bool hs = p.dir > 0 ? s.l[i] <= p.st : s.h[i] >= p.st, ht = p.dir > 0 ? s.h[i] >= p.tg : s.l[i] <= p.tg;
        if (hs) out = -1; else if (ht) out = RR; else if (s.t[i] >= p.tEnd || tm >= hm_to_tday(15, 55)) out = p.dir * (s.c[i] - p.e) / p.R;
        if (!std::isnan(out)) { double net = out - COST * A / p.R; tr[k].back().push_back(net); ptsSum[k] += net * p.R * 320 / A; p.dir = 0; } }
      auto upd = [&](std::vector<FB>& b, int tf) { if ((tm - hm_to_tday(9, 30)) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
        else { FB& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } };
      upd(b1, 1); upd(b3, 3); upd(b5, 5); const bool c3 = (tm + 1 - hm_to_tday(9, 30)) % 3 == 0, c5f = (tm + 1 - hm_to_tday(9, 30)) % 5 == 0;
      hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
      double vw = vv > 0 ? pv / vv : s.c[i], pe20 = e20; e20 += 2.0 / 21 * (s.c[i] - e20);
      bool c5 = (tm + 1) % 5 == 0; h5 = std::max(h5, (double)s.h[i]); l5 = std::min(l5, (double)s.l[i]); double pe9 = e9;
      if (c5) e9 = std::isnan(e9) ? s.c[i] : e9 + 0.2 * (s.c[i] - e9);
      const double c = s.c[i]; const int D = c > so ? 1 : c < so ? -1 : 0;
      if (D != lastDir) { ext = c; lastDir = D; } double prevExt = ext; if (D > 0) ext = std::max(ext, (double)s.h[i]); else if (D < 0) ext = std::min(ext, (double)s.l[i]);
      double st = hi > lo ? std::fabs(c - so) / (hi - lo) : 0;
      bool base = tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0) && D && (c - vw) * D > 0;
      bool any = false;
      if (base && st >= 0.40) {
        bool a = D > 0 ? (s.l[i] <= pe20 && c > e20) : (s.h[i] >= pe20 && c < e20);
        bool b = D > 0 ? (s.l[i] <= vw + 0.02 * A && c > vw) : (s.h[i] >= vw - 0.02 * A && c < vw);
        bool cc = c5 && !std::isnan(pe9) && (D > 0 ? (l5 <= pe9 && c > e9) : (h5 >= pe9 && c < e9));
        bool dd = D * (c - prevExt) > 0 && i > d.rth0 + 10 && [&] { double m = D > 0 ? 1e18 : -1e18; for (int k = i - 10; k < i; ++k) m = D > 0 ? std::min(m, (double)s.l[k]) : std::max(m, (double)s.h[k]); return D * (prevExt - m) >= 0.05 * A; }();
        any = a || b || cc || dd; }
      if (c5) { h5 = -1e18; l5 = 1e18; }
      double fs[NST]; for (double& x : fs) x = NAN;
      if (base && st >= 0.55) { double a5 = c5f ? flagStop(b5, 6, 5, 0.20, 0.6, D, A) : NAN, l5 = c5f ? flagStop(b5, 6, 5, 0.10, 0.6, D, A) : NAN;
        double m1 = flagStop(b1, 10, 5, 0.10, 0.6, D, A), m3 = c3 ? flagStop(b3, 3, 2, 0.10, 0.8, D, A) : NAN, m5 = c5f ? flagStop(b5, 4, 3, 0.10, 0.6, D, A) : NAN;
        fs[FL5] = a5; fs[FL5L] = l5; for (double x : {a5, m5, m3, m1}) if (std::isnan(fs[FLMIX]) && !std::isnan(x)) fs[FLMIX] = x; fs[FLCOIN] = fs[FLMIX]; }
      bool sig[NST] = {any && st >= 0.55, any, base && st >= 0.70 && tm < hm_to_tday(11, 30) && (tm + 1) % 15 == 0, any && st >= 0.55, !std::isnan(fs[FL5]), !std::isnan(fs[FL5L]), !std::isnan(fs[FLMIX]), !std::isnan(fs[FLCOIN])};
      for (int k = 0; k < NST; ++k) { if (!sig[k] || pos[k].dir || (int)tr[k].back().size() >= 5) continue; int dir = (k == COIN || k == FLCOIN) ? ((rng() & 1) ? 1 : -1) : D;
        double R = k >= FL5 ? D * (c - fs[k]) : STOP * A; if (!(R > 0.25) || R > 0.6 * A) continue;
        Pos& p = pos[k]; p.dir = dir; p.e = c; p.R = R; p.st = c - dir * R; p.tg = c + dir * RR * R; p.tEnd = s.t[i] + 3600; }
    }
  }
  const int ND = dates.size();
  printf("Vest $5k account with 4-5 trades a day — NQ NY %d days. Per-trade net R and trades/day first:\n", ND);
  for (int k = 0; k < NST; ++k) { double n = 0, sm = 0; for (auto& v : tr[k]) for (double x : v) { n++; sm += x; } printf("  %-50s %.2f a day, net %+.3f R (%+.2f pts a trade at today's range)\n", STN[k], n / ND, sm / n, ptsSum[k] / n); }
  const double RISK[5] = {40, 60, 80, 100, 150}, PSTOP[5] = {1e9, 75, 100, 150, 250};
  for (int k = 0; k < NST; ++k) { if (k >= FL5) continue; printf("\n== %s\n  risk  profit-stop |  pass%%  fail%%  t/o%%  days-to-pass | EV per $40 attempt | pass%% 2010-18 / 2019-22 / 2023-26\n", STN[k]);
    for (double rk : RISK) for (double ps : PSTOP) {
      long pass = 0, fail = 0, to = 0, tot = 0; double dsum = 0; long pe[3] = {0}, ne[3] = {0};
      for (int st = 0; st + 60 < ND; ++st) { double bal = 0; int res = 0, day = 0;
        for (day = 0; day < 60 && !res; ++day) { double dp = 0; for (double r : tr[k][st + day]) { if (dp >= ps) break;
            double room = std::min(150 + dp, 300 + bal); double risk = std::min(rk, room); if (risk < 20) break;
            double pl = r * risk; dp += pl; bal += pl; if (bal >= 500) { res = 1; break; } if (bal <= -300 + 1e-9 || dp <= -150 + 1e-9) { if (bal <= -300 + 1e-9) res = -1; break; } } }
        int e = dates[st] < 20190101 ? 0 : dates[st] < 20230101 ? 1 : 2; ne[e]++; tot++;
        if (res == 1) { pass++; dsum += day; pe[e]++; } else if (res == -1) fail++; else to++; }
      printf("  $%3.0f  %11s | %5.1f %5.1f %5.1f %8.1f      | %+7.0f            | %5.1f %5.1f %5.1f\n", rk, ps > 1e8 ? "none" : (std::string("$") + std::to_string((int)ps)).c_str(),
             100.0 * pass / tot, 100.0 * fail / tot, 100.0 * to / tot, pass ? dsum / pass : 0, 400.0 * pass / tot - 40, 100.0 * pe[0] / ne[0], 100.0 * pe[1] / ne[1], 100.0 * pe[2] / ne[2]); } }
  // ---- operation: 5 accounts, eval (+$500 → funded) then funded (pay 80 % of the balance above $250 whenever it reaches +$500), blown
  // accounts replaced the same day ($40 each), every account takes the same trades (same direction — allowed). 12-month windows started
  // each month. Funded rules assumed = eval rules (unconfirmed, as in port5).
  printf("\n\n######## OPERATION: 5 accounts, blown ones replaced, eval then funded with payouts — 12-month windows started each month\n");
  printf("  stream                                           eval$ fund$ pstop | net $/yr mean  median  losing%% | fees/yr payouts/yr | mean net 2010-18 / 2019-22 / 2023-26\n");
  const double ER[3] = {60, 100, 150}, FR[3] = {40, 60, 80}, PS2[2] = {1e9, 150};
  for (int k = 0; k < NST; ++k) for (double er : ER) for (double fr : FR) for (double ps : PS2) {
    std::vector<double> nets; double fsum = 0, psum = 0, es[3] = {0}; long en[3] = {0};
    for (int st = 0; st < ND; ++st) { if (st > 0 && dates[st] / 100 == dates[st - 1] / 100) continue; int end = st; while (end < ND && dates[end] < dates[st] + 10000) ++end; if (end >= ND || dates[st] > 20251016) break;
      struct Ac { double bal; bool fund; }; std::vector<Ac> ac; double fees = 0, paid = 0;
      for (int day = st; day < end; ++day) {
        while ((int)ac.size() < 5) { ac.push_back({0, false}); fees += 40; }
        for (auto& a : ac) { double dp = 0, rk = a.fund ? fr : er;
          for (double r : tr[k][day]) { if (dp >= ps) break; double room = std::min(150 + dp, 300 + a.bal); double risk = std::min(rk, room); if (risk < 20) break;
            double pl = r * risk; dp += pl; a.bal += pl; if (!a.fund && a.bal >= 500) break; if (a.bal <= -300 + 1e-9 || dp <= -150 + 1e-9) break; }
          if (!a.fund && a.bal >= 500) { a.fund = true; a.bal = 0; } else if (a.fund && a.bal >= 500) { paid += 0.8 * (a.bal - 250); a.bal = 250; } }
        ac.erase(std::remove_if(ac.begin(), ac.end(), [](const Ac& a) { return a.bal <= -300 + 1e-9; }), ac.end()); }
      double net = paid - fees; nets.push_back(net); fsum += fees; psum += paid; int e = dates[st] < 20190101 ? 0 : dates[st] < 20230101 ? 1 : 2; es[e] += net; en[e]++; }
    std::vector<double> so = nets; std::sort(so.begin(), so.end()); long lose = 0; double sm = 0; for (double x : nets) { sm += x; if (x < 0) lose++; }
    printf("  %-48s %4.0f %4.0f %5s | %+8.0f %+8.0f %6.1f   | %6.0f %8.0f      | %+7.0f %+7.0f %+7.0f\n", STN[k], er, fr, ps > 1e8 ? "none" : "$150", sm / nets.size(), so[so.size() / 2], 100.0 * lose / nets.size(),
           fsum / nets.size(), psum / nets.size(), es[0] / en[0], es[1] / en[1], es[2] / en[2]); }
  return 0;
}
