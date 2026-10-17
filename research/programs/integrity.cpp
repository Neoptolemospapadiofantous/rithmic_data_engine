// Data integrity of the edge_lab cache: per symbol — trading days, full RTH sessions, the 09:30 bar present year-round
// (DST), missing / duplicate minutes inside RTH, non-monotonic time, OHLC sanity, ATR range, roll-day jumps.
#include "research/edge_lab.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
int main(int argc, char** argv) {
  for (int a = 1; a < argc; ++a) {
    Series s; std::string err; if (!load_raw("data/edge_lab/cache", argv[a], s, err)) { printf("%s: %s\n", argv[a], err.c_str()); continue; }
    paper::VixRegime vix; build_days(s, {}, vix);
    long dup = 0, back = 0, badohlc = 0; for (size_t i = 1; i < s.size(); ++i) { if (s.t[i] == s.t[i-1]) ++dup; else if (s.t[i] < s.t[i-1]) ++back; }
    for (size_t i = 0; i < s.size(); ++i) if (!(s.h[i] >= std::max(s.o[i], s.c[i]) - 1e-6 && s.l[i] <= std::min(s.o[i], s.c[i]) + 1e-6)) ++badohlc;
    int days = 0, ok = 0, open_ok = 0, open_ok_dst = 0, n_dst = 0, short_rth = 0; long missing = 0; double amin = 1e18, amax = 0; int bigjump = 0, bigjump_roll = 0;
    for (size_t k = 0; k < s.days.size(); ++k) {
      const Day& d = s.days[k]; ++days; if (!d.ok) { if (d.rth0 >= 0) ++short_rth; continue; } ++ok;
      const bool dst = (d.date % 10000) > 315 && (d.date % 10000) < 1101;     // rough: DST months
      if (tday_min(s.t[d.rth0]) == kRthOpen) { ++open_ok; if (dst) ++open_ok_dst; } if (dst) ++n_dst;
      missing += std::max(0, 390 - (d.rth1 - d.rth0));
      if (!std::isnan(d.atr)) { amin = std::min(amin, d.atr); amax = std::max(amax, d.atr); }
      if (!std::isnan(d.pc) && d.atr > 0 && std::fabs(d.ro - d.pc) > 3 * d.atr) { ++bigjump; if (d.rollwin) ++bigjump_roll; }
    }
    printf("%-8s bars %zu | days %d, full RTH %d (short %d) | 09:30 bar on full days %d/%d (DST months %d/%d) | RTH minutes missing on full days %ld (%.2f/day) | dup %ld, out-of-order %ld, bad OHLC %ld | ATR14 %.1f…%.1f | open-vs-prior-close gaps > 3 ATR: %d (%d inside the roll window)\n",
           argv[a], s.size(), days, ok, short_rth, open_ok, ok, open_ok_dst, n_dst, missing, ok ? (double)missing / ok : 0, dup, back, badohlc, amin, amax, bigjump, bigjump_roll);
  }
}
