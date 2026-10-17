
// ───────────────────────────── meta / exits (meta.hpp) ─────────────────────────────
struct Base { Market* m = nullptr; std::vector<Variant> vs; WfOut w; double cost = 1, pv = 20; };

Base prepare(Ctx& c, const Spec& sp) {
    Base b;
    b.m = get_market(c, sp.symbol);
    if (!b.m) throw std::runtime_error("no market data for " + sp.symbol);
    b.cost = sp.cost.count(sp.symbol) ? sp.cost.at(sp.symbol) : 1.0;
    b.pv = sp.symbol == "ES" ? 50 : sp.symbol == "NQ" ? 20 : 1;
    Evaluator ev(*b.m);
    b.vs = compile(sp, ev);
    auto vt = run_all(c, *b.m, sp, b.vs, 0);
    b.w = walk(b.m->s(), vt, b.cost, sp.train_months, std::max(5, sp.min_trades / 20));
    return b;
}

struct Line { int n = 0; double net = 0, avg = 0, pf = 0, win = 0, dd = 0; };
Line line_of(const std::vector<double>& net) {
    Line L; Stat st = stat_of(net); L.n = st.n; L.net = st.net; L.avg = st.avg; L.pf = st.pf; L.win = st.win;
    double eq = 0, pk = 0; for (double x : net) { eq += x; pk = std::max(pk, eq); L.dd = std::max(L.dd, pk - eq); }
    return L;
}
std::string line_row(const std::string& name, const Line& L, double pv) {
    return "| " + name + " | " + std::to_string(L.n) + " | " + sg(L.avg) + " | $" + f2(L.avg * pv, 0) + " | " + f2(L.pf) + " | " +
           f2(100 * L.win, 0) + "% | " + sg(L.net, 0) + " | " + f2(L.dd, 0) + " |\n";
}
const char* line_head() { return "| | trades | net pts/trade | $/NQ/trade | PF | win | net pts | max drawdown pts |\n|---|---|---|---|---|---|---|---|\n"; }

std::pair<std::vector<PropOut>, double> props_of(const Series& s, const std::vector<Trade>& tr, const Spec& sp) {
    std::vector<PropOut> out; double best = 0;
    for (int q : sp.prop_sizes) { out.push_back(prop_chain(s, tr, sp.prop_plan, q, sp.mnq_cost)); best = std::max(best, out.back().be_fee); }
    return {out, best};
}
void prop_table(std::ostringstream& o, const std::string& who, const std::vector<PropOut>& P) {
    for (auto& p : P)
        o << "| " << who << " | " << p.size << " | " << p.evals << " | " << p.passes << " | " << p.payouts << " | "
          << f2(p.years > 0 ? p.paid / p.years : 0, 0) << " | " << f2(p.be_fee, 0) << " |\n";
}

