// 2026-10-19 (founder: "optimize bull and bear flags as much as possible", "check all sessions"). A large flag grid searched on
// IN-SAMPLE 2010-07 → 2020-12 only, picked by fixed rules, then read on 2021-01 → 2026-10 (out of sample; 2025+ was already looked at
// once for flag5_trend, so OOS is not fully virgin). Sessions Asia (18:00–03:00 ET), London (03:00–09:30), NY (09:30–15:55), each with its
// own open / VWAP / straightness. Grid: chart 3/5/10/15 min x pole bars 3/4/6/8 x pole size 0.10/0.15/0.20/0.30 ATR x flag bars 2/3/5 x
// tightness 0.4/0.6/0.8 x max retrace 0.5/1.0 of the pole x flag volume below pole volume off/on  (2,304 detectors) x context
// (none / with the session's side of open+VWAP / + straightness 0.40/0.55/0.70) x window (first 2 h / whole session) x side (bull/bear/
// both) x target (1/1.5/2/3 R / measured move) = 1.04 M configs. Entry on the close beyond the flag, stop beyond the flag (+0.25),
// 120-min time exit, flat at session end, one position at a time, ≤3 trades a session, cost 2 NQ pts at today's cost-to-range.
// Selection rule (fixed before reading OOS): IS n ≥ 120, net R > 0 in both IS halves (2010-15, 2016-20), ≥ 75 % of grid neighbours
// positive in IS, ranked by IS t. argv: symbol. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <algorithm>
using namespace lab;

static const int TFS[4] = {3, 5, 10, 15}, NS_[4] = {3, 4, 6, 8}, FS[3] = {2, 3, 5};
static const double MS[4] = {0.10, 0.15, 0.20, 0.30}, TS[3] = {0.4, 0.6, 0.8}, RS[2] = {0.5, 1.0}, TG[4] = {1, 1.5, 2, 3};
static const int DIMS[7] = {4, 4, 4, 3, 3, 2, 2}; static const int NDET = 4 * 4 * 4 * 3 * 3 * 2 * 2, NPOST = 3 * 5 * 2 * 3 * 5;
static const double COST = 2.0 / 320.0;
static const int S0[3] = {0, 540, 930}, SE[3] = {535, 925, 1315};
static const char* SN[3] = {"Asia", "London", "NY"};
static const char* CTXN[5] = {"any", "with", "with+st.40", "with+st.55", "with+st.70"};
static const char* SIDEN[3] = {"bull", "bear", "both"};
static const char* TGN[5] = {"1R", "1.5R", "2R", "3R", "measured"};
struct Bar { float o, h, l, c, v; int il; };
struct Seg { int date, day, sess, per; std::vector<Bar> b[4]; };
struct Sig { int ei; short tm; signed char sess, fd, per; float st; bool with; float Ra; float out[5]; int xi[5]; int date; };
struct St { double n = 0, s = 0, q = 0, pts = 0, gp = 0, gl = 0; void add(double r, double p) { n++; s += r; q += r * r; pts += p; if (p > 0) gp += p; else gl -= p; }
  double m() const { return n ? s / n : NAN; } double t() const { if (n < 3) return NAN; double mu = m(), sd = std::sqrt(std::max(1e-12, q / n - mu * mu)); return mu / (sd / std::sqrt(n)); } };
struct Cfg { St p[3]; };   // IS1 2010-15, IS2 2016-20, OOS 2021+

static Series s; static std::vector<Seg> segs; static std::vector<float> STR; static std::vector<signed char> DD, VS;

