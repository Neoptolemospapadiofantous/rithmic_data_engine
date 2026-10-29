// 2026-10-22 flag_extra.cpp (preregistered/2026-10-22_external_ideas.md E2-E4, ideas from papers / open-source projects), on top of the
// candidate (BOOK2 + gap sizing + the 15m trend filter, i.e. htf_context VARIANT=FILT_TREND_15m — its two files are this program's input):
//   E2 absorption at the flag's low (Bookmap's MBO idea, 1m proxy: sellers hit, price held, bids refilled; NQ book days 2023-03 → 2026-03)
//   E3 quarterly re-fit of the stop buffer k and target multiple m (Arévalo et al. 2017's dynamic window) + the best static (k, m) on 2014-19
//   E4 a second, independent detector (Lo, Mamaysky & Wang 2000: Nadaraya-Watson smoothing of the flag's chart, extrema)
// Trade level: R and edge over post-registration twins, by half, shuffle p. Variants (column 72 = size) → OUTDIR (env VARIANT=<name> writes
// only that one, to OUTDIR/tmp_{nq,es}.csv — the disk is ~100 % full and nothing is deleted).
// Usage: flag_extra CAND_NQ.csv CAND_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr; std::vector<int> pos, TM; std::vector<double> O, H, L, C, V, BV, BQ, AQ; bool flow; };
static std::string f2(double v) { char b[64]; snprintf(b, sizeof b, "%.2f", v); return b; }
static std::string f4(double v) { char b[64]; snprintf(b, sizeof b, "%.4f", v); return b; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_extra CAND_NQ.csv CAND_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (!mk) load_book("data/edge_lab/cache", sym, s); build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{d.atr, std::vector<int>(400, -1), {}, {}, {}, {}, {}, {}, {}, {}, {}, d.flow};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]);
        z.V.push_back(s.v[i]); z.BV.push_back(s.bv[i]); z.BQ.push_back(s.bidq.empty() ? NAN : (double)s.bidq[i]); z.AQ.push_back(s.askq.empty() ? NAN : (double)s.askq[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 72) continue; rows.push_back(std::move(r)); } }
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  auto traded = [&](const Row& r) { return r.a[11] > 0.5 && book2(r); };   // a candidate trade (the 15m filter already set ok = 0 on skipped flags)
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.O[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  // per candidate trade: absorption (E2, -1 unknown), kernel confirmation (E4, -1 unknown), twin
  struct X { int ab = -1, kc = -1; double tw = NAN; int top = -1; };
  std::vector<X> F(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r)) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
    const int sd = (int)r.a[1], tf = (int)r.a[2], reg = (int)r.a[3], em = (int)r.a[12]; const double PH = sd > 0 ? r.a[9] : -r.a[8], cost = r.mk ? 0.5 : 1.5;
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; }; auto cl = [&](int k) { return sd * z.C[k]; };
    const int e = em >= 930 && em < 1320 ? z.pos[em - 930] : -1, rg = reg >= 930 && reg < 1320 ? z.pos[reg - 930] : -1; if (e < 0 || rg < 0) continue;
    int top = -1; for (int k = std::min(rg, e) - 1; k >= 0 && k >= rg - 3 * tf - 90; --k) if (std::fabs(hi(k) - PH) < 1e-6) { top = k; break; } if (top < 0) continue; F[i].top = top;
    F[i].tw = twin(z, sd, em, std::fabs(r.a[14] - r.a[15]), std::fabs(r.a[16] - r.a[14]), cost, (unsigned)(r.a[0] * 13 + em * 7 + tf + r.mk * 99991));
    // E2 (NQ, flow + book minutes)
    if (r.mk == 0 && z.flow && !std::isnan(z.BQ[std::max(0, e - 1)])) {
      double aggS = 0, restS = 0; int n = 0; bool any = false; double mn = 1e18;
      for (int k = 0; k < e; ++k) { const double agg = sd > 0 ? z.V[k] - z.BV[k] : z.BV[k], rest = sd > 0 ? z.BQ[k] : z.AQ[k];
        if (k > top) { mn = std::min(mn, lo(k)); const double half = (PH + mn) / 2, rng = hi(k) - lo(k);
          if (n > 0 && std::isfinite(rest) && lo(k) <= half && agg >= 1.5 * aggS / n && rng > 0 && (cl(k) - lo(k)) / rng >= 0.5 && rest >= restS / n) any = true; }
        if (std::isfinite(rest)) { aggS += agg; restS += rest; n++; } }
      F[i].ab = any; }
    // E4: Nadaraya-Watson (Gaussian, h = 3 bars) on the flag's chart closes before the fill
    { std::vector<double> c, rngv; int cur = -1;
      for (int k = 0; k < e; ++k) { const int b = (z.TM[k] - 930) / tf; if (b != cur) { c.push_back(cl(k)); rngv.push_back(hi(k) - lo(k)); cur = b; } else { c.back() = cl(k); rngv.back() = std::max(rngv.back(), hi(k) - lo(k)); } }
      // drop a chart bar that is not complete at the fill
      if (!c.empty() && ((z.TM[e - 1] - 930) / tf + 1) * tf + 930 > em) { c.pop_back(); rngv.pop_back(); }
      const int n = (int)c.size();
      if (n >= 12) { std::vector<double> m(n); for (int t = 0; t < n; ++t) { double w = 0, s2 = 0; for (int u = 0; u < n; ++u) { const double kk = std::exp(-0.5 * (t - u) * (t - u) / 9.0); w += kk; s2 += kk * c[u]; } m[t] = s2 / w; }
        double avg = 0; for (double x : rngv) avg += x; avg /= n; int jmax = -1;
        for (int j = n - 2; j >= std::max(1, n - 8); --j) if (m[j] > m[j - 1] && m[j] >= m[j + 1]) { jmax = j; break; }
        int jmin = -1; if (jmax > 0) for (int j = jmax - 1; j >= 1; --j) if (m[j] < m[j - 1] && m[j] <= m[j + 1]) { jmin = j; break; }
        if (jmax > 0 && jmin < 0) jmin = 0;
        int ok = 0; if (jmax > 0) { const double rise = m[jmax] - m[jmin], dec = rise > 0 ? (m[jmax] - m[n - 1]) / rise : -1; ok = rise >= 2 * avg && dec >= 0.05 && dec <= 0.62; }
        F[i].kc = ok; } } }
  // trade-level tables + shuffle (within year × market)
  std::mt19937 rng(20261023);
  auto table = [&](const char* name, std::function<int(size_t)> feat, int split) {
    struct A { double n = 0, r = 0, tw = 0; void add(double rr, double t) { if (std::isnan(t)) return; n++; r += rr; tw += t; } } g[2][2][2];
    std::vector<size_t> all; std::map<int, std::vector<size_t>> grp;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r) || r.a[0] < 20140101) continue; const int v = feat(i); if (v < 0) continue;
      g[r.mk][r.a[0] >= split][v].add(r.a[18], F[i].tw); all.push_back(i); grp[(int)r.a[0] / 10000 * 2 + r.mk].push_back(i); }
    std::map<size_t, size_t> where; for (size_t q = 0; q < all.size(); ++q) where[all[q]] = q;
    auto diff = [&](const std::vector<int>& lab) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (size_t q = 0; q < all.size(); ++q) { const double rr = rows[all[q]].a[18]; if (lab[q]) { s1 += rr; n1++; } else { s0 += rr; n0++; } } return (n1 ? s1 / n1 : 0) - (n0 ? s0 / n0 : 0); };
    std::vector<int> lab(all.size()); for (size_t q = 0; q < all.size(); ++q) lab[q] = feat(all[q]);
    const double obs = diff(lab); int ge = 0;
    for (int rep = 0; rep < 1000; ++rep) { std::vector<int> sh = lab; for (auto& [k, ix] : grp) { std::vector<int> v; for (size_t i : ix) v.push_back(lab[where[i]]); std::shuffle(v.begin(), v.end(), rng); for (size_t q = 0; q < ix.size(); ++q) sh[where[ix[q]]] = v[q]; } ge += diff(sh) >= obs; }
    printf("  %-38s |", name);
    for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { auto& y = g[mk][h][1]; auto& n_ = g[mk][h][0];
      printf(" %s %s yes %+5.2f/%+5.2f (%4.0f) no %+5.2f (%4.0f) |", mk ? "ES" : "NQ", h ? "late " : "early", y.n ? y.r / y.n : 0, y.n ? (y.r - y.tw) / y.n : 0, y.n, n_.n ? n_.r / n_.n : 0, n_.n); }
    printf(" diff %+6.3f shuffle p=%.3f\n", obs, ge / 1000.0); };
  printf("Candidate trades (BOOK2 + gap sizing + 15m trend filter): yes = feature present — R / edge over twins (n) vs no R (n)\n");
  printf("E2 halves: early = 2023-03 → 2024-09, late = 2024-10 → 2026-03 (NQ only). E4 halves: early = 2014-19, late = 2020-26.\n");
  table("E2 absorption at the flag's low (NQ)", [&](size_t i) { return F[i].ab; }, 20241001);
  table("E4 kernel detector confirms the flag", [&](size_t i) { return F[i].kc; }, 20200101);
  // ── E3: re-simulate every candidate trade on the (k, m) grid from its own fill
  const double KS[5] = {0, 0.05, 0.1, 0.2, 0.3}, MS[4] = {0.75, 1.0, 1.25, 1.5};
  struct Sim { bool ok = false; int xm = 0; double sp = 0, tg = 0, xp = 0, net = 0, r = 0; };
  std::vector<std::array<Sim, 20>> G(rows.size());
  int nchk = 0, nbad = 0;
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r)) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
    const int sd = (int)r.a[1], em = (int)r.a[12]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL, cost = r.mk ? 0.5 : 1.5;
    const double en = sd * r.a[14], fl = sd * r.a[15] + 0.1 * z.atr; const int k0 = em >= 930 && em < 1320 ? z.pos[em - 930] : -1; if (k0 < 0) continue;
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    for (int a = 0; a < 5; ++a) for (int b = 0; b < 4; ++b) { Sim& s = G[i][a * 4 + b]; s.sp = fl - KS[a] * z.atr; s.tg = PH + MS[b] * M; if (!(en - s.sp > 0) || !(s.tg > en)) continue;
      s.xm = z.TM[k0]; s.xp = NAN;
      if (lo(k0) <= s.sp) s.xp = s.sp;
      else for (int k = k0 + 1; k < (int)z.C.size(); ++k) { s.xm = z.TM[k]; if (lo(k) <= s.sp) { s.xp = s.sp; break; } if (hi(k) >= s.tg) { s.xp = s.tg; break; } if (z.TM[k] >= 1315) { s.xp = sd * z.O[k]; break; } }
      if (std::isnan(s.xp)) s.xp = sd * z.C.back(); s.net = s.xp - en - cost; s.r = s.net / (en - s.sp); s.ok = true; }
    // check on the trade itself (same exit price and minute); R can differ in the 3rd decimal because the export stores prices rounded to cents
    const Sim& f0 = G[i][2 * 4 + 1]; nchk++; if (!f0.ok || std::fabs(sd * f0.xp - r.a[17]) > 0.011 || f0.xm != (int)r.a[13] || std::fabs(f0.r - r.a[18]) > 0.01 * std::max(1.0, std::fabs(r.a[18]))) { nbad++;
      if (std::getenv("E3DEBUG") && nbad <= 6) printf("  mismatch %d %s side %d em %d: export exit %.2f at %d R %.4f · resim exit %.2f at %d R %.4f · stop %.2f vs %.2f · target %.2f vs %.2f\n", (int)r.a[0], r.mk ? "ES" : "NQ", sd, em,
        r.a[17], (int)r.a[13], r.a[18], sd * f0.xp, f0.xm, f0.r, r.a[15], sd * f0.sp, r.a[16], sd * f0.tg); } }
  printf("\nE3 check: the fixed (k 0.1, m 1.0) re-simulation reproduces the candidate's trades (exit price, exit minute, R within 1 %%) on %d of %d\n", nchk - nbad, nchk);
  // quarterly walk-forward: best total net R over the previous 4 quarters
  auto qOf = [](const Row& r) { const int d = (int)r.a[0]; return (d / 10000) * 4 + ((d / 100) % 100 - 1) / 3; };
  std::map<int, std::array<double, 20>> qsum; for (size_t i = 0; i < rows.size(); ++i) { if (!G[i][9].ok) continue; auto& a = qsum[qOf(rows[i])]; for (int c = 0; c < 20; ++c) a[c] += G[i][c].ok ? G[i][c].r : 0; }
  std::map<int, int> qpick; std::map<int, int> pickCount;
  for (auto& [q, a] : qsum) { std::array<double, 20> s{}; int nq = 0; for (int p = q - 4; p < q; ++p) { auto it = qsum.find(p); if (it == qsum.end()) continue; nq++; for (int c = 0; c < 20; ++c) s[c] += it->second[c]; }
    int best = 9; if (nq >= 2) { double bv = -1e18; for (int c = 0; c < 20; ++c) if (s[c] > bv) { bv = s[c]; best = c; } } qpick[q] = best; if (q / 4 >= 2014) pickCount[best]++; }
  std::array<double, 20> st{}; for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!G[i][9].ok || r.a[0] < 20140101 || r.a[0] >= 20200101) continue; for (int c = 0; c < 20; ++c) st[c] += G[i][c].ok ? G[i][c].r : 0; }
  int sbest = 9; for (int c = 0; c < 20; ++c) if (st[c] > st[sbest]) sbest = c;
  printf("E3 quarterly picks (2014-26): "); for (auto& [c, n] : pickCount) printf("k %.2f m %.2f ×%d · ", KS[c / 4], MS[c % 4], n); printf("\nE3 best static on 2014-19: k %.2f m %.2f\n", KS[sbest / 4], MS[sbest % 4]);
  for (int mode = 0; mode < 2; ++mode) { double s[2][2] = {}, n[2][2] = {};
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!G[i][9].ok || r.a[0] < 20140101) continue; const int c = mode ? sbest : qpick[qOf(r)]; if (!G[i][c].ok) continue; s[r.mk][r.a[0] >= 20200101] += G[i][c].r; n[r.mk][r.a[0] >= 20200101]++; }
    double b[2][2] = {}, bn[2][2] = {}; for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!G[i][9].ok || r.a[0] < 20140101) continue; b[r.mk][r.a[0] >= 20200101] += r.a[18]; bn[r.mk][r.a[0] >= 20200101]++; }
    printf("  %-24s R per trade NQ %+.3f / %+.3f  ES %+.3f / %+.3f   (candidate NQ %+.3f / %+.3f  ES %+.3f / %+.3f; 2014-19 / 2020-26)\n", mode ? "E3 best static" : "E3 quarterly re-fit",
      s[0][0] / std::max(1.0, n[0][0]), s[0][1] / std::max(1.0, n[0][1]), s[1][0] / std::max(1.0, n[1][0]), s[1][1] / std::max(1.0, n[1][1]), b[0][0] / bn[0][0], b[0][1] / bn[0][1], b[1][0] / bn[1][0], b[1][1] / bn[1][1]); }
  // ── variants
  auto write = [&](const std::string& name, std::function<void(size_t, std::vector<std::string>&)> mod) {
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str());
      for (size_t i = 0; i < rows.size(); ++i) { if (rows[i].mk != mk) continue; std::vector<std::string> t = rows[i].tok; if (traded(rows[i])) mod(i, t);
        for (size_t j = 0; j < t.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", t[j].c_str()); fprintf(fo, "\n"); }
      fclose(fo); }
    printf("  wrote %s\n", name.c_str()); };
  auto setSim = [&](size_t i, std::vector<std::string>& t, int c) { const Sim& s = G[i][c]; if (!s.ok) { t[11] = "0"; return; } const int sd = (int)rows[i].a[1];
    t[13] = std::to_string(s.xm); t[15] = f2(sd * s.sp); t[16] = f2(sd * s.tg); t[17] = f2(sd * s.xp); t[18] = f4(s.r); t[19] = f4(s.net); };
  printf("\nvariants:\n");
  write("E0cand", [](size_t, std::vector<std::string>&) {});
  write("E2skip", [&](size_t i, std::vector<std::string>& t) { if (F[i].ab == 0) t[11] = "0"; });
  write("E2plus", [&](size_t i, std::vector<std::string>& t) { if (F[i].ab == 1) t[71] = std::to_string(std::stoi(t[71]) + 1); });
  write("E3fixed", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 9); });
  write("E3refit", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, qpick[qOf(rows[i])]); });
  write("E3static", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, sbest); });
  write("E4skip", [&](size_t i, std::vector<std::string>& t) { if (F[i].kc == 0) t[11] = "0"; });
  return 0;
}
