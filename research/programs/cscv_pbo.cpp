// 2026-10-22 cscv_pbo.cpp (founder: "test validate and lets implement", after flag_dependence). Runs
// research/preregistered/2026-10-22_cscv_pbo.md: combinatorially symmetric cross-validation (Bailey, Borwein, López de Prado, Zhu)
// over a set of flag-book variants + the deflated Sharpe ratio (Bailey, López de Prado). Each variant = <dir>/<name>_nq.csv +
// <name>_es.csv in the W15 71-column format; the matrix column = daily pooled NQ+ES net R (the file's `r`) at 1 unit a trade under
// the BOOK2 rule (ok, tf >= 4, pole mult >= 3, pullback 5–38 %, grade A or B, 2 open per market, 2014-01-02 → 2026-10-15), every
// session in the union of traded days (0 when flat). Usage: cscv_pbo <dir> <name1> <name2> …  [--blocks 16] [--candidate base]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <vector>
struct T { int date, em, xm; double r; };
static bool load(const std::string& fn, int mk, std::vector<T>& out) {
  std::ifstream f(fn); if (!f) return false; std::string ln; std::getline(f, ln); std::vector<T> cand;
  while (std::getline(f, ln)) { std::vector<double> a; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) a.push_back(strtod(t.c_str(), nullptr)); if (a.size() < 71) continue;
    const int date = (int)a[0]; if (date < 20140102 || date > 20261015) continue; const double mult = a[22], pull = a[24] * 100; const int g = (int)a[6];
    if (!(a[11] > 0.5 && (int)a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2))) continue; if (!(std::fabs(a[14] - a[15]) > 0)) continue;
    cand.push_back({date, (int)a[12], (int)a[13], a[18]}); }
  std::stable_sort(cand.begin(), cand.end(), [](const T& x, const T& y) { return x.date != y.date ? x.date < y.date : x.em < y.em; });
  std::vector<T> taken; for (auto& t : cand) { int open = 0; for (auto it = taken.rbegin(); it != taken.rend() && it->date == t.date; ++it) if (it->xm > t.em) ++open; if (open < 2) taken.push_back(t); }
  out.insert(out.end(), taken.begin(), taken.end()); (void)mk; return true; }
static double norm_cdf(double z) { return 0.5 * std::erfc(-z / std::sqrt(2.0)); }
static double norm_inv(double p) {   // Acklam's rational approximation, refined with one Newton step
  static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
  static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01};
  static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
  static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00};
  double x; if (p < 0.02425) { const double q = std::sqrt(-2 * std::log(p)); x = (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1); }
  else if (p > 1 - 0.02425) { const double q = std::sqrt(-2 * std::log(1 - p)); x = -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5]) / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1); }
  else { const double q = p - 0.5, r = q * q; x = (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1); }
  const double e = norm_cdf(x) - p; x -= e * std::sqrt(2 * M_PI) * std::exp(x * x / 2); return x; }
