// 2026-10-17 "can we see if it is trending, mean-reverting or choppy — accurately?"
// For every NQ session 2010-07 → 2026-10 (roll windows out) and decision times 09:30 / 10:00 / 10:30 / 11:00:
//   what the REST of the session (T → 15:55) did, labelled from its own path:
//     eff = |close − price_T| / (high − low after T)
//     CONTINUE  eff ≥ 0.5 and it moved the way the session had moved from the 09:30 open to T
//     REVERSE   eff ≥ 0.5 and it moved the other way
//     CHOP      eff < 0.5 (went nowhere relative to its own range)
//   (at 09:30 there is no morning move yet: the "direction" is the gap's.)
// Rules, fixed in advance (not searched): |move from open| in ATR, morning efficiency, overnight range, gap, weak day + VIX.
// Accuracy = how often the rule's call matches the label, against the base rate (always calling the most common label).
#include "research/bars.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
using namespace lab;

static double px_at(const Series& s, const Day& D, int tm) {
  int i = first_bar_at(s, D, tm, D.b0); if (i >= D.b1) return s.c[D.b1 - 1]; return i > D.b0 ? s.c[i - 1] : s.o[i]; }
static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : 2; }
enum { CONT, REV, CHOP };
static const char* LN[] = {"continue", "reverse", "chop"};

struct Obs { int date, label; double move, meff, onr, gap, rest_atr; bool weakvix; };

