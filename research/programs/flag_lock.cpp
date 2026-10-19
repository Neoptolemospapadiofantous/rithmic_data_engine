// 2026-10-19 (founder: "I want 3-5 flags a day" → "test profit lock"). The loose flag streams that reach several flags a day — NQ and ES,
// NY 1-5m flags with the day, levels L2 (straightness >= 0.40, open + VWAP side, pole 6 >= 0.15 ATR, flag 5 <= 0.7 x pole) and L4 (same,
// no VWAP condition, pole 4 / flag 3), one at a time, <= 5 a day per market, entries 09:45–15:00, 2 pts cost — under profit locks:
//  in-trade: E0 1.5R target to 15:55 (base) · E1 at +0.5R stop → break-even · E2 at +1R stop → +0.25R · E3 at +1R stop → +0.5R ·
//            E4 at +1R stop → +0.5R, target 2R · E5 at +1R stop → +0.5R, target 3R · E6 at +0.75R stop → +0.25R
//  daily (on top of E0): D1 stop the day once up >= +1.5R · D2 once up >= +3R · D3 after 2 losses · D4 after the first win
// Lock moves are decided on 1-minute closes and act from the next minute. Win = net > 0. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0, r = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int era, bool win, double pts, double R) { n++; if (win) w++; p += pts; r += R; pe[era] += pts; ne[era]++; } };
static const char* EN[7] = {"E0 1.5R to close (base)", "E1 +0.5R → break-even", "E2 +1R → lock +0.25R", "E3 +1R → lock +0.5R", "E4 +1R → lock +0.5R, target 2R", "E5 +1R → lock +0.5R, target 3R", "E6 +0.75R → lock +0.25R"};
static const char* DN[5] = {"none", "D1 stop day at +1.5R", "D2 stop day at +3R", "D3 stop after 2 losses", "D4 stop after the first win"};
int main() {
  const double STR = 0.40, PM = 0.15, FT = 0.7; const bool VWL[2] = {true, false}; const int PN[2] = {6, 4}, FNN[2] = {5, 3};
  printf("Profit locks on the loose NY 1-5m flag streams (with the day), NQ and ES, one at a time, <= 5 a day each, 2 pts cost.\n"
         "pts = net NQ-equivalent points per trade at today's range; total = pts per trade x trades per day x 252 (per 1 NQ-sized unit a year).\n");
  for (int lv = 0; lv < 2; ++lv) for (const char* sym : {"NQ", "ES"}) {
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    build_days(s, fomc, vix);
    printf("\n== %s — %s\n  %-34s %-26s %6s %6s %8s %8s %8s %8s\n", sym, lv ? "L4 (no VWAP, pole 4 / flag 3)" : "L2 (straightness .40, pole .15, flag .7)", "in-trade lock", "daily rule", "/day", "WR", "pts", "2025-26", "2019-24", "pts/yr");
    for (int ex = 0; ex < 7; ++ex) for (int dr = 0; dr < 5; ++dr) { if (ex > 0 && dr > 0) continue; St a; double days = 0;
      for (auto& d : s.days) {
        if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; days++; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
        std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue;
        std::vector<float> ST(I.size()); std::vector<signed char> D(I.size()), VS(I.size());
        { double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0; for (size_t k = 0; k < I.size(); ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
            double c = s.c[i], vw = vv > 0 ? pv / vv : c; ST[k] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; D[k] = c > so ? 1 : c < so ? -1 : 0; VS[k] = c > vw ? 1 : c < vw ? -1 : 0; } }
        std::vector<FB> b[5]; size_t busy = 0; bool have = false; int nd = 0, losses = 0, wins = 0; double dayR = 0;
        for (size_t k = 0; k < I.size(); ++k) { int i = I[k], tm = tday_min(s.t[i]);
          for (int t = 0; t < 5; ++t) { int tf = t + 1; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
            else { FB& x = b[t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } }
          if ((have && k <= busy) || nd >= 5) continue;
          if ((dr == 1 && dayR >= 1.5) || (dr == 2 && dayR >= 3) || (dr == 3 && losses >= 2) || (dr == 4 && wins >= 1)) break;
          if (!(D[k] && ST[k] >= STR && (!VWL[lv] || VS[k] == D[k]) && tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0))) continue;
          for (int t = 4; t >= 0; --t) { int tf = t + 1; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; const auto& v = b[t]; int n = v.size(), N = PN[lv], F = FNN[lv]; if (n < N + F + 1) continue;
            int p0 = n - 1 - F - N, p1 = n - 2 - F, Dd = D[k]; double pole = Dd * (v[p1].c - v[p0].o); if (pole < PM * A) continue;
            double fh = -1e18, fl = 1e18; for (int q = n - 1 - F; q < n - 1; ++q) { fh = std::max(fh, v[q].h); fl = std::min(fl, v[q].l); } if (fh - fl > FT * pole) continue;
            double c = v[n - 1].c; if (Dd > 0 ? !(c > fh) : !(c < fl)) continue; double R = Dd * (c - (Dd > 0 ? fl - 0.25 : fh + 0.25)); if (!(R > 0.25) || R > 0.6 * A) continue;
            // exit with the chosen lock
            double tgtR = ex == 4 ? 2 : ex == 5 ? 3 : 1.5, trig = ex == 1 ? 0.5 : ex == 6 ? 0.75 : ex >= 2 ? 1.0 : 1e9, lockR = ex == 1 ? 0 : ex == 2 || ex == 6 ? 0.25 : 0.5;
            double e = c, st = e - Dd * R, tg = e + Dd * tgtR * R, px = NAN; size_t kk; bool armed = false;
            for (kk = k + 1; kk < I.size(); ++kk) { int j = I[kk]; if (Dd > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; break; } if (Dd > 0 ? s.h[j] >= tg : s.l[j] <= tg) { px = tg; break; }
              if (tday_min(s.t[j]) >= hm_to_tday(15, 55)) { px = s.c[j]; break; }
              if (!armed && Dd * (s.c[j] - e) >= trig * R) { armed = true; double ns = e + Dd * lockR * R; st = Dd > 0 ? std::max(st, ns) : std::min(st, ns); } }
            if (kk >= I.size()) { kk = I.size() - 1; px = s.c[I[kk]]; }
            double net = Dd * (px - e) - 2.0 * A / 320; a.add(era, net > 0, net * 320 / A, net / R); dayR += net / R; if (net > 0) wins++; else losses++;
            busy = kk; have = true; nd++; break; } } }
      double pd = a.n / days;
      printf("  %-34s %-26s %6.2f %5.1f%% %+8.2f %+8.2f %+8.2f %+8.0f\n", EN[ex], DN[dr], pd, 100 * a.w / a.n, a.p / a.n, a.pe[2] / std::max(1.0, a.ne[2]), a.pe[1] / std::max(1.0, a.ne[1]), a.p / a.n * pd * 252); } }
  return 0;
}
