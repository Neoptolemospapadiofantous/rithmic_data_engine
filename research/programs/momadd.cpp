// 2026-10-17 momentum add-ons (research/preregistered/2026-10-17_momentum_addons.md).
#include "research/bars.hpp"
#include <array>
#include <cstdio>
using namespace lab;
constexpr int ND = 1380, R0 = 930; constexpr double COST = 1.6;
struct Tr { int date; double r, r_half, vix; int flow_all, flow15, plan; };   // flow / plan: +1 agrees, −1 disagrees, 0 unknown
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) return 1; build_days(s, fomc, vix);
  std::array<std::vector<Tr>, 2> T;
  std::vector<double> o(ND), h(ND), l(ND), c(ND), v(ND), bv(ND), vw(ND);
  for (auto& D : s.days) {
    if (!D.ok || D.rollwin || !(D.atr > 0) || std::isnan(D.pc) || D.date > 20241231) continue;
    { int j = D.b0; double last = s.o[D.b0]; for (int m = 0; m < ND; ++m) { while (j < D.b1 && tday_min(s.t[j]) < m) ++j;
        if (j < D.b1 && tday_min(s.t[j]) == m) { o[m] = s.o[j]; h[m] = s.h[j]; l[m] = s.l[j]; c[m] = s.c[j]; v[m] = s.v[j]; bv[m] = s.bv[j]; } else { o[m] = h[m] = l[m] = c[m] = last; v[m] = bv[m] = 0; } last = c[m]; } }
    { double pv = 0, vv = 0; for (int m = R0; m < ND; ++m) { const double w = std::max(1.0, v[m]); pv += (h[m] + l[m] + c[m]) / 3 * w; vv += w; vw[m] = pv / vv; } }
    const double op = o[R0], atr = D.atr;
    const int NB = ND / 5; std::vector<double> BH(NB), BL(NB), BC(NB); for (int k = 0; k < NB; ++k) { BH[k] = -1e18; BL[k] = 1e18; for (int m = 5 * k; m < 5 * k + 5; ++m) { BH[k] = std::max(BH[k], h[m]); BL[k] = std::min(BL[k], l[m]); } BC[k] = c[5 * k + 4]; }
    std::vector<double> E20(NB); { double x = BC[0]; for (int k = 0; k < NB; ++k) { x += 2.0 / 21 * (BC[k] - x); E20[k] = x; } }
    int pwhen = 1 << 30, pd = 0; { const int CM[4] = {20, 60, 90, 120}; const double CK[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { const double mv = c[R0 + CM[q] - 1] - op; if (std::fabs(mv) >= CK[q] * atr) { pwhen = R0 + CM[q]; pd = mv > 0 ? 1 : -1; break; } }
      const double gap = (op - D.pc) / atr; const int g = gap >= 0.1 ? 1 : gap <= -0.1 ? -1 : 0;
      if (g) { double orh = -1e18, orl = 1e18; for (int m = R0; m < R0 + 15; ++m) { orh = std::max(orh, h[m]); orl = std::min(orl, l[m]); }
        for (int k = 3; 5 * k + 5 <= 265; ++k) { const int kk = R0 / 5 + k; if (g > 0 ? (BL[kk] <= orh && BC[kk] > orh) : (BH[kk] >= orl && BC[kk] < orl)) { if (R0 + 5 * k + 5 < pwhen) { pwhen = R0 + 5 * k + 5; pd = g; } break; } } } }
    const bool flowday = D.flow && D.date >= 20230301;
    for (int rule = 0; rule < 2; ++rule) { const int W = rule == 0 ? 45 : 30; int busy = 0, ntr = 0; bool lastwin = true; const double st = 0.25 * atr;
      for (int k = (R0 + 15) / 5 - 1; k < 1195 / 5; ++k) { const int end = 5 * k + 5; if (ntr >= 3 || !lastwin) break; if (5 * k < busy) continue;
        const int mm = end - 1; double path = 0; for (int q = mm - W + 1; q <= mm; ++q) path += std::fabs(c[q] - c[q - 1]); const double net = c[mm] - c[mm - W];
        if (path <= 0 || std::fabs(net) / path < 0.3) continue; const int d = net > 0 ? 1 : -1; const double C = BC[k];
        if (d * (C - vw[mm]) <= 0 || d * (C - op) <= 0) continue;
        if (rule == 0) { const double vb = vw[5 * k - 1]; if (!(d > 0 ? (BL[k] <= vb + 0.05 * atr && C > vb) : (BH[k] >= vb - 0.05 * atr && C < vb))) continue; }
        else { const double ep = E20[k - 1]; if (!(d > 0 ? (BL[k] <= ep + 0.03 * atr && C > ep) : (BH[k] >= ep - 0.03 * atr && C < ep))) continue; }
        // full position: BE at +1R, hold; half-out: half at +1R, rest BE hold
        double stop = C - d * st, best = C; bool be = false; double pts = NAN; int xm = R0 + 385; bool hit1r = false;
        for (int m = end; m < R0 + 385; ++m) { if (d > 0 ? l[m] <= stop : h[m] >= stop) { pts = d * (stop - C); xm = m + 1; break; }
          best = d > 0 ? std::max(best, h[m]) : std::min(best, l[m]); if (!be && d * (best - C) >= st) { be = true; hit1r = true; stop = C + d * COST; } }
        if (std::isnan(pts)) pts = d * (c[R0 + 384] - C);
        const double full = (pts - COST) / st, half = hit1r ? (0.5 * st + 0.5 * pts - COST) / st : full;
        double cd = 0, c15 = 0; for (int m = R0; m < end; ++m) { cd += 2 * bv[m] - v[m]; if (m >= end - 15) c15 += 2 * bv[m] - v[m]; }
        T[rule].push_back({D.date, full, half, D.vix, flowday ? (d * cd > 0 ? 1 : -1) : 0, flowday ? (d * c15 > 0 ? 1 : -1) : 0, (pd && end >= pwhen) ? (pd == d ? 1 : -1) : 0});
        busy = xm; ++ntr; lastwin = pts - COST > 0; } } }
  const char* RN[2] = {"A mom_dir_45_vwap", "B mom_dir_30_ema20"};
  auto era = [](int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; };
  for (int rule = 0; rule < 2; ++rule) { auto& v = T[rule]; printf("\n===== %s (%zu trades 2010-24) =====\n", RN[rule], v.size());
    auto grp = [&](const char* nm, auto pred) { double r = 0; int n = 0; for (auto& t : v) if (pred(t)) { r += t.r; ++n; } printf("  %-36s n %4d  %+.3f R/trade\n", nm, n, n ? r / n : NAN); };
    printf(" 1. order flow (2023-03 → 2024-12):\n");
    grp("RTH delta agrees", [](const Tr& t) { return t.flow_all > 0; }); grp("RTH delta disagrees", [](const Tr& t) { return t.flow_all < 0; });
    grp("last-15-min delta agrees", [](const Tr& t) { return t.flow15 > 0; }); grp("last-15-min delta disagrees", [](const Tr& t) { return t.flow15 < 0; });
    printf(" 2. VIX (prior close):\n");
    grp("VIX < 16", [](const Tr& t) { return t.vix < 16; }); grp("VIX 16-22", [](const Tr& t) { return t.vix >= 16 && t.vix < 22; }); grp("VIX >= 22", [](const Tr& t) { return t.vix >= 22; });
    printf(" 4. cascade / plan direction at entry:\n");
    grp("plan agrees", [](const Tr& t) { return t.plan > 0; }); grp("plan disagrees", [](const Tr& t) { return t.plan < 0; }); grp("no plan direction yet", [](const Tr& t) { return t.plan == 0; });
    // policies: units per trade; total R, max drawdown (R), ratio, by era
    auto policy = [&](const char* nm, auto units, bool halfout) { printf("  %-44s", nm); for (int e = 0; e < 4; ++e) { double eq = 0, pk = 0, dd = 0, risk = 0; int n = 0;
        for (auto& t : v) { if (e < 3 && era(t.date) != e) continue; const double u = units(t); if (u <= 0) continue; eq += u * (halfout ? t.r_half : t.r); risk += u; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); ++n; }
        printf(" | %s %+6.1fR dd %5.1f ret/dd %5.2f R/unit %+.3f", e == 3 ? "ALL" : (e == 0 ? "10-18" : e == 1 ? "19-22" : "23-24"), eq, dd, dd > 0 ? eq / dd : NAN, risk > 0 ? eq / risk : NAN); } printf("\n"); };
    printf(" policies (total R, worst drawdown, return/drawdown, R per unit risked):\n");
    policy("flat 1 unit", [](const Tr&) { return 1.0; }, false);
    policy("2 units when VIX >= 20", [](const Tr& t) { return t.vix >= 20 ? 2.0 : 1.0; }, false);
    policy("2 units when the plan agrees", [](const Tr& t) { return t.plan > 0 ? 2.0 : 1.0; }, false);
    policy("only when the plan agrees", [](const Tr& t) { return t.plan > 0 ? 1.0 : 0.0; }, false);
    policy("3. scale out: half at +1R, rest BE hold", [](const Tr&) { return 1.0; }, true); }
}
