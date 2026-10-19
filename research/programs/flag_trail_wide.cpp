// 2026-10-19 (founder: "increase trailing, maybe"): wider trailing stops on the same entries — the NY wick-close flags of
// flag_wickentry.cpp (inputs_20261019/flag_wickentry_w1_full.csv, the measured-move rows give the entries: entry time and price, the stop,
// the pole length, chart). Each entry is re-run from its fill on 1-minute data with these exits (bars = bars of the flag's own chart):
//  MM  measured move, no trail (reference) · T1 previous bar after +1R (current trail) · T2 lowest low of the last 3 bars after +1R ·
//  T3 last 5 bars after +1R · T4 previous bar after +2R · T5 last 3 bars after +2R · T6 1R behind the best close after +1R ·
//  T7 break-even at +1R, last 3 bars after +2R · T8 measured-move target AND last 3 bars after +1.5R. Flat 15:55. Trades are scored on
// their own (no one-at-a-time), net of 2 pts cost scaled to the day's range. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <sstream>
using namespace lab;
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::map<int, int> dayIx; for (size_t i = 0; i < s.days.size(); ++i) dayIx[s.days[i].date] = i;
  struct E { int chart, dir, date; long t; double e, stop, pole; }; std::vector<E> ents;
  { std::ifstream f("research/programs/inputs_20261019/flag_wickentry_w1_full.csv"); std::string l; std::getline(f, l);
    while (std::getline(f, l)) { std::vector<std::string> c; std::stringstream ss(l); std::string x; while (std::getline(ss, x, ',')) c.push_back(x); if (c.size() < 21 || c[0] != "NY" || c[20] != "measured move") continue;
      struct tm tmv = {}; sscanf((c[4] + " " + c[5]).c_str(), "%d-%d-%d %d:%d", &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday, &tmv.tm_hour, &tmv.tm_min); tmv.tm_year -= 1900; tmv.tm_mon -= 1;
      long t = timegm(&tmv) - 60;   // the entry is the close of the minute that STARTS 1 minute before the listed time
      ents.push_back({std::stoi(c[1]), c[2] == "bull" ? 1 : -1, std::stoi(c[3]), t, std::stod(c[6]), std::stod(c[7]), std::stod(c[19])}); } }
  const int NX = 9; const char* XN[NX] = {"MM no trail (measured move)", "T1 previous bar after +1R (now)", "T2 last 3 bars after +1R", "T3 last 5 bars after +1R", "T4 previous bar after +2R", "T5 last 3 bars after +2R",
    "T6 1R behind best close after +1R", "T7 BE at +1R, last 3 bars after +2R", "T8 target + last 3 bars after +1.5R"};
  struct Acc { double n = 0, s = 0, w = 0, n2 = 0, s2 = 0, big = 0; };
  std::map<std::string, std::vector<Acc>> acc;
  for (auto& en : ents) { auto it = dayIx.find(en.date); if (it == dayIx.end()) continue; const Day& d = s.days[it->second]; const double A = d.atr; int k0 = -1;
    for (int i = d.rth0; i < d.b1; ++i) if (s.t[i] == en.t) { k0 = i; break; } if (k0 < 0) continue;
    const int D = en.dir, tf = en.chart; const double R = D * (en.e - en.stop); if (R <= 0) continue; const double cost = 2.0 * A / 320.0;
    std::string key = std::string(D > 0 ? "bull " : "bear ") + std::to_string(tf) + "m"; auto& V = acc[key]; if (V.empty()) V.resize(NX);
    for (int x = 0; x < NX; ++x) {
      double st = en.stop, tg = (x == 0 || x == 8) ? en.e + D * en.pole : NAN, best = en.e, px = NAN; std::vector<std::pair<double, double>> done;   // completed chart bars (h, l)
      double bh = -1e18, bl = 1e18;
      for (int i = k0 + 1; i < d.b1; ++i) { int tm = tday_min(s.t[i]);
        if (D > 0 ? s.l[i] <= st : s.h[i] >= st) { px = st; break; } if (!std::isnan(tg) && (D > 0 ? s.h[i] >= tg : s.l[i] <= tg)) { px = tg; break; } if (tm >= hm_to_tday(15, 55)) { px = s.c[i]; break; }
        bh = std::max(bh, (double)s.h[i]); bl = std::min(bl, (double)s.l[i]); best = D > 0 ? std::max(best, (double)s.c[i]) : std::min(best, (double)s.c[i]);
        if ((tm + 1 - hm_to_tday(9, 30)) % tf == 0) { done.push_back({bh, bl}); bh = -1e18; bl = 1e18; }
        double gain = D * (s.c[i] - en.e) / R, mfe = D * (best - en.e) / R;
        auto lastN = [&](int nb) { if ((int)done.size() < 1) return (double)NAN; double v = D > 0 ? 1e18 : -1e18; for (int j = std::max(0, (int)done.size() - nb); j < (int)done.size(); ++j) v = D > 0 ? std::min(v, done[j].second) : std::max(v, done[j].first); return D > 0 ? v - 0.25 : v + 0.25; };
        auto raise = [&](double ns) { if (std::isnan(ns)) return; st = D > 0 ? std::max(st, ns) : std::min(st, ns); };
        (void)gain;
        if (x == 1 && mfe >= 1) raise(lastN(1)); if (x == 2 && mfe >= 1) raise(lastN(3)); if (x == 3 && mfe >= 1) raise(lastN(5));
        if (x == 4 && mfe >= 2) raise(lastN(1)); if (x == 5 && mfe >= 2) raise(lastN(3)); if (x == 6 && mfe >= 1) raise(best - D * R);
        if (x == 7) { if (mfe >= 1) raise(en.e); if (mfe >= 2) raise(lastN(3)); } if (x == 8 && mfe >= 1.5) raise(lastN(3)); }
      if (std::isnan(px)) px = s.c[d.b1 - 1]; double net = (D * (px - en.e) - cost) / R; Acc& a = V[x]; a.n++; a.s += net; a.w += net > 0; a.big += net >= 2; if (en.date >= 20250101) { a.n2++; a.s2 += net; } } }
  printf("Wider trailing stops on the NY wick-close flag entries (%zu entries, 2010-07 → 2026-10). Net R per trade | win %% | share >= 2R | 2025-26 net R (n).\n", ents.size());
  for (auto& [key, V] : acc) { if (key.find(" 1m") != std::string::npos || key.find("15m") != std::string::npos) continue; printf("\n== %s\n", key.c_str());
    for (int x = 0; x < NX; ++x) { const Acc& a = V[x]; printf("  %-38s %+.3f | %4.1f%% | %4.1f%% | 2025-26 %+.3f (%.0f)\n", XN[x], a.s / a.n, 100 * a.w / a.n, 100 * a.big / a.n, a.n2 ? a.s2 / a.n2 : 0, a.n2); } }
  return 0;
}
