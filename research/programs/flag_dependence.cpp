// 2026-10-22 flag_dependence.cpp (founder: "do more tests on the flags, correlations, to get a better output"). Runs
// research/preregistered/2026-10-22_flag_dependence.md exactly as written before the run: how the BOOK2 flag trades depend on
// each other — D1 day clustering (effective N), D2 NQ–ES daily dependence, D3 within-day sequence (permutation), D4 daily P&L
// memory (ACF, Ljung-Box, runs, variance ratio), D5 12-month drawdown under iid vs block bootstrap, D6 stability in time,
// D7 regime-conditional dependence (VIX / ATR terciles). BOOK2 = flag_prop_ev's rule: ok, tf >= 4, pole mult >= 3, pullback
// 5–38 %, grade A or B, 2 open per market, 2014-01-02 → 2026-10-15; net R = the file's `r` column (pts / |entry − stop|, where pts is
// ALREADY net of the 1.5 NQ / 0.5 ES cost — flag_prop_ev adds the cost back before rescaling to today's ATR, so subtracting it
// again double-counts; a first version did and read +0.058R on NQ instead of +0.181R). D3 is reported twice: every consecutive
// pair, and the CAUSAL pairs only (trade k−1 exited before trade k entered) — 80 % of same-day pairs overlap in time.
// Usage: flag_dependence W15_NQ.csv W15_ES.csv [cache_dir] [regime_dir]   (read-only; defaults data/edge_lab/cache, data/regime)
#include "research/meta.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <vector>
using namespace lab;
struct T { int date, mk, side, em, xm, year; double r; };
static double mean(const std::vector<double>& v) { return v.empty() ? NAN : std::accumulate(v.begin(), v.end(), 0.0) / v.size(); }
static double sd(const std::vector<double>& v) { if (v.size() < 2) return NAN; double m = mean(v), s = 0; for (double x : v) s += (x - m) * (x - m); return std::sqrt(s / (v.size() - 1)); }
static double pearson(const std::vector<double>& a, const std::vector<double>& b) {
  if (a.size() != b.size() || a.size() < 3) return NAN; double ma = mean(a), mb = mean(b), sab = 0, saa = 0, sbb = 0;
  for (size_t i = 0; i < a.size(); ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
  return (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : NAN; }
static std::vector<double> ranks(const std::vector<double>& v) {
  std::vector<int> idx(v.size()); std::iota(idx.begin(), idx.end(), 0); std::sort(idx.begin(), idx.end(), [&](int i, int j) { return v[i] < v[j]; });
  std::vector<double> r(v.size()); for (size_t i = 0; i < idx.size();) { size_t j = i; while (j + 1 < idx.size() && v[idx[j + 1]] == v[idx[i]]) ++j; const double rk = (i + j) / 2.0 + 1; for (size_t k = i; k <= j; ++k) r[idx[k]] = rk; i = j + 1; } return r; }
static double spearman(const std::vector<double>& a, const std::vector<double>& b) { return pearson(ranks(a), ranks(b)); }
// two-sided p of a correlation on n points (t approximation), normal tail, chi-square upper tail
static double norm_sf(double z) { return 0.5 * std::erfc(z / std::sqrt(2.0)); }
static double gammq(double a, double x) {   // regularized upper incomplete gamma Q(a, x) (Numerical Recipes gser / gcf)
  if (x <= 0) return 1; if (x < a + 1) { double ap = a, sum = 1 / a, del = sum; for (int n = 0; n < 500; ++n) { ap += 1; del *= x / ap; sum += del; if (std::fabs(del) < std::fabs(sum) * 1e-14) break; } return 1 - sum * std::exp(-x + a * std::log(x) - std::lgamma(a)); }
  double b = x + 1 - a, c = 1e300, d = 1 / b, h = d; for (int i = 1; i < 500; ++i) { const double an = -i * (i - a); b += 2; d = an * d + b; if (std::fabs(d) < 1e-300) d = 1e-300; c = b + an / c; if (std::fabs(c) < 1e-300) c = 1e-300; d = 1 / d; const double del = d * c; h *= del; if (std::fabs(del - 1) < 1e-14) break; } return std::exp(-x + a * std::log(x) - std::lgamma(a)) * h; }
static double chi2_sf(double x, int k) { return gammq(k / 2.0, x / 2.0); }
static double corr_p(double r, size_t n) { if (std::isnan(r) || n < 4) return NAN; const double t = r * std::sqrt((n - 2) / std::max(1e-12, 1 - r * r)); return 2 * norm_sf(std::fabs(t)); }   // normal approx (n is large here)
static double binom_sf(int k, int n) { double p = 0; for (int i = k; i <= n; ++i) p += std::exp(std::lgamma(n + 1) - std::lgamma(i + 1) - std::lgamma(n - i + 1)) * std::pow(0.5, n); return p; }
static double maxdd(const std::vector<double>& path) { double peak = 0, cum = 0, dd = 0; for (double x : path) { cum += x; peak = std::max(peak, cum); dd = std::max(dd, peak - cum); } return dd; }
static std::vector<double> holm(std::vector<double> p) {   // Holm step-down adjusted p-values (NaN kept)
  std::vector<int> idx; for (size_t i = 0; i < p.size(); ++i) if (!std::isnan(p[i])) idx.push_back(i);
  std::sort(idx.begin(), idx.end(), [&](int a, int b) { return p[a] < p[b]; }); std::vector<double> adj = p; double run = 0; const int m = idx.size();
  for (int k = 0; k < m; ++k) { run = std::max(run, std::min(1.0, p[idx[k]] * (m - k))); adj[idx[k]] = run; } return adj; }

int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_dependence W15_NQ.csv W15_ES.csv [cache_dir] [regime_dir]\n"); return 1; }
  const std::string cache = argc > 3 ? argv[3] : "data/edge_lab/cache", regdir = argc > 4 ? argv[4] : "data/regime";
  std::vector<T> all;
  for (int mk = 0; mk < 2; ++mk) {
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    std::vector<T> cand;
    while (std::getline(f, ln)) { std::vector<double> a; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) a.push_back(strtod(t.c_str(), nullptr)); if (a.size() < 71) continue;
      const int date = (int)a[0]; if (date < 20140102 || date > 20261015) continue;
      const double mult = a[22], pull = a[24] * 100; const int g = (int)a[6];
      if (!(a[11] > 0.5 && (int)a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2))) continue;
      const double risk = std::fabs(a[14] - a[15]); if (!(risk > 0)) continue;
      cand.push_back({date, mk, (int)a[1], (int)a[12], (int)a[13], date / 10000, a[18]}); }
    // 2 open per market: take a trade when fewer than 2 taken trades of that day are still open at its entry minute
    std::stable_sort(cand.begin(), cand.end(), [](const T& x, const T& y) { return x.date != y.date ? x.date < y.date : x.em < y.em; });
    std::vector<T> taken; for (auto& t : cand) { int open = 0; for (auto it = taken.rbegin(); it != taken.rend() && it->date == t.date; ++it) if (it->xm > t.em) ++open; if (open < 2) taken.push_back(t); }
    printf("%s: %zu qualifying, %zu taken under 2-open\n", mk ? "ES" : "NQ", cand.size(), taken.size()); all.insert(all.end(), taken.begin(), taken.end()); }
  std::vector<double> holm_p; std::vector<std::string> holm_name;
  // ── D1 clustering ─────────────────────────────────────────────────────────────────────────────────────────────────
  printf("\n== D1 day clustering (mean net R, naive vs day-clustered SE)\n%-8s %6s %8s %9s %9s %7s %7s %8s %6s\n", "set", "n", "meanR", "SE_naive", "SE_clust", "t_naive", "t_clust", "design", "effN");
  auto d1 = [&](const char* name, auto pred) { std::vector<double> r; std::map<int, std::pair<double, int>> byday; for (auto& t : all) if (pred(t)) { r.push_back(t.r); auto& d = byday[t.date]; d.first += t.r; d.second++; }
    if (r.size() < 10) return; const double m = mean(r), sen = sd(r) / std::sqrt((double)r.size()); double s = 0; for (auto& [d, v] : byday) s += (v.first - v.second * m) * (v.first - v.second * m); const double sec = std::sqrt(s) / r.size(); const double de = (sec / sen) * (sec / sen);
    printf("%-8s %6zu %8.4f %9.4f %9.4f %7.2f %7.2f %8.2f %6.0f\n", name, r.size(), m, sen, sec, m / sen, m / sec, de, r.size() / de); };
  d1("NQ", [](const T& t) { return t.mk == 0; }); d1("ES", [](const T& t) { return t.mk == 1; }); d1("pooled", [](const T&) { return true; });
  d1("NQ14-19", [](const T& t) { return t.mk == 0 && t.year <= 2019; }); d1("NQ20-26", [](const T& t) { return t.mk == 0 && t.year >= 2020; });
  // daily series
  std::map<int, double> dnq, des; std::map<int, std::vector<double>> trades_by_day[2];
  for (auto& t : all) { (t.mk ? des : dnq)[t.date] += t.r; trades_by_day[t.mk][t.date].push_back(t.r); }
  std::set<int> nydays; { Series s; std::string err; if (load_raw(cache, "NQ", s, err)) { std::set<int> fomc; paper::VixRegime vix; vix.load(regdir); build_days(s, fomc, vix); for (auto& d : s.days) if (d.ok && d.date >= 20140102 && d.date <= 20261015) nydays.insert(d.date); } else fprintf(stderr, "NQ bars: %s (falling back to traded days)\n", err.c_str()); }
  if (nydays.empty()) { for (auto& [d, v] : dnq) nydays.insert(d); for (auto& [d, v] : des) nydays.insert(d); }
  // ── D2 NQ vs ES ──────────────────────────────────────────────────────────────────────────────────────────────────
  { std::vector<double> a, b, a0, b0; int nqneg = 0, both = 0, esneg_all = 0, nall = 0; for (int d : nydays) { const double x = dnq.count(d) ? dnq[d] : 0, y = des.count(d) ? des[d] : 0; a0.push_back(x); b0.push_back(y); if (dnq.count(d) && des.count(d)) { a.push_back(x); b.push_back(y); nall++; if (y < 0) esneg_all++; if (x < 0) { nqneg++; if (y < 0) both++; } } }
    int same = 0, pairs = 0; for (auto& [d, v] : trades_by_day[0]) if (trades_by_day[1].count(d)) { for (auto& t : all) if (t.date == d && t.mk == 0) for (auto& u : all) if (u.date == d && u.mk == 1) { pairs++; if (t.side == u.side) same++; } }
    printf("\n== D2 NQ vs ES daily book P&L\n days both traded: %zu  Pearson %.3f (p %.3g)  Spearman %.3f | all NY days (%zu, 0 when flat): Pearson %.3f  Spearman %.3f\n lower tail: P(ES<0 | NQ<0) = %.3f vs P(ES<0) = %.3f (n=%d)  | same-day NQ×ES trade pairs on the same side: %.1f%% of %d\n",
      a.size(), pearson(a, b), corr_p(pearson(a, b), a.size()), spearman(a, b), a0.size(), pearson(a0, b0), spearman(a0, b0), nqneg ? (double)both / nqneg : NAN, nall ? (double)esneg_all / nall : NAN, nall, pairs ? 100.0 * same / pairs : NAN, pairs); }
  // ── D3 within-day sequence ───────────────────────────────────────────────────────────────────────────────────────
  printf("\n== D3 trade k given sign of trade k-1 (same market, same day) — ALL consecutive pairs vs CAUSAL pairs (k-1 exited before k entered)\n");
  { std::map<int, std::vector<T>> byday[2]; for (auto& t : all) byday[t.mk][t.date].push_back(t); size_t pairs = 0, overlap = 0;
    for (int mk = 0; mk < 2; ++mk) for (auto& [d, v] : byday[mk]) { std::sort(v.begin(), v.end(), [](const T& x, const T& y) { return x.em < y.em; }); for (size_t k = 1; k < v.size(); ++k) { pairs++; if (v[k - 1].xm > v[k].em) overlap++; } }
    printf(" same-day consecutive pairs %zu, overlapping in time %zu (%.0f%%)\n", pairs, overlap, 100.0 * overlap / pairs);
    for (int causal = 0; causal < 2; ++causal) for (int mk = 0; mk < 2; ++mk) for (int half = 0; half < 3; ++half) {
      std::vector<std::vector<std::pair<double, double>>> days;   // per day: (prev R, this R) pairs that qualify
      for (auto& [d, v] : byday[mk]) { const int y = d / 10000; if (half == 1 && y > 2019) continue; if (half == 2 && y < 2020) continue; std::vector<std::pair<double, double>> pr;
        for (size_t k = 1; k < v.size(); ++k) if (!causal || v[k - 1].xm <= v[k].em) pr.push_back({v[k - 1].r, v[k].r}); if (!pr.empty()) days.push_back(pr); }
      auto stat = [&](const std::vector<std::vector<std::pair<double, double>>>& D, double& ml, double& mw, int& nl, int& nw) { double sl = 0, sw = 0; nl = nw = 0; for (auto& v : D) for (auto& [a, b] : v) { if (a < 0) { sl += b; nl++; } else { sw += b; nw++; } } ml = nl ? sl / nl : NAN; mw = nw ? sw / nw : NAN; return ml - mw; };
      double ml, mw; int nl, nw; const double obs = stat(days, ml, mw, nl, nw); std::mt19937 g(7); int ge = 0; const int NP = 2000;
      // permutation null: the previous trade's sign carries no information about the next R. ALL pairs: shuffle within each day
      // (overlapping pairs share the day's move, so the day is the exchangeable unit). CAUSAL pairs: most days hold ONE such pair and a
      // within-day shuffle cannot move it (a first version read p = 1.000 on NQ for that reason), so the labels are pooled across pairs.
      auto D = days; std::vector<double> pool; for (auto& v : days) for (auto& q : v) pool.push_back(q.first);
      for (int p = 0; p < NP; ++p) { if (causal) { std::shuffle(pool.begin(), pool.end(), g); size_t i = 0; for (auto& v : D) for (auto& q : v) q.first = pool[i++]; }
        else for (auto& v : D) { std::vector<double> prev; for (auto& q : v) prev.push_back(q.first); std::shuffle(prev.begin(), prev.end(), g); for (size_t i = 0; i < v.size(); ++i) v[i].first = prev[i]; }
        double a, b; int c, e; if (std::fabs(stat(D, a, b, c, e)) >= std::fabs(obs) - 1e-12) ge++; }
      const double pv = (ge + 1.0) / (NP + 1); printf(" %-6s %s %-8s after loss: mean R %+.4f (n %4d)  after win: %+.4f (n %4d)  diff %+.4f  perm p %.3f\n", causal ? "CAUSAL" : "ALL", mk ? "ES" : "NQ", half == 0 ? "all" : half == 1 ? "2014-19" : "2020-26", ml, nl, mw, nw, obs, pv);
      if (half == 0 && causal) { holm_p.push_back(pv); holm_name.push_back(std::string("D3 causal ") + (mk ? "ES" : "NQ")); } } }
  // ── D4 daily memory ──────────────────────────────────────────────────────────────────────────────────────────────
  std::vector<double> daily; for (int d : nydays) daily.push_back((dnq.count(d) ? dnq[d] : 0) + (des.count(d) ? des[d] : 0));
  { const size_t n = daily.size(); const double m = mean(daily); double v0 = 0; for (double x : daily) v0 += (x - m) * (x - m); double Q = 0; printf("\n== D4 pooled daily P&L memory (%zu NY days, 0 when flat)\n ACF:", n);
    for (int L = 1; L <= 5; ++L) { double c = 0; for (size_t i = L; i < n; ++i) c += (daily[i] - m) * (daily[i - L] - m); const double rho = c / v0; Q += rho * rho / (n - L); printf(" lag%d %+.3f", L, rho); } Q *= n * (n + 2.0); const double lbp = chi2_sf(Q, 5);
    int pos = 0, neg = 0, runs = 0, prev = 0; for (double x : daily) { if (x == 0) continue; const int s = x > 0 ? 1 : -1; if (s != prev) runs++; prev = s; (s > 0 ? pos : neg)++; }
    const double N = pos + neg, er = 1 + 2.0 * pos * neg / N, vr = 2.0 * pos * neg * (2.0 * pos * neg - N) / (N * N * (N - 1)), zr = (runs - er) / std::sqrt(vr);
    std::vector<double> s5; for (size_t i = 0; i + 5 <= n; i += 5) s5.push_back(std::accumulate(daily.begin() + i, daily.begin() + i + 5, 0.0)); const double VR5 = (sd(s5) * sd(s5)) / (5 * (v0 / (n - 1)));
    printf("\n Ljung-Box Q(5) = %.2f  p = %.4f | runs test (non-zero days %d: %d runs, expected %.0f) z = %+.2f p = %.3f | variance ratio VR(5) = %.3f (1 = no memory, >1 trending, <1 mean-reverting)\n", Q, lbp, (int)N, runs, er, zr, 2 * norm_sf(std::fabs(zr)), VR5);
    holm_p.push_back(lbp); holm_name.push_back("D4 Ljung-Box"); }
  // ── D5 drawdown under dependence ─────────────────────────────────────────────────────────────────────────────────
  { std::mt19937 g(11); const int NP = 2000, H = 252; const size_t n = daily.size(); printf("\n== D5 12-month max drawdown, R units at 1 unit a trade (%d paths)\n%-14s %8s %8s %8s\n", NP, "resampling", "median", "p95", "mean");
    for (int L : {1, 5, 20}) { std::vector<double> dds; for (int p = 0; p < NP; ++p) { std::vector<double> path; while ((int)path.size() < H) { const size_t s0 = g() % n; for (int k = 0; k < L && (int)path.size() < H; ++k) path.push_back(daily[(s0 + k) % n]); } dds.push_back(maxdd(path)); }
      std::sort(dds.begin(), dds.end()); printf("%-14s %8.2f %8.2f %8.2f\n", L == 1 ? "iid days" : L == 5 ? "blocks of 5" : "blocks of 20", dds[NP / 2], dds[(int)(NP * 0.95)], mean(dds)); } }
  // ── D6 stability ─────────────────────────────────────────────────────────────────────────────────────────────────
  { printf("\n== D6 stability in time\n year   NQ n  meanR   ES n  meanR\n"); int posy = 0, ny = 0; for (int y = 2014; y <= 2026; ++y) { std::vector<double> a, b; for (auto& t : all) if (t.year == y) (t.mk ? b : a).push_back(t.r); const double py = mean(a) * a.size() + mean(b) * b.size(); printf(" %d %5zu %+.4f %5zu %+.4f %s\n", y, a.size(), mean(a), b.size(), mean(b), py > 0 ? "+" : "-"); ny++; if (py > 0) posy++; }
    printf(" positive years %d of %d, binomial P(>= %d | p=0.5) = %.3f\n", posy, ny, posy, binom_sf(posy, ny));
    std::vector<T> ord = all; std::stable_sort(ord.begin(), ord.end(), [](const T& x, const T& y) { return x.date != y.date ? x.date < y.date : x.em < y.em; }); std::vector<double> idx, rr; for (size_t i = 0; i < ord.size(); ++i) { idx.push_back(i); rr.push_back(ord[i].r); }
    const double rho = spearman(idx, rr), rp = corr_p(rho, idx.size()); printf(" Spearman(net R, trade order) = %+.4f  p = %.3f\n", rho, rp); holm_p.push_back(rp); holm_name.push_back("D6 trend");
    auto clus = [&](auto pred, double& m, double& se, size_t& n) { std::vector<double> r; std::map<int, std::pair<double, int>> byday; for (auto& t : all) if (pred(t)) { r.push_back(t.r); auto& d = byday[t.date]; d.first += t.r; d.second++; } m = mean(r); double s = 0; for (auto& [d, v] : byday) s += (v.first - v.second * m) * (v.first - v.second * m); se = std::sqrt(s) / r.size(); n = r.size(); };
    double m1, s1, m2, s2; size_t n1, n2; clus([](const T& t) { return t.year <= 2019; }, m1, s1, n1); clus([](const T& t) { return t.year >= 2020; }, m2, s2, n2); const double z = (m2 - m1) / std::sqrt(s1 * s1 + s2 * s2), sp = 2 * norm_sf(std::fabs(z));
    printf(" 2014-19 mean R %+.4f (n %zu)  2020-26 %+.4f (n %zu)  difference %+.4f  z (clustered) %+.2f  p = %.3f\n", m1, n1, m2, n2, m2 - m1, z, sp); holm_p.push_back(sp); holm_name.push_back("D6 split"); }
  // ── D7 regime-conditional ─────────────────────────────────────────────────────────────────────────────────────────
  { std::map<int, std::pair<double, double>> ctx; Series s; std::string err; if (load_raw(cache, "NQ", s, err)) { std::set<int> fomc; paper::VixRegime vix; vix.load(regdir); build_days(s, fomc, vix); for (auto& d : s.days) if (d.ok) ctx[d.date] = {d.vix, d.atr}; }
    for (int which = 0; which < 2; ++which) { std::vector<double> vals; std::set<int> tdays; for (auto& [d, v] : dnq) tdays.insert(d); for (auto& [d, v] : des) tdays.insert(d); for (int d : tdays) if (ctx.count(d)) { const double v = which ? ctx[d].second : ctx[d].first; if (!std::isnan(v)) vals.push_back(v); }
      if (vals.size() < 30) { printf("\n== D7 %s: no context (%zu days)\n", which ? "ATR" : "VIX", vals.size()); continue; } std::sort(vals.begin(), vals.end()); const double q1 = vals[vals.size() / 3], q2 = vals[2 * vals.size() / 3];
      printf("\n== D7 by %s tercile (prior close; cuts %.1f / %.1f)\n %-7s %7s %7s %8s %8s %8s %8s\n", which ? "ATR14" : "VIX", q1, q2, "tercile", "days", "trd/day", "NQ meanR", "ES meanR", "corr", "P(both<0)");
      for (int q = 0; q < 3; ++q) { std::vector<double> a, b, an, bn; int days = 0, trades = 0, both = 0, nqneg = 0; for (int d : tdays) { if (!ctx.count(d)) continue; const double v = which ? ctx[d].second : ctx[d].first; if (std::isnan(v)) continue; const int t = v < q1 ? 0 : v < q2 ? 1 : 2; if (t != q) continue; days++;
          for (double r : trades_by_day[0][d]) { an.push_back(r); trades++; } for (double r : trades_by_day[1][d]) { bn.push_back(r); trades++; } if (dnq.count(d) && des.count(d)) { a.push_back(dnq[d]); b.push_back(des[d]); if (dnq[d] < 0) { nqneg++; if (des[d] < 0) both++; } } }
        printf(" %-7s %7d %7.2f %+8.4f %+8.4f %8.3f %8.3f\n", q == 0 ? "low" : q == 1 ? "mid" : "high", days, days ? (double)trades / days : NAN, mean(an), mean(bn), pearson(a, b), nqneg ? (double)both / nqneg : NAN); } } }
  // ── Holm ──────────────────────────────────────────────────────────────────────────────────────────────────────────
  { auto adj = holm(holm_p); printf("\n== judged p-values (raw → Holm-adjusted over %zu tests)\n", holm_p.size()); for (size_t i = 0; i < holm_p.size(); ++i) printf(" %-16s %.4f → %.4f %s\n", holm_name[i].c_str(), holm_p[i], adj[i], adj[i] < 0.05 ? "SIGNIFICANT" : ""); }
  return 0;
}
