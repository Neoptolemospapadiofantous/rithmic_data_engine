// 2026-10-22 flag_model.cpp (founder: "How a flag is identified can we create a model?" → "both"). Two models that need no hand labels, both judged
// against today's A-list (flag_tiny_tp's walk-forward 14-trait linear score, top third):
//   1 a stronger flag JUDGE on the same flags: 35 measurements per flag at its registration (flag_hhhl.cpp export: chart, time, pole size and
//     straightness, pullback depth, gap, last hour, 5m / 15m structure, first pullback, levels inside the pole, volume dry-up, order flow, day
//     position, VWAP distance, 5m opening-range direction, last 5 / 20 sessions, VIX, ATR trend, …)
//   2 the same judge on LOOSER candidates (flag_hhhl.cpp export FILE loose: pullback 10-60 %, pole >= 3 bars' average, above the open and VWAP only)
//     — the model, not the hand rules, decides which shapes are worth taking
// Models, per side (bull / bear), WALK-FORWARD by year: trained on every earlier year (2010 →), scored on the test year 2014-2026, never seeing it:
//   RIDGE  linear regression on standardised features (lambda 20)        GBM  100 depth-2 boosted trees (learning rate 0.05, leaves >= 60 flags)
// Target = the flag-mid · S2 trade's net R clipped to [−1.5, 5]. Selection = the test-year flags whose score beats the TRAINING scores' top third
// (as the A-list) or top 10 %. One position at a time across 3-5m, cost in R as exported. Usage: flag_model FEATURES.csv [label]. Read-only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>
enum { NX = 35 };
static const char* XN[NX] = {"chart minutes", "registration time", "pole / avg bar", "pole / ATR", "pullback depth", "gap / ATR", "last hour / ATR", "5m HH/HL",
  "15m HH/HL", "first pullback", "pullback number", "level inside pole", "straight 09:30-10:00", "sellers in flag", "volume dries up", "buyers in pole",
  "order flow present", "first leg of day", "pullback 38-62 %", "pullback > 62 %", "pole low at VWAP", "pole low at EMA20", "pole makes new high",
  "day position", "VWAP distance / ATR", "pole bars", "pole straightness", "5m opening-range break dir", "last 5 sessions (side)", "last 20 sessions (side)",
  "VIX", "ATR trend (20 sessions)", "side", "flag / pole volume", "day range / ATR"};
