// 2026-10-22 flag_types.cpp (founder: "do more tests on how we can identify better flags. on any of the 5 timeframes … split the research
// to see what types of poles and what type of flags are the best … Possible models can identify them?" → "do all"). Runs
// research/preregistered/2026-10-22_pole_flag_types.md exactly as written before the run:
//   T1 pole type × flag type (measurements known at the fill), every result as EDGE = flag R − its post-registration twin's R
//   T2 order flow: session delta with / against the trade (NQ flow days)
//   T3 shape-only models: SHAPE-GBM and PATH-kNN, walk-forward by year
// Variants for the account check are written as modified wide15 copies to OUTDIR/<name>_{nq,es}.csv (judged by `flag_prop_ev A B prob`).
// Usage: flag_types W15_NQ.csv W15_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930, 15:55 = 1315).
#include "research/meta.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr, gap, prh, prl, onh, onl, orh, orl; bool flow; std::vector<int> pos, TM; std::vector<double> O, H, L, C, V, BV, VW; };
enum { NF = 20 };   // shape features for the GBM
static const char* FN[NF] = {"chart minutes", "pole / avg bar", "pole / ATR", "pullback depth (export)", "pole bars", "pole straightness (export)",
  "flag / pole volume (export)", "pole minutes", "pole straightness (1m)", "pole spike", "origin overnight", "origin prior high", "origin 09:30-35",
  "origin VWAP", "flag depth", "flag drift", "flag bar size", "flag down bars", "flag length / pole", "flag volume / pole"};
struct T { int ri, mk, date, year, side, tf, grade, em, reg; double r, twin, x[NF], path[16], delta; bool flowday; int pa, orig, fa, chan, tg, ptime, gapw; };

// ── GBM (flag_model.cpp's: 100 depth-2 trees on 16 quantile bins, lr 0.05, leaves >= 60), generic over NF features
struct Tree { int f0; double t0; int f[2]; double t[2]; double v[4]; };
struct ShapeGbm { double base = 0; std::vector<Tree> trees; double operator()(const double* x) const { double s = base; for (auto& t : trees) { const int c = x[t.f0] > t.t0; const int g = t.f[c] >= 0 && x[t.f[c]] > t.t[c]; s += t.v[c * 2 + g]; } return s; } };
static ShapeGbm trainGbm(const std::vector<const double*>& X, const std::vector<double>& y, std::vector<double>* gain) {
  const size_t n = X.size(); ShapeGbm m; double ym = 0; for (double v : y) ym += v; ym /= n; m.base = ym;
  std::vector<std::array<double, 15>> cut(NF);
  for (int j = 0; j < NF; ++j) { std::vector<double> v; v.reserve(n); for (auto x : X) v.push_back(x[j]); std::sort(v.begin(), v.end()); for (int q = 0; q < 15; ++q) cut[j][q] = v[(size_t)((q + 1) * n / 16)]; }
  std::vector<std::array<unsigned char, NF>> B(n); for (size_t k = 0; k < n; ++k) for (int j = 0; j < NF; ++j) { int b = 0; while (b < 15 && X[k][j] > cut[j][b]) ++b; B[k][j] = (unsigned char)b; }
  std::vector<double> F(n, ym), res(n); const double lr = 0.05; const int minLeaf = 60;
  auto bestSplit = [&](const std::vector<int>& idx, int& bf, int& bb, double& bg) { bf = -1; bg = 0; double tot = 0; for (int k : idx) tot += res[k]; const double nn = idx.size();
    for (int j = 0; j < NF; ++j) { double s[16] = {0}, c[16] = {0}; for (int k : idx) { s[B[k][j]] += res[k]; c[B[k][j]]++; } double sl = 0, cl = 0;
      for (int b = 0; b < 15; ++b) { sl += s[b]; cl += c[b]; const double cr = nn - cl; if (cl < minLeaf || cr < minLeaf) continue; const double sr = tot - sl;
        const double g = sl * sl / cl + sr * sr / cr - tot * tot / nn; if (g > bg) { bg = g; bf = j; bb = b; } } } };
  for (int it = 0; it < 100; ++it) { for (size_t k = 0; k < n; ++k) res[k] = y[k] - F[k];
    std::vector<int> idx(n); for (size_t k = 0; k < n; ++k) idx[k] = (int)k; int f0, b0; double g0; bestSplit(idx, f0, b0, g0); if (f0 < 0) break; if (gain) (*gain)[f0] += g0;
    std::vector<int> ch[2]; for (int k : idx) ch[B[k][f0] > b0].push_back(k); Tree t; t.f0 = f0; t.t0 = cut[f0][b0];
    for (int c = 0; c < 2; ++c) { int f1, b1; double g1; bestSplit(ch[c], f1, b1, g1); t.f[c] = f1; t.t[c] = f1 >= 0 ? cut[f1][b1] : 0; if (gain && f1 >= 0) (*gain)[f1] += g1;
      double s[2] = {0, 0}, cc[2] = {0, 0}; for (int k : ch[c]) { const int g = f1 >= 0 && B[k][f1] > b1; s[g] += res[k]; cc[g]++; }
      for (int g = 0; g < 2; ++g) t.v[c * 2 + g] = cc[g] ? lr * s[g] / cc[g] : 0; }
    for (int k : idx) { const int c = X[k][t.f0] > t.t0; const int g = t.f[c] >= 0 && X[k][t.f[c]] > t.t[c]; F[k] += t.v[c * 2 + g]; }
    m.trees.push_back(t); }
  return m; }

