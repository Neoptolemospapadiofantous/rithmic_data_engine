// 2026-10-19 session model (research/preregistered/2026-10-19_session_model.md): does what is known at a session's open — prior
// sessions, levels, path, VIX, calendar, ES — predict the Asia / London / NY session's direction, size and trend? Walk-forward by year
// (fit on earlier years, predict the next), boosted depth-2 trees + ridge cross-check. NQ 1m, read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
using namespace lab::detail;

struct Win { double o = NAN, h = NAN, l = NAN, c = NAN; bool ok = false; };
// [t0, t1) in trading-day minutes (18:00 = 0)
static Win window(const Series& s, const Day& d, int t0, int t1) {
  Win w; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < t0 || tm >= t1) continue;
    if (!w.ok) { w.o = s.o[i]; w.h = s.h[i]; w.l = s.l[i]; w.ok = true; }
    w.h = std::max(w.h, (double)s.h[i]); w.l = std::min(w.l, (double)s.l[i]); w.c = s.c[i]; }
  return w; }

static const char* SN[3] = {"Asia 18:00-03:00", "London 03:00-09:30", "NY 09:30-16:00"};
static const std::vector<std::string> FN = {
  "prev NY move", "prev NY range", "prev NY close loc", "gap (open vs prev close)", "open vs prev NY high", "open vs prev NY low",
  "Asia move", "Asia range", "London move", "London range", "open vs overnight high", "open vs overnight low",
  "e5", "r5", "e20", "r20", "p20", "vs 50-day avg", "VIX", "VIX 1d change", "VIX/VIX3M", "weekday", "FOMC", "CPI", "NFP", "earnings",
  "week so far", "ES same pre-move"};

struct Ex { int date; int sess; std::vector<float> x; double move, absm; int up, trend; };

