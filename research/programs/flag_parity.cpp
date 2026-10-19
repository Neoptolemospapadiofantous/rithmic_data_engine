// 2026-10-20 (founder: "lets validate again the bull flags that everything is correct"): parity between the research detector
// (the C++ used by flag_inner / flag_take_opt / flag_turn) and the dashboard /flags page (bot/frontend/src/lib/flags.ts findFlags()).
// For a fixed sample of days this writes ONE JSON file with, per day: the RTH 1-minute bars (wall-clock epoch, the research cache),
// the context the page needs (prior RTH high / low / close, overnight high / low, ATR), and the research detector's flags (both
// sides, 3/4/5m, page rules) with every field the page shows: pole start / top / H / L, registration bar, pullback #, broke a level,
// pole ×bar, gap with it, status (the page's definition), pole-top break minute, 2m trigger minute / entry / stop, TAKE.
// scripts/flag_parity.sh runs the page's TypeScript on the SAME bars and context and compares field by field.
// Usage: flag_parity OUT.json [N_RANDOM_DAYS] — the last 10 cache days are always included. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: flag_parity OUT.json [N]\n"); return 2; }
  const int NR = argc > 2 ? atoi(argv[2]) : 60;
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::vector<const Day*> ok; for (auto& d : s.days) if (d.ok && !d.rollwin && d.atr > 0 && !std::isnan(d.pc) && d.date <= 20261016) ok.push_back(&d);
  std::vector<const Day*> pick; std::mt19937 rng(20261020); std::set<int> used;
  for (int i = 0; i < NR && i < (int)ok.size(); ++i) { const Day* d = ok[rng() % ok.size()]; if (used.insert(d->date).second) pick.push_back(d); }
  for (size_t i = ok.size() >= 10 ? ok.size() - 10 : 0; i < ok.size(); ++i) if (used.insert(ok[i]->date).second) pick.push_back(ok[i]);
  std::sort(pick.begin(), pick.end(), [](auto a, auto b) { return a->date < b->date; });
  FILE* fo = fopen(argv[1], "w"); if (!fo) { perror(argv[1]); return 1; }
  fprintf(fo, "[\n"); long nFlags = 0; bool firstDay = true;
  for (const Day* dp : pick) { const Day& d = *dp;
    std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); } if (I.size() < 370) continue;
    const size_t n = I.size(); const double A = d.atr;
    double onh = -1e18, onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { if (tday_min(s.t[i]) >= 930) break; onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < 960; ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
    fprintf(fo, "%s{\"date\":%d,\"t930\":%lld,\"ctx\":{\"prior\":{\"h\":%.2f,\"l\":%.2f,\"c\":%.2f},\"overnight\":{\"hi\":%.2f,\"lo\":%.2f},\"atr\":%.6f},\"m1\":[",
            firstDay ? "" : ",\n", d.date, (long long)s.t[I[0]], d.ph, d.pl, d.pc, onh, onl, A); firstDay = false;
    for (size_t k = 0; k < n; ++k) { int i = I[k]; fprintf(fo, "%s[%lld,%.2f,%.2f,%.2f,%.2f,%.0f]", k ? "," : "", (long long)s.t[i], s.o[i], s.h[i], s.l[i], s.c[i], (double)s.v[i]); }
    fprintf(fo, "],\"flags\":["); bool firstF = true;
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> strict(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      const double so = O[0];
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
          const double vw = vv > 0 ? pv / vv : Cc[k]; strict[k] = Cc[k] > so && Cc[k] > vw && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
      const double gapA = (so - side * d.pc) / A;
      for (int tf = 3; tf <= 5; ++tf) {
        std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - 930) / tf != (TM[b.back().k0] - 930) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } }
        int busy = -1; double lod = 1e18; int lodBar = 0; std::vector<int> regBars;
        for (int q = 0; q < (int)b.size(); ++q) { if (b[q].l < lod) { lod = b[q].l; lodBar = q; } if (q < 6 || q <= busy) continue;
          const int minute = (TM[b[q].k0] - 930) + tf; if (minute < 15 || minute >= 270) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool okp = true; for (int r = l; r <= h; ++r) if (b[r].l < L) okp = false; if (!okp) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 4 * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          if (!strict[b[q].k]) continue;
          int pb = 1; for (int rb : regBars) if (rb > lodBar) pb++; regBars.push_back(q);
          std::vector<double> lv = {d.ph, d.pl, onh, onl}; if (TM[b[q].k] + 1 >= 960) { lv.push_back(orh); lv.push_back(orl); }
          bool broke = false;
          for (double x : lv) { const double m = side * x; if (m > L && m <= H) broke = true; }
          if (std::floor(H / 100) * 100 > L) broke = true;   // the page's round-100 test, in mirrored units
          // the page's follow: minute by minute from the first minute after bar q
          std::string status = "forming"; int entryT = -1, nb = 0; const size_t kq = b[q].k; size_t kk = kq + 1;
          for (; kk < n; ++kk) { if (Hh[kk] > H) { status = "triggered"; entryT = kk; break; }
            if ((TM[kk] - 930) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M) { status = "failed"; break; } if (nb >= 12) { status = "expired"; break; } } }
          // research 2m trigger (flag_take_opt / flag_review2): 2m closes after the registration minute, until 15:00 or the flag ends
          int k2m = -1; double e2m = NAN, s2m = NAN; { double flo = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false; int nb2 = 0;
            for (size_t k = kq + 1; k < n; ++k) { const int t2 = TM[k]; if (t2 >= 1260) break; b2h = std::max(b2h, Hh[k]); hbl = std::min(hbl, Lw[k]);
              if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[k]; if (!std::isnan(p2h) && b2h <= H && k2m < 0) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(flo, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = k; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
              if ((t2 + 1 - 930) % tf == 0) { nb2++; flo = std::min(flo, hbl); hbl = 1e18; if (Hh[k] > H || Cc[k] < H - 0.5 * M || nb2 >= 12) break; } } }
          const bool take = side > 0 && tf >= 4 && pb == 1 && broke && M >= 5 * ar && gapA > 0.1;
          fprintf(fo, "%s{\"side\":%d,\"tf\":%d,\"poleStart\":%lld,\"top\":%lld,\"reg\":%lld,\"H\":%.2f,\"L\":%.2f,\"poleX\":%.4f,\"pbNo\":%d,\"broke\":%s,\"status\":\"%s\",\"entryTime\":%lld,\"t2Time\":%lld,\"t2Entry\":%.2f,\"t2Stop\":%.2f,\"take\":%s}",
                  firstF ? "" : ",", side, tf, (long long)s.t[I[b[l].k0]], (long long)s.t[I[b[h].k0]], (long long)s.t[I[b[q].k0]], side * H, side * L, M / ar, pb, broke ? "true" : "false", status.c_str(),
                  entryT >= 0 ? (long long)s.t[I[entryT]] : -1LL, k2m >= 0 ? (long long)s.t[I[k2m]] - 60 : -1LL, k2m >= 0 ? side * e2m : 0.0, k2m >= 0 ? side * s2m : 0.0, take ? "true" : "false");
          firstF = false; nFlags++; busy = q + nb; } } }
    fprintf(fo, "]}"); }
  fprintf(fo, "\n]\n"); fclose(fo);
  printf("flag_parity: %zu days, %ld research flags (3/4/5m, both sides) → %s\n", pick.size(), nFlags, argv[1]);
  return 0;
}