int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_types W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{d.atr, d.ro - d.pc, d.ph, d.pl, -1e18, 1e18, -1e18, 1e18, d.flow, std::vector<int>(400, -1), {}, {}, {}, {}, {}, {}, {}, {}};
      double pv = 0, vv = 0;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]);
        if (tm < 930) { z.onh = std::max(z.onh, (double)s.h[i]); z.onl = std::min(z.onl, (double)s.l[i]); continue; } if (tm >= 1320) continue;
        if (tm < 935) { z.orh = std::max(z.orh, (double)s.h[i]); z.orl = std::min(z.orl, (double)s.l[i]); }
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); z.BV.push_back(s.bv[i]);
        pv += (s.h[i] + s.l[i] + s.c[i]) / 3 * s.v[i]; vv += s.v[i]; z.VW.push_back(vv > 0 ? pv / vv : s.c[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; rows.push_back(std::move(r)); } }
  auto write = [&](const std::string& name, const std::vector<Row>& rs) {
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + name + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str()); for (auto& r : rs) if (r.mk == mk) { for (size_t j = 0; j < r.tok.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", r.tok[j].c_str()); fprintf(fo, "\n"); } fclose(fo); } };
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return r.a[11] > 0.5 && (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };

  // ── measurements per trade
  std::vector<T> tr; int miss[4] = {0, 0, 0, 0};
  for (size_t ri = 0; ri < rows.size(); ++ri) { const Row& r = rows[ri]; if (r.a[11] < 0.5) continue; const int mk = r.mk;
    auto it = days[mk].find((int)r.a[0]); if (it == days[mk].end()) { miss[0]++; continue; } const DB& z = it->second;
    T t{}; t.ri = (int)ri; t.mk = mk; t.date = (int)r.a[0]; t.year = t.date / 10000; t.side = (int)r.a[1]; t.tf = (int)r.a[2]; t.grade = (int)r.a[6]; t.reg = (int)r.a[3]; t.em = (int)r.a[12]; t.r = r.a[18];
    const int sd = t.side; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL; if (!(M > 0) || t.em < 930 || t.em >= 1316) { miss[1]++; continue; }
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    auto cl = [&](int k) { return sd * z.C[k]; }; auto op = [&](int k) { return sd * z.O[k]; };
    const int e = z.pos[t.em - 930], rg = z.pos[std::min(t.reg, 1319) - 930]; if (e < 0 || rg < 0) { miss[2]++; continue; }
    int top = -1; for (int k = std::min(rg, e) - 1; k >= 0 && k >= rg - 3 * t.tf - 90; --k) if (std::fabs(hi(k) - PH) < 1e-6) { top = k; break; }
    int low = -1; if (top >= 0) for (int k = top; k >= 0 && k >= top - 240; --k) if (std::fabs(lo(k) - PL) < 1e-6) { low = k; break; }
    if (top < 0 || low < 0 || top >= e) { miss[3]++; continue; }
    // pole
    double sumr = 0, big = 0, pvol = 0; for (int k = low; k <= top; ++k) { const double rr = hi(k) - lo(k); sumr += rr; big = std::max(big, rr); pvol += z.V[k]; }
    const int pmin = top - low + 1; const double pst = sumr > 0 ? M / sumr : 0, spike = big / M;
    auto crossed = [&](double lv) { return PL < lv && lv <= PH; };
    const double onv = sd > 0 ? z.onh : -z.onl, prv = sd > 0 ? z.prh : -z.prl, orv = sd > 0 ? z.orh : -z.orl;
    const bool oON = std::isfinite(onv) && std::fabs(onv) < 1e17 && crossed(onv), oPR = std::isfinite(prv) && crossed(prv), oOR = std::fabs(orv) < 1e17 && crossed(orv) && low >= 5;
    const bool oVW = sd * z.VW[low] > PL && sd * z.VW[top] < PH && lo(low) <= sd * z.VW[low] && hi(top) >= sd * z.VW[top];
    // flag (closed 1m bars after the top, before the entry minute)
    double mn = 1e18, sx = 0, sy = 0, sxx = 0, sxy = 0, fr = 0, fvol = 0; int nd = 0, m = 0;
    for (int k = top + 1; k < e; ++k) { mn = std::min(mn, lo(k)); const double q = m; sx += q; sy += cl(k); sxx += q * q; sxy += q * cl(k); fr += hi(k) - lo(k); fvol += z.V[k]; nd += cl(k) < op(k); m++; }
    const double depth = m ? (PH - mn) / M : 0, slope = m > 1 ? (m * sxy - sx * sy) / (m * sxx - sx * sx) : 0, drift = m > 1 ? slope * m / M : 0;
    const double bars = m && sumr > 0 ? (fr / m) / (sumr / pmin) : 0, down = m ? (double)nd / m : 0, flen = (double)m / pmin, fv = m && pvol > 0 ? (fvol / m) / (pvol / pmin) : 0;
    const double xs[NF] = {(double)t.tf, r.a[22], r.a[23], r.a[24], r.a[45], r.a[46], r.a[53], (double)pmin, pst, spike, (double)oON, (double)oPR, (double)oOR, (double)oVW, depth, drift, bars, down, flen, fv};
    for (int j = 0; j < NF; ++j) t.x[j] = std::isfinite(xs[j]) ? xs[j] : 0;
    for (int q = 0; q < 16; ++q) { const double pos = low + (double)q * (e - 1 - low) / 15.0; const int k0 = (int)pos; const int k1 = std::min(e - 1, k0 + 1); const double w = pos - k0;
      t.path[q] = ((1 - w) * cl(k0) + w * cl(k1) - PL) / M; }
    t.orig = oON || oPR || oOR || oVW; t.chan = drift < -0.25; t.fa = depth > 0.5 ? 3 : depth > 0.3 ? 2 : bars <= 0.6 ? 0 : 1;
    t.tg = t.tf <= 2 ? 0 : t.tf == 3 ? 1 : 2;
    { const int ttop = z.TM[top]; t.ptime = ttop < 960 ? 0 : ttop < 1020 ? 1 : ttop < 1080 ? 2 : ttop < 1200 ? 3 : 4; }
    t.gapw = sd * z.gap > 0; t.pa = spike >= 0.4 ? 0 : 1;   // FAST / SLOW resolved after the 2010-13 medians
    t.flowday = z.flow; double dl = 0; for (int k = 0; k < e; ++k) dl += 2 * z.BV[k] - z.V[k]; t.delta = sd * dl;
    // twin: 5 random minutes in the 60 after registration, same stop / target distances, 1m to 15:55
    const double risk = std::fabs(r.a[14] - r.a[15]), tgd = std::fabs(r.a[16] - r.a[14]), cost = mk ? 0.5 : 1.5;
    std::mt19937 g((unsigned)(t.date * 31 + t.reg * 7 + t.tf + mk * 1000003)); double tw = 0; int tn = 0;
    if (risk > 0) for (int rep = 0; rep < 5; ++rep) { const int tm = t.reg + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue;
      const double en = op(k0); double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { const int tmk = z.TM[k];
        if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (tmk >= 1315) { gross = cl(k) - en; break; } }
      if (std::isnan(gross)) gross = cl((int)z.C.size() - 1) - en; tw += (gross - cost) / risk; tn++; }
    if (!tn) { miss[1]++; continue; } t.twin = tw / tn;
    tr.push_back(t); }
  printf("Trades measured: %zu (skipped: no day %d, bad row %d, no entry bar %d, pole not found %d)\n", tr.size(), miss[0], miss[1], miss[2], miss[3]);

  // ── cuts from 2010-13 only
  auto tercile = [&](int mk, std::function<double(const T&)> f, double& c1, double& c2) { std::vector<double> v; for (auto& t : tr) if (t.mk == mk && t.year <= 2013) v.push_back(f(t)); std::sort(v.begin(), v.end());
    c1 = v.empty() ? 0 : v[v.size() / 3]; c2 = v.empty() ? 0 : v[v.size() * 2 / 3]; };
  for (int mk = 0; mk < 2; ++mk) for (int tf = 1; tf <= 5; ++tf) { std::vector<double> v; for (auto& t : tr) if (t.mk == mk && t.tf == tf && t.year <= 2013 && t.pa) v.push_back(t.x[7]); std::sort(v.begin(), v.end());
    const double med = v.empty() ? 0 : v[v.size() / 2]; for (auto& t : tr) if (t.mk == mk && t.tf == tf && t.pa) t.pa = t.x[7] <= med ? 1 : 2; }

  struct Acc { double n = 0, r = 0, tw = 0; void add(const T& t) { n++; r += t.r; tw += t.twin; } double edge() const { return n ? (r - tw) / n : NAN; } };
  struct Cell { Acc all, a, b; };   // all 2014-26, a 2014-21, b 2022-26
  auto addc = [](Cell& c, const T& t) { if (t.year < 2014) return; c.all.add(t); (t.year <= 2021 ? c.a : c.b).add(t); };
  Cell U[2], UB[2]; for (auto& t : tr) { addc(U[t.mk], t); if (book2(rows[t.ri])) addc(UB[t.mk], t); }
  auto pr = [](const Cell& c) { printf(" %6.0f %+6.3f %+6.3f %+6.3f | %+6.3f (%5.0f) | %+6.3f (%5.0f) ||", c.all.n, c.all.n ? c.all.r / c.all.n : 0, c.all.n ? c.all.tw / c.all.n : 0, c.all.edge(), c.a.edge(), c.a.n, c.b.edge(), c.b.n); };
  printf("\nColumns per market: n R twin EDGE | 2014-21 edge (n) | 2022-26 edge (n). Every flag counted (charts overlap). Test years 2014-26.\n");
  printf("%-44s", "UNIVERSE every wide15 flag"); pr(U[0]); pr(U[1]); printf("\n%-44s", "BOOK2 trades"); pr(UB[0]); pr(UB[1]); printf("\n");

  // ── T1 marginal tables
  auto table = [&](const char* title, int nb, const char* const* names, std::function<int(const T&)> bucket) {
    printf("\n== %s\n", title); std::vector<Cell> c[2]; c[0].assign(nb, {}); c[1].assign(nb, {});
    for (auto& t : tr) { const int b = bucket(t); if (b >= 0 && b < nb) addc(c[t.mk][b], t); }
    for (int b = 0; b < nb; ++b) { printf("  %-42s", names[b]); pr(c[0][b]); pr(c[1][b]); printf("\n"); } };
  auto terc = [&](const char* title, int j, const char* lo, const char* hi) { double cut[2][2]; for (int mk = 0; mk < 2; ++mk) tercile(mk, [j](const T& t) { return t.x[j]; }, cut[mk][0], cut[mk][1]);
    std::string a = std::string("low (") + lo + ")", b = "middle", c = std::string("high (") + hi + ")"; const char* nm[3] = {a.c_str(), b.c_str(), c.c_str()};
    char tt[160]; snprintf(tt, sizeof tt, "%s — 2010-13 terciles NQ %.3g / %.3g, ES %.3g / %.3g", title, cut[0][0], cut[0][1], cut[1][0], cut[1][1]);
    table(tt, 3, nm, [&cut, j](const T& t) { return t.x[j] <= cut[t.mk][0] ? 0 : t.x[j] <= cut[t.mk][1] ? 1 : 2; }); };
  printf("\n######## T1 POLE measurements\n");
  terc("P-SIZE pole / ATR", 2, "small", "big"); terc("pole / average bar (export)", 1, "small", "big"); terc("P-SPEED pole minutes", 7, "fast", "slow");
  terc("P-STRAIGHT (1m)", 8, "choppy", "straight"); terc("P-SPIKE largest 1m bar / pole", 9, "steady", "one big bar");
  { const char* nm[5] = {"no level crossed", "crossed overnight high", "crossed prior RTH high", "crossed 09:30-35 high", "crossed VWAP"};
    printf("\n== P-ORIGIN (a flag can cross several — each row is 'crossed X', the first row none)\n");
    for (int b = 0; b < 5; ++b) { Cell c[2]; for (auto& t : tr) { const bool yes = b == 0 ? !t.orig : t.x[9 + b] > 0.5; if (yes) addc(c[t.mk], t); } printf("  %-42s", nm[b]); pr(c[0]); pr(c[1]); printf("\n"); } }
  { const char* nm[5] = {"pole top 09:30-10:00", "10:00-11:00", "11:00-12:00", "12:00-14:00", "14:00-16:00"}; table("P-TIME", 5, nm, [](const T& t) { return t.ptime; }); }
  { const char* nm[2] = {"against the gap", "with the gap"}; table("P-GAP", 2, nm, [](const T& t) { return t.gapw; }); }
  { const char* nm[3] = {"SPIKE (1m bar >= 40 % of pole)", "FAST steady", "SLOW steady"}; table("Pole archetype", 3, nm, [](const T& t) { return t.pa; }); }
  printf("\n######## T1 FLAG measurements (at the fill)\n");
  { const char* nm[4] = {"depth <= 0.30", "0.30-0.38", "0.38-0.50", "> 0.50"}; table("F-DEPTH so far", 4, nm, [](const T& t) { return t.x[14] <= 0.3 ? 0 : t.x[14] <= 0.38 ? 1 : t.x[14] <= 0.5 ? 2 : 3; }); }
  terc("F-DRIFT", 15, "falling", "rising"); terc("F-BARS flag bar / pole bar", 16, "small bars", "big bars"); terc("F-DOWN share of down bars", 17, "few", "many");
  terc("F-LEN flag / pole minutes", 18, "short", "long"); terc("F-VOL flag / pole volume", 19, "dried up", "heavy");
  { const char* nm[8] = {"TIGHT", "TIGHT + channel", "LOOSE", "LOOSE + channel", "MEDIUM", "MEDIUM + channel", "DEEP", "DEEP + channel"};
    table("Flag archetype", 8, nm, [](const T& t) { return t.fa * 2 + t.chan; }); }
  { const char* nm[3] = {"1-2m", "3m", "4-5m"}; table("Chart group", 3, nm, [](const T& t) { return t.tg; }); }
  { const char* nm[2] = {"bull", "bear"}; table("Side", 2, nm, [](const T& t) { return t.side > 0 ? 0 : 1; }); }

  // ── T1 grid + selection on 2014-21
  const char* PA[3] = {"SPIKE", "FAST", "SLOW"}; const char* FA[4] = {"TIGHT", "LOOSE", "MEDIUM", "DEEP"}; const char* TG[3] = {"1-2m", "3m", "4-5m"};
  auto key = [](const T& t) { return ((t.pa * 2 + t.orig) * 8 + t.fa * 2 + t.chan) * 3 + t.tg; };
  auto kname = [&](int k) { const int tg = k % 3, f = (k / 3) % 8, p = k / 24; return std::string(PA[p / 2]) + (p % 2 ? "+level" : "") + " · " + FA[f / 2] + (f % 2 ? "+chan" : "") + " · " + TG[tg]; };
  std::vector<Cell> G[2]; G[0].assign(144, {}); G[1].assign(144, {}); for (auto& t : tr) addc(G[t.mk][key(t)], t);
  std::vector<int> ks; for (int k = 0; k < 144; ++k) if (G[0][k].a.n >= 60 && G[1][k].a.n >= 60) ks.push_back(k);
  std::sort(ks.begin(), ks.end(), [&](int x, int y) { return G[0][x].a.edge() + G[1][x].a.edge() > G[0][y].a.edge() + G[1][y].a.edge(); });
  printf("\n######## T1 GRID pole archetype × origin × flag archetype × chart (144 cells, %zu with n >= 60 on both markets in 2014-21)\n", ks.size());
  printf("Top 10 by 2014-21 edge (avg of NQ, ES), then the unseen 2022-26. Universe 2022-26 edge NQ %+.3f ES %+.3f.\n", U[0].b.edge(), U[1].b.edge());
  std::set<int> top10, bad; Cell T10[2];
  for (size_t q = 0; q < ks.size() && q < 10; ++q) { const int k = ks[q]; top10.insert(k); printf("  %2zu %-38s", q + 1, kname(k).c_str()); pr(G[0][k]); pr(G[1][k]);
    const bool pass = G[0][k].b.n >= 30 && G[1][k].b.n >= 30 && G[0][k].b.edge() >= U[0].b.edge() + 0.10 && G[1][k].b.edge() >= U[1].b.edge() + 0.10; printf(" %s\n", pass ? "2022-26 PASS" : "fail"); }
  for (auto& t : tr) if (top10.count(key(t))) addc(T10[t.mk], t);
  printf("  %-41s", "UNION of the top 10"); pr(T10[0]); pr(T10[1]);
  const bool unionPass = T10[0].b.n >= 30 && T10[1].b.n >= 30 && T10[0].b.edge() >= U[0].b.edge() + 0.10 && T10[1].b.edge() >= U[1].b.edge() + 0.10; printf(" %s\n", unionPass ? "2022-26 PASS (edge part)" : "fail (edge part)");
  printf("  Bottom 10 by 2014-21 edge (the 'bad' cells for the skip variant are those <= -0.15R on BOTH markets):\n");
  for (size_t q = ks.size() >= 10 ? ks.size() - 10 : 0; q < ks.size(); ++q) { const int k = ks[q]; printf("  %2zu %-38s", q + 1, kname(k).c_str()); pr(G[0][k]); pr(G[1][k]); printf("\n"); }
  for (int k : ks) if (G[0][k].a.edge() <= -0.15 && G[1][k].a.edge() <= -0.15) bad.insert(k);
  printf("  bad cells: %zu\n", bad.size());
  { std::vector<Row> K = rows, D = rows; int nk = 0, nd = 0, nb = 0;
    for (auto& t : tr) { if (!book2(rows[t.ri])) continue; nb++; if (!top10.count(key(t))) { K[t.ri].tok[11] = "0"; nk++; } if (bad.count(key(t))) { D[t.ri].tok[11] = "0"; nd++; } }
    printf("  account variants: T1keep drops %d of %d BOOK2 trades, T1skip drops %d\n", nk, nb, nd); write("base", rows); write("T1keep", K); write("T1skip", D); }

  // ── T2 order flow (NQ)
  printf("\n######## T2 ORDER FLOW, NQ flow days — session delta 09:30 → fill, with / against the trade\n");
  for (int set = 0; set < 2; ++set) { printf("  %s\n", set ? "BOOK2 trades" : "every wide15 flag");
    for (int h = 0; h < 3; ++h) { Acc w, a; for (auto& t : tr) { if (t.mk || !t.flowday || (set && !book2(rows[t.ri]))) continue; const bool first = t.date < 20241001;
        if (h == 1 && !first) continue; if (h == 2 && first) continue; (t.delta < 0 ? a : w).add(t); }
      printf("    %-22s with: n %5.0f R %+6.3f edge %+6.3f · against: n %5.0f R %+6.3f edge %+6.3f · against − with %+6.3f\n", h == 0 ? "all flow days" : h == 1 ? "2023-03 → 2024-09" : "2024-10 → (2026)",
        w.n, w.n ? w.r / w.n : 0, w.edge(), a.n, a.n ? a.r / a.n : 0, a.edge(), a.edge() - w.edge()); } }

  // ── T3 models, walk-forward per market and side
  printf("\n######## T3 SHAPE MODELS (walk-forward by year, trained on earlier years from 2010)\n");
  std::vector<double> sg(tr.size(), NAN), tg3(tr.size(), NAN), tg6(tr.size(), NAN), sk(tr.size(), NAN), tk3(tr.size(), NAN), tk6(tr.size(), NAN); std::vector<double> gain(NF, 0.0);
  auto yv = [](double r) { return std::max(-1.5, std::min(5.0, r)); };
  for (int mk = 0; mk < 2; ++mk) for (int side = 1; side >= -1; side -= 2) for (int Y = 2014; Y <= 2026; ++Y) {
    std::vector<int> a, b; for (size_t i = 0; i < tr.size(); ++i) if (tr[i].mk == mk && tr[i].side == side) { if (tr[i].year < Y) a.push_back((int)i); else if (tr[i].year == Y) b.push_back((int)i); }
    if (a.size() < 500 || b.empty()) continue;
    std::vector<const double*> X; std::vector<double> y; for (int i : a) { X.push_back(tr[i].x); y.push_back(yv(tr[i].r)); }
    const ShapeGbm m = trainGbm(X, y, Y == 2026 ? &gain : nullptr);
    // kNN on the path (16 points) + chart minutes / 5
    auto dist = [&](const T& p, const T& q) { double d = 0; for (int j = 0; j < 16; ++j) { const double u = p.path[j] - q.path[j]; d += u * u; } const double u = (p.tf - q.tf) / 5.0; return d + u * u; };
    auto knn = [&](const T& p, int self) { std::vector<std::pair<double, double>> v; v.reserve(a.size()); for (int i : a) if (i != self) v.push_back({dist(p, tr[i]), tr[i].r});
      const size_t K = std::min<size_t>(50, v.size()); std::nth_element(v.begin(), v.begin() + K, v.end()); double s = 0; for (size_t j = 0; j < K; ++j) s += yv(v[j].second); return s / K; };
    std::vector<double> ts, kts; for (int i : a) ts.push_back(m(tr[i].x));
    std::mt19937 g(Y * 7 + side + mk); for (int rep = 0; rep < 600; ++rep) { const int i = a[g() % a.size()]; kts.push_back(knn(tr[i], i)); }   // training score distribution (sampled, leave-one-out)
    std::sort(ts.begin(), ts.end()); std::sort(kts.begin(), kts.end());
    for (int i : b) { sg[i] = m(tr[i].x); tg3[i] = ts[ts.size() / 3]; tg6[i] = ts[ts.size() * 2 / 3]; sk[i] = knn(tr[i], -1); tk3[i] = kts[kts.size() / 3]; tk6[i] = kts[kts.size() * 2 / 3]; } }
  auto sel = [&](const char* name, std::function<bool(size_t)> f) { Cell c[2]; for (size_t i = 0; i < tr.size(); ++i) if (!std::isnan(sg[i]) && f(i)) addc(c[tr[i].mk], tr[i]);
    printf("  %-40s", name); pr(c[0]); pr(c[1]); printf("\n"); return std::array<double, 2>{c[0].b.edge(), c[1].b.edge()}; };
  printf("  (same columns as T1)\n");
  sel("every scored flag", [](size_t) { return true; }); const auto A = sel("A-list (grade 1)", [&](size_t i) { return tr[i].grade == 1; });
  const auto Gt = sel("SHAPE-GBM top third", [&](size_t i) { return sg[i] > tg6[i]; }); sel("SHAPE-GBM bottom third", [&](size_t i) { return sg[i] <= tg3[i]; });
  const auto Kt = sel("PATH-kNN top third", [&](size_t i) { return sk[i] > tk6[i]; }); sel("PATH-kNN bottom third", [&](size_t i) { return sk[i] <= tk3[i]; });
  sel("SHAPE-GBM top third + A-list", [&](size_t i) { return sg[i] > tg6[i] && tr[i].grade == 1; });
  sel("BOOK2 trades", [&](size_t i) { return book2(rows[tr[i].ri]); }); sel("BOOK2 + SHAPE-GBM top two thirds", [&](size_t i) { return book2(rows[tr[i].ri]) && sg[i] > tg3[i]; });
  for (int mdl = 0; mdl < 2; ++mdl) { const auto& v = mdl ? Kt : Gt;
    const bool p = v[0] >= U[0].b.edge() + 0.10 && v[1] >= U[1].b.edge() + 0.10 && v[0] > A[0] && v[1] > A[1];
    printf("  %s 2022-26 edge part: NQ %+.3f vs universe %+.3f / A-list %+.3f · ES %+.3f vs %+.3f / %+.3f → %s\n", mdl ? "PATH-kNN" : "SHAPE-GBM", v[0], U[0].b.edge(), A[0], v[1], U[1].b.edge(), A[1], p ? "PASS (edge part)" : "fail"); }
  { std::vector<int> ord(NF); for (int j = 0; j < NF; ++j) ord[j] = j; std::sort(ord.begin(), ord.end(), [&](int a2, int b2) { return gain[a2] > gain[b2]; }); double gt = 0; for (double x : gain) gt += x;
    printf("  SHAPE-GBM leans on (2026 models, both markets and sides):"); for (int k = 0; k < 8; ++k) printf(" %s %.0f%%%s", FN[ord[k]], gt > 0 ? 100 * gain[ord[k]] / gt : 0, k < 7 ? " ·" : "\n"); }
  // account variants: the model's thirds replace the grade on 4-5m rows (2014+); BOOK2 then takes 1 or 2
  for (int mdl = 0; mdl < 2; ++mdl) { std::vector<Row> R = rows; int n = 0;
    for (size_t i = 0; i < tr.size(); ++i) { if (std::isnan(sg[i]) || tr[i].tf < 4) continue; const double s = mdl ? sk[i] : sg[i], t3 = mdl ? tk3[i] : tg3[i], t6 = mdl ? tk6[i] : tg6[i];
      R[tr[i].ri].tok[6] = s > t6 ? "1" : s > t3 ? "2" : "3"; n++; }
    printf("  account variant %s: %d rows regraded\n", mdl ? "T3knn" : "T3gbm", n); write(mdl ? "T3knn" : "T3gbm", R); }
  // T9 of 2026-10-22_book_more.md: every BOOK2 trade kept, grade 1 (sized 2 micros by `flag_prop_ev … sizing` GRADE) = SHAPE-GBM top third, else 2
  { std::vector<Row> R = rows; int n1 = 0, n2 = 0;
    for (size_t i = 0; i < tr.size(); ++i) { if (std::isnan(sg[i]) || tr[i].tf < 4 || (tr[i].grade != 1 && tr[i].grade != 2)) continue; const bool top = sg[i] > tg6[i]; R[tr[i].ri].tok[6] = top ? "1" : "2"; (top ? n1 : n2)++; }
    printf("  account variant T9size: %d rows grade 1 (SHAPE-GBM top third), %d grade 2\n", n1, n2); write("T9size", R); }
  return 0;
}