int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s, es; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err) || !load_raw("data/edge_lab/cache", "ES", es, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix); build_days(es, fomc, vix); load_macro("data/edge_lab/cache", s); load_events("data/edge_lab/cache", s);
  std::map<int, int> esd; for (int k = 0; k < (int)es.days.size(); ++k) if (es.days[k].ok) esd[es.days[k].date] = k;
  const int A0 = hm_to_tday(18, 0), A1 = hm_to_tday(3, 0), L1 = hm_to_tday(9, 30), N1 = hm_to_tday(16, 0);

  std::vector<Ex> ex;
  int prevk = -1; double weekOpen = NAN; int weekKey = -1;
  for (int k = 0; k < (int)s.days.size(); ++k) {
    Day& d = s.days[k];
    if (!d.ok || d.rollwin || !(d.atr > 0) || prevk < 0) { if (d.ok) prevk = k; continue; }
    const Day& p = s.days[prevk]; const double A = d.atr;
    Win as = window(s, d, A0, A1), ld = window(s, d, A1, L1), ny = window(s, d, L1, N1);
    if (!as.ok || !ld.ok || !ny.ok) { prevk = k; continue; }
    // week so far: from this ISO week's first RTH open to the prior close
    int wd = weekday_of(d.date); int wk = add_days(d.date, -((wd + 6) % 7));
    if (wk != weekKey) { weekKey = wk; weekOpen = d.ro; }
    double wsf = (wd == 1 || std::isnan(weekOpen)) ? 0 : (d.pc - weekOpen) / A;
    // ES pre-session move for each session
    double esPre[3] = {NAN, NAN, NAN};
    if (auto it = esd.find(d.date); it != esd.end()) { const Day& e = es.days[it->second]; if (e.atr > 0) {
      Win ea = window(es, e, A0, A1), el = window(es, e, A1, L1);
      if (ea.ok) esPre[0] = (ea.o - e.pc) / e.atr; if (ea.ok && el.ok) esPre[1] = (el.o - ea.o) / e.atr; if (el.ok) esPre[2] = (e.ro - e.pc) / e.atr; } }
    for (int sn = 0; sn < 3; ++sn) {
      Win w = sn == 0 ? as : sn == 1 ? ld : ny; if (!(w.h > w.l)) continue;
      double open = w.o;
      bool knowA = sn >= 1, knowL = sn >= 2;
      double onH = sn == 0 ? NAN : sn == 1 ? as.h : std::max(as.h, ld.h), onL = sn == 0 ? NAN : sn == 1 ? as.l : std::min(as.l, ld.l);
      std::vector<float> x = {
        (float)((p.rc - p.ro) / A), (float)((p.rh - p.rl) / A), (float)d.pibs, (float)((open - d.pc) / A),
        (float)((open - d.ph) / A), (float)((open - d.pl) / A),
        knowA ? (float)((as.c - as.o) / A) : NAN, knowA ? (float)((as.h - as.l) / A) : NAN,
        knowL ? (float)((ld.c - ld.o) / A) : NAN, knowL ? (float)((ld.h - ld.l) / A) : NAN,
        sn ? (float)((open - onH) / A) : NAN, sn ? (float)((open - onL) / A) : NAN,
        (float)d.e5, (float)d.r5, (float)d.e20, (float)d.r20, (float)d.p20, (float)((d.pc - d.sma50) / A),
        (float)d.vix, (float)d.vixc1, (float)d.vts, (float)wd, (float)d.fomc, (float)d.cpi, (float)d.nfp, (float)d.earn,
        (float)wsf, (float)esPre[sn]};
      double mv = (w.c - w.o) / A;
      ex.push_back({d.date, sn, x, mv, std::fabs(mv), mv > 0, std::fabs(w.c - w.o) >= 0.5 * (w.h - w.l)});
    }
    prevk = k;
  }
  printf("NQ session model — %zu session rows, %d → %d. Walk-forward by year 2014–2024 (fit on all earlier years), 2025–26 one look.\n",
         ex.size(), ex.front().date, ex.back().date);

  for (int sn = 0; sn < 3; ++sn) {
    printf("\n================ %s ================\n", SN[sn]);
    struct Pred { int date; double pm, pr, pt, ptr, move; int up, trend; double thr; };
    std::vector<Pred> P; std::vector<double> gainM(FN.size(), 0), gainT(FN.size(), 0);
    for (int Y = 2014; Y <= 2026; ++Y) {
      std::vector<std::vector<float>> X; std::vector<double> ym, yt;
      for (auto& e : ex) if (e.sess == sn && e.date / 10000 < Y && e.date / 10000 < 2025) { X.push_back(e.x); ym.push_back(e.move); yt.push_back(e.trend); }
      if (X.size() < 500) continue;
      GbmParams gp; Gbm gm = gbm_fit(X, ym, gp), gt = gbm_fit(X, yt, gp); Ridge rm = ridge_fit(X, ym), rt = ridge_fit(X, yt);
      if (Y <= 2024) for (size_t f = 0; f < FN.size(); ++f) { gainM[f] += gm.gain[f]; gainT[f] += gt.gain[f]; }
      std::vector<double> tr; for (auto& x : X) tr.push_back(std::fabs(gm.predict(x))); std::sort(tr.begin(), tr.end()); double thr = tr[tr.size() / 2];
      for (auto& e : ex) if (e.sess == sn && e.date / 10000 == Y)
        P.push_back({e.date, gm.predict(e.x), rm.predict(e.x), gt.predict(e.x), rt.predict(e.x), e.move, e.up, e.trend, thr});
    }
    // base up-rate from training years for the hit-rate comparison
    auto eraOf = [](int d) { return d < 20190101 ? 0 : d < 20250101 ? 1 : 2; };
    const char* EN[3] = {"2014-18", "2019-24", "2025-26 (holdout)"};
    for (int e = 0; e < 3; ++e) {
      int n = 0, hit = 0, hitR = 0, up = 0; double sa = 0, sb = 0, saa = 0, sbb = 0, sab = 0;
      std::vector<std::pair<double, int>> tq; std::vector<double> tradeN; double tsum = 0, tss = 0;
      for (auto& q : P) { if (eraOf(q.date) != e) continue; ++n; up += q.up; hit += (q.pm > 0) == (q.move > 0); hitR += (q.pr > 0) == (q.move > 0);
        sa += q.pm; sb += q.move; saa += q.pm * q.pm; sbb += q.move * q.move; sab += q.pm * q.move; tq.push_back({q.pt, q.trend});
        if (std::fabs(q.pm) >= q.thr) { double v = (q.pm > 0 ? 1 : -1) * q.move; tradeN.push_back(v); tsum += v; tss += v * v; } }
      if (!n) continue;
      double corr = (n * sab - sa * sb) / std::sqrt((n * saa - sa * sa) * (n * sbb - sb * sb));
      std::sort(tq.begin(), tq.end()); int q5 = n / 5, tlo = 0, thi = 0; for (int i = 0; i < q5; ++i) { tlo += tq[i].second; thi += tq[n - 1 - i].second; }
      int tn = tradeN.size(); double tm = tn ? tsum / tn : NAN, tsd = tn ? std::sqrt(std::max(0.0, tss / tn - tm * tm)) : NAN;
      printf("  %-18s n %4d | direction hit %4.1f%% (ridge %4.1f%%, always-up %4.1f%%) | corr pred/actual %+.3f | trend share: top fifth %4.1f%% vs bottom fifth %4.1f%% (all %4.1f%%) | trade top-half |pred|: %d, %+.4f ATR/trade (t %+.2f)\n",
             EN[e], n, 100.0 * hit / n, 100.0 * hitR / n, 100.0 * up / n, corr, 100.0 * thi / std::max(1, q5), 100.0 * tlo / std::max(1, q5),
             100.0 * std::count_if(tq.begin(), tq.end(), [](auto& z) { return z.second; }) / n, tn, tm, tn ? tm / (tsd / std::sqrt((double)tn)) : NAN);
    }
    std::vector<int> o(FN.size()); for (size_t f = 0; f < FN.size(); ++f) o[f] = f;
    std::sort(o.begin(), o.end(), [&](int a, int b) { return gainM[a] > gainM[b]; });
    double gs = 0; for (double g : gainM) gs += g; printf("  move model, top features by gain: ");
    for (int i = 0; i < 6; ++i) printf("%s %.0f%%%s", FN[o[i]].c_str(), 100 * gainM[o[i]] / gs, i < 5 ? ", " : "\n");
    std::sort(o.begin(), o.end(), [&](int a, int b) { return gainT[a] > gainT[b]; });
    gs = 0; for (double g : gainT) gs += g; printf("  trend model, top features by gain: ");
    for (int i = 0; i < 6; ++i) printf("%s %.0f%%%s", FN[o[i]].c_str(), 100 * gainT[o[i]] / gs, i < 5 ? ", " : "\n");
    // dump predictions for follow-ups
    char fn[96]; snprintf(fn, sizeof fn, "research/programs/out/session_model_%d.csv", sn);
    if (FILE* f = fopen(fn, "w")) { fprintf(f, "date,pred_move,ridge_move,pred_trend,ridge_trend,move,trend\n");
      for (auto& q : P) fprintf(f, "%d,%.5f,%.5f,%.5f,%.5f,%.5f,%d\n", q.date, q.pm, q.pr, q.pt, q.ptr, q.move, q.trend); fclose(f); }
  }
  return 0;
}
