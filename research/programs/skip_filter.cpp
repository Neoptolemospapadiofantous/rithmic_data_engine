// 2026-10-22 skip_filter.cpp (founder: "test"). Part 4 of research/preregistered/2026-10-22_reversal_plan.md, as written before the run: the
// filter "skip a flag registered <= W minutes after a same-side reversal signal (R1 / R2 / D1)" — T1 walk-forward 2017-26, T2 inside each
// registration-time bucket, T3 W = 60 / 90 / 120 / 180, T4 on 1-2 minute flags (never used to find it). NQ and ES. Pass rules in the plan.
// Usage: skip_filter FLAGS_NQ TF15_NQ RP_NQ RD_NQ FLAGS_ES TF15_ES RP_ES RD_ES. Read-only (CSV exports). tday = minutes since 18:00 ET.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <vector>
static std::vector<std::vector<double>> readCsv(const char* fn) { std::vector<std::vector<double>> o; std::ifstream f(fn); if (!f) { perror(fn); exit(1); } std::string ln; std::getline(f, ln);
  while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; } o.push_back(a); } return o; }
struct Fl { int date, year, side, tf, reg, em, xm, grade, hh5, orb; double r; int lastSig; };   // lastSig: minutes since the latest same-side signal (99999 = none)
struct St { double n = 0, s = 0; double m() const { return n ? s / n : 0; } };
int main(int argc, char** argv) {
  if (argc < 9) { fprintf(stderr, "usage: skip_filter FLAGS_NQ TF15_NQ RP_NQ RD_NQ FLAGS_ES TF15_ES RP_ES RD_ES\n"); return 1; }
  std::vector<Fl> F[2], G[2];   // F = 3-5m flags (standard export), G = 1-2m flags (tf15 export)
  for (int mk = 0; mk < 2; ++mk) { std::map<int, std::vector<std::pair<int, int>>> sig;   // date → (side, signal minute)
    for (int q = 0; q < 2; ++q) for (auto& a : readCsv(argv[3 + 4 * mk + q])) { if (a.size() < 15) continue; const int s = (int)a[2]; if (s == 1 || s == 2 || s == 11) sig[(int)a[0]].push_back({(int)a[1], (int)a[3]}); }
    auto load = [&](const char* fn, std::vector<Fl>& out, int tfLo, int tfHi) { for (auto& a : readCsv(fn)) { if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101 || a[2] < tfLo || a[2] > tfHi) continue;
        Fl f{(int)a[0], (int)a[0] / 10000, (int)a[1], (int)a[2], (int)a[3], (int)a[12], (int)a[13], (int)a[6], (int)a[7], (int)a[47], a[18], 99999};
        auto it = sig.find(f.date); if (it != sig.end()) for (auto& sg : it->second) if (sg.first == f.side && sg.second <= f.reg) f.lastSig = std::min(f.lastSig, f.reg - sg.second); out.push_back(f); }
      std::sort(out.begin(), out.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); };
    load(argv[1 + 4 * mk], F[mk], 3, 5); load(argv[2 + 4 * mk], G[mk], 1, 2); }
  auto run = [](const std::vector<Fl>& v, std::function<bool(const Fl&)> sel, int y0, int y1) { St a; int lastD = 0, busy = -1;
    for (auto& f : v) { if (f.year < y0 || f.year > y1 || !sel(f)) continue; if (f.date != lastD) { lastD = f.date; busy = -1; } if (f.em <= busy) continue; busy = f.xm; a.n++; a.s += f.r; } return a; };
  auto after = [](const Fl& f, int W) { return f.lastSig <= W; };
  const char* MK[2] = {"NQ", "ES"};
  struct Set { const char* n; std::function<bool(const Fl&)> f; };
  const Set sets[] = {{"every flag", [](const Fl&) { return true; }}, {"A-list + 5m HH/HL + with the break", [](const Fl& f) { return f.grade == 1 && f.hh5 && f.orb > 0; }}};
  printf("The skip filter (preregistered/2026-10-22_reversal_plan.md, Part 4): skip a flag registered <= W minutes after a same-side reversal signal.\n");
  bool p1 = true, p2 = true, p3 = true, p4 = true;
  printf("\n######## T1 WALK-FORWARD 2017-2026 (W = 120; the filter is used in a year only if earlier years show after-signal flags >= 0.10R worse)\n");
  for (int si = 0; si < 2; ++si) { printf("  %s\n", sets[si].n);
    for (int mk = 0; mk < 2; ++mk) { St fil, raw; int used = 0;
      for (int Y = 2017; Y <= 2026; ++Y) { const St a = run(F[mk], [&](const Fl& f) { return sets[si].f(f) && after(f, 120); }, 2014, Y - 1), b = run(F[mk], [&](const Fl& f) { return sets[si].f(f) && !after(f, 120); }, 2014, Y - 1);
        const bool use = a.n >= 20 && a.m() <= b.m() - 0.10; used += use;
        const St t = run(F[mk], [&](const Fl& f) { return sets[si].f(f) && !(use && after(f, 120)); }, Y, Y), r = run(F[mk], sets[si].f, Y, Y); fil.n += t.n; fil.s += t.s; raw.n += r.n; raw.s += r.s; }
      const double d = fil.m() - raw.m(); if (si == 0 && d < 0.05) p1 = false; if (si == 1 && d < 0) p1 = false;
      printf("    %s: filter used in %d of 10 years · filtered %5.0f tr %+6.3fR (total %+6.1fR) · unfiltered %5.0f tr %+6.3fR (total %+6.1fR) · per trade %+6.3fR\n", MK[mk], used, fil.n, fil.m(), fil.s, raw.n, raw.m(), raw.s, d); } }
  printf("\n######## T2 INSIDE EACH REGISTRATION-TIME BUCKET (every flag, W = 120, 2014-26): after a signal vs without\n");
  for (int mk = 0; mk < 2; ++mk) { int neg = 0; printf("  %s:", MK[mk]); const int B[4] = {0, 990, 1080, 9999}; static const char* BN[3] = {"before 10:30", "10:30-12:00", "12:00-14:00"};
    for (int k = 0; k < 3; ++k) { const St a = run(F[mk], [&](const Fl& f) { return f.reg >= B[k] && f.reg < B[k + 1] && after(f, 120); }, 2014, 2026), b = run(F[mk], [&](const Fl& f) { return f.reg >= B[k] && f.reg < B[k + 1] && !after(f, 120); }, 2014, 2026);
      neg += a.m() < b.m(); printf("  %s %+6.3fR (%4.0f) vs %+6.3fR (%4.0f) ·", BN[k], a.m(), a.n, b.m(), b.n); } printf("  gap negative in %d of 3\n", neg); if (neg < 2) p2 = false; }
  printf("\n######## T3 WINDOWS (every flag, 2014-26): after a signal vs without\n");
  for (int mk = 0; mk < 2; ++mk) { printf("  %s:", MK[mk]); for (int W : {60, 90, 120, 180}) { const St a = run(F[mk], [&](const Fl& f) { return after(f, W); }, 2014, 2026), b = run(F[mk], [&](const Fl& f) { return !after(f, W); }, 2014, 2026);
      if (a.m() >= b.m()) p3 = false; printf("  W %3d: %+6.3fR (%4.0f) vs %+6.3fR (%4.0f) ·", W, a.m(), a.n, b.m(), b.n); } printf("\n"); }
  printf("\n######## T4 NEW FLAGS — 1-2 minute charts (never used to find the filter), W = 120, 2014-26\n");
  for (int mk = 0; mk < 2; ++mk) { for (int si = 0; si < 2; ++si) { const St a = run(G[mk], [&](const Fl& f) { return sets[si].f(f) && after(f, 120); }, 2014, 2026), b = run(G[mk], [&](const Fl& f) { return sets[si].f(f) && !after(f, 120); }, 2014, 2026);
      if (si == 0 && a.m() >= b.m()) p4 = false; printf("  %s %-38s after a signal %+6.3fR (%4.0f) vs without %+6.3fR (%4.0f) → %+6.3fR\n", MK[mk], sets[si].n, a.m(), a.n, b.m(), b.n, a.m() - b.m()); } }
  printf("\nPASS: T1 %s · T2 %s · T3 %s · T4 %s → %s\n", p1 ? "yes" : "no", p2 ? "yes" : "no", p3 ? "yes" : "no", p4 ? "yes" : "no", p1 && p2 && p3 && p4 ? "THE FILTER PASSES" : "the filter is dropped");
  return 0;
}