json run_meta(Ctx& c, const Spec& sp, std::string& md) {
    auto t0 = std::chrono::steady_clock::now();
    Base b = prepare(c, sp);
    const Series& s = b.m->s();
    Market* esm = sp.confirm.empty() || sp.confirm == sp.symbol ? nullptr : get_market(c, sp.confirm);
    MetaData d = meta_data(*b.m, esm ? &esm->s() : nullptr, b.w.trades, b.cost);
    const auto& FN = meta_feature_names();
    auto gfit = [](const std::vector<std::vector<float>>& X, const std::vector<double>& y) { return gbm_fit(X, y); };
    auto rfit = [](const std::vector<std::vector<float>>& X, const std::vector<double>& y) { return ridge_fit(X, y); };
    const int MINY = 3;
    std::vector<double> pg = meta_walk(d, d.y, MINY, gfit), pr = meta_walk(d, d.y, MINY, rfit);

    std::vector<int> T;   // tested trades (a prediction exists)
    for (size_t i = 0; i < d.tr.size(); ++i) if (!std::isnan(pg[i])) T.push_back((int)i);
    if (T.empty()) throw std::runtime_error(sp.id + ": not enough history for the meta walk-forward");
    auto net = [&](int i) { return d.tr[i].pts - b.cost; };
    auto pick = [&](const std::vector<double>& p) { std::vector<int> k; for (int i : T) if (p[i] > 0) k.push_back(i); return k; };
    auto nets = [&](const std::vector<int>& idx) { std::vector<double> v; for (int i : idx) v.push_back(net(i)); return v; };
    std::vector<int> K = pick(pg), KR = pick(pr);
    Line Lb = line_of(nets(T)), Lm = line_of(nets(K)), Lr = line_of(nets(KR));

    // random skip: same number of trades skipped per year
    std::map<int, std::vector<int>> by_year; std::map<int, int> keep_y;
    for (int i : T) by_year[d.year[i]].push_back(i);
    for (int i : K) ++keep_y[d.year[i]];
    const int SK = 1000; int beat_skip = 0; double skip_mean = 0;
    for (int k = 0; k < SK; ++k) {
        std::mt19937_64 rng(7919ULL * (k + 1));
        double n = 0;
        for (auto& [y, idx] : by_year) {
            std::vector<int> v = idx; std::shuffle(v.begin(), v.end(), rng);
            for (int j = 0; j < keep_y[y]; ++j) n += net(v[j]);
        }
        skip_mean += n / SK;
        if (Lm.net > n + 1e-6) ++beat_skip;
    }
    // shuffled labels: results shuffled within each year, the whole walk-forward refit
    const int SL = 100; std::vector<double> lab_net(SL, 0);
    {
        std::atomic<int> nx{0}; std::vector<std::thread> th;
        for (int k = 0; k < std::max(1, c.threads); ++k) th.emplace_back([&] {
            for (int sd; (sd = nx++) < SL;) {
                std::mt19937_64 rng(104729ULL * (sd + 1));
                std::vector<double> ys = d.y;
                std::map<int, std::vector<int>> yi; for (size_t i = 0; i < d.y.size(); ++i) yi[d.year[i]].push_back((int)i);
                for (auto& [y, idx] : yi) { std::vector<int> p = idx; std::shuffle(p.begin(), p.end(), rng); for (size_t j = 0; j < idx.size(); ++j) ys[idx[j]] = d.y[p[j]]; }
                std::vector<double> ps = meta_walk(d, ys, MINY, gfit);
                double n = 0; for (int i : T) if (ps[i] > 0) n += net(i);
                lab_net[sd] = n;
            }
        });
        for (auto& t : th) t.join();
    }
    int beat_lab = 0; double lab_mean = 0;
    for (double n : lab_net) { lab_mean += n / SL; if (Lm.net > n + 1e-6) ++beat_lab; }
    const double p_skip = (double)beat_skip / SK, p_lab = (double)beat_lab / SL;

    // eras: improvement per trade
    double eb[3] = {0, 0, 0}, em[3] = {0, 0, 0}; int nb[3] = {0, 0, 0}, nm[3] = {0, 0, 0};
    for (int i : T) { int e = era_of(s.days[d.tr[i].day].date); eb[e] += net(i); ++nb[e]; }
    for (int i : K) { int e = era_of(s.days[d.tr[i].day].date); em[e] += net(i); ++nm[e]; }
    int eras_up = 0, eras_ran = 0; bool latest_up = false;
    for (int e = 0; e < 3; ++e) if (nb[e] && nm[e]) { ++eras_ran; bool up = em[e] / nm[e] > eb[e] / nb[e]; eras_up += up; if (e == 2) latest_up = up; }

    // prop
    std::vector<Trade> tb, tm; for (int i : T) tb.push_back(d.tr[i]); for (int i : K) tm.push_back(d.tr[i]);
    auto [pb, beb] = props_of(s, tb, sp);
    auto [pm, bem] = props_of(s, tm, sp);

    // prediction quintiles (out of sample): does a higher prediction mean a better trade?
    std::vector<int> ord = T; std::sort(ord.begin(), ord.end(), [&](int a, int z) { return pg[a] < pg[z]; });
    // final model on everything, for the importance table only
    Gbm full = gbm_fit(d.X, d.y);
    double gsum = 0; for (double g : full.gain) gsum += g;
    std::vector<int> fo(FN.size()); std::iota(fo.begin(), fo.end(), 0);
    std::sort(fo.begin(), fo.end(), [&](int a, int z) { return full.gain[a] > full.gain[z]; });

    const bool g1 = Lm.n >= 100 && Lm.avg > Lb.avg && Lm.net > 0 && Lm.pf >= 1.05;
    const bool g2 = p_skip >= 0.95, g3 = p_lab >= 0.95;
    const bool g4 = latest_up && eras_up >= 2 && Lr.avg > Lb.avg;
    const bool g5 = bem > beb && bem > sp.prop_fee;
    const bool gates[5] = {g1, g2, g3, g4, g5};
    static const char* gname[5] = {"IMPROVES", "RANDOM SKIP", "SHUFFLED LABELS", "ROBUSTNESS", "PROP FIT"};
    int stage = 5; for (int k = 0; k < 5; ++k) if (!gates[k]) { stage = k; break; }
    const std::string verdict = stage == 5 ? "PASS" : std::string("FAIL at ") + gname[stage];

    std::ostringstream o;
    o << "# Meta-label (take / skip) on " << sp.name << " (`" << sp.id << "__meta`)\n\n";
    o << "The base trades are the idea's walk-forward trades. For every year from the " << (MINY + 1) << "th on, a model trained only on\n"
         "EARLIER years predicts each signal's result in R from the context at the signal; the signal is taken when the\n"
         "prediction is > 0 (fixed in advance). Primary model: boosted depth-2 trees (150 rounds, shrink 0.05, ≥60 trades a\n"
         "leaf); cross-check: ridge regression. A skipped signal skips the day (the later checks are not re-run).\n\n";
    o << "## Verdict: **" << verdict << "**\n\n| gate | result |\n|---|---|\n";
    o << "| 1 Improves | " << (g1 ? "pass" : "FAIL") << " — taken " << Lm.n << " of " << Lb.n << ": " << sg(Lm.avg) << " vs " << sg(Lb.avg) << " pts/trade, PF " << f2(Lm.pf) << " vs " << f2(Lb.pf) << " |\n";
    o << "| 2 Random skip | " << (g2 ? "pass" : "FAIL") << " — beats skipping the same number per year at random in " << f2(100 * p_skip, 1) << "% of " << SK << " seeds (need 95%); random mean " << sg(skip_mean, 0) << " pts vs " << sg(Lm.net, 0) << " |\n";
    o << "| 3 Shuffled labels | " << (g3 ? "pass" : "FAIL") << " — beats the same model trained on shuffled results in " << f2(100 * p_lab, 0) << "% of " << SL << " seeds (need 95%); shuffled mean " << sg(lab_mean, 0) << " pts |\n";
    o << "| 4 Robustness | " << (g4 ? "pass" : "FAIL") << " — better per trade in " << eras_up << "/" << eras_ran << " eras (latest: " << (latest_up ? "yes" : "NO") << "); ridge cross-check " << sg(Lr.avg) << " vs base " << sg(Lb.avg) << " |\n";
    o << "| 5 Prop fit | " << (g5 ? "pass" : "FAIL") << " — best break-even eval fee $" << f2(bem, 0) << " vs base $" << f2(beb, 0) << " (need above both base and $" << f2(sp.prop_fee, 0) << ") |\n\n";
    o << "## Out-of-sample " << d.year[T.front()] << "–" << d.year[T.back()] << " (" << sp.symbol << ", real cost " << f2(b.cost) << " pts)\n\n" << line_head();
    o << line_row("base (every signal)", Lb, b.pv) << line_row("meta trees (take if > 0)", Lm, b.pv) << line_row("meta ridge (cross-check)", Lr, b.pv);
    o << "\n## Per year\n\n| year | base n | base net pts | meta n | meta net pts |\n|---|---|---|---|---|\n";
    for (auto& [y, idx] : by_year) {
        double nbs = 0, nms = 0; int nn = 0;
        for (int i : idx) { nbs += net(i); if (pg[i] > 0) { nms += net(i); ++nn; } }
        o << "| " << y << " | " << idx.size() << " | " << sg(nbs, 0) << " | " << nn << " | " << sg(nms, 0) << " |\n";
    }
    o << "\n## Eras (net pts/trade)\n\n| era | base | meta |\n|---|---|---|\n";
    for (int e = 0; e < 3; ++e) o << "| " << era_name(e) << " | " << (nb[e] ? sg(eb[e] / nb[e]) + " (n " + std::to_string(nb[e]) + ")" : "—") << " | " << (nm[e] ? sg(em[e] / nm[e]) + " (n " + std::to_string(nm[e]) + ")" : "—") << " |\n";
    o << "\n## Prediction quintiles (out of sample, lowest → highest prediction)\n\n| quintile | trades | net pts/trade | win |\n|---|---|---|---|\n";
    for (int q = 0; q < 5; ++q) {
        size_t a = ord.size() * q / 5, z = ord.size() * (q + 1) / 5;
        std::vector<double> v; for (size_t j = a; j < z; ++j) v.push_back(net(ord[j]));
        Stat st = stat_of(v);
        o << "| " << (q + 1) << " | " << st.n << " | " << sg(st.avg) << " | " << f2(100 * st.win, 0) << "% |\n";
    }
    o << "\n## What the model leans on (split gain, model fit on all years — description only)\n\n| feature | share |\n|---|---|\n";
    for (size_t k = 0; k < fo.size() && k < 10; ++k) o << "| " << FN[fo[k]] << " | " << f2(gsum > 0 ? 100 * full.gain[fo[k]] / gsum : 0, 1) << "% |\n";
    o << "\n## Prop (" << sp.prop_plan << ", MNQ at " << f2(sp.mnq_cost) << " pts)\n\n| trades | MNQ | evals | passed | payouts | paid $/yr | break-even fee |\n|---|---|---|---|---|---|---|\n";
    prop_table(o, "base", pb); prop_table(o, "meta", pm);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    o << "\n_edge_lab meta run in " << f2(secs, 1) << " s_\n";
    md = o.str();

    json R; R["id"] = sp.id + "__meta"; R["name"] = "Meta-label: " + sp.name; R["verdict"] = verdict; R["stage_reached"] = stage; R["quick"] = false;
    R["oos"] = {{"n", Lm.n}, {"avg_pts", Lm.avg}, {"pf", Lm.pf}, {"base_n", Lb.n}, {"base_avg_pts", Lb.avg}, {"base_pf", Lb.pf}, {"ridge_avg_pts", Lr.avg}};
    R["controls"] = {{"random_skip_pct", p_skip}, {"shuffled_label_pct", p_lab}};
    R["prop"] = {{"meta_be_fee", bem}, {"base_be_fee", beb}};
    R["seconds"] = secs;
    return R;
}

