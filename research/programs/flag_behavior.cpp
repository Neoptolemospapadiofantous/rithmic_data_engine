// 2026-10-22 flag_behavior.cpp (founder: "what about the flag behaivor … the behavior of the actual flag" → "yes"). Runs
// research/preregistered/2026-10-22_flag_behaviour.md exactly as written before the run: A the flag's shape (descriptive), B a delayed entry
// after 4 flag bars (D1 mid limit / D2 breakout stop, HOLD+QUIET and HOLD only), C exits when the flag misbehaves after today's entry (X1 time,
// X2 half-way, X3 flag-low close, X4 break-even after a new high). Flags = flag_hhhl `export wide15`, NQ + ES, mirrored to bull; trades on 1m
// bars, stop = flag low so far − 0.1 ATR, target = H + M, flat 15:55, cost 1.5 NQ / 0.5 ES pts (and +1×), one position at a time per market.
// Usage: flag_behavior W15_NQ.csv W15_ES.csv. Read-only. tday = minutes since 18:00 ET (09:30 = 930, 15:00 = 1260, 15:55 = 1315).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Tr { int date, em, xm; double r, rc; };
enum { BASE = 0, X1, X2, X3, X4, D1Q, D2Q, D1H, D2H, S1_3, S1_5, S1_10, S1_15, S2_3, S2_5, S2_10, S2_15, Q1, Q15, Q2, NV };
static const char* VN[NV] = {"today's trade (early mid entry)", "C X1 time: no new high in 6 bars", "C X2 close below half-way to stop", "C X3 close below the flag low",
  "C X4 break-even after a new high", "B D1 mid limit after 4 bars, HOLD+QUIET", "B D2 breakout stop after 4 bars, HOLD+QUIET", "B D1 mid limit after 4 bars, HOLD only",
  "B D2 breakout stop after 4 bars, HOLD only", "D S1 3 min: exit unless +0.5R reached", "D S1 5 min: exit unless +0.5R reached", "D S1 10 min: exit unless +0.5R reached",
  "D S1 15 min: exit unless +0.5R reached", "D S2 3 min: exit unless in profit", "D S2 5 min: exit unless in profit", "D S2 10 min: exit unless in profit", "D S2 15 min: exit unless in profit",
  "D Q target +1R", "D Q target +1.5R", "D Q target +2R"};
