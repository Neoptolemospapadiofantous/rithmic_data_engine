#include "research/edge_lab.hpp"
#include <cstdio>
using namespace lab;
int main() { Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); paper::VixRegime v; build_days(s, {}, v);
  for (auto& d : s.days) if (d.ok && !std::isnan(d.pc) && d.atr > 0 && std::fabs(d.ro - d.pc) > 3 * d.atr)
    printf("%d prior close %.2f → open %.2f (%.1f ATR, ATR %.1f) rollwin %d\n", d.date, d.pc, d.ro, (d.ro - d.pc) / d.atr, d.atr, d.rollwin); }
