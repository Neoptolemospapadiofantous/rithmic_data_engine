// 2026-10-22 flag_quality.cpp (founder: "yes" to the combined test). Runs research/preregistered/2026-10-22_flag_quality.md exactly as written:
// filters DT50 / DT200 (yesterday's close on the trade's side of the 50- / 200-day average), CLEAN (>= 3 of 4 bar conditions, each cut at the
// median of EARLIER years' flags), NOFOMC, QUALITY = DT50 + CLEAN + NOFOMC; sets main rule / bull rule / every 3-5m flag, and FRESH 1-2m flags;
// walk-forward 2017-26 use decision (filtered >= +0.05R better over earlier years), halves 2017-21 / 2022-26, NQ and ES, the 3-point pass.
// Usage: flag_quality NQ_STD NQ_TF15 ES_STD ES_TF15 [aplus]. Read-only. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;
struct Fl { int date, year, em, xm, tf, side, grade, hh5, orb; double r, c[4]; bool dt50, dt200, fomc, clean; };
int main(int argc, char** argv) {
  if (argc < 5) { fprintf(stderr, "usage: flag_quality NQ_STD NQ_TF15 ES_STD ES_TF15\n"); return 1; }
  std::set<int> fomc; { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  std::vector<Fl> STD[2], FR[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ"; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    struct DB { double pc, s50, s200; bool fomc; std::vector<int> TM; std::vector<double> O, H, L, C; }; std::map<int, DB> days;
    for (auto& d : s.days) { if (!d.ok || d.rollwin || d.date < 20140101 || d.date > 20261016) continue; DB z{d.pc, d.sma50, d.sma200, d.fomc, {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); }
      if (z.TM.size() >= 370) days[d.date] = std::move(z); }
    auto load = [&](const char* fn, std::vector<Fl>& out, int tfLo, int tfHi) { std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln);
      while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
        if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101 || a[2] < tfLo || a[2] > tfHi) continue; auto it = days.find((int)a[0]); if (it == days.end()) continue; const DB& z = it->second;
        const int side = (int)a[1], tf = (int)a[2], reg = (int)a[3], em = (int)a[12]; const double plo = a[8], phi = a[9];
        struct B { int t0; double o, h, l, c; }; std::vector<B> b;
        for (size_t k = 0; k < z.TM.size(); ++k) { const int t0 = 930 + (z.TM[k] - 930) / tf * tf; const double o = side * z.O[k], h = side > 0 ? z.H[k] : -z.L[k], l = side > 0 ? z.L[k] : -z.H[k], c = side * z.C[k];
          if (b.empty() || b.back().t0 != t0) b.push_back({t0, o, h, l, c}); else { B& x = b.back(); x.h = std::max(x.h, h); x.l = std::min(x.l, l); x.c = c; } }
        const int hbT = reg - 3 * tf; int hi = -1; for (int k = 0; k < (int)b.size(); ++k) if (b[k].t0 == hbT) { hi = k; break; } if (hi < 2) continue;
        const double poleLowM = side > 0 ? plo : -phi; int lo = -1; for (int k = hi - 2; k >= std::max(0, hi - 15); --k) if (std::fabs(b[k].l - poleLowM) < 0.01) { lo = k; break; } if (lo < 0) continue;
        std::vector<int> fl; for (int k = hi + 1; k < (int)b.size() && b[k].t0 + tf <= em; ++k) fl.push_back(k); if (fl.size() < 2) continue;
        auto rng = [&](int k) { return std::max(b[k].h - b[k].l, 0.25); };
        double pr = 0, pup = 0; for (int k = lo; k <= hi; ++k) { pr += rng(k); pup += b[k].c > b[k].o; } const int np = hi - lo + 1; const double poleH = b[hi].h - b[lo].l; if (!(poleH > 0)) continue;
        double fr = 0, fdn = 0; for (int k : fl) { fr += rng(k); fdn += b[k].c < b[k].o; }
        Fl x{(int)a[0], (int)a[0] / 10000, em, (int)a[13], tf, side, (int)a[6], (int)a[7], (int)a[47], a[18], {poleH / pr, pup / np, (fr / fl.size()) / (pr / np), fdn / fl.size()},
             side > 0 ? z.pc > z.s50 : z.pc < z.s50, side > 0 ? z.pc > z.s200 : z.pc < z.s200, z.fomc, false};
        if (std::isnan(z.s50)) x.dt50 = true; if (std::isnan(z.s200)) x.dt200 = true; out.push_back(x); }
      std::sort(out.begin(), out.end(), [](const Fl& p, const Fl& q) { return p.date != q.date ? p.date < q.date : p.em < q.em; });
      // CLEAN: medians of EARLIER years only (2014 has none → not clean)
      for (int Y = 2015; Y <= 2026; ++Y) { double med[4]; for (int j = 0; j < 4; ++j) { std::vector<double> v; for (auto& q : out) if (q.year < Y) v.push_back(q.c[j]); if (v.empty()) { med[j] = NAN; continue; }
          std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end()); med[j] = v[v.size() / 2]; }
        for (auto& q : out) if (q.year == Y) { const int n = (q.c[0] >= med[0]) + (q.c[1] >= med[1]) + (q.c[2] <= med[2]) + (q.c[3] <= med[3]); q.clean = n >= 3; } } };
    load(argv[1 + 2 * mk], STD[mk], 3, 5); load(argv[2 + 2 * mk], FR[mk], 1, 2); }
  struct St { double n = 0, s = 0, gw = 0, gl = 0; double m() const { return n ? s / n : 0; } double pf() const { return gl > 0 ? gw / gl : 99; } };
  auto run = [](const std::vector<Fl>& v, std::function<bool(const Fl&)> sel, int y0, int y1) { St a; int lastD = 0, busy = -1;
    for (auto& x : v) { if (x.year < y0 || x.year > y1 || !sel(x)) continue; if (x.date != lastD) { lastD = x.date; busy = -1; } if (x.em <= busy) continue; busy = x.xm; a.n++; a.s += x.r; (x.r > 0 ? a.gw : a.gl) += std::fabs(x.r); } return a; };
  struct F { const char* n; std::function<bool(const Fl&)> f; bool clean; };
  const F filt[] = {{"QUALITY (DT50 + CLEAN + no FOMC)", [](const Fl& x) { return x.dt50 && x.clean && !x.fomc; }, true}, {"DT50", [](const Fl& x) { return x.dt50; }, false},
                    {"DT200", [](const Fl& x) { return x.dt200; }, false}, {"CLEAN", [](const Fl& x) { return x.clean; }, true}, {"no FOMC", [](const Fl& x) { return !x.fomc; }, false},
                    {"DT50 + no FOMC", [](const Fl& x) { return x.dt50 && !x.fomc; }, false}};
  struct S { const char* n; std::function<bool(const Fl&)> f; };
  const S sets[] = {{"main rule (A-list + 5m HH/HL + with the break)", [](const Fl& x) { return x.grade == 1 && x.hh5 && x.orb > 0; }}, {"bull rule", [](const Fl& x) { return x.side > 0 && x.grade == 1 && x.hh5 && x.orb > 0; }},
                    {"every 3-5m flag", [](const Fl&) { return true; }}};
  const char* MK[2] = {"NQ", "ES"}; bool p1 = true, p2 = true, p3 = true;
  // 2026-10-22 Part 2 (the A+ setup, preregistered/2026-10-22_flag_quality.md): `flag_quality … aplus` prints only this section
  if (argc > 5 && std::string(argv[5]) == "aplus") {
    auto bullR = [](const Fl& x) { return x.side > 0 && x.grade == 1 && x.hh5 && x.orb > 0; }; auto bearR = [](const Fl& x) { return x.side < 0 && x.grade == 1 && x.hh5 && x.orb > 0; };
    auto aplus = [](const Fl& x) { return x.dt200 && x.clean && !x.fomc; };
    bool q1 = true, q2 = true, q3 = true, q4 = true;
    printf("The A+ setup (preregistered/2026-10-22_flag_quality.md, Part 2): bull rule + yesterday above the 200-day average + clean bars + no FOMC. Walk-forward 2017-2026.\n");
    for (int side = 1; side >= -1; side -= 2) { auto base = side > 0 ? std::function<bool(const Fl&)>(bullR) : std::function<bool(const Fl&)>(bearR);
      printf("\n######## %s\n", side > 0 ? "BULL rule → A+ (judged)" : "BEAR rule → its mirror (reported, not judged)");
      for (int mk = 0; mk < 2; ++mk) { St fa, ra, fh[2], rh[2]; int used = 0; std::map<int, std::pair<St, St>> yr;
        for (int Y = 2017; Y <= 2026; ++Y) { const St a = run(STD[mk], [&](const Fl& x) { return base(x) && aplus(x); }, 2015, Y - 1), b = run(STD[mk], base, 2015, Y - 1);
          const bool use = a.n >= 15 && a.m() >= b.m() + 0.10; used += use; const St t = run(STD[mk], [&](const Fl& x) { return base(x) && (!use || aplus(x)); }, Y, Y), r = run(STD[mk], base, Y, Y);
          yr[Y] = {t, r}; for (St* p : {&fa, &fh[Y >= 2022]}) { p->n += t.n; p->s += t.s; p->gw += t.gw; p->gl += t.gl; } for (St* p : {&ra, &rh[Y >= 2022]}) { p->n += r.n; p->s += r.s; p->gw += r.gw; p->gl += r.gl; } }
        const St all = run(STD[mk], [&](const Fl& x) { return base(x) && aplus(x); }, 2017, 2026);
        const double d = fa.m() - ra.m(), d0 = fh[0].m() - rh[0].m(), d1 = fh[1].m() - rh[1].m();
        if (side > 0) { if (d < 0.10 || fa.pf() <= ra.pf()) q1 = false; if (d0 <= 0 || d1 <= 0) q2 = false; if (all.n / 10 < 5) q3 = false; }
        printf("  %s walk-forward: %5.0f trades (%4.1f/yr) %+6.3fR PF %.2f · rule %5.0f (%4.1f/yr) %+6.3fR PF %.2f · difference %+6.3fR (2017-21 %+6.3f / 2022-26 %+6.3f) · A+ used %d/10 years\n",
               MK[mk], fa.n, fa.n / 10, fa.m(), fa.pf(), ra.n, ra.n / 10, ra.m(), ra.pf(), d, d0, d1, used);
        printf("     A+ every year (no walk-forward decision): %5.0f trades (%4.1f/yr) %+6.3fR PF %.2f · by year:", all.n, all.n / 10, all.m(), all.pf());
        for (int Y = 2017; Y <= 2026; ++Y) { const St a = run(STD[mk], [&](const Fl& x) { return base(x) && aplus(x); }, Y, Y); printf(" %d %+.2f(%.0f)", Y % 100, a.m(), a.n); } printf("\n"); } }
    printf("\n######## FRESH bull A-list 1-2m flags, 2015-2026: A+ conditions vs the rest\n");
    for (int mk = 0; mk < 2; ++mk) { auto b = [](const Fl& x) { return x.side > 0 && x.grade == 1; }; const St a = run(FR[mk], [&](const Fl& x) { return b(x) && aplus(x); }, 2015, 2026), r = run(FR[mk], [&](const Fl& x) { return b(x) && !aplus(x); }, 2015, 2026);
      if (a.m() - r.m() < 0.05) q4 = false; printf("  %s A+ %+6.3fR (%4.0f, PF %.2f) vs rest %+6.3fR (%4.0f) → %+6.3fR\n", MK[mk], a.m(), a.n, a.pf(), r.m(), r.n, a.m() - r.m()); }
    printf("\nPASS (A+): (1) >= +0.10R and higher PF, both markets: %s · (2) both halves better, both: %s · (3) >= 5 a year, both: %s · (4) fresh 1-2m >= +0.05R, both: %s → %s\n",
           q1 ? "yes" : "no", q2 ? "yes" : "no", q3 ? "yes" : "no", q4 ? "yes" : "no", q1 && q2 && q3 && q4 ? "A+ PASSES" : "A+ does not pass");
    return 0; }
  printf("Flag quality (preregistered/2026-10-22_flag_quality.md): walk-forward 2017-2026, a filter is used in a year only if it beat no filter by >= +0.05R over the earlier years.\n");
  printf("Columns per market: filtered trades a year, avg R, PF · unfiltered avg R, PF · difference (2017-21 / 2022-26) · years the filter was used.\n");
  for (auto& st : sets) { printf("\n######## %s\n", st.n);
    for (auto& fi : filt) { printf("  %-34s", fi.n);
      for (int mk = 0; mk < 2; ++mk) { St fa, ra, fh[2], rh[2]; int used = 0;
        for (int Y = 2017; Y <= 2026; ++Y) { const int y0 = fi.clean ? 2015 : 2014; const St a = run(STD[mk], [&](const Fl& x) { return st.f(x) && fi.f(x); }, y0, Y - 1), b = run(STD[mk], st.f, y0, Y - 1);
          const bool use = a.n >= 20 && a.m() >= b.m() + 0.05; used += use; const St t = run(STD[mk], [&](const Fl& x) { return st.f(x) && (!use || fi.f(x)); }, Y, Y), r = run(STD[mk], st.f, Y, Y);
          for (St* p : {&fa, &fh[Y >= 2022]}) { p->n += t.n; p->s += t.s; p->gw += t.gw; p->gl += t.gl; } for (St* p : {&ra, &rh[Y >= 2022]}) { p->n += r.n; p->s += r.s; p->gw += r.gw; p->gl += r.gl; } }
        const double d = fa.m() - ra.m(), d0 = fh[0].m() - rh[0].m(), d1 = fh[1].m() - rh[1].m();
        if (&fi == &filt[0]) { if (&st == &sets[0] || &st == &sets[2]) { if (d < 0.05 || fa.pf() <= ra.pf()) p1 = false; } if (&st == &sets[2] && (d0 <= 0 || d1 <= 0)) p2 = false; }
        printf(" | %s %5.1f/yr %+6.3fR PF %.2f · raw %+6.3fR PF %.2f · %+6.3fR (%+6.3f / %+6.3f) · used %2d/10", MK[mk], fa.n / 10, fa.m(), fa.pf(), ra.m(), ra.pf(), d, d0, d1, used); }
      printf("\n"); } }
  printf("\n######## FRESH 1-2m flags (never used to find the filters), 2015-2026: filtered vs the rest\n");
  for (int si = 0; si < 2; ++si) { const S fs = si == 0 ? S{"every 1-2m flag", [](const Fl&) { return true; }} : S{"A-list 1-2m", [](const Fl& x) { return x.grade == 1; }};
    for (auto& fi : filt) { printf("  %-16s %-34s", fs.n, fi.n);
      for (int mk = 0; mk < 2; ++mk) { const St a = run(FR[mk], [&](const Fl& x) { return fs.f(x) && fi.f(x); }, 2015, 2026), b = run(FR[mk], [&](const Fl& x) { return fs.f(x) && !fi.f(x); }, 2015, 2026);
        if (si == 0 && &fi == &filt[0] && a.m() - b.m() < 0.05) p3 = false; printf(" | %s %+6.3fR (%5.0f) vs rest %+6.3fR (%5.0f) → %+6.3fR", MK[mk], a.m(), a.n, b.m(), b.n, a.m() - b.m()); }
      printf("\n"); } }
  printf("\nPASS (QUALITY): (1) walk-forward >= +0.05R and higher PF, main rule and every flag, both markets: %s · (2) every flag better in both halves, both markets: %s · (3) fresh 1-2m >= +0.05R, both: %s → %s\n",
         p1 ? "yes" : "no", p2 ? "yes" : "no", p3 ? "yes" : "no", p1 && p2 && p3 ? "QUALITY IS ADOPTED" : "QUALITY is not adopted");
  return 0;
}