static const char* SH[5] = {"TIGHT BOX", "DOWN CHANNEL", "DEEP", "WEDGE", "OTHER"};
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: flag_behavior W15_NQ.csv W15_ES.csv\n"); return 1; }
  // per market × set: trades per variant, shape stats
  std::vector<Tr> T[2][3][NV]; double shN[2][3][5] = {}, shB[2][3][5] = {}, shR[2][3][5] = {}; double rediff[2] = {0, 0}, rediffN[2] = {0, 0};
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; const double cost = mk ? 0.5 : 1.5;
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    struct DB { double atr; std::vector<int> TM; std::vector<double> O, H, L, C; }; std::map<int, DB> days;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.atr, {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
      if (z.TM.size() >= 370) days[d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; auto it = days.find((int)a[0]); if (it == days.end()) continue; const DB& z = it->second; const double A = z.atr;
      const int date = (int)a[0], sd = (int)a[1], tf = (int)a[2], reg = (int)a[3], em = (int)a[12], grade = (int)a[6], hh5 = (int)a[7], orb = (int)a[47]; const double mult = a[22], pull = a[24] * 100;
      const bool inSet[3] = {tf >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (grade == 1 || grade == 2), tf >= 3 && mult >= 4 && pull >= 15 && pull < 50 && grade == 1 && hh5 && orb > 0, tf >= 3 && mult >= 4 && pull >= 15 && pull < 50};
      if (!inSet[0] && !inSet[1] && !inSet[2]) continue;
      // mirrored 1m bars and the chart bars on the 09:30 grid
      const size_t n = z.TM.size(); std::vector<double> O(n), H(n), L(n), C(n); for (size_t k = 0; k < n; ++k) { O[k] = sd * z.O[k]; C[k] = sd * z.C[k]; H[k] = sd > 0 ? z.H[k] : -z.L[k]; L[k] = sd > 0 ? z.L[k] : -z.H[k]; }
      struct B { int t0, k0, k1; double o, h, l, c; }; std::vector<B> b; std::vector<int> barOf(n);
      for (size_t k = 0; k < n; ++k) { const int t0 = 930 + (z.TM[k] - 930) / tf * tf; if (b.empty() || b.back().t0 != t0) b.push_back({t0, (int)k, (int)k, O[k], H[k], L[k], C[k]}); else { B& x = b.back(); x.k1 = (int)k; x.h = std::max(x.h, H[k]); x.l = std::min(x.l, L[k]); x.c = C[k]; } barOf[k] = (int)b.size() - 1; }
      int hi = -1; for (int j = 0; j < (int)b.size(); ++j) if (b[j].t0 == reg - 3 * tf) { hi = j; break; } if (hi < 2) continue;
      const double PH = sd > 0 ? a[9] : -a[8], PL = sd > 0 ? a[8] : -a[9], M = PH - PL; if (!(M > 0)) continue;
      int lo = -1; for (int j = hi - 2; j >= std::max(0, hi - 15); --j) if (std::fabs(b[j].l - PL) < 0.01) { lo = j; break; } if (lo < 0) continue;
      double poleAvg = 0; for (int j = lo; j <= hi; ++j) poleAvg += b[j].h - b[j].l; poleAvg /= (hi - lo + 1);
      // the flag: bars after the pole-high bar until breakout / failure / 12 bars
      int brk = -1, fail = -1, endJ = std::min((int)b.size() - 1, hi + 12);
      for (int j = hi + 1; j <= std::min((int)b.size() - 1, hi + 12); ++j) { if (b[j].h > PH) { brk = j; endJ = j; break; } if (b[j].c < PH - 0.5 * M) { fail = j; endJ = j; break; } }
      // A shape over the first up-to-6 flag bars before a breakout
      { std::vector<int> fb; for (int j = hi + 1; j <= std::min(endJ, hi + 6); ++j) { if (j == brk) break; fb.push_back(j); }
        int sh = 4; if (!fb.empty()) { double mn = 1e18, sx = 0, sy = 0, sxx = 0, sxy = 0; int lh = 0; const int m = (int)fb.size();
          for (int q = 0; q < m; ++q) { const B& x = b[fb[q]]; mn = std::min(mn, x.l); sx += q; sy += x.c; sxx += q * q; sxy += q * x.c; lh += x.h < b[fb[q] - 1].h; }
          const double depth = (PH - mn) / M, slope = m > 1 ? (m * sxy - sx * sy) / (m * sxx - sx * sx) / M : 0, lhs = (double)lh / m;
          double r1 = 0, r2 = 0; int n1 = 0, n2 = 0; for (int q = 0; q < m; ++q) { const double rr = b[fb[q]].h - b[fb[q]].l; if (q < m / 2) { r1 += rr; n1++; } else { r2 += rr; n2++; } }
          if (depth > 0.5) sh = 2; else if (slope < -0.03 && lhs >= 0.6) sh = 1; else if (depth <= 0.38 && std::fabs(slope) <= 0.03) sh = 0; else if (m >= 4 && n1 && n2 && (r2 / n2) < 0.7 * (r1 / n1)) sh = 3; }
        for (int st = 0; st < 3; ++st) if (inSet[st]) { shN[mk][st][sh]++; shB[mk][st][sh] += brk >= 0; shR[mk][st][sh] += a[18]; } }
      // trade simulator from minute index k (entry at price e, stop st0, target tg); fillMin = stop checked in the fill minute; exit rule x
      auto sim = [&](size_t k, double e, double st0, double tg, bool fillMin, int x, double flowAtEntry, int& xm) -> double { double stp = st0, hiSince = -1e18; bool qcheck = false; xm = 1315;
        if (fillMin && L[k] <= stp) { xm = z.TM[k]; return stp - e; } const int eb = barOf[k];
        for (size_t q = k + 1; q < n; ++q) { if (z.TM[q] >= 1315) { xm = z.TM[q]; return O[q] - e; } if (L[q] <= stp) { xm = z.TM[q]; return stp - e; } if (H[q] >= tg) { xm = z.TM[q]; return tg - e; }
          hiSince = std::max(hiSince, H[q]); if (x == X4 && hiSince > PH) stp = std::max(stp, e);
          if (x >= S1_3 && x <= S2_15) { static const int KM[8] = {3, 5, 10, 15, 3, 5, 10, 15}; const int K = KM[x - S1_3];
            if (z.TM[q] - z.TM[k] >= K && !qcheck) { qcheck = true; const bool keep = x <= S1_15 ? hiSince >= e + 0.5 * (e - st0) : C[q] > e; if (!keep) { xm = z.TM[q]; return C[q] - e; } } }
          if ((int)q == b[barOf[q]].k1) { const int bi = barOf[q];
            if (x == X1 && bi == eb + 6 && hiSince <= PH) { xm = z.TM[q]; return C[q] - e; }
            if (x == X2 && C[q] < e - 0.5 * (e - st0)) { xm = z.TM[q]; return C[q] - e; }
            if (x == X3 && C[q] < flowAtEntry) { xm = z.TM[q]; return C[q] - e; } } }
        xm = z.TM[n - 1]; return C[n - 1] - e; };
      auto push = [&](int v, int emv, int xmv, double g, double risk) { if (!(risk > 0.5)) return; const Tr t{date, emv, xmv, (g - cost) / risk, (g - 2 * cost) / risk}; for (int st = 0; st < 3; ++st) if (inSet[st]) T[mk][st][v].push_back(t); };
      // today's trade and its exits
      { size_t k = 0; while (k < n && z.TM[k] < em) ++k; if (k < n) { const double e = sd * a[14], st = sd * a[15], tg = sd * a[16], flow = st + 0.1 * A;
          for (int x = BASE; x <= X4; ++x) { int xm; const double g = sim(k, e, st, tg, true, x, flow, xm); push(x, em, xm, g, e - st); if (x == BASE) { rediff[mk] += std::fabs((g - cost) / (e - st) - a[18]); rediffN[mk]++; } }
          for (int x = S1_3; x <= S2_15; ++x) { int xm; const double g = sim(k, e, st, tg, true, x, flow, xm); push(x, em, xm, g, e - st); }
          for (int x = Q1; x <= Q2; ++x) { const double mR = x == Q1 ? 1.0 : x == Q15 ? 1.5 : 2.0; int xm; const double g = sim(k, e, st, e + mR * (e - st), true, BASE, flow, xm); push(x, em, xm, g, e - st); } } }
      // B delayed entries after the 4th flag bar
      { const int j4 = hi + 4; const bool alive = j4 < (int)b.size() && (brk < 0 || brk > j4) && (fail < 0 || fail > j4);
        if (alive) { double fl4 = 1e18, rr = 0; for (int j = hi + 1; j <= j4; ++j) { fl4 = std::min(fl4, b[j].l); rr += b[j].h - b[j].l; }
          if (fl4 >= PH - 0.5 * M) { const bool quiet = rr / 4 <= 0.6 * poleAvg; const double lim = (PH + fl4) / 2, tg = PH + M;
            for (int kind = 0; kind < 2; ++kind) { double flow = fl4; size_t kE = 0; double eP = 0;
              for (size_t q = b[j4].k1 + 1; q < n && (int)barOf[q] <= endJ && z.TM[q] < 1260; ++q) {
                if (kind == 0) { if (H[q] > PH && L[q] > lim) break; if (L[q] <= lim) { kE = q; eP = std::min(O[q], lim); break; } }
                else { if (H[q] >= PH + 0.25) { kE = q; eP = std::max(O[q], PH + 0.25); break; } }
                flow = std::min(flow, L[q]); if ((int)q == b[barOf[q]].k1 && C[q] < PH - 0.5 * M) break; }
              if (!kE) continue; const double st = flow - 0.1 * A; int xm; const double g = sim(kE, eP, st, tg, kind == 0, BASE, flow, xm);
              push(kind == 0 ? D1H : D2H, z.TM[kE], xm, g, eP - st); if (quiet) push(kind == 0 ? D1Q : D2Q, z.TM[kE], xm, g, eP - st); } } } } } }
  // report
  struct St { double n = 0, s = 0, sc = 0, gw = 0, gl = 0, h[2] = {0, 0}, hn[2] = {0, 0}; double m() const { return n ? s / n : 0; } double hm(int k) const { return hn[k] ? h[k] / hn[k] : 0; } };
  auto take = [](std::vector<Tr> v) { std::sort(v.begin(), v.end(), [](const Tr& a, const Tr& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); St a; int lastD = 0, busy = -1;
    for (auto& t : v) { if (t.date != lastD) { lastD = t.date; busy = -1; } if (t.em <= busy) continue; busy = t.xm; a.n++; a.s += t.r; a.sc += t.rc; (t.r > 0 ? a.gw : a.gl) += std::fabs(t.r); const int k = t.date >= 20200101; a.h[k] += t.r; a.hn[k]++; } return a; };
  static const char* SN[3] = {"BOOK2 (4-5m, pole >= 3×, pullback 5-38 %, A or B)", "RULE (3-5m, pole >= 4×, 15-50 %, A-list, HH/HL, with the break)", "EVERY (3-5m, pole >= 4×, 15-50 %)"};
  printf("Flag behaviour (preregistered/2026-10-22_flag_behaviour.md) — NQ + ES 2014-01 → 2026-10, mirrored to bull. Re-simulated today's trade vs the export: mean |R difference| NQ %.3f, ES %.3f.\n",
         rediff[0] / std::max(1.0, rediffN[0]), rediff[1] / std::max(1.0, rediffN[1]));
  bool pass[NV]; for (bool& p : pass) p = true;
  for (int st = 0; st < 3; ++st) { printf("\n################ %s\n", SN[st]);
    printf("  A shape over the first up-to-6 flag bars (descriptive — those bars can come after today's entry): share of flags · breakout rate · today's avg R\n");
    for (int mk = 0; mk < 2; ++mk) { double tot = 0; for (int k = 0; k < 5; ++k) tot += shN[mk][st][k]; printf("    %s:", mk ? "ES" : "NQ");
      for (int k = 0; k < 5; ++k) printf("  %s %2.0f%% · breaks %2.0f%% · %+5.2fR |", SH[k], 100 * shN[mk][st][k] / std::max(1.0, tot), 100 * shB[mk][st][k] / std::max(1.0, shN[mk][st][k]), shR[mk][st][k] / std::max(1.0, shN[mk][st][k])); printf("\n"); }
    printf("  B / C variants — per market: trades, avg R, PF, 2014-19 / 2020-26, at +1× cost; difference vs today's trade\n");
    St base[2]; for (int mk = 0; mk < 2; ++mk) base[mk] = take(T[mk][st][BASE]);
    for (int v = 0; v < NV; ++v) { printf("    %-44s", VN[v]); bool ok = true;
      for (int mk = 0; mk < 2; ++mk) { const St a = take(T[mk][st][v]); const double d = a.m() - base[mk].m();
        if (v != BASE && !(a.n >= 30 && d >= 0.05 && a.hm(0) >= base[mk].hm(0) + 0.05 && a.hm(1) >= base[mk].hm(1) + 0.05 && a.sc / std::max(1.0, a.n) >= base[mk].sc / std::max(1.0, base[mk].n))) ok = false;
        printf(" | %s %5.0f %+6.3fR PF %.2f (%+6.3f / %+6.3f) cost %+6.3f · %+6.3f", mk ? "ES" : "NQ", a.n, a.m(), a.gl > 0 ? a.gw / a.gl : 99.0, a.hm(0), a.hm(1), a.n ? a.sc / a.n : 0, d); }
      if (v != BASE && (st == 0 || st == 2) && !ok) pass[v] = false; printf("%s\n", v != BASE && ok ? "  ← better on both" : ""); } }
  printf("\nPASS (better by >= 0.05R on BOOK2 AND EVERY, both markets, both halves, and at +1× cost):"); for (int v = 1; v < NV; ++v) printf("\n  %-44s %s", VN[v], pass[v] ? "PASSES → account EV check next" : "no"); printf("\n");
  return 0;
}
