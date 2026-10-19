// 2026-10-19 EMA 9 / 14 cross event study (research/preregistered/2026-10-19_ema_9_14_cross_study.md): after a 9/14 cross on the 1, 5 or
// 15-minute chart, what does NQ do next — by session and by context. NQ 1m 2010-07 → 2026-10, moves in the day's ATR, read-only.
#include "research/meta.hpp"
#include <cstdio>
#include <random>
using namespace lab;

static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
struct Acc { double s[4] = {0}, q[4] = {0}; long n[4] = {0};
  void add(int d, double v) { if (std::isnan(v)) return; for (int e : {era(d), 3}) { s[e] += v; q[e] += v * v; n[e]++; } }
  double m(int e) const { return n[e] ? s[e] / n[e] : NAN; }
  double t(int e) const { if (n[e] < 3) return NAN; double mu = m(e), sd = std::sqrt(std::max(1e-12, q[e] / n[e] - mu * mu)); return mu / (sd / std::sqrt((double)n[e])); }
  bool consistent() const { return n[0] > 30 && n[1] > 30 && n[2] > 30 && (m(0) > 0) == (m(1) > 0) && (m(1) > 0) == (m(2) > 0) && std::fabs(t(3)) >= 2; } };
static const int H[5] = {5, 15, 30, 60, 120};
struct Cell { Acc fwd[5], net30, net60, mfe, mae, hit30, hit60, rnd60, mom60; long years = 0; };
static void print(const char* lab, const Cell& c, double yrs) {
  printf("  %-44s %6.0f/yr | +5 %+.3f +15 %+.3f +30 %+.3f +60 %+.3f +120 %+.3f | 60m net %+.3f (t %+.1f, eras %+.3f %+.3f %+.3f)%s | win60 %4.1f%% | MFE60 %.2f MAE60 %.2f\n",
         lab, c.fwd[0].n[3] / yrs, c.fwd[0].m(3), c.fwd[1].m(3), c.fwd[2].m(3), c.fwd[3].m(3), c.fwd[4].m(3), c.net60.m(3), c.net60.t(3), c.net60.m(0), c.net60.m(1), c.net60.m(2),
         c.net60.consistent() ? " CONSISTENT" : "", 100 * c.hit60.m(3), c.mfe.m(3), c.mae.m(3)); }

