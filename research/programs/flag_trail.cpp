// 2026-10-19 (founder: "possible trail?" after flag_wr showed a full 1:3 is reached ≤ ~22 % of the time). Same flags as flag_wr.cpp
// (12 settings per chart, 1/2/3/4/5/15 min, all sessions, bull and bear, context any / with the day / with + straight) under exits:
//  X0 3R target, 120-min time exit (flag_wr reference)        X1 3R target, held to session end
//  X2 break-even at +1R, then 3R target, to session end       X3 trail 1R behind the best close once +1R, no target, to session end
//  X4 trail at the previous bar's low (bull) / high (bear) on the flag's chart once +1R, no target, to session end
// Per row: median setting's win % (net > 0), share of trades ending >= 3R, and net pts/trade at today's range (2 pts cost).
#include "research/meta.hpp"
#include <cstdio>
#include <algorithm>
using namespace lab;
static const int TFS[6] = {1, 2, 3, 4, 5, 15}, NN[3] = {4, 6, 8}, FF[2] = {3, 5}; static const double MM[2] = {0.10, 0.20};
static const int S0[3] = {0, 540, 930}, SE[3] = {535, 925, 1315}; static const char* SN[3] = {"Asia", "London", "NY"};
static const char* CN[3] = {"any", "with the day", "with + straight"}; static const double COST = 2.0 / 320.0; enum { NX = 5 };
struct Acc { double n[2] = {0, 0}, w = 0, big = 0, pts[2] = {0, 0}; double N() const { return n[0] + n[1]; } };
struct B { double o, h, l, c; int il; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  static Acc acc[6][12][3][2][3][NX]; long nDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; nDays++; const double A = d.atr; const int half = d.date >= 20190101;
    for (int se = 0; se < 3; ++se) {
      std::vector<int> idx; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) idx.push_back(i); } if (idx.size() < 30) continue;
      std::vector<float> st(idx.size()); std::vector<signed char> D(idx.size()), V(idx.size()); double so = s.o[idx[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < idx.size(); ++k) { int i = idx[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; st[k] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; D[k] = c > so ? 1 : c < so ? -1 : 0; V[k] = c > vw ? 1 : c < vw ? -1 : 0; }
      for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<B> b; std::vector<int> bk, barOf(idx.size());
        for (size_t k = 0; k < idx.size(); ++k) { int i = idx[k], gi = (tday_min(s.t[i]) - S0[se]) / tf;
          if (b.empty() || bk.back() != gi) { b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); bk.push_back(gi); }
          else { B& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.il = k; } barOf[k] = b.size() - 1; }
        for (int ni = 0; ni < 3; ++ni) for (int mi = 0; mi < 2; ++mi) for (int fi = 0; fi < 2; ++fi) { const int N = NN[ni], F = FF[fi], sx = (ni * 2 + mi) * 2 + fi;
          int busy[2][3][NX]; for (auto& a : busy) for (auto& c : a) for (int& x : c) x = -1;
          for (int k = N + F; k < (int)b.size(); ++k) { int p0 = k - F - N, p1 = k - F - 1; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < MM[mi] * A) continue;
            double fh = -1e18, fl = 1e18; for (int q = k - F; q < k; ++q) { fh = std::max(fh, b[q].h); fl = std::min(fl, b[q].l); } if (fh - fl > 0.6 * ap) continue;
            double c = b[k].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue; double stop0 = fd > 0 ? fl - 0.25 : fh + 0.25, R = fd * (c - stop0); if (R <= 0.25 || R > 0.6 * A) continue;
            int ek = b[k].il, tm = tday_min(s.t[idx[ek]]); if (tm < S0[se] + 15 || tm >= SE[se] - 30) continue;
            bool with = D[ek] == fd && V[ek] == fd; int ctxMax = !with ? 0 : st[ek] >= 0.55 ? 2 : 1; int side = fd > 0 ? 0 : 1;
            for (int x = 0; x < NX; ++x) { double stop = stop0, tp = x <= 2 ? c + fd * 3 * R : NAN, best = c, out = NAN; int xk = ek; bool armed = false;
              for (int j = ek + 1; j < (int)idx.size(); ++j) { int i = idx[j]; xk = j; if (x == 0 && s.t[i] > s.t[idx[ek]] + 7200) break;
                if (fd > 0 ? s.l[i] <= stop : s.h[i] >= stop) { out = fd * (stop - c) / R; break; }
                if (!std::isnan(tp) && (fd > 0 ? s.h[i] >= tp : s.l[i] <= tp)) { out = 3; break; }
                if (fd * (s.c[i] - c) >= R) armed = true;   // decisions on minute closes
                best = fd > 0 ? std::max(best, (double)s.c[i]) : std::min(best, (double)s.c[i]);
                if (armed && x == 2) stop = fd > 0 ? std::max(stop, c) : std::min(stop, c);
                if (armed && x == 3) { double ns = best - fd * R; stop = fd > 0 ? std::max(stop, ns) : std::min(stop, ns); }
                if (armed && x == 4 && barOf[j] > k && j == b[barOf[j]].il) { double ns = fd > 0 ? b[barOf[j]].l - 0.25 : b[barOf[j]].h + 0.25; stop = fd > 0 ? std::max(stop, ns) : std::min(stop, ns); } }
              if (std::isnan(out)) out = fd * (s.c[idx[xk]] - c) / R;
              double net = out - COST * A / R;
              for (int cx = 0; cx <= ctxMax; ++cx) { int& bz = busy[side][cx][x]; if (ek <= bz) continue; bz = xk; Acc& a = acc[ti][sx][se][side][cx][x];
                a.n[half]++; if (net > 0) a.w++; if (out >= 3) a.big++; a.pts[half] += net * R * 320 / A; } } } } } } }
  printf("Flags with trailing exits — NQ %ld days. Per exit: median setting's win %% | share ending >= 3R | net pts/trade (2 pts cost, today's range).\n"
         "X0 3R + 120-min time exit · X1 3R to session end · X2 break-even at +1R then 3R · X3 trail 1R behind the best close after +1R · X4 trail previous bar's low/high after +1R\n", nDays);
  auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v.empty() ? NAN : v[v.size() / 2]; };
  for (int se = 0; se < 3; ++se) { printf("\n==================== %s\n  %-4s %-5s %-16s |", SN[se], "TF", "side", "context"); for (int x = 0; x < NX; ++x) printf("  X%d win  >=3R   pts |", x); printf("  pts 2010-18 / 2019-26 (best exit)\n");
    for (int ti = 0; ti < 6; ++ti) for (int side = 0; side < 2; ++side) for (int cx = 0; cx < 3; cx += 2) { printf("  %2dm  %-5s %-16s |", TFS[ti], side ? "bear" : "bull", CN[cx]); double bp = -1e9; int bx = 0; std::vector<double> h0, h1;
      for (int x = 0; x < NX; ++x) { std::vector<double> w, bg, p; for (int sx = 0; sx < 12; ++sx) { const Acc& a = acc[ti][sx][se][side][cx][x]; if (a.N() < 100) continue; w.push_back(a.w / a.N()); bg.push_back(a.big / a.N()); p.push_back((a.pts[0] + a.pts[1]) / a.N()); }
        printf(" %5.1f%% %5.1f%% %+6.2f |", 100 * med(w), 100 * med(bg), med(p)); if (med(p) > bp) { bp = med(p); bx = x; } }
      std::vector<double> e0, e1; for (int sx = 0; sx < 12; ++sx) { const Acc& a = acc[ti][sx][se][side][cx][bx]; if (a.N() < 100) continue; e0.push_back(a.pts[0] / std::max(1.0, a.n[0])); e1.push_back(a.pts[1] / std::max(1.0, a.n[1])); }
      printf("  X%d: %+6.2f / %+6.2f\n", bx, med(e0), med(e1)); } }
  return 0;
}