int main(int argc, char** argv) {
  const char* sym = argc > 1 ? argv[1] : "NQ";
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  STR.assign(s.size(), 0); DD.assign(s.size(), 0); VS.assign(s.size(), 0);
  double perDays[3] = {0};
  for (int di = 0; di < (int)s.days.size(); ++di) { auto& d = s.days[di];
    if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue;
    int per = d.date < 20160101 ? 0 : d.date < 20210101 ? 1 : 2; perDays[per]++;
    for (int se = 0; se < 3; ++se) { Seg g; g.date = d.date; g.day = di; g.sess = se; g.per = per;
      double so = NAN, hi = 0, lo = 0, pv = 0, vv = 0; int cur[4] = {-1, -1, -1, -1};
      for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < S0[se] || tm >= SE[se]) continue;
        if (std::isnan(so)) { so = s.o[i]; hi = s.h[i]; lo = s.l[i]; }
        hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; STR[i] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; DD[i] = c > so ? 1 : c < so ? -1 : 0; VS[i] = c > vw ? 1 : c < vw ? -1 : 0;
        for (int k = 0; k < 4; ++k) { int bk = (tm - S0[se]) / TFS[k];
          if (bk != cur[k]) { cur[k] = bk; g.b[k].push_back({(float)s.o[i], (float)s.h[i], (float)s.l[i], (float)s.c[i], (float)s.v[i], i}); }
          else { Bar& x = g.b[k].back(); x.h = std::max(x.h, (float)s.h[i]); x.l = std::min(x.l, (float)s.l[i]); x.c = s.c[i]; x.v += s.v[i]; x.il = i; } } }
      if (g.b[0].size() > 10) segs.push_back(std::move(g)); } }
  const double yrsIS = (perDays[0] + perDays[1]) / 252.0, yrsO = perDays[2] / 252.0;
  std::vector<Cfg> R((size_t)NDET * NPOST);
  #pragma omp parallel for schedule(dynamic)
  for (int det = 0; det < NDET; ++det) {
    int x = det; int vol = x % 2; x /= 2; int ri = x % 2; x /= 2; int ti = x % 3; x /= 3; int fi = x % 3; x /= 3; int mi = x % 4; x /= 4; int ni = x % 4; x /= 4; int tfi = x;
    const int N = NS_[ni], F = FS[fi]; const double M = MS[mi], TT = TS[ti], RT = RS[ri];
    std::vector<Sig> sig;
    for (auto& g : segs) { const auto& b = g.b[tfi]; const double A = s.days[g.day].atr; const int db1 = s.days[g.day].b1;
      for (int k = N + F; k < (int)b.size(); ++k) { int p0 = k - F - N, p1 = k - F - 1;
        double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < M * A) continue;
        double fh = -1e18, fl = 1e18, fv = 0, pvv = 0; for (int q = k - F; q < k; ++q) { fh = std::max(fh, (double)b[q].h); fl = std::min(fl, (double)b[q].l); fv += b[q].v; }
        if (fh - fl > TT * ap) continue;
        if ((fd > 0 ? b[p1].c - fl : fh - b[p1].c) > RT * ap) continue;
        if (vol) { for (int q = p0; q <= p1; ++q) pvv += b[q].v; if (fv / F >= pvv / N) continue; }
        double c = b[k].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue;
        double stop = fd > 0 ? fl - 0.25 : fh + 0.25, Rr = fd * (c - stop); if (Rr <= 0.25 || Rr > 0.6 * A) continue;
        int ei = b[k].il; Sig z; z.ei = ei; z.tm = tday_min(s.t[ei]); z.sess = g.sess; z.fd = fd; z.per = g.per; z.st = STR[ei]; z.with = DD[ei] == fd && VS[ei] == fd; z.Ra = Rr / A; z.date = g.date;
        double tgt[5] = {TG[0], TG[1], TG[2], TG[3], ap / Rr}; bool done[5] = {false}; int left = 5; int j = ei + 1, last = ei;
        for (; j < db1 && left; ++j) { int tm = tday_min(s.t[j]); if (tm >= SE[g.sess] || s.t[j] > s.t[ei] + 7200) break; last = j;
          bool hs = fd > 0 ? s.l[j] <= stop : s.h[j] >= stop;
          for (int q = 0; q < 5; ++q) { if (done[q]) continue; double tp = c + fd * tgt[q] * Rr;
            if (hs) { z.out[q] = -1; z.xi[q] = j; done[q] = true; left--; } else if (fd > 0 ? s.h[j] >= tp : s.l[j] <= tp) { z.out[q] = tgt[q]; z.xi[q] = j; done[q] = true; left--; } } }
        for (int q = 0; q < 5; ++q) if (!done[q]) { z.out[q] = fd * (s.c[last] - c) / Rr; z.xi[q] = last; }
        sig.push_back(z); } }
    Cfg* out = &R[(size_t)det * NPOST];
    for (int se = 0; se < 3; ++se) for (int cx = 0; cx < 5; ++cx) for (int w = 0; w < 2; ++w) for (int sd = 0; sd < 3; ++sd) {
      int lastX[5] = {-1, -1, -1, -1, -1}, dayN[5] = {0}, curDate[5] = {0};
      const double stT[5] = {0, 0, 0.40, 0.55, 0.70};
      for (auto& z : sig) { if (z.sess != se) continue;
        if (z.tm < S0[se] + 15 || z.tm >= (w ? SE[se] - 60 : S0[se] + 120)) continue;
        if (sd == 0 && z.fd < 0) continue; if (sd == 1 && z.fd > 0) continue;
        if (cx >= 1 && !z.with) continue; if (cx >= 2 && z.st < stT[cx]) continue;
        for (int q = 0; q < 5; ++q) { if (z.date != curDate[q]) { curDate[q] = z.date; dayN[q] = 0; }
          if (z.ei <= lastX[q] || dayN[q] >= 3) continue; lastX[q] = z.xi[q]; dayN[q]++;
          double net = z.out[q] - COST / z.Ra; out[(((se * 5 + cx) * 2 + w) * 3 + sd) * 5 + q].p[(int)z.per].add(net, (z.out[q] * z.Ra - COST) * 320); } } }
  }
  auto detOf = [](size_t c) { return (int)(c / NPOST); };
  auto label = [&](size_t c) { static char buf[200]; int det = detOf(c), post = c % NPOST; int x = det; int vol = x % 2; x /= 2; int ri = x % 2; x /= 2; int ti = x % 3; x /= 3; int fi = x % 3; x /= 3; int mi = x % 4; x /= 4; int ni = x % 4; x /= 4; int tfi = x;
    int q = post % 5; post /= 5; int sd = post % 3; post /= 3; int w = post % 2; post /= 2; int cx = post % 5;
    snprintf(buf, sizeof buf, "%2dm pole %d>=%.2f flag %d<=%.1f rt%.1f%s %-10s %-5s %-4s %-8s", TFS[tfi], NS_[ni], MS[mi], FS[fi], TS[ti], RS[ri], vol ? " vol" : "    ", CTXN[cx], w ? "full" : "2h", SIDEN[sd], TGN[q]); return buf; };
  auto isSt = [&](size_t c) { St a = R[c].p[0]; const St& b = R[c].p[1]; a.n += b.n; a.s += b.s; a.q += b.q; a.pts += b.pts; a.gp += b.gp; a.gl += b.gl; return a; };
  auto robust = [&](size_t c) { int det = detOf(c), post = c % NPOST; int idx[7]; int x = det; for (int k = 6; k >= 0; --k) { idx[k] = x % DIMS[k]; x /= DIMS[k]; }
    int pos = 0, tot = 0; for (int k = 0; k < 7; ++k) for (int dd = -1; dd <= 1; dd += 2) { int v = idx[k] + dd; if (v < 0 || v >= DIMS[k]) continue; int id2[7]; std::copy(idx, idx + 7, id2); id2[k] = v;
      int d2 = 0; for (int kk = 0; kk < 7; ++kk) d2 = d2 * DIMS[kk] + id2[kk]; St a = isSt((size_t)d2 * NPOST + post); if (a.n < 60) continue; tot++; if (a.m() > 0) pos++; }
    return tot >= 4 && pos >= 0.75 * tot; };
  printf("Flag optimisation — %s. %d detectors x %d = %zu configs. IS 2010-07 → 2020-12 (%.1f yrs), OOS 2021-01 → 2026-10 (%.1f yrs, no data 2026-03-20 → 09-18).\n"
         "pts = net NQ-equivalent points per trade at today's range. Selection uses IS only.\n", sym, NDET, NPOST, R.size(), yrsIS, yrsO);
  for (int se = 0; se < 3; ++se) {
    std::vector<size_t> elig, pick; for (size_t c = 0; c < R.size(); ++c) { if ((int)((c % NPOST) / (5 * 3 * 2 * 5)) != se) continue; St a = isSt(c); if (a.n < 120) continue; elig.push_back(c);
      if (R[c].p[0].m() > 0 && R[c].p[1].m() > 0 && a.t() > 0 && robust(c)) pick.push_back(c); }
    std::sort(elig.begin(), elig.end(), [&](size_t a, size_t b) { return isSt(a).t() > isSt(b).t(); });
    std::sort(pick.begin(), pick.end(), [&](size_t a, size_t b) { return isSt(a).t() > isSt(b).t(); });
    auto oosSum = [&](const std::vector<size_t>& v, size_t k) { double pos = 0, sp = 0, sn = 0; k = std::min(k, v.size()); for (size_t i = 0; i < k; ++i) { const St& o = R[v[i]].p[2]; if (o.n < 10) continue; sn++; if (o.pts > 0) pos++; sp += o.pts / o.n; }
      printf("    %-34s %6zu configs: OOS positive %5.1f%%, mean OOS %+6.2f pts/trade\n", "", k, sn ? 100 * pos / sn : 0, sn ? sp / sn : 0); };
    printf("\n==================== %s — %zu configs with IS n >= 120, %zu pass the robust rule\n", SN[se], elig.size(), pick.size());
    printf("  does IS ranking carry forward?  all eligible / top 5%% / top 1%% by IS t / robust picks / top 20 robust:\n");
    oosSum(elig, elig.size()); oosSum(elig, elig.size() / 20); oosSum(elig, elig.size() / 100); oosSum(pick, pick.size()); oosSum(pick, 20);
    printf("  top robust picks (IS t)                                                    IS: /yr  pts   PF    t  |  OOS: /yr  pts   PF    t\n");
    auto line = [&](size_t c) { St a = isSt(c); const St& o = R[c].p[2];
      printf("  %s %5.0f %+6.2f %4.2f %+5.1f | %5.0f %+6.2f %4.2f %+5.1f\n", label(c), a.n / yrsIS, a.pts / a.n, a.gl > 0 ? a.gp / a.gl : 0, a.t(), o.n / yrsO, o.n ? o.pts / o.n : 0, o.gl > 0 ? o.gp / o.gl : 0, o.t()); };
    for (size_t i = 0; i < std::min<size_t>(15, pick.size()); ++i) line(pick[i]);
    for (double rate : {0.5, 1.0, 2.0}) { for (size_t c : pick) if (isSt(c).n / yrsIS / 252 >= rate) { printf("  best robust pick with >= %.1f a day:\n", rate); line(c); break; } }
    if (se == 2) { // flag5_trend reference: 5m pole 6>=0.20 flag 5<=0.6 rt1.0 no vol, with+st.55, full, both, 1.5R
      int det = ((((((1 * 4 + 2) * 4 + 2) * 3 + 2) * 3 + 1) * 2 + 1) * 2 + 0); size_t c = (size_t)det * NPOST + (((2 * 5 + 3) * 2 + 1) * 3 + 2) * 5 + 1;
      printf("  reference flag5_trend (adopted):\n"); line(c); }
  }
  return 0;
}
