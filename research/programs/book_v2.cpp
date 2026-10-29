// 2026-10-21 book_v2.cpp (founder: "how to improve the system" → "yes build book v2"). book_corr.cpp's strategies (same inputs and rules,
// its header follows) run as ONE book with the risk layer, every variant fixed before the run, NQ 2010-07 → 2026-10:
//  V0 every strategy with PF >= 1.25, one unit each, cost 1.5 pts (book_corr's BOOK A)
//  V1 ONE MOMENTUM SLOT: cascade, strong open, gap retest, flag-mid, strong 10:30 and the breakout are one family — only the day's FIRST
//     momentum entry is taken (by entry time; same minute → the higher PF); + the weak × VIX day sleeve + Monday long
//  V2 V1 with the MEASURED live slippage: market / stop entries pay 5 ticks more (1.25 pts; live fills 2026-09/10 averaged 4.6-5.7
//     ticks), limit entries (flag-mid) nothing extra; every exit 1 tick more
//  V3 V2 + a BOOK DAILY STOP of 250 pts at one unit (= −$500 at 1 MNQ): trades taken in entry order, a trade whose worst open loss
//     reaches the day's remaining room is closed there, no new trade after the stop
//  V4 V3 with the weakness and Monday trades at HALF size (they hold all day with no stop)
//  V5 V3 without weakness and Monday (momentum slot only)
// Points at today's ATR = constant risk in ATR (volatility sizing). Own account: pts/day, PF, years, drawdown, spread; 2025-26 separately
// (NB gap_or_retest, monday_long, pd_hold lists end 2024 — their sealed holdout). Prop: each variant at 1 / 2 MNQ per unit through
// Apex 50K EOD and Tradeify Select Flex 50K (payout_sim.hpp run_eval → run_account 120 days), from every 5th day 2014 → 2025-04:
// pass %, funded paid %, $ per eval bought before split and fees.
// ONE MONTH (founder: "we want to pass and get paid within 1 month"): eval traded with one variant at q_e MNQ per unit, the funded account
// (from the day after the pass) with another at q_f; share of evals whose FIRST payout lands within 21 / 42 trading days of buying, the
// first payout's size, and evals (fees) per success. V6 = the strong-day 10:30 trade alone (an eval tool). Usage: book_v2 [inputs dir]
// (header of book_corr.cpp:)
#include "research/meta.hpp"
#include "paper/payout_sim.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
struct Tr { size_t day; int tm, fam, strat; double g, mae, mfe; bool limit; };   // g / mae / mfe in points at today's ATR, gross; fam 0 momentum, 1 weak, 2 monday
int main(int argc, char** argv) {
  const std::string in = argc > 1 ? argv[1] : "research/programs/inputs_20261021"; const double cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr, vix; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; DB z{d.date, d.atr, d.vix, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; std::map<int, size_t> ci; for (size_t i = 0; i < days.size(); ++i) ci[days[i].date] = i; const size_t D = days.size();
  std::vector<Tr> T; std::vector<std::string> SN; std::vector<double> SPF;
  // momentum family members (priority = PF rank from book_corr: strong_1030 1.55, strong_breakout 1.49, flag_mid 1.45, gap_or_retest 1.34, strong_open 1.30, so_cascade 1.28)
  for (const char* id : {"so_cascade", "gap_or_retest", "strong_open", "monday_long"}) { const int st = SN.size(); SN.push_back(id);
    std::ifstream f(in + "/edge_lab/" + id + ".trades.csv"); if (!f) { fprintf(stderr, "missing %s\n", id); return 1; }
    std::string line; std::getline(f, line); while (std::getline(f, line)) { int date, dir; long long e, x; double g, net, mae, mfe; if (sscanf(line.c_str(), "%d,%lld,%lld,%d,%lf,%lf,%lf,%lf", &date, &e, &x, &dir, &g, &net, &mae, &mfe) != 8) continue;
      auto it = ci.find(date); if (it == ci.end()) continue; const double f2 = Anow / days[it->second].atr; const bool mon = std::string(id) == "monday_long";
      T.push_back({it->second, tday_min((int64_t)e), mon ? 2 : 0, st, g * f2, std::fabs(mae) * f2, std::fabs(mfe) * f2, false}); } }
  { const int st = SN.size(); SN.push_back("flag_mid"); std::ifstream f(in + "/flagmid_trades.csv"); if (!f) { fprintf(stderr, "missing flagmid\n"); return 1; }
    std::string line; std::getline(f, line); while (std::getline(f, line)) { int date, top, k; double g, mae, mfe; if (sscanf(line.c_str(), "%d,%d,%lf,%lf,%lf,%d", &date, &top, &g, &mae, &mfe, &k) != 6) continue;
      auto it = ci.find(date); if (it == ci.end()) continue; T.push_back({it->second, 930 + k, 0, st, g, mae, mfe, true}); } }
  const int sW = SN.size(); SN.push_back("weak_vix20_day"); const int sS = SN.size(); SN.push_back("strong_1030"); const int sB = SN.size(); SN.push_back("strong_breakout");
  std::vector<double> cls; for (auto& z : days) cls.push_back(z.C.back());
  for (size_t di = 1; di < D; ++di) { const DB& z = days[di]; const DB& p = days[di - 1]; const double A = z.atr, f = Anow / A;
    double ph = -1e18, pl = 1e18; for (size_t j = 0; j < p.C.size(); ++j) { ph = std::max(ph, p.H[j]); pl = std::min(pl, p.L[j]); }
    const double pc = p.C.back(), po = p.O[0], ibs = ph > pl ? (pc - pl) / (ph - pl) : 0.5; const bool lower2 = di >= 3 && cls[di - 1] < cls[di - 2] && cls[di - 2] < cls[di - 3];
    double a1 = NAN, l1 = NAN; for (size_t q = (di >= 30 ? di - 30 : 1); q < di; ++q) { const double ch = cls[q] - cls[q - 1], g = std::max(ch, 0.0), l = std::max(-ch, 0.0); a1 = std::isnan(a1) ? g : (a1 + g) / 2; l1 = std::isnan(l1) ? l : (l1 + l) / 2; }
    const double rsi2 = l1 > 0 ? 100 - 100 / (1 + a1 / l1) : 100; const bool weak = ibs < 0.3 || rsi2 < 20 || lower2 || (pc - po) < -1.0 * p.atr;
    if (weak && z.vix >= 20) { size_t j = 0; double mae = 0, mfe = 0; for (; j < z.C.size() && z.TM[j] < 1315; ++j) { mae = std::max(mae, (z.O[0] - z.L[j]) * f); mfe = std::max(mfe, (z.H[j] - z.O[0]) * f); }
      if (j < z.C.size()) T.push_back({di, 930, 1, sW, (z.O[j] - z.O[0]) * f, mae, mfe, false}); }
    double hi = -1e18, lo = 1e18; size_t k = 0; for (; k < z.C.size() && z.TM[k] < 990; ++k) { hi = std::max(hi, z.H[k]); lo = std::min(lo, z.L[k]); }
    if (k == 0 || k >= z.C.size()) continue; const size_t ke = k - 1; const double mv = z.C[ke] - z.O[0], path = hi > lo ? std::fabs(mv) / (hi - lo) : 0;
    if (!(path >= 0.70 && std::fabs(mv) >= 0.5 * A)) continue; const int dir = mv > 0 ? 1 : -1;
    auto run = [&](size_t k0, double e, bool fillK, double& mae, double& mfe) -> double { const double st = 0.15 * A; mae = mfe = 0; if (fillK && (dir > 0 ? e - z.L[k0] : z.H[k0] - e) >= st) { mae = st; return -st; }
      for (size_t j = k0 + 1; j < z.C.size(); ++j) { if (z.TM[j] >= 1315) return dir * (z.O[j] - e); const double adv = dir > 0 ? e - z.L[j] : z.H[j] - e; if (adv >= st) { mae = st; return -st; }
        mae = std::max(mae, adv); mfe = std::max(mfe, dir > 0 ? z.H[j] - e : e - z.L[j]); } return dir * (z.C.back() - e); };
    { double mae, mfe; const double g = run(ke, z.C[ke], false, mae, mfe); T.push_back({di, z.TM[ke], 0, sS, g * f, mae * f, mfe * f, false}); }
    const double ex = dir > 0 ? hi : lo, lev = ex + dir * 0.25;
    for (size_t j = ke + 1; j < z.C.size() && z.TM[j] < 1200; ++j) if (dir * ((dir > 0 ? z.H[j] : z.L[j]) - lev) >= 0) { const double e = dir > 0 ? std::max(z.O[j], lev) : std::min(z.O[j], lev); double mae, mfe; const double g = run(j, e, true, mae, mfe); T.push_back({di, z.TM[j], 0, sB, g * f, mae * f, mfe * f, false}); break; }
  }
  // priority within the momentum family (higher first) for same-minute ties
  std::map<int, int> prio; { const char* order[] = {"strong_1030", "strong_breakout", "flag_mid", "gap_or_retest", "strong_open", "so_cascade"}; for (int i = 0; i < 6; ++i) for (size_t s2 = 0; s2 < SN.size(); ++s2) if (SN[s2] == order[i]) prio[s2] = 6 - i; }
  std::vector<std::vector<size_t>> byDay(D); for (size_t i = 0; i < T.size(); ++i) byDay[T[i].day].push_back(i);
  for (auto& v : byDay) std::sort(v.begin(), v.end(), [&](size_t a, size_t b) { return T[a].tm != T[b].tm ? T[a].tm < T[b].tm : prio[T[a].strat] > prio[T[b].strat]; });
  // a variant → per day the list of (net pts, worst open loss pts, best open pts) at one unit, after its rules
  struct Out { double pnl, mae, mfe; };
  auto build = [&](int v) { std::vector<std::vector<Out>> R(D);
    for (size_t d = 0; d < D; ++d) { bool mom = false; double day = 0; bool stopped = false;
      for (size_t i : byDay[d]) { const Tr& t = T[i]; if (stopped) break;
        if (v == 0) { if (SN[t.strat] == "flag_mid" || SN[t.strat] == "so_cascade" || SN[t.strat] == "gap_or_retest" || SN[t.strat] == "strong_open" || SN[t.strat] == "strong_1030" || SN[t.strat] == "strong_breakout" || t.fam == 1 || t.fam == 2) R[d].push_back({t.g - cost, t.mae + cost, t.mfe}); continue; }
        if (t.fam == 0) { if (mom) continue; mom = true; } if (v == 5 && t.fam != 0) continue; if (v == 6 && SN[t.strat] != "strong_1030") continue;
        const double sz = (v == 4 && t.fam != 0) ? 0.5 : 1.0; const double c = v >= 2 ? cost + (t.limit ? 0 : 1.25) + 0.25 : cost;
        double pnl = sz * (t.g - c), mae = sz * (t.mae + c), mfe = sz * t.mfe;
        if (v >= 3) { const double room = 250 + day; if (mae >= room) { pnl = -room; mae = room; stopped = true; } }
        R[d].push_back({pnl, mae, mfe}); day += pnl; if (v >= 3 && day <= -250) stopped = true; } }
    return R; };
  const char* VN[7] = {"V0 everything PF>=1.25, 1 each", "V1 one momentum slot + weak + Monday", "V2 V1 + measured slippage", "V3 V2 + book day stop 250 pts", "V4 V3, weak / Monday half size", "V5 V3, momentum slot only", "V6 strong-day 10:30 alone"};
  auto era = [](int y) { return y <= 2015 ? 0 : y <= 2020 ? 1 : y <= 2024 ? 2 : 3; };
  printf("BOOK V2 — NQ, %zu NY days 2010-07 → 2026-10, points at today's ATR (%.0f), one unit = 1 contract (MNQ $ = pts × 2). IN-SAMPLE trade lists.\n\n", D, Anow);
  std::vector<std::vector<std::vector<Out>>> RR; for (int v = 0; v < 7; ++v) RR.push_back(build(v));
  for (int v = 0; v < 6; ++v) { const auto& R = RR[v]; double n = 0, gw = 0, gl = 0, sum = 0, cum = 0, pk = 0, dd = 0, worst = 0, m[4] = {0}, c[4] = {0}; int d2 = 0, d0 = 0; std::map<int, double> yr; size_t under = 0, maxUnder = 0;
    for (size_t d = 0; d < D; ++d) { double bd = 0; for (auto& o : R[d]) { n++; (o.pnl > 0 ? gw : gl) += std::fabs(o.pnl); bd += o.pnl; } d2 += R[d].size() >= 2; d0 += R[d].empty();
      sum += bd; cum += bd; if (cum >= pk) { pk = cum; under = 0; } else maxUnder = std::max(maxUnder, ++under); dd = std::max(dd, pk - cum); worst = std::min(worst, bd); yr[days[d].date / 10000] += bd; m[era(days[d].date / 10000)] += bd; c[era(days[d].date / 10000)]++; }
    int yp = 0; double yrDDmax = 0; for (auto& y : yr) yp += y.second > 0; (void)yrDDmax;
    printf("%-40s %.2f trades/day (2+ on %2.0f%% of days, none on %2.0f%%) · %+5.1f pts/day · PF %.2f · worst day %5.0f · max DD %5.0f ($%5.0f MNQ) · longest below high %3zu days · %zu/%zu yrs · pts/day by era %+.1f %+.1f %+.1f %+.1f\n",
           VN[v], n / D, 100.0 * d2 / D, 100.0 * d0 / D, sum / D, gl > 0 ? gw / gl : 99, worst, dd, 2 * dd, maxUnder, (size_t)yp, yr.size(), m[0] / c[0], m[1] / c[1], m[2] / c[2], m[3] / c[3]); }
  // ── prop ──
  printf("\nPROP (from every 5th day 2014-01 → 2025-04; eval to its target, then 120 funded days; $ before the trader's split and fees)\n");
  std::vector<std::vector<payout::Trade>> PD(D); std::vector<const std::vector<payout::Trade>*> ptr(D);
  for (const char* fn : {"apex_eod_50k", "select_flex_50k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == fn) P = &p; if (!P) continue;
    for (int v = 0; v < 6; ++v) for (int q : {1, 2}) {
      for (size_t d = 0; d < D; ++d) { PD[d].clear(); for (auto& o : RR[v][d]) PD[d].push_back({q * 2 * o.pnl, -q * 2 * o.mae, q * 2 * o.mfe}); ptr[d] = PD[d].empty() ? nullptr : &PD[d]; }
      int starts = 0, pass = 0, nF = 0, paid = 0, paid20 = 0, blown = 0; double sumPay = 0; std::vector<int> dtp;
      for (size_t s0 = 0; s0 < D; s0 += 5) { if (days[s0].date < 20140101 || days[s0].date > 20250430) continue; starts++;
        // eval: walk days until pass / fail (run_eval returns days used)
        const payout::EvalOut e = payout::run_eval(*P, ptr, s0, 120); if (!e.passed) continue; pass++; dtp.push_back(e.days); const int f0 = s0 + e.days; if (f0 >= (int)D) continue; nF++;
        const payout::RunOut o = payout::run_account(*P, ptr, f0, 120); paid += o.paid; paid20 += o.paid && o.first_pay <= 20; blown += o.breach && !o.paid; sumPay += o.paid_sum; }
      std::sort(dtp.begin(), dtp.end());
      printf("  %-17s %-40s %d MNQ/unit: eval pass %3.0f%% (median %3d days) · funded paid %3.0f%% (within 20 days %3.0f%%) · blown before paying %3.0f%% · $%5.0f per funded · $%5.0f per eval bought\n",
             fn, VN[v], q, 100.0 * pass / starts, dtp.empty() ? -1 : dtp[dtp.size() / 2], nF ? 100.0 * paid / nF : 0, nF ? 100.0 * paid20 / nF : 0, nF ? 100.0 * blown / nF : 0, nF ? sumPay / nF : 0, nF ? sumPay / nF * pass / starts : 0); } }

  // ── ONE MONTH ──
  printf("\nONE MONTH: first payout within 21 / 42 trading days of buying the eval (every 5th day 2014-01 → 2025-04). Eval variant at q_e MNQ, funded at q_f.\n");
  auto dayTr = [&](int v, int q, std::vector<std::vector<payout::Trade>>& X, std::vector<const std::vector<payout::Trade>*>& pt) { X.assign(D, {}); pt.assign(D, nullptr);
    for (size_t d = 0; d < D; ++d) { for (auto& o : RR[v][d]) X[d].push_back({q * 2 * o.pnl, -q * 2 * o.mae, q * 2 * o.mfe}); if (!X[d].empty()) pt[d] = &X[d]; } };
  struct Res { std::string line; double p21; };
  for (const char* fn : {"apex_eod_50k", "select_flex_50k", "select_daily_50k"}) { const payout::Plan* P = nullptr; for (auto& p : payout::plans()) if (p.name == fn) P = &p; if (!P) continue; std::vector<Res> res;
    for (int ve : {0, 1, 4, 6}) for (int qe : {1, 2, 3, 5, 10}) { std::vector<std::vector<payout::Trade>> XE; std::vector<const std::vector<payout::Trade>*> PE; dayTr(ve, qe, XE, PE);
      for (int vf : {0, 1, 4, 5}) for (int qf : {1, 2, 3}) { std::vector<std::vector<payout::Trade>> XF; std::vector<const std::vector<payout::Trade>*> PF2; dayTr(vf, qf, XF, PF2);
        int starts = 0, ok21 = 0, ok42 = 0, pass = 0; double first = 0;
        for (size_t s0 = 0; s0 < D; s0 += 5) { if (days[s0].date < 20140101 || days[s0].date > 20250430) continue; starts++;
          const payout::EvalOut e = payout::run_eval(*P, PE, s0, 42); if (!e.passed) continue; pass++; const int f0 = s0 + e.days; if (f0 >= (int)D) continue;
          const payout::RunOut o = payout::run_account(*P, PF2, f0, 42 - e.days); if (!o.paid) continue; const int tot = e.days + o.first_pay; ok42++; if (tot <= 21) { ok21++; first += o.pay_days.empty() ? 0 : 0; }
          if (tot <= 21) first += 0; }
        // first payout size (within 42)
        double fsum = 0; int fn2 = 0; for (size_t s0 = 0; s0 < D; s0 += 5) { if (days[s0].date < 20140101 || days[s0].date > 20250430) continue; const payout::EvalOut e = payout::run_eval(*P, PE, s0, 42); if (!e.passed) continue; const int f0 = s0 + e.days; if (f0 >= (int)D) continue;
          const payout::RunOut o = payout::run_account(*P, PF2, f0, 42 - e.days); if (o.paid && o.payouts > 0) { fsum += o.paid_sum / o.payouts; fn2++; } }
        char b[400]; snprintf(b, 400, "  %-17s eval %-30s %2d MNQ · funded %-30s %d MNQ: pass in 42 days %3.0f%% · PAID within 21 days %4.1f%% · within 42 days %4.1f%% · avg payout $%5.0f · evals per 21-day success %5.1f",
          fn, VN[ve], qe, VN[vf], qf, 100.0 * pass / starts, 100.0 * ok21 / starts, 100.0 * ok42 / starts, fn2 ? fsum / fn2 : 0, ok21 ? (double)starts / ok21 : 99.0);
        res.push_back({b, 100.0 * ok21 / starts}); } }
    std::sort(res.begin(), res.end(), [](const Res& a, const Res& b) { return a.p21 > b.p21; }); for (size_t i = 0; i < std::min<size_t>(8, res.size()); ++i) printf("%s\n", res[i].line.c_str()); }
  return 0;
}