int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int TF[3] = {1, 5, 15}; const char* SN[3] = {"Asia", "London", "NY"};
  std::mt19937 rng(7);
  const double yrs = 16.2;
  printf("EMA 9/14 cross event study — NQ 1m %s → %s. Moves in the day's ATR, signed in the cross's direction; net = minus 1 NQ pt. CONSISTENT = same sign in all eras (2010-18 / 2019-22 / 2023-26) and |t| >= 2.\n",
         ymd_str(s.days.front().date).c_str(), ymd_str(s.days.back().date).c_str());
  for (int ti = 0; ti < 3; ++ti) {
    const int tf = TF[ti];
    // contexts: [session][ctx id]
    enum { ALL, UP, DOWN, AGREE, DISAGREE, VW_ON, VW_OFF, STRAIGHT, CHOPPY, CLEAN, SLOPE_ON, SLOPE_OFF, T_OPEN, T_MID, T_LATE, AGREE_STRAIGHT_VW, NCTX };
    const char* CN[NCTX] = {"all crosses", "up crosses", "down crosses", "cross WITH the session's move", "cross AGAINST the session's move",
      "on the VWAP side of the cross", "against VWAP", "session path straight (>= 0.55)", "choppy (>= 3 crosses in last 60 min)", "clean (no other cross in 60 min)",
      "EMA14 sloping the cross's way", "EMA14 sloping against", "NY 09:30-10:30", "NY 10:30-12:00", "NY 12:00-15:00", "with move + VWAP side + straight"};
    static Cell cell[3][NCTX]; for (auto& a : cell) for (auto& b : a) b = Cell{};
    double e9 = NAN, e14 = NAN; int lastSign = 0; std::vector<double> e14h; std::vector<int64_t> crossT;
    int curDay = -1, curSess = -1; double so = NAN, shi = 0, slo = 0, pv = 0, vv = 0;
    for (size_t i = 0; i < s.size(); ++i) {
      int di = s.day_of[i]; if (di < 0) continue; const Day& d = s.days[di]; int tm = tday_min(s.t[i]);
      int sess = tm < 540 ? 0 : tm < 930 ? 1 : tm < 1320 ? 2 : -1;
      if (di != curDay || sess != curSess) { curDay = di; curSess = sess; so = s.o[i]; shi = s.h[i]; slo = s.l[i]; pv = 0; vv = 0; }
      shi = std::max(shi, (double)s.h[i]); slo = std::min(slo, (double)s.l[i]); double tp = (s.h[i] + s.l[i] + s.c[i]) / 3; pv += tp * s.v[i]; vv += s.v[i];
      if ((tm + 1) % tf) continue;                      // not a TF close
      double c = s.c[i];
      if (std::isnan(e9)) { e9 = e14 = c; continue; }
      e9 += 2.0 / 10 * (c - e9); e14 += 2.0 / 15 * (c - e14); e14h.push_back(e14);
      int sg = e9 > e14 ? 1 : e9 < e14 ? -1 : 0; bool cross = sg && lastSign && sg != lastSign; if (sg) lastSign = sg;
      if (!cross) continue;
      int64_t now = s.t[i]; while (!crossT.empty() && crossT.front() < now - 3600) crossT.erase(crossT.begin()); int recent = crossT.size(); crossT.push_back(now);
      if (sess < 0 || !d.ok || d.rollwin || !(d.atr > 0) || (sess == 2 && tm >= 1260)) continue;
      const double A = d.atr; const int dir = sg;
      // forward moves
      double fw[5]; for (int h = 0; h < 5; ++h) fw[h] = NAN;
      double best = 0, worst = 0;
      for (size_t j = i + 1; j < s.size() && s.day_of[j] == di; ++j) { int64_t dt = s.t[j] - now; if (dt > 120 * 60) break;
        if (dt <= 60 * 60) { best = std::max(best, dir * ((dir > 0 ? s.h[j] : s.l[j]) - c)); worst = std::min(worst, dir * ((dir > 0 ? s.l[j] : s.h[j]) - c)); }
        for (int h = 0; h < 5; ++h) if (dt <= H[h] * 60) fw[h] = dir * (s.c[j] - c) / A; }
      for (int h = 0; h < 5; ++h) { // require the horizon actually reached (last bar within 2 min of it)
        size_t j = i; while (j + 1 < s.size() && s.day_of[j + 1] == di && s.t[j + 1] - now <= H[h] * 60) ++j; if (s.t[j] - now < H[h] * 60 - 120) fw[h] = NAN; }
      double vw = vv > 0 ? pv / vv : c, st = shi > slo ? std::fabs(c - so) / (shi - slo) : 0;
      double slope = e14h.size() > 3 ? dir * (e14 - e14h[e14h.size() - 4]) : 0;
      std::vector<int> ctx = {ALL, dir > 0 ? UP : DOWN, (c - so) * dir > 0 ? AGREE : DISAGREE, (c - vw) * dir > 0 ? VW_ON : VW_OFF};
      if (st >= 0.55) ctx.push_back(STRAIGHT); if (recent >= 3) ctx.push_back(CHOPPY); if (recent == 0) ctx.push_back(CLEAN);
      ctx.push_back(slope > 0 ? SLOPE_ON : SLOPE_OFF);
      if (sess == 2) ctx.push_back(tm < 990 ? T_OPEN : tm < 1080 ? T_MID : T_LATE);
      if ((c - so) * dir > 0 && (c - vw) * dir > 0 && st >= 0.55) ctx.push_back(AGREE_STRAIGHT_VW);
      int rd = (rng() & 1) ? 1 : -1, md = (s.c[i] >= s.o[i] - 0) ? 1 : -1;
      for (int k : ctx) { Cell& x = cell[sess][k]; for (int h = 0; h < 5; ++h) x.fwd[h].add(d.date, fw[h]);
        x.net30.add(d.date, std::isnan(fw[2]) ? NAN : fw[2] - 1.0 / A); x.net60.add(d.date, std::isnan(fw[3]) ? NAN : fw[3] - 1.0 / A);
        x.mfe.add(d.date, best / A); x.mae.add(d.date, worst / A); x.hit60.add(d.date, std::isnan(fw[3]) ? NAN : fw[3] > 0);
        x.rnd60.add(d.date, std::isnan(fw[3]) ? NAN : rd * dir * fw[3]); x.mom60.add(d.date, std::isnan(fw[3]) ? NAN : md * dir * fw[3]); }
    }
    for (int se = 0; se < 3; ++se) {
      printf("\n== %d-min chart, %s session\n", tf, SN[se]);
      printf("  baselines (60 min, ATR): random direction %+.3f | last bar's direction %+.3f\n", cell[se][ALL].rnd60.m(3), cell[se][ALL].mom60.m(3));
      for (int k = 0; k < NCTX; ++k) { if (cell[se][k].fwd[0].n[3] < 50) continue; print(CN[k], cell[se][k], yrs); }
    }
  }
  return 0;
}
