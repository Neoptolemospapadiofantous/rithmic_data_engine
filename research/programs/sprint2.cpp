// 2026-10-17 one-week payout sprint search (founder: "we want to make it happen", "test all", "find a way").
// Trades: the plan's own setups (cascade 09:50 / 10:30 / 11:00 / 11:30, gap retest), one position at a time, flat 15:55.
// Varied: target (0.5 / 1 / 1.5 / 2 / 3 R), mode (1 trade a day | again only after a win, ≤ 3), $ risk per trade in the evaluation
// and in the funded phase (micros = floor(risk / (stop pts × $2)), ≥ 1, ≤ the plan's cap), account plan, and k accounts started on k
// consecutive days. Payout rules = src/paper/payout_sim.hpp. Days merged pessimistic. Cost 1.5 pts/trade. NQ 2019-01 → 2024-12.
#include "research/meta.hpp"
#include <array>
#include <cstdio>
using namespace lab;
struct T { double pts, st, mae; };   // pts after cost, stop distance, worst adverse (≤ 0, before cost)
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  const int NM = 386, LAST = 265; const double TGS[5] = {0.5, 1, 1.5, 2, 3};
  std::vector<int> dates; std::vector<std::array<std::array<std::vector<T>, 5>, 2>> cal;   // [day][mode][target]
  for (auto& D : s.days) {
    if (!D.ok || D.date < 20190101 || D.date > 20241231) continue; dates.push_back(D.date); cal.emplace_back();
    if (D.rollwin || !(D.atr > 0) || std::isnan(D.pc)) continue;
    const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
    std::array<double, NM> o, h, l, c; { double last = s.o[i0]; int j = i0; for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
      if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; } else o[m] = h[m] = l[m] = c[m] = last; last = c[m]; } }
    const double op = o[0], atr = D.atr, gap = (op - D.pc) / atr; const int gdir = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
    double orh = -1e18, orl = 1e18; for (int m = 0; m < 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
    auto px = [&](int m) { return c[m - 1]; };
    for (int md = 0; md < 2; ++md) for (int ti = 0; ti < 5; ++ti) {
      const double R = TGS[ti]; bool used[4] = {false, false, false, false}, gused = false; int Dd = 0, X = 0, ntr = 0; bool lastwin = true;
      const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      while (ntr < (md == 0 ? 1 : 3)) { if (ntr > 0 && !lastwin) break;
        int bm = 1 << 30, bd = 0, src = -1, bi = -1; double bst = 0.25;
        auto cand = [&](int m, int d, double sk, int sc, int idx) { if (m < X || m > LAST || (Dd && d != Dd) || !d) return; if (m < bm) { bm = m; bd = d; bst = sk; src = sc; bi = idx; } };
        for (int q = 0; q < 4; ++q) if (!used[q] && CM[q] >= X) { const double mv = px(CM[q]) - op; if (std::fabs(mv) >= CK[q] * atr) cand(CM[q], mv > 0 ? 1 : -1, 0.25, 0, q); }
        if (!gused && gdir) for (int k = 3; 5 * k <= LAST; ++k) { if (5 * k < X) continue; double H = -1e18, L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); }
          if (gdir > 0 ? (L <= orh && c[5 * k + 4] > orh) : (H >= orl && c[5 * k + 4] < orl)) { cand(5 * k + 5, gdir, 0.35, 1, 0); break; } }
        if (src < 0) break; if (src == 0) for (int q = 0; q <= bi; ++q) used[q] = true; if (src == 1) gused = true; if (!Dd) Dd = bd;
        const double p = px(bm), st = bst * atr; double ma = 0, pts = NAN; int xm = NM - 1;
        for (int m = bm; m < NM - 1; ++m) { ma = std::min(ma, bd > 0 ? l[m] - p : p - h[m]);
          if (bd > 0 ? l[m] <= p - st : h[m] >= p + st) { pts = -st; ma = std::max(ma, -st); xm = m + 1; break; }
          if (bd > 0 ? h[m] >= p + R * st : l[m] <= p - R * st) { pts = R * st; xm = m + 1; break; } }
        if (std::isnan(pts)) pts = bd * (c[NM - 2] - p);
        cal.back()[md][ti].push_back({pts - 1.5, st, ma - 1.5}); ++ntr; lastwin = pts > 0; X = xm; if (X >= NM - 1) break; } } }
  const int Dn = dates.size();
  auto plan_of = [&](const char* nm) { for (auto& p : payout::plans()) if (p.name == std::string(nm)) return &p; return (const payout::Plan*)nullptr; };
  auto md_name = [](int m) { return m == 0 ? "1 trade/day" : "again after a win"; };
  // Lightning Funded (no evaluation; Tradeify help centre read 2026-10-17 + imantrading table): EOD trailing $1,000 (25K) / $2,000 (50K),
  // payout 1 needs +$1,500 / +$3,000 with best day ≤ 20 % of the profit, ≤ $1,000 / $1,500 withdrawable (50K cap assumed), floor lock as Select.
  { payout::Plan L25 = *plan_of("select_daily_25k"); L25.name = "lightning_25k"; L25.daily_loss = 0; L25.buffer = 0; L25.payout_mult = 0; L25.min_payout = 0;
    L25.min_profit = 1500; L25.consistency = 0.20; L25.profit_share = 1.0; L25.caps = {1000}; L25.win_days_needed = 0; L25.win_day_min = 0;
    payout::Plan L50 = L25; L50.name = "lightning_50k"; L50.start = 50000; L50.drawdown = 2000; L50.daily_loss = 1250; L50.min_profit = 3000; L50.caps = {1500}; L50.max_minis = 4;
    for (auto* pl : {&L25, &L50}) { const int cap = pl->max_minis * 10; const double k2 = pl->drawdown / 1000.0;
      printf("\n== %s (no evaluation) — top 8 by P(first payout ≤ 7 trading days)\n", pl->name.c_str());
      struct Row { double p5, p7, p10, p20, bl7; double r; int md, ti; double amt; }; std::vector<Row> rows;
      for (int md = 0; md < 2; ++md) for (int ti = 0; ti < 5; ++ti) for (double rk : {100., 150., 200., 250., 300., 400., 500., 600.}) {
        std::vector<std::vector<payout::Trade>> sc(Dn); std::vector<const std::vector<payout::Trade>*> cv(Dn, nullptr);
        for (int i = 0; i < Dn; ++i) { auto& v = cal[i][md][ti]; if (v.empty()) continue; double p = 0, ma = 0, mf = 0;
          for (auto& tr : v) { const int n = std::clamp((int)std::floor(rk * k2 / (tr.st * 2.0)), 1, cap); p += n * tr.pts * 2; ma += n * tr.mae * 2; mf += n * std::max(0.0, tr.pts) * 2; }
          sc[i] = {{p, std::min(ma, p), mf}}; cv[i] = &sc[i]; }
        int st = 0, p5 = 0, p7 = 0, p10 = 0, p20 = 0, bl = 0; double amt = 0; int na = 0;
        for (int s0 = 0; s0 + 22 < Dn; ++s0) { ++st; auto r = payout::run_account(*pl, cv, s0, 20); if (r.breach && (r.pay_days.empty() || r.pay_days[0] > 7) && r.days <= 7) ++bl;
          if (!r.pay_days.empty()) { const int d = r.pay_days[0]; p5 += d <= 5; p7 += d <= 7; p10 += d <= 10; p20 += d <= 20; if (d <= 10) { amt += r.paid_sum; ++na; } } }
        rows.push_back({100.0 * p5 / st, 100.0 * p7 / st, 100.0 * p10 / st, 100.0 * p20 / st, 100.0 * bl / st, rk * k2, md, ti, na ? amt / na : 0}); }
      std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.p7 + 0.5 * a.p10 > b.p7 + 0.5 * b.p10; });
      for (int q = 0; q < 8; ++q) { auto& r = rows[q]; printf("  %-18s target %.1fR  risk $%4.0f | ≤5d %5.2f%%  ≤7d %5.2f%%  ≤10d %5.2f%%  ≤20d %5.2f%%  blown≤7d %5.1f%%  payout ~$%.0f\n",
          md_name(r.md), TGS[r.ti], r.r, r.p5, r.p7, r.p10, r.p20, r.bl7, r.amt); } } }
  const char* MN[2] = {"1 trade/day", "again after a win"};
  printf("SPRINT search: plan setups, NQ 2019-2024 (%d days), micros sized by $ risk, cost 1.5 pts\n", Dn);
  for (const char* pname : {"select_daily_25k", "select_daily_50k", "growth_25k", "select_flex_25k"}) { const payout::Plan* pl = plan_of(pname); const int cap = pl->max_minis ? pl->max_minis * 10 : 40; const double k2 = pl->drawdown / 1000.0;
    struct Row { double p5, p7, p10, bl7; double re, rf; int md, ti; double amt; };
    std::vector<Row> rows;
    auto build = [&](int md, int ti, double risk, std::vector<std::vector<payout::Trade>>& sc, std::vector<const std::vector<payout::Trade>*>& cv) { sc.assign(Dn, {}); cv.assign(Dn, nullptr);
      for (int i = 0; i < Dn; ++i) { auto& v = cal[i][md][ti]; if (v.empty()) continue; double p = 0, ma = 0, mf = 0;
        for (auto& t : v) { const int n = std::clamp((int)std::floor(risk / (t.st * 2.0)), 1, cap); p += n * t.pts * 2; ma += n * t.mae * 2; mf += n * std::max(0.0, t.pts) * 2; }
        sc[i] = {{p, std::min(ma, p), mf}}; cv[i] = &sc[i]; } };
    for (int md = 0; md < 2; ++md) for (int ti = 0; ti < 5; ++ti) for (double re : {150., 250., 350., 450., 600., 800.}) for (double rf : {300., 450., 600., 750., 900., 1200.}) {
      std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf; build(md, ti, re * k2, se, ce); build(md, ti, rf * k2, sf, cf);
      int st = 0, p5 = 0, p7 = 0, p10 = 0, bl = 0; double amt = 0; int na = 0;
      for (int s0 = 0; s0 + 12 < Dn; ++s0) { ++st; auto e = payout::run_eval(*pl, ce, s0, 10); if (!e.passed) { bl += e.failed && e.days <= 7; continue; }
        auto r = payout::run_account(*pl, cf, s0 + e.days, 10 - e.days); if (r.breach && r.pay_days.empty() && e.days + r.days <= 7) ++bl;
        if (!r.pay_days.empty()) { const int d = e.days + r.pay_days[0]; p5 += d <= 5; p7 += d <= 7; p10 += d <= 10; if (d <= 10) { amt += r.paid_sum; ++na; } } }
      rows.push_back({100.0 * p5 / st, 100.0 * p7 / st, 100.0 * p10 / st, 100.0 * bl / st, re * k2, rf * k2, md, ti, na ? amt / na : 0}); }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.p7 + 0.5 * a.p10 > b.p7 + 0.5 * b.p10; });
    printf("\n== %s — top 8 by P(payout ≤ 7 days) (+ half ≤ 10 days)\n", pname);
    for (int q = 0; q < 8; ++q) { auto& r = rows[q]; printf("  %-18s target %.1fR  eval risk $%4.0f  funded risk $%4.0f | ≤5d %5.2f%%  ≤7d %5.2f%%  ≤10d %5.2f%%  blown≤7d %5.1f%%  payout ~$%.0f\n",
                                                     MN[r.md], TGS[r.ti], r.re, r.rf, r.p5, r.p7, r.p10, r.bl7, r.amt); }
    // k accounts started on k consecutive days with the best setting: P(at least one payout within 7 / 10 trading days of the first start)
    { auto& r = rows[0]; std::vector<std::vector<payout::Trade>> se, sf; std::vector<const std::vector<payout::Trade>*> ce, cf; build(r.md, r.ti, r.re, se, ce); build(r.md, r.ti, r.rf, sf, cf);
      std::vector<int> pd(Dn, 1 << 30);   // trading day (from its own start) of the first payout for an account started at s0
      for (int s0 = 0; s0 + 12 < Dn; ++s0) { auto e = payout::run_eval(*pl, ce, s0, 12); if (!e.passed) continue; auto a = payout::run_account(*pl, cf, s0 + e.days, 12 - e.days); if (!a.pay_days.empty()) pd[s0] = e.days + a.pay_days[0]; }
      printf("  several accounts, best setting, started on consecutive days:");
      for (int k : {1, 3, 5, 10}) { int st = 0, h7 = 0, h10 = 0; for (int s0 = 0; s0 + k + 12 < Dn; ++s0) { ++st; bool a7 = false, a10 = false; for (int j = 0; j < k; ++j) { const int t = j + pd[s0 + j]; a7 |= t <= 7; a10 |= t <= 10; } h7 += a7; h10 += a10; }
        printf("  k=%d: ≤7d %.1f%% ≤10d %.1f%%", k, 100.0 * h7 / st, 100.0 * h10 / st); }
      printf("\n"); } }
}
