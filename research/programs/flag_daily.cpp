// 2026-10-19 (founder: "has and must be 1-5 flags a day"). The best flag stream that delivers 1-5 trades a day. Candidate pool: every NY
// flag on the 1/2/3/4/5/15-min charts (pole 6 bars >= 0.10 ATR, flag 5 bars <= 0.8 x pole, close beyond the flag), entries 09:45-15:00,
// stop beyond the flag, 1.5R held to 15:55, 2 pts cost. Each flag gets the IS-built quality score of flag_features.cpp (19 real-time
// features, weights = their 2010-18 Spearman with net R, z-scored on 2010-18). A stream = flags whose score is above a cut (the cut is a
// percentile of the 2010-18 scores), taken one at a time, <= 5 a day. Optional hard rule: after 10:00 only in the 10:00 direction, none
// before. Reported: trades a day (2010-18 / 2019-26), share of days with >= 1 trade, net pts and R per trade by period, and a FAIR control
// (5 draws: random day, random minute 10:00-15:00, the day's 10:00 direction, the trade's stop in ATR, same exit). Writes inputs_20261019/flagdaily40_k.csv = the "any direction, top 40 %" stream.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
#include <algorithm>
#include <functional>
using namespace lab;
static const int TFS[6] = {1, 2, 3, 4, 5, 15};
enum { F_TF, F_POLE, F_POLEER, F_POLEDIR, F_FLAGRATIO, F_RETRACE, F_SLOPE, F_CHAN, F_VOLR, F_BRKPOS, F_BRKBODY, F_BRKDIST, F_RISK, F_ST, F_MOVE, F_WITH, F_VWSIDE, F_VWDIST, F_TIME, F_GAP, F_PREV, F_VIX, F_D10, F_NTH, NF };
struct Sig { int date, side, era, day; long te, tx; double x[NF], pts, r, ra; bool win, d10ok; size_t ke; };
struct B { double o, h, l, c, v; int k; };
static double spearman(std::vector<std::pair<double, double>> v) { size_t n = v.size(); if (n < 30) return 0;
  auto rank = [&](int w) { std::vector<size_t> id(n); for (size_t i = 0; i < n; ++i) id[i] = i; std::sort(id.begin(), id.end(), [&](size_t a, size_t b) { return (w ? v[a].second : v[a].first) < (w ? v[b].second : v[b].first); });
    std::vector<double> r(n); for (size_t i = 0; i < n;) { size_t j = i; double val = w ? v[id[i]].second : v[id[i]].first; while (j < n && (w ? v[id[j]].second : v[id[j]].first) == val) ++j; for (size_t q = i; q < j; ++q) r[id[q]] = (i + j - 1) / 2.0; i = j; } return r; };
  auto a = rank(0), b = rank(1); double m = (n - 1) / 2.0, sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (a[i] - m) * (b[i] - m); saa += (a[i] - m) * (a[i] - m); sbb += (b[i] - m) * (b[i] - m); } return saa > 0 && sbb > 0 ? sab / std::sqrt(saa * sbb) : 0; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DI { std::vector<int> I; int d10, era, date, dow; double A, vixv, atrpct, gap, or30; }; std::vector<DI> days; std::vector<Sig> sig; double nd[3] = {0, 0, 0};
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.rth0 < 0 || d.date > 20261016) continue; const double A = d.atr; const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    std::vector<int> I; for (int i = d.rth0; i < d.b1 && tday_min(s.t[i]) < hm_to_tday(16, 0); ++i) I.push_back(i); if (I.size() < 60) continue; const size_t n = I.size(); nd[era]++;
    const int prevDir = (!std::isnan(d.pc) && !std::isnan(d.pc2)) ? (d.pc > d.pc2 ? 1 : d.pc < d.pc2 ? -1 : 0) : 0;
    std::vector<double> ST(n), MV(n), VW(n); std::vector<int> D(n); const double so = s.o[I[0]];
    { double hi = -1e18, lo = 1e18, pv = 0, vv = 0; for (size_t k = 0; k < n; ++k) { int i = I[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        VW[k] = vv > 0 ? pv / vv : s.c[i]; ST[k] = hi > lo ? std::fabs(s.c[i] - so) / (hi - lo) : 0; MV[k] = (s.c[i] - so) / A; D[k] = s.c[i] > so ? 1 : s.c[i] < so ? -1 : 0; } }
    const int gapDir = !std::isnan(d.pc) ? (so > d.pc ? 1 : so < d.pc ? -1 : 0) : 0; int d10 = 0; for (size_t k = 0; k < n; ++k) if (tday_min(s.t[I[k]]) == hm_to_tday(9, 59)) d10 = D[k];
    double or30 = 0; { double h3 = -1e18, l3 = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < hm_to_tday(10, 0); ++k) { h3 = std::max(h3, (double)s.h[I[k]]); l3 = std::min(l3, (double)s.l[I[k]]); } or30 = (h3 - l3) / A; }
    days.push_back({I, d10, era, d.date, d.dow, A, std::isnan(d.vix) ? 18.0 : d.vix, 0, std::isnan(d.pc) ? 0 : std::fabs(so - d.pc) / A, or30}); const int di = days.size() - 1; int nth = 0;
    for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<B> b;
      for (size_t k = 0; k < n; ++k) { int i = I[k], g = (tday_min(s.t[i]) - hm_to_tday(9, 30)) / tf;
        if (b.empty() || (tday_min(s.t[I[b.back().k]]) - hm_to_tday(9, 30)) / tf != g) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (double)s.v[i], (int)k});
        else { B& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.v += s.v[i]; x.k = k; } }
      for (int q = 11; q < (int)b.size(); ++q) { int p0 = q - 11, p1 = q - 6; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < 0.10 * A) continue;
        double fh = -1e18, fl = 1e18, fv = 0, pv2 = 0; for (int r = q - 5; r < q; ++r) { fh = std::max(fh, b[r].h); fl = std::min(fl, b[r].l); fv += b[r].v; } if (fh - fl > 0.8 * ap) continue;
        double c = b[q].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue; size_t k = b[q].k; int tm = tday_min(s.t[I[k]]); if (tm < hm_to_tday(9, 45) || tm >= hm_to_tday(15, 0)) continue;
        double stop = fd > 0 ? fl - 0.25 : fh + 0.25, R = fd * (c - stop); if (R <= 0.25 || R > 0.6 * A) continue;
        Sig z; z.date = d.date; z.side = fd > 0 ? 0 : 1; z.era = era; z.day = di; z.te = s.t[I[k]];
        double path = 0; int dirBars = 0; for (int r = p0; r <= p1; ++r) { path += std::fabs(b[r].c - b[r].o); if (fd * (b[r].c - b[r].o) > 0) dirBars++; pv2 += b[r].v; }
        int chan = 0; for (int r = q - 4; r < q; ++r) if (fd > 0 ? b[r].h < b[r - 1].h : b[r].l > b[r - 1].l) chan++; double br = b[q].h - b[q].l;
        z.x[F_TF] = tf; z.x[F_POLE] = ap / A; z.x[F_POLEER] = path > 0 ? ap / path : 0; z.x[F_POLEDIR] = dirBars / 6.0; z.x[F_FLAGRATIO] = (fh - fl) / ap; z.x[F_RETRACE] = (fd > 0 ? b[p1].c - fl : fh - b[p1].c) / ap;
        z.x[F_SLOPE] = -fd * (b[q - 1].c - b[q - 5].o) / ap; z.x[F_CHAN] = chan / 4.0; z.x[F_VOLR] = pv2 > 0 ? (fv / 5) / (pv2 / 6) : 1; z.x[F_BRKPOS] = br > 0 ? (fd > 0 ? (c - b[q].l) / br : (b[q].h - c) / br) : 0.5;
        z.x[F_BRKBODY] = br > 0 ? fd * (c - b[q].o) / br : 0; z.x[F_BRKDIST] = (fd * (c - (fd > 0 ? fh : fl))) / R; z.x[F_RISK] = R / A; z.x[F_ST] = ST[k]; z.x[F_MOVE] = fd * MV[k]; z.x[F_WITH] = D[k] == fd;
        z.x[F_VWSIDE] = fd * (c - VW[k]) > 0; z.x[F_VWDIST] = fd * (c - VW[k]) / A; z.x[F_TIME] = tm - hm_to_tday(9, 30); z.x[F_GAP] = gapDir == fd; z.x[F_PREV] = prevDir == fd; z.x[F_VIX] = std::isnan(d.vix) ? 18 : d.vix;
        z.x[F_D10] = tm >= hm_to_tday(10, 0) && d10 == fd; z.x[F_NTH] = nth++; z.d10ok = tm >= hm_to_tday(10, 0) && d10 == fd; z.ra = R / A; z.ke = k;
        double e = c, st = stop, tg = e + fd * 1.5 * R, px = NAN; size_t kx = n - 1; for (size_t kk = k + 1; kk < n; ++kk) { int j = I[kk]; if (fd > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = kk; break; } if (fd > 0 ? s.h[j] >= tg : s.l[j] <= tg) { px = tg; kx = kk; break; } }
        if (std::isnan(px)) px = s.c[I[n - 1]]; z.tx = s.t[I[kx]]; double net = fd * (px - e) - 2.0 * A / 320; z.pts = net * 320 / A; z.r = net / R; z.win = net > 0; sig.push_back(z); } } }
  // IS-built score (per side)
  std::vector<double> w[2], mu[2], sd[2]; for (int side = 0; side < 2; ++side) { w[side].assign(NF, 0); mu[side].assign(NF, 0); sd[side].assign(NF, 1);
    for (int f = 0; f < NF; ++f) { std::vector<std::pair<double, double>> v; double m = 0, q2 = 0, cnt = 0; for (auto& z : sig) if (z.side == side && z.era == 0) { v.push_back({z.x[f], z.r}); m += z.x[f]; q2 += z.x[f] * z.x[f]; cnt++; }
      w[side][f] = spearman(v); mu[side][f] = m / cnt; sd[side][f] = std::sqrt(std::max(1e-12, q2 / cnt - mu[side][f] * mu[side][f])); if (std::fabs(w[side][f]) < 0.02) w[side][f] = 0; } }
  std::vector<double> sc(sig.size()); std::vector<double> isS; for (size_t i = 0; i < sig.size(); ++i) { double v = 0; for (int f = 0; f < NF; ++f) v += w[sig[i].side][f] * (sig[i].x[f] - mu[sig[i].side][f]) / sd[sig[i].side][f]; sc[i] = v; if (sig[i].era == 0) isS.push_back(v); }
  std::sort(isS.begin(), isS.end());
  std::vector<size_t> ord(sig.size()); for (size_t i = 0; i < ord.size(); ++i) ord[i] = i; std::sort(ord.begin(), ord.end(), [&](size_t a, size_t b) { return sig[a].te < sig[b].te; });
  std::mt19937 rng(97); FILE* exp40 = fopen("research/programs/inputs_20261019/flagdaily40_k.csv", "w");
  printf("Flag streams that deliver 1-5 trades a day — NQ NY, all charts 1-15m, IS-built quality score, one at a time, <= 5 a day, 1.5R to close, 2 pts cost.\n"
         "Days: 2010-18 %.0f, 2019-24 %.0f, 2025-26 %.0f. 'top X%%' = score above the (100-X)th percentile of 2010-18 scores.\n\n", nd[0], nd[1], nd[2]);
  printf("  %-40s %-17s %-14s %5s | %-30s | %6s | fair control\n", "stream", "trades/day IS/OOS", "days >= 1 OOS", "win%", "pts/trade 2010-18 / 2019-24 / 2025-26", "R");
  for (int hard = 0; hard < 2; ++hard) for (double top : {1.0, 0.8, 0.6, 0.5, 0.4, 0.3, 0.2}) {
    double cut = top >= 1.0 ? -1e18 : isS[(size_t)((1 - top) * isS.size())];
    double n[3] = {0, 0, 0}, p[3] = {0, 0, 0}, wn = 0, nt = 0, rs = 0; std::map<int, int> perDay; std::vector<double> RA; std::vector<int> E;
    int curDay = -1, cntDay = 0; long busy = 0;
    for (size_t oi : ord) { const Sig& z = sig[oi]; if (sc[oi] <= cut) continue; if (hard && !z.d10ok) continue;
      if (z.day != curDay) { curDay = z.day; cntDay = 0; busy = 0; } if (cntDay >= 5 || z.te <= busy) continue;
      if (!hard && std::fabs(top - 0.4) < 1e-9) fprintf(exp40, "%d,%ld,%ld,%d,%.4f,%.4f,0,0,%d,%.5f\n", z.date, z.te, z.tx, z.side ? -1 : 1, z.pts * days[z.day].A / 320 + 2.0 * days[z.day].A / 320, z.pts * days[z.day].A / 320 + 2.0 * days[z.day].A / 320 - 1, (int)z.x[F_TF], z.ra);
      cntDay++; busy = z.tx; n[z.era]++; p[z.era] += z.pts; wn += z.win; nt++; rs += z.r; perDay[z.day]++; RA.push_back(z.ra); E.push_back(z.era); }
    long daysOOS = 0, withOOS = 0; for (size_t di = 0; di < days.size(); ++di) if (days[di].era > 0) { daysOOS++; if (perDay.count(di)) withOOS++; }
    // fair control
    double cp = 0, cn = 0; for (int rep = 0; rep < 5; ++rep) for (double ra : RA) { const DI& x = days[std::uniform_int_distribution<size_t>(0, days.size() - 1)(rng)]; if (!x.d10) continue;
        std::vector<size_t> cd; for (size_t k = 0; k < x.I.size(); ++k) { int tm = tday_min(s.t[x.I[k]]); if (tm >= hm_to_tday(10, 0) && tm < hm_to_tday(15, 0)) cd.push_back(k); } if (cd.empty()) continue;
        size_t k0 = cd[std::uniform_int_distribution<size_t>(0, cd.size() - 1)(rng)]; int dir = x.d10; double R = ra * x.A, e = s.c[x.I[k0]], st = e - dir * R, tg = e + dir * 1.5 * R, px = NAN;
        for (size_t k = k0 + 1; k < x.I.size(); ++k) { int i = x.I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; break; } }
        if (std::isnan(px)) px = s.c[x.I.back()]; cp += (dir * (px - e) - 2.0 * x.A / 320) * 320 / x.A; cn++; }
    char lab[80]; snprintf(lab, sizeof lab, "%s, %s", hard ? "10:00 direction only" : "any direction", top >= 1.0 ? "all flags" : (std::string("top ") + std::to_string((int)std::lround(top * 100)) + "% by score").c_str());
    printf("  %-40s %5.2f / %5.2f      %5.1f%%        %4.1f%% | %+7.2f / %+7.2f / %+7.2f       | %+6.3f | %+6.2f\n", lab, n[0] / nd[0], (n[1] + n[2]) / (nd[1] + nd[2]), 100.0 * withOOS / daysOOS, 100 * wn / nt,
           p[0] / std::max(1.0, n[0]), p[1] / std::max(1.0, n[1]), p[2] / std::max(1.0, n[2]), rs / nt, cn ? cp / cn : 0); }
  // ---- deeper look at the "any direction, top 40 %" stream: by year, by market condition, and exits
  { std::vector<double> hist; for (auto& x : days) { hist.push_back(x.A); size_t a0 = hist.size() > 252 ? hist.size() - 252 : 0; double below = 0; for (size_t i = a0; i < hist.size(); ++i) if (hist[i] < x.A) below++; x.atrpct = below / (hist.size() - a0); }
    double cut = isS[(size_t)(0.6 * isS.size())]; std::vector<size_t> pick; int curDay = -1, cntDay = 0; long busy = 0;
    for (size_t oi : ord) { const Sig& z = sig[oi]; if (sc[oi] <= cut) continue; if (z.day != curDay) { curDay = z.day; cntDay = 0; busy = 0; } if (cntDay >= 5 || z.te <= busy) continue; cntDay++; busy = z.tx; pick.push_back(oi); }
    printf("\n######## The top-40 %% stream in detail (%zu trades)\n  by year: pts per trade (trades a day)\n   ", pick.size());
    std::map<int, std::pair<double, double>> yr; std::map<int, double> ydays; for (auto& x : days) ydays[x.date / 10000]++; for (size_t oi : pick) { auto& p = yr[sig[oi].date / 10000]; p.first += sig[oi].pts; p.second++; }
    for (auto& [y, p] : yr) printf(" %d %+5.1f (%.1f)", y, p.first / p.second, p.second / ydays[y]); printf("\n");
    struct Cond { const char* name; std::function<int(const DI&)> g; std::vector<const char*> lab; };
    std::vector<Cond> conds = { {"VIX (prior close)", [](const DI& x) { return x.vixv < 15 ? 0 : x.vixv < 20 ? 1 : x.vixv < 25 ? 2 : 3; }, {"< 15", "15-20", "20-25", ">= 25"}},
      {"ATR vs its last year", [](const DI& x) { return x.atrpct < 0.33 ? 0 : x.atrpct < 0.67 ? 1 : 2; }, {"low third", "middle", "high third"}},
      {"gap at the open (ATR)", [](const DI& x) { return x.gap < 0.1 ? 0 : x.gap < 0.3 ? 1 : 2; }, {"< 0.1", "0.1-0.3", ">= 0.3"}},
      {"09:30-10:00 range (ATR)", [](const DI& x) { return x.or30 < 0.2 ? 0 : x.or30 < 0.35 ? 1 : 2; }, {"< 0.20", "0.20-0.35", ">= 0.35"}},
      {"weekday", [](const DI& x) { return std::clamp(x.dow - 1, 0, 4); }, {"Mon", "Tue", "Wed", "Thu", "Fri"}} };
    for (auto& c : conds) { printf("\n  %s: pts per trade 2010-24 (n) | 2025-26 (n)\n", c.name); double a[6] = {}, na[6] = {}, b2[6] = {}, nb[6] = {};
      for (size_t oi : pick) { const Sig& z = sig[oi]; int g = c.g(days[z.day]); if (z.era < 2) { a[g] += z.pts; na[g]++; } else { b2[g] += z.pts; nb[g]++; } }
      for (size_t g = 0; g < c.lab.size(); ++g) printf("    %-10s %+6.2f (%4.0f) | %+6.2f (%3.0f)\n", c.lab[g], a[g] / std::max(1.0, na[g]), na[g], b2[g] / std::max(1.0, nb[g]), nb[g]); }
    printf("\n  exits on the same entries (one at a time re-run per exit): pts per trade 2010-18 | 2019-24 | 2025-26, win %%, trades a day\n");
    struct EX { const char* n; double tgt; int tmax; }; std::vector<EX> ex = {{"1R target, to close", 1, 9999}, {"1.5R target, to close (base)", 1.5, 9999}, {"2R target, to close", 2, 9999}, {"1.5R, 60-min time stop", 1.5, 60}, {"1.5R, 30-min time stop", 1.5, 30}, {"1R, 30-min time stop", 1, 30}};
    for (auto& e : ex) { double p[3] = {}, nn[3] = {}, w = 0, nt = 0; int cd = -1, cn = 0; long bz = 0;
      for (size_t oi : ord) { const Sig& z = sig[oi]; if (sc[oi] <= cut) continue; if (z.day != cd) { cd = z.day; cn = 0; bz = 0; } if (cn >= 5 || z.te <= bz) continue;
        const DI& x = days[z.day]; int dir = z.side ? -1 : 1; double R = z.ra * x.A, en = s.c[x.I[z.ke]], st = en - dir * R, tg = en + dir * e.tgt * R, px = NAN; size_t kx = x.I.size() - 1;
        for (size_t k = z.ke + 1; k < x.I.size(); ++k) { int i = x.I[k]; if (dir > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; kx = k; break; } if (dir > 0 ? s.h[i] >= tg : s.l[i] <= tg) { px = tg; kx = k; break; } if ((long)(k - z.ke) >= e.tmax) { px = s.c[i]; kx = k; break; } }
        if (std::isnan(px)) px = s.c[x.I.back()]; cn++; bz = s.t[x.I[kx]]; double pts = (dir * (px - en) - 2.0 * x.A / 320) * 320 / x.A; p[z.era] += pts; nn[z.era]++; w += pts > 0; nt++; }
      printf("    %-30s %+6.2f | %+6.2f | %+6.2f | %4.1f%% | %.2f\n", e.n, p[0] / nn[0], p[1] / nn[1], p[2] / nn[2], 100 * w / nt, nt / (nd[0] + nd[1] + nd[2])); } }
  fclose(exp40);
  return 0;
}