int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int times[] = {hm_to_tday(9, 30), hm_to_tday(10, 0), hm_to_tday(10, 30), hm_to_tday(11, 0)};
  const char* tn[] = {"09:30", "10:00", "10:30", "11:00"};
  for (int ti = 0; ti < 4; ++ti) {
    const int T = times[ti], END = hm_to_tday(15, 55);
    std::vector<Obs> obs;
    for (auto& d : s.days) {
      if (!d.ok || d.rollwin || !(d.atr > 0) || std::isnan(d.pc) || d.date > 20261016) continue;
      const double pT = ti == 0 ? d.ro : px_at(s, d, T), pE = px_at(s, d, END);
      double hi = -1e18, lo = 1e18, mh = -1e18, ml = 1e18, oh = -1e18, ol = 1e18;
      for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]);
        if (tm < kRthOpen) { oh = std::max(oh, (double)s.h[i]); ol = std::min(ol, (double)s.l[i]); }
        else if (tm < T) { mh = std::max(mh, (double)s.h[i]); ml = std::min(ml, (double)s.l[i]); }
        else if (tm < END) { hi = std::max(hi, (double)s.h[i]); lo = std::min(lo, (double)s.l[i]); } }
      if (!(hi > lo)) continue;
      const double gap = (d.ro - d.pc) / d.atr, move = ti == 0 ? gap : (pT - d.ro) / d.atr;
      const int dir = move > 0 ? 1 : move < 0 ? -1 : 0;
      const double eff = std::fabs(pE - pT) / (hi - lo);
      int lab = eff < 0.5 ? CHOP : (dir != 0 && (pE - pT) * dir > 0) ? CONT : REV;
      if (dir == 0 && eff >= 0.5) lab = CONT;
      Obs o{d.date, lab, move, ti == 0 ? NAN : (mh > ml ? std::fabs(pT - d.ro) / (mh - ml) : 0), oh > ol ? (oh - ol) / d.atr : NAN, gap,
            dir * (pE - pT) / d.atr, d.vix >= 20 && (d.pibs <= 0.3 || d.rsi2 <= 20)};
      obs.push_back(o);
    }
    int base[3] = {0, 0, 0}; for (auto& o : obs) ++base[o.label];
    const int N = obs.size();
    printf("\n══ decision at %s — %d sessions. What the rest of the day did: continue %.0f%%  reverse %.0f%%  chop %.0f%%\n",
           tn[ti], N, 100.0 * base[CONT] / N, 100.0 * base[REV] / N, 100.0 * base[CHOP] / N);
    const int most = std::max_element(base, base + 3) - base;
    printf("   always guessing the most common (%s) is right %.0f%% of the time — the bar any rule must clear\n", LN[most], 100.0 * base[most] / N);
    // bucket table: share of each outcome, and the mean continuation in ATR, per bucket of one feature
    auto table = [&](const char* title, std::function<int(const Obs&)> bucket, std::vector<const char*> names) {
      printf("   %s\n", title);
      for (size_t b = 0; b < names.size(); ++b) {
        int n = 0, c[3] = {0, 0, 0}, ce[3][3] = {}; double rest = 0; int ne[3] = {0, 0, 0};
        for (auto& o : obs) if (bucket(o) == (int)b) { ++n; ++c[o.label]; rest += o.rest_atr; ++ce[era(o.date)][o.label]; ++ne[era(o.date)]; }
        if (!n) continue;
        printf("     %-26s n %4d  continue %3.0f%%  reverse %3.0f%%  chop %3.0f%%  | rest of day %+.2f ATR in the morning's direction | continue by era %2.0f/%2.0f/%2.0f%%\n",
               names[b], n, 100.0 * c[CONT] / n, 100.0 * c[REV] / n, 100.0 * c[CHOP] / n, rest / n,
               ne[0] ? 100.0 * ce[0][CONT] / ne[0] : NAN, ne[1] ? 100.0 * ce[1][CONT] / ne[1] : NAN, ne[2] ? 100.0 * ce[2][CONT] / ne[2] : NAN);
      }
    };
    if (ti > 0) {
      table("by how far price is from the 09:30 open (ATR):", [](const Obs& o) { double a = std::fabs(o.move); return a < 0.15 ? 0 : a < 0.3 ? 1 : a < 0.5 ? 2 : 3; },
            {"< 0.15 (flat)", "0.15–0.30", "0.30–0.50", "≥ 0.50 (strong)"});
      table("by morning efficiency (|move| / morning range):", [](const Obs& o) { return o.meff < 0.3 ? 0 : o.meff < 0.6 ? 1 : 2; },
            {"< 0.3 (two-sided)", "0.3–0.6", "≥ 0.6 (one-way)"});
    }
    table("by overnight range (18:00–09:29, ATR):", [](const Obs& o) { return std::isnan(o.onr) ? -1 : o.onr < 0.4 ? 0 : o.onr < 0.8 ? 1 : 2; },
          {"< 0.4 (quiet)", "0.4–0.8", "≥ 0.8 (wide)"});
    table("by weak day + VIX ≥ 20 (the mean-reversion setup):", [](const Obs& o) { return o.weakvix ? 1 : 0; }, {"not a weak-day setup", "weak day + VIX ≥ 20"});
    // fixed rules scored: call CONTINUE when the rule fires, CHOP otherwise (the "don't trade" call)
    auto score = [&](const char* name, std::function<bool(const Obs&)> fire) {
      int fired = 0, right_cont = 0, notf = 0, right_chop = 0, nonrev = 0;
      for (auto& o : obs) { if (fire(o)) { ++fired; right_cont += o.label == CONT; nonrev += o.label != REV; } else { ++notf; right_chop += o.label == CHOP; } }
      const int acc = right_cont + right_chop;
      printf("   rule %-38s fires %4.0f%% of days · when it says TREND it continues %3.0f%% (doesn't reverse %3.0f%%) · when it says CHOP it is chop %3.0f%% · overall right %3.0f%% (base %.0f%%)\n",
             name, 100.0 * fired / N, fired ? 100.0 * right_cont / fired : NAN, fired ? 100.0 * nonrev / fired : NAN, notf ? 100.0 * right_chop / notf : NAN, 100.0 * acc / N, 100.0 * base[most] / N);
    };
    if (ti > 0) {
      score("|move| ≥ 0.30 ATR (the cascade's test)", [](const Obs& o) { return std::fabs(o.move) >= 0.3; });
      score("|move| ≥ 0.50 ATR", [](const Obs& o) { return std::fabs(o.move) >= 0.5; });
      score("|move| ≥ 0.30 and efficiency ≥ 0.6", [](const Obs& o) { return std::fabs(o.move) >= 0.3 && o.meff >= 0.6; });
    } else {
      score("|gap| ≥ 0.3 ATR", [](const Obs& o) { return std::fabs(o.gap) >= 0.3; });
      score("overnight range ≥ 0.8 ATR", [](const Obs& o) { return o.onr >= 0.8; });
    }
  }
}