struct Fl { int date, year, side, em, xm, grade, hh5; double r, x[NX]; };
struct Tree { int f0; double t0; int f[2]; double t[2]; double v[4]; };
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: flag_model FEATURES.csv [label]\n"); return 1; }
  const std::string label = argc > 2 ? argv[2] : argv[1];
  std::vector<Fl> all;
  { std::ifstream f(argv[1]); if (!f) { perror(argv[1]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> v; v.reserve(56); const char* p = ln.c_str(); char* e;
      while (*p) { v.push_back(strtod(p, &e)); p = e; if (*p == ',') ++p; else break; }
      if (v.size() < 20 + NX || v[11] < 0.5) continue; Fl z; z.date = (int)v[0]; z.year = z.date / 10000; z.side = (int)v[1]; z.grade = (int)v[6]; z.hh5 = (int)v[7];
      z.em = (int)v[12]; z.xm = (int)v[13]; z.r = v[18]; for (int j = 0; j < NX; ++j) z.x[j] = std::isfinite(v[20 + j]) ? v[20 + j] : 0; all.push_back(z); } }
  std::sort(all.begin(), all.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  const size_t N = all.size(); auto yv = [](double r) { return std::max(-1.5, std::min(5.0, r)); };
  // walk-forward scores: sc[m][i] = model m's score for flag i, thr[m][q][i] = that test year's threshold (q 0 top third, 1 top 10 %)
  std::vector<double> sc[2]; for (auto& v : sc) v.assign(N, NAN); std::vector<double> th[2][2]; for (auto& a : th) for (auto& v : a) v.assign(N, NAN);
  std::vector<double> gain(NX, 0.0);
  for (int side = 1; side >= -1; side -= 2) for (int Y = 2014; Y <= 2026; ++Y) {
    std::vector<int> tr, te; for (size_t i = 0; i < N; ++i) if (all[i].side == side) { if (all[i].year < Y) tr.push_back((int)i); else if (all[i].year == Y) te.push_back((int)i); }
    if (tr.size() < 300 || te.empty()) continue; const size_t n = tr.size();
    double mu[NX], sd[NX]; for (int j = 0; j < NX; ++j) { double s1 = 0, s2 = 0; for (int i : tr) { s1 += all[i].x[j]; s2 += all[i].x[j] * all[i].x[j]; } mu[j] = s1 / n; sd[j] = std::sqrt(std::max(1e-12, s2 / n - mu[j] * mu[j])); }
    double ym = 0; for (int i : tr) ym += yv(all[i].r); ym /= n;
    // ── RIDGE ──
    std::vector<double> w(NX, 0.0);
    { std::vector<std::array<double, NX + 1>> A(NX); for (auto& row : A) row.fill(0);
      for (int i : tr) { double z[NX]; for (int j = 0; j < NX; ++j) z[j] = (all[i].x[j] - mu[j]) / sd[j]; const double yy = yv(all[i].r) - ym;
        for (int a = 0; a < NX; ++a) { for (int b = 0; b < NX; ++b) A[a][b] += z[a] * z[b]; A[a][NX] += z[a] * yy; } }
      for (int a = 0; a < NX; ++a) A[a][a] += 20;
      for (int c = 0; c < NX; ++c) { int pv = c; for (int r = c + 1; r < NX; ++r) if (std::fabs(A[r][c]) > std::fabs(A[pv][c])) pv = r; std::swap(A[c], A[pv]);
        for (int r = 0; r < NX; ++r) if (r != c && A[c][c] != 0) { const double f = A[r][c] / A[c][c]; for (int k = c; k <= NX; ++k) A[r][k] -= f * A[c][k]; } }
      for (int a = 0; a < NX; ++a) w[a] = A[a][a] != 0 ? A[a][NX] / A[a][a] : 0; }
    auto ridge = [&](const Fl& f) { double s = ym; for (int j = 0; j < NX; ++j) s += w[j] * (f.x[j] - mu[j]) / sd[j]; return s; };
    // ── GBM: depth-2 trees on 16 quantile bins per feature ──
    std::vector<std::array<double, 15>> cut(NX);
    for (int j = 0; j < NX; ++j) { std::vector<double> v; v.reserve(n); for (int i : tr) v.push_back(all[i].x[j]); std::sort(v.begin(), v.end()); for (int q = 0; q < 15; ++q) cut[j][q] = v[(size_t)((q + 1) * n / 16)]; }
    auto binOf = [&](int j, double x) { int b = 0; while (b < 15 && x > cut[j][b]) ++b; return b; };
    std::vector<std::array<unsigned char, NX>> B(n); for (size_t k = 0; k < n; ++k) for (int j = 0; j < NX; ++j) B[k][j] = (unsigned char)binOf(j, all[tr[k]].x[j]);
    std::vector<double> F(n, ym), res(n); std::vector<Tree> trees; const double lr = 0.05; const int minLeaf = 60;
    // best split of the rows in `idx` on residuals: feature, bin threshold (<= b goes left), gain
    auto bestSplit = [&](const std::vector<int>& idx, int& bf, int& bb, double& bg) { bf = -1; bg = 0; double tot = 0; for (int k : idx) tot += res[k]; const double nn = idx.size();
      for (int j = 0; j < NX; ++j) { double s[16] = {0}, c[16] = {0}; for (int k : idx) { s[B[k][j]] += res[k]; c[B[k][j]]++; } double sl = 0, cl = 0;
        for (int b = 0; b < 15; ++b) { sl += s[b]; cl += c[b]; const double cr = nn - cl; if (cl < minLeaf || cr < minLeaf) continue; const double sr = tot - sl;
          const double g = sl * sl / cl + sr * sr / cr - tot * tot / nn; if (g > bg) { bg = g; bf = j; bb = b; } } } };
    for (int it = 0; it < 100; ++it) { for (size_t k = 0; k < n; ++k) res[k] = yv(all[tr[k]].r) - F[k];
      std::vector<int> idx(n); for (size_t k = 0; k < n; ++k) idx[k] = (int)k; int f0, b0; double g0; bestSplit(idx, f0, b0, g0); if (f0 < 0) break; gain[f0] += Y == 2026 ? g0 : 0;
      std::vector<int> ch[2]; for (int k : idx) ch[B[k][f0] > b0].push_back(k); Tree t; t.f0 = f0; t.t0 = cut[f0][b0];
      for (int c = 0; c < 2; ++c) { int f1, b1; double g1; bestSplit(ch[c], f1, b1, g1); t.f[c] = f1; t.t[c] = f1 >= 0 ? cut[f1][b1] : 0; if (Y == 2026 && f1 >= 0) gain[f1] += g1;
        double s[2] = {0, 0}, cc[2] = {0, 0}; for (int k : ch[c]) { const int g = f1 >= 0 && B[k][f1] > b1; s[g] += res[k]; cc[g]++; }
        for (int g = 0; g < 2; ++g) t.v[c * 2 + g] = cc[g] ? lr * s[g] / cc[g] : 0; }
      for (int k : idx) { const Fl& f = all[tr[k]]; const int c = f.x[t.f0] > t.t0; const int g = t.f[c] >= 0 && f.x[t.f[c]] > t.t[c]; F[k] += t.v[c * 2 + g]; }
      trees.push_back(t); }
    auto gbm = [&](const Fl& f) { double s = ym; for (auto& t : trees) { const int c = f.x[t.f0] > t.t0; const int g = t.f[c] >= 0 && f.x[t.f[c]] > t.t[c]; s += t.v[c * 2 + g]; } return s; };
    // thresholds from the TRAINING scores, applied to the test year
    for (int m = 0; m < 2; ++m) { std::vector<double> ts; ts.reserve(n); for (int i : tr) ts.push_back(m ? gbm(all[i]) : ridge(all[i])); std::sort(ts.begin(), ts.end());
      const double t3 = ts[n * 2 / 3], t10 = ts[n * 9 / 10]; for (int i : te) { sc[m][i] = m ? gbm(all[i]) : ridge(all[i]); th[m][0][i] = t3; th[m][1][i] = t10; } } }
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, cum = 0, pk = 0, dd = 0, m[4] = {0}, c[4] = {0}; };
  auto per = [](int y) { return y <= 2017 ? 0 : y <= 2021 ? 1 : y <= 2024 ? 2 : 3; };
  auto run = [&](const char* name, std::function<bool(const Fl&, size_t)> sel) { St a; int lastD = 0, busy = -1; double yr[13] = {0}, yc[13] = {0};
    for (size_t i = 0; i < N; ++i) { const Fl& f = all[i]; if (f.year < 2014 || f.year > 2026 || !sel(f, i)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm;
      const double x = f.r; a.n++; a.s += x; a.w += x > 0; (x > 0 ? a.gw : a.gl) += std::fabs(x); a.cum += x; a.pk = std::max(a.pk, a.cum); a.dd = std::max(a.dd, a.pk - a.cum); a.m[per(f.year)] += x; a.c[per(f.year)]++;
      yr[f.year - 2014] += x; yc[f.year - 2014]++; }
    if (a.n < 20) { printf("    %-46s fewer than 20 trades (%.0f)\n", name, a.n); return; }
    int py = 0, ny = 0; for (int k = 0; k < 13; ++k) if (yc[k] >= 5) { ny++; py += yr[k] > 0; }
    printf("    %-46s", name); for (int e = 0; e < 4; ++e) printf(" %+6.2f (%4.0f) |", a.c[e] ? a.m[e] / a.c[e] : 0, a.c[e]);
    printf(" %5.1f/yr %+6.3fR win %4.1f%% PF %.2f · DD %5.1fR · years > 0 %d/%d\n", a.n / 12.8, a.s / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.dd, py, ny); };
  printf("Flag models, walk-forward — %s: %zu flags with a trade. Score year Y with a model trained on years < Y (from 2010), test 2014-2026.\n", label.c_str(), N);
  printf("Cells: 2014-17 | 2018-21 | 2022-24 | 2025-26 avg R (n) · trades a year, R, win %%, PF, worst drawdown R, years > 0. One position at a time across 3-5m.\n");
  for (int side : {0, 1, -1}) { printf("\n######## %s\n", side == 0 ? "BOTH SIDES" : side > 0 ? "BULL flags" : "BEAR flags");
    auto sd = [side](const Fl& f) { return side == 0 || f.side == side; };
    auto has = [&](size_t i) { return !std::isnan(sc[0][i]); };
    run("every flag", [&](const Fl& f, size_t i) { return sd(f) && has(i); });
    run("A-list (today's grade)", [&](const Fl& f, size_t i) { return sd(f) && has(i) && f.grade == 1; });
    run("A-list + 5m HH/HL", [&](const Fl& f, size_t i) { return sd(f) && has(i) && f.grade == 1 && f.hh5; });
    run("A-list + 5m HH/HL + with the 5m ORB break", [&](const Fl& f, size_t i) { return sd(f) && has(i) && f.grade == 1 && f.hh5 && f.x[27] > 0.5; });
    run("RIDGE top third", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[0][i] > th[0][0][i]; });
    run("GBM top third", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[1][i] > th[1][0][i]; });
    run("RIDGE top 10 %", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[0][i] > th[0][1][i]; });
    run("GBM top 10 %", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[1][i] > th[1][1][i]; });
    run("GBM top third + A-list", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[1][i] > th[1][0][i] && f.grade == 1; });
    run("GBM top third + with the 5m ORB break", [&](const Fl& f, size_t i) { return sd(f) && has(i) && sc[1][i] > th[1][0][i] && f.x[27] > 0.5; }); }
  std::vector<int> ord(NX); for (int j = 0; j < NX; ++j) ord[j] = j; std::sort(ord.begin(), ord.end(), [&](int a, int b) { return gain[a] > gain[b]; });
  double gt = 0; for (double g : gain) gt += g;
  printf("\nWhat the GBM leans on (share of split gain, the 2026 models trained on 2010-2025, both sides):");
  for (int k = 0; k < 10; ++k) printf(" %s %.0f%%%s", XN[ord[k]], gt > 0 ? 100 * gain[ord[k]] / gt : 0, k < 9 ? " ·" : "\n");
  return 0;
}
