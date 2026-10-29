// 2026-10-23 flag_realism.cpp (preregistered/2026-10-23_realism_plateau.md F1, F2) on the candidate (BOOK2 + gap sizing + the 15m trend
// filter = htf_context VARIANT=FILT_TREND_15m files):
//   F1 the flag-mid limit fills only when the 1m low trades T ticks THROUGH it (queue position), T = 0 must reproduce the candidate
//   F2 entry time of day / weekday: buckets chosen on 2014-19 (mean R < 0 → skipped), judged on 2020-25 by the account
// Variants (column 72 = size) → OUTDIR (env VARIANT=<name> writes only that one, to OUTDIR/tmp_{nq,es}.csv — overwritten each run).
// Usage: flag_realism CAND_NQ.csv CAND_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr; bool fomc, cpi, nfp; std::vector<int> pos, TM; std::vector<double> O, H, L, C; };
struct Sim { bool ok = false; int em = 0, xm = 0; double en = 0, sp = 0, tg = 0, xp = 0, net = 0, r = 0; };
static std::string f2(double v) { char b[64]; snprintf(b, sizeof b, "%.2f", v); return b; }
static std::string f4(double v) { char b[64]; snprintf(b, sizeof b, "%.4f", v); return b; }
static int weekday(int ymd) { int y = ymd / 10000, m = ymd / 100 % 100, d = ymd % 100; if (m < 3) { m += 12; y--; }   // 0 = Monday
  const int h = (d + 13 * (m + 1) / 5 + y + y / 4 - y / 100 + y / 400) % 7; return (h + 5) % 7; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_realism CAND_NQ.csv CAND_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(ff, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
    build_days(s, fomc, vix); load_events("data/edge_lab/cache", s);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{d.atr, d.fomc, d.cpi, d.nfp, std::vector<int>(400, -1), {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 72) continue; rows.push_back(std::move(r)); } }
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  auto traded = [&](const Row& r) { return r.a[11] > 0.5 && book2(r); };
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.O[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  // ── F1: page fill loop (lib/flags.ts, as rebuilt in flag_engulf.cpp) with a trade-through requirement of T ticks
  // configs: {entry ticks through, target ticks through, stop slip ticks} — 0-2 = F1 (2026-10-23_realism_plateau), 3-5 = G1
  // (2026-10-23_exits_news_uncertainty): target needs 1 tick through / stop slips 2 ticks / all three pessimistic
  // 6-10: H6 (2026-10-23_live_planning) LIVE fills with target = entry + k × risk, k = KR[T - 6]
  const int CFG[11][3] = {{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {0, 1, 0}, {0, 0, 2}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}, {1, 1, 2}};
  const double KR[5] = {0.33, 0.5, 0.75, 1.0, 1.5};
  const int NTT = 11; std::vector<std::array<Sim, NTT>> G(rows.size()); std::vector<double> TW(rows.size(), NAN);
  int nchk = 0, nbad = 0;
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r)) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
    const int sd = (int)r.a[1], reg = (int)r.a[3], end = (int)r.a[4]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL, cost = r.mk ? 0.5 : 1.5;
    if (!(M > 0) || reg < 930 || reg >= 1316) continue;
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    const int kr0 = z.pos[reg - 930], ke0 = end >= 930 && end < 1320 ? z.pos[end - 930] : -1; if (kr0 < 0) continue;
    const double fl0 = sd > 0 ? r.a[10] : -r.a[10], lim = (PH + fl0) / 2;
    for (int T = 0; T < NTT; ++T) { Sim& s = G[i][T]; double fl = fl0; int kf = -1; double price = NAN;
      for (int k = kr0; k < (int)z.C.size(); ++k) { if (z.TM[k] >= 1290) break; if (ke0 >= 0 && k > ke0) break; if (ke0 >= 0 && k == ke0 && hi(k) > PH) break;
        if (lo(k) <= lim - 0.25 * CFG[T][0]) { price = std::min(sd * z.O[k], lim); kf = k; break; } fl = std::min(fl, lo(k)); }
      if (kf < 0) continue; s.sp = fl - 0.1 * z.atr; s.tg = T >= 6 ? price + KR[T - 6] * (price - s.sp) : PH + M; s.en = price; if (!(price - s.sp > 0.5) || !(s.tg > price)) continue;
      s.em = z.TM[kf]; s.xm = s.em; s.xp = NAN;
      const double tgT = s.tg + 0.25 * CFG[T][1], slp = 0.25 * CFG[T][2];
      auto stopFill = [&](int k) { return slp > 0 ? std::max(lo(k), s.sp - slp) : s.sp; };   // slip up to slp, never past the bar's low
      if (lo(kf) <= s.sp) s.xp = stopFill(kf);
      else for (int k = kf + 1; k < (int)z.C.size(); ++k) { s.xm = z.TM[k]; if (lo(k) <= s.sp) { s.xp = stopFill(k); break; } if (hi(k) >= tgT) { s.xp = s.tg; break; } if (z.TM[k] >= 1315) { s.xp = sd * z.O[k]; break; } }
      if (std::isnan(s.xp)) s.xp = sd * z.C.back(); s.net = s.xp - s.en - cost; s.r = s.net / (s.en - s.sp); s.ok = true; }
    const Sim& s0 = G[i][0]; nchk++;
    if (!s0.ok || s0.em != (int)r.a[12] || s0.xm != (int)r.a[13] || std::fabs(sd * s0.xp - r.a[17]) > 0.011 || std::fabs(s0.r - r.a[18]) > 0.01 * std::max(1.0, std::fabs(r.a[18]))) { nbad++;
      if (std::getenv("F1DEBUG") && nbad <= 8) printf("  mismatch %d %s side %d: export em %d xm %d x %.2f R %.4f · sim ok %d em %d xm %d x %.2f R %.4f\n", (int)r.a[0], r.mk ? "ES" : "NQ", sd,
        (int)r.a[12], (int)r.a[13], r.a[17], r.a[18], s0.ok, s0.em, s0.xm, sd * s0.xp, s0.r); }
    TW[i] = twin(z, sd, (int)r.a[12], std::fabs(r.a[14] - r.a[15]), std::fabs(r.a[16] - r.a[14]), cost, (unsigned)(r.a[0] * 13 + r.a[12] * 7 + r.a[2] + r.mk * 99991)); }
  printf("F1 check: T = 0 reproduces the candidate's trades (fill minute, exit minute, exit price, R within 1 %%) on %d of %d\n", nchk - nbad, nchk);
  for (int T = 1; T < 3; ++T) { double n[2][2] = {}, d[2][2] = {}, rd[2][2] = {}, rk[2][2] = {}, nk[2][2] = {}, rl[2][2] = {};
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!G[i][0].ok || r.a[0] < 20140101) continue; const int h = r.a[0] >= 20200101;
      n[r.mk][h]++; if (!G[i][T].ok) { d[r.mk][h]++; rd[r.mk][h] += G[i][0].r; } else { nk[r.mk][h]++; rk[r.mk][h] += G[i][0].r; rl[r.mk][h] += G[i][T].r; } }
    printf("F1 T = %d tick(s) through: ", T);
    for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) printf("%s %s: %.0f%% touch-only (mean R %+.3f) · kept mean R %+.3f → %+.3f | ", mk ? "ES" : "NQ", h ? "2020-26" : "2014-19",
      100 * d[mk][h] / std::max(1.0, n[mk][h]), rd[mk][h] / std::max(1.0, d[mk][h]), rk[mk][h] / std::max(1.0, nk[mk][h]), rl[mk][h] / std::max(1.0, nk[mk][h]));
    printf("\n"); }
  // ── F2: time-of-day and weekday buckets (trade level, candidate R)
  auto tod = [](int em) { return em < 990 ? 0 : em < 1080 ? 1 : em < 1170 ? 2 : 3; };
  const char* TODN[4] = {"09:30-10:30", "10:30-12:00", "12:00-13:30", "13:30-15:00"}; const char* DOWN[5] = {"Mon", "Tue", "Wed", "Thu", "Fri"};
  auto bucketTable = [&](const char* title, int nb, const char* const* names, std::function<int(const Row&)> b, std::vector<int>& skip) {
    struct A { double n = 0, r = 0, tw = 0; } g[8][2][2], pool[8];
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r) || r.a[0] < 20140101) continue; const int k = b(r); if (k < 0 || k >= nb) continue; const int h = r.a[0] >= 20200101;
      A& a = g[k][r.mk][h]; a.n++; a.r += r.a[18]; if (std::isfinite(TW[i])) a.tw += TW[i]; else a.tw += r.a[18];
      if (!h) { pool[k].n++; pool[k].r += r.a[18]; } }
    printf("\n%s — mean R / edge over twins (n), 2014-19 | 2020-26\n", title);
    for (int k = 0; k < nb; ++k) { printf("  %-12s", names[k]);
      for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { const A& a = g[k][mk][h]; printf(" %s %s %+5.2f/%+5.2f (%4.0f) |", mk ? "ES" : "NQ", h ? "late " : "early", a.n ? a.r / a.n : 0, a.n ? (a.r - a.tw) / a.n : 0, a.n); }
      const double pm = pool[k].n ? pool[k].r / pool[k].n : 0; if (pool[k].n && pm < 0) skip.push_back(k); printf(" pooled 2014-19 %+5.3f%s\n", pm, pool[k].n && pm < 0 ? "  → SKIP" : ""); } };
  std::vector<int> skipTod, skipDow;
  bucketTable("F2 entry time (ET)", 4, TODN, [&](const Row& r) { return tod((int)r.a[12]); }, skipTod);
  bucketTable("F2 weekday", 5, DOWN, [&](const Row& r) { return weekday((int)r.a[0]); }, skipDow);
  // ── G3: news days (trade level)
  { struct A { double n = 0, r = 0; } g[4][2][2];   // 0 FOMC all, 1 FOMC entries >= 13:30, 2 CPI or NFP, 3 none of these
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!traded(r) || r.a[0] < 20140101) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue;
      const DB& z = it->second; const int h = r.a[0] >= 20200101; const double R = r.a[18];
      if (z.fomc) { g[0][r.mk][h].n++; g[0][r.mk][h].r += R; if ((int)r.a[12] >= 1170) { g[1][r.mk][h].n++; g[1][r.mk][h].r += R; } }
      if (z.cpi || z.nfp) { g[2][r.mk][h].n++; g[2][r.mk][h].r += R; }
      if (!z.fomc && !z.cpi && !z.nfp) { g[3][r.mk][h].n++; g[3][r.mk][h].r += R; } }
    const char* NN[4] = {"FOMC day", "FOMC day, entry >= 13:30", "CPI or NFP day", "no news"};
    printf("\nG3 news days — mean R (n), 2014-19 | 2020-26\n");
    for (int k = 0; k < 4; ++k) { printf("  %-26s", NN[k]); double pn = 0, pr = 0;
      for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) { const auto& a = g[k][mk][h]; printf(" %s %s %+5.2f (%4.0f) |", mk ? "ES" : "NQ", h ? "late " : "early", a.n ? a.r / a.n : 0, a.n); if (!h) { pn += a.n; pr += a.r; } }
      printf(" pooled 2014-19 %+5.3f\n", pn ? pr / pn : 0); } }
  // ── variants
  auto write = [&](const std::string& name, std::function<void(size_t, std::vector<std::string>&)> mod) {
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str());
      for (size_t i = 0; i < rows.size(); ++i) { if (rows[i].mk != mk) continue; std::vector<std::string> t = rows[i].tok; if (traded(rows[i])) mod(i, t);
        for (size_t j = 0; j < t.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", t[j].c_str()); fprintf(fo, "\n"); }
      fclose(fo); }
    printf("  wrote %s\n", name.c_str()); };
  auto setSim = [&](size_t i, std::vector<std::string>& t, int T) { const Sim& s = G[i][T]; if (!s.ok) { t[11] = "0"; return; } const int sd = (int)rows[i].a[1];
    t[12] = std::to_string(s.em); t[13] = std::to_string(s.xm); t[14] = f2(sd * s.en); t[15] = f2(sd * s.sp); t[16] = f2(sd * s.tg); t[17] = f2(sd * s.xp); t[18] = f4(s.r); t[19] = f4(s.net); };
  auto in = [](const std::vector<int>& v, int k) { return std::find(v.begin(), v.end(), k) != v.end(); };
  printf("\nvariants (skip time buckets:"); for (int k : skipTod) printf(" %s", TODN[k]); printf(" · skip weekdays:"); for (int k : skipDow) printf(" %s", DOWN[k]); printf(")\n");
  write("F0cand", [](size_t, std::vector<std::string>&) {});
  write("F1t0", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 0); });
  write("F1t1", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 1); });
  write("F1t2", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 2); });
  write("F2tod", [&](size_t i, std::vector<std::string>& t) { if (in(skipTod, tod((int)rows[i].a[12]))) t[11] = "0"; });
  write("G1tgt", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 3); });
  write("G1slip", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 4); });
  write("G1all", [&](size_t i, std::vector<std::string>& t) { setSim(i, t, 5); });
  auto dayOf = [&](size_t i) -> const DB* { auto it = days[rows[i].mk].find((int)rows[i].a[0]); return it == days[rows[i].mk].end() ? nullptr : &it->second; };
  for (int t = 6; t < 11; ++t) { char nm[16]; snprintf(nm, sizeof nm, "H6k%03d", (int)std::lround(KR[t - 6] * 100)); write(nm, [&, t](size_t i, std::vector<std::string>& tk) { setSim(i, tk, t); }); }
  write("G3fomc", [&](size_t i, std::vector<std::string>& t) { const DB* z = dayOf(i); if (z && z->fomc) t[11] = "0"; });
  write("G3fomcpm", [&](size_t i, std::vector<std::string>& t) { const DB* z = dayOf(i); if (z && z->fomc && (int)rows[i].a[12] >= 1170) t[11] = "0"; });
  write("G3data", [&](size_t i, std::vector<std::string>& t) { const DB* z = dayOf(i); if (z && (z->cpi || z->nfp)) t[11] = "0"; });
  write("F2dow", [&](size_t i, std::vector<std::string>& t) { if (in(skipDow, weekday((int)rows[i].a[0]))) t[11] = "0"; });
  return 0;
}
