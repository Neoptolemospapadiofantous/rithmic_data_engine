// 2026-10-22 orb_flag.cpp (founder: "test orb with a flag but show me the trades so I see what flags are you taking", "lets create a system of
// identifying flags and bear flags"). The opening range sets the direction, the flag gives the entry:
//   ORB    5-minute range 09:30-09:35 (as live) and 15-minute range 09:30-09:45; the break = the first 1-minute close beyond it, before 12:00
//   FLAGS  flag_hhhl.cpp export (flag_tiny_tp.cpp's 3-5m bull / bear flags, registered 09:45-14:00; flag-mid limit entry, stop flag low − 0.1 ATR,
//          target the measured move, flat 15:55, cost 1.5 pts), one position at a time across 3-5m
// Rules, fixed before the run:
//   F0 every flag (no ORB)            · F1 flags on the break side registered after the break · F2 only the FIRST such flag of the day
//   F3 flags AGAINST the break side    · F4 F1 + A-list (walk-forward top third)                 · F5 F1 + 5m HH/HL (LH/LL)
//   ORB the plain break trade for reference: enter at the break close, stop the other side of the range, out 15:55
// Years 2014-26 (grades exist from 2014). Points at today's ATR. Writes TRADES.json (F1 on the 5-minute ORB, newest first, with the flag's own
// 3-5m bars) for the page. Usage: orb_flag FLAGS.csv [TRADES.json [MAX]]. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct F { int date, side, tf, reg, end, pole, grade, hh5; double plo, phi, flo; int ok, em, xm; double e, st, tg, x, r, pts; };
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: orb_flag FLAGS.csv [TRADES.json [MAX]]\n"); return 1; }
  const double cost = 1.5; const int MAXJ = argc > 3 ? atoi(argv[3]) : 150;
  std::map<int, std::vector<F>> fl;
  { std::ifstream f(argv[1]); std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { F z; if (sscanf(ln.c_str(), "%d,%d,%d,%d,%d,%d,%d,%d,%lf,%lf,%lf,%d,%d,%d,%lf,%lf,%lf,%lf,%lf,%lf", &z.date, &z.side, &z.tf, &z.reg, &z.end, &z.pole, &z.grade, &z.hh5,
                             &z.plo, &z.phi, &z.flo, &z.ok, &z.em, &z.xm, &z.e, &z.st, &z.tg, &z.x, &z.r, &z.pts) == 20 && z.ok && z.date >= 20140101) fl[z.date].push_back(z); } }
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { int date; double atr; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::vector<DB> days;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z{d.date, d.atr, {}, {}, {}, {}, {}};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); }
    if (z.O.size() >= 370) days.push_back(std::move(z)); }
  const double Anow = days.back().atr; const long nd = days.size();
  struct Orb { int dir = 0, brk = 0; double hi = 0, lo = 0, be = 0; size_t kb = 0; };
  auto orb = [](const DB& z, int len) { Orb o; o.hi = -1e18; o.lo = 1e18; size_t k = 0; for (; k < z.C.size() && z.TM[k] < 930 + len; ++k) { o.hi = std::max(o.hi, z.H[k]); o.lo = std::min(o.lo, z.L[k]); }
    for (; k < z.C.size() && z.TM[k] < 1200; ++k) if (z.C[k] > o.hi || z.C[k] < o.lo) { o.dir = z.C[k] > o.hi ? 1 : -1; o.brk = z.TM[k] + 1; o.be = z.C[k]; o.kb = k; break; } return o; };
  auto per = [](int d) { const int y = d / 10000; return y <= 2017 ? 0 : y <= 2021 ? 1 : y <= 2024 ? 2 : 3; };
  printf("ORB first, then the flag — NQ, %ld NY days 2014-01 → 2026-10. Flags: 3-5m bull / bear (flag_tiny_tp rules), flag-mid limit, stop flag low − 0.1 ATR, measured-move target, flat 15:55, cost %.1f pts,\n", nd, cost);
  printf("one position at a time. Points at today's ATR (%.0f). Cells: 2014-17 | 2018-21 | 2022-24 | 2025-26 avg R (n) · all: R, win %%, PF, pts/trade, trades a year, worst drawdown R, periods > 0.\n", Anow);
  std::vector<std::pair<const F*, Orb>> jsonT;
  for (int len : {5, 15}) {
    std::vector<Orb> ob(days.size()); for (size_t i = 0; i < days.size(); ++i) ob[i] = orb(days[i], len);
    int nb = 0; for (auto& o : ob) nb += o.dir != 0;
    printf("\n######## %d-minute opening range — breaks on %.0f%% of days\n", len, 100.0 * nb / nd);
    // ORB reference
    { double m[4] = {0}, cn[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, pts = 0, cum = 0, pk = 0, dd = 0;
      for (size_t i = 0; i < days.size(); ++i) { const Orb& o = ob[i]; if (!o.dir) continue; const DB& z = days[i]; const double st = o.dir > 0 ? o.lo : o.hi, risk = std::fabs(o.be - st); if (risk < 1) continue;
        double g = NAN; for (size_t k = o.kb + 1; k < z.C.size(); ++k) { if (z.TM[k] >= 1315) { g = o.dir * (z.O[k] - o.be); break; } if ((o.dir > 0 ? z.L[k] <= st : z.H[k] >= st)) { g = -risk; break; } }
        if (std::isnan(g)) g = o.dir * (z.C.back() - o.be); const double x = (g - cost) / risk; const int e = per(z.date);
        m[e] += x; cn[e]++; sum += x; N++; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); pts += g * Anow / z.atr - cost; cum += x; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); }
      int pos = 0; for (int e = 0; e < 4; ++e) pos += m[e] > 0;
      printf("    %-46s", "ORB break trade alone (stop other side)"); for (int e = 0; e < 4; ++e) printf(" %+6.2f (%4.0f) |", m[e] / std::max(1.0, cn[e]), cn[e]);
      printf(" all %+6.3fR win %4.1f%% PF %.2f · %+6.1f pts · %5.1f/yr · DD %5.1fR · %d/4\n", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, pts / N, N / nd * 252.0, dd, pos); }
    auto run = [&](const char* name, std::function<bool(const F&, const Orb&, bool firstAfter)> sel, bool keep) {
      double m[4] = {0}, cn[4] = {0}, sum = 0, N = 0, w = 0, gw = 0, gl = 0, pts = 0, cum = 0, pk = 0, dd = 0, nbull = 0;
      for (size_t i = 0; i < days.size(); ++i) { auto it = fl.find(days[i].date); if (it == fl.end()) continue; std::vector<const F*> v; for (auto& f : it->second) v.push_back(&f);
        std::sort(v.begin(), v.end(), [](const F* a, const F* b) { return a->em != b->em ? a->em < b->em : a->tf < b->tf; });
        const Orb& o = ob[i]; int busy = -1; bool seen = false;
        for (const F* f : v) { const bool after = o.dir && f->reg >= o.brk && f->side == o.dir; const bool first = after && !seen; if (after) seen = true;
          if (!sel(*f, o, first) || f->em <= busy) continue; busy = f->xm; const double x = f->r; const int e = per(f->date);
          m[e] += x; cn[e]++; sum += x; N++; w += x > 0; (x > 0 ? gw : gl) += std::fabs(x); pts += (f->pts + cost) * Anow / days[i].atr - cost; cum += x; pk = std::max(pk, cum); dd = std::max(dd, pk - cum); nbull += f->side > 0;
          if (keep) jsonT.push_back({f, o}); } }
      if (N < 20) { printf("    %-46s fewer than 20 trades (%.0f)\n", name, N); return; }
      int pos = 0, have = 0; for (int e = 0; e < 4; ++e) if (cn[e] >= 10) { have++; pos += m[e] > 0; }
      printf("    %-46s", name); for (int e = 0; e < 4; ++e) { if (cn[e] < 10) printf("   —    (%4.0f) |", cn[e]); else printf(" %+6.2f (%4.0f) |", m[e] / cn[e], cn[e]); }
      printf(" all %+6.3fR win %4.1f%% PF %.2f · %+6.1f pts · %5.1f/yr · DD %5.1fR · %d/%d · %.0f%% bull\n", sum / N, 100 * w / N, gl > 0 ? gw / gl : 99.0, pts / N, N / nd * 252.0, dd, pos, have, 100 * nbull / N); };
    auto side = [](const F& f, const Orb& o) { return o.dir && f.reg >= o.brk && f.side == o.dir; };
    run("F0 every flag (no ORB)", [](const F&, const Orb&, bool) { return true; }, false);
    run("F1 flag on the break side, after the break", [&](const F& f, const Orb& o, bool) { return side(f, o); }, len == 5);
    run("F2 only the first flag after the break", [&](const F& f, const Orb& o, bool first) { return side(f, o) && first; }, false);
    run("F3 flag AGAINST the break side", [&](const F& f, const Orb& o, bool) { return o.dir && f.reg >= o.brk && f.side == -o.dir; }, false);
    run("F4 F1 + A-list", [&](const F& f, const Orb& o, bool) { return side(f, o) && f.grade == 1; }, false);
    run("F5 F1 + 5m HH/HL (LH/LL)", [&](const F& f, const Orb& o, bool) { return side(f, o) && f.hh5; }, false);
    run("F6 F1 + A-list + 5m HH/HL", [&](const F& f, const Orb& o, bool) { return side(f, o) && f.grade == 1 && f.hh5; }, false);
    for (int sd : {1, -1}) run(sd > 0 ? "   F1 bull only (break up)" : "   F1 bear only (break down)", [&, sd](const F& f, const Orb& o, bool) { return side(f, o) && f.side == sd; }, false); }
  if (argc > 2) { FILE* fo = fopen(argv[2], "w"); if (!fo) { perror(argv[2]); return 1; }
    std::map<int, size_t> di; for (size_t i = 0; i < days.size(); ++i) di[days[i].date] = i;
    std::sort(jsonT.begin(), jsonT.end(), [](auto& a, auto& b) { return a.first->date != b.first->date ? a.first->date > b.first->date : a.first->em > b.first->em; });
    fprintf(fo, "[\n"); int nw = 0;
    for (auto& [f, o] : jsonT) { if (nw >= MAXJ) break; const DB& z = days[di[f->date]];
      const char* why = std::fabs(f->x - f->tg) < 0.01 ? "target" : (f->side > 0 ? f->x <= f->st + 0.01 : f->x >= f->st - 0.01) ? "stop" : "15:55";
      fprintf(fo, "%s{\"date\":%d,\"side\":%d,\"tf\":%d,\"grade\":%d,\"hh5\":%d,\"reg\":%d,\"end\":%d,\"pole\":%d,\"plo\":%.2f,\"phi\":%.2f,\"flo\":%.2f,\"em\":%d,\"xm\":%d,\"e\":%.2f,\"st\":%.2f,\"tg\":%.2f,\"x\":%.2f,\"r\":%.3f,\"pts\":%.2f,\"why\":\"%s\",\"orbHi\":%.2f,\"orbLo\":%.2f,\"brk\":%d,\"bars\":[",
              nw ? ",\n" : "", f->date, f->side, f->tf, f->grade, f->hh5, f->reg, f->end, f->pole, f->plo, f->phi, f->flo, f->em, f->xm, f->e, f->st, f->tg, f->x, f->r, f->pts, why, o.hi, o.lo, o.brk);
      // the flag's own chart, 09:30 → 16:00 on the 09:30 grid: [start minute, o, h, l, c]
      int nbw = 0; for (size_t k = 0; k < z.C.size();) { const int b0 = 930 + (z.TM[k] - 930) / f->tf * f->tf; double o2 = z.O[k], h2 = z.H[k], l2 = z.L[k], c2 = z.C[k];
        while (k < z.C.size() && 930 + (z.TM[k] - 930) / f->tf * f->tf == b0) { h2 = std::max(h2, z.H[k]); l2 = std::min(l2, z.L[k]); c2 = z.C[k]; ++k; }
        fprintf(fo, "%s[%d,%.2f,%.2f,%.2f,%.2f]", nbw++ ? "," : "", b0, o2, h2, l2, c2); }
      fprintf(fo, "]}"); nw++; }
    fprintf(fo, "\n]\n"); fclose(fo); fprintf(stderr, "wrote %d trades to %s\n", nw, argv[2]); }
  return 0;
}
