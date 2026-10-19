// 2026-10-19 (founder: "need to see all stats and trades"): every trade of the two NY setups that held up today, 2010-07 → 2026-10,
// for the NY Setups Log page.
//  WICK  wick-close flag (flag_wickentry.cpp W1, measured-move target, no trail) on 2-5m — read from inputs_20261019/flag_wickentry_w1_full.csv.
//  CHOCH change of character with the session direction (choch_ny_detail.cpp) on 4m and 5m: bull = price above the 09:30 open, the chart's
//        structure had turned down, a close above the last swing high → buy at that close; bear = the mirror. Stop beyond the swing
//        (lowest low since the broken swing), capped at 40 pts at today's range (40 × ATR/320); hold to 15:55; ≤ 5 a day, one at a time.
// Cost per trade = 2 pts × ATR/320 (as in every test today). Prints a summary (the repro output). With argv[1] = a directory it also writes
// <dir>/trades.js (const T=[...]) and <dir>/data/YYYY.csv (1-minute NY bars 09:30-16:00 of every day that has a trade: t = minute index of
// the New York wall clock, o,h,l,c × 4). Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
using namespace lab;
struct FB { double o, h, l, c; int k; };
struct Tr { int setup, tf, dir, day; std::string et; double e, stop, R; std::string xt; double x, pts, cost; int why; double tgt; std::vector<std::pair<std::string, std::string>> mk; };
static std::string hm(long ep) { time_t e = ep; struct tm x; gmtime_r(&e, &x); char b[24]; strftime(b, sizeof b, "%Y-%m-%d %H:%M", &x); return b; }
int main(int argc, char** argv) {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::map<int, int> dayIx; for (size_t i = 0; i < s.days.size(); ++i) dayIx[s.days[i].date] = i;
  std::vector<Tr> T;
  { std::ifstream f("research/programs/inputs_20261019/flag_wickentry_w1_full.csv"); std::string l; std::getline(f, l);
    while (std::getline(f, l)) { std::vector<std::string> c; std::stringstream ss(l); std::string x; while (std::getline(ss, x, ',')) c.push_back(x);
      if (c.size() < 21 || c[0] != "NY" || c[20] != "measured move") continue; int tf = std::stoi(c[1]); if (tf < 2 || tf > 5) continue; int day = std::stoi(c[3]); auto it = dayIx.find(day); if (it == dayIx.end()) continue;
      const double A = s.days[it->second].atr; int D = c[2] == "bull" ? 1 : -1; double e = std::stod(c[6]), st = std::stod(c[7]);
      int why = c[14] == "stop" ? 0 : c[14] == "target" ? 3 : 2;
      T.push_back({0, tf, D, day, c[4] + " " + c[5], e, st, std::stod(c[8]), c[9] + " " + c[10], std::stod(c[11]), std::stod(c[12]), 2.0 * A / 320.0, why, e + D * std::stod(c[19]), {{c[15], "pole"}, {c[16], "top"}, {c[17], "flag"}, {c[18], "wick"}}}); } }
  const int S0 = 930, SE = 1315;
  for (int tf = 4; tf <= 5; ++tf) for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016 || d.b1 - d.b0 < 300) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= S0 && tm < SE) I.push_back(i); } if (I.size() < 300) continue;
    const double sc = d.atr / 320.0, so = s.o[I[0]]; std::vector<FB> b; double PH = NAN, PL = NAN; int PHb = -1, PLb = -1; bool PHu = true, PLu = true; int trend = 0, nday = 0; size_t busy = 0; bool have = false;
    auto bt = [&](int r) { return hm(s.t[I[b[r].k]] - 60L * (tf - 1)); };
    for (size_t k = 0; k < I.size(); ++k) { int i = I[k], tm = tday_min(s.t[i]);
      if ((tm - S0) % tf == 0 || b.empty()) b.push_back({(double)s.o[i], (double)s.h[i], (double)s.l[i], (double)s.c[i], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, (double)s.h[i]); z.l = std::min(z.l, (double)s.l[i]); z.c = s.c[i]; z.k = k; }
      if ((tm + 1 - S0) % tf) continue; int q = b.size() - 1;
      if (q >= 4) { int p = q - 2; if (b[p].h > b[p - 1].h && b[p].h > b[p - 2].h && b[p].h > b[p + 1].h && b[p].h > b[p + 2].h) { PH = b[p].h; PHb = p; PHu = false; } if (b[p].l < b[p - 1].l && b[p].l < b[p - 2].l && b[p].l < b[p + 1].l && b[p].l < b[p + 2].l) { PL = b[p].l; PLb = p; PLu = false; } }
      const double c = b[q].c; int D = 0, brk = -1;
      if (!std::isnan(PH) && !PHu && c > PH) { PHu = true; if (trend == -1 && c > so) { D = 1; brk = PHb; } trend = 1; }
      else if (!std::isnan(PL) && !PLu && c < PL) { PLu = true; if (trend == 1 && c < so) { D = -1; brk = PLb; } trend = -1; }
      if (!D || tm < S0 + 15 || tm >= SE - 30 || nday >= 5 || (have && k <= busy)) continue;
      double ext = D > 0 ? 1e18 : -1e18; int eb = q; for (int r = q; r >= brk && r >= 0; --r) if (D > 0 ? b[r].l < ext : b[r].h > ext) { ext = D > 0 ? b[r].l : b[r].h; eb = r; }
      double swingR = D * (c - ext) + 0.25; if (!(swingR > 0.25) || swingR > 2 * d.atr) continue; double R = std::min(swingR, 40 * sc), st = c - D * R, px = NAN; size_t kx = I.size() - 1; int why = 2;
      for (size_t kk = k + 1; kk < I.size(); ++kk) { int j = I[kk]; if (D > 0 ? s.l[j] <= st : s.h[j] >= st) { px = st; kx = kk; why = 0; break; } }
      if (std::isnan(px)) px = s.c[I.back()];
      T.push_back({1, tf, D, d.date, hm(s.t[i] + 60), c, st, R, hm(s.t[I[kx]] + 60), px, D * (px - c), 2 * sc, why, 0, {{bt(brk), D > 0 ? "swing high" : "swing low"}, {bt(eb), "stop swing"}, {bt(q), "break"}}});
      busy = kx; have = true; nday++; } }
  std::sort(T.begin(), T.end(), [](const Tr& a, const Tr& b) { return a.et < b.et; });
  const char* SN[2] = {"Wick flag", "CHoCH"};
  printf("NY setups export — %zu trades, 2010-07 → 2026-10. Net = points after cost (2 × ATR/320), R = net / risk.\n", T.size());
  for (int st = 0; st < 2; ++st) for (int tf = 2; tf <= 5; ++tf) for (int D = 1; D >= -1; D -= 2) { double n = 0, w = 0, p = 0, r = 0, n2 = 0, p2 = 0; for (auto& t : T) if (t.setup == st && t.tf == tf && t.dir == D) { double net = t.pts - t.cost; n++; w += net > 0; p += net; r += net / t.R; if (t.day >= 20250101) { n2++; p2 += net; } }
    if (n) printf("  %-9s %dm %s  %5.0f trades | win %4.1f%% | net %+6.2f pts | %+.3fR | 2025-26 %+6.2f (%.0f)\n", SN[st], tf, D > 0 ? "bull" : "bear", n, 100 * w / n, p / n, r / n, n2 ? p2 / n2 : 0, n2); }
  if (argc < 2) return 0;
  std::string dir = argv[1]; mkdir(dir.c_str(), 0755); mkdir((dir + "/data").c_str(), 0755);
  FILE* js = fopen((dir + "/trades.js").c_str(), "w"); fprintf(js, "const T=[");
  for (size_t j = 0; j < T.size(); ++j) { auto& t = T[j]; fprintf(js, "%s[%d,%d,%d,\"%s\",%.2f,%.2f,%.2f,\"%s\",%.2f,%.2f,%.2f,%d,%d,%.2f,[", j ? "," : "", t.setup, t.tf, t.dir, t.et.c_str(), t.e, t.stop, t.R, t.xt.c_str(), t.x, t.pts, t.cost, t.why, t.day, t.tgt);
    for (size_t m = 0; m < t.mk.size(); ++m) fprintf(js, "%s[\"%s\",\"%s\"]", m ? "," : "", t.mk[m].first.c_str(), t.mk[m].second.c_str()); fprintf(js, "]]"); }
  fprintf(js, "];\n"); fclose(js);
  std::set<int> days; for (auto& t : T) days.insert(t.day); std::map<int, FILE*> yf; size_t nb = 0;
  for (int dd : days) { const Day& d = s.days[dayIx[dd]]; int y = dd / 10000; if (!yf.count(y)) yf[y] = fopen((dir + "/data/" + std::to_string(y) + ".csv").c_str(), "w");
    for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue; fprintf(yf[y], "%ld,%d,%d,%d,%d\n", (long)(s.t[i] / 60), (int)lround(s.o[i] * 4), (int)lround(s.h[i] * 4), (int)lround(s.l[i] * 4), (int)lround(s.c[i] * 4)); nb++; } }
  for (auto& [y, f] : yf) fclose(f); fprintf(stderr, "wrote %zu trades, %zu days, %zu bars, %zu year files\n", T.size(), days.size(), nb, yf.size());
  return 0;
}
