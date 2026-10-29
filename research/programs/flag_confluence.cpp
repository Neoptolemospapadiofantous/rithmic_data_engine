// 2026-10-23 flag_confluence.cpp (preregistered/2026-10-23_flag_confluence.md; founder: "what about using our flag system with our
// strategies as confluence" → "test all matrices"). Strategy entries come from edge_lab --trades files (fixed book settings):
// TRADES_DIR/cf_{cas,grt,g10,wkl}_{nq,es}.trades.csv (date, entry_epoch = ET wall-clock epoch, dir, net_pts, ...).
//   A  every wide15 flag trade (tf 1-5 + BOOK2) × strategy signal earlier that day: AGREE / AGAINST / NONE — R, edge over twins, shuffle p
//   B  strategy trades × a BOOK2 / any wide15 flag filled earlier that day: AGREE / AGAINST / NONE — net pts
//   C  account variants on the candidate files (column 72 = size) → OUTDIR (env VARIANT=<name> → OUTDIR/tmp_{nq,es}.csv)
// Usage: flag_confluence W15_NQ W15_ES CAND_NQ CAND_ES TRADES_DIR OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { std::vector<int> pos, TM; std::vector<double> O, H, L, C; };
struct Sig { int tm, dir; double net; };
static const int NS = 4; static const char* SN[NS + 1] = {"CAS", "GRT", "G10", "WKL", "ANY"}; static const char* SF[NS] = {"cas", "grt", "g10", "wkl"};
static std::vector<Row> readRows(const char* fn, int mk, size_t need, std::string* header) {
  std::vector<Row> v; std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln); if (header) *header = ln;
  while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
    if (r.a.size() >= need) v.push_back(std::move(r)); }
  return v; }
