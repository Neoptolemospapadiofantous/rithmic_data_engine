// 2026-10-20 vol_trend_model.cpp (founder, after the prop tests failed: "what models can we use and train" → "to get better results").
// Models that predict SIZE (which every earlier test found forecastable) rather than direction (which none did), trained
// walk-forward — each test year 2014-26 is predicted by a model fitted only on the years before it. Fixed before the run:
//  M1 RANGE model (ridge regression, λ = 1) of log(today's RTH range / ATR14) from what is known at 09:30: gap, yesterday's range,
//     the overnight range, log VIX, its 1- and 5-day change, yesterday's IBS, 2-day RSI, 5- / 10-day path and move (e5 r5 e10 r10),
//     FOMC / CPI / NFP / OPEX day. Scored by R² against "today = the average range".
//  M2 TREND-DAY model (logistic regression) at 10:30 of "the day closes in the first hour's direction with |close − open| >= 50 %
//     of the day's range", from the 09:30 inputs + the first hour's move, range and straightness (all / ATR) and its volume vs the
//     last 20 days' first hours. Scored by AUC.
//  U1 the weak-day trade (weak_day_1k.cpp) sized by M1: units = round(N × median predicted range / predicted range), 0 if the
//     prediction is in the top 10 % of earlier years' predictions (stand aside on the wildest days)
//  U2 the TREND trade: at 10:30 go with the first hour (long if 10:30 close > 09:30 open), hold to 15:55, no stop; all days vs the
//     days in M2's top third (threshold from earlier years); also with M1 sizing
//  U3 (founder: "test") BOTH on one account: U1 sized by M1 + U2 M2 top third sized by M1; a day with both trades is one
//     trade with the summed P&L and the summed worst open losses (conservative); also 3 / 5 accounts on the same trades
//  FAST (founder: "we cant wait months to get a payout"): from every trading day 2014-01 … 2025-10 buy an eval and keep buying a
//     new one the day after each fails; how long until the FIRST payout lands, what share of starts are paid within 20 / 40 / 60
//     trading days, fees spent and payouts received in those 60 days, per strategy and size (2-20 MNQ; the plan caps 4 minis)
//  Each on Tradeify Select Daily 50K (payout_sim.hpp rules; $165 evals; 90 % share; points scaled to today's ATR; cost 1.5 pts +
//  $1.82): pass %, funded payouts, 12-month windows from every month 2014-01 … 2025-10 — net a month, % of windows >= $500 a month.
// Usage: vol_trend_model
#include "paper/payout_sim.hpp"
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <numeric>
#include <functional>
using namespace lab;
// ridge regression / logistic regression on standardised features (intercept unpenalised)
struct Lin { std::vector<double> mu, sd, w; double b = 0; };
static Lin fit(const std::vector<std::vector<double>>& X, const std::vector<double>& y, bool logistic, double lam) {
  const size_t n = X.size(), p = X[0].size(); Lin m; m.mu.assign(p, 0); m.sd.assign(p, 0); m.w.assign(p, 0);
  for (auto& x : X) for (size_t j = 0; j < p; ++j) m.mu[j] += x[j] / n;
  for (auto& x : X) for (size_t j = 0; j < p; ++j) m.sd[j] += (x[j] - m.mu[j]) * (x[j] - m.mu[j]) / n;
  for (auto& s : m.sd) s = s > 1e-12 ? std::sqrt(s) : 1;
  double ym = std::accumulate(y.begin(), y.end(), 0.0) / n; m.b = logistic ? std::log(std::clamp(ym, 0.01, 0.99) / (1 - std::clamp(ym, 0.01, 0.99))) : ym;
  for (int it = 0; it < 3000; ++it) { std::vector<double> g(p, 0); double gb = 0;
    for (size_t i = 0; i < n; ++i) { double z = m.b; for (size_t j = 0; j < p; ++j) z += m.w[j] * (X[i][j] - m.mu[j]) / m.sd[j];
      const double pr = logistic ? 1 / (1 + std::exp(-z)) : z, e = pr - y[i]; gb += e / n; for (size_t j = 0; j < p; ++j) g[j] += e * (X[i][j] - m.mu[j]) / m.sd[j] / n; }
    for (size_t j = 0; j < p; ++j) m.w[j] -= 0.1 * (g[j] + lam * m.w[j] / n); m.b -= 0.1 * gb; }
  return m; }
