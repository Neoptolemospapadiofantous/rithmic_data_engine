// 2026-10-22 regime_size.cpp (founder: "so sometimes the length, volatility or other features are more likely to give us a positive EV on
// the flag" → "go for it"). Runs research/preregistered/2026-10-22_regime_sizing.md exactly as written before the run: five features known
// before each BOOK2 entry (R1 volatility rising, R2 prior week, R3 gap, E1 15m + daily EMA20 trend, N1 noise band), the favourable value of
// each chosen on 2014-19, and variants that size favourable trades 2 micros (column 72 "size", read by `flag_prop_ev … mtm`).
// Usage: regime_size W15_NQ.csv W15_ES.csv OUTDIR [context] (context: preregistered/2026-10-22_context_sizing.md C1-C8). Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr, atr20, pc, open, wk, dema; std::vector<int> pos; std::vector<double> C; std::vector<double> sig; std::vector<double> e15; std::vector<int> t15;
            double ph = NAN, pl = NAN, onh = NAN, onl = NAN, pwh = NAN, pwl = NAN, sma20 = NAN; std::vector<double> H, L, VW;
            std::vector<int> hT, fT; std::vector<double> hC, hE, fC, fE; double pbody = NAN;
            std::vector<int> mT[2]; std::vector<double> mC[2], mE[2]; };   // 15m / 30m buckets (addendum 2)   // 1h / 4h buckets: end tday minute, close, EMA20; prior RTH open→close   // context mode (2026-10-22_context_sizing.md)
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: regime_size W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    // per day: RTH 1m closes, |close/open − 1| by minute (for the noise band), 15m RTH closes, chained EMA20s
    struct DR { int date; double atr, pc, open, rc; bool ok; std::vector<double> C; std::vector<int> pos; double ph, pl, pwh, pwl, onh = -1e18, onl = 1e18; std::vector<double> H, L, VW;
                std::vector<std::pair<int, double>> all; };   // every 1m bar of the trading day (tday minute, close)
    std::vector<DR> dr;
    for (auto& d : s.days) { DR x{d.date, d.atr, d.pc, d.ro, d.rc, d.ok && !d.rollwin && d.atr > 0, {}, std::vector<int>(400, -1), d.ph, d.pl, d.pwh, d.pwl};
      double pv = 0, vv = 0;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); x.all.push_back({tm, (double)s.c[i]}); if (tm < 930) { x.onh = std::max(x.onh, (double)s.h[i]); x.onl = std::min(x.onl, (double)s.l[i]); continue; } if (tm >= 1320) continue;
        x.pos[tm - 930] = (int)x.C.size(); x.C.push_back(s.c[i]); x.H.push_back(s.h[i]); x.L.push_back(s.l[i]);
        pv += (s.h[i] + s.l[i] + s.c[i]) / 3 * s.v[i]; vv += s.v[i]; x.VW.push_back(vv > 0 ? pv / vv : s.c[i]); }
      if (x.C.size() < 370) x.ok = false; dr.push_back(std::move(x)); }
    double ema15 = NAN, emaD = NAN; const double a15 = 2.0 / 21, aD = 2.0 / 21; double emaH = NAN, emaF = NAN; double prevBody = NAN; double emaM[2] = {NAN, NAN};
    for (size_t k = 0; k < dr.size(); ++k) { DR& x = dr[k]; if (!x.ok) { continue; }
      DB z{x.atr, NAN, x.pc, x.open, NAN, emaD, x.pos, x.C, std::vector<double>(390, NAN), {}, {}};
      z.ph = x.ph; z.pl = x.pl; z.pwh = x.pwh; z.pwl = x.pwl; if (x.onh > -1e17) { z.onh = x.onh; z.onl = x.onl; } z.H = x.H; z.L = x.L; z.VW = x.VW;
      { double sm = 0; int n = 0; for (int j = (int)k - 1; j >= 0 && n < 20; --j) if (dr[j].ok) { sm += dr[j].rc; n++; } if (n == 20) z.sma20 = sm / 20; }
      z.pbody = prevBody; prevBody = x.rc - x.open;   // yesterday's RTH open → close (known before today)
      // 1h / 4h buckets of the whole trading day (from 18:00), EMA20 chained across days; value after each completed bucket
      for (int w : {60, 240}) { auto& T = w == 60 ? z.hT : z.fT; auto& Cc = w == 60 ? z.hC : z.fC; auto& E = w == 60 ? z.hE : z.fE; double& em = w == 60 ? emaH : emaF;
        int cur = -1; double lastC = NAN;
        auto close = [&](int b) { em = std::isnan(em) ? lastC : em + (2.0 / 21) * (lastC - em); T.push_back((b + 1) * w); Cc.push_back(lastC); E.push_back(em); };
        for (auto& [tm, c] : x.all) { const int b = tm / w; if (cur >= 0 && b != cur) close(cur); cur = b; lastC = c; }
        if (cur >= 0) close(cur); }
      for (int wi = 0; wi < 2; ++wi) { const int w = wi ? 30 : 15; auto& T = z.mT[wi]; auto& Cc = z.mC[wi]; auto& E = z.mE[wi]; double& em = emaM[wi]; int cur = -1; double lastC = NAN;
        auto close = [&](int b) { em = std::isnan(em) ? lastC : em + (2.0 / 21) * (lastC - em); T.push_back((b + 1) * w); Cc.push_back(lastC); E.push_back(em); };
        for (auto& [tm, c] : x.all) { const int b = tm / w; if (cur >= 0 && b != cur) close(cur); cur = b; lastC = c; }
        if (cur >= 0) close(cur); }
      // ATR 20 valid sessions earlier, last 5 sessions' change (prior close vs the close 5 valid sessions before)
      { int n = 0; for (int j = (int)k - 1; j >= 0; --j) if (dr[j].ok) { if (++n == 20) { z.atr20 = dr[j].atr; break; } } }
      { int n = 0; for (int j = (int)k - 1; j >= 0; --j) if (dr[j].ok) { if (++n == 6) { z.wk = x.pc - dr[j].rc; break; } } }
      // noise band sigma: mean over the previous 14 valid sessions of |close/open − 1| at the same minute
      { std::vector<int> prev; for (int j = (int)k - 1; j >= 0 && prev.size() < 14; --j) if (dr[j].ok && dr[j].pos[0] >= 0) prev.push_back(j);
        if (prev.size() == 14) for (int m = 0; m < 390; ++m) { double sm = 0; int n = 0; for (int j : prev) { const int q = dr[j].pos[m]; if (q < 0) continue; if (!(dr[j].open > 0)) continue; sm += std::fabs(dr[j].C[q] / dr[j].open - 1); n++; }
          z.sig[m] = n ? sm / n : NAN; } }
      // 15m RTH bars chained across days (EMA value after each completed bar)
      for (int b = 0; b < 26; ++b) { const int last = 930 + b * 15 + 14; int q = -1; for (int t = last; t >= 930 + b * 15; --t) if (x.pos[t - 930] >= 0) { q = x.pos[t - 930]; break; } if (q < 0) continue;
        ema15 = std::isnan(ema15) ? x.C[q] : ema15 + a15 * (x.C[q] - ema15); z.e15.push_back(ema15); z.t15.push_back(930 + b * 15 + 15); }
      days[mk][x.date] = std::move(z);
      emaD = std::isnan(emaD) ? x.rc : emaD + aD * (x.rc - emaD);   // daily EMA known from the NEXT session on (z.dema was set before this update)
    }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln + ",size";
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; r.tok.resize(71); r.tok.push_back("1"); rows.push_back(std::move(r)); } }
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return r.a[11] > 0.5 && (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  // features per BOOK2 trade: -1 unknown, 0 no, 1 yes
  const char* FN[5] = {"R1 volatility rising", "R2 prior week with the flag", "R3 gap with the flag", "E1 15m + daily EMA20 with the flag", "N1 beyond the noise band"};
  std::vector<std::array<int, 5>> feat(rows.size()); for (auto& v : feat) v.fill(-1);
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r)) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
    const int sd = (int)r.a[1], em = (int)r.a[12];
    if (std::isfinite(z.atr20)) feat[i][0] = z.atr > z.atr20;
    if (std::isfinite(z.wk)) feat[i][1] = sd * z.wk > 0;
    if (std::isfinite(z.pc) && std::isfinite(z.open)) feat[i][2] = sd * (z.open - z.pc) > 0;
    { int b = -1; for (size_t q = 0; q < z.t15.size(); ++q) if (z.t15[q] <= em) b = (int)q; const int pm = em - 1 - 930;
      if (b >= 0 && std::isfinite(z.dema) && pm >= 0 && z.pos[pm] >= 0) { const int last15 = z.t15[b] - 1; int q = -1; for (int t = last15; t >= last15 - 14; --t) if (t >= 930 && z.pos[t - 930] >= 0) { q = z.pos[t - 930]; break; }
        if (q >= 0) feat[i][3] = sd * (z.C[q] - z.e15[b]) > 0 && sd * (z.pc - z.dema) > 0; } }
    { const int pm = em - 1 - 930; if (pm >= 0 && pm < 390 && z.pos[pm] >= 0 && std::isfinite(z.sig[pm]) && z.open > 0) feat[i][4] = sd * (z.C[z.pos[pm]] / z.open - 1) > z.sig[pm]; } }
  // mean net R by feature value, 2014-19 (selection) and 2020-26, per market
  int fav[5];
  printf("BOOK2 trades: mean net R by feature (2014-19 | 2020-26), NQ and ES; the favourable value is chosen on 2014-19, both markets pooled\n");
  for (int k = 0; k < 5; ++k) { double s[2][2][2] = {}, n[2][2][2] = {}, ps[2] = {0, 0}, pn[2] = {0, 0};
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const int v = feat[i][k]; if (v < 0 || (int)r.a[0] < 20140101) continue; const int h = (int)r.a[0] >= 20200101;
      s[r.mk][h][v] += r.a[18]; n[r.mk][h][v]++; if (!h) { ps[v] += r.a[18]; pn[v]++; } }
    fav[k] = (pn[1] ? ps[1] / pn[1] : -9) >= (pn[0] ? ps[0] / pn[0] : -9) ? 1 : 0;
    printf("  %-36s favourable = %-3s |", FN[k], fav[k] ? "yes" : "no");
    for (int mk = 0; mk < 2; ++mk) for (int v = 1; v >= 0; --v) printf(" %s %s %+6.3f (%4.0f) / %+6.3f (%4.0f) |", mk ? "ES" : "NQ", v ? "yes" : "no ", n[mk][0][v] ? s[mk][0][v] / n[mk][0][v] : 0, n[mk][0][v], n[mk][1][v] ? s[mk][1][v] / n[mk][1][v] : 0, n[mk][1][v]);
    printf("\n"); }
  auto score = [&](size_t i) { int c = 0; for (int k = 0; k < 5; ++k) c += feat[i][k] == fav[k]; return c; };
  auto write = [&](const std::string& name, std::function<int(size_t)> size) {   // size: 2, 1, or 0 = skip (ok → 0)
    // 2026-10-22 (preregistered/2026-10-22_external_ideas.md): env VARIANT=<name> writes only that variant, to OUTDIR/tmp_{nq,es}.csv
    // (overwritten on every run — the disk is ~100 % full and nothing is deleted)
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    int n2 = 0, n0 = 0;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str());
      for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (r.mk != mk) continue; std::vector<std::string> t = r.tok; if (book2(r)) { const int sz = size(i); if (sz == 0) { t[11] = "0"; n0++; } else { t[71] = std::to_string(sz); n2 += sz == 2; } }
        for (size_t j = 0; j < t.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", t[j].c_str()); fprintf(fo, "\n"); }
      fclose(fo); }
    printf("  variant %-6s: %d BOOK2 trades at 2 micros, %d skipped\n", name.c_str(), n2, n0); };
  printf("\n");
  write("base", [](size_t) { return 1; });
  for (int k = 0; k < 5; ++k) write("S" + std::to_string(k + 1), [&, k](size_t i) { return feat[i][k] == fav[k] ? 2 : 1; });
  for (int c = 2; c <= 4; ++c) write("SC" + std::to_string(c), [&, c](size_t i) { return score(i) >= c ? 2 : 1; });
  write("SK", [&](size_t i) { const int c = score(i); return c >= 3 ? 2 : c == 0 ? 0 : 1; });
  // ── mode context (preregistered/2026-10-22_context_sizing.md): C1-C8, A_Ck (2 micros when favourable) and B_Ck (gap sizing + 1 when favourable)
  if (argc > 4 && std::string(argv[4]) == "context") {
    const char* CN[13] = {"C1 open beyond yesterday", "C2 open in overnight top third", "C3 room left (range < 0.6 ATR)", "C4 VWAP side", "C5 upper half of day range",
                         "C6 above 20-session average", "C7 opening drive with the flag", "C8 above last week's midpoint", "C9 1h trend with the flag", "C10 4h trend with the flag",
                         "C11 yesterday a pole day with the flag", "C12 15m trend with the flag", "C13 30m trend with the flag"};
    std::vector<std::array<int, 13>> cf(rows.size()); for (auto& v : cf) v.fill(-1);
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r)) continue; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
      const int sd = (int)r.a[1], em = (int)r.a[12], pm = em - 1 - 930; const double op = z.open;
      if (std::isfinite(z.ph) && std::isfinite(z.pl)) cf[i][0] = sd > 0 ? op > z.ph : op < z.pl;
      if (std::isfinite(z.onh) && z.onh > z.onl) { const double pos = (op - z.onl) / (z.onh - z.onl); cf[i][1] = sd > 0 ? pos >= 2.0 / 3 : pos <= 1.0 / 3; }
      if (pm >= 0 && pm < 390 && z.pos[pm] >= 0) { const int k = z.pos[pm]; double hi = -1e18, lo = 1e18; for (int q = 0; q <= k; ++q) { hi = std::max(hi, z.H[q]); lo = std::min(lo, z.L[q]); }
        cf[i][2] = (hi - lo) < 0.6 * z.atr; cf[i][3] = sd * (z.C[k] - z.VW[k]) > 0; cf[i][4] = hi > lo ? (sd > 0 ? (z.C[k] - lo) / (hi - lo) >= 0.5 : (hi - z.C[k]) / (hi - lo) >= 0.5) : 0;
        const int k10 = std::min(k, z.pos[29] >= 0 ? z.pos[29] : k); cf[i][6] = sd * (z.C[k10] - op) > 0; }
      if (std::isfinite(z.sma20)) cf[i][5] = sd * (z.pc - z.sma20) > 0;
      if (std::isfinite(z.pwh) && std::isfinite(z.pwl)) cf[i][7] = sd * (z.pc - (z.pwh + z.pwl) / 2) > 0;
      // C9 / C10: the last 1h / 4h bucket COMPLETED before the entry minute (buckets end at (b + 1) × w tday minutes)
      for (int w = 0; w < 2; ++w) { const auto& T = w ? z.fT : z.hT; const auto& Cc = w ? z.fC : z.hC; const auto& E = w ? z.fE : z.hE; int q = -1;
        for (size_t j = 0; j < T.size(); ++j) if (T[j] <= em) q = (int)j; if (q >= 0) cf[i][8 + w] = sd * (Cc[q] - E[q]) > 0;
        else { auto itp = days[r.mk].find((int)r.a[0]); (void)itp; } }
      if (std::isfinite(z.pbody)) cf[i][10] = sd * z.pbody > 0.5 * z.atr;
      for (int wi = 0; wi < 2; ++wi) { int q = -1; for (size_t j = 0; j < z.mT[wi].size(); ++j) if (z.mT[wi][j] <= em) q = (int)j; if (q >= 0) cf[i][11 + wi] = sd * (z.mC[wi][q] - z.mE[wi][q]) > 0; } }
    int cfav[13];
    printf("\n######## CONTEXT: BOOK2 mean net R by context (2014-19 | 2020-26), favourable chosen on 2014-19, NQ + ES pooled\n");
    for (int k = 0; k < 13; ++k) { double sm[2][2][2] = {}, n[2][2][2] = {}, ps[2] = {0, 0}, pn[2] = {0, 0};
      for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const int v = cf[i][k]; if (v < 0 || (int)r.a[0] < 20140101) continue; const int h = (int)r.a[0] >= 20200101;
        sm[r.mk][h][v] += r.a[18]; n[r.mk][h][v]++; if (!h) { ps[v] += r.a[18]; pn[v]++; } }
      cfav[k] = (pn[1] ? ps[1] / pn[1] : -9) >= (pn[0] ? ps[0] / pn[0] : -9) ? 1 : 0;
      printf("  %-34s fav = %-3s |", CN[k], cfav[k] ? "yes" : "no");
      for (int mk = 0; mk < 2; ++mk) for (int v = 1; v >= 0; --v) printf(" %s %s %+6.3f (%4.0f) / %+6.3f (%4.0f) |", mk ? "ES" : "NQ", v ? "yes" : "no ", n[mk][0][v] ? sm[mk][0][v] / n[mk][0][v] : 0, n[mk][0][v], n[mk][1][v] ? sm[mk][1][v] / n[mk][1][v] : 0, n[mk][1][v]);
      printf("\n"); }
    for (int k = 0; k < 13; ++k) {
      write("A_C" + std::to_string(k + 1), [&, k](size_t i) { return cf[i][k] == cfav[k] ? 2 : 1; });
      write("B_C" + std::to_string(k + 1), [&, k](size_t i) { return 1 + (feat[i][2] == fav[2] ? 1 : 0) + (cf[i][k] == cfav[k] ? 1 : 0); }); } }
  return 0;
}
