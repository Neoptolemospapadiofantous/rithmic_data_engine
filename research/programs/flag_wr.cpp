// 2026-10-19 (founder: "do the math for bull flags bear flags on the 1,2,3,4,5,15 minutes … all sessions … if we don't see at least
// 40 % WR we don't consider it", target 1:3). Win rate and net result of bull and bear flags by chart, session, side and context, at
// 1R / 1.5R / 2R / 3R targets. Flag: pole N bars moving >= m ATR, F bars within 0.6 x pole, close beyond the flag; stop beyond the flag
// (+0.25); 120-min time exit, flat at session end; one position at a time per setting; cost 2 NQ pts at today's cost-to-range.
// WIN = net result > 0. Grid per chart: N 4/6/8 x m 0.10/0.20 x F 3/5 (12 settings); the table gives the MEDIAN setting and the best
// setting by 3R win rate, with both halves (2010-07 → 2018 / 2019 → 2026-10). Context: any / with the session's side of open + VWAP /
// with + straightness >= 0.55. NQ, read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <algorithm>
using namespace lab;
static const int TFS[6] = {1, 2, 3, 4, 5, 15}, NN[3] = {4, 6, 8}, FF[2] = {3, 5}; static const double MM[2] = {0.10, 0.20}, TG[4] = {1, 1.5, 2, 3};
static const int S0[3] = {0, 540, 930}, SE[3] = {535, 925, 1315}; static const char* SN[3] = {"Asia", "London", "NY"};
static const char* CN[3] = {"any", "with the day", "with + straight"}; static const double COST = 2.0 / 320.0;
struct Acc { double hit = 0, n[2] = {0, 0}, w[2] = {0, 0}, r[2] = {0, 0}, pts[2] = {0, 0}; double N() const { return n[0] + n[1]; } double WR(int h = -1) const { return h < 0 ? (w[0] + w[1]) / std::max(1.0, N()) : w[h] / std::max(1.0, n[h]); }
  double R() const { return (r[0] + r[1]) / std::max(1.0, N()); } double P() const { return (pts[0] + pts[1]) / std::max(1.0, N()); } };