int main(int argc, char** argv) {
  if (argc < 7) { fprintf(stderr, "usage: flag_confluence W15_NQ W15_ES CAND_NQ CAND_ES TRADES_DIR OUTDIR\n"); return 1; }
  const std::string tdir = argv[5], out = argv[6]; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue; DB z{std::vector<int>(400, -1), {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); } }
  // strategy signals: per market, strategy, date → entries (tday minute, dir, net pts)
  std::map<int, std::vector<Sig>> sig[2][NS];
  for (int mk = 0; mk < 2; ++mk) for (int k = 0; k < NS; ++k) { const std::string fn = tdir + "/cf_" + SF[k] + (mk ? "_es" : "_nq") + ".trades.csv"; std::ifstream f(fn); if (!f) { perror(fn.c_str()); return 1; }
    std::string ln; std::getline(f, ln); while (std::getline(f, ln)) { int d; long long ep, xp; int dir; double g, net; if (sscanf(ln.c_str(), "%d,%lld,%lld,%d,%lf,%lf", &d, &ep, &xp, &dir, &g, &net) != 6) continue;
      const int clock = (int)((ep % 86400) / 60); const int tday = (clock - 1080 + 1440) % 1440; sig[mk][k][d].push_back({tday, dir, net}); } }
  auto state = [&](int mk, int k, int date, int tm, int side) {   // 1 agree, -1 against, 0 none (k == NS: any; agree wins over against only if no against? → report both separately)
    int ag = 0, an = 0; for (int kk = (k == NS ? 0 : k); kk < (k == NS ? NS : k + 1); ++kk) { auto it = sig[mk][kk].find(date); if (it == sig[mk][kk].end()) continue;
      for (auto& s : it->second) if (s.tm < tm) { if (s.dir == side) ag = 1; else if (s.dir == -side) an = 1; } }
    return ag && !an ? 1 : an && !ag ? -1 : ag && an ? 2 : 0; };   // 2 = mixed (ANY only)
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.O[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  // ── A
  std::vector<Row> W; { auto a = readRows(argv[1], 0, 71, nullptr), b = readRows(argv[2], 1, 71, nullptr); W = a; W.insert(W.end(), b.begin(), b.end()); }
  std::vector<double> TW(W.size(), NAN); std::vector<int> use(W.size(), 0);
  for (size_t i = 0; i < W.size(); ++i) { const Row& r = W[i]; if (r.a[11] < 0.5 || r.a[0] < 20140101) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue;
    use[i] = 1; TW[i] = twin(it->second, (int)r.a[1], (int)r.a[12], std::fabs(r.a[14] - r.a[15]), std::fabs(r.a[16] - r.a[14]), r.mk ? 0.5 : 1.5, (unsigned)(r.a[0] * 13 + r.a[12] * 7 + r.a[2] + r.mk * 99991)); }
  std::mt19937 rng(20261023);
  printf("MATRIX A — flags given a strategy signal earlier that day. Cell = mean net R / edge over twins (n). early = 2014-19, late = 2020-26.\n");
  printf("REAL = AGREE beats NONE in R and edge in both halves on both markets.\n");
  const char* SETN[6] = {"1m", "2m", "3m", "4m", "5m", "BOOK2"};
  for (int set = 0; set < 6; ++set) for (int k = 0; k <= NS; ++k) {
    auto inSet = [&](const Row& r) { return set == 5 ? book2(r) : (int)r.a[2] == set + 1; };
    struct A { double n = 0, r = 0, e = 0; } g[3][2][2];   // state 0 agree,1 none,2 against
    std::vector<size_t> idx; std::vector<int> lab;
    for (size_t i = 0; i < W.size(); ++i) { const Row& r = W[i]; if (!use[i] || !inSet(r) || std::isnan(TW[i])) continue; const int st = state(r.mk, k, (int)r.a[0], (int)r.a[12], (int)r.a[1]); if (st == 2) continue;
      const int c = st == 1 ? 0 : st == 0 ? 1 : 2; A& a = g[c][r.mk][r.a[0] >= 20200101]; a.n++; a.r += r.a[18]; a.e += r.a[18] - TW[i]; if (c != 2) { idx.push_back(i); lab.push_back(c == 0); } }
    // shuffle p for AGREE − NONE (R), within year × market
    auto diff = [&](const std::vector<int>& L) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (size_t q = 0; q < idx.size(); ++q) { const double v = W[idx[q]].a[18]; if (L[q]) { s1 += v; n1++; } else { s0 += v; n0++; } } return (n1 ? s1 / n1 : 0) - (n0 ? s0 / n0 : 0); };
    std::map<int, std::vector<size_t>> grp; for (size_t q = 0; q < idx.size(); ++q) grp[(int)W[idx[q]].a[0] / 10000 * 2 + W[idx[q]].mk].push_back(q);
    const double obs = diff(lab); int ge = 0;
    for (int rep = 0; rep < 1000; ++rep) { std::vector<int> sh = lab; for (auto& [key, qs] : grp) { std::vector<int> v; for (size_t q : qs) v.push_back(lab[q]); std::shuffle(v.begin(), v.end(), rng); for (size_t j = 0; j < qs.size(); ++j) sh[qs[j]] = v[j]; } ge += diff(sh) >= obs; }
    bool real = true; for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { const A &y = g[0][mk][h], &n_ = g[1][mk][h]; if (!(y.n >= 10 && n_.n >= 10 && y.r / y.n > n_.r / n_.n && y.e / y.n > n_.e / n_.n)) real = false; }
    printf("%-5s %-3s |", SETN[set], SN[k]);
    for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { printf(" %s %s", mk ? "ES" : "NQ", h ? "late " : "early");
      for (int c = 0; c < 3; ++c) { const A& a = g[c][mk][h]; printf(" %s %+5.2f/%+5.2f(%4.0f)", c == 0 ? "AG" : c == 1 ? "NO" : "AN", a.n ? a.r / a.n : 0, a.n ? a.e / a.n : 0, a.n); } printf(" |"); }
    printf(" AG-NO %+.3f p=%.3f%s\n", obs, ge / 1000.0, real ? "  REAL" : ""); }
  // ── B
  printf("\nMATRIX B — strategy trades given a flag FILLED earlier that day on the same / other side. Cell = mean net pts (n).\n");
  for (int fs = 0; fs < 2; ++fs) for (int k = 0; k < NS; ++k) {
    struct A { double n = 0, s = 0; } g[3][2][2];
    std::map<int, std::vector<std::pair<int, int>>> fl[2];   // date → (fill minute, side)
    for (size_t i = 0; i < W.size(); ++i) { const Row& r = W[i]; if (r.a[11] < 0.5 || (fs == 0 && !book2(r))) continue; fl[r.mk][(int)r.a[0]].push_back({(int)r.a[12], (int)r.a[1]}); }
    for (int mk = 0; mk < 2; ++mk) for (auto& [d, v] : sig[mk][k]) { if (d < 20140101) continue; for (auto& s : v) { int ag = 0, an = 0; auto it = fl[mk].find(d);
        if (it != fl[mk].end()) for (auto& [tm, sd] : it->second) if (tm < s.tm) { if (sd == s.dir) ag = 1; else an = 1; }
        if (ag && an) continue; const int c = ag ? 0 : an ? 2 : 1; A& a = g[c][mk][d >= 20200101]; a.n++; a.s += s.net; } }
    printf("%-3s × %-10s |", SN[k], fs ? "any flag" : "BOOK2 flag");
    for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { printf(" %s %s", mk ? "ES" : "NQ", h ? "late " : "early"); for (int c = 0; c < 3; ++c) { const A& a = g[c][mk][h]; printf(" %s %+6.2f(%4.0f)", c == 0 ? "AG" : c == 1 ? "NO" : "AN", a.n ? a.s / a.n : 0, a.n); } printf(" |"); }
    printf("\n"); }
  // ── C (candidate files)
  std::string header; std::vector<Row> Cn = readRows(argv[3], 0, 72, &header), Ce = readRows(argv[4], 1, 72, nullptr);
  auto traded = [&](const Row& r) { return r.a[11] > 0.5 && book2(r); };
  { struct A { double n = 0, r = 0; } g[3][2]; for (auto* V : {&Cn, &Ce}) for (auto& r : *V) { if (!traded(r) || r.a[0] < 20140101) continue; const int st = state(r.mk, NS, (int)r.a[0], (int)r.a[12], (int)r.a[1]);
      if (st == 2) continue; const int c = st == 1 ? 0 : st == 0 ? 1 : 2; g[c][r.a[0] >= 20200101].n++; g[c][r.a[0] >= 20200101].r += r.a[18]; }
    printf("\nCandidate trades by ANY signal (pooled NQ+ES): early AG %+.3f (%0.f) NO %+.3f (%0.f) AN %+.3f (%0.f) | late AG %+.3f (%0.f) NO %+.3f (%0.f) AN %+.3f (%0.f)\n",
      g[0][0].r / std::max(1.0, g[0][0].n), g[0][0].n, g[1][0].r / std::max(1.0, g[1][0].n), g[1][0].n, g[2][0].r / std::max(1.0, g[2][0].n), g[2][0].n,
      g[0][1].r / std::max(1.0, g[0][1].n), g[0][1].n, g[1][1].r / std::max(1.0, g[1][1].n), g[1][1].n, g[2][1].r / std::max(1.0, g[2][1].n), g[2][1].n); }
  auto write = [&](const std::string& name, std::function<void(const Row&, std::vector<std::string>&, int)> mod) {
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str());
      for (auto& r : (mk ? Ce : Cn)) { std::vector<std::string> t = r.tok; if (traded(r)) mod(r, t, state(r.mk, NS, (int)r.a[0], (int)r.a[12], (int)r.a[1])); for (size_t j = 0; j < t.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", t[j].c_str()); fprintf(fo, "\n"); }
      fclose(fo); }
    printf("  wrote %s\n", name.c_str()); };
  printf("\nvariants:\n");
  write("CF0cand", [](const Row&, std::vector<std::string>&, int) {});
  write("CF_skipAgainst", [](const Row&, std::vector<std::string>& t, int st) { if (st == -1) t[11] = "0"; });
  write("CF_agree2", [](const Row&, std::vector<std::string>& t, int st) { if (st == 1) t[71] = "2"; });
  write("CF_onlyAgree", [](const Row&, std::vector<std::string>& t, int st) { if (st != 1) t[11] = "0"; });
  return 0;
}
