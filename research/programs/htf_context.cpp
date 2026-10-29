// 2026-10-22 htf_context.cpp (founder: "create a plan on how to test this I think you are making a mistake" → "do all"). Runs
// research/preregistered/2026-10-22_htf_context.md exactly as written before the run: the higher-timeframe picture (15m, 30m, 1h, 4h, daily)
// from HTF bars that closed BEFORE each 5m flag's pole started — FLAG (a bigger flag in progress: nesting), TREND (close vs EMA20) and ROOM
// (the 5m target inside the HTF range) — on every 4-5m wide15 flag (set A, trade level, edge over post-registration twins, shuffle test)
// and as SIZE / FILTER / STRICT uses on BOOK2 (set B; variants with a size column for `flag_prop_ev … mtm`).
// Usage: htf_context W15_NQ.csv W15_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct HB { int di, tend; double o, h, l, c; };                 // an HTF bar: trading-day index, end minute (tday; daily = 1320)
struct DB { int di; double atr, pc, open; std::vector<int> pos, TM; std::vector<double> O, H, L, C; };
static const int NT = 5; static const char* TN[NT] = {"15m", "30m", "1h", "4h", "daily"};
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: htf_context W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2]; std::vector<HB> htf[2][NT]; std::vector<double> ema[2][NT];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    int di = 0;
    for (auto& d : s.days) { ++di;
      // intraday HTF buckets over the whole trading day (all bars, also on days not traded)
      for (int t = 0; t < 4; ++t) { const int w = t == 0 ? 15 : t == 1 ? 30 : t == 2 ? 60 : 240; int cur = -1;
        for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); const int b = tm / w;
          if (b != cur) { htf[mk][t].push_back({di, (b + 1) * w, s.o[i], s.h[i], s.l[i], s.c[i]}); cur = b; }
          else { auto& x = htf[mk][t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } } }
      if (d.ok && std::isfinite(d.rh)) htf[mk][4].push_back({di, 1320, d.ro, d.rh, d.rl, d.rc});
      if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{di, d.atr, d.pc, d.ro, std::vector<int>(400, -1), {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    for (int t = 0; t < NT; ++t) { double e = NAN; for (auto& b : htf[mk][t]) { e = std::isnan(e) ? b.c : e + (2.0 / 21) * (b.c - e); ema[mk][t].push_back(e); } }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln + ",size";
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; r.tok.resize(71); r.tok.push_back("1"); rows.push_back(std::move(r)); } }
  auto setA = [](const Row& r) { return r.a[11] > 0.5 && (int)r.a[2] >= 4; };
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return r.a[11] > 0.5 && (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.O[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  // features: [t][0] FLAG (-1 disagree, 0 none, 1 agree), [t][1] TREND (0/1), [t][2] ROOM (0/1); -9 = unknown
  struct Fx { int f[NT][3]; double tw = NAN; bool gap = false; bool ok = false; };
  std::vector<Fx> X(rows.size()); int miss = 0;
  for (size_t i = 0; i < rows.size(); ++i) { Fx& x = X[i]; for (auto& a : x.f) for (int& v : a) v = -9; const Row& r = rows[i]; if (!setA(r)) continue;
    auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) { miss++; continue; } const DB& z = it->second;
    const int sd = (int)r.a[1], em = (int)r.a[12], reg = (int)r.a[3]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL;
    auto hi1 = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo1 = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    const int e = em >= 930 && em < 1320 ? z.pos[em - 930] : -1, rg = reg >= 930 && reg < 1320 ? z.pos[reg - 930] : -1; if (e < 0 || rg < 0 || !(M > 0)) { miss++; continue; }
    int top = -1; for (int k = std::min(rg, e) - 1; k >= 0 && k >= rg - 3 * (int)r.a[2] - 90; --k) if (std::fabs(hi1(k) - PH) < 1e-6) { top = k; break; }
    int low = -1; if (top >= 0) for (int k = top; k >= 0 && k >= top - 240; --k) if (std::fabs(lo1(k) - PL) < 1e-6) { low = k; break; }
    if (low < 0) { miss++; continue; }
    const int ps = z.TM[low]; x.ok = true; x.gap = std::isfinite(z.pc) && sd * (z.open - z.pc) > 0;
    const double tgt = PH + M;
    for (int t = 0; t < NT; ++t) { const auto& B = htf[r.mk][t];
      // last HTF bar closed before the pole start: day < z.di, or same day and tend <= ps (daily: day < z.di)
      int lo_ = 0, hi_ = (int)B.size() - 1, last = -1;
      while (lo_ <= hi_) { const int mid = (lo_ + hi_) / 2; const bool closed = B[mid].di < z.di || (t < 4 && B[mid].di == z.di && B[mid].tend <= ps); if (closed) { last = mid; lo_ = mid + 1; } else hi_ = mid - 1; }
      if (last < 20) continue;
      auto H = [&](int j, int s2) { return s2 > 0 ? B[j].h : -B[j].l; }; auto L = [&](int j, int s2) { return s2 > 0 ? B[j].l : -B[j].h; };
      auto flagIn = [&](int s2) { int jH = last; for (int j = last; j > last - 12; --j) if (H(j, s2) > H(jH, s2)) jH = j;
        double pl = 1e18; for (int j = jH; j >= jH - 12 && j >= 0; --j) pl = std::min(pl, L(j, s2)); const double pole = H(jH, s2) - pl;
        double avg = 0; for (int j = last - 19; j <= last; ++j) avg += B[j].h - B[j].l; avg /= 20;
        const int nAfter = last - jH; double la = 1e18; for (int j = jH + 1; j <= last; ++j) la = std::min(la, L(j, s2));
        const double depth = nAfter > 0 ? (H(jH, s2) - la) / pole : 0; const double c = s2 * B[last].c;
        return pole >= 2.5 * avg && nAfter >= 1 && nAfter <= 8 && depth >= 0.05 && depth <= 0.62 && c < H(jH, s2); };
      const bool ag = flagIn(sd), dis = flagIn(-sd); x.f[t][0] = ag ? 1 : dis ? -1 : 0;
      x.f[t][1] = sd * (B[last].c - ema[r.mk][t][last]) > 0;
      double hh = -1e18; for (int j = last - 19; j <= last; ++j) hh = std::max(hh, H(j, sd)); x.f[t][2] = tgt <= hh; }
    const double risk = std::fabs(r.a[14] - r.a[15]), tgd = std::fabs(r.a[16] - r.a[14]);
    x.tw = twin(z, sd, em, risk, tgd, r.mk ? 0.5 : 1.5, (unsigned)(r.a[0] * 13 + em * 7 + r.a[2] + r.mk * 99991)); }
  printf("set A (4-5m wide15 flags with a trade): %zu rows read, %d not located\n", rows.size(), miss);
  // ── favourable sides on 2014-19 (set B, pooled): FLAG fixed = agree; TREND / ROOM chosen
  int fav[NT][3];
  for (int t = 0; t < NT; ++t) { fav[t][0] = 1; for (int k = 1; k < 3; ++k) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0;
      for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const int v = X[i].f[t][k]; if (v < 0 || !book2(r) || r.a[0] < 20140101 || r.a[0] >= 20200101) continue; (v ? s1 : s0) += r.a[18]; (v ? n1 : n0)++; }
      fav[t][k] = (n1 ? s1 / n1 : -9) >= (n0 ? s0 / n0 : -9) ? 1 : 0; } }
  auto isFav = [&](size_t i, int t, int k) { const int v = X[i].f[t][k]; if (v == -9) return -1; return k == 0 ? (v == 1 ? 1 : 0) : (v == fav[t][k] ? 1 : 0); };
  auto isBad = [&](size_t i, int t, int k) { const int v = X[i].f[t][k]; if (v == -9) return false; return k == 0 ? v == -1 : v != fav[t][k]; };
  const char* KN[3] = {"FLAG (nested in a bigger flag)", "TREND at the pole's start", "ROOM (target inside the HTF range)"};
  // ── set A tables + shuffle test
  std::mt19937 rng(20261022);
  printf("\nSet A: favourable vs rest — n, mean net R, edge over twins; NQ 2014-19 | NQ 2020-26 | ES 2014-19 | ES 2020-26; shuffle p (2014-26, 1,000 shuffles within year × market)\n");
  for (int t = 0; t < NT; ++t) for (int k = 0; k < 3; ++k) {
    struct A { double n = 0, r = 0, tw = 0; void add(double rr, double t2) { if (std::isnan(t2)) return; n++; r += rr; tw += t2; } } g[2][2][2];
    std::map<int, std::vector<size_t>> grp; std::vector<size_t> all;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!setA(r) || r.a[0] < 20140101) continue; const int v = isFav(i, t, k); if (v < 0) continue;
      g[r.mk][r.a[0] >= 20200101][v].add(r.a[18], X[i].tw); grp[(int)r.a[0] / 10000 * 2 + r.mk].push_back(i); all.push_back(i); }
    auto diff = [&](const std::vector<int>& lab) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (size_t q = 0; q < all.size(); ++q) { const double rr = rows[all[q]].a[18]; if (lab[q]) { s1 += rr; n1++; } else { s0 += rr; n0++; } }
      return (n1 ? s1 / n1 : 0) - (n0 ? s0 / n0 : 0); };
    std::vector<int> lab(all.size()); for (size_t q = 0; q < all.size(); ++q) lab[q] = isFav(all[q], t, k);
    const double obs = diff(lab); int ge = 0;
    std::map<size_t, size_t> where; for (size_t q = 0; q < all.size(); ++q) where[all[q]] = q;
    const int NREP = std::getenv("NOSHUFFLE") ? 0 : 1000;   // NOSHUFFLE: variant writing only (the tables then show p=0.000)
    for (int rep = 0; rep < NREP; ++rep) { std::vector<int> sh = lab;
      for (auto& [key, ix] : grp) { std::vector<int> v; for (size_t i : ix) v.push_back(lab[where[i]]); std::shuffle(v.begin(), v.end(), rng); for (size_t q = 0; q < ix.size(); ++q) sh[where[ix[q]]] = v[q]; }
      ge += diff(sh) >= obs; }
    printf("  %-5s %-36s fav=%s |", TN[t], KN[k], k == 0 ? "agree" : fav[t][k] ? "yes" : "no ");
    for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { auto& y = g[mk][h][1]; auto& n_ = g[mk][h][0];
      printf(" %+5.2f/%+5.2f (%4.0f) vs %+5.2f (%4.0f) |", y.n ? y.r / y.n : 0, y.n ? (y.r - y.tw) / y.n : 0, y.n, n_.n ? n_.r / n_.n : 0, n_.n); }
    printf(" diff %+6.3f p=%.3f\n", obs, ge / 1000.0); }
  printf("  (cells: favourable R / edge (n) vs rest R (n))\n");
  // ── variants (set B): CAND = gap sizing; SIZE / FILTER / STRICT per feature
  auto write = [&](const std::string& name, std::function<int(size_t)> size) {
    // 2026-10-22 (preregistered/2026-10-22_external_ideas.md): env VARIANT=<name> writes only that variant, to OUTDIR/tmp_{nq,es}.csv
    // (overwritten on every run — the disk is ~100 % full and nothing is deleted)
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    int n2 = 0, n0 = 0;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str());
      for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (r.mk != mk) continue; std::vector<std::string> tk = r.tok; if (book2(r)) { const int sz = size(i); if (sz == 0) { tk[11] = "0"; n0++; } else { tk[71] = std::to_string(sz); n2 += sz == 2; } }
        for (size_t j = 0; j < tk.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", tk[j].c_str()); fprintf(fo, "\n"); }
      fclose(fo); }
    printf("  %-14s %5d BOOK2 trades at 2 micros, %5d dropped\n", name.c_str(), n2, n0); };
  printf("\nvariants:\n");
  write("FLAT", [](size_t) { return 1; });
  write("CAND", [&](size_t i) { return X[i].gap ? 2 : 1; });
  const char* KS[3] = {"FLAG", "TREND", "ROOM"};
  for (int t = 0; t < NT; ++t) for (int k = 0; k < 3; ++k) { const std::string nm = std::string(KS[k]) + "_" + TN[t];
    write("SIZE_" + nm, [&, t, k](size_t i) { return isFav(i, t, k) == 1 ? 2 : 1; });
    write("FILT_" + nm, [&, t, k](size_t i) { return isBad(i, t, k) ? 0 : X[i].gap ? 2 : 1; });
    write("STRICT_" + nm, [&, t, k](size_t i) { return isFav(i, t, k) == 1 ? (X[i].gap ? 2 : 1) : isFav(i, t, k) == 0 ? 0 : (X[i].gap ? 2 : 1); }); }
  return 0;
}
