// 2026-10-17 manual-execution tests on the sprint (research/preregistered/2026-10-17_manual_execution.md).
#include "research/meta.hpp"
#include <array>
#include <fstream>
#include <cstdio>
#include <random>
using namespace lab;
struct T { double pts, st, mae; };
struct V { int delay = 0; double slip = 0, be = 0; bool second = false, skipnews = false; };
int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix); load_events("data/edge_lab/cache", s);
  const int NM = 386, LAST = 265;
  auto gen = [&](const V& v) { std::vector<std::vector<T>> cal;
    for (size_t di = 1; di < s.days.size(); ++di) { const Day& D = s.days[di]; const Day& P = s.days[di - 1];
      if (!D.ok || D.date < 20190101 || D.date > 20241231) continue; cal.emplace_back();
      if (D.rollwin || !(D.atr > 0) || std::isnan(D.pc)) continue; if (v.skipnews && (D.cpi || D.ppi || D.nfp || D.fomc)) continue;
      const int i0 = first_bar_at(s, D, hm_to_tday(9, 30), D.b0); if (i0 >= D.b1 || tday_min(s.t[i0]) != hm_to_tday(9, 30)) continue;
      std::array<double, NM> o, h, l, c; { double last = s.o[i0]; int j = i0; for (int m = 0; m < NM; ++m) { const int tm = hm_to_tday(9, 30) + m; while (j < D.b1 && tday_min(s.t[j]) < tm) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == tm) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; } else o[m] = h[m] = l[m] = c[m] = last; last = c[m]; } }
      const double op = o[0], atr = D.atr, gap = (op - D.pc) / atr; const int gdir = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      double orh = -1e18, orl = 1e18; for (int m = 0; m < 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
      const bool weak = D.vix >= 20 && ((!std::isnan(D.pibs) && D.pibs <= 0.3) || (!std::isnan(D.rsi2) && D.rsi2 <= 20) || (!std::isnan(P.ro) && (P.rc - P.ro) / atr <= -1));
      auto trade = [&](int sigm, int d, double stk) -> std::pair<T, int> {   // signal at minute sigm (price = close of the minute before, or the open at 0)
        const int em = sigm + v.delay; const double p = (em == 0 ? op : c[em - 1]) + d * v.slip, st = stk * atr; double ma = 0, pts = NAN, stop = p - d * st; bool be = false; int xm = NM - 1;
        for (int m = em; m < NM - 1; ++m) { ma = std::min(ma, d > 0 ? l[m] - p : p - h[m]);
          if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - p); xm = m + 1; break; }
          if (d > 0 ? h[m] >= p + 0.5 * st : l[m] <= p - 0.5 * st) { pts = 0.5 * st; xm = m + 1; break; }
          if (v.be > 0 && !be && (d > 0 ? h[m] - p : p - l[m]) >= v.be * st) { be = true; stop = p + d * 1.5; } }
        if (std::isnan(pts)) pts = d * (c[NM - 2] - p); return {{pts - 1.5, st, std::max(ma, -st) - 1.5}, xm}; };
      if (weak || D.dow == 1) { auto [t, x] = trade(0, 1, 0.25); cal.back().push_back(t); (void)x; continue; }
      // plan setups in time order, same direction; first = the day's trade; second only after a stop-out when v.second
      bool used[4] = {false, false, false, false}, gused = false; int Dd = 0, X = 0, ntr = 0; const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      while (ntr < (v.second ? 2 : 1)) { int bm = 1 << 30, bd = 0, src = -1, bi = -1; double bst = 0.25;
        auto cand = [&](int m, int d, double sk, int sc, int idx) { if (m < X || m > LAST || (Dd && d != Dd) || !d) return; if (m < bm) { bm = m; bd = d; bst = sk; src = sc; bi = idx; } };
        for (int q = 0; q < 4; ++q) if (!used[q] && CM[q] >= X) { const double mv = c[CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) cand(CM[q], mv > 0 ? 1 : -1, 0.25, 0, q); }
        if (!gused && gdir) for (int k = 3; 5 * k <= LAST; ++k) { if (5 * k < X) continue; double H = -1e18, L = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { H = std::max(H, h[m]); L = std::min(L, l[m]); }
          if (gdir > 0 ? (L <= orh && c[5 * k + 4] > orh) : (H >= orl && c[5 * k + 4] < orl)) { cand(5 * k + 5, gdir, 0.35, 1, 0); break; } }
        if (src < 0) break; if (src == 0) for (int q = 0; q <= bi; ++q) used[q] = true; if (src == 1) gused = true; if (!Dd) Dd = bd;
        if (bm + v.delay >= NM - 2) break;
        auto [t, xm] = trade(bm, bd, bst); cal.back().push_back(t); ++ntr; X = xm; if (t.pts > 0 || X >= NM - 1) break; } }
    return cal; };
  const payout::Plan* base = nullptr; for (auto& p : payout::plans()) if (p.name == "select_daily_25k") base = &p;
  payout::Plan L = *base; L.daily_loss = 0; L.buffer = 0; L.payout_mult = 0; L.min_payout = 0; L.min_profit = 1500; L.consistency = 0.20; L.profit_share = 1.0; L.caps = {1000}; L.win_days_needed = 0; L.win_day_min = 0;
  auto score = [&](const std::vector<std::vector<T>>& cal, std::vector<char> keep, const char* nm, bool quiet = false) -> std::array<double, 5> {
    const int Dn = cal.size(); std::vector<std::vector<payout::Trade>> sc(Dn); std::vector<const std::vector<payout::Trade>*> cv(Dn, nullptr); double rs = 0; int rn = 0;
    for (int i = 0; i < Dn; ++i) { if (cal[i].empty() || (!keep.empty() && !keep[i])) continue; double p = 0, ma = 0, mf = 0;
      for (auto& t : cal[i]) { const int n = std::clamp((int)std::floor(600 / (t.st * 2.0)), 1, 10); p += n * t.pts * 2; ma += n * t.mae * 2; mf += n * std::max(0.0, t.pts) * 2; rs += t.pts / t.st; ++rn; }
      sc[i] = {{p, std::min(ma, p), mf}}; cv[i] = &sc[i]; }
    int st = 0, p7 = 0, p10 = 0, p20 = 0, bl = 0;
    for (int s0 = 0; s0 + 22 < Dn; ++s0) { ++st; auto r = payout::run_account(L, cv, s0, 20); if (r.breach && r.days <= 7 && (r.pay_days.empty() || r.pay_days[0] > r.days)) ++bl;
      if (!r.pay_days.empty()) { const int d = r.pay_days[0]; p7 += d <= 7; p10 += d <= 10; p20 += d <= 20; } }
    std::array<double, 5> out = {rs / rn, 100.0 * p7 / st, 100.0 * p10 / st, 100.0 * p20 / st, 100.0 * bl / st};
    if (!quiet) printf("  %-44s %+.3f R/trade (n %4d) | payout ≤7d %5.2f%%  ≤10d %5.2f%%  ≤20d %5.2f%%  blown ≤7d %5.1f%%\n", nm, out[0], rn, out[1], out[2], out[3], out[4]);
    return out; };
  printf("Sprint execution tests — Lightning 25K, $600 risk, target 0.5R, NQ 2019-2024\n");
  V b; auto cb = gen(b); score(cb, {}, "BASE (as on the Plan page)");
  printf("1. late entry\n"); for (int dl : {1, 2, 3, 5}) { V x; x.delay = dl; char n[64]; snprintf(n, 64, "enter %d min late", dl); score(gen(x), {}, n); }
  printf("2. worse fill\n"); for (double sl : {5., 10., 20.}) { V x; x.slip = sl; char n[64]; snprintf(n, 64, "fill %.0f pts worse", sl); score(gen(x), {}, n); }
  printf("3. missed signals (mean of 20 random seeds)\n");
  for (double pm : {0.1, 0.2, 0.3}) { std::array<double, 5> m{}; for (int sd = 1; sd <= 20; ++sd) { std::mt19937 rng(sd); std::vector<char> keep(cb.size()); for (auto& k : keep) k = std::uniform_real_distribution<>(0, 1)(rng) >= pm;
      auto r = score(cb, keep, "", true); for (int q = 0; q < 5; ++q) m[q] += r[q] / 20; }
    printf("  skip %2.0f%% of days at random                  %+.3f R/trade          | payout ≤7d %5.2f%%  ≤10d %5.2f%%  ≤20d %5.2f%%  blown ≤7d %5.1f%%\n", pm * 100, m[0], m[1], m[2], m[3], m[4]); }
  { std::vector<char> keep(cb.size(), 1); for (size_t i = 1; i < cb.size(); ++i) { int j = i - 1; while (j >= 0 && cb[j].empty()) --j; if (j >= 0 && cb[j].back().pts < 0) keep[i] = 0; } score(cb, keep, "skip the day after a losing day"); }
  printf("4. news days\n"); { V x; x.skipnews = true; score(gen(x), {}, "skip CPI / PPI / NFP / FOMC days"); }
  printf("5. break-even\n"); { V x; x.be = 0.25; score(gen(x), {}, "break-even at +0.25R"); }
  printf("6. second chance\n"); { V x; x.second = true; score(gen(x), {}, "after a stop-out, the next setup (≤ 2/day)"); }
}