struct Mom { double mean, sd, sr, skew, kurt; };
static Mom moments(const std::vector<double>& v) { const size_t n = v.size(); double m = 0; for (double x : v) m += x; m /= n; double s2 = 0, s3 = 0, s4 = 0; for (double x : v) { const double d = x - m; s2 += d * d; s3 += d * d * d; s4 += d * d * d * d; }
  s2 /= n; const double sd = std::sqrt(s2); return {m, sd, sd > 0 ? m / sd : 0, sd > 0 ? (s3 / n) / (sd * sd * sd) : 0, sd > 0 ? (s4 / n) / (s2 * s2) : 3}; }
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: cscv_pbo <dir> <name…> [--blocks 16] [--candidate base]\n"); return 1; }
  const std::string dir = argv[1]; std::vector<std::string> names; int S = 16; std::string candidate = "base";
  for (int i = 2; i < argc; ++i) { if (!strcmp(argv[i], "--blocks") && i + 1 < argc) S = atoi(argv[++i]); else if (!strcmp(argv[i], "--candidate") && i + 1 < argc) candidate = argv[++i]; else names.push_back(argv[i]); }
  const int N = names.size(); std::vector<std::map<int, double>> daily(N); std::set<int> days;
  for (int v = 0; v < N; ++v) { std::vector<T> tr; if (!load(dir + "/" + names[v] + "_nq.csv", 0, tr)) { fprintf(stderr, "missing %s_nq.csv\n", names[v].c_str()); return 1; } load(dir + "/" + names[v] + "_es.csv", 1, tr);
    for (auto& t : tr) { daily[v][t.date] += t.r; days.insert(t.date); } printf("%-6s %5zu trades\n", names[v].c_str(), tr.size()); }
  std::vector<int> dv(days.begin(), days.end()); const int Tn = dv.size(); std::vector<std::vector<double>> M(N, std::vector<double>(Tn, 0.0));
  for (int v = 0; v < N; ++v) for (int i = 0; i < Tn; ++i) { auto it = daily[v].find(dv[i]); if (it != daily[v].end()) M[v][i] = it->second; }
  printf("matrix: %d variants × %d days (%d → %d), %d blocks\n", N, Tn, dv.front(), dv.back(), S);
  // full-sample Sharpe + moments per variant
  std::vector<Mom> full(N); for (int v = 0; v < N; ++v) full[v] = moments(M[v]);
  printf("\n%-6s %8s %8s %8s %7s %7s\n", "name", "meanR/d", "sd", "SR/day", "skew", "kurt"); for (int v = 0; v < N; ++v) printf("%-6s %8.4f %8.4f %8.4f %7.2f %7.2f\n", names[v].c_str(), full[v].mean, full[v].sd, full[v].sr, full[v].skew, full[v].kurt);
  // block sums: per variant, per block: sum, sumsq, count → IS / OOS Sharpe from block aggregates (exact for mean/sd over the union of blocks)
  std::vector<std::vector<double>> bs(N, std::vector<double>(S, 0)), bq(N, std::vector<double>(S, 0)); std::vector<int> bn(S, 0);
  for (int i = 0; i < Tn; ++i) { const int b = std::min(S - 1, (int)((long long)i * S / Tn)); bn[b]++; for (int v = 0; v < N; ++v) { bs[v][b] += M[v][i]; bq[v][b] += M[v][i] * M[v][i]; } }
  auto sharpe = [&](int v, const std::vector<int>& blocks) { double s = 0, q = 0; int n = 0; for (int b : blocks) { s += bs[v][b]; q += bq[v][b]; n += bn[b]; } const double m = s / n, var = q / n - m * m; return var > 0 ? m / std::sqrt(var) : 0.0; };
  // enumerate C(S, S/2) splits
  std::vector<int> comb(S / 2); std::iota(comb.begin(), comb.end(), 0); long splits = 0, pbo_hits = 0, oos_neg = 0; std::vector<long> wins(N, 0); std::vector<double> logits, is_sr, oos_sr; long cand_rank_sum = 0; const int ci = std::find(names.begin(), names.end(), candidate) - names.begin();
  std::vector<double> cand_oos_rel;
  while (true) {
    std::vector<int> is(comb.begin(), comb.end()), oos; { std::set<int> in(is.begin(), is.end()); for (int b = 0; b < S; ++b) if (!in.count(b)) oos.push_back(b); }
    std::vector<double> sri(N), sro(N); for (int v = 0; v < N; ++v) { sri[v] = sharpe(v, is); sro[v] = sharpe(v, oos); }
    const int best = std::max_element(sri.begin(), sri.end()) - sri.begin(); wins[best]++;
    // relative OOS rank of the IS winner: ω = (rank among N, 1 = worst) / (N + 1)
    int rank = 1; for (int v = 0; v < N; ++v) if (v != best && sro[v] < sro[best]) rank++; const double w = (double)rank / (N + 1);
    if (w <= 0.5) pbo_hits++; if (sro[best] < 0) oos_neg++; logits.push_back(std::log(w / (1 - w))); is_sr.push_back(sri[best]); oos_sr.push_back(sro[best]); splits++;
    if (ci < N) { int rc = 1; for (int v = 0; v < N; ++v) if (v != ci && sro[v] < sro[ci]) rc++; cand_oos_rel.push_back((double)rc / (N + 1)); cand_rank_sum += rc; }
    // next combination
    int k = S / 2 - 1; while (k >= 0 && comb[k] == S - S / 2 + k) k--; if (k < 0) break; comb[k]++; for (int j = k + 1; j < S / 2; ++j) comb[j] = comb[j - 1] + 1; }
  // degradation slope OOS on IS
  double mx = 0, my = 0; for (long i = 0; i < splits; ++i) { mx += is_sr[i]; my += oos_sr[i]; } mx /= splits; my /= splits; double sxy = 0, sxx = 0; for (long i = 0; i < splits; ++i) { sxy += (is_sr[i] - mx) * (oos_sr[i] - my); sxx += (is_sr[i] - mx) * (is_sr[i] - mx); }
  std::sort(logits.begin(), logits.end());
  printf("\n== CSCV over %ld splits\n PBO = %.3f  (share of splits where the in-sample winner ranks at or below the OOS median)\n P(OOS Sharpe of the IS winner < 0) = %.3f\n logit(ω) of the IS winner: median %+.2f, mean %+.2f, share < 0 = %.3f\n IS winner's Sharpe: mean %.4f → its OOS Sharpe: mean %.4f  (degradation slope %.3f; negative = anti-persistent)\n",
    splits, (double)pbo_hits / splits, (double)oos_neg / splits, logits[splits / 2], std::accumulate(logits.begin(), logits.end(), 0.0) / splits, (double)pbo_hits / splits, mx, my, sxx > 0 ? sxy / sxx : NAN);
  printf(" in-sample winner by variant:"); for (int v = 0; v < N; ++v) if (wins[v]) printf(" %s %.1f%%", names[v].c_str(), 100.0 * wins[v] / splits); printf("\n");
  if (ci < N) { std::sort(cand_oos_rel.begin(), cand_oos_rel.end()); printf(" candidate %s: mean OOS relative rank %.3f (1 = best), median %.3f, share of splits in the top half %.3f\n", candidate.c_str(), (double)cand_rank_sum / splits / (N + 1), cand_oos_rel[splits / 2], (double)std::count_if(cand_oos_rel.begin(), cand_oos_rel.end(), [](double x) { return x > 0.5; }) / splits); }
  // deflated Sharpe
  { double msr = 0; for (int v = 0; v < N; ++v) msr += full[v].sr; msr /= N; double vsr = 0; for (int v = 0; v < N; ++v) vsr += (full[v].sr - msr) * (full[v].sr - msr); vsr /= std::max(1, N - 1);
    const double gamma = 0.5772156649, emax = std::sqrt(vsr) * ((1 - gamma) * norm_inv(1 - 1.0 / N) + gamma * norm_inv(1 - 1.0 / (N * M_E)));
    auto dsr = [&](int v) { const Mom& m = full[v]; const double den = std::sqrt(std::max(1e-12, 1 - m.skew * m.sr + (m.kurt - 1) / 4 * m.sr * m.sr)); return norm_cdf((m.sr - emax) * std::sqrt(Tn - 1.0) / den); };
    const int isbest = std::max_element(full.begin(), full.end(), [](const Mom& a, const Mom& b) { return a.sr < b.sr; }) - full.begin();
    printf("\n== deflated Sharpe (N = %d trials, var of daily SRs %.2e, E[max SR] = %.4f/day, T = %d days)\n", N, vsr, emax, Tn);
    if (ci < N) printf(" %-6s SR %.4f/day (annualised %.2f)  DSR = %.3f%s\n", candidate.c_str(), full[ci].sr, full[ci].sr * std::sqrt(252.0), dsr(ci), dsr(ci) > 0.95 ? "  > 0.95" : "");
    printf(" %-6s SR %.4f/day (annualised %.2f)  DSR = %.3f%s  (full-sample winner)\n", names[isbest].c_str(), full[isbest].sr, full[isbest].sr * std::sqrt(252.0), dsr(isbest), dsr(isbest) > 0.95 ? "  > 0.95" : ""); }
  return 0;
}
