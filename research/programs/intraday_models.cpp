// 2026-10-19 intraday models (research/preregistered/2026-10-19_intraday_models.md): A) predict the NY RTH range at 09:30 from what is
// known before the open; B) at 09:50 / 10:30, predict the move to 15:55 from the move so far, path, VWAP, ES, levels and VIX — and compare
// with "go with the move". Walk-forward by year 2014–2024, 2025–26 one look. Boosted depth-2 trees + ridge. NQ 1m, read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
using namespace lab::detail;

struct Win { double o = NAN, h = NAN, l = NAN, c = NAN; bool ok = false; };
static Win window(const Series& s, const Day& d, int t0, int t1) {
  Win w; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < t0 || tm >= t1) continue;
    if (!w.ok) { w.o = s.o[i]; w.h = s.h[i]; w.l = s.l[i]; w.ok = true; }
    w.h = std::max(w.h, (double)s.h[i]); w.l = std::min(w.l, (double)s.l[i]); w.c = s.c[i]; }
  return w; }
static double px_at(const Series& s, const Day& D, int h, int m) {
  int i = first_bar_at(s, D, hm_to_tday(h, m), D.b0); if (i >= D.b1) return s.c[D.b1 - 1]; return i > D.b0 ? s.c[i - 1] : s.o[i]; }

struct Ex { int date; std::vector<float> x; double y, aux; };
struct Pred { int date; double pm, pr, y, aux; double thr; };

static std::vector<Pred> walk(const std::vector<Ex>& ex, std::vector<double>* gain) {
  std::vector<Pred> P;
  for (int Y = 2014; Y <= 2026; ++Y) {
    std::vector<std::vector<float>> X; std::vector<double> y;
    for (auto& e : ex) if (e.date / 10000 < Y && e.date < 20250101) { X.push_back(e.x); y.push_back(e.y); }
    if (X.size() < 500) continue;
    Gbm g = gbm_fit(X, y, GbmParams{}); Ridge r = ridge_fit(X, y);
    if (gain && Y <= 2024) { if (gain->empty()) gain->assign(g.gain.size(), 0); for (size_t f = 0; f < g.gain.size(); ++f) (*gain)[f] += g.gain[f]; }
    std::vector<double> tr; for (auto& x : X) tr.push_back(std::fabs(g.predict(x))); std::sort(tr.begin(), tr.end()); double thr = tr[tr.size() / 2];
    for (auto& e : ex) if (e.date / 10000 == Y) P.push_back({e.date, g.predict(e.x), r.predict(e.x), e.y, e.aux, thr});
  }
  return P; }
static int era(int d) { return d < 20140101 ? -1 : d < 20190101 ? 0 : d < 20250101 ? 1 : 2; }
static const char* EN[3] = {"2014-18", "2019-24", "2025-26 (holdout)"};
static void topgain(const char* lab, const std::vector<std::string>& FN, const std::vector<double>& g) {
  std::vector<int> o(FN.size()); for (size_t f = 0; f < FN.size(); ++f) o[f] = f; std::sort(o.begin(), o.end(), [&](int a, int b) { return g[a] > g[b]; });
  double gs = 0; for (double v : g) gs += v; printf("  %s top features by gain: ", lab);
  for (int i = 0; i < 7; ++i) printf("%s %.0f%%%s", FN[o[i]].c_str(), 100 * g[o[i]] / gs, i < 6 ? ", " : "\n"); }

