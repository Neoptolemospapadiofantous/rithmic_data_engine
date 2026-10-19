// 2026-10-19 (founder: "create a sequence for the bull flag in order to validate and correlate and get the best entry"):
// every NY bull-flag CANDIDATE on 3/4/5m, 2010-07 → 2026-10, followed bar by bar, with (1) a step-by-step validation funnel,
// (2) the correlation of every pole / flag / context feature with the result (2010-18 and 2019-26 separately) and (3) seven
// entries compared on the SAME flags.
// Candidate: pole = one impulse leg from the lowest low of the 3-15 bars before the pole top to the pole top (no lower low / higher
// high inside), ≥ 2× the session's average chart bar (the funnel tightens it); flag = the bars after the top with no new high and a
// pullback of 15-50 % of the pole. Registered when the 2nd flag bar closes; then followed until the price trades above the pole top,
// a bar closes below the 50 % retracement, 12 flag bars pass, or 15:25. One candidate at a time per chart.
// Entries (stop and the common target = pole top + pole length; flat 15:55; 2 pts cost scaled to the day; R = net / risk):
//  E0 at once      close of the 2nd flag bar               stop 78.6 % retracement
//  E1 flag break   first close above the flag's high       stop flag low
//  E2 top break    stop order 1 tick above the pole top    stop flag low
//  E3 wick close   close of the first flag candle with a lower wick ≥ 50 % of its range, close in its top half   stop below the wick
//  E4 wick break   1 tick above that wick candle within 2 bars                                                     stop below the wick
//  E5 50 % limit   limit at the 50 % retracement           stop 78.6 % retracement
//  E6 EMA20 limit  limit at the chart's 20-bar EMA inside the flag   stop 78.6 % retracement
// A fill on the same minute as the stop counts as a loss. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c, v; int k; };
static double spear(std::vector<double> a, std::vector<double> b) { size_t n = a.size(); if (n < 30) return NAN; auto rk = [](std::vector<double>& x) { std::vector<size_t> ix(x.size()); for (size_t i = 0; i < ix.size(); ++i) ix[i] = i; std::sort(ix.begin(), ix.end(), [&](size_t p, size_t q) { return x[p] < x[q]; }); std::vector<double> r(x.size()); for (size_t i = 0; i < ix.size();) { size_t j = i; while (j + 1 < ix.size() && x[ix[j + 1]] == x[ix[i]]) ++j; for (size_t m = i; m <= j; ++m) r[ix[m]] = (i + j) / 2.0; i = j + 1; } return r; };
  auto ra = rk(a), rb = rk(b); double ma = 0, mb = 0; for (size_t i = 0; i < n; ++i) { ma += ra[i]; mb += rb[i]; } ma /= n; mb /= n; double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < n; ++i) { sab += (ra[i] - ma) * (rb[i] - mb); saa += (ra[i] - ma) * (ra[i] - ma); sbb += (rb[i] - mb) * (rb[i] - mb); } return sab / std::sqrt(saa * sbb); }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int NE = 7; const char* EN[NE] = {"E0 at once", "E1 flag-high break", "E2 pole-top break", "E3 wick close", "E4 wick-high break", "E5 50% limit", "E6 EMA20 limit"};
  const int NF = 16; const char* FN[NF] = {"pole size / avg bar", "pole bars", "pole share of up bars", "pole volume / session avg bar vol", "pullback depth (share of pole)", "flag bars at setup", "flag range / pole",
    "flag volume / pole volume (per bar)", "pole size / ATR", "distance above VWAP / ATR", "straightness of session path", "minutes after 09:30", "day's range so far / ATR", "pole top above opening-range high (0/1)", "pole top above prior-day high (0/1)", "gap from prior close / ATR"};
  struct C { int tf, date, era; double sc; double f[NF]; bool ctxOpen, ctxVwap, ctxStraight; double R[NE], rk[NE]; bool fill[NE]; double P(int e) const { return R[e] * rk[e] / sc; } };
  std::vector<C> cs; double ndays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue; ndays++;
    const double A = d.atr, sc = A / 320.0, cost = 2 * sc, so = s.o[I[0]]; const size_t n = I.size();
    std::vector<double> vw(n), hiS(n), loS(n); { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { int i = I[k]; pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i]; vw[k] = vv > 0 ? pv / vv : s.c[i]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); hiS[k] = hi; loS[k] = lo; } }
    double orh = -1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < 960; ++k) orh = std::max(orh, (double)s.h[I[k]]);
    for (int tf = 3; tf <= 5; ++tf) { std::vector<FB> b; std::vector<double> ema; size_t busyUntilBar = 0;
      // simulate an exit from minute index k0 (entry filled during minute k0; same-minute stop = loss)
      double lastRisk = 0; auto exitR = [&](size_t k0, double e, double st, double tg, bool checkFillMinute) -> double { double px = NAN; lastRisk = e - st;
        if (checkFillMinute && s.l[I[k0]] <= st) px = st;
        if (std::isnan(px)) for (size_t k = k0 + 1; k < n; ++k) { int j = I[k]; if (s.l[j] <= st) { px = st; break; } if (s.h[j] >= tg) { px = tg; break; } if (tday_min(s.t[j]) >= 1315) { px = s.o[j]; break; } }
        if (std::isnan(px)) px = s.c[I[n - 1]]; return (px - e - cost) / (e - st); };
      for (size_t k = 0; k < n; ++k) { int i = I[k], tm = tday_min(s.t[i]);
        if ((tm - 930) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (double)s.v[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.v += s.v[i]; z.k = k; }
        if ((tm + 1 - 930) % tf) continue; const int q = b.size() - 1; ema.push_back(ema.empty() ? b[q].c : ema.back() + (b[q].c - ema.back()) * 2.0 / 21.0);
        if (q < 6 || (size_t)q <= busyUntilBar || tm < 945 || tm >= 1285) continue;
        // a flag that qualifies exactly at its 2nd bar: pole top h = q-2
        const int h = q - 2; double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
        int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 3; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
        bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L || b[r].h > H) ok = false; if (!ok) continue;
        // the pole top must be the highest high of the pole (already) and the pole low its lowest; require the pole top bar to be a fresh high vs the bar before the pole
        double ar = 0, av = 0; for (int r = 0; r <= q; ++r) { ar += b[r].h - b[r].l; av += b[r].v; } ar /= (q + 1); av /= (q + 1);
        const double M = H - L; if (M < 2 * ar) continue; int up = 0; double pv = 0; for (int r = l; r <= h; ++r) { up += b[r].c > b[r].o; pv += b[r].v; }
        double FH = std::max(b[h + 1].h, b[q].h), FL = std::min(b[h + 1].l, b[q].l), pull = (H - FL) / M; if (pull < 0.15 || pull > 0.5) continue;
        C c{}; c.tf = tf; c.date = d.date; c.era = d.date < 20190101 ? 0 : 1; c.sc = sc;
        const size_t kq = b[q].k; const double cl = b[q].c;
        c.f[0] = M / ar; c.f[1] = h - l + 1; c.f[2] = (double)up / (h - l + 1); c.f[3] = pv / (h - l + 1) / av; c.f[4] = pull; c.f[5] = 2; c.f[6] = (FH - FL) / M;
        c.f[7] = ((b[h + 1].v + b[q].v) / 2) / (pv / (h - l + 1)); c.f[8] = M / A; c.f[9] = (cl - vw[kq]) / A; c.f[10] = hiS[kq] > loS[kq] ? (cl - so) / (hiS[kq] - loS[kq]) : 0; c.f[11] = tm + 1 - 930;
        c.f[12] = (hiS[kq] - loS[kq]) / A; c.f[13] = H > orh && tm >= 960; c.f[14] = H > d.ph; c.f[15] = std::isnan(d.pc) ? 0 : (so - d.pc) / A;
        c.ctxOpen = cl > so; c.ctxVwap = cl > vw[kq]; c.ctxStraight = c.f[10] >= 0.55;
        const double tg = H + M, s786 = H - 0.786 * M - 0.25;
        for (int e = 0; e < NE; ++e) { c.fill[e] = false; c.R[e] = NAN; }
        c.fill[0] = true; c.R[0] = exitR(kq, cl, s786, tg, false); c.rk[0] = lastRisk;
        // follow the flag minute by minute
        double fh = FH, fl = FL; bool done = false; int wickBar = -1; double wH = NAN, wL = NAN; size_t lastK = kq; int qq = q;
        std::vector<double> curBar; double bo = 0, bh = -1e18, bl = 1e18; int nbar = 0; double emaPrev = ema.back();
        for (size_t kk = kq + 1; kk < n && !done; ++kk) { int j = I[kk], t2 = tday_min(s.t[j]); lastK = kk;
          if (t2 >= 1285) break;
          const double mh = s.h[j], ml = s.l[j];
          if (bh < -1e17) bo = s.o[j]; bh = std::max(bh, mh); bl = std::min(bl, ml);
          // limits first (they rest in the market): E5 50 %, E6 EMA20 (as of the last completed chart bar)
          if (!c.fill[5] && ml <= H - 0.5 * M) { c.fill[5] = true; c.R[5] = exitR(kk, H - 0.5 * M, s786, tg, true); c.rk[5] = lastRisk; }
          if (!c.fill[6] && emaPrev < H && emaPrev > s786 && ml <= emaPrev && mh >= emaPrev) { c.fill[6] = true; c.R[6] = exitR(kk, emaPrev, s786, tg, true); c.rk[6] = lastRisk; }
          // stop orders: E2 above the pole top; E4 above the wick candle within 2 bars
          if (mh > H) { if (!c.fill[2]) { c.fill[2] = true; c.R[2] = exitR(kk, H + 0.25, fl - 0.25, tg, true); c.rk[2] = lastRisk; } }
          if (wickBar >= 0 && !c.fill[4] && nbar - wickBar <= 2 && mh > wH) { c.fill[4] = true; c.R[4] = exitR(kk, wH + 0.25, wL - 0.25, tg, true); c.rk[4] = lastRisk; }
          if ((t2 + 1 - 930) % tf == 0) { // a chart bar closed
            const double bc = s.c[j]; nbar++;
            if (!c.fill[1] && bc > fh) { c.fill[1] = true; c.R[1] = exitR(kk, bc, fl - 0.25, tg, false); c.rk[1] = lastRisk; }
            if (!c.fill[3] && wickBar < 0 && bh <= H) { double rg = bh - bl, wk = std::min(bo, bc) - bl; if (rg > 0 && wk >= 0.5 * rg && (bc - bl) / rg >= 0.5) { wickBar = nbar; wH = bh; wL = bl; c.fill[3] = true; c.R[3] = exitR(kk, bc, bl - 0.25, tg, false); c.rk[3] = lastRisk; } }
            emaPrev = emaPrev + (bc - emaPrev) * 2.0 / 21.0; qq++;
            if (bh > H || bc < H - 0.5 * M || nbar >= 12) done = true;   // flag resolved: broke out, failed, or ran out of time
            fh = std::max(fh, bh); fl = std::min(fl, bl); bh = -1e18; bl = 1e18; } }
        busyUntilBar = q + nbar; (void)lastK; (void)qq; cs.push_back(c); } } }
  printf("Bull-flag sequence — NY 3/4/5m, NQ 2010-07 → 2026-10, %zu flag candidates on %.0f days. R after 2 pts cost; common target = pole top + pole length.\n", cs.size(), ndays);
  // ---------- 1. the funnel
  struct Step { const char* name; std::function<bool(const C&)> f; };
  std::vector<Step> steps = {
    {"0 every candidate (pole >= 2x avg bar, pullback 15-50%)", [](const C&) { return true; }},
    {"1 + price above the 09:30 open", [](const C& c) { return c.ctxOpen; }},
    {"2 + above VWAP", [](const C& c) { return c.ctxVwap; }},
    {"3 + straight session path (>= 0.55)", [](const C& c) { return c.ctxStraight; }},
    {"4 + pole >= 4x the average bar", [](const C& c) { return c.f[0] >= 4; }},
    {"5 + pole >= 60% up bars", [](const C& c) { return c.f[2] >= 0.6; }},
    {"6 + flag volume below pole volume", [](const C& c) { return c.f[7] < 1; }},
    {"7 + entry before 14:00", [](const C& c) { return c.f[11] < 270; }},
    {"8 + pole >= 0.40 x ATR", [](const C& c) { return c.f[8] >= 0.40; }},
    {"9 + pole volume >= 0.8 x session avg bar", [](const C& c) { return c.f[3] >= 0.8; }},
    {"10 + entry before 12:00", [](const C& c) { return c.f[11] < 150; }} };
  printf("\n== 1. Validation sequence (cumulative). Per entry: avg R 2010-18 / 2019-26 (fill %%). n/day = candidates a day on 3+4+5m.\n");
  printf("  %-56s %6s", "step", "n/day"); for (int e = 0; e < NE; ++e) printf(" | %-19s", EN[e]); printf("\n");
  std::vector<const C*> cur; for (auto& c : cs) cur.push_back(&c);
  for (auto& st : steps) { std::vector<const C*> nx; for (auto* c : cur) if (st.f(*c)) nx.push_back(c); cur = nx;
    printf("  %-56s %6.2f", st.name, cur.size() / ndays);
    for (int e = 0; e < NE; ++e) { double s0 = 0, n0 = 0, s1 = 0, n1 = 0, nf = 0; for (auto* c : cur) if (c->fill[e] && !std::isnan(c->R[e])) { nf++; if (c->era == 0) { s0 += c->R[e]; n0++; } else { s1 += c->R[e]; n1++; } }
      printf(" | %+.3f/%+.3f (%2.0f%%)", n0 ? s0 / n0 : 0, n1 ? s1 / n1 : 0, cur.size() ? 100 * nf / cur.size() : 0); } printf("\n");
    printf("  %-56s %6s", "    net pts at today's range 2010-18 / 2019-26, trades a day", ""); for (int e = 0; e < NE; ++e) { double s0 = 0, n0 = 0, s1 = 0, n1 = 0; for (auto* c : cur) if (c->fill[e] && !std::isnan(c->R[e])) { if (c->era == 0) { s0 += c->P(e); n0++; } else { s1 += c->P(e); n1++; } } printf(" | %+5.1f/%+5.1f %.2f/d   ", n0 ? s0 / n0 : 0, n1 ? s1 / n1 : 0, (n0 + n1) / ndays); } printf("\n"); }
  printf("\n  Final sequence (all 10 steps), per entry: trades a year | win %% | avg R | profit factor (R) | years with R > 0 | 2025-26 avg R (n)\n");
  for (int e = 0; e < NE; ++e) { std::map<int, double> yr; double n = 0, w = 0, sr = 0, gp = 0, gl = 0, n2 = 0, s2 = 0; for (auto* c : cur) if (c->fill[e] && !std::isnan(c->R[e])) { double r = c->R[e]; n++; w += r > 0; sr += r; (r > 0 ? gp : gl) += std::fabs(r); yr[c->date / 10000] += r; if (c->date >= 20250101) { n2++; s2 += r; } }
    int py = 0; for (auto& [y, v] : yr) py += v > 0; printf("    %-20s %5.1f/yr | %4.1f%% | %+.3fR | PF %.2f | %d/%zu yrs | %+.3f (%.0f)\n", EN[e], n / (ndays / 252), n ? 100 * w / n : 0, n ? sr / n : 0, gl ? gp / gl : 0, py, yr.size(), n2 ? s2 / n2 : 0, n2); }
  // ---------- 2. feature correlation, inside step 3 (context on) and with each reference entry
  std::vector<const C*> ctx; for (auto& c : cs) if (c.ctxOpen && c.ctxVwap && c.ctxStraight) ctx.push_back(&c);
  printf("\n== 2. Which features matter — candidates with the context on (steps 1-3), %zu flags. Spearman with R per period; then mean R by tercile of the feature (low / mid / high), 2010-26.\n", ctx.size());
  for (int e : {3, 1, 2}) { printf("  -- entry %s\n", EN[e]);
    for (int f = 0; f < NF; ++f) { std::vector<double> x0, y0, x1, y1, xa; for (auto* c : ctx) if (c->fill[e] && !std::isnan(c->R[e])) { (c->era ? x1 : x0).push_back(c->f[f]); (c->era ? y1 : y0).push_back(c->R[e]); xa.push_back(c->f[f]); }
      std::vector<double> srt = xa; std::sort(srt.begin(), srt.end()); if (srt.size() < 60) continue; double t1 = srt[srt.size() / 3], t2 = srt[2 * srt.size() / 3]; double sm[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
      for (auto* c : ctx) if (c->fill[e] && !std::isnan(c->R[e])) { int g = c->f[f] < t1 ? 0 : c->f[f] < t2 ? 1 : 2; if (t1 == t2) g = c->f[f] < t1 ? 0 : 2; sm[g] += c->R[e]; nm[g]++; }
      double r0 = spear(x0, y0), r1 = spear(x1, y1); bool same = r0 * r1 > 0 && std::fabs(r0) >= 0.03 && std::fabs(r1) >= 0.03;
      printf("    %-40s rho %+.3f / %+.3f %s | terciles %+.3f / %+.3f / %+.3f  (cuts %.2f, %.2f)\n", FN[f], r0, r1, same ? "SAME SIGN" : "         ", nm[0] ? sm[0] / nm[0] : 0, nm[1] ? sm[1] / nm[1] : 0, nm[2] ? sm[2] / nm[2] : 0, t1, t2); } }
  // ---------- 3. best entry on the same flags
  printf("\n== 3. Entries compared on the same flags (context on, steps 1-3). Per chart: fill %% | win %% | avg R | pts at today's range | 2019-26 avg R.\n");
  for (int tf = 3; tf <= 5; ++tf) { printf("  -- %dm\n", tf); for (int e = 0; e < NE; ++e) { double nf = 0, w = 0, sr = 0, sp = 0, n1 = 0, s1 = 0, nt = 0;
      for (auto* c : ctx) { if (c->tf != tf) continue; nt++; if (!c->fill[e] || std::isnan(c->R[e])) continue; nf++; w += c->R[e] > 0; sr += c->R[e]; if (c->era) { n1++; s1 += c->R[e]; } }
      printf("    %-20s fill %4.1f%% | win %4.1f%% | %+.3fR | 2019-26 %+.3fR (%.0f)\n", EN[e], 100 * nf / nt, nf ? 100 * w / nf : 0, nf ? sr / nf : 0, n1 ? s1 / n1 : 0, n1); (void)sp; } }
  printf("  Paired: on flags where BOTH fill, mean difference in NET POINTS at today's range vs E3 wick close (positive = better than E3), 2010-18 / 2019-26:\n");
  for (int e = 0; e < NE; ++e) { if (e == 3) continue; double d0 = 0, n0 = 0, d1 = 0, n1 = 0; for (auto* c : ctx) if (c->fill[e] && c->fill[3] && !std::isnan(c->R[e]) && !std::isnan(c->R[3])) { double df = c->P(e) - c->P(3); if (c->era) { d1 += df; n1++; } else { d0 += df; n0++; } }
    printf("    %-20s %+.3f (%.0f) / %+.3f (%.0f)\n", EN[e], n0 ? d0 / n0 : 0, n0, n1 ? d1 / n1 : 0, n1); }
  return 0;
}