static double pred(const Lin& m, const std::vector<double>& x, bool logistic) { double z = m.b; for (size_t j = 0; j < x.size(); ++j) z += m.w[j] * (x[j] - m.mu[j]) / m.sd[j]; return logistic ? 1 / (1 + std::exp(-z)) : z; }
static double auc(const std::vector<double>& p, const std::vector<double>& y) { std::vector<size_t> o(p.size()); std::iota(o.begin(), o.end(), 0);
  std::sort(o.begin(), o.end(), [&](size_t a, size_t b) { return p[a] < p[b]; }); double r = 0, np = 0, nn = 0; for (size_t k = 0; k < o.size(); ++k) { if (y[o[k]] > 0.5) { r += k + 1; np++; } else nn++; }
  return (r - np * (np + 1) / 2) / std::max(1.0, np * nn); }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); load_events("data/edge_lab/cache", s);
  struct D { int date, year; double atr, vix, lrange, trend, dir, wkPts, wkMae, tdPts, tdMae; bool weak; std::vector<double> x0, x1; double pR = NAN, pT = NAN, thrR = NAN, thrT = NAN, medR = NAN; };
  std::vector<D> V; double atrNow = 0; int dNow = 0; const Day* p1 = nullptr; const Day* p2 = nullptr; const Day* p3 = nullptr; std::vector<double> v1h;
  for (auto& d : s.days) { if (!d.ok) continue; if (d.atr > 0 && d.date <= 20261016) { atrNow = d.atr; dNow = d.date; }
    double onh = -1e18, onl = 1e18, e = NAN, x = NAN, lo = 1e18, h1 = -1e18, l1 = 1e18, c1 = NAN, vol1 = 0, x2 = NAN, mae2 = 0;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930) { onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); continue; }
      if (std::isnan(e)) e = s.o[i]; if (tm >= 1315) { x = s.o[i]; break; } lo = std::min(lo, (double)s.l[i]);
      if (tm < 990) { h1 = std::max(h1, (double)s.h[i]); l1 = std::min(l1, (double)s.l[i]); c1 = s.c[i]; vol1 += s.v[i]; } }
    const bool okRow = p1 && p2 && p3 && !d.rollwin && d.atr > 0 && !std::isnan(d.vix) && !std::isnan(e) && !std::isnan(x) && !std::isnan(c1) && d.date >= 20100901 && d.date <= 20261016 && onh > onl;
    if (okRow) { const double dir = c1 > e ? 1 : -1;   // the trend trade: from the 10:30 close (c1 = the 10:29 bar's close) to the 15:55 open
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 990) continue; if (tm >= 1315) { x2 = s.o[i]; break; } mae2 = std::min(mae2, dir > 0 ? s.l[i] - c1 : c1 - s.h[i]); }
      double vavg = 0; for (double v : v1h) vavg += v; vavg = v1h.empty() ? vol1 : vavg / v1h.size();
      const double A = d.atr, ibs = p1->rh > p1->rl ? (p1->rc - p1->rl) / (p1->rh - p1->rl) : 0.5, gap = (e - d.pc) / A;
      D z; z.date = d.date; z.year = d.date / 10000; z.atr = A; z.vix = d.vix; z.dir = dir;
      z.lrange = std::log(std::max((d.rh - d.rl) / A, 0.05)); z.trend = ((d.rc - d.ro) * dir > 0 && std::fabs(d.rc - d.ro) >= 0.5 * (d.rh - d.rl)) ? 1 : 0;
      z.weak = ibs < 0.3 || (!std::isnan(d.rsi2) && d.rsi2 < 20) || p1->rc <= p1->ro - A || (p1->rc < p2->rc && p2->rc < p3->rc);
      z.wkPts = x - e; z.wkMae = std::min(0.0, lo - e); z.tdPts = std::isnan(x2) ? 0 : (x2 - c1) * dir; z.tdMae = mae2;
      auto nz = [](double v) { return std::isnan(v) ? 0.0 : v; };
      z.x0 = {gap, std::fabs(gap), (p1->rh - p1->rl) / A, (onh - onl) / A, std::log(d.vix), nz(d.vixc1), nz(d.vixc5), ibs, nz(d.rsi2) / 100, nz(d.e5), nz(d.r5), nz(d.e10), nz(d.r10), (double)d.fomc, (double)d.cpi, (double)d.nfp, (double)d.opex};
      z.x1 = z.x0; const double r1 = std::max(h1 - l1, 0.25); z.x1.insert(z.x1.end(), {(c1 - e) / A * dir, r1 / A, std::fabs(c1 - e) / r1, std::log(std::max(vol1, 1.0) / std::max(vavg, 1.0))});
      V.push_back(std::move(z)); }
    v1h.push_back(vol1); if (v1h.size() > 20) v1h.erase(v1h.begin());
    p3 = p2; p2 = p1; p1 = &d; }
  // walk-forward: models refit each test year on all earlier years
  for (int Y = 2014; Y <= 2026; ++Y) { std::vector<std::vector<double>> X0, X1; std::vector<double> yR, yT;
    for (auto& z : V) if (z.year < Y) { X0.push_back(z.x0); yR.push_back(z.lrange); X1.push_back(z.x1); yT.push_back(z.trend); }
    const Lin mR = fit(X0, yR, false, 1.0), mT = fit(X1, yT, true, 1.0);
    std::vector<double> trR, trT; for (auto& z : V) if (z.year < Y) { trR.push_back(pred(mR, z.x0, false)); trT.push_back(pred(mT, z.x1, true)); }
    std::sort(trR.begin(), trR.end()); std::sort(trT.begin(), trT.end());
    for (auto& z : V) if (z.year == Y) { z.pR = pred(mR, z.x0, false); z.pT = pred(mT, z.x1, true); z.thrR = trR[trR.size() * 9 / 10]; z.thrT = trT[trT.size() * 2 / 3]; z.medR = trR[trR.size() / 2]; } }
  std::vector<D*> TS; for (auto& z : V) if (!std::isnan(z.pR)) TS.push_back(&z);
  printf("SIZE MODELS — NQ, walk-forward 2014-26 (%zu test days), points scaled to ATR now %.0f (%d)\n", TS.size(), atrNow, dNow);
  { double m = 0; for (auto* z : TS) m += z->lrange; m /= TS.size(); double ss = 0, sr = 0, sb = 0, bm = 0;
    for (auto* z : TS) { ss += (z->lrange - m) * (z->lrange - m); sr += (z->lrange - z->pR) * (z->lrange - z->pR); }
    // baseline: each year predicted by the earlier years' average
    for (auto* z : TS) { double a = 0; int k = 0; for (auto& w : V) if (w.year < z->year) { a += w.lrange; k++; } bm = a / k; sb += (z->lrange - bm) * (z->lrange - bm); }
    printf("\n  M1 range model: R² %.3f out of sample (vs the earlier years' average: R² %.3f)\n", 1 - sr / ss, 1 - sb / ss);
    std::vector<D*> o = TS; std::sort(o.begin(), o.end(), [](D* a, D* b) { return a->pR < b->pR; });
    printf("     by predicted range, fifths (actual range / ATR):"); for (int q = 0; q < 5; ++q) { double a = 0; int k = 0; for (size_t i = o.size() * q / 5; i < o.size() * (q + 1) / 5; ++i) { a += std::exp(o[i]->lrange); k++; } printf(" %.2f", a / k); } printf("\n"); }
  { std::vector<double> p, y; for (auto* z : TS) { p.push_back(z->pT); y.push_back(z->trend); } double base = std::accumulate(y.begin(), y.end(), 0.0) / y.size();
    double tt = 0, tn = 0; for (auto* z : TS) if (z->pT >= z->thrT) { tt += z->trend; tn++; }
    printf("  M2 trend-day model: AUC %.3f (0.5 = chance) · trend days %.0f%% of all, %.0f%% of the top third (%.0f days)\n", auc(p, y), 100 * base, 100 * tt / tn, tn); }

  const payout::Plan* PL = nullptr; for (auto& p : payout::plans()) if (p.name == "select_daily_50k") PL = &p;
  std::vector<int> cal; for (auto* z : TS) cal.push_back(z->date); std::map<int, int> ci; for (size_t i = 0; i < cal.size(); ++i) ci[cal[i]] = (int)i;
  auto dayNum = [](int dt) { std::tm a{}; a.tm_year = dt / 10000 - 1900; a.tm_mon = dt / 100 % 100 - 1; a.tm_mday = dt % 100; a.tm_hour = 12; return (int)(timegm(&a) / 86400); };
  std::vector<char> lastOfWeek(cal.size(), 0); for (size_t i = 0; i < cal.size(); ++i) lastOfWeek[i] = i + 1 == cal.size() || (dayNum(cal[i + 1]) + 3) / 7 != (dayNum(cal[i]) + 3) / 7;
  const payout::Plan* PLs[2] = {PL, PL};
  struct Acct { bool funded = false, dead = false; double bal = 0, peak = 0, floor = 0, best = 0, cyc = 0; int wins = 0, days = 0, payouts = 0; bool traded = false; };
    auto fresh = [&](int kind) { Acct a; a.bal = a.peak = PLs[kind]->start; a.floor = a.bal - PLs[kind]->drawdown; return a; };
  // (same day logic as flag_setforget.cpp: eval = EOD trail at each trade's MAE, soft daily loss, target + consistency; funded = run_account)
  auto step = [&](int kind, Acct& a, const std::vector<payout::Trade>& tr, size_t ix) -> double {
    const payout::Plan& p = *PLs[kind];
    if (!a.funded) { double cut = p.eval_daily_loss, dp = 0; bool any = false;
      for (auto& t : tr) { any = true; const double w = std::min(t.mae, t.pnl); if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; }
      a.traded |= any; if (lastOfWeek[ix] && !a.traded) { dp -= 5; any = true; } if (lastOfWeek[ix]) a.traded = false;
      if (!any) return 0;
      a.bal += dp; a.days++; a.best = std::max(a.best, dp); a.peak = std::max(a.peak, a.bal); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, p.start + p.lock_above_start));
      if (a.bal <= a.floor) { a.dead = true; return 0; }
      const double profit = a.bal - p.start;
      if (profit >= p.eval_target && a.days >= p.eval_min_days && (p.eval_consistency <= 0 || a.best <= p.eval_consistency * profit)) { a = fresh(kind); a.funded = true; }
      return 0; }
    const double cut = p.daily_loss, lock = p.start + p.lock_above_start; double dp = 0;
    auto trail = [&](double eq) { a.peak = std::max(a.peak, eq); a.floor = std::max(a.floor, std::min(a.peak - p.drawdown, lock)); };
    for (auto& t : tr) { if (p.intraday_trail && t.mfe > 0) trail(a.bal + dp + t.mfe); const double w = std::min(t.mae, t.pnl);
      if (a.bal + dp + w <= a.floor) { a.dead = true; return 0; } if (cut > 0 && dp + w <= -cut) { dp = -cut; break; } dp += t.pnl; if (p.intraday_trail) trail(a.bal + dp); }
    a.bal += dp; if (dp >= p.win_day_min && !tr.empty()) a.wins++; a.cyc += dp; a.best = std::max(a.best, dp); trail(a.bal);
    const double profit = a.bal - p.start; const bool cons = p.consistency <= 0 || (a.cyc > 0 && a.best <= p.consistency * a.cyc);
    if (a.wins >= p.win_days_needed && profit > 0 && cons && (a.payouts == 0 || a.cyc > 0)) {
      const double cap = p.caps[std::min<size_t>(a.payouts, p.caps.size() - 1)]; double amt = 0;
      if (p.payout_mult > 0) { if (profit > p.buffer && a.cyc > 0) amt = std::min({cap, p.payout_mult * a.cyc, profit - p.buffer}); if (amt < p.min_payout) amt = 0; }
      else if (profit >= p.min_profit) amt = std::min(cap, p.profit_share * profit);
      if (amt > 0) { a.bal -= amt; a.payouts++; a.wins = 0; a.best = 0; a.cyc = 0; a.peak = a.bal; return amt * 0.9; } }
    return 0; };

  // a strategy = units of the weak-day trade and of the trend trade per day (0 = none); both on one day = one combined trade
  struct Strat { const char* name; std::function<double(const D&)> uw, ut; };
  auto volSize = [](const D& z) { return z.pR >= z.thrR ? 0.0 : std::clamp(std::exp(z.medR - z.pR), 0.5, 2.0); };
  auto none = [](const D&) { return 0.0; };
  auto wkFlat = [](const D& z) { return z.weak && z.vix >= 20 ? 1.0 : 0.0; };
  auto wkM1 = [&](const D& z) { return z.weak && z.vix >= 20 ? volSize(z) : 0.0; };
  auto tdM2M1 = [&](const D& z) { return z.pT >= z.thrT ? volSize(z) : 0.0; };
  std::vector<Strat> SS = {
    {"U1 weak day, flat size", wkFlat, none},
    {"U1 weak day, sized by M1", wkM1, none},
    {"U2 trend trade, every day", none, [](const D&) { return 1.0; }},
    {"U2 trend trade, M2 top third", none, [](const D& z) { return z.pT >= z.thrT ? 1.0 : 0.0; }},
    {"U2 trend trade, M2 top third, sized by M1", none, tdM2M1},
    {"U3 BOTH (U1 M1 + U2 M2 M1)", wkM1, tdM2M1},
  };
  auto dayPnl = [&](const D& z, const Strat& st, double N, double& mae) { const double k = atrNow / z.atr, uw = st.uw(z) * N, ut = st.ut(z) * N; double p = 0; mae = 0;
    if (uw > 0) { p += uw * (2 * (z.wkPts * k - 1.5) - 1.82); mae += uw * 2 * (z.wkMae * k - 1.5); }
    if (ut > 0) { p += ut * (2 * (z.tdPts * k - 1.5) - 1.82); mae += ut * 2 * (z.tdMae * k - 1.5); }
    return (uw > 0 || ut > 0) ? p : NAN; };
  printf("\n  per MNQ unit, cost 1.5 pts + $1.82, 2014-26 (a day with both trades counts once):\n");
  for (auto& st : SS) { double sum = 0, gw = 0, gl = 0, eq = 0, pk = 0, dd = 0; int n = 0, w = 0; std::map<int, double> yr; std::map<int, double> mo;
    for (auto* z : TS) { double m; const double p = dayPnl(*z, st, 1, m); if (std::isnan(p)) continue;
      sum += p; n++; w += p > 0; (p > 0 ? gw : gl) += std::fabs(p); eq += p; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); yr[z->year] += p; mo[z->date / 100] += p; }
    int yp = 0; for (auto& [y, v] : yr) yp += v > 0; int mp = 0; for (auto& [m, v] : mo) mp += v > 0;
    printf("    %-44s %4d days (%.0f/yr, %.1f/month) · $%+5.0f a day · win %2.0f%% · PF %.2f · $%+6.0f a year · %d/%zu years > 0 · %d/%zu months with a trade > 0 · max DD $%.0f\n",
           st.name, n, n / 12.8, n / 153.6, sum / std::max(n, 1), 100.0 * w / std::max(n, 1), gl > 0 ? gw / gl : 0, sum / 12.8, yp, yr.size(), mp, mo.size(), dd); }
  printf("\n  Tradeify Select Daily 50K (12-month windows from every month 2014-01 … 2025-10; a dead account replaced the next day, $165 each):\n");
  for (auto& st : SS) for (int N : {1, 2, 3}) for (int K : {1, 3, 5}) { if (K > 1 && std::string(st.name).rfind("U3", 0) != 0) continue;
    std::vector<std::vector<payout::Trade>> DT(cal.size());
    for (auto* z : TS) { double m; const double p = dayPnl(*z, st, N, m); if (!std::isnan(p)) DT[ci[z->date]].push_back({p, m, 0}); }
    int pass = 0, fail = 0; double payF = 0; int nF = 0;
    if (K == 1) for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; s0 += 5) { Acct a = fresh(0); size_t i = s0;
      for (; i < cal.size() && !a.funded && !a.dead; ++i) step(0, a, DT[i], i);
      if (a.dead) { fail++; continue; } if (!a.funded) continue; pass++; double pay = 0; for (size_t j = i; j < cal.size() && !a.dead; ++j) pay += step(0, a, DT[j], j); payF += pay; nF++; }
    std::vector<double> nets, worst; int k5 = 0, pos = 0; std::vector<double> mon(12);
    std::vector<int> paidMonths;
    for (int y = 2014; y <= 2025; ++y) for (int m = 1; m <= 12; ++m) { const int d0 = y * 10000 + m * 100 + 1; if (d0 > 20251001) break;
      size_t s0 = std::lower_bound(cal.begin(), cal.end(), d0) - cal.begin(); std::vector<Acct> A(K); for (auto& a : A) a = fresh(0); double net = -165.0 * K, low = net; std::set<int> pm;
      for (size_t i = s0; i < cal.size() && cal[i] < d0 + 10000; ++i) for (int k = 0; k < K; ++k) { if (A[k].dead) { A[k] = fresh(0); net -= 165; low = std::min(low, net); continue; }
        const double pay = step(0, A[k], DT[i], i); if (pay > 0) pm.insert(cal[i] / 100); net += pay; low = std::min(low, net); }
      nets.push_back(net / 12); worst.push_back(low); k5 += net / 12 >= 500.0 * K; pos += net > 0; paidMonths.push_back((int)pm.size()); }
    std::vector<double> sn = nets; std::sort(sn.begin(), sn.end()); std::sort(worst.begin(), worst.end()); const size_t nw = nets.size();
    std::sort(paidMonths.begin(), paidMonths.end());
    printf("    %-44s %d MNQ %d acct: %s net a month mean $%+5.0f, median $%+5.0f, 10th $%+5.0f · >= $500/month/acct in %3.0f%% · > 0 in %3.0f%% · months with a payout (of 12) median %d · worst running loss $%.0f\n",
           st.name, N, K, K == 1 ? ("pass " + std::to_string((int)std::lround(100.0 * pass / std::max(1, pass + fail))) + "% · funded pays $" + std::to_string((int)(nF ? payF / nF : 0)) + " ·").c_str() : "",
           std::accumulate(nets.begin(), nets.end(), 0.0) / nw, sn[nw / 2], sn[nw / 10], 100.0 * k5 / nw, 100.0 * pos / nw, paidMonths[nw / 2], worst.front()); }
  printf("\n  FAST — buy an eval, buy a new one the day after each fails, from every trading day; first payout and 60-day money (Select Daily 50K):\n");
  for (auto& st : SS) { if (st.name[1] == '1') continue;
    for (int N : {2, 4, 6, 10, 20}) {
      std::vector<std::vector<payout::Trade>> DT(cal.size());
      for (auto* z : TS) { double m; const double p = dayPnl(*z, st, N, m); if (!std::isnan(p)) DT[ci[z->date]].push_back({p, m, 0}); }
      int starts = 0, by20 = 0, by40 = 0, by60 = 0; double fees = 0, pays = 0; std::vector<int> firstD; std::vector<double> net60;
      for (size_t s0 = 0; s0 < cal.size() && cal[s0] <= 20251031; ++s0) { starts++; Acct a = fresh(0); double f = 165, pay = 0; int first = -1;
        for (size_t i = s0; i < cal.size() && i < s0 + 60; ++i) { if (a.dead) { a = fresh(0); f += 165; continue; } const double x = step(0, a, DT[i], i); if (x > 0 && first < 0) first = (int)(i - s0 + 1); pay += x; }
        fees += f; pays += pay; net60.push_back(pay - f); if (first > 0) { firstD.push_back(first); by20 += first <= 20; by40 += first <= 40; by60++; } }
      std::sort(firstD.begin(), firstD.end()); std::sort(net60.begin(), net60.end());
      printf("    %-44s %2d MNQ: first payout within 20 days %3.0f%% · 40 %3.0f%% · 60 %3.0f%% (median day %d) · in 60 days: fees $%4.0f, payouts $%5.0f, net mean $%+5.0f, median $%+5.0f\n",
             st.name, N, 100.0 * by20 / starts, 100.0 * by40 / starts, 100.0 * by60 / starts, firstD.empty() ? -1 : firstD[firstD.size() / 2], fees / starts, pays / starts, (pays - fees) / starts, net60[net60.size() / 2]); } }
  return 0;
}
