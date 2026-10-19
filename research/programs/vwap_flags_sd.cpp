// 2026-10-19 (founder: "test flags with vwap and also test vwap with supply and demand"). NQ NY 09:45–15:00, 1.5R target held to 15:55,
// 2 pts cost, one position at a time per rule, <= 5 a day.
// PART 1 — flags (1-5m, with the day: straightness >= 0.40, day's side of the 09:30 open, pole 6 bars >= 0.15 ATR, flag 5 <= 0.7 x pole,
// stop beyond the flag) by where the flag sits against VWAP at the breakout:
//   V0 no VWAP condition · V1 breakout on the day's side of VWAP · V2 flag pulled back TO VWAP (its extreme within 0.10 ATR of VWAP) ·
//   V3 flag held VWAP (extreme stayed on the day's side) · V4 flag stretched (extreme > 0.30 ATR beyond VWAP) · V5 flag dipped through VWAP
//   and the breakout closed back on the day's side (reclaim).
// PART 2 — supply and demand on the 5m chart: a base of 1-3 small bars (each range <= 0.5 x the leg-out bar) followed by a leg-out bar
// with body >= 0.15 ATR closing beyond the base = a zone (demand below a rally, supply above a drop). First return only: a 1m bar touching
// the zone's near edge while closing inside/beyond it → enter at that close, stop beyond the zone's far edge + 0.25.
//   Z0 any zone · Z1 zones with the day · Z2 zones that contain VWAP (± 0.10 ATR) at the touch · Z3 with the day + contain VWAP ·
//   W1 plain VWAP touch with the day (low/high touches VWAP, closes back on the day's side, stop 0.10 ATR beyond VWAP).
// Control per rule (20 random draws per trade, averaged): the same count per day at random minutes with the rule's context, same stop size. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;
struct FB { double o, h, l, c; };
struct St { double n = 0, w = 0, p = 0, pe[3] = {0, 0, 0}, ne[3] = {0, 0, 0}; void add(int e, bool win, double pts) { n++; if (win) w++; p += pts; pe[e] += pts; ne[e]++; } };
struct Zone { int dir; double nearE, farE; bool used; int born; };
enum { V0, V1, V2, V3, V4, V5, Z0, Z1, Z2, Z3, W1, S1, S2, LV, B1, B2, LB, NR };
static const char* RN[NR] = {"V0 flag, no VWAP condition", "V1 flag, breakout on the day's side of VWAP", "V2 flag pulled back TO VWAP", "V3 flag held VWAP (never crossed)", "V4 flag stretched > 0.3 ATR from VWAP", "V5 flag dipped through VWAP, reclaimed",
  "Z0 supply/demand zone, any", "Z1 zone with the day", "Z2 zone containing VWAP", "Z3 zone with the day + containing VWAP", "W1 VWAP touch with the day (no zone)", "S1 flag pulled back to the +-1 SD band (day side)", "S2 flag pulled back past VWAP to the far 1 SD band", "LV flag pulled back to a level", "B1 1 SD band touch with the day (no flag)", "B2 2 SD band fade against the day (no flag)", "LB level touch with the day (no flag)"};
