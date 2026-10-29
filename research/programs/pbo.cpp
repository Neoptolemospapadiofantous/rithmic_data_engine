// 2026-10-22 pbo.cpp (preregistered/2026-10-22_external_ideas.md E1): probability of backtest overfitting by combinatorially symmetric
// cross-validation (Bailey, Borwein, López de Prado, Zhu) and the deflated Sharpe ratio (Bailey & López de Prado) over the daily P&L series
// of every account variant tried on 2026-10-22 (flag_prop_ev mtm FPEV_MTM_DUMP files: date,pnl,worst).
// Usage: pbo DAILY_DIR CANDIDATE_NAME [S=16]. Pure computation, reads only DAILY_DIR/*.csv.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
static double invNorm(double p) {   // Acklam's rational approximation
  static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
  static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01};
  static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
  static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00};
  const double pl = 0.02425; double q, r;
  if (p < pl) { q = std::sqrt(-2 * std::log(p)); return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1); }
  if (p > 1 - pl) { q = std::sqrt(-2 * std::log(1 - p)); return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1); }
  q = p - 0.5; r = q * q; return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1); }
static double normCdf(double x) { return 0.5 * std::erfc(-x / std::sqrt(2.0)); }
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: pbo DAILY_DIR CANDIDATE_NAME [S]\n"); return 1; }
  const std::string dir = argv[1], cand = argv[2]; const int S = argc > 3 ? atoi(argv[3]) : 16;
  std::vector<std::string> names; std::vector<std::map<int, double>> ser; std::set<int> dates;
  DIR* dp = opendir(dir.c_str()); if (!dp) { perror(dir.c_str()); return 1; }
  std::vector<std::string> files; while (dirent* e = readdir(dp)) { std::string f = e->d_name; if (f.size() > 4 && f.substr(f.size() - 4) == ".csv") files.push_back(f); } closedir(dp);
  std::sort(files.begin(), files.end());
  for (auto& f : files) { std::ifstream in(dir + "/" + f); std::string ln; std::getline(in, ln); std::map<int, double> m;
    while (std::getline(in, ln)) { int d; double p; if (sscanf(ln.c_str(), "%d,%lf", &d, &p) == 2 && d >= 20140101) { m[d] = p; } }
    names.push_back(f.substr(0, f.size() - 4)); ser.push_back(m); }
  // sessions = every date any variant traded (2014+); a variant with no trade that day earns 0
  for (auto& m : ser) for (auto& [d, p] : m) dates.insert(d);
  const std::vector<int> D(dates.begin(), dates.end()); const int T = (int)D.size(), N = (int)names.size();
  std::vector<std::vector<double>> X(N, std::vector<double>(T, 0)); for (int n = 0; n < N; ++n) for (int t = 0; t < T; ++t) { auto it = ser[n].find(D[t]); if (it != ser[n].end()) X[n][t] = it->second; }
  int ci = -1; for (int n = 0; n < N; ++n) if (names[n] == cand) ci = n;
  printf("E1 overfitting check: %d variants × %d sessions (%d → %d); candidate %s%s\n", N, T, D.front(), D.back(), cand.c_str(), ci < 0 ? " NOT FOUND" : "");
  auto sharpe = [&](int n, const std::vector<int>& idx) { double s = 0, s2 = 0; for (int t : idx) { s += X[n][t]; s2 += X[n][t] * X[n][t]; } const double k = idx.size(), mu = s / k, sd = std::sqrt(std::max(1e-12, s2 / k - mu * mu)); return mu / sd; };
  // CSCV
  std::vector<std::vector<int>> blk(S); for (int t = 0; t < T; ++t) blk[std::min(S - 1, t * S / T)].push_back(t);
  long splits = 0, below = 0, candBest = 0; double lambdaSum = 0; std::map<std::string, int> bestCount;
  std::vector<int> pick(S, 0); std::fill(pick.begin(), pick.begin() + S / 2, 1); std::sort(pick.begin(), pick.end());
  do { std::vector<int> is, oos; for (int b = 0; b < S; ++b) for (int t : blk[b]) (pick[b] ? is : oos).push_back(t);
    int best = 0; double bv = -1e18; for (int n = 0; n < N; ++n) { const double v = sharpe(n, is); if (v > bv) { bv = v; best = n; } }
    std::vector<double> o(N); for (int n = 0; n < N; ++n) o[n] = sharpe(n, oos);
    int rank = 1; for (int n = 0; n < N; ++n) if (o[n] < o[best]) rank++;   // 1 = worst, N = best out of sample
    const double w = rank / (N + 1.0), lam = std::log(w / (1 - w));
    splits++; below += lam <= 0; lambdaSum += lam; candBest += best == ci; bestCount[names[best]]++;
  } while (std::next_permutation(pick.begin(), pick.end()));
  printf("CSCV, S = %d blocks: %ld splits · PBO = %.3f (share of splits where the in-sample best is at or below the out-of-sample median) · mean logit %.2f\n", S, splits, (double)below / splits, lambdaSum / splits);
  printf("in-sample winner most often:"); std::vector<std::pair<int, std::string>> bc; for (auto& [k, v] : bestCount) bc.push_back({v, k}); std::sort(bc.rbegin(), bc.rend());
  for (size_t i = 0; i < bc.size() && i < 6; ++i) printf(" %s %.1f%%%s", bc[i].second.c_str(), 100.0 * bc[i].first / splits, i + 1 < std::min<size_t>(6, bc.size()) ? " ·" : "\n");
  if (ci >= 0) printf("the candidate (%s) is the in-sample winner in %.1f%% of splits\n", cand.c_str(), 100.0 * candBest / splits);
  // deflated Sharpe of the candidate (daily, not annualised)
  if (ci >= 0) { std::vector<int> all(T); for (int t = 0; t < T; ++t) all[t] = t; std::vector<double> sr(N); double m1 = 0, m2 = 0;
    for (int n = 0; n < N; ++n) { sr[n] = sharpe(n, all); m1 += sr[n]; m2 += sr[n] * sr[n]; } const double vSR = std::max(1e-12, m2 / N - (m1 / N) * (m1 / N));
    double mu = 0; for (double x : X[ci]) mu += x; mu /= T; double v2 = 0, v3 = 0, v4 = 0; for (double x : X[ci]) { const double d = x - mu; v2 += d * d; v3 += d * d * d; v4 += d * d * d * d; } v2 /= T; v3 /= T; v4 /= T;
    const double g3 = v3 / std::pow(v2, 1.5), g4 = v4 / (v2 * v2), SR = sr[ci], eg = 0.5772156649;
    const double SR0 = std::sqrt(vSR) * ((1 - eg) * invNorm(1 - 1.0 / N) + eg * invNorm(1 - 1.0 / (N * std::exp(1.0))));
    const double z = (SR - SR0) * std::sqrt(T - 1.0) / std::sqrt(std::max(1e-12, 1 - g3 * SR + (g4 - 1) / 4 * SR * SR));
    int rk = 1; for (int n = 0; n < N; ++n) if (sr[n] > SR) rk++;
    printf("deflated Sharpe: candidate daily SR %.4f (annualised %.2f, rank %d of %d) · expected max SR of %d trials %.4f · skew %.2f · kurtosis %.1f · DSR = %.3f\n",
           SR, SR * std::sqrt(252.0), rk, N, N, SR0, g3, g4, normCdf(z)); }
  return 0;
}
