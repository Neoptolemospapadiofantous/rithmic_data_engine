// 2026-10-19 (founder: "more tests" after flag_features): the refined flag rule built from the findings, as a pre-declared ladder.
// NY, entries 10:00–15:00 in the 10:00 direction (side of the 09:30 open at the 10:00 close); flag = pole 6 bars, flag 5 bars, close
// beyond the flag; stop beyond the flag + 0.25; 1.5R target held to 15:55; one position at a time, <= 3 a day; 2 pts cost.
//  R0 3/4/5-min charts, pole >= 0.20 ATR, flag <= 0.6 x pole
//  R1 R0 + minimum stop 0.05 ATR (≈ 16 NQ pts today; tiny flags lose to cost)
//  R2 R1 + strong breakout bar (closes in the outer 30 % of its range, in the trade's direction)
//  R3 R2 with the flag allowed up to 0.8 x pole
//  R4 R2 adding the 15-min chart
//  R5 R2 with NO ATR rules: any pole size in the 10:00 direction, no minimum or maximum stop (founder 2026-10-19 "remove atr filter")
//  R6 R0 with NO ATR rules and no strong-bar rule
//  R7-R11 (founder: "up to 5 flags a day"): R6 with a cap of 5 a day; + 2m; + 2m and 1m; + 15m; 1-5m with overlapping trades (one open per chart)
// Control: 20 random draws per trade at minutes after 10:00 with the same 10:00 direction, same stop size, same exit.
//  R12 / R13: R7 with entries only until 12:00 / 13:00. argv[1] = symbol (default NQ; NQ also writes inputs_20261019/flagr7_k.csv = rung R7 and flagr12_k.csv = rung R12).
// Writes research/programs/inputs_20261019/flagref_k.csv (rung R2, _k format: 10th column = stop / ATR) for the account simulators.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int e, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[e] += pts; ne[e]++; } };
int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ"; const bool isNQ = std::string(sym) == "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int NRG = 14; const char* RN[NRG] = {"R0 10:00 direction, 3-5m, pole >= .20, flag <= .6", "R1 + minimum stop 0.05 ATR", "R2 + strong breakout bar", "R3 + flag up to 0.8 x pole", "R4 R2 + 15-min chart", "R5 R2 with NO ATR rules (no minimum pole, no stop cap)", "R6 R0 with NO ATR rules (no strong-bar rule either)", "R7 R6 with up to 5 a day", "R8 R7 + 2-min chart", "R9 R7 + 2-min and 1-min charts", "R10 R9 + 15-min chart", "R11 R9 with overlapping trades (one open per chart)", "R12 R7 with entries 10:00-12:00 only", "R13 R7 with entries 10:00-13:00 only"};
  std::mt19937 rng(83); St acc[NRG][2], ctl[NRG][2], ctl2[NRG][2]; struct DI { std::vector<int> I; int d10, era; double A; }; std::vector<DI> all; std::vector<double> RA[NRG]; double days = 0; FILE* out = isNQ ? fopen("research/programs/inputs_20261019/flagref_k.csv", "w") : nullptr; FILE* out7 = isNQ ? fopen("research/programs/inputs_20261019/flagr7_k.csv", "w") : nullptr; FILE* out12 = isNQ ? fopen("research/programs/inputs_20261019/flagr12_k.csv", "w") : nullptr;
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; days++; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue; const size_t n = I.size();
    const double so = s.o[I[0]]; int d10 = 0; for (size_t k = 0; k < n; ++k) if (tday_min(s.t[I[k]]) == hm_to_tday(9, 59)) d10 = s.c[I[k]] > so ? 1 : s.c[I[k]] < so ? -1 : 0; if (!d10) continue;
    auto run = [&](size_t k0, int dir, double R, bool& win, double& pts) -> size_t { double e = s.c[I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN; size_t k;
      for (k = k0 + 1; k < n; ++k) { int i = I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
      if (k >= n) { k = n - 1; px = s.c[I[k]]; } double net = dir * (px - e) - 2.0 * A / 320; win = net > 0; pts = net * 320 / A; return k; };
    all.push_back({I, d10, era, A});
    std::vector<size_t> cand; for (size_t k = 0; k < n; ++k) { int tm = tday_min(s.t[I[k]]); if (tm >= hm_to_tday(10, 0) && tm < hm_to_tday(15, 0)) cand.push_back(k); }
    for (int rg = 0; rg < NRG; ++rg) {
      const std::vector<int> TF = rg == 4 ? std::vector<int>{5, 4, 3, 15} : rg == 8 ? std::vector<int>{5, 4, 3, 2} : rg == 10 ? std::vector<int>{15, 5, 4, 3, 2, 1} : (rg == 9 || rg == 11) ? std::vector<int>{5, 4, 3, 2, 1} : std::vector<int>{5, 4, 3};
      const int ntf = TF.size(), CAP = rg >= 7 ? 5 : 3; const bool OVL = rg == 11; const double FT = rg == 3 ? 0.8 : 0.6;
      std::vector<FB> b[6]; size_t busy = 0, busyT[6] = {0}; bool have = false, haveT[6] = {false}; int nd = 0; std::vector<double> Rs;
      for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
        for (int t = 0; t < ntf; ++t) { int tf = TF[t]; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
          else { FB& x = b[t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } }
        if (tm < hm_to_tday(10, 0) || tm >= hm_to_tday(rg == 12 ? 12 : rg == 13 ? 13 : 15, 0) || (!OVL && have && k <= busy) || nd >= CAP) continue;
        for (int t = 0; t < ntf; ++t) { int tf = TF[t]; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; if (OVL && haveT[t] && k <= busyT[t]) continue; const auto& v = b[t]; int m = v.size(); if (m < 12) continue;
          int p0 = m - 12, p1 = m - 7, D = d10; double pole = D * (v[p1].c - v[p0].o); if (pole <= 0 || (rg < 5 && pole < 0.20 * A)) continue;
          double fh = -1e18, fl = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, v[q].h); fl = std::min(fl, v[q].l); } if (fh - fl > FT * pole) continue;
          const FB& bb = v[m - 1]; double c = bb.c; if (D > 0 ? !(c > fh) : !(c < fl)) continue;
          double R = D * (c - (D > 0 ? fl - 0.25 : fh + 0.25)); if (!(R > 0.25) || (rg < 5 && R > 0.6 * A)) continue;
          if (rg >= 1 && rg < 5 && R < 0.05 * A) continue;
          if (rg >= 2 && rg <= 5) { double br = bb.h - bb.l; double pos = br > 0 ? (D > 0 ? (c - bb.l) / br : (bb.h - c) / br) : 0; if (pos < 0.7) continue; }
          bool win; double pts; size_t kx = run(k, D, R, win, pts); busy = kx; have = true; nd++; busyT[t] = kx; haveT[t] = true; acc[rg][D > 0 ? 0 : 1].add(era, win, pts, (pts * A / 320) / R); Rs.push_back(R); RA[rg].push_back(R / A);
          if (rg == 2 && out) fprintf(out, "%d,%ld,%ld,%d,%.4f,%.4f,0,0,%d,%.5f\n", d.date, (long)s.t[I[k]], (long)s.t[I[kx]], D, pts * A / 320 + 2.0 * A / 320, pts * A / 320 + 2.0 * A / 320 - 1, tf, R / A);
          if (rg == 7 && out7) fprintf(out7, "%d,%ld,%ld,%d,%.4f,%.4f,0,0,%d,%.5f\n", d.date, (long)s.t[I[k]], (long)s.t[I[kx]], D, pts * A / 320 + 2.0 * A / 320, pts * A / 320 + 2.0 * A / 320 - 1, tf, R / A);
          if (rg == 12 && out12) fprintf(out12, "%d,%ld,%ld,%d,%.4f,%.4f,0,0,%d,%.5f\n", d.date, (long)s.t[I[k]], (long)s.t[I[kx]], D, pts * A / 320 + 2.0 * A / 320, pts * A / 320 + 2.0 * A / 320 - 1, tf, R / A);
          break; } }
      for (int rep = 0; rep < 20; ++rep) { size_t after = 0; for (double R : Rs) { std::vector<size_t> ok; for (size_t k : cand) if (k > after) ok.push_back(k); if (ok.empty()) break;
          size_t k = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)]; bool w; double p; after = run(k, d10, R, w, p); ctl[rg][d10 > 0 ? 0 : 1].add(era, w, p, (p * A / 320) / R); } } } }
  if (out) fclose(out); if (out7) fclose(out7); if (out12) fclose(out12);
  // fair control: random day (any day with a 10:00 direction), random minute 10:00-15:00, 10:00 direction, stop = the flag trade's stop in ATR
  for (int rg = 0; rg < NRG; ++rg) for (int rep = 0; rep < 20; ++rep) for (double ra : RA[rg]) { const DI& x = all[std::uniform_int_distribution<size_t>(0, all.size() - 1)(rng)];
      std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) { int tm = tday_min(s.t[x.I[k]]); if (tm >= hm_to_tday(10, 0) && tm < hm_to_tday(15, 0)) cd.push_back(k); } if (cd.empty()) continue;
      size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; int dir = x.d10; double R = ra * x.A, e = s.c[x.I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN;
      for (size_t k = k0 + 1; k < x.I.size(); ++k) { int i = x.I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
      if (std::isnan(px)) px = s.c[x.I.back()]; double net = dir * (px - e) - 2.0 * x.A / 320; ctl2[rg][dir > 0 ? 0 : 1].add(x.era, net > 0, net * 320 / x.A, net / R); }
  printf("Refined flag rule ladder — %s NY, 10:00 direction, entries 10:00-15:00, 1.5R to 15:55, <= 3 a day, 2 pts cost. %.0f days.\n"
         "Per rung and side: trades/day | win %% | pts/trade (and R) | pts 2010-18 / 2019-24 / 2025-26 (n) | controls (20 random draws, same direction and stop): flag-day = random minutes on the SAME days (hindsight: those days trended), ANY-day = random days + random minutes after 10:00 (fair).\n", sym, days);
  for (int rg = 0; rg < NRG; ++rg) { printf("\n== %s\n", RN[rg]); St t, tc, tc2; for (int sd = 0; sd < 2; ++sd) { const St& a = acc[rg][sd]; const St& c = ctl[rg][sd];
      printf("  %-5s %5.2f/day | %4.1f%% | %+6.2f (%+.3fR) | %+6.2f / %+6.2f / %+6.2f (%3.0f) | flag-day control %+6.2f | ANY-day control %+6.2f\n", sd ? "bear" : "bull", a.n / days, 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n), a.r / std::max(1.0, a.n),
             a.pe[0] / std::max(1.0, a.ne[0]), a.pe[1] / std::max(1.0, a.ne[1]), a.pe[2] / std::max(1.0, a.ne[2]), a.ne[2], c.p / std::max(1.0, c.n), ctl2[rg][sd].p / std::max(1.0, ctl2[rg][sd].n));
      t.n += a.n; t.w += a.w; t.p += a.p; t.r += a.r; tc.n += c.n; tc.p += c.p; tc2.n += ctl2[rg][sd].n; tc2.p += ctl2[rg][sd].p; for (int e = 0; e < 3; ++e) { t.pe[e] += a.pe[e]; t.ne[e] += a.ne[e]; } }
    printf("  both  %5.2f/day | %4.1f%% | %+6.2f (%+.3fR) | %+6.2f / %+6.2f / %+6.2f (%3.0f) | flag-day control %+6.2f | ANY-day control %+6.2f\n", t.n / days, 100 * t.w / t.n, t.p / t.n, t.r / t.n, t.pe[0] / t.ne[0], t.pe[1] / t.ne[1], t.pe[2] / std::max(1.0, t.ne[2]), t.ne[2], tc.p / tc.n, tc2.p / tc2.n); }
  return 0;
}
