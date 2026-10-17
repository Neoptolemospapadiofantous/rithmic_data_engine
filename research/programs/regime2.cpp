// 2026-10-17 regime measurement, per session, with a validated instrument.
//   regime2 DATA_DIR [label]
// Per trading day and session (Asia 18:00–03:00, London 03:00–08:00, New York 09:30–16:00 ET), from 5-minute closes:
//   AC1 = autocorrelation of consecutive 5-minute returns (> 0 trending, < 0 mean-reverting, 0 random)
//   VR  = variance ratio of 30-minute vs 5-minute returns (> 1 trending, < 1 mean-reverting, 1 random)
// Questions: (1) the session's character over all days; (2) does the first half of a session tell you the second half's
// regime (persistence); (3) does one session tell you the next (Asia → London → New York, yesterday's NY → today's NY).
// Run on real NQ, on a pure random walk (must show nothing) and on planted regimes (must show persistence).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;

struct SessStat { double ac = NAN, vr = NAN, ac1 = NAN, ac2 = NAN, first = NAN, rest = NAN; };
static double corr(const std::vector<double>& a, const std::vector<double>& b) {
  int n = a.size(); if (n < 30) return NAN; double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
  return sab / std::sqrt(saa * sbb); }
static double ac1(const std::vector<double>& r, size_t a, size_t b) {
  double num = 0, den = 0; for (size_t j = a; j < b; ++j) { den += r[j] * r[j]; if (j + 1 < b) num += r[j] * r[j + 1]; } return den > 0 ? num / den : NAN; }

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "data/edge_lab/cache", label = argc > 2 ? argv[2] : "real NQ";
  const int STEP = argc > 3 ? std::atoi(argv[3]) : 5;   // return interval, minutes
  std::set<int> fomc; paper::VixRegime vix;
  Series s; std::string err; if (!load_raw(dir, "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct Sess { const char* name; int a, b; };   // trading-day minutes (18:00 = 0)
  const Sess sess[3] = {{"Asia 18:00-03:00", hm_to_tday(18, 0), hm_to_tday(3, 0)}, {"London 03:00-08:00", hm_to_tday(3, 0), hm_to_tday(8, 0)},
                        {"New York 09:30-16:00", hm_to_tday(9, 30), hm_to_tday(16, 0)}};
  std::vector<std::array<SessStat, 3>> D; std::vector<int> dates;
  for (auto& d : s.days) {
    if (d.b1 - d.b0 < 1000 || d.rollwin || d.date > 20261016) continue;
    std::array<SessStat, 3> st;
    for (int k = 0; k < 3; ++k) {
      std::vector<double> px; double last = NAN; int next = sess[k].a;
      for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < sess[k].a || tm >= sess[k].b) continue;
        while (tm >= next) { px.push_back(std::isnan(last) ? s.o[i] : last); next += STEP; } last = s.c[i]; }
      if (!std::isnan(last)) px.push_back(last);
      if (px.size() < (size_t)std::max(25, 150 / STEP)) continue;
      std::vector<double> r; for (size_t j = 1; j < px.size(); ++j) r.push_back(std::log(px[j] / px[j - 1]));
      const size_t n = r.size(), h = n / 2, third = n / 3;
      double v1 = 0, v6 = 0; int m6 = 0; for (size_t j = 0; j < n; ++j) { v1 += r[j] * r[j]; if (j + 6 <= n) { double R = 0; for (int q = 0; q < 6; ++q) R += r[j + q]; v6 += R * R; ++m6; } }
      st[k].ac = ac1(r, 0, n); st[k].vr = v1 > 0 && m6 ? (v6 / m6) / (6 * v1 / n) : NAN;
      st[k].ac1 = ac1(r, 0, h); st[k].ac2 = ac1(r, h, n);
      double f = 0, g = 0; for (size_t j = 0; j < third; ++j) f += r[j]; for (size_t j = third; j < n; ++j) g += r[j];
      st[k].first = f; st[k].rest = g;
    }
    D.push_back(st); dates.push_back(d.date);
  }
  printf("\n### %s — %zu days — %d-minute returns\n", label.c_str(), D.size(), STEP);
  for (int k = 0; k < 3; ++k) {
    std::vector<double> ac, vr, a1, a2, fs, rs; double e[3] = {0, 0, 0}; int en[3] = {0, 0, 0};
    for (size_t i = 0; i < D.size(); ++i) { auto& x = D[i][k]; if (std::isnan(x.ac)) continue; ac.push_back(x.ac); vr.push_back(x.vr); a1.push_back(x.ac1); a2.push_back(x.ac2);
      fs.push_back(x.first); rs.push_back(x.rest); int er = dates[i] < 20190101 ? 0 : dates[i] < 20230101 ? 1 : 2; e[er] += x.ac; ++en[er]; }
    double m = 0, sd = 0; for (double v : ac) m += v; m /= ac.size(); for (double v : ac) sd += (v - m) * (v - m); sd = std::sqrt(sd / ac.size());
    double mv = 0; for (double v : vr) mv += v; mv /= vr.size();
    int agree = 0, nz = 0, cont = 0, nf = 0; for (size_t i = 0; i < a1.size(); ++i) { if (std::isnan(a1[i]) || std::isnan(a2[i])) continue; ++nz; agree += (a1[i] > 0) == (a2[i] > 0); }
    for (size_t i = 0; i < fs.size(); ++i) if (fs[i] != 0 && rs[i] != 0) { ++nf; cont += (fs[i] > 0) == (rs[i] > 0); }
    printf("  %-22s character: AC1 %+.3f (t %+.1f; eras %+.3f %+.3f %+.3f)  VR %.3f | first half's regime → second half: corr %+.3f, same sign %.1f%% | first-third direction continues %.1f%%\n",
           sess[k].name, m, m / (sd / std::sqrt((double)ac.size())), e[0] / en[0], e[1] / en[1], e[2] / en[2], mv, corr(a1, a2), 100.0 * agree / nz, 100.0 * cont / nf);
  }
  auto cross = [&](const char* nm, int ka, int kb, int lag) {
    std::vector<double> a, b;
    for (size_t i = lag; i < D.size(); ++i) { double x = D[i - lag][ka].ac, y = D[i][kb].ac; if (std::isnan(x) || std::isnan(y)) continue; a.push_back(x); b.push_back(y); }
    int agree = 0; for (size_t i = 0; i < a.size(); ++i) agree += (a[i] > 0) == (b[i] > 0);
    printf("  %-40s regime corr %+.3f, same sign %.1f%% (n %zu)\n", nm, corr(a, b), 100.0 * agree / a.size(), a.size()); };
  cross("Asia → London (same day)", 0, 1, 0);
  cross("London → New York (same day)", 1, 2, 0);
  cross("Asia → New York (same day)", 0, 2, 0);
  cross("yesterday's New York → today's New York", 2, 2, 1);
  // (a) is the persistence only volatility? first-half vs second-half regime corr inside terciles of the first half's range
  // (b) is it useful? continuation of the first third's direction, and the rest-of-session move, by tercile of the
  //     first half's AC1 (most mean-reverting tape → most trending tape)
  for (int k = 0; k < 3; ++k) {
    struct R { double a1, a2, vol, first, rest; };
    std::vector<R> v;
    for (auto& d : D) { auto& x = d[k]; if (std::isnan(x.ac1) || std::isnan(x.ac2) || x.first == 0) continue; v.push_back({x.ac1, x.ac2, std::fabs(x.first) + 1e-12, x.first, x.rest}); }
    if (v.size() < 300) continue;
    auto terc = [&](auto key) { std::vector<double> q; for (auto& r : v) q.push_back(key(r)); std::sort(q.begin(), q.end()); return std::make_pair(q[q.size() / 3], q[2 * q.size() / 3]); };
    // volatility proxy: |first-third move| is noisy; use the first half's AC1 denominator would need raw returns — use |first|+|rest|
    auto vt = terc([](const R& r) { return std::fabs(r.first) + std::fabs(r.rest); });
    printf("  %s:\n    persistence inside volatility terciles (low/mid/high):", sess[k].name);
    for (int t = 0; t < 3; ++t) { std::vector<double> a, b; for (auto& r : v) { double z = std::fabs(r.first) + std::fabs(r.rest); int tt = z < vt.first ? 0 : z < vt.second ? 1 : 2; if (tt == t) { a.push_back(r.a1); b.push_back(r.a2); } }
      printf(" corr %+.3f", corr(a, b)); }
    auto at = terc([](const R& r) { return r.a1; });
    printf("\n    by the first half's tape (AC1 tercile):");
    const char* nm[] = {"mean-reverting", "middle", "trending"};
    for (int t = 0; t < 3; ++t) { int n = 0, c = 0; double mv = 0; for (auto& r : v) { int tt = r.a1 < at.first ? 0 : r.a1 < at.second ? 1 : 2; if (tt != t) continue; ++n; c += (r.first > 0) == (r.rest > 0); mv += (r.first > 0 ? 1 : -1) * r.rest * 1e4; }
      printf("  %s: first-third direction continues %.1f%%, rest %+.1f bp", nm[t], 100.0 * c / n, mv / n); }
    printf("\n");
  }
}