struct B { double o, h, l, c; int il; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  static Acc acc[6][12][3][2][3][4];   // tf, setting, session, side(bull,bear), ctx, target
  long nDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; nDays++; const double A = d.atr; const int half = d.date >= 20190101;
    for (int se = 0; se < 3; ++se) {
      std::vector<int> idx; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0[se] && tm < SE[se]) idx.push_back(i); } if (idx.size() < 30) continue;
      std::vector<float> st(idx.size()); std::vector<signed char> D(idx.size()), V(idx.size()); double so = s.o[idx[0]], hi = -1e18, lo = 1e18, pv = 0, vv = 0;
      for (size_t k = 0; k < idx.size(); ++k) { int i = idx[k]; hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); pv += (s.h[i] + s.l[i] + s.c[i]) / 3.0 * s.v[i]; vv += s.v[i];
        double c = s.c[i], vw = vv > 0 ? pv / vv : c; st[k] = hi > lo ? std::fabs(c - so) / (hi - lo) : 0; D[k] = c > so ? 1 : c < so ? -1 : 0; V[k] = c > vw ? 1 : c < vw ? -1 : 0; }
      for (int ti = 0; ti < 6; ++ti) { const int tf = TFS[ti]; std::vector<B> b; std::vector<int> bk;   // bars + position in idx of their last minute
        for (size_t k = 0; k < idx.size(); ++k) { int i = idx[k], g = (tday_min(s.t[i]) - S0[se]) / tf;
          if (b.empty() || bk.back() != g) { b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); bk.push_back(g); }
          else { B& x = b.back(); x.h = std::max(x.h, (double)s.h[i]); x.l = std::min(x.l, (double)s.l[i]); x.c = s.c[i]; x.il = k; } }
        for (int ni = 0; ni < 3; ++ni) for (int mi = 0; mi < 2; ++mi) for (int fi = 0; fi < 2; ++fi) { const int N = NN[ni], F = FF[fi], sx = (ni * 2 + mi) * 2 + fi;
          int busy[2][3][4]; for (auto& a : busy) for (auto& c : a) for (int& x : c) x = -1;
          for (int k = N + F; k < (int)b.size(); ++k) { int p0 = k - F - N, p1 = k - F - 1; double pole = b[p1].c - b[p0].o; int fd = pole > 0 ? 1 : -1; double ap = std::fabs(pole); if (ap < MM[mi] * A) continue;
            double fh = -1e18, fl = 1e18; for (int q = k - F; q < k; ++q) { fh = std::max(fh, b[q].h); fl = std::min(fl, b[q].l); } if (fh - fl > 0.6 * ap) continue;
            double c = b[k].c; if (fd > 0 ? !(c > fh) : !(c < fl)) continue; double stop = fd > 0 ? fl - 0.25 : fh + 0.25, R = fd * (c - stop); if (R <= 0.25 || R > 0.6 * A) continue;
            int ek = b[k].il, tm = tday_min(s.t[idx[ek]]); if (tm < S0[se] + 15 || tm >= SE[se] - 30) continue;
            bool with = D[ek] == fd && V[ek] == fd; int ctxMax = !with ? 0 : st[ek] >= 0.55 ? 2 : 1; int side = fd > 0 ? 0 : 1;
            for (int q = 0; q < 4; ++q) { double tp = c + fd * TG[q] * R, out = NAN; int xk = ek;
              for (int j = ek + 1; j < (int)idx.size(); ++j) { int i = idx[j]; xk = j; if (s.t[i] > s.t[idx[ek]] + 7200) break;
                if (fd > 0 ? s.l[i] <= stop : s.h[i] >= stop) { out = -1; break; } if (fd > 0 ? s.h[i] >= tp : s.l[i] <= tp) { out = TG[q]; break; } }
              if (std::isnan(out)) out = fd * (s.c[idx[xk]] - c) / R;
              double net = out - COST * A / R;
              for (int cx = 0; cx <= ctxMax; ++cx) { int& bz = busy[side][cx][q]; if (ek <= bz) continue; bz = xk; Acc& a = acc[ti][sx][se][side][cx][q];
                a.n[half]++; if (net > 0) a.w[half]++; if (out == TG[q]) a.hit++; a.r[half] += net; a.pts[half] += net * R * 320 / A; } } } } } } }
  const double yrs = nDays / 252.0;
  printf("Bull / bear flags — NQ %ld days (%.1f yrs). WIN = net > 0 after 2 pts cost. WIN counts time exits in profit, so at 3R most wins are small — \"3R hit\" is the share that reach the full 3R. Breakeven win rate before cost: 1R 50 %%, 1.5R 40 %%, 2R 33 %%, 3R 25 %%.\n"
         "Columns per target: win %% of the MEDIAN setting (of 12) | net pts/trade of the median setting. 'best 3R' = setting with the highest 3R win rate.\n", nDays, yrs);
  double best3 = 0; char bestLab[160] = "";
  for (int se = 0; se < 3; ++se) { printf("\n==================== %s\n  %-4s %-5s %-16s %6s |", SN[se], "TF", "side", "context", "/day");
    for (double t : TG) printf("   %3.1fR win  pts  |", t); printf(" best-3R setting: win %% (2010-18 / 2019-26)  pts  /day\n");
    for (int ti = 0; ti < 6; ++ti) for (int side = 0; side < 2; ++side) for (int cx = 0; cx < 3; ++cx) {
      std::vector<double> wr[4], pt[4], nn; for (int sx = 0; sx < 12; ++sx) { if (acc[ti][sx][se][side][cx][3].N() < 100) continue; nn.push_back(acc[ti][sx][se][side][cx][3].N()); for (int q = 0; q < 4; ++q) { wr[q].push_back(acc[ti][sx][se][side][cx][q].WR()); pt[q].push_back(acc[ti][sx][se][side][cx][q].P()); } }
      if (nn.empty()) continue; auto med = [](std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; };
      printf("  %2dm  %-5s %-16s %6.2f |", TFS[ti], side ? "bear" : "bull", CN[cx], med(nn) / nDays);
      for (int q = 0; q < 4; ++q) printf("  %5.1f%% %+6.2f  |", 100 * med(wr[q]), med(pt[q]));
      { std::vector<double> h3; for (int sx = 0; sx < 12; ++sx) { const Acc& z = acc[ti][sx][se][side][cx][3]; if (z.N() >= 100) h3.push_back(z.hit / z.N()); } printf(" 3R hit %4.1f%% |", 100 * med(h3)); }
      int bs = -1; double bw = -1; for (int sx = 0; sx < 12; ++sx) { const Acc& a = acc[ti][sx][se][side][cx][3]; if (a.N() >= 100 && a.WR() > bw) { bw = a.WR(); bs = sx; } }
      const Acc& a = acc[ti][bs][se][side][cx][3];
      printf("  pole %d>=%.2f flag %d: %5.1f%% (%4.1f / %4.1f) %+6.2f %5.2f  %4.1f%%\n", NN[bs / 4], MM[bs / 2 % 2], FF[bs % 2], 100 * a.WR(), 100 * a.WR(0), 100 * a.WR(1), a.P(), a.N() / nDays, 100 * a.hit / a.N());
      if (a.WR() > best3) { best3 = a.WR(); snprintf(bestLab, sizeof bestLab, "%s %dm %s %s pole %d>=%.2f flag %d (n %.0f)", SN[se], TFS[ti], side ? "bear" : "bull", CN[cx], NN[bs / 4], MM[bs / 2 % 2], FF[bs % 2], a.N()); } } }
  printf("\nHighest 3R win rate anywhere (>= 100 trades): %.1f %% — %s\n", 100 * best3, bestLab);
  return 0;
}
