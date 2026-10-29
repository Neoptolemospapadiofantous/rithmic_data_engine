// 2026-10-22 book_tests.cpp (founder: "what else can we test" → "test all"). Runs research/preregistered/2026-10-22_book_tests.md
// T2-T7 exactly as written before the run. Each variant = a modified copy of the two wide15 exports (flag_hhhl `export … wide15`),
// written to OUTDIR/<variant>_{nq,es}.csv and judged by the unchanged `flag_prop_ev A B prob` (same booking, daily stop, bootstrap, EV).
//   T2 shape at the fill (closed flag bars before the fill minute, flag_behavior Part A rules): a skip deep/down channel, b skip down
//      channel, c tight box only (info)
//   T3 one trade per move (BOOK2 booking emulated: 2 open per market, entry order): a no two open same market + side, b no two open
//      same side across NQ/ES, c both
//   T4 NQ-ES agreement: a other market on the same side of its open and VWAP at the registration minute, b other market registered a
//      same-side 4-5m wide flag within 15 min before
//   T5 limit offset: flag-mid limit moved k ticks toward the pole top (+1, +2) or away (-1, -2), entry re-simulated on 1m bars
//      (k = 0 must reproduce the export: checked and printed)
//   T6 grade refit monthly instead of yearly (same 14 features, break-entry target, thirds); yearly reproduction checked and printed
//   T7 time of day: a drop BOOK2 registered at/after 12:00, b at/after 11:00 (+ a descriptive split)
// Usage: book_tests W15_NQ.csv W15_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr; std::vector<int> TM; std::vector<double> O, H, L, C, VW; double so; };
static int col(const Row& r, int j) { return (int)r.a[j]; }
static bool book2(const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = col(r, 6); return r.a[11] > 0.5 && col(r, 2) >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); }
static std::string fmt(double v, int dec) { char b[64]; snprintf(b, sizeof b, "%.*f", dec, v); return b; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: book_tests W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue; DB z{d.atr, {}, {}, {}, {}, {}, {}, NAN};
      double pv = 0, vv = 0;
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; if (std::isnan(z.so)) z.so = s.o[i];
        z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]);
        pv += (s.h[i] + s.l[i] + s.c[i]) / 3 * s.v[i]; vv += s.v[i]; z.VW.push_back(vv > 0 ? pv / vv : s.c[i]); }
      if (z.TM.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; rows.push_back(std::move(r)); } }
  auto write = [&](const std::string& name, const std::vector<Row>& rs) {
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + name + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str()); for (auto& r : rs) if (r.mk == mk) { for (size_t j = 0; j < r.tok.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", r.tok[j].c_str()); fprintf(fo, "\n"); } fclose(fo); } };
  auto drop = [](Row& r) { r.tok[11] = "0"; r.a[11] = 0; };
  write("base", rows);
  auto at = [&](const Row& r) -> const DB* { auto it = days[r.mk].find(col(r, 0)); return it == days[r.mk].end() ? nullptr : &it->second; };
  auto idxOf = [](const DB& z, int tm) { for (size_t k = 0; k < z.TM.size(); ++k) if (z.TM[k] == tm) return (int)k; return -1; };

  // ── T2 shape at the fill
  { int shapeOf_n[5] = {}; std::vector<Row> A = rows, B = rows, Cc = rows;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (r.a[11] < 0.5) continue; const DB* z = at(r); if (!z) continue;
      const int sd = col(r, 1), tf = col(r, 2), reg = col(r, 3), em = col(r, 12); const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL; if (!(M > 0)) continue;
      // chart bars on the 09:30 grid, mirrored; the pole-top bar starts at reg - 3 tf; flag bars = those after it that CLOSED before em
      struct B_ { int t0; double h, l, c; }; std::vector<B_> b;
      for (size_t k = 0; k < z->TM.size(); ++k) { const int t0 = 930 + (z->TM[k] - 930) / tf * tf; const double hi = sd > 0 ? z->H[k] : -z->L[k], lo = sd > 0 ? z->L[k] : -z->H[k], c = sd * z->C[k];
        if (b.empty() || b.back().t0 != t0) b.push_back({t0, hi, lo, c}); else { b.back().h = std::max(b.back().h, hi); b.back().l = std::min(b.back().l, lo); b.back().c = c; } }
      int hi = -1; for (int j = 0; j < (int)b.size(); ++j) if (b[j].t0 == reg - 3 * tf) { hi = j; break; } if (hi < 1) continue;
      std::vector<int> fb; for (int j = hi + 1; j < (int)b.size() && j <= hi + 6; ++j) { if (b[j].t0 + tf > em) break; if (b[j].h > PH) break; fb.push_back(j); }
      int sh = -1; const int m = (int)fb.size();
      if (m) { double mn = 1e18, sx = 0, sy = 0, sxx = 0, sxy = 0; int lh = 0;
        for (int q = 0; q < m; ++q) { const auto& x = b[fb[q]]; mn = std::min(mn, x.l); sx += q; sy += x.c; sxx += q * q; sxy += q * x.c; lh += x.h < b[fb[q] - 1].h; }
        const double depth = (PH - mn) / M, slope = m > 1 ? (m * sxy - sx * sy) / (m * sxx - sx * sx) / M : 0, lhs = (double)lh / m;
        double r1 = 0, r2 = 0; int n1 = 0, n2 = 0; for (int q = 0; q < m; ++q) { const double rr = b[fb[q]].h - b[fb[q]].l; if (q < m / 2) { r1 += rr; n1++; } else { r2 += rr; n2++; } }
        sh = depth > 0.5 ? 2 : (slope < -0.03 && lhs >= 0.6) ? 1 : (depth <= 0.38 && std::fabs(slope) <= 0.03) ? 0 : (m >= 4 && n1 && n2 && (r2 / n2) < 0.7 * (r1 / n1)) ? 3 : 4; }
      if (book2(r)) shapeOf_n[sh < 0 ? 4 : sh]++;
      if (sh == 2 || sh == 1) drop(A[i]); if (sh == 1) drop(B[i]); if (sh >= 1) drop(Cc[i]); }
    printf("T2 shape at the fill, BOOK2 trades (rows): tight box %d · down channel %d · deep %d · wedge %d · other / no closed bar yet %d\n", shapeOf_n[0], shapeOf_n[1], shapeOf_n[2], shapeOf_n[3], shapeOf_n[4]);
    write("T2a", A); write("T2b", B); write("T2c", Cc); }

  // ── T3 one trade per move (BOOK2 booking emulated, both markets, entry order)
  { std::map<int, std::vector<size_t>> byDay; for (size_t i = 0; i < rows.size(); ++i) if (book2(rows[i]) && rows[i].a[0] >= 20140101) byDay[col(rows[i], 0)].push_back(i);
    for (int var = 0; var < 3; ++var) { std::vector<Row> V = rows; int kept = 0, cut = 0;
      for (auto& [d, ix] : byDay) { std::sort(ix.begin(), ix.end(), [&](size_t a, size_t b) { return rows[a].a[12] != rows[b].a[12] ? rows[a].a[12] < rows[b].a[12] : a < b; });
        struct O_ { int mk, sd, xm; }; std::vector<O_> open;
        for (size_t i : ix) { const Row& r = rows[i]; const int em = col(r, 12); open.erase(std::remove_if(open.begin(), open.end(), [&](const O_& o) { return o.xm < em; }), open.end());
          int nMk = 0; bool sameMkSide = false, sameSide = false; for (auto& o : open) { if (o.mk == r.mk) { nMk++; if (o.sd == col(r, 1)) sameMkSide = true; } if (o.sd == col(r, 1)) sameSide = true; }
          if (nMk >= 2) continue;   // the base booking would not take it either (flag_prop_ev re-books identically)
          const bool block = (var == 0 && sameMkSide) || (var == 1 && sameSide) || (var == 2 && (sameMkSide || sameSide));
          if (block) { drop(V[i]); cut++; continue; } open.push_back({r.mk, col(r, 1), col(r, 13)}); kept++; } }
      printf("T3%c: %d BOOK2 trades kept, %d removed\n", 'a' + var, kept, cut); write(std::string("T3") + char('a' + var), V); } }

  // ── T4 NQ-ES agreement
  { std::vector<Row> A = rows, B = rows; int nA = 0, nB = 0, nT = 0;
    std::map<long, std::vector<int>> regs;   // (mk, date, side) → registration minutes of 4-5m wide flags
    for (auto& r : rows) if (col(r, 2) >= 4) regs[((long)r.mk * 100000000L + col(r, 0)) * 3 + (col(r, 1) + 1)].push_back(col(r, 3));
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r)) continue; nT++; const int o = 1 - r.mk, sd = col(r, 1), reg = col(r, 3);
      auto it = days[o].find(col(r, 0)); bool agreeA = false;
      if (it != days[o].end()) { const int k = idxOf(it->second, reg - 1); if (k >= 0) { const DB& z = it->second; agreeA = sd * (z.C[k] - z.so) > 0 && sd * (z.C[k] - z.VW[k]) > 0; } }
      bool agreeB = false; auto jt = regs.find(((long)o * 100000000L + col(r, 0)) * 3 + (sd + 1)); if (jt != regs.end()) for (int m : jt->second) if (m <= reg && m >= reg - 15) agreeB = true;
      if (!agreeA) drop(A[i]); else nA++; if (!agreeB) drop(B[i]); else nB++; }
    printf("T4: BOOK2 rows %d · other market on the same side of open + VWAP %d (%.0f %%) · other market flagged same side within 15 min %d (%.0f %%)\n", nT, nA, 100.0 * nA / nT, nB, 100.0 * nB / nT);
    write("T4a", A); write("T4b", B); }

  // ── T5 limit offset (re-simulated entry; k = 0 checks the reproduction)
  for (int kk : {0, 1, 2, -1, -2}) { std::vector<Row> V = rows; int same = 0, diff = 0, filled = 0, baseFilled = 0; std::map<long, int> lastEx;
    std::vector<size_t> order(rows.size()); for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { const Row &x = rows[a], &y = rows[b]; if (x.mk != y.mk) return x.mk < y.mk; if (x.a[0] != y.a[0]) return x.a[0] < y.a[0]; if (x.a[1] != y.a[1]) return x.a[1] > y.a[1]; if (x.a[2] != y.a[2]) return x.a[2] < y.a[2]; return x.a[3] < y.a[3]; });
    for (size_t i : order) { const Row& r = rows[i]; Row& w = V[i]; const DB* z = at(r); baseFilled += r.a[11] > 0.5;
      if (!z) { drop(w); continue; }
      const int sd = col(r, 1), tf = col(r, 2), reg = col(r, 3), endM = col(r, 4), poleM = col(r, 5); const double cost = r.mk ? 0.5 : 1.5, A = z->atr;
      const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], fl0 = sd * r.a[10], M = PH - PL; const long key = ((long)r.mk * 100000000L + col(r, 0)) * 20 + (sd + 1) * 6 + tf;
      const size_t n = z->TM.size(); std::vector<double> O(n), H(n), L(n), C(n); for (size_t k = 0; k < n; ++k) { O[k] = sd * z->O[k]; C[k] = sd * z->C[k]; H[k] = sd > 0 ? z->H[k] : -z->L[k]; L[k] = sd > 0 ? z->L[k] : -z->H[k]; }
      const int kc = idxOf(*z, reg - 1); drop(w); if (kc < 0) continue;
      const double mid = (PH + fl0) / 2 + 0.25 * kk; int kE = -1; double e = NAN;
      for (size_t k = kc + 1; k < n; ++k) { if (z->TM[k] > endM - 1) break; if (poleM >= 0 && z->TM[k] >= poleM) break; if (L[k] <= mid) { kE = (int)k; e = std::min(O[k], mid); break; } }
      if (kE < 0 || z->TM[kE] >= 1260) continue;
      double FL = fl0; for (int k = kc + 1; k < kE; ++k) FL = std::min(FL, L[k]); const double st = FL - 0.1 * A, tg = PH + M;
      auto le = lastEx.find(key); if (!(e - st > 0.5) || tg <= e || (le != lastEx.end() && kE <= le->second)) continue;
      double px = NAN; int ko = (int)n - 1; if (L[kE] <= st) { px = st; ko = kE; }
      for (size_t k = kE + 1; k < n && std::isnan(px); ++k) { if (L[k] <= st) { px = st; ko = (int)k; break; } if (H[k] >= tg) { px = tg; ko = (int)k; break; } if (z->TM[k] >= 1315) { px = O[k]; ko = (int)k; break; } }
      if (std::isnan(px)) px = C[n - 1];
      lastEx[key] = ko; filled++; const double pts = px - e - cost, rr = pts / (e - st);
      w.tok[11] = "1"; w.a[11] = 1; w.tok[12] = std::to_string(z->TM[kE]); w.tok[13] = std::to_string(z->TM[ko]); w.tok[14] = fmt(sd * e, 2); w.tok[15] = fmt(sd * st, 2); w.tok[16] = fmt(sd * tg, 2);
      w.tok[17] = fmt(sd * px, 2); w.tok[18] = fmt(rr, 4); w.tok[19] = fmt(pts, 4); for (int j : {12, 13, 14, 15, 16, 17, 18, 19}) w.a[j] = strtod(w.tok[j].c_str(), nullptr);
      if (kk == 0 && r.a[11] > 0.5) { if (std::fabs(rr - r.a[18]) < 2e-4 && col(r, 12) == z->TM[kE]) same++; else diff++; } }
    if (kk == 0) printf("T5 check: k = 0 re-simulation vs export — %d trades identical, %d differ, %d filled vs %d in the export\n", same, diff, filled, baseFilled);
    else { printf("T5 k = %+d ticks: %d trades filled (all wide flags)\n", kk, filled); write(std::string("T5") + (kk > 0 ? "p" : "m") + std::to_string(std::abs(kk)), V); } }

  // ── T6 grade refit monthly (and the yearly reproduction)
  { auto feat = [](const Row& r, bool* f) { const double* x = &r.a[20]; f[0] = x[9] > 0.5; f[1] = x[11] > 0.5; f[2] = x[5] > 0.1; f[3] = x[2] >= 5; f[4] = x[6] > 0.1; f[5] = x[1] < 960; f[6] = x[12] > 0.5;
      f[7] = x[13] > 0.5; f[8] = x[17] > 0.5; f[9] = x[18] > 0.5; f[10] = x[22] > 0.5; f[11] = x[20] > 0.5; f[12] = (int)x[0] == 5; f[13] = (int)x[0] == 4; };
    for (int mk = 0; mk < 2; ++mk) for (int side : {1, -1}) {
      std::vector<size_t> ix; for (size_t i = 0; i < rows.size(); ++i) if (rows[i].mk == mk && col(rows[i], 1) == side) ix.push_back(i);
      std::vector<std::array<bool, 14>> F(rows.size()); for (size_t i : ix) feat(rows[i], F[i].data());
      auto grade = [&](int cutDate, const std::vector<size_t>& apply, std::vector<int>& g) {   // train on rows dated < cutDate
        double w[14]; for (int f = 0; f < 14; ++f) { double sy = 0, ny = 0, sn = 0, nn = 0; for (size_t i : ix) { const Row& r = rows[i]; if (r.a[0] >= cutDate || std::isnan(r.a[55])) continue; if (F[i][f]) { sy += r.a[55]; ny++; } else { sn += r.a[55]; nn++; } }
          w[f] = ny >= 30 && nn >= 30 ? sy / ny - sn / nn : 0; }
        std::vector<double> tr; for (size_t i : ix) { const Row& r = rows[i]; if (r.a[0] >= cutDate || std::isnan(r.a[55])) continue; double sc = 0; for (int f = 0; f < 14; ++f) sc += F[i][f] ? w[f] : 0; tr.push_back(sc); }
        if (tr.size() < 100) return; std::sort(tr.begin(), tr.end()); const double thr = tr[tr.size() * 2 / 3], thr2 = tr[tr.size() / 3];
        for (size_t i : apply) { double sc = 0; for (int f = 0; f < 14; ++f) sc += F[i][f] ? w[f] : 0; g[i] = sc >= thr ? 1 : sc >= thr2 ? 2 : 3; } };
      static std::vector<int> gy, gm; if (gy.empty()) { gy.assign(rows.size(), 0); gm.assign(rows.size(), 0); }
      for (int Y = 2014; Y <= 2026; ++Y) { std::vector<size_t> ap; for (size_t i : ix) if (col(rows[i], 0) / 10000 == Y) ap.push_back(i); grade(Y * 10000 + 101, ap, gy);
        for (int Mo = 1; Mo <= 12; ++Mo) { std::vector<size_t> am; for (size_t i : ix) if (col(rows[i], 0) / 100 == Y * 100 + Mo) am.push_back(i); if (!am.empty()) grade(Y * 10000 + Mo * 100 + 1, am, gm); } }
      if (mk == 1 && side == -1) {
        int rep = 0, repN = 0, flip = 0, flipB2 = 0, nB2 = 0; std::vector<Row> V = rows;
        for (size_t i = 0; i < rows.size(); ++i) { if (rows[i].a[0] < 20140101) continue; repN++; rep += gy[i] == col(rows[i], 6); flip += gm[i] != gy[i];
          if (book2(rows[i])) { nB2++; Row tmp = rows[i]; tmp.a[6] = gm[i]; flipB2 += !book2(tmp); }
          V[i].tok[6] = std::to_string(gm[i]); V[i].a[6] = gm[i]; }
        printf("T6 check: yearly refit reproduces the export grade on %d of %d rows (2014+)\n", rep, repN);
        printf("T6: monthly vs yearly grade differs on %.1f %% of flags; %d of %d BOOK2 trades leave BOOK2 under the monthly grade\n", 100.0 * flip / repN, flipB2, nB2);
        write("T6m", V); } } }

  // ── T7 time of day
  { std::vector<Row> A = rows, B = rows; double s[2][3][2] = {}, n[2][3][2] = {};
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r) || r.a[0] < 20140101) continue; const int reg = col(r, 3), w = reg < 1020 ? 0 : reg < 1080 ? 1 : 2, h = r.a[0] >= 20200101;
      s[r.mk][w][h] += r.a[18]; n[r.mk][w][h]++; if (reg >= 1080) drop(A[i]); if (reg >= 1020) drop(B[i]); }
    const char* WN[3] = {"09:45-11:00", "11:00-12:00", "12:00-14:00"};
    printf("T7 BOOK2 rows by registration window, avg R before booking (2014-19 / 2020-26):\n");
    for (int w = 0; w < 3; ++w) printf("   %s  NQ %4.0f trades %+.3f / %+.3f · ES %4.0f trades %+.3f / %+.3f\n", WN[w], n[0][w][0] + n[0][w][1], s[0][w][0] / std::max(1.0, n[0][w][0]), s[0][w][1] / std::max(1.0, n[0][w][1]),
                                       n[1][w][0] + n[1][w][1], s[1][w][0] / std::max(1.0, n[1][w][0]), s[1][w][1] / std::max(1.0, n[1][w][1]));
    write("T7a", A); write("T7b", B); }
  return 0;
}
