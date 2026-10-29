// 2026-10-22 reversal_flag_combo.cpp (founder: "and combine reversal with flags? test ideas in that as well"). Part 3 of
// research/preregistered/2026-10-22_reversal_plan.md, as written before the run:
//   C1 flag after a reversal — flags registered <= 120 min after a same-side reversal signal (R1 / R2 / D1) vs flags without one
//   C2 reversal after a failed flag — reversal signals <= 60 min after an opposite-side flag trade was stopped out vs without
// Pass: >= 100 trades, "after" >= +0.15R over "without" on NQ in 2014-19 AND 2020-26, and >= +0.10R on ES.
// Usage: reversal_flag_combo FLAGS_NQ RP_NQ RD_NQ FLAGS_ES RP_ES RD_ES. Read-only (CSV exports only). tday = minutes since 18:00 ET.
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
struct Fl { int date, side, reg, em, xm, grade, hh5, orb; double r, st, ex; };
struct Rv { int date, side, setup, sig, em, xm; double r; };
struct St { double n = 0, s = 0, w = 0, gw = 0, gl = 0, h[2] = {0, 0}, hn[2] = {0, 0}; void add(double r, int y) { n++; s += r; w += r > 0; (r > 0 ? gw : gl) += std::fabs(r); h[y >= 2020] += r; hn[y >= 2020]++; }
  double m() const { return n ? s / n : 0; } double hm(int k) const { return hn[k] ? h[k] / hn[k] : 0; } };
template <class T> static St runOne(const std::vector<T>& v, std::function<bool(const T&)> sel) { St a; int lastD = 0, busy = -1;
  for (auto& t : v) { if (!sel(t)) continue; if (t.date != lastD) { lastD = t.date; busy = -1; } if (t.em <= busy) continue; busy = t.xm; a.add(t.r, t.date / 10000); } return a; }