json run_exits(Ctx& c, const Spec& sp, std::string& md) {
    auto t0 = std::chrono::steady_clock::now();
    Base b = prepare(c, sp);
    const Series& s = b.m->s();
    const auto& tr = b.w.trades;
    if (tr.empty()) throw std::runtime_error(sp.id + ": no walk-forward trades");
    ExitSim sim(*b.m);
    std::vector<std::vector<ExitOut>> res(X__COUNT);
    for (int r = 0; r < X__COUNT; ++r) for (auto& t : tr) res[r].push_back(sim.run(t, r));
    int mismatch = 0; for (size_t i = 0; i < tr.size(); ++i) if (std::fabs(res[X_BASE][i].pts - tr[i].pts) > 1e-6) ++mismatch;
    auto nets = [&](int r) { std::vector<double> v; for (auto& x : res[r]) v.push_back(x.pts - b.cost); return v; };
    auto as_trades = [&](int r) { std::vector<Trade> v = tr; for (size_t i = 0; i < v.size(); ++i) { v[i].x = res[r][i].x; v[i].pts = res[r][i].pts; v[i].mae = res[r][i].mae; v[i].mfe = res[r][i].mfe; } return v; };
    Line L[X__COUNT]; for (int r = 0; r < X__COUNT; ++r) L[r] = line_of(nets(r));
    auto [pbase, bebase] = props_of(s, tr, sp);

    struct RuleOut { double p_time = 0, twin_mean = 0; int fired = 0, eras_up = 0, eras_ran = 0; bool latest_up = false; double be = 0; bool pass = false; std::string why; };
    std::vector<RuleOut> ro(X__COUNT);
    double eb[3] = {0, 0, 0}; int nb[3] = {0, 0, 0};
    for (size_t i = 0; i < tr.size(); ++i) { int e = era_of(s.days[tr[i].day].date); eb[e] += res[X_BASE][i].pts - b.cost; ++nb[e]; }
    const double NEED = 0.99;   // 7 exit rules are tested → the timing control must clear 99 %, not 95 %
    const int SEEDS = 200;
    for (int r = 1; r < X__COUNT; ++r) {
        RuleOut& q = ro[r];
        std::vector<int> durs; std::vector<size_t> fired;
        for (size_t i = 0; i < tr.size(); ++i) if (res[r][i].thesis) { durs.push_back(res[r][i].x - tr[i].e); fired.push_back(i); }
        q.fired = (int)fired.size();
        int beat = 0;
        for (int k = 0; k < SEEDS && !durs.empty(); ++k) {
            std::mt19937_64 rng(15485863ULL * (k + 1) + r);
            double n = L[r].net;
            for (size_t i : fired) {
                int at = tr[i].e + std::max(1, durs[rng() % durs.size()]);
                ExitOut z = sim.run(tr[i], r, at);
                n += (z.pts - b.cost) - (res[r][i].pts - b.cost);
            }
            q.twin_mean += n / SEEDS;
            if (L[r].net > n + 1e-6) ++beat;
        }
        q.p_time = (double)beat / SEEDS;
        double em[3] = {0, 0, 0}; int nm[3] = {0, 0, 0};
        for (size_t i = 0; i < tr.size(); ++i) { int e = era_of(s.days[tr[i].day].date); em[e] += res[r][i].pts - b.cost; ++nm[e]; }
        for (int e = 0; e < 3; ++e) if (nb[e]) { ++q.eras_ran; bool up = em[e] / nm[e] > eb[e] / nb[e]; q.eras_up += up; if (e == 2) q.latest_up = up; }
        q.be = props_of(s, as_trades(r), sp).second;
        const bool x1 = L[r].avg > L[X_BASE].avg && L[r].net > 0, x2 = q.p_time >= NEED, x3 = q.latest_up && q.eras_up >= 2, x4 = q.be > bebase && q.be > sp.prop_fee;
        q.pass = x1 && x2 && x3 && x4;
        q.why = q.pass ? "PASS" : !x1 ? "FAIL: not better than base" : !x2 ? "FAIL: timing control" : !x3 ? "FAIL: eras" : "FAIL: prop fit";
    }
    // walk-forward over the exit rules: each month uses the rule best over the previous train_months
    int lo = INT32_MAX, hi = INT32_MIN;
    for (auto& t : tr) { int m = month_idx(s.days[t.day].date); lo = std::min(lo, m); hi = std::max(hi, m); }
    wf::Group g; g.key = "exits";
    for (int r = 0; r < X__COUNT; ++r) {
        g.variants.push_back(exit_key(r)); g.cells.emplace_back(hi - lo + 1);
        for (size_t i = 0; i < tr.size(); ++i) { auto& cl = g.cells[r][month_idx(s.days[tr[i].day].date) - lo]; ++cl.n; cl.net += res[r][i].pts - b.cost; }
    }
    wf::Summary ws = wf::walk_forward(g, hi - lo + 1, sp.train_months, 5);
    double base_same = 0; int base_n = 0;
    for (auto& p : ws.picks) { base_same += g.cells[X_BASE][p.month].net; base_n += g.cells[X_BASE][p.month].n; }
    std::map<std::string, int> pick_count; for (auto& p : ws.picks) ++pick_count[exit_key(p.variant)];

    int best = -1; for (int r = 1; r < X__COUNT; ++r) if (ro[r].pass && (best < 0 || L[r].avg > L[best].avg)) best = r;
    const std::string verdict = best >= 0 ? std::string("PASS (") + exit_key(best) + ")" : "FAIL — no exit rule passes";

    std::ostringstream o;
    o << "# Thesis exits on " << sp.name << " (`" << sp.id << "__exits`)\n\n";
    o << "Same entries as the idea's walk-forward trades (" << tr.size() << "), different exits. The hard stop always stays; the thesis\n"
         "checks run on completed 5-minute bars (15-minute for the trail) and fill at the next bar's open. A rule passes when it\n"
         "beats the base exit per trade, beats the random-timing twin (its own holding times shuffled across the trades it\n"
         "exited) in ≥ 99 % of " << SEEDS << " seeds (7 rules tested), is better in ≥ 2 of 3 eras including 2023–26, and raises the prop\n"
         "break-even fee. Base reproduces the walk-forward trades exactly: " << (mismatch ? "NO (" + std::to_string(mismatch) + " differ)" : "yes") << ".\n\n";
    o << "## Verdict: **" << verdict << "**\n\n" << line_head();
    for (int r = 0; r < X__COUNT; ++r) o << line_row(exit_name(r), L[r], b.pv);
    o << "\n## Gates per rule\n\n| rule | thesis exits | timing control | twin mean pts | eras better | latest era | break-even fee | verdict |\n|---|---|---|---|---|---|---|---|\n";
    o << "| base | — | — | — | — | — | $" << f2(bebase, 0) << " | — |\n";
    for (int r = 1; r < X__COUNT; ++r) {
        auto& q = ro[r];
        o << "| " << exit_key(r) << " | " << q.fired << " | " << f2(100 * q.p_time, 1) << "% | " << sg(q.twin_mean, 0) << " vs " << sg(L[r].net, 0) << " | "
          << q.eras_up << "/" << q.eras_ran << " | " << (q.latest_up ? "yes" : "no") << " | $" << f2(q.be, 0) << " | " << q.why << " |\n";
    }
    o << "\n## Walk-forward over the rules (each month: the rule best over the previous " << sp.train_months << " months)\n\n";
    o << "- " << ws.oos_trades << " trades, net " << sg(ws.oos_net, 0) << " pts (" << sg(ws.oos_trades ? ws.oos_net / ws.oos_trades : 0) << "/trade) vs base in the same months "
      << sg(base_same, 0) << " pts (" << sg(base_n ? base_same / base_n : 0) << "/trade)\n- months picking each rule:";
    for (auto& [k, n] : pick_count) o << " " << k << " " << n << ";";
    o << "\n\n## Eras (net pts/trade)\n\n| rule | 2010-18 | 2019-22 | 2023-26 |\n|---|---|---|---|\n";
    for (int r = 0; r < X__COUNT; ++r) {
        double em[3] = {0, 0, 0}; int nm[3] = {0, 0, 0};
        for (size_t i = 0; i < tr.size(); ++i) { int e = era_of(s.days[tr[i].day].date); em[e] += res[r][i].pts - b.cost; ++nm[e]; }
        o << "| " << exit_key(r); for (int e = 0; e < 3; ++e) o << " | " << (nm[e] ? sg(em[e] / nm[e]) : "—"); o << " |\n";
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    o << "\n_edge_lab exits run in " << f2(secs, 1) << " s_\n";
    md = o.str();

    json R; R["id"] = sp.id + "__exits"; R["name"] = "Thesis exits: " + sp.name; R["verdict"] = verdict; R["stage_reached"] = best >= 0 ? 5 : 0; R["quick"] = false;
    const int rep = best >= 0 ? best : X_BASE;
    R["oos"] = {{"n", L[rep].n}, {"avg_pts", L[rep].avg}, {"pf", L[rep].pf}, {"base_avg_pts", L[X_BASE].avg}, {"rule", exit_key(rep)}};
    json rules = json::array();
    for (int r = 1; r < X__COUNT; ++r) rules.push_back({{"rule", exit_key(r)}, {"avg_pts", L[r].avg}, {"pf", L[r].pf}, {"timing_pct", ro[r].p_time}, {"be_fee", ro[r].be}, {"verdict", ro[r].why}});
    R["rules"] = rules; R["prop"] = {{"base_be_fee", bebase}}; R["seconds"] = secs;
    return R;
}
