// 2026-10-19 (founder: "from all NQ analysis and behaviour and all the bull/bear flag information we gathered, do a full review and
// analysis … to do more targeted tests. I want to trade bear/bull flags but we want to calculate those correctly"):
// one dataset of every structural NY flag on 2/3/4/5m, bull and bear (bears on the mirrored session), NQ 2010-07 → 2026-10, with
// the features today's studies pointed at, then three targeted tests:
//  A  why 2025-26 differs: R by year and by regime known at the entry (VIX, ATR percentile, prior-day trend vs SMA50, session path)
//  B  order flow (aggressor volume, MBO months 2023-03 → 2026-03 + collector months 2026-09+): buy share inside the pole and the flag
//  C  a scoring model fitted only on earlier years (walk-forward logistic regression, refit every year): does the top 30 % of next
//     year's flags beat the rest?
// Flag (as flag_mtf_entry.cpp): pole = lowest low of the 2-15 bars before the pole top up to the top, ≥ 3× the session's average
// bar; flag = the 2 bars after the top with no new high, pullback 15-50 % of the pole; registered at the 2nd flag bar's close,
// 09:45-14:00, one at a time per chart and side. Context is recorded, not required. Entries: P = stop 1 tick above the pole top
// (stop = flag low), T2 = the 2m trigger (first 2m close above the previous 2m high after a lower high inside the flag, stop = flag
// low); target = pole top + pole length; flat 15:55; 2 pts cost scaled to the day; same-minute fill + stop = loss.
// Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k; };
enum F { POLEX, POLEATR, POLEBARS, POLESPEED, PULL, FLAGVOLR, CTXOPEN, CTXVWAP, STRAIGHT, MINUTE, GAP, RANGESOFAR, VIX, ATRPCT, TREND50, DOW, POLEBUY, FLAGBUY, NF };
const char* FNAME[NF] = {"pole / avg bar", "pole / ATR", "pole bars", "pole speed (ATR per bar)", "pullback share", "flag vol / pole vol", "above 09:30 open", "above VWAP", "path straightness",
  "minutes after 09:30", "gap / ATR (trend side +)", "range so far / ATR", "VIX (prior close)", "ATR percentile (250d)", "prior close vs SMA50 (trend side +)", "day of week", "pole buy share", "flag buy share"};
