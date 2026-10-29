// 2026-10-21 book_corr.cpp (founder: "everything we can add that is above 1.25" → "I want at least 2-3 trades a day" → "yes but they are
// correlated" → "at least 2 uncorrelated"). Every strategy that held up, on NQ, as daily P&L at today's ATR after cost, then: how
// correlated they are, and the largest book of strategies with PF >= 1.25 whose daily P&L correlations are all below 0.30.
//  INPUTS (in-sample trade lists — the edge_lab screen's best variant, written with `edge_lab run --quick --trades`, and flag_tiny_tp's
//  export): inputs_20261021/edge_lab/{so_cascade,gap_or_retest,strong_open,monday_long,weak_day_long,pd_hold,mom_scan_3}.trades.csv,
//  inputs_20261021/flagmid_trades.csv. COMPUTED HERE: WEAK = the dry-run intraday sleeve (config/tradeify_day_config.json: the prior
//  RTH session closed weak — IBS < 0.3 or RSI2 < 20 or 2 lower closes or body < −1 ATR — and VIX prior close >= 20 → buy the 09:30 open,
//  sell 15:55); S1030 = trend_day_strats.cpp T1 (strong at 10:30 → with the move, stop 0.15 ATR, hold to 15:55); SBRK = T5 (the 10:30
//  extreme breakout, stop 0.15 ATR, until 14:00 entry).
// Points: gross × (today's ATR / that day's ATR) − 1.5 pts (one NQ round trip at today's size). Daily correlation over every NY day
// (0 on days a strategy does not trade). The book takes every member's trades at 1 unit each (no netting, members may be long and short
// the same day — on prop accounts that is one account's own trades, not cross-account hedging). Usage: book_corr [inputs dir] [--book a,b,c …]
// (each --book prints that named book's stats and the SPREAD of its trades — per day / week / month, the longest dry run, entry times — and its DRAWDOWN profile; added 2026-10-21 for the founder's "flags?")
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <ctime>
using namespace lab;
int main(int argc, char** argv) {
  const std::string in = argc > 1 && std::string(argv[1]).rfind("--", 0) != 0 ? argv[1] : "research/programs/inputs_20261021"; const double cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr, vix; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days; std::map<int, double> atrOf;
  for (auto& d : s.days) { if (d.ok && d.atr > 0) atrOf[d.date] = d.atr; if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z{d.date, d.atr, d.vix, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; std::map<int, size_t> ci; for (size_t i = 0; i < days.size(); ++i) ci[days[i].date] = i;
  std::vector<std::string> names; std::vector<std::vector<std::vector<double>>> P; std::vector<std::vector<std::vector<int>>> E;   // strategy → day → trade nets / entry tday_min
  auto addStrat = [&](const std::string& n) { names.push_back(n); P.push_back(std::vector<std::vector<double>>(days.size())); E.push_back(std::vector<std::vector<int>>(days.size())); return P.size() - 1; };
  auto put = [&](size_t k, size_t di, double net, int tm) { P[k][di].push_back(net); E[k][di].push_back(tm); };
  for (const char* id : {"so_cascade", "gap_or_retest", "strong_open", "monday_long", "weak_day_long", "pd_hold", "mom_scan_3"}) {
    std::ifstream f(in + "/edge_lab/" + id + ".trades.csv"); if (!f) { fprintf(stderr, "missing %s\n", id); return 1; } const size_t k = addStrat(id);
    std::string line; std::getline(f, line); while (std::getline(f, line)) { int date; long long e, x; int dir; double g; if (sscanf(line.c_str(), "%d,%lld,%lld,%d,%lf", &date, &e, &x, &dir, &g) != 5) continue;
      auto it = ci.find(date); if (it == ci.end()) continue; put(k, it->second, g * Anow / days[it->second].atr - cost, tday_min((int64_t)e)); } }
  { std::ifstream f(in + "/flagmid_trades.csv"); if (!f) { fprintf(stderr, "missing flagmid_trades.csv\n"); return 1; } const size_t k = addStrat("flag_mid");
    std::string line; std::getline(f, line); while (std::getline(f, line)) { int date, top, kk = 0; double g, m1, m2; if (sscanf(line.c_str(), "%d,%d,%lf,%lf,%lf,%d", &date, &top, &g, &m1, &m2, &kk) < 3) continue; auto it = ci.find(date); if (it == ci.end()) continue; put(k, it->second, g - cost, 930 + kk); } }
  // WEAK, S1030, SBRK
  const size_t kW = addStrat("weak_vix20_day"), kS = addStrat("strong_1030"), kB = addStrat("strong_breakout");
  std::vector<double> cls; for (size_t di = 0; di < days.size(); ++di) cls.push_back(days[di].C.back());
  for (size_t di = 1; di < days.size(); ++di) { const DB& z = days[di]; const DB& p = days[di - 1]; const double A = z.atr, f = Anow / A;
    // prior RTH session weakness
    double ph = -1e18, pl = 1e18; for (size_t j = 0; j < p.C.size() && p.TM[j] < 1320; ++j) { ph = std::max(ph, p.H[j]); pl = std::min(pl, p.L[j]); }
    const double pc = p.C.back(), po = p.O[0], ibs = ph > pl ? (pc - pl) / (ph - pl) : 0.5;
    bool lower2 = di >= 3 && cls[di - 1] < cls[di - 2] && cls[di - 2] < cls[di - 3];
    double ag = 0, al = 0; if (di >= 4) { for (int q = 3; q >= 1; --q) { (void)q; } double a1 = NAN, l1 = NAN; for (size_t q = (di >= 30 ? di - 30 : 1); q < di; ++q) { const double ch = cls[q] - cls[q - 1], g = std::max(ch, 0.0), l = std::max(-ch, 0.0); a1 = std::isnan(a1) ? g : (a1 + g) / 2; l1 = std::isnan(l1) ? l : (l1 + l) / 2; } ag = a1; al = l1; }
    const double rsi2 = al > 0 ? 100 - 100 / (1 + ag / al) : 100; const bool weak = ibs < 0.3 || rsi2 < 20 || lower2 || (pc - po) < -1.0 * p.atr;
    if (weak && z.vix >= 20) { size_t j = 0; while (j < z.C.size() && z.TM[j] < 1315) ++j; if (j < z.C.size()) put(kW, di, (z.O[j] - z.O[0]) * f - cost, 930); }
    // strong at 10:30
    double hi = -1e18, lo = 1e18; size_t k = 0; for (; k < z.C.size() && z.TM[k] < 990; ++k) { hi = std::max(hi, z.H[k]); lo = std::min(lo, z.L[k]); }
    if (k == 0 || k >= z.C.size()) continue; const size_t ke = k - 1; const double mv = z.C[ke] - z.O[0], path = hi > lo ? std::fabs(mv) / (hi - lo) : 0;
    if (!(path >= 0.70 && std::fabs(mv) >= 0.5 * A)) continue; const int dir = mv > 0 ? 1 : -1;
    auto run = [&](size_t k0, double e, bool fillK) -> double { const double st = 0.15 * A; if (fillK && (dir > 0 ? e - z.L[k0] : z.H[k0] - e) >= st) return -st;
      for (size_t j = k0 + 1; j < z.C.size(); ++j) { if (z.TM[j] >= 1315) return dir * (z.O[j] - e); if ((dir > 0 ? e - z.L[j] : z.H[j] - e) >= st) return -st; } return dir * (z.C.back() - e); };
    put(kS, di, run(ke, z.C[ke], false) * f - cost, z.TM[ke]);
    const double ex = dir > 0 ? hi : lo, lev = ex + dir * 0.25;
    for (size_t j = ke + 1; j < z.C.size() && z.TM[j] < 1200; ++j) if (dir * ((dir > 0 ? z.H[j] : z.L[j]) - lev) >= 0) { const double e = dir > 0 ? std::max(z.O[j], lev) : std::min(z.O[j], lev); put(kB, di, run(j, e, true) * f - cost, z.TM[j]); break; } }
  const size_t N = names.size(), D = days.size();
  std::vector<std::vector<double>> dp(N, std::vector<double>(D, 0)); for (size_t a = 0; a < N; ++a) for (size_t d = 0; d < D; ++d) for (double x : P[a][d]) dp[a][d] += x;
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  printf("Book of the surviving strategies — NQ, %zu NY days 2010-07 → 2026-10, points at today's ATR (%.0f) after %.1f pts per trade. IN-SAMPLE trade lists.\n\n", D, Anow, cost);
  printf("%-18s %7s %6s %8s %6s %6s  2010-15  2016-20  2021-24  2025-26 (pts/trade)\n", "strategy", "trades", "/day", "pts/tr", "PF", "win");
  std::vector<double> pf(N);
  for (size_t a = 0; a < N; ++a) { double n = 0, sm = 0, gw = 0, gl = 0, w = 0, m[4] = {0}, c[4] = {0}; for (size_t d = 0; d < D; ++d) for (double x : P[a][d]) { n++; sm += x; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); m[era(days[d].date / 10000)] += x; c[era(days[d].date / 10000)]++; }
    pf[a] = gl > 0 ? gw / gl : 99; printf("%-18s %7.0f %6.2f %+8.2f %6.2f %5.0f%%", names[a].c_str(), n, n / D, n ? sm / n : 0, pf[a], n ? 100 * w / n : 0);
    for (int e = 0; e < 4; ++e) printf("  %+7.2f", c[e] ? m[e] / c[e] : NAN); printf("\n"); }
  auto corr = [&](size_t a, size_t b) { double ma = 0, mb = 0; for (size_t d = 0; d < D; ++d) { ma += dp[a][d]; mb += dp[b][d]; } ma /= D; mb /= D; double sab = 0, saa = 0, sbb = 0;
    for (size_t d = 0; d < D; ++d) { const double x = dp[a][d] - ma, y = dp[b][d] - mb; sab += x * y; saa += x * x; sbb += y * y; } return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0.0; };
  auto sameDay = [&](size_t a, size_t b) { int both = 0, ea = 0; for (size_t d = 0; d < D; ++d) { if (!P[a][d].empty()) { ea++; if (!P[b][d].empty()) both++; } } return ea ? (double)both / ea : 0.0; };
  printf("\nDAILY P&L CORRELATION (0 on non-trading days)\n%-18s", ""); for (size_t b = 0; b < N; ++b) printf(" %6.6s", names[b].c_str()); printf("\n");
  for (size_t a = 0; a < N; ++a) { printf("%-18s", names[a].c_str()); for (size_t b = 0; b < N; ++b) printf(" %+6.2f", a == b ? 1.0 : corr(a, b)); printf("\n"); }
  printf("\nSHARE OF A's TRADING DAYS ON WHICH B ALSO TRADES\n%-18s", ""); for (size_t b = 0; b < N; ++b) printf(" %6.6s", names[b].c_str()); printf("\n");
  for (size_t a = 0; a < N; ++a) { printf("%-18s", names[a].c_str()); for (size_t b = 0; b < N; ++b) printf(" %5.0f%%", a == b ? 100.0 : 100 * sameDay(a, b)); printf("\n"); }
  // books
  auto bookStats = [&](const std::vector<size_t>& mem, const char* title) { std::vector<double> bd(D, 0); double tr = 0, days2 = 0, gw = 0, gl = 0, sum = 0, worst = 0, cum = 0, pk = 0, dd = 0; int ypos = 0, yn = 0; std::map<int, double> yr;
    for (size_t d = 0; d < D; ++d) { int nt = 0; for (size_t a : mem) { bd[d] += dp[a][d]; nt += P[a][d].size(); for (double x : P[a][d]) (x > 0 ? gw : gl) += std::fabs(x); } tr += nt; days2 += nt >= 2; sum += bd[d]; worst = std::min(worst, bd[d]); cum += bd[d]; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); yr[days[d].date / 10000] += bd[d]; }
    for (auto& y : yr) { yn++; ypos += y.second > 0; } double mx = 0; for (size_t i = 0; i < mem.size(); ++i) for (size_t j = i + 1; j < mem.size(); ++j) mx = std::max(mx, corr(mem[i], mem[j]));
    printf("\n%s:", title); for (size_t a : mem) printf(" %s", names[a].c_str());
    printf("\n   %.2f trades a day (>= 2 trades on %.0f%% of days) · %+.1f pts a day · PF (trades) %.2f · worst day %.0f pts · max drawdown %.0f pts · %d/%d years positive · max pair correlation %.2f\n",
           tr / D, 100 * days2 / D, sum / D, gl > 0 ? gw / gl : 99, worst, dd, ypos, yn, mx); };
  std::vector<size_t> all; for (size_t a = 0; a < N; ++a) if (pf[a] >= 1.25) all.push_back(a); bookStats(all, "BOOK A — every strategy with PF >= 1.25 (correlation ignored)");
  // greedy: highest daily mean / sd first, add while every pair correlation < 0.30 and PF >= 1.25
  std::vector<size_t> order = all; auto sr = [&](size_t a) { double m = 0, v = 0; for (double x : dp[a]) m += x; m /= D; for (double x : dp[a]) v += (x - m) * (x - m); return m / std::sqrt(v / D + 1e-9); };
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return sr(a) > sr(b); });
  for (double cap : {0.30, 0.20, 0.10}) { std::vector<size_t> bk; for (size_t a : order) { bool ok = true; for (size_t b : bk) if (corr(a, b) >= cap) ok = false; if (ok) bk.push_back(a); }
    char t[96]; snprintf(t, 96, "BOOK — PF >= 1.25 and every pair's daily correlation < %.2f (greedy by daily mean / sd)", cap); bookStats(bk, t); }

  // spread of a book's trades: per day, per calendar week / month, longest run of trading days without a trade, entry time of day
  auto spread = [&](const std::vector<size_t>& mem, const std::string& title) {
    int hist[4] = {0, 0, 0, 0}; std::map<long, int> wk, mo; int gap = 0, maxGap = 0, gapEnd = 0; int tb[6] = {0}; long tot = 0;
    for (size_t d = 0; d < D; ++d) { int nt = 0; for (size_t a : mem) { nt += P[a][d].size(); for (int tm : E[a][d]) { tb[tm < 960 ? 0 : tm < 990 ? 1 : tm < 1020 ? 2 : tm < 1080 ? 3 : tm < 1200 ? 4 : 5]++; tot++; } }
      hist[std::min(nt, 3)]++; const int dt = days[d].date; const int y = dt / 10000, m = dt / 100 % 100, dd = dt % 100;
      std::tm t{}; t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = dd; t.tm_hour = 12; const std::time_t tt = timegm(&t); const long wkey = (long)((tt / 86400 + 3) / 7);   // Monday-based week
      wk[wkey] += nt; mo[(long)y * 100 + m] += nt; if (nt) gap = 0; else if (++gap > maxGap) { maxGap = gap; gapEnd = dt; } }
    auto dist = [&](std::map<long, int>& mp, std::vector<int> cuts, const char* unit) { std::vector<int> c(cuts.size() + 1, 0), v; for (auto& x : mp) { v.push_back(x.second); size_t i = 0; while (i < cuts.size() && x.second > cuts[i]) ++i; c[i]++; }
      std::sort(v.begin(), v.end()); printf("   per %s (%zu): median %d, 10th pct %d, 90th pct %d · ", unit, v.size(), v[v.size() / 2], v[v.size() / 10], v[v.size() * 9 / 10]);
      for (size_t i = 0; i <= cuts.size(); ++i) { if (i == 0) printf("<=%d: %.0f%%", cuts[0], 100.0 * c[0] / v.size()); else if (i < cuts.size()) printf(" · %d-%d: %.0f%%", cuts[i - 1] + 1, cuts[i], 100.0 * c[i] / v.size()); else printf(" · >%d: %.0f%%", cuts.back(), 100.0 * c[i] / v.size()); } printf("\n"); };
    printf("SPREAD %s\n   per day: no trade %.0f%% · 1 trade %.0f%% · 2 trades %.0f%% · 3+ trades %.0f%% · longest run of NY days without a trade %d (ending %d)\n", title.c_str(),
           100.0 * hist[0] / D, 100.0 * hist[1] / D, 100.0 * hist[2] / D, 100.0 * hist[3] / D, maxGap, gapEnd);
    dist(wk, {0, 2, 5, 10}, "week"); dist(mo, {5, 10, 20, 40}, "month");
    printf("   entry time: 09:30-10:00 %.0f%% · 10:00-10:30 %.0f%% · 10:30-11:00 %.0f%% · 11:00-12:00 %.0f%% · 12:00-14:00 %.0f%% · after 14:00 %.0f%%\n",
           100.0 * tb[0] / tot, 100.0 * tb[1] / tot, 100.0 * tb[2] / tot, 100.0 * tb[3] / tot, 100.0 * tb[4] / tot, 100.0 * tb[5] / tot); };

  // drawdown profile of a book (points at today's ATR, 1 contract of each member; × $2 = MNQ, × $20 = NQ)
  auto ddProfile = [&](const std::vector<size_t>& mem, const std::string& title) {
    std::vector<double> b(D, 0); for (size_t d = 0; d < D; ++d) for (size_t a : mem) b[d] += dp[a][d];
    double cum = 0, pk = 0, maxDD = 0; size_t pkI = 0, ddS = 0, ddE = 0, under = 0, maxUnder = 0, uS = 0, uE = 0; std::vector<double> depths; double curDepth = 0;
    int streak = 0, maxStreak = 0; double worst5 = 0, worst20 = 0; std::map<int, double> yrDD, yrPnl; std::map<int, double> yrPk, yrCum;
    for (size_t d = 0; d < D; ++d) { cum += b[d];
      if (cum >= pk) { if (curDepth > 0) depths.push_back(curDepth); curDepth = 0; pk = cum; pkI = d; under = 0; } else { under++; curDepth = std::max(curDepth, pk - cum); if (under > maxUnder) { maxUnder = under; uS = pkI; uE = d; } }
      if (pk - cum > maxDD) { maxDD = pk - cum; ddS = pkI; ddE = d; }
      if (b[d] < 0) { if (++streak > maxStreak) maxStreak = streak; } else if (b[d] > 0) streak = 0;
      double s5 = 0, s20 = 0; for (size_t q = d >= 4 ? d - 4 : 0; q <= d; ++q) s5 += b[q]; for (size_t q = d >= 19 ? d - 19 : 0; q <= d; ++q) s20 += b[q]; worst5 = std::min(worst5, s5); worst20 = std::min(worst20, s20);
      const int y = days[d].date / 10000; yrCum[y] += b[d]; yrPnl[y] += b[d]; yrPk[y] = std::max(yrPk[y], yrCum[y]); yrDD[y] = std::max(yrDD[y], yrPk[y] - yrCum[y]); }
    if (curDepth > 0) depths.push_back(curDepth);
    int over[4] = {0}; const double lv[4] = {250, 500, 1000, 2000}; for (double x : depths) for (int i = 0; i < 4; ++i) over[i] += x >= lv[i];
    double worstDay = 0; for (double x : b) worstDay = std::min(worstDay, x);
    printf("DRAWDOWN %s (pts at today's ATR, 1 contract each; MNQ $ = × 2, NQ $ = × 20)\n", title.c_str());
    printf("   max drawdown %.0f pts ($%.0f MNQ / $%.0f NQ) from %d to %d · longest time below a high %zu NY days (%d → %d) · still below its high %s\n",
           maxDD, 2 * maxDD, 20 * maxDD, days[ddS].date, days[ddE].date, maxUnder, days[uS].date, days[uE].date, cum < pk ? ("yes, " + std::to_string((int)(pk - cum)) + " pts").c_str() : "no");
    printf("   worst day %.0f pts · worst 5 days %.0f · worst 20 days %.0f · longest run of losing days %d · drawdowns deeper than 250 / 500 / 1,000 / 2,000 pts: %d / %d / %d / %d\n",
           worstDay, worst5, worst20, maxStreak, over[0], over[1], over[2], over[3]);
    printf("   by year (P&L / max drawdown inside the year, pts):"); for (auto& y : yrPnl) printf(" %d %+.0f/%.0f", y.first, y.second, yrDD[y.first]); printf("\n"); };
  for (int ai = 1; ai + 1 < argc; ++ai) if (std::string(argv[ai]) == "--book") { std::vector<size_t> bk; std::stringstream ss(argv[ai + 1]); std::string nm;
      while (std::getline(ss, nm, ',')) for (size_t a = 0; a < N; ++a) if (names[a] == nm) bk.push_back(a);
      bookStats(bk, ("CUSTOM BOOK " + std::string(argv[ai + 1])).c_str()); spread(bk, argv[ai + 1]); ddProfile(bk, argv[ai + 1]); }
  for (size_t a : all) { std::vector<size_t> one{a}; char t[64]; snprintf(t, 64, "single %s", names[a].c_str()); bookStats(one, t); }
  return 0;
}