int main(int argc, char** argv) {
  if (argc < 7) { fprintf(stderr, "usage: reversal_flag_combo FLAGS_NQ RP_NQ RD_NQ FLAGS_ES RP_ES RD_ES\n"); return 1; }
  std::vector<Fl> F[2]; std::vector<Rv> R[2];
  for (int mk = 0; mk < 2; ++mk) { for (auto& a : readCsv(argv[1 + 3 * mk])) { if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue;
      F[mk].push_back({(int)a[0], (int)a[1], (int)a[3], (int)a[12], (int)a[13], (int)a[6], (int)a[7], (int)a[47], a[18], a[15], a[17]}); }
    for (int q = 0; q < 2; ++q) for (auto& a : readCsv(argv[2 + 3 * mk + q])) { if (a.size() < 15 || a[0] < 20140101) continue; const int s = (int)a[2]; if (s != 1 && s != 2 && s != 11) continue;
        R[mk].push_back({(int)a[0], (int)a[1], s, (int)a[3], (int)a[6], (int)a[13], a[10]}); }
    std::sort(F[mk].begin(), F[mk].end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; });
    std::sort(R[mk].begin(), R[mk].end(), [](const Rv& a, const Rv& b) { return a.date != b.date ? a.date < b.date : a.em < b.em; }); }
  // lookups per market: same-side reversal signal <= 120 min before a flag's registration; opposite-side flag stopped out <= 60 min before a signal
  std::vector<char> after[2], failed[2];
  for (int mk = 0; mk < 2; ++mk) { std::map<int, std::vector<const Rv*>> rd; for (auto& r : R[mk]) rd[r.date].push_back(&r); std::map<int, std::vector<const Fl*>> fd; for (auto& f : F[mk]) fd[f.date].push_back(&f);
    for (auto& f : F[mk]) { bool hit = false; auto it = rd.find(f.date); if (it != rd.end()) for (auto* r : it->second) if (r->side == f.side && r->sig <= f.reg && f.reg - r->sig <= 120) hit = true; after[mk].push_back(hit); }
    for (auto& r : R[mk]) { bool hit = false; auto it = fd.find(r.date); if (it != fd.end()) for (auto* f : it->second) { const bool stopped = f->r < 0 && std::fabs(f->ex - f->st) < 0.01;
          if (f->side == -r.side && stopped && f->xm <= r.sig && r.sig - f->xm <= 60) hit = true; } failed[mk].push_back(hit); } }
  auto line = [](const char* nm, const St& a) { printf("      %-26s %5.0f tr %+6.3fR win %4.1f%% PF %.2f · 2014-19 %+6.3f / 2020-26 %+6.3f\n", nm, a.n, a.m(), a.n ? 100 * a.w / a.n : 0, a.gl > 0 ? a.gw / a.gl : 99.0, a.hm(0), a.hm(1)); };
  printf("Reversals combined with flags (preregistered/2026-10-22_reversal_plan.md, Part 3). NQ and ES 2014-01 → 2026-10, R after cost, one position at a time per group.\n");
  int passes = 0, judged = 0;
  auto judge = [&](const char* title, const St an[2], const St wn[2]) { judged++; const bool ok = an[0].n >= 100 && an[0].hm(0) - wn[0].hm(0) >= 0.15 && an[0].hm(1) - wn[0].hm(1) >= 0.15 && an[1].m() - wn[1].m() >= 0.10;
    passes += ok; printf("    → %s: NQ difference %+6.3fR (2014-19 %+6.3f / 2020-26 %+6.3f), ES %+6.3fR%s\n", title, an[0].m() - wn[0].m(), an[0].hm(0) - wn[0].hm(0), an[0].hm(1) - wn[0].hm(1), an[1].m() - wn[1].m(), ok ? "  ← PASSES" : ""); };
  printf("\n######## C1 FLAG AFTER A SAME-SIDE REVERSAL SIGNAL (<= 120 min before the flag registered)\n");
  struct S1 { const char* n; std::function<bool(const Fl&)> f; };
  const S1 sets[] = {{"every flag", [](const Fl&) { return true; }}, {"A-list", [](const Fl& f) { return f.grade == 1; }}, {"A-list + 5m HH/HL", [](const Fl& f) { return f.grade == 1 && f.hh5; }},
                     {"flags WITH the 5m break", [](const Fl& f) { return f.orb > 0; }}, {"flags AGAINST the 5m break (reversal flags)", [](const Fl& f) { return f.orb < 0; }},
                     {"A-list + HH/HL AGAINST the break", [](const Fl& f) { return f.orb < 0 && f.grade == 1 && f.hh5; }}};
  for (auto& S0 : sets) { printf("  %s\n", S0.n); St an[2], wn[2];
    for (int mk = 0; mk < 2; ++mk) { size_t i0 = 0; std::vector<int> idx(F[mk].size()); for (size_t i = 0; i < F[mk].size(); ++i) idx[i] = (int)i; (void)i0;
      an[mk] = runOne<Fl>(F[mk], [&](const Fl& f) { return S0.f(f) && after[mk][&f - &F[mk][0]]; }); wn[mk] = runOne<Fl>(F[mk], [&](const Fl& f) { return S0.f(f) && !after[mk][&f - &F[mk][0]]; });
      printf("    %s\n", mk ? "ES" : "NQ"); line("after a reversal signal", an[mk]); line("no reversal signal before", wn[mk]); }
    judge(S0.n, an, wn); }
  printf("\n######## C2 REVERSAL SIGNAL AFTER AN OPPOSITE-SIDE FLAG WAS STOPPED OUT (<= 60 min before), exit X2\n");
  static const char* SN[3] = {"R1 CHoCH pullback", "R2 sweep and reclaim", "D1 double bottom / top"}; const int SC[3] = {1, 2, 11};
  for (int k = 0; k < 3; ++k) { printf("  %s\n", SN[k]); St an[2], wn[2];
    for (int mk = 0; mk < 2; ++mk) { an[mk] = runOne<Rv>(R[mk], [&](const Rv& r) { return r.setup == SC[k] && failed[mk][&r - &R[mk][0]]; }); wn[mk] = runOne<Rv>(R[mk], [&](const Rv& r) { return r.setup == SC[k] && !failed[mk][&r - &R[mk][0]]; });
      printf("    %s\n", mk ? "ES" : "NQ"); line("after a failed flag", an[mk]); line("no failed flag before", wn[mk]); }
    judge(SN[k], an, wn); }
  printf("\n%d of %d judged combinations pass.\n", passes, judged);
  return 0;
}
