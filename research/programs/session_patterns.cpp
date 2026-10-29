// 2026-10-20 session_patterns.cpp (founder: "can we get per session paterns?", after vol_trend_model.cpp). The same honest method on
// each session of the trading day — ASIA 18:00-02:00, LONDON 02:00-09:30, NY 09:30-16:00 ET — with patterns fixed before the run:
//  P1 first hour, go with it: at open + 60 min trade the first hour's direction, hold to the session end − 5 min
//  P2 straight first hour only (|close − open| >= 55 % of the first hour's range AND >= 0.25 of the session's 20-day average range)
//  P3 follow the previous session: at the open go with the previous session's direction, hold to the end − 5 min
//  P4 fade the previous session (P3 reversed — the same trades, so P3 + P4 = −2 × cost)
//  P5 follow the previous session only when it was big (range >= 1.25 × its 20-day average) and straight (>= 55 %)
//  P6 the TREND model at open + 60 (logistic, walk-forward, the session's own history): P1 on its top third only
//  R  the RANGE model at the open (ridge, walk-forward): R² out of sample vs the earlier years' average
//  Inputs of both models: previous session's move / range / straightness, the two before it, gap of this open vs the previous
//  RTH close, log VIX + its 1-day change, weekday; the trend model adds the first hour's move / range / straightness.
// Points scaled to today's ATR (× ATR now / ATR then), cost 1.5 pts + $1.82 per MNQ. Per pattern: trades a year, $ per MNQ a trade,
// win %, PF, years > 0, and the two halves 2010-17 / 2018-26 (models: 2014-26 only). NQ 2010-07 … 2026-10-16.
// Usage: session_patterns
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <numeric>
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
  build_days(s, fomc, vix);
  const char* SN[3] = {"ASIA 18:00-02:00", "LONDON 02:00-09:30", "NY 09:30-16:00"}; const int SA[3] = {0, 480, 930}, SB[3] = {480, 930, 1320};
  struct Sx { bool ok = false; double o, h, l, c, h1, l1, c1, x1, x0; };   // x1 = price at the end − 5 min, x0 = open-time entry
  struct Row { int date, year; double atr, vix; int dow; Sx ss[3]; };
  std::vector<Row> R; double atrNow = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; atrNow = d.atr;
    Row r{}; r.date = d.date; r.year = d.date / 10000; r.atr = d.atr; r.vix = d.vix; r.dow = d.dow;
    for (int k = 0; k < 3; ++k) { Sx z; z.h = z.h1 = -1e18; z.l = z.l1 = 1e18; z.o = z.c1 = z.x1 = NAN; int nb = 0;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < SA[k] || tm >= SB[k]) continue;
        if (std::isnan(z.o)) z.o = s.o[i]; nb++; z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i];
        if (tm < SA[k] + 60) { z.h1 = std::max(z.h1, (double)s.h[i]); z.l1 = std::min(z.l1, (double)s.l[i]); z.c1 = s.c[i]; }
        if (tm >= SB[k] - 5 && std::isnan(z.x1)) z.x1 = s.o[i]; }
      z.ok = nb >= (SB[k] - SA[k]) * 0.8 && !std::isnan(z.c1) && !std::isnan(z.x1); z.x0 = z.o; r.ss[k] = z; }
    R.push_back(r); }
  auto mnq = [&](const Row& r, double pts) { return 2 * (pts * atrNow / r.atr - 1.5) - 1.82; };
  printf("SESSION PATTERNS — NQ 2010-07 … 2026-10-16, $ per MNQ a trade at today's ATR (%.0f), cost 1.5 pts + $1.82\n", atrNow);
  for (int k = 0; k < 3; ++k) {
    // per day: this session, the previous session (k-1, or yesterday's NY for Asia) and the two before it
    struct E { const Row* r; Sx cur, p1, p2, p3; double avgR, avgP1R; std::vector<double> x0, x1; double lr, trend, pR = NAN, pT = NAN, thrT = NAN; };
    std::vector<E> ev; std::vector<double> hist, histP;
    for (size_t i = 1; i < R.size(); ++i) { auto prevS = [&](int back) -> Sx { int kk = k, ii = (int)i; for (int b = 0; b < back; ++b) { kk--; if (kk < 0) { kk = 2; ii--; } } return ii >= 0 ? R[ii].ss[kk] : Sx{}; };
      const Sx cur = R[i].ss[k], p1 = prevS(1), p2 = prevS(2), p3 = prevS(3);
      if (cur.ok && hist.size() >= 20 && histP.size() >= 20 && p1.ok && p2.ok && p3.ok) { E e{&R[i], cur, p1, p2, p3, 0, 0, {}, {}, 0, 0};
        e.avgR = std::accumulate(hist.end() - 20, hist.end(), 0.0) / 20; e.avgP1R = std::accumulate(histP.end() - 20, histP.end(), 0.0) / 20;
        const double A = R[i].atr; auto mv = [&](const Sx& z) { return (z.c - z.o) / A; }; auto rg = [&](const Sx& z) { return (z.h - z.l) / A; }; auto st = [&](const Sx& z) { return std::fabs(z.c - z.o) / std::max(z.h - z.l, 0.25); };
        const double gap = (cur.o - R[i - 1].ss[2].c) / A, lv = std::isnan(R[i].vix) ? 3.0 : std::log(R[i].vix);
        e.x0 = {mv(p1), rg(p1), st(p1), mv(p2), rg(p2), mv(p3), rg(p3), gap, lv, (double)(R[i].dow == 1), (double)(R[i].dow == 5)};
        const double dir1 = cur.c1 > cur.o ? 1 : -1; e.x1 = e.x0; e.x1.insert(e.x1.end(), {(cur.c1 - cur.o) / A * dir1, (cur.h1 - cur.l1) / A, std::fabs(cur.c1 - cur.o) / std::max(cur.h1 - cur.l1, 0.25), mv(p1) * dir1});
        e.lr = std::log(std::max((cur.h - cur.l) / A, 0.02)); e.trend = (cur.x1 - cur.c1) * dir1 > 0 ? 1 : 0;   // the first hour's direction held to the end
        ev.push_back(e); }
      if (cur.ok) hist.push_back(cur.h - cur.l); if (p1.ok) histP.push_back(p1.h - p1.l); }
    for (int Y = 2014; Y <= 2026; ++Y) { std::vector<std::vector<double>> X0, X1; std::vector<double> y0, y1; for (auto& e : ev) if (e.r->year < Y) { X0.push_back(e.x0); y0.push_back(e.lr); X1.push_back(e.x1); y1.push_back(e.trend); }
      const Lin m0 = fit(X0, y0, false, 1.0), m1 = fit(X1, y1, true, 1.0); std::vector<double> tr; for (auto& e : ev) if (e.r->year < Y) tr.push_back(pred(m1, e.x1, true)); std::sort(tr.begin(), tr.end());
      for (auto& e : ev) if (e.r->year == Y) { e.pR = pred(m0, e.x0, false); e.pT = pred(m1, e.x1, true); e.thrT = tr[tr.size() * 2 / 3]; } }
    printf("\n%s  (%zu sessions; average range %.0f%% of the RTH ATR)\n", SN[k], ev.size(), 100 * std::accumulate(ev.begin(), ev.end(), 0.0, [](double a, const E& e) { return a + std::exp(e.lr); }) / ev.size());
    { double m = 0; int n = 0; for (auto& e : ev) if (!std::isnan(e.pR)) { m += e.lr; n++; } m /= n; double ss = 0, sr = 0, sb = 0;
      for (auto& e : ev) if (!std::isnan(e.pR)) { ss += (e.lr - m) * (e.lr - m); sr += (e.lr - e.pR) * (e.lr - e.pR); double a = 0; int c = 0; for (auto& w : ev) if (w.r->year < e.r->year) { a += w.lr; c++; } sb += (e.lr - a / c) * (e.lr - a / c); }
      std::vector<double> p, y; for (auto& e : ev) if (!std::isnan(e.pT)) { p.push_back(e.pT); y.push_back(e.trend); }
      printf("  R  range model R² %.3f (baseline %.3f) · trend model AUC %.3f · the first hour holds to the end on %.0f%% of sessions\n", 1 - sr / ss, 1 - sb / ss, auc(p, y), 100 * std::accumulate(y.begin(), y.end(), 0.0) / y.size()); }
    auto show = [&](const char* nm, auto pick) {   // pick(e) -> signed points (NAN = no trade)
      double sum = 0, gw = 0, gl = 0; int n = 0, w = 0; std::map<int, double> yr; double h1 = 0, h2 = 0; int n1 = 0, n2 = 0; bool models = false;
      for (auto& e : ev) { const double pts = pick(e, models); if (std::isnan(pts)) continue; const double p = mnq(*e.r, pts); sum += p; n++; w += p > 0; (p > 0 ? gw : gl) += std::fabs(p); yr[e.r->year] += p; (e.r->year <= 2017 ? h1 : h2) += p; (e.r->year <= 2017 ? n1 : n2)++; }
      int yp = 0; for (auto& [y, v] : yr) yp += v > 0;
      printf("  %-58s %5d (%3.0f/yr) · $%+5.0f a trade · win %2.0f%% · PF %.2f · %2d/%2zu years > 0 · 2010-17 $%+5.0f (%d) · 2018-26 $%+5.0f (%d)\n", nm, n, n / (yr.empty() ? 1.0 : (double)yr.size()), n ? sum / n : 0, n ? 100.0 * w / n : 0, gl > 0 ? gw / gl : 0, yp, yr.size(), n1 ? h1 / n1 : 0, n1, n2 ? h2 / n2 : 0, n2); };
    auto first = [](const E& e) { return (e.cur.x1 - e.cur.c1) * (e.cur.c1 > e.cur.o ? 1 : -1); };
    show("P1 first hour, go with it", [&](const E& e, bool&) { return first(e); });
    show("P2 straight first hour (>= 55 % and >= 0.25 avg range)", [&](const E& e, bool&) { const double m = std::fabs(e.cur.c1 - e.cur.o); return m >= 0.55 * (e.cur.h1 - e.cur.l1) && m >= 0.25 * e.avgR ? first(e) : NAN; });
    show("P3 follow the previous session", [&](const E& e, bool&) { return (e.cur.x1 - e.cur.o) * (e.p1.c > e.p1.o ? 1 : -1); });
    show("P4 fade the previous session", [&](const E& e, bool&) { return (e.cur.x1 - e.cur.o) * (e.p1.c > e.p1.o ? -1 : 1); });
    show("P5 follow a big straight previous session", [&](const E& e, bool&) { return e.p1.h - e.p1.l >= 1.25 * e.avgP1R && std::fabs(e.p1.c - e.p1.o) >= 0.55 * (e.p1.h - e.p1.l) ? (e.cur.x1 - e.cur.o) * (e.p1.c > e.p1.o ? 1 : -1) : NAN; });
    show("P5' fade a big straight previous session", [&](const E& e, bool&) { return e.p1.h - e.p1.l >= 1.25 * e.avgP1R && std::fabs(e.p1.c - e.p1.o) >= 0.55 * (e.p1.h - e.p1.l) ? (e.cur.x1 - e.cur.o) * (e.p1.c > e.p1.o ? -1 : 1) : NAN; });
    show("P1 2014-26 (the models' years, for comparison)", [&](const E& e, bool&) { return std::isnan(e.pT) ? NAN : first(e); });
    show("P6 first hour on the trend model's top third (2014-26)", [&](const E& e, bool&) { return !std::isnan(e.pT) && e.pT >= e.thrT ? first(e) : NAN; });
  }
  return 0;
}