static int r_era(int d) { return d < 20190101 ? 0 : d < 20250101 ? 1 : 2; }
struct Row { int date, era, year, tf, side; bool flow; double f[NF]; double R[2]; double risk[2]; int em[2], xm[2]; };   // em/xm = entry / exit minute after 09:30
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  // ATR percentile over the prior 250 sessions
  std::map<int, double> atrPct; { std::vector<double> hist; for (auto& d : s.days) { if (!(d.atr > 0)) continue; if (hist.size() >= 60) { int below = 0; size_t n0 = hist.size() > 250 ? hist.size() - 250 : 0; for (size_t j = n0; j < hist.size(); ++j) below += hist[j] < d.atr; atrPct[d.date] = (double)below / (hist.size() - n0); } hist.push_back(d.atr); } }
  std::vector<Row> rows;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const double A = d.atr, sc = A / 320.0, cost = 2 * sc; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    struct tm tmv = {}; tmv.tm_year = d.date / 10000 - 1900; tmv.tm_mon = d.date / 100 % 100 - 1; tmv.tm_mday = d.date % 100; timegm(&tmv);
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), BV(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; BV[k] = side > 0 ? s.bv[i] : s.v[i] - s.bv[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3 * V[k]; vv += V[k]; vw[k] = vv > 0 ? pv / vv : Cc[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]); hiS[k] = hi; loS[k] = lo; } }
      const double so = O[0], pc = side * d.pc, sma = side * d.sma50;
      int lastX = 0; auto exitR = [&](size_t k0, double e, double st, double tg, bool fillMin) -> double { double px = NAN; lastX = TM[k0] - 930; if (fillMin && Lw[k0] <= st) px = st;
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { lastX = TM[k] - 930; if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / (e - st); };
      for (int tf = 2; tf <= 5; ++tf) { std::vector<FB> b; int busy = -1;
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          if ((tm - 930) % tf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], BV[k], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.bv += BV[k]; z.k = k; }
          if ((tm + 1 - 930) % tf) continue; const int q = b.size() - 1; if (q < 6 || q <= busy || tm < 944 || tm >= 1260) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 3 * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const size_t kq = b[q].k; const double cl = b[q].c;
          Row r{}; r.date = d.date; r.era = era; r.year = d.date / 10000; r.tf = tf; r.side = side; r.flow = d.flow;
          double pv = 0, pbv = 0, fv = 0, fbv = 0; for (int x = l; x <= h; ++x) { pv += b[x].v; pbv += b[x].bv; } for (int x = h + 1; x <= q; ++x) { fv += b[x].v; fbv += b[x].bv; }
          r.f[POLEX] = M / ar; r.f[POLEATR] = M / A; r.f[POLEBARS] = h - l + 1; r.f[POLESPEED] = M / A / (h - l + 1); r.f[PULL] = pull; r.f[FLAGVOLR] = (fv / 2) / (pv / (h - l + 1));
          r.f[CTXOPEN] = cl > so; r.f[CTXVWAP] = cl > vw[kq]; r.f[STRAIGHT] = hiS[kq] > loS[kq] ? (cl - so) / (hiS[kq] - loS[kq]) : 0; r.f[MINUTE] = tm + 1 - 930;
          r.f[GAP] = std::isnan(pc) ? 0 : (so - pc) / A; r.f[RANGESOFAR] = (hiS[kq] - loS[kq]) / A; r.f[VIX] = std::isnan(d.vix) ? 18 : d.vix; r.f[ATRPCT] = atrPct.count(d.date) ? atrPct[d.date] : 0.5;
          r.f[TREND50] = (std::isnan(pc) || std::isnan(sma)) ? 0 : (pc - sma) / A; r.f[DOW] = tmv.tm_wday; r.f[POLEBUY] = pv > 0 ? pbv / pv : 0.5; r.f[FLAGBUY] = fv > 0 ? fbv / fv : 0.5;
          r.R[0] = r.R[1] = NAN; r.risk[0] = r.risk[1] = NAN;
          // follow: pole-top break (P) on 1m; the 2m trigger (T2) on 2m bars built from the flag start
          double fl = fl0; int nb = 0; double hbh = -1e18, hbl = 1e18; double p2h = NAN; bool lowerHigh = false; double b2h = -1e18, b2l = 1e18, b2o = 0; bool doneF = false;
          for (size_t kk = kq + 1; kk < n && !doneF; ++kk) { const int t2 = TM[kk]; if (t2 >= 1260) break;
            if (std::isnan(r.R[0]) && Hh[kk] > H) { r.R[0] = exitR(kk, H + 0.25, std::min(fl, hbl) - 0.25, H + M, true); r.em[0] = TM[kk] - 930; r.xm[0] = lastX; r.risk[0] = (H + 0.5 - std::min(fl, hbl)) / sc; }
            if (b2h < -1e17) b2o = O[kk]; b2h = std::max(b2h, Hh[kk]); b2l = std::min(b2l, Lw[kk]); hbh = std::max(hbh, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
            if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[kk];
              if (!std::isnan(p2h) && b2h <= H && std::isnan(r.R[1])) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { r.R[1] = exitR(kk, c2, fln - 0.25, H + M, false); r.em[1] = TM[kk] - 930; r.xm[1] = lastX; r.risk[1] = (c2 - fln + 0.25) / sc; } }
              p2h = b2h; b2h = -1e18; b2l = 1e18; (void)b2o; }
            if ((t2 + 1 - 930) % tf == 0) { nb++; fl = std::min(fl, hbl); if (hbh > H || Cc[kk] < H - 0.5 * M || nb >= 12) doneF = true; hbh = -1e18; hbl = 1e18; } }
          busy = q + nb; rows.push_back(r); } } } }
  printf("Flag review — NY 2/3/4/5m structural flags, bull + bear, NQ 2010-07 → 2026-10: %zu flags. R after 2 pts cost; P = pole-top stop entry, T2 = 2m trigger.\n", rows.size());
  auto stat = [&](const std::vector<const Row*>& a, int e, double& n, double& m, double& w) { n = 0; m = 0; w = 0; for (auto* r : a) if (!std::isnan(r->R[e])) { n++; m += r->R[e]; w += r->R[e] > 0; } if (n) { m /= n; w /= n; } };
  auto core = [](const Row& r) { return r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0 && r.f[STRAIGHT] >= 0.55 && r.f[POLEX] >= 4 && r.tf >= 3; };
  // ---------- A. by year, the core definition (context + pole ≥ 4× bar, 3-5m)
  printf("\n== A1. Core flags (context on, pole >= 4x bar, 3-5m) by year — n | avg R for P and T2 entries\n  year |        bull P         bull T2 |        bear P         bear T2\n");
  for (int y = 2010; y <= 2026; ++y) { printf("  %d |", y); for (int side : {1, -1}) { for (int e = 0; e < 2; ++e) { std::vector<const Row*> a; for (auto& r : rows) if (r.year == y && r.side == side && core(r)) a.push_back(&r); double n, m, w; stat(a, e, n, m, w); printf(" %4.0f %+6.3f", n, m); } printf(" |"); } printf("\n"); }
  // ---------- A2. regimes known at the entry, core bull flags, per period
  printf("\n== A2. Core flags by regime known at the entry — avg R (n) per period 2010-18 | 2019-24 | 2025-26, entry T2\n");
  struct Bk { const char* name; std::function<bool(const Row&)> f; };
  std::vector<Bk> bks = { {"VIX < 15", [](const Row& r) { return r.f[VIX] < 15; }}, {"VIX 15-20", [](const Row& r) { return r.f[VIX] >= 15 && r.f[VIX] < 20; }}, {"VIX 20-25", [](const Row& r) { return r.f[VIX] >= 20 && r.f[VIX] < 25; }}, {"VIX >= 25", [](const Row& r) { return r.f[VIX] >= 25; }},
    {"ATR pct < 0.33", [](const Row& r) { return r.f[ATRPCT] < 0.33; }}, {"ATR pct 0.33-0.67", [](const Row& r) { return r.f[ATRPCT] >= 0.33 && r.f[ATRPCT] < 0.67; }}, {"ATR pct >= 0.67", [](const Row& r) { return r.f[ATRPCT] >= 0.67; }},
    {"prior close with SMA50 trend", [](const Row& r) { return r.f[TREND50] > 0; }}, {"prior close against SMA50", [](const Row& r) { return r.f[TREND50] <= 0; }},
    {"gap with the flag", [](const Row& r) { return r.f[GAP] > 0.1; }}, {"no gap", [](const Row& r) { return std::fabs(r.f[GAP]) <= 0.1; }}, {"gap against", [](const Row& r) { return r.f[GAP] < -0.1; }},
    {"before 11:00", [](const Row& r) { return r.f[MINUTE] < 90; }}, {"11:00-12:30", [](const Row& r) { return r.f[MINUTE] >= 90 && r.f[MINUTE] < 180; }}, {"after 12:30", [](const Row& r) { return r.f[MINUTE] >= 180; }},
    {"pole >= 0.40 ATR", [](const Row& r) { return r.f[POLEATR] >= 0.40; }}, {"pole < 0.40 ATR", [](const Row& r) { return r.f[POLEATR] < 0.40; }},
    {"range so far < 0.6 ATR", [](const Row& r) { return r.f[RANGESOFAR] < 0.6; }}, {"range so far >= 0.6 ATR", [](const Row& r) { return r.f[RANGESOFAR] >= 0.6; }} };
  for (int side : {1, -1}) { printf("  -- %s\n", side > 0 ? "bull" : "bear"); for (auto& bk : bks) { printf("  %-30s", bk.name); for (int era = 0; era < 3; ++era) { std::vector<const Row*> a; for (auto& r : rows) if (r.side == side && r.era == era && core(r) && bk.f(r)) a.push_back(&r); double n, m, w; stat(a, 1, n, m, w); printf(" | %+6.3f (%4.0f)", m, n); } printf("\n"); } }
  // ---------- B. order flow, flow days only, core flags, terciles of pole and flag buy share (trend side)
  printf("\n== B. Order flow (aggressor volume; flow days only) — core flags, entry P and T2: avg R (n) by tercile of the trend-side buy share\n");
  for (int side : {1, -1}) for (int fi : {POLEBUY, FLAGBUY}) { std::vector<const Row*> a; for (auto& r : rows) if (r.flow && r.side == side && core(r)) a.push_back(&r); if (a.size() < 60) continue;
    std::vector<double> v; for (auto* r : a) v.push_back(r->f[fi]); std::sort(v.begin(), v.end()); double c1 = v[v.size() / 3], c2 = v[2 * v.size() / 3];
    printf("  %s %-16s (cuts %.3f %.3f):", side > 0 ? "bull" : "bear", FNAME[fi], c1, c2);
    for (int g = 0; g < 3; ++g) { std::vector<const Row*> b; for (auto* r : a) { int gg = r->f[fi] < c1 ? 0 : r->f[fi] < c2 ? 1 : 2; if (gg == g) b.push_back(r); } double n, m, w, n2, m2, w2; stat(b, 0, n, m, w); stat(b, 1, n2, m2, w2); printf("  %s P %+.3f (%.0f) T2 %+.3f (%.0f)", g == 0 ? "low" : g == 1 ? "mid" : "high", m, n, m2, n2); }
    printf("\n"); }
  { int nf = 0; for (auto& r : rows) nf += r.flow; printf("  (%d flags on flow days)\n", nf); }
  // ---------- C. walk-forward logistic model, all flags (no hard filter), per side, entry T2 (fallback P)
  printf("\n== C. Walk-forward model: logistic regression on all %d features (except order flow), refit each year on all earlier years, predicts P(R > 0) for entry T2.\n", NF - 2);
  printf("   Per test year: mean R of all flags | of the model's top 30 %% | of the core rule; then totals per period.\n");
  for (int side : {1, -1}) { printf("  -- %s\n", side > 0 ? "bull" : "bear"); double tot[3][3] = {}, cnt[3][3] = {};
    for (int ty = 2013; ty <= 2026; ++ty) {
      std::vector<const Row*> tr, te; for (auto& r : rows) if (r.side == side && !std::isnan(r.R[1])) { if (r.year < ty) tr.push_back(&r); else if (r.year == ty) te.push_back(&r); } if (tr.size() < 500 || te.size() < 20) continue;
      const int K = NF - 2; std::vector<double> mu(K, 0), sd(K, 0); for (auto* r : tr) for (int j = 0; j < K; ++j) mu[j] += r->f[j]; for (int j = 0; j < K; ++j) mu[j] /= tr.size();
      for (auto* r : tr) for (int j = 0; j < K; ++j) sd[j] += (r->f[j] - mu[j]) * (r->f[j] - mu[j]); for (int j = 0; j < K; ++j) sd[j] = std::sqrt(sd[j] / tr.size()) + 1e-9;
      std::vector<double> wv(K + 1, 0); for (int it = 0; it < 300; ++it) { std::vector<double> g(K + 1, 0); for (auto* r : tr) { double z = wv[K]; for (int j = 0; j < K; ++j) z += wv[j] * (r->f[j] - mu[j]) / sd[j]; double p = 1 / (1 + std::exp(-z)), y = r->R[1] > 0; for (int j = 0; j < K; ++j) g[j] += (p - y) * (r->f[j] - mu[j]) / sd[j]; g[K] += p - y; }
        for (int j = 0; j <= K; ++j) wv[j] -= 0.5 * (g[j] / tr.size() + (j < K ? 0.01 * wv[j] : 0)); }
      std::vector<std::pair<double, const Row*>> sc2; for (auto* r : te) { double z = wv[K]; for (int j = 0; j < K; ++j) z += wv[j] * (r->f[j] - mu[j]) / sd[j]; sc2.push_back({z, r}); }
      // the cut is the training set's 70th percentile of the score (no peeking at the test year)
      std::vector<double> trs; for (auto* r : tr) { double z = wv[K]; for (int j = 0; j < K; ++j) z += wv[j] * (r->f[j] - mu[j]) / sd[j]; trs.push_back(z); } std::sort(trs.begin(), trs.end()); double cut = trs[trs.size() * 7 / 10];
      double ma = 0, na = te.size(), mt = 0, nt = 0, mc = 0, nc = 0; for (auto& [z, r] : sc2) { ma += r->R[1]; if (z >= cut) { mt += r->R[1]; nt++; } if (core(*r)) { mc += r->R[1]; nc++; } }
      const int era = ty < 2019 ? 0 : ty < 2025 ? 1 : 2; tot[era][0] += ma; cnt[era][0] += na; tot[era][1] += mt; cnt[era][1] += nt; tot[era][2] += mc; cnt[era][2] += nc;
      printf("    %d  all %+.3f (%4.0f) | model top 30%% %+.3f (%4.0f) | core rule %+.3f (%3.0f)\n", ty, ma / na, na, nt ? mt / nt : 0, nt, nc ? mc / nc : 0, nc);
      if (ty == 2026) { printf("    weights (fitted on 2010-2025, standardised):"); for (int j = 0; j < K; ++j) printf(" %s %+.2f;", FNAME[j], wv[j]); printf("\n"); } }
    const char* EP[3] = {"2013-18", "2019-24", "2025-26"}; for (int e = 0; e < 3; ++e) printf("    %s  all %+.3f | model top 30%% %+.3f (%.0f) | core rule %+.3f (%.0f)\n", EP[e], cnt[e][0] ? tot[e][0] / cnt[e][0] : 0, cnt[e][1] ? tot[e][1] / cnt[e][1] : 0, cnt[e][1], cnt[e][2] ? tot[e][2] / cnt[e][2] : 0, cnt[e][2]); }
  // ---------- D. trading every day: the FIRST qualifying flag of each session (either side, 3-5m), one trade a day
  printf("\n== D. One trade a day: the first qualifying flag of each session (bull or bear, 3-5m; entry T2, else P). Share of sessions with a trade | avg R per period 2010-18 / 2019-24 / 2025-26 | win %%\n");
  { std::set<int> allDays; for (auto& r : rows) allDays.insert(r.date); double perEra[3] = {0, 0, 0}; for (int d0 : allDays) perEra[d0 < 20190101 ? 0 : d0 < 20250101 ? 1 : 2]++;
    struct Lv { const char* name; std::function<bool(const Row&)> f; };
    std::vector<Lv> lv = { {"core rule, both sides", [&](const Row& r) { return core(r); }}, {"core rule, bull only", [&](const Row& r) { return core(r) && r.side > 0; }},
      {"core + before 12:30 + pole >= 0.4 ATR, both", [&](const Row& r) { return core(r) && r.f[MINUTE] < 180 && r.f[POLEATR] >= 0.4; }},
      {"loose: pole >= 3x, above open + VWAP, both", [](const Row& r) { return r.tf >= 3 && r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0; }},
      {"loosest: any structural flag 3-5m, both", [](const Row& r) { return r.tf >= 3; }} };
    for (auto& L : lv) { std::map<int, const Row*> first; for (auto& r : rows) { if (!L.f(r)) continue; const double R = !std::isnan(r.R[1]) ? r.R[1] : r.R[0]; if (std::isnan(R)) continue; auto it = first.find(r.date); if (it == first.end() || r.f[MINUTE] < it->second->f[MINUTE]) first[r.date] = &r; }
      double n[3] = {0, 0, 0}, sR[3] = {0, 0, 0}, w = 0, tot = 0; for (auto& [d0, r] : first) { const double R = !std::isnan(r->R[1]) ? r->R[1] : r->R[0]; n[r->era]++; sR[r->era] += R; w += R > 0; tot++; }
      printf("  %-46s days with a trade %3.0f%% / %3.0f%% / %3.0f%% | avg R %+.3f / %+.3f / %+.3f | win %2.0f%%\n", L.name, 100 * n[0] / perEra[0], 100 * n[1] / perEra[1], 100 * n[2] / perEra[2], n[0] ? sR[0] / n[0] : 0, n[1] ? sR[1] / n[1] : 0, n[2] ? sR[2] / n[2] : 0, tot ? 100 * w / tot : 0); } }
  // ---------- E. up to N trades a day: the first N qualifying flags of each session, at least 15 min apart (so 3m/4m/5m copies of one move count once)
  printf("\n== E. Up to N trades a day — first N qualifying flags of the session, >= 15 min apart (entry T2, else P). Trades a day | avg R per trade 2010-18 / 2019-24 / 2025-26 | R per day 2019-24 / 2025-26 | days with >= 3 | win %%\n");
  { std::set<int> allDays; for (auto& r : rows) allDays.insert(r.date); double perEra[3] = {0, 0, 0}; for (int d0 : allDays) perEra[d0 < 20190101 ? 0 : d0 < 20250101 ? 1 : 2]++;
    struct Lv { const char* name; std::function<bool(const Row&)> f; };
    std::vector<Lv> lv = { {"core rule, both sides, 3-5m", [&](const Row& r) { return core(r); }},
      {"loose: pole >= 3x, above open + VWAP, 3-5m", [](const Row& r) { return r.tf >= 3 && r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0; }},
      {"loose, 2-5m", [](const Row& r) { return r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0; }},
      {"loose, 2-5m, before 12:30", [](const Row& r) { return r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0 && r.f[MINUTE] < 180; }},
      {"loose, 2-5m, pole >= 0.3 ATR", [](const Row& r) { return r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0 && r.f[POLEATR] >= 0.3; }},
      {"loosest: any structural flag 2-5m", [](const Row&) { return true; }} };
    for (int N : {3, 5}) for (auto& L : lv) { std::map<int, std::vector<const Row*>> byDay; for (auto& r : rows) { if (!L.f(r)) continue; const double R = !std::isnan(r.R[1]) ? r.R[1] : r.R[0]; if (std::isnan(R)) continue; byDay[r.date].push_back(&r); }
      double n[3] = {0, 0, 0}, sR[3] = {0, 0, 0}, w = 0, tot = 0, d3 = 0, nd = 0;
      for (auto& [d0, v] : byDay) { std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->f[MINUTE] < b->f[MINUTE]; }); std::vector<double> took;
        for (auto* r : v) { if ((int)took.size() >= N) break; bool near = false; for (double t : took) if (std::fabs(r->f[MINUTE] - t) < 15) near = true; if (near) continue; took.push_back(r->f[MINUTE]); const double R = !std::isnan(r->R[1]) ? r->R[1] : r->R[0]; n[r->era]++; sR[r->era] += R; w += R > 0; tot++; }
        nd++; d3 += took.size() >= 3; }
      printf("  N=%d %-42s %.2f/day | %+.3f / %+.3f / %+.3f | per day %+.2fR / %+.2fR | >=3 on %2.0f%% of days | win %2.0f%%\n", N, L.name, tot / (perEra[0] + perEra[1] + perEra[2]), n[0] ? sR[0] / n[0] : 0, n[1] ? sR[1] / n[1] : 0, n[2] ? sR[2] / n[2] : 0, sR[1] / perEra[1], sR[2] / perEra[2], 100 * d3 / (perEra[0] + perEra[1] + perEra[2]), tot ? 100 * w / tot : 0); } }
  // ---------- F. daily stop rules: one position at a time (a flag is taken only if its entry is after the previous trade's exit),
  // flags in time order, entry T2 else P; a rule decides from the trades already closed whether to take the next one
  printf("\n== F. Daily stop rules — one position at a time, next flag only after the last trade closed (entry T2, else P), at most 5 a day.\n");
  printf("   trades a day | avg R per trade 2010-18 / 2019-24 / 2025-26 | R per day 2010-18 / 2019-24 / 2025-26 | %% green days | worst day | days with >= 3 trades\n");
  { std::set<int> allDays; for (auto& r : rows) allDays.insert(r.date); double perEra[3] = {0, 0, 0}; for (int d0 : allDays) perEra[d0 < 20190101 ? 0 : d0 < 20250101 ? 1 : 2]++;
    auto strict = [](const Row& r) { return r.f[MINUTE] < 180 && r.f[POLEATR] >= 0.4 && r.f[GAP] > 0.1; };
    struct Pool { const char* name; std::function<bool(const Row&)> f; };
    std::vector<Pool> pools = { {"loose 3-5m (pole >= 3x, above open + VWAP)", [](const Row& r) { return r.tf >= 3 && r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0; }},
      {"loose 2-5m", [](const Row& r) { return r.f[CTXOPEN] > 0 && r.f[CTXVWAP] > 0; }} };
    struct Rule { const char* name; int maxN; std::function<bool(const std::vector<double>&, const Row&)> take; };
    auto sumv = [](const std::vector<double>& v) { double s0 = 0; for (double x : v) s0 += x; return s0; };
    auto losses = [](const std::vector<double>& v) { int k = 0; for (double x : v) k += x <= 0; return k; };
    auto wins = [](const std::vector<double>& v) { int k = 0; for (double x : v) k += x > 0; return k; };
    std::vector<Rule> rules = {
      {"first flag only", 1, [](const std::vector<double>&, const Row&) { return true; }},
      {"up to 3, no stop", 3, [](const std::vector<double>&, const Row&) { return true; }},
      {"up to 5, no stop", 5, [](const std::vector<double>&, const Row&) { return true; }},
      {"up to 5, stop after a win", 5, [&](const std::vector<double>& v, const Row&) { return wins(v) == 0; }},
      {"up to 5, stop after 2 losses", 5, [&](const std::vector<double>& v, const Row&) { return losses(v) < 2; }},
      {"up to 5, stop after a win or 2 losses", 5, [&](const std::vector<double>& v, const Row&) { return wins(v) == 0 && losses(v) < 2; }},
      {"up to 5, stop at day +1R or -2R", 5, [&](const std::vector<double>& v, const Row&) { double s0 = sumv(v); return s0 < 1 && s0 > -2; }},
      {"up to 5, stop at day +2R or -2R", 5, [&](const std::vector<double>& v, const Row&) { double s0 = sumv(v); return s0 < 2 && s0 > -2; }},
      {"up to 3, stop after a win or 2 losses", 3, [&](const std::vector<double>& v, const Row&) { return wins(v) == 0 && losses(v) < 2; }},
      {"up to 5, stop after a win, extra trades before 12:30", 5, [&](const std::vector<double>& v, const Row& r) { return wins(v) == 0 && (v.empty() || r.f[MINUTE] < 180); }},
      {"first flag + extras only if strict (12:30, 0.4 ATR, gap)", 5, [&](const std::vector<double>& v, const Row& r) { return v.empty() || strict(r); }},
      {"first flag + strict extras, stop after a win", 5, [&](const std::vector<double>& v, const Row& r) { return wins(v) == 0 && (v.empty() || strict(r)); }},
      {"after a loss: next only if before 12:30, stop after win or 3 losses", 5, [&](const std::vector<double>& v, const Row& r) { return wins(v) == 0 && losses(v) < 3 && (v.empty() || r.f[MINUTE] < 180); }} };
    for (auto& P : pools) { printf("  -- pool: %s\n", P.name); std::map<int, std::vector<const Row*>> byDay; for (auto& r : rows) { if (!P.f(r)) continue; int e = !std::isnan(r.R[1]) ? 1 : !std::isnan(r.R[0]) ? 0 : -1; if (e < 0) continue; byDay[r.date].push_back(&r); }
      for (auto& v : byDay) std::sort(v.second.begin(), v.second.end(), [](auto a, auto b) { int ea = !std::isnan(a->R[1]) ? a->em[1] : a->em[0], eb = !std::isnan(b->R[1]) ? b->em[1] : b->em[0]; return ea < eb; });
      for (auto& Rl : rules) { double nT[3] = {0, 0, 0}, sR[3] = {0, 0, 0}, green = 0, days = 0, worst = 0, d3 = 0;
        for (auto& [d0, v] : byDay) { std::vector<double> got; int freeAt = -1;
          for (auto* r : v) { if ((int)got.size() >= Rl.maxN) break; const int e = !std::isnan(r->R[1]) ? 1 : 0; if (r->em[e] < freeAt) continue; if (!Rl.take(got, *r)) { if (!got.empty() && (Rl.name[0] == 'u' || Rl.name[0] == 'f' || Rl.name[0] == 'a')) { /* a stop rule ends the day only when it is a STOP (win / loss / P&L) */ } continue; } got.push_back(r->R[e]); freeAt = r->xm[e]; }
          if (got.empty()) continue; const double dsum = sumv(got); const int era = r_era(d0); nT[era] += got.size(); sR[era] += dsum; green += dsum > 0; days++; worst = std::min(worst, dsum); d3 += got.size() >= 3; }
        const double tot = nT[0] + nT[1] + nT[2];
        printf("  %-62s %.2f/day | %+.3f / %+.3f / %+.3f | %+.2f / %+.2f / %+.2f R/day | green %2.0f%% | worst %+.1fR | >=3 on %2.0f%%\n", Rl.name, tot / (perEra[0] + perEra[1] + perEra[2]), nT[0] ? sR[0] / nT[0] : 0, nT[1] ? sR[1] / nT[1] : 0, nT[2] ? sR[2] / nT[2] : 0, sR[0] / perEra[0], sR[1] / perEra[1], sR[2] / perEra[2], days ? 100 * green / days : 0, worst, 100 * d3 / (perEra[0] + perEra[1] + perEra[2])); } } }
  return 0;
}
