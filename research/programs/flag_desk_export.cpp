// 2026-10-22 flag_desk_export.cpp (founder: "add them as filters on the page. and I wanna see trades taken"). Writes the data of the ORB Flag Desk
// page: every flag registered after the 5-minute opening range broke (09:30-09:35, first 1m close beyond it before 12:00), 2014-01 → 2026-10, both the
// TREND flags (on the break's side) and the REVERSAL flags (the other side), with everything the page filters on (grade, 5m structure, chart, time,
// first of the day, gap, VIX, last 5 sessions, ATR trend) and three exits per flag: the measured move (the rule), the nearest session level >= 0.5 R
// (overnight / Asia / London / yesterday / last week / last month high or low; none → measured move) and hold to 15:55 — same entry (flag-mid limit)
// and stop (flag low − 0.1 ATR) as flag_level_target.cpp. Bars: one series per (date, chart) on the 09:30 grid, prices in ticks from the series' first
// open. Usage: flag_desk_export SYM EXPORT.csv OUT.json (EXPORT = flag_hhhl.cpp export). Read-only. tday = minutes since 18:00 ET (09:30 = 930).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <map>
using namespace lab;
struct Fl { int date, side, tf, reg, end, em, xm, grade, hh5; double plo, phi, flo, e, st, tg, x, r, x5, x27, x28, x30, x31; };
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_desk_export SYM EXPORT.csv OUT.json\n"); return 1; }
  const std::string sym = argv[1]; const double cost = sym == "ES" ? 0.5 : 1.5, tick = 0.25;
  std::vector<Fl> X; { std::ifstream f(argv[2]); if (!f) { perror(argv[2]); return 1; } std::string ln; std::getline(f, ln);
    while (std::getline(f, ln)) { std::vector<double> a; const char* p = ln.c_str(); char* e; while (*p) { a.push_back(strtod(p, &e)); if (e == p) break; p = e; if (*p == ',') ++p; else break; }
      if (a.size() < 55 || a[11] < 0.5 || a[0] < 20140101) continue;
      X.push_back({(int)a[0], (int)a[1], (int)a[2], (int)a[3], (int)a[4], (int)a[12], (int)a[13], (int)a[6], (int)a[7], a[8], a[9], a[10], a[14], a[15], a[16], a[17], a[18], a[25], a[47], a[48], a[50], a[51]}); } }
  std::sort(X.begin(), X.end(), [](const Fl& a, const Fl& b) { return a.date != b.date ? a.date < b.date : a.em != b.em ? a.em < b.em : a.tf < b.tf; });
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  struct DB { double atr, lv[2][6], orbHi, orbLo; int dir, brk; std::vector<double> O, H, L, C; std::vector<int> TM; };
  std::map<int, DB> days; double Anow = 1;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20140101 || d.date > 20261016) continue; DB z; z.atr = d.atr;
    double h[3] = {-1e18, -1e18, -1e18}, l[3] = {1e18, 1e18, 1e18};
    for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]);
      if (tm < 930) { h[0] = std::max(h[0], (double)s.h[i]); l[0] = std::min(l[0], (double)s.l[i]); const int w = tm < 480 ? 1 : tm < 840 ? 2 : -1;
        if (w > 0) { h[w] = std::max(h[w], (double)s.h[i]); l[w] = std::min(l[w], (double)s.l[i]); } }
      else if (tm < 1320) { z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.TM.push_back(tm); } }
    if (z.O.size() < 370) continue; Anow = d.atr;
    const double ph[6] = {h[0], h[1], h[2], d.ph, d.pwh, d.pmh}, pl[6] = {l[0], l[1], l[2], d.pl, d.pwl, d.pml};
    for (int j = 0; j < 6; ++j) { z.lv[0][j] = std::isfinite(ph[j]) && ph[j] > -1e17 ? ph[j] : NAN; z.lv[1][j] = std::isfinite(pl[j]) && pl[j] < 1e17 ? pl[j] : NAN; }
    z.orbHi = -1e18; z.orbLo = 1e18; size_t k = 0; for (; k < z.C.size() && z.TM[k] < 935; ++k) { z.orbHi = std::max(z.orbHi, z.H[k]); z.orbLo = std::min(z.orbLo, z.L[k]); }
    z.dir = 0; z.brk = 0; for (; k < z.C.size() && z.TM[k] < 1200; ++k) if (z.C[k] > z.orbHi || z.C[k] < z.orbLo) { z.dir = z.C[k] > z.orbHi ? 1 : -1; z.brk = z.TM[k] + 1; break; }
    days[d.date] = std::move(z); }
  auto sim = [](const DB& z, const Fl& f, double tg, int& xm, double& xp) -> double { const int sd = f.side; size_t k0 = 0; while (k0 < z.TM.size() && z.TM[k0] < f.em) ++k0; xm = 1315; xp = f.e;
    if (k0 >= z.TM.size()) return 0; if (sd > 0 ? z.L[k0] <= f.st : z.H[k0] >= f.st) { xm = z.TM[k0]; xp = f.st; return -std::fabs(f.e - f.st); }
    for (size_t k = k0 + 1; k < z.TM.size(); ++k) { if (z.TM[k] >= 1315) { xm = z.TM[k]; xp = z.O[k]; return sd * (z.O[k] - f.e); }
      if (sd > 0 ? z.L[k] <= f.st : z.H[k] >= f.st) { xm = z.TM[k]; xp = f.st; return -std::fabs(f.e - f.st); }
      if (!std::isnan(tg) && (sd > 0 ? z.H[k] >= tg : z.L[k] <= tg)) { xm = z.TM[k]; xp = tg; return std::fabs(tg - f.e); } }
    xm = z.TM.back(); xp = z.C.back(); return sd * (z.C.back() - f.e); };
  auto nearest = [](const DB& z, const Fl& f, double from) { double b = NAN; for (int j = 0; j < 6; ++j) { const double v = z.lv[f.side > 0 ? 0 : 1][j]; if (std::isnan(v)) continue;
      if (f.side > 0 ? v > from : v < from) if (std::isnan(b) || std::fabs(v - from) < std::fabs(b - from)) b = v; } return b; };
  FILE* fo = fopen(argv[3], "w"); if (!fo) { perror(argv[3]); return 1; }
  std::map<std::pair<int, int>, int> ser; std::vector<std::pair<int, int>> serKeys; int nt = 0; std::map<std::pair<int, int>, int> firstSeen;   // (date, kind) → first
  fprintf(fo, "{\"sym\":\"%s\",\"atrNow\":%.2f,\"cost\":%.2f,\"trades\":[", sym.c_str(), Anow, cost);
  for (auto& f : X) { auto it = days.find(f.date); if (it == days.end()) continue; const DB& z = it->second; if (!z.dir || f.reg < z.brk) continue;
    const int kind = f.side == z.dir ? 1 : 2; const bool first = !firstSeen.count({f.date, kind}); firstSeen[{f.date, kind}] = 1;
    const auto key = std::make_pair(f.date, f.tf); if (!ser.count(key)) { ser[key] = (int)serKeys.size(); serKeys.push_back(key); }
    const double R = std::fabs(f.e - f.st); const double l5 = nearest(z, f, f.e + f.side * 0.5 * R);
    int xmM, xmL, xmH; double xpM, xpL, xpH; const double gM = sim(z, f, f.tg, xmM, xpM), gL = sim(z, f, std::isnan(l5) ? f.tg : l5, xmL, xpL), gH = sim(z, f, NAN, xmH, xpH);
    fprintf(fo, "%s\n{\"d\":%d,\"s\":%d,\"k\":%d,\"tf\":%d,\"g\":%d,\"h\":%d,\"rg\":%d,\"en\":%d,\"pl\":%.2f,\"ph\":%.2f,\"fl\":%.2f,\"em\":%d,\"e\":%.2f,\"st\":%.2f,\"tg\":%.2f,\"lv\":%s,"
            "\"x\":[[%d,%.2f,%.4f],[%d,%.2f,%.4f],[%d,%.2f,%.4f]],\"fi\":%d,\"gp\":%d,\"vx\":%.1f,\"d5\":%d,\"au\":%d,\"f\":%.4f,\"oh\":%.2f,\"ol\":%.2f,\"bk\":%d,\"sr\":%d}",
            nt ? "," : "", f.date, f.side, kind, f.tf, f.grade, f.hh5, f.reg, f.end, f.plo, f.phi, f.flo, f.em, f.e, f.st, f.tg, std::isnan(l5) ? "null" : std::to_string(l5).c_str(),
            xmM, xpM, (gM - cost) / R, xmL, xpL, (gL - cost) / R, xmH, xpH, (gH - cost) / R, first ? 1 : 0, f.x5 > 1e-9 ? 1 : f.x5 < -1e-9 ? -1 : 0, f.x30,
            f.x28 * f.side < 0 ? 1 : 0, f.x31 > 1 ? 1 : 0, Anow / z.atr, z.orbHi, z.orbLo, z.brk, ser[key]);
    nt++; }
  fprintf(fo, "\n],\"series\":[");
  for (size_t q = 0; q < serKeys.size(); ++q) { const DB& z = days[serKeys[q].first]; const int tf = serKeys[q].second; const double base = z.O[0];
    fprintf(fo, "%s\n[%.2f", q ? "," : "", base); size_t k = 0;
    for (int b0 = 930; b0 < 1320; b0 += tf) { double o = NAN, h = -1e18, l = 1e18, c = NAN; while (k < z.C.size() && z.TM[k] < b0 + tf) { if (z.TM[k] >= b0) { if (std::isnan(o)) o = z.O[k]; h = std::max(h, z.H[k]); l = std::min(l, z.L[k]); c = z.C[k]; } ++k; }
      if (std::isnan(o)) fprintf(fo, ",null,null,null,null"); else fprintf(fo, ",%ld,%ld,%ld,%ld", lround((o - base) / tick), lround((h - base) / tick), lround((l - base) / tick), lround((c - base) / tick)); }
    fprintf(fo, "]"); }
  fprintf(fo, "\n]}\n"); fclose(fo); fprintf(stderr, "wrote %d flags, %zu bar series to %s\n", nt, serKeys.size(), argv[3]); return 0;
}
