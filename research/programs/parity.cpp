// edge_lab's cascade trades (paper so_cascade_2r's exact rules) on the recorded sessions, for a trade-by-trade parity
// check against the paper engine's backfill.
#include "research/edge_lab.hpp"
#include <cstdio>
using namespace lab;
int main() {
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); paper::VixRegime v; build_days(s, {}, v);
  Market m(s); Evaluator ev(m);
  Spec sp = parse_spec(json::parse(R"J({"id":"p","why":"parity","from":"2026-09-18",
    "checks":[{"at":"09:50","when":"abs(px-open) > 0.25*atr","dir":"sign(px-open)"},
              {"at":"10:30","when":"abs(px-open) > 0.3*atr","dir":"sign(px-open)"},
              {"at":"11:00","when":"abs(px-open) > 0.3*atr","dir":"sign(px-open)"},
              {"at":"11:30","when":"abs(px-open) > 0.3*atr","dir":"sign(px-open)"},
              {"at":"12:00","when":"abs(px-open) > 0","dir":"sign(px-open)"}],
    "stop":"0.25*atr","target":"2","flat":"15:55"})J"));
  Variant vr; for (auto& c : sp.checks) { vr.checks.emplace_back(parse(c.when), parse(c.dir)); ev.bind(*vr.checks.back().first); ev.bind(*vr.checks.back().second); }
  vr.stop = parse(sp.stop); vr.target = parse(sp.target); ev.bind(*vr.stop); ev.bind(*vr.target);
  for (auto& t : run_variant(ev, s, sp, vr, 0)) {
    auto hm = [&](int i) { int m2 = (int)((s.t[i] % 86400) / 60); char b[8]; snprintf(b, 8, "%02d:%02d", m2 / 60, m2 % 60); return std::string(b); };
    printf("%d %-5s %s %.2f → %s %+.1f pts (stop %.1f, ATR %.1f)\n", s.days[t.day].date, t.dir > 0 ? "LONG" : "SHORT", hm(t.e).c_str(), s.o[t.e], hm(t.x).c_str(), t.pts, t.stop, s.days[t.day].atr);
  }
}