int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(71); St acc[NR], ctl[NR]; double days = 0;
  for (auto& d : s.days) {
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; days++; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue;
    const size_t n = I.size(); std::vector<double> VW(n), ST(n); std::vector<int> D(n);
    std::vector<double> SDV(n); double pq = 0;
    { double so = s.o[I[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0; for (size_t k = 0; k < n; ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; { double tp = (s.h[i] + s.l[i] + s.c[i]) / 3.0; pq += tp * tp * s.v[i]; }
        VW[k] = vv > 0 ? pv / vv : s.c[i]; SDV[k] = vv > 0 ? std::sqrt(std::max(0.0, pq / vv - VW[k] * VW[k])) : 0; ST[k] = hi > lo ? std::fabs(s.c[i] - so) / (hi - lo) : 0; D[k] = s.c[i] > so ? 1 : s.c[i] < so ? -1 : 0; } }
    // levels: prior day high / low / close, overnight high / low, opening range (09:30-09:45) high / low, round 100s (added on the fly)
    std::vector<double> LVL = {d.ph, d.pl, d.pc}; { double oh = -1e18, ol = 1e18; for (int i = d.b0; i < d.rth0; ++i) { oh = std::max(oh, (double)s.h[i]); ol = std::min(ol, (double)s.l[i]); } if (oh > -1e17) { LVL.push_back(oh); LVL.push_back(ol); } }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < hm_to_tday(9, 45); ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
    auto nearLevel = [&](double p, double tol) { for (double L : LVL) if (!std::isnan(L) && std::fabs(p - L) <= tol) return true; if (std::fabs(p - orh) <= tol || std::fabs(p - orl) <= tol) return true; double r = std::round(p / 100.0) * 100.0; return std::fabs(p - r) <= tol; };
    auto inWin = [&](size_t k) { int tm = tday_min(s.t[I[k]]); return tm >= hm_to_tday(9, 45) && tm < hm_to_tday(15, 0); };
    auto run = [&](size_t k0, int dir, double R, bool& win, double& pts) -> size_t { double e = s.c[I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN; size_t k;
      for (k = k0 + 1; k < n; ++k) { int i = I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } if (tday_min(s.t[i]) >= hm_to_tday(15, 55)) { px = s.c[i]; break; } }
      if (k >= n) { k = n - 1; px = s.c[I[k]]; } double net = dir * (px - e) - 2.0 * A / 320; win = net > 0; pts = net * 320 / A; return k; };
    // candidate signals per minute: dir and stop distance per rule (0 = none)
    std::vector<std::array<double, NR>> SR(n); std::vector<std::array<int, NR>> SD(n); for (auto& a : SR) a.fill(0); for (auto& a : SD) a.fill(0);
    std::vector<FB> b[5]; std::vector<FB> z5; std::vector<Zone> zones;
    for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
      for (int t = 0; t < 5; ++t) { int tf = t + 1; if ((tm - hm_to_tday(9, 30)) % tf == 0 || b[t].empty()) b[t].push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i]});
        else { FB& x = b[t].back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; } }
      const double c = s.c[i], vw = VW[k]; const int Dd = D[k];
      // zones: on each 5m close, look for base + leg-out ending at this bar
      if ((tm + 1 - hm_to_tday(9, 30)) % 5 == 0) { const auto& v = b[4]; int m = v.size();
        if (m >= 2) { const FB& lo_ = v[m - 1]; double body = lo_.c - lo_.o, rng = lo_.h - lo_.l;
          if (std::fabs(body) >= 0.15 * A) for (int nb = 1; nb <= 3 && m - 1 - nb >= 0; ++nb) { bool small = true; double bh = -1e18, bl = 1e18;
              for (int q = m - 1 - nb; q < m - 1; ++q) { if (v[q].h - v[q].l > 0.5 * rng) small = false; bh = std::max(bh, v[q].h); bl = std::min(bl, v[q].l); }
              if (!small) continue; if (body > 0 && lo_.c > bh) zones.push_back({1, bh, bl, false, (int)k}); else if (body < 0 && lo_.c < bl) zones.push_back({-1, bl, bh, false, (int)k}); break; } } }
      if (!inWin(k)) continue;
      // PART 1 flags
      if (Dd && ST[k] >= 0.40) for (int t = 4; t >= 0; --t) { int tf = t + 1; if ((tm + 1 - hm_to_tday(9, 30)) % tf) continue; const auto& v = b[t]; int m = v.size(); if (m < 12) continue;
          int p0 = m - 12, p1 = m - 7; double pole = Dd * (v[p1].c - v[p0].o); if (pole < 0.15 * A) continue;
          double fh = -1e18, fl = 1e18; for (int q = m - 6; q < m - 1; ++q) { fh = std::max(fh, v[q].h); fl = std::min(fl, v[q].l); } if (fh - fl > 0.7 * pole) continue;
          if (Dd > 0 ? !(c > fh) : !(c < fl)) continue; double R = Dd * (c - (Dd > 0 ? fl - 0.25 : fh + 0.25)); if (!(R > 0.25) || R > 0.6 * A) continue;
          double ext = Dd > 0 ? fl : fh, gapv = Dd * (ext - vw);   // >0: flag extreme stayed on the day's side
          bool side = Dd * (c - vw) > 0;
          auto set = [&](int r) { SR[k][r] = R; SD[k][r] = Dd; };
          double sd = SDV[k]; if (sd > 0 && side && std::fabs(ext - (vw + Dd * sd)) <= 0.10 * A) set(S1); if (sd > 0 && side && std::fabs(ext - (vw - Dd * sd)) <= 0.10 * A) set(S2); if (side && nearLevel(ext, 0.05 * A)) set(LV);
          set(V0); if (side) set(V1); if (std::fabs(gapv) <= 0.10 * A && side) set(V2); if (gapv > 0 && side) set(V3); if (gapv > 0.30 * A) set(V4); if (gapv < 0 && side) set(V5); break; }
      // PART 2 zones: first touch of the near edge on this minute
      for (auto& z : zones) { if (z.used || (int)k <= z.born) continue; bool touch = z.dir > 0 ? s.l[i] <= z.nearE : s.h[i] >= z.nearE; if (!touch) continue; z.used = true;
        bool holds = z.dir > 0 ? c > z.farE : c < z.farE; if (!holds) continue; double R = z.dir * (c - (z.dir > 0 ? z.farE - 0.25 : z.farE + 0.25)); if (!(R > 0.25) || R > 0.6 * A) continue;
        bool withDay = z.dir == Dd, hasVW = vw >= std::min(z.nearE, z.farE) - 0.10 * A && vw <= std::max(z.nearE, z.farE) + 0.10 * A;
        auto set = [&](int r) { if (!SD[k][r]) { SR[k][r] = R; SD[k][r] = z.dir; } };
        set(Z0); if (withDay) set(Z1); if (hasVW) set(Z2); if (withDay && hasVW) set(Z3); }
      if (Dd && (Dd > 0 ? s.l[i] <= vw && c > vw : s.h[i] >= vw && c < vw)) { SR[k][W1] = 0.10 * A + std::fabs(c - vw); SD[k][W1] = Dd; }
      { double sd = SDV[k]; if (Dd && sd > 0) { double band = vw + Dd * sd; if (Dd > 0 ? s.l[i] <= band && c > band : s.h[i] >= band && c < band) { SR[k][B1] = 0.10 * A + std::fabs(c - band); SD[k][B1] = Dd; }
          double b2 = vw + Dd * 2 * sd; if (Dd > 0 ? s.h[i] >= b2 && c < b2 : s.l[i] <= b2 && c > b2) { SR[k][B2] = 0.10 * A + std::fabs(c - b2); SD[k][B2] = -Dd; } }
        if (Dd) { double ext1 = Dd > 0 ? s.l[i] : s.h[i]; for (double L : LVL) { if (std::isnan(L)) continue; if (Dd > 0 ? ext1 <= L && c > L : ext1 >= L && c < L) { SR[k][LB] = 0.10 * A + std::fabs(c - L); SD[k][LB] = Dd; break; } } } } }
    for (int r = 0; r < NR; ++r) { size_t busy = 0; bool have = false; int nd = 0; std::vector<double> Rs; std::vector<int> Ds;
      for (size_t k = 0; k < n; ++k) { if (!SD[k][r] || (have && k <= busy) || nd >= 5) continue; bool w; double p; busy = run(k, SD[k][r], SR[k][r], w, p); have = true; nd++; acc[r].add(era, w, p); Rs.push_back(SR[k][r]); }
      // control: random in-window minutes, direction = day's side for with-the-day rules, coin flip otherwise
      std::vector<size_t> cand; for (size_t k = 0; k < n; ++k) if (inWin(k) && D[k]) cand.push_back(k);
      for (int rep = 0; rep < 20; ++rep) { size_t after = 0; for (double R : Rs) { std::vector<size_t> ok; for (size_t k : cand) if (k > after) ok.push_back(k); if (ok.empty()) break; size_t k = ok[std::uniform_int_distribution<size_t>(0, ok.size() - 1)(rng)];
        int dir = r == B2 ? -D[k] : (r == Z0 || r == Z2) ? ((rng() & 1) ? 1 : -1) : D[k]; bool w; double p; after = run(k, dir, R, w, p); ctl[r].add(era, w, p); } } } }
  printf("%s NY — flags with VWAP, and supply/demand with VWAP. 1.5R to 15:55, 2 pts cost, one at a time, <= 5 a day per rule. %.0f days.\n"
         "pts = net NQ-equivalent points per trade at today's range. Columns: /day | win %% | pts all | pts 2010-18 / 2019-24 / 2025-26 | control win %% and pts.\n\n", sym, days);
  for (int r = 0; r < NR; ++r) { if (r == Z0 || r == S1) printf("\n"); const St& a = acc[r]; const St& c = ctl[r];
    printf("  %-44s %5.2f | %4.1f%% | %+6.2f | %+6.2f / %+6.2f / %+6.2f | control %4.1f%% %+6.2f\n", RN[r], a.n / days, 100 * a.w / std::max(1.0, a.n), a.p / std::max(1.0, a.n),
           a.pe[0] / std::max(1.0, a.ne[0]), a.pe[1] / std::max(1.0, a.ne[1]), a.pe[2] / std::max(1.0, a.ne[2]), 100 * c.w / std::max(1.0, c.n), c.p / std::max(1.0, c.n)); }
  return 0;
}
