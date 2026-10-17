// Walk-forward trades of an edge_lab idea spec — the same variant compilation as edge_lab_main.cpp compile()
// (fixed-time checks AND scan specs, plus break-even / partial / add / trail nodes).
static std::vector<Trade> wf_trades(Market& m, Evaluator& ev, const char* path) {
  std::ifstream f(path); Spec sp = parse_spec(json::parse(f)); std::vector<std::vector<Trade>> vt;
  for (auto& p : grid(sp)) {
    Variant v; v.p = p;
    for (auto& c : sp.checks) { v.checks.emplace_back(parse(c.when, p), parse(c.dir, p)); ev.bind(*v.checks.back().first); ev.bind(*v.checks.back().second); }
    if (sp.scan) { v.scan_when = parse(sp.scan_when, p); v.scan_dir = parse(sp.scan_dir, p); ev.bind(*v.scan_when); ev.bind(*v.scan_dir); }
    v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); ev.bind(*v.stop); ev.bind(*v.target);
    v.be = parse(sp.be_r, p); v.part = parse(sp.partial_r, p); ev.bind(*v.be); ev.bind(*v.part);
    v.add = parse(sp.add_r, p); v.trail = parse(sp.trail_r, p); ev.bind(*v.add); ev.bind(*v.trail);
    vt.push_back(run_variant(ev, m.s(), sp, v, 0));
  }
  return walk(m.s(), vt, 1.0, sp.train_months, std::max(5, sp.min_trades / 20)).trades;
}
