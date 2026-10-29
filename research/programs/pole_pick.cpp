// 2026-10-22 pole_pick.cpp (founder: "ok so we can take the best ones from each size", after flag_traits.cpp's pole-size table). Walk-forward:
// for each test year 2017-2026 and each pole-size bucket (2-3 / 3-4 / 4-5 / 5-7 / >= 7 × the average bar), pick the filter (every flag /
// A-list / A-list + 5m HH/HL / … + with the 5m opening-range break, × both sides / bull only = 8 choices) with the best average R over ALL
// EARLIER years (>= 30 trades there), or stand aside if the best is <= 0; trade every bucket's pick that year, one position at a time overall.
// Compared with today's fixed rule (pole >= 4×, A-list + HH/HL + break, both sides) and with the in-sample pick (best filter per bucket over
// 2014-26 — not tradable). Input: flag_hhhl.cpp `export F pole2`. Usage: pole_pick POLE2_NQ.csv POLE2_ES.csv. Read-only.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>
struct P { int date, year, em, xm, side, grade, hh5, orb; double r, mult; };
static const double BK[6] = {2, 3, 4, 5, 7, 1e9};
static int bucket(double m) { for (int k = 0; k < 5; ++k) if (m >= BK[k] && m < BK[k + 1]) return k; return -1; }
static bool inSet(const P& p, int c) { const int st = c % 4, bull = c / 4; if (bull && p.side < 0) return false; if (st >= 1 && p.grade != 1) return false; if (st >= 2 && !p.hh5) return false; if (st >= 3 && p.orb < 1) return false; return true; }
static const char* CN[8] = {"every flag", "A-list", "A + HH/HL", "A + HH/HL + break", "bull: every flag", "bull: A-list", "bull: A + HH/HL", "bull: A + HH/HL + break"};
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: pole_pick POLE2_NQ.csv POLE2_ES.csv\n"); return 1; }
  printf("Best filter per pole size, walk-forward — each test year 2017-2026 uses, per pole bucket, the filter that was best over all earlier years\n(>= 30 trades, stand aside if <= 0); all buckets trade together, one position at a time. Pole >= 2× export, grades refit on it.\n");
  for (int mk = 0; mk < 2; ++mk) { std::vector<P> v; std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue; v.push_back({(int)a[0], (int)a[0] / 10000, (int)a[12], (int)a[13], (int)a[1], (int)a[6], (int)a[7], (int)a[47], a[18], a[22]}); }
    std::sort(v.begin(), v.end(), [](const P& a, const P& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    // trades of a selection in [y0, y1], one position at a time
    auto take = [&](std::function<bool(const P&)> sel, int y0, int y1, std::vector<const P*>& out) { int lastD = 0, busy = -1;
      for (auto& p : v) { if (p.year < y0 || p.year > y1 || !sel(p)) continue; if (p.date != lastD) { lastD = p.date; busy = -1; } if (p.em <= busy) continue; busy = p.xm; out.push_back(&p); } };
    auto avgOf = [&](std::function<bool(const P&)> sel, int y0, int y1, double& n) { std::vector<const P*> t; take(sel, y0, y1, t); double s = 0; for (auto* p : t) s += p->r; n = t.size(); return n ? s / n : 0.0; };
    struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, yr[10] = {0}, yc[10] = {0}; void add(const P& p) { n++; s += p.r; w += p.r > 0; (p.r > 0 ? gw : gl) += std::fabs(p.r); yr[p.year - 2017] += p.r; yc[p.year - 2017]++; } };
    auto line = [](const char* nm, const St& a) { int py = 0, ny = 0; for (int k = 0; k < 10; ++k) if (a.yc[k] >= 3) { ny++; py += a.yr[k] > 0; }
      printf("  %-46s %5.0f trades (%5.1f a year) %+6.3fR win %4.1f%% PF %.2f · years > 0 %d of %d\n", nm, a.n, a.n / 9.8, a.n ? a.s / a.n : 0, a.n ? 100 * a.w / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, py, ny); };
    printf("\n################ %s\n  year | pick per pole bucket (2-3× / 3-4× / 4-5× / 5-7× / 7×+), from earlier years\n", mk ? "ES" : "NQ");
    St wf; int pick[11][5];
    for (int Y = 2017; Y <= 2026; ++Y) { for (int k = 0; k < 5; ++k) { double best = 0; int bc = -1;
        for (int c = 0; c < 8; ++c) { double n; const double m = avgOf([&](const P& p) { return bucket(p.mult) == k && inSet(p, c); }, 2014, Y - 1, n); if (n >= 30 && m > best) { best = m; bc = c; } } pick[Y - 2016][k] = bc; }
      printf("  %d |", Y); for (int k = 0; k < 5; ++k) printf(" %-24s|", pick[Y - 2016][k] < 0 ? "stand aside" : CN[pick[Y - 2016][k]]); printf("\n");
      std::vector<const P*> t; take([&](const P& p) { const int k = bucket(p.mult); return k >= 0 && pick[Y - 2016][k] >= 0 && inSet(p, pick[Y - 2016][k]); }, Y, Y, t); for (auto* p : t) wf.add(*p); }
    printf("\n  2017-2026, out of sample:\n"); line("walk-forward best filter per pole size", wf);
    { St a; std::vector<const P*> t; take([](const P& p) { return p.mult >= 4 && inSet(p, 3); }, 2017, 2026, t); for (auto* p : t) a.add(*p); line("today's rule: pole >= 4×, A + HH/HL + break", a); }
    { St a; std::vector<const P*> t; take([](const P& p) { return p.mult >= 4 && inSet(p, 2); }, 2017, 2026, t); for (auto* p : t) a.add(*p); line("pole >= 4×, A + HH/HL", a); }
    { St a; std::vector<const P*> t; take([](const P& p) { return p.mult >= 4 && p.mult < 7 && inSet(p, 3); }, 2017, 2026, t); for (auto* p : t) a.add(*p); line("pole 4-7×, A + HH/HL + break", a); }
    // in-sample pick, for reading only
    int ins[5]; for (int k = 0; k < 5; ++k) { double best = 0; ins[k] = -1; for (int c = 0; c < 8; ++c) { double n; const double m = avgOf([&](const P& p) { return bucket(p.mult) == k && inSet(p, c); }, 2014, 2026, n); if (n >= 30 && m > best) { best = m; ins[k] = c; } } }
    { St a; std::vector<const P*> t; take([&](const P& p) { const int k = bucket(p.mult); return k >= 0 && ins[k] >= 0 && inSet(p, ins[k]); }, 2017, 2026, t); for (auto* p : t) a.add(*p);
      char nm[200]; snprintf(nm, sizeof nm, "in-sample pick (hindsight, not tradable)"); line(nm, a); printf("    in-sample picks:"); for (int k = 0; k < 5; ++k) printf(" %s", ins[k] < 0 ? "stand aside" : CN[ins[k]]); printf("\n"); } }
  return 0;
}
