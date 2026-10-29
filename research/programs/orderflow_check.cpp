// 2026-10-22 orderflow_check.cpp (founder: "test with order flow data as well", "for both reversal and flags"). Runs
// research/preregistered/2026-10-22_orderflow_flags_reversals.md exactly as written before the run: NQ flow days only (aggressor buy volume
// 2023-03 → 2026-03 and 2026-09+, book imbalance to 2026-03-18), flags (flag_hhhl export) with FF1-FF5 and reversals (reversal_plan /
// reversal_double SIGNALS exports: R1, R2, D1) with RF1-RF5, each feature true / false, combos >= 3 of 5 and 1+3+5, 5-point pass.
// Usage: orderflow_check FLAGS.csv RP_SIGNALS.csv RD_SIGNALS.csv. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Min { std::vector<int> tm; std::vector<double> h, l, v, bv, qi; };
struct T { int date, side, em, xm; double r, rc; bool f[5], known4; };
static std::vector<std::vector<double>> readCsv(const char* fn) { std::vector<std::vector<double>> o; std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln);
  while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; } o.push_back(a); } return o; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: orderflow_check FLAGS.csv RP_SIGNALS.csv RD_SIGNALS.csv\n"); return 1; }
  const double cost = 1.5;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  const size_t nb = load_book("data/edge_lab/cache", "NQ", s); build_days(s, fomc, vix);
  std::map<int, Min> M; long flowDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !d.flow || d.date > 20261016) continue; Min z;
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.tm.push_back(tm); z.h.push_back(s.h[i]); z.l.push_back(s.l[i]); z.v.push_back(s.v[i]); z.bv.push_back(s.bv[i]); z.qi.push_back(s.qimb[i]); }
    if (z.tm.size() >= 370) { M[d.date] = std::move(z); flowDays++; } }
  // window sums on the trade's side: side volume, total volume, mean book imbalance (NaN = no book)
  struct W { double sv = 0, v = 0, qs = 0; int qn = 0; double delta() const { return 2 * sv - v; } double share() const { return v > 0 ? sv / v : 0.5; } };
  auto win = [](const Min& z, int side, int a, int b) { W w; for (size_t k = 0; k < z.tm.size(); ++k) { if (z.tm[k] < a || z.tm[k] > b) continue; const double sv = side > 0 ? z.bv[k] : z.v[k] - z.bv[k];
      w.sv += sv; w.v += z.v[k]; if (std::isfinite(z.qi[k])) { w.qs += side * z.qi[k]; w.qn++; } } return w; };
  // ── flags ──
  std::vector<T> fl[2];   // 0 every flag, 1 A-list + 5m HH/HL + with the break
  for (auto& a : readCsv(argv[1])) { if (a.size() < 55 || a[11] < 0.5) continue; const int date = (int)a[0]; auto it = M.find(date); if (it == M.end()) continue; const Min& z = it->second;
    const int side = (int)a[1], tf = (int)a[2], reg = (int)a[3], em = (int)a[12]; const double plo = a[8], phi = a[9];
    // the flag's chart bars on the 09:30 grid: pole high bar starts at reg − 3·tf, pole start = the bar holding the pole's other end, <= 15 bars back
    const int hb = reg - 3 * tf; int ps = hb;
    for (int q = 2; q <= 15; ++q) { const int b0 = hb - q * tf; if (b0 < 930) break; double hi = -1e18, lo = 1e18; for (size_t k = 0; k < z.tm.size(); ++k) if (z.tm[k] >= b0 && z.tm[k] < b0 + tf) { hi = std::max(hi, z.h[k]); lo = std::min(lo, z.l[k]); }
      if (side > 0 ? std::fabs(lo - plo) < 0.01 : std::fabs(hi - phi) < 0.01) { ps = b0; break; } }
    const W pole = win(z, side, ps, hb + tf - 1), flag = win(z, side, hb + tf, reg - 1), cvd = win(z, side, 930, reg - 1), book = win(z, side, reg - 10, reg - 1), def = win(z, side, em - 5, em - 1);
    T t; t.date = date; t.side = side; t.em = em; t.xm = (int)a[13]; t.r = a[18]; const double R = std::fabs(a[14] - a[15]); t.rc = t.r - cost / R;
    t.f[0] = pole.share() >= 0.55; t.f[1] = flag.share() >= 0.50; t.f[2] = cvd.delta() > 0; t.known4 = book.qn > 0; t.f[3] = t.known4 && book.qs / book.qn > 0; t.f[4] = def.delta() >= 0;
    fl[0].push_back(t); if (a[6] == 1 && a[7] > 0.5 && a[47] > 0.5) fl[1].push_back(t); }
  // ── reversals: R1 (setup 1), R2 (2), D1 (11); exits X1 2R / X2 measured move / X4 hold ──
  std::vector<T> rv[3][3];
  auto addRev = [&](const char* fn) { for (auto& a : readCsv(fn)) { if (a.size() < 15) continue; const int setup = (int)a[2]; const int si = setup == 1 ? 0 : setup == 2 ? 1 : setup == 11 ? 2 : -1; if (si < 0) continue;
      const int date = (int)a[0]; auto it = M.find(date); if (it == M.end()) continue; const Min& z = it->second; const int side = (int)a[1], sig = (int)a[3], low1 = (int)a[4], low2 = (int)a[5], em = (int)a[6];
      const int lowC = setup == 11 ? low2 : low1; const W lowB = win(z, side, lowC - 5, lowC - 1), since = win(z, side, lowC - 5, sig - 1), sigB = win(z, side, sig - 5, sig - 1);
      const W prevB = setup == 11 ? win(z, side, low1 - 5, low1 - 1) : win(z, side, lowC - 10, lowC - 6); const W sess = win(z, side, 930, lowC - 6);
      const double nb5 = std::max(0, lowC - 5 - 930) / 5.0, avg5 = nb5 >= 1 ? sess.v / nb5 : NAN; const double R = std::fabs(a[7] - a[8]);
      for (int x = 0; x < 3; ++x) { T t; t.date = date; t.side = side; t.em = em; t.xm = (int)a[12 + x]; t.r = a[9 + x]; t.rc = t.r - cost / R;
        t.f[0] = since.delta() > 0; t.f[1] = lowC - 5 >= 930 && std::isfinite(avg5) && lowB.v >= 1.5 * avg5 && lowB.delta() < 0; t.f[2] = lowC - 5 >= 930 && lowB.v > 0 && prevB.v > 0 && lowB.delta() > prevB.delta();
        t.known4 = sigB.qn > 0; t.f[3] = t.known4 && sigB.qs / sigB.qn > 0; t.f[4] = sigB.share() >= 0.55; rv[si][x].push_back(t); } } };
  addRev(argv[2]); addRev(argv[3]);
  for (auto& v : fl) std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  for (auto& s3 : rv) for (auto& v : s3) std::sort(v.begin(), v.end(), [](const T& a, const T& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
  struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, sc = 0, h[2] = {0, 0}, hn[2] = {0, 0}; };
  auto run = [](const std::vector<T>& v, std::function<bool(const T&)> sel) { St a; int lastD = 0, busy = -1;
    for (auto& t : v) { if (!sel(t)) continue; if (t.date != lastD) { lastD = t.date; busy = -1; } if (t.em <= busy) continue; busy = t.xm;
      a.n++; a.s += t.r; a.w += t.r > 0; (t.r > 0 ? a.gw : a.gl) += std::fabs(t.r); a.sc += t.rc; const int h = t.date >= 20250101; a.h[h] += t.r; a.hn[h]++; } return a; };
  auto str = [](const St& a) { char b[200]; if (a.n < 1) return std::string("     no trades"); snprintf(b, sizeof b, "%4.0f tr %+6.3fR win %4.1f%% PF %.2f · 2023-24 %+6.3f / 2025-26 %+6.3f · +1× cost %+6.3fR",
      a.n, a.s / a.n, 100 * a.w / a.n, a.gl > 0 ? a.gw / a.gl : 99.0, a.hn[0] ? a.h[0] / a.hn[0] : 0, a.hn[1] ? a.h[1] / a.hn[1] : 0, a.sc / a.n); return std::string(b); };
  int passes = 0, judged = 0;
  auto block = [&](const char* title, const std::vector<T>& v, const char* const* FN, int x) {
    printf("\n######## %s\n    %-44s %s\n", title, "all flow-day trades", str(run(v, [](const T&) { return true; })).c_str());
    auto judge = [&](const char* nm, std::function<bool(const T&)> yes, std::function<bool(const T&)> no) { const St a = run(v, yes), b = run(v, no); judged++;
      const bool ok = a.n >= 60 && a.s / a.n >= 0.20 && (a.gl > 0 ? a.gw / a.gl : 99) >= 1.30 && a.hn[0] && a.h[0] > 0 && a.hn[1] && a.h[1] > 0 && a.sc > 0 && b.n && a.s / a.n - b.s / b.n >= 0.20; passes += ok;
      printf("    %-44s %s\n    %-44s %s  → difference %+6.3fR%s\n", nm, str(a).c_str(), "      … without it", str(b).c_str(), a.n && b.n ? a.s / a.n - b.s / b.n : 0, ok ? "  ← PASSES" : ""); };
    for (int f = 0; f < 5; ++f) { if (f == 3) judge(FN[f], [](const T& t) { return t.known4 && t.f[3]; }, [](const T& t) { return t.known4 && !t.f[3]; });
      else judge(FN[f], [f](const T& t) { return t.f[f]; }, [f](const T& t) { return !t.f[f]; }); }
    judge(">= 3 of the five", [](const T& t) { int c = 0; for (bool b : t.f) c += b; return c >= 3; }, [](const T& t) { int c = 0; for (bool b : t.f) c += b; return c < 3; });
    judge("1 + 3 + 5", [](const T& t) { return t.f[0] && t.f[2] && t.f[4]; }, [](const T& t) { return !(t.f[0] && t.f[2] && t.f[4]); }); (void)x; };
  printf("Order flow on flags and reversals (preregistered/2026-10-22_orderflow_flags_reversals.md) — NQ, %ld flow days (2023-03 → 2026-03, 2026-09+), book minutes matched %zu.\n", flowDays, nb);
  printf("Each feature: trades WITH it, then WITHOUT it, one position at a time per set. Pass: >= 60 trades, >= +0.20R, PF >= 1.30, both halves > 0, > 0 at +1× cost, >= +0.20R over without.\n");
  static const char* FF[5] = {"FF1 pole bought (share >= 0.55)", "FF2 pullback not sold (share >= 0.50)", "FF3 session delta on our side", "FF4 book on our side (10 min)", "FF5 buyers defend the mid (5 min)"};
  static const char* RF[5] = {"RF1 delta since the low on our side", "RF2 absorption at the low", "RF3 delta divergence at the low", "RF4 book on our side (signal bar)", "RF5 signal bar bought (>= 0.55)"};
  block("FLAGS — every flag, 3-5m, flag-mid · measured move", fl[0], FF, 0);
  block("FLAGS — A-list + 5m HH/HL + with the 5m opening-range break", fl[1], FF, 0);
  static const char* SN[3] = {"REVERSALS — R1 CHoCH pullback", "REVERSALS — R2 sweep and reclaim", "REVERSALS — D1 double bottom / top, close above the neck"};
  for (int si = 0; si < 3; ++si) block((std::string(SN[si]) + ", exit X2 measured move").c_str(), rv[si][1], RF, 1);
  printf("\nOther exits for the reversals (X1 2R / X4 hold), every trade and RF1 + RF3 + RF5:\n");
  for (int si = 0; si < 3; ++si) for (int x : {0, 2}) { printf("    %-58s all  %s\n", (std::string(SN[si] + 12) + (x ? " · X4 hold" : " · X1 2R")).c_str(), str(run(rv[si][x], [](const T&) { return true; })).c_str());
    printf("    %-58s 135  %s\n", "", str(run(rv[si][x], [](const T& t) { return t.f[0] && t.f[2] && t.f[4]; })).c_str()); }
  printf("\n%d of %d judged cells pass (expect 2-3 by chance at these thresholds with no second market).\n", passes, judged);
  return 0;
}