int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s, es; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err) || !load_raw("data/edge_lab/cache", "ES", es, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); build_days(es, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  std::map<int, int> esd; for (int k = 0; k < (int)es.days.size(); ++k) if (es.days[k].ok) esd[es.days[k].date] = k;
  const int A0 = hm_to_tday(18, 0), A1 = hm_to_tday(3, 0), L1 = hm_to_tday(9, 30), N1 = hm_to_tday(16, 0);

  // ───────────── A. size model at 09:30
  const std::vector<std::string> FA = {"prev NY move", "prev NY range", "prev NY close loc", "gap", "open vs prev NY high", "open vs prev NY low",
    "Asia move", "Asia range", "London move", "London range", "open vs overnight high", "open vs overnight low", "e5", "r5", "e20", "r20", "p20",
    "vs 50-day avg", "VIX", "VIX 1d change", "VIX/VIX3M", "weekday", "FOMC", "CPI", "NFP", "earnings", "week so far", "ES pre-move"};
  const std::vector<std::string> FB = {"move from open", "path straightness", "range so far", "last 15 min move", "20-min efficiency", "vs VWAP",
    "ES move from open", "gap", "overnight range", "price vs prev NY high", "price vs prev NY low", "price vs overnight high", "price vs overnight low",
    "prev NY move", "VIX", "VIX 1d change", "VIX/VIX3M", "e20", "r20", "weekday", "FOMC", "CPI", "NFP"};
  std::vector<Ex> exA, exB[2];
  int prevk = -1; double weekOpen = NAN; int weekKey = -1;
  for (int k = 0; k < (int)s.days.size(); ++k) {
    Day& d = s.days[k];
    if (!d.ok || d.rollwin || !(d.atr > 0) || prevk < 0) { if (d.ok) prevk = k; continue; }
    const Day& p = s.days[prevk]; const double A = d.atr;
    Win as = window(s, d, A0, A1), ld = window(s, d, A1, L1);
    if (!as.ok || !ld.ok) { prevk = k; continue; }
    int wd = weekday_of(d.date); int wk = add_days(d.date, -((wd + 6) % 7));
    if (wk != weekKey) { weekKey = wk; weekOpen = d.ro; }
    double wsf = (wd == 1 || std::isnan(weekOpen)) ? 0 : (d.pc - weekOpen) / A;
    const Day* e = nullptr; if (auto it = esd.find(d.date); it != esd.end() && es.days[it->second].atr > 0) e = &es.days[it->second];
    double onH = std::max(as.h, ld.h), onL = std::min(as.l, ld.l), open = d.ro;
    std::vector<float> xa = {(float)((p.rc - p.ro) / A), (float)((p.rh - p.rl) / A), (float)d.pibs, (float)((open - d.pc) / A),
      (float)((open - d.ph) / A), (float)((open - d.pl) / A), (float)((as.c - as.o) / A), (float)((as.h - as.l) / A), (float)((ld.c - ld.o) / A),
      (float)((ld.h - ld.l) / A), (float)((open - onH) / A), (float)((open - onL) / A), (float)d.e5, (float)d.r5, (float)d.e20, (float)d.r20,
      (float)d.p20, (float)((d.pc - d.sma50) / A), (float)d.vix, (float)d.vixc1, (float)d.vts, (float)wd, (float)d.fomc, (float)d.cpi,
      (float)d.nfp, (float)d.earn, (float)wsf, e ? (float)((e->ro - e->pc) / e->atr) : NAN};
    exA.push_back({d.date, xa, (d.rh - d.rl) / A, 0});
    // B: at 09:50 and 10:30
    const int TH[2] = {9, 10}, TM[2] = {50, 30};
    for (int c = 0; c < 2; ++c) {
      int tT = hm_to_tday(TH[c], TM[c]); double hi = -1e18, lo = 1e18, pv = 0, vv = 0; int n = 0; std::vector<double> cl;
      for (int i = d.rth0; i < d.rth1 && tday_min(s.t[i]) < tT; ++i) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]);
        double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i]; cl.push_back(s.c[i]); ++n; }
      if (n < 15) continue;
      double px = cl.back(), mv = (px - open) / A, rng = hi - lo, vw = vv > 0 ? pv / vv : NAN;
      double l15 = (px - cl[cl.size() - 15]) / A; double path = 0; int m20 = std::min<int>(20, cl.size() - 1);
      for (int j = cl.size() - m20; j < (int)cl.size(); ++j) path += std::fabs(cl[j] - cl[j - 1]);
      double er20 = path > 0 ? std::fabs(px - cl[cl.size() - 1 - m20]) / path : 0;
      double esmv = NAN; if (e) { double ep = px_at(es, *e, TH[c], TM[c]); esmv = (ep - e->ro) / e->atr; }
      double y = (px_at(s, d, 15, 55) - px) / A;
      std::vector<float> xb = {(float)mv, (float)(rng > 0 ? std::fabs(px - open) / rng : 0), (float)(rng / A), (float)l15, (float)er20,
        (float)((px - vw) / A), (float)esmv, (float)((open - d.pc) / A), (float)((onH - onL) / A), (float)((px - d.ph) / A), (float)((px - d.pl) / A),
        (float)((px - onH) / A), (float)((px - onL) / A), (float)((p.rc - p.ro) / A), (float)d.vix, (float)d.vixc1, (float)d.vts,
        (float)d.e20, (float)d.r20, (float)wd, (float)d.fomc, (float)d.cpi, (float)d.nfp};
      exB[c].push_back({d.date, xb, y, mv});
    }
    prevk = k;
  }

  printf("NQ intraday models — %zu days. Walk-forward by year 2014–2024, 2025–26 one look. ATR units.\n", exA.size());
  printf("\n== A. NY range at 09:30 (range / ATR)\n");
  { std::vector<double> g; auto P = walk(exA, &g);
    for (int e = 0; e < 3; ++e) { std::vector<std::pair<double, double>> v; double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0, sr = 0, srr = 0, srb = 0;
      for (auto& q : P) if (era(q.date) == e) { v.push_back({q.pm, q.y}); sa += q.pm; sb += q.y; saa += q.pm * q.pm; sbb += q.y * q.y; sab += q.pm * q.y; sr += q.pr; srr += q.pr * q.pr; srb += q.pr * q.y; }
      int n = v.size(); if (!n) continue; double c = (n * sab - sa * sb) / std::sqrt((n * saa - sa * sa) * (n * sbb - sb * sb)), cr = (n * srb - sr * sb) / std::sqrt((n * srr - sr * sr) * (n * sbb - sb * sb));
      std::sort(v.begin(), v.end()); printf("  %-18s n %4d | corr pred/actual %+.3f (ridge %+.3f) | actual range by predicted fifth:", EN[e], n, c, cr);
      for (int q = 0; q < 5; ++q) { double m = 0; int a = q * n / 5, b = (q + 1) * n / 5; for (int i = a; i < b; ++i) m += v[i].second; printf(" %.2f", m / (b - a)); } printf("\n"); }
    topgain("range model", FA, g);
    if (FILE* f = fopen("research/programs/out/intraday_range_pred.csv", "w")) { fprintf(f, "date,pred_range,ridge_range,range\n"); for (auto& q : P) fprintf(f, "%d,%.5f,%.5f,%.5f\n", q.date, q.pm, q.pr, q.y); fclose(f); } }

  for (int c = 0; c < 2; ++c) {
    printf("\n== B. Direction after the open, from %s to 15:55 (move in ATR; trades after 1 pt cost)\n", c ? "10:30" : "09:50");
    std::vector<double> g; auto P = walk(exB[c], &g);
    std::map<int, double> atrOf; for (auto& d : s.days) if (d.atr > 0) atrOf[d.date] = d.atr;
    for (int e = 0; e < 3; ++e) {
      int n = 0, hit = 0, hitR = 0; double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
      double t1 = 0, t2 = 0, t3 = 0, t3r = 0, q1 = 0, q2 = 0, q3 = 0; int n1 = 0, n2 = 0, n3 = 0, n3r = 0;
      for (auto& q : P) { if (era(q.date) != e) continue; double cost = 1.0 / atrOf[q.date]; ++n;
        hit += (q.pm > 0) == (q.y > 0); hitR += (q.pr > 0) == (q.y > 0); sa += q.pm; sb += q.y; saa += q.pm * q.pm; sbb += q.y * q.y; sab += q.pm * q.y;
        if (std::fabs(q.aux) > 0.25) { int dir = q.aux > 0 ? 1 : -1; double v = dir * q.y - cost; t1 += v; q1 += v * v; ++n1;
          if (q.pm * dir > 0) { t3 += v; q3 += v * v; ++n3; } if (q.pr * dir > 0) { t3r += v; ++n3r; } }
        if (std::fabs(q.pm) >= q.thr) { double v = (q.pm > 0 ? 1 : -1) * q.y - cost; t2 += v; q2 += v * v; ++n2; } }
      if (!n) continue;
      auto tt = [](double s, double q, int n) -> double { if (n < 3) return NAN; double m = s / n, sd = std::sqrt(std::max(1e-12, q / n - m * m)); return m / (sd / std::sqrt((double)n)); };
      double corr = (n * sab - sa * sb) / std::sqrt((n * saa - sa * sa) * (n * sbb - sb * sb));
      printf("  %-18s n %4d | hit %4.1f%% (ridge %4.1f%%) corr %+.3f || (i) go with the move: %4d %+.4f (t %+.2f) | (ii) model direction: %4d %+.4f (t %+.2f) | (iii) move + model agrees: %4d %+.4f (t %+.2f), ridge agrees: %4d %+.4f\n",
             EN[e], n, 100.0 * hit / n, 100.0 * hitR / n, corr, n1, n1 ? t1 / n1 : NAN, tt(t1, q1, n1), n2, n2 ? t2 / n2 : NAN, tt(t2, q2, n2), n3, n3 ? t3 / n3 : NAN, tt(t3, q3, n3), n3r, n3r ? t3r / n3r : NAN);
    }
    topgain("direction model", FB, g);
    char fn[96]; snprintf(fn, sizeof fn, "research/programs/out/intraday_dir_%s.csv", c ? "1030" : "0950");
    if (FILE* f = fopen(fn, "w")) { fprintf(f, "date,pred,ridge,move_to_close,move_so_far\n"); for (auto& q : P) fprintf(f, "%d,%.5f,%.5f,%.5f,%.5f\n", q.date, q.pm, q.pr, q.y, q.aux); fclose(f); }
  }
  return 0;
}
