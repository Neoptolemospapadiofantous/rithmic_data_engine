// edge_lab — runs strategy ideas (research/ideas/*.json) through the five gates in edge_lab.hpp and writes one
// report card per idea (data/edge_lab/reports/<id>.md + .json) and a row per run in data/edge_lab/results.csv
// (scripts/edge_lab.sh loads it into research_ideas / research_runs).
//
//   edge_lab run research/ideas/x.json [more.json …] [--data DIR] [--out DIR] [--threads N] [--quick]
//   --quick skips controls, the delayed-entry and confirm-market re-runs (a fast first look, never a verdict)
//   edge_lab meta  SPEC.json … [--book]   take/skip model (--book: + flow / order-book inputs → <id>__meta_book) on the idea's walk-forward trades (meta.hpp) → reports/<id>__meta.md
//   edge_lab exits SPEC.json …   thesis exits on the same trades (meta.hpp)                   → reports/<id>__exits.md
// Read-only on market data; no database connection (the script does the I/O).
#include <unistd.h>

#include <chrono>
#include <functional>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

#include "edge_lab.hpp"
#include "meta.hpp"

using namespace lab;

namespace {

struct Ctx {
    std::string data = "data/edge_lab/cache", out = "data/edge_lab", regime = "data/regime";
    int threads = 4; bool quick = false; bool meta_book = false;   // meta: add the flow / order-book inputs
    bool meta_page = false;                                        // meta: + the ORB page's reads and the feature study (2026-10-18)
    std::map<std::string, std::unique_ptr<Series>> series;
    std::map<std::string, std::unique_ptr<Market>> market;
    std::set<int> fomc; paper::VixRegime vix;
};

Market* get_market(Ctx& c, const std::string& sym) {
    auto it = c.market.find(sym);
    if (it != c.market.end()) return it->second.get();
    auto s = std::make_unique<Series>();
    std::string err;
    if (!load_raw(c.data, sym, *s, err)) { std::cerr << "[edge_lab] " << err << "\n"; return nullptr; }
    build_days(*s, c.fomc, c.vix);
    const size_t book = load_book(c.data, sym, *s);
    load_macro(c.data, *s);
    load_events(c.data, *s);
    std::cerr << "[edge_lab] " << sym << ": " << s->size() << " bars, " << s->days.size() << " days, " << book << " order-book minutes\n";
    auto m = std::make_unique<Market>(*s);
    Market* p = m.get();
    c.series[sym] = std::move(s); c.market[sym] = std::move(m);
    return p;
}

std::vector<Variant> compile(const Spec& sp, Evaluator& ev) {
    std::vector<Variant> out;
    for (auto& p : grid(sp)) {
        Variant v; v.p = p; v.label = label_of(p);
        for (auto& c : sp.checks) { v.checks.emplace_back(parse(c.when, p), parse(c.dir, p)); ev.bind(*v.checks.back().first); ev.bind(*v.checks.back().second); }
        if (sp.scan) { v.scan_when = parse(sp.scan_when, p); v.scan_dir = parse(sp.scan_dir, p); ev.bind(*v.scan_when); ev.bind(*v.scan_dir); }
        v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); ev.bind(*v.stop); ev.bind(*v.target);
        v.be = parse(sp.be_r, p); v.part = parse(sp.partial_r, p); ev.bind(*v.be); ev.bind(*v.part);
        v.add = parse(sp.add_r, p); v.trail = parse(sp.trail_r, p); ev.bind(*v.add); ev.bind(*v.trail);
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<std::vector<Trade>> run_all(Ctx& c, Market& m, const Spec& sp, const std::vector<Variant>& vs, int delay) {
    std::vector<std::vector<Trade>> out(vs.size());
    Evaluator ev(m);
    std::atomic<size_t> next{0};
    std::vector<std::thread> th;
    const int T = std::max(1, std::min<int>(c.threads, (int)vs.size()));
    for (int k = 0; k < T; ++k) th.emplace_back([&] {
        Evaluator local(m);   // stateless besides the shared, already-filled caches
        for (size_t i; (i = next++) < vs.size();) { out[i] = run_variant(local, m.s(), sp, vs[i], delay); for (auto& t : out[i]) t.var = (int)i; }
    });
    for (auto& t : th) t.join();
    return out;
}

std::string f2(double x, int p = 2) { char b[64]; std::snprintf(b, sizeof b, "%.*f", p, x); return b; }
std::string sg(double x, int p = 2) { char b[64]; std::snprintf(b, sizeof b, "%+.*f", p, x); return b; }

json run_idea(Ctx& c, const Spec& sp, std::string& md) {
    json R; R["id"] = sp.id; R["name"] = sp.name;
    std::ostringstream o;
    auto t0 = std::chrono::steady_clock::now();
    Market* m = get_market(c, sp.symbol);
    if (!m) throw std::runtime_error("no market data for " + sp.symbol);
    m->pair = nullptr;
    if (!sp.pair.empty()) { Market* pm = get_market(c, sp.pair); if (!pm) throw std::runtime_error("no data for pair " + sp.pair); m->pair = &pm->s(); }
    const Series& s = m->s();
    const double cost = sp.cost.count(sp.symbol) ? sp.cost.at(sp.symbol) : 1.0;
    const double pv = sp.symbol == "ES" ? 50 : sp.symbol == "NQ" ? 20 : 1;

    Evaluator ev(*m);
    std::vector<Variant> vs = compile(sp, ev);
    auto vt = run_all(c, *m, sp, vs, 0);

    // ── 1 SCREEN
    struct VS { int v; Stat st; };
    std::vector<VS> rank;
    int positive = 0, eligible = 0;
    for (size_t v = 0; v < vs.size(); ++v) {
        std::vector<double> net, gr;
        for (auto& t : vt[v]) { net.push_back(t.pts - cost); gr.push_back(t.pts); }
        Stat st = stat_of(net, gr);
        rank.push_back({(int)v, st});
        if (st.n >= sp.min_trades) { ++eligible; if (st.net > 0) ++positive; }
    }
    std::sort(rank.begin(), rank.end(), [](const VS& a, const VS& b) { return a.st.net > b.st.net; });
    {   // every variant's in-sample result (for frozen-setting holdouts and plateau checks)
        std::ofstream vf(c.out + "/reports/" + sp.id + ".variants.csv");
        vf << "label,n,net_avg,pf,t\n";
        for (auto& r : rank) vf << '"' << vs[r.v].label << "\"," << r.st.n << "," << r.st.avg << "," << r.st.pf << "," << r.st.t << "\n";
    }
    const VS* best = nullptr;
    for (auto& r : rank) if (r.st.n >= sp.min_trades) { best = &r; break; }
    const double years = s.days.empty() ? 1 : std::max(1.0, (double)(s.days.back().date / 10000 - std::max(sp.from_ymd, s.days.front().date) / 10000) + 0.5);
    // Bonferroni: t a single best-of-V needs for a two-sided 5 % test (normal approximation)
    auto z_for = [](double p) { double lo = 0, hi = 10; for (int k = 0; k < 80; ++k) { double mid = (lo + hi) / 2; if (0.5 * std::erfc(mid / std::sqrt(2.0)) > p) lo = mid; else hi = mid; } return lo; };
    const double t_need = z_for(0.025 / std::max<size_t>(1, vs.size()));
    const bool g1 = best && best->st.net > 0;

    o << "# " << sp.name << " (`" << sp.id << "`)\n\n";
    o << "**Why it should work:** " << sp.why << "\n\n";
    o << "Market " << sp.symbol << " · real cost " << f2(cost) << " pts/contract round trip · " << vs.size()
      << " variants · data from " << ymd_str(std::max(sp.from_ymd, s.days.front().date)) << " to " << ymd_str(std::min(sp.to_ymd, s.days.back().date)) << "\n\n";

    // ── 2 WALK-FORWARD
    WfOut w = walk(s, vt, cost, sp.train_months, std::max(5, sp.min_trades / 20));
    std::vector<double> oos, oos_g;
    for (auto& t : w.trades) { oos.push_back(t.pts - cost); oos_g.push_back(t.pts); }
    Stat so = stat_of(oos, oos_g);
    const bool g2 = so.n >= 30 && so.net > 0 && so.pf >= 1.05;

    // ── 3 CONTROLS
    Controls ct;
    if (!c.quick) {
        DayCache cdc;
        auto dir_at = [&](const Trade& t, int i) -> int {
            const Variant& v = vs[t.var];
            const Node& dn = t.src >= 0 ? *v.checks[t.src].second : *v.scan_dir;
            double x = ev.eval(dn, i, cdc);
            return x > 0 ? 1 : x < 0 ? -1 : 0;
        };
        ct = controls(s, w.trades, cost, sp.control_seeds, sp.max_bars, dir_at);
    }
    auto claims = [&](const char* k) { return std::find(sp.claims.begin(), sp.claims.end(), k) != sp.claims.end(); };
    const double cp = sp.control_pct;
    const bool g3 = !c.quick && ct.dir_pct >= cp &&
                    (sp.claims.empty() ? (ct.time_pct >= cp || ct.day_pct >= cp)
                                       : ((!claims("time") || ct.time_pct >= cp) && (!claims("day") || ct.day_pct >= cp)));
    std::string claimed; for (auto& k : sp.claims) claimed += (claimed.empty() ? "" : "+") + k;
    if (claimed.empty()) claimed = "dir + (time or day)";

    // ── 4 ROBUSTNESS
    double era_net[3] = {0, 0, 0}; int era_n[3] = {0, 0, 0};
    std::map<int, std::pair<int, double>> by_year;
    for (auto& t : w.trades) { int d = s.days[t.day].date; era_net[era_of(d)] += t.pts - cost; ++era_n[era_of(d)]; auto& y = by_year[d / 10000]; ++y.first; y.second += t.pts - cost; }
    struct Chk { std::string name; bool ok; std::string detail; bool ran; };
    std::vector<Chk> rb;
    for (int e = 0; e < 3; ++e) rb.push_back({std::string("era ") + era_name(e), era_n[e] > 0 && era_net[e] > 0, era_n[e] ? sg(era_net[e] / era_n[e]) + " pts/trade, n " + std::to_string(era_n[e]) : "no trades", era_n[e] > 0});
    WfOut w15 = walk(s, vt, cost * 1.5, sp.train_months, std::max(5, sp.min_trades / 20));
    rb.push_back({"cost × 1.5", net_of(w15.trades, cost * 1.5) > 0, sg(net_of(w15.trades, cost * 1.5), 0) + " pts total", true});
    if (!c.quick) {
        auto vt1 = run_all(c, *m, sp, vs, 1);
        WfOut w1 = walk(s, vt1, cost, sp.train_months, std::max(5, sp.min_trades / 20));
        rb.push_back({"entry 1 bar late", net_of(w1.trades, cost) > 0, sg(net_of(w1.trades, cost), 0) + " pts total, n " + std::to_string(w1.trades.size()), true});
        Market* cm = sp.confirm.empty() || sp.confirm == sp.symbol ? nullptr : get_market(c, sp.confirm);
        if (cm) {
            Evaluator cev(*cm);
            std::vector<Variant> cvs = compile(sp, cev);
            auto cvt = run_all(c, *cm, sp, cvs, 0);
            const double cc = sp.cost.count(sp.confirm) ? sp.cost.at(sp.confirm) : cost;
            WfOut wc = walk(cm->s(), cvt, cc, sp.train_months, std::max(5, sp.min_trades / 20));
            rb.push_back({"confirm on " + sp.confirm, net_of(wc.trades, cc) > 0, sg(net_of(wc.trades, cc), 0) + " pts total, n " + std::to_string(wc.trades.size()), true});
        }
    }
    const double plateau = eligible ? (double)positive / eligible : 0;
    rb.push_back({"plateau (variants net > 0)", plateau >= 0.5, f2(100 * plateau, 0) + "% of " + std::to_string(eligible), true});
    int rb_ok = 0, rb_ran = 0;
    for (auto& r : rb) if (r.ran) { ++rb_ran; if (r.ok) ++rb_ok; }
    const int rb_need = std::max(1, (int)std::ceil(rb_ran * 5.0 / 7.0));
    const bool recent_ok = era_n[2] > 0 && era_net[2] > 0;   // a rule that stopped working is not an edge, whatever else passes
    const bool g4 = !c.quick && rb_ok >= rb_need && recent_ok;

    // ── 5 PROP
    std::vector<PropOut> props;
    if (!sp.micro.empty()) for (int q : sp.prop_sizes) props.push_back(prop_chain(s, w.trades, sp.prop_plan, q, sp.micro_cost, sp.micro_pv));
    const PropOut* bp = nullptr;
    for (auto& p : props) if (!bp || p.be_fee > bp->be_fee) bp = &p;
    const bool prop_na = sp.micro.empty();   // no micro on our prop firm: judged on gates 1–4
    const bool g5 = prop_na || (bp && bp->be_fee > sp.prop_fee && bp->payouts > 0);

    const bool gates[5] = {g1, g2, g3, g4, g5};
    static const char* gname[5] = {"SCREEN", "WALK-FORWARD", "CONTROLS", "ROBUSTNESS", "PROP FIT"};
    int stage = 5;
    for (int k = 0; k < 5; ++k) if (!gates[k]) { stage = k; break; }
    std::string verdict = stage == 5 ? "PASS" : c.quick ? "QUICK-LOOK" : std::string("FAIL at ") + gname[stage];

    o << "## Verdict: **" << verdict << "**\n\n| gate | result |\n|---|---|\n";
    o << "| 1 Screen | " << (g1 ? "pass" : "FAIL") << " — best variant (" << (best ? vs[best->v].label : "none with ≥" + std::to_string(sp.min_trades) + " trades")
      << ") net " << (best ? sg(best->st.avg) + " pts/trade, t " + f2(best->st.t) : "—") << "; single-test t needed for " << vs.size() << " variants ≈ " << f2(t_need) << " |\n";
    o << "| 2 Walk-forward | " << (g2 ? "pass" : "FAIL") << " — " << so.n << " out-of-sample trades, " << sg(so.avg) << " pts/trade net, PF " << f2(so.pf) << ", t " << f2(so.t) << " |\n";
    o << "| 3 Controls | " << (c.quick ? "skipped (--quick)" : g3 ? "pass" : "FAIL") << (c.quick ? "" :  " — beats random direction in " + f2(100 * ct.dir_pct, 0) + "%, random time in " + f2(100 * ct.time_pct, 0) + "%, random day in " + f2(100 * ct.day_pct, 0) + "% of " + std::to_string(ct.seeds) + " seeds (need " + f2(100 * sp.control_pct, 0) + "% on the claimed " + claimed + ")") << " |\n";
    o << "| 4 Robustness | " << (c.quick ? "partial (--quick)" : g4 ? "pass" : "FAIL") << " — " << rb_ok << "/" << rb_ran << " checks (need " << rb_need << ", and the latest era positive: " << (recent_ok ? "yes" : "NO") << ") |\n";
    if (prop_na) o << "| 5 Prop fit | n/a — no micro contract for " << sp.symbol << " on our prop firm (judged on gates 1–4) |\n\n";
    else o << "| 5 Prop fit | " << (g5 ? "pass" : "FAIL") << " — " << sp.prop_plan << ": best break-even eval fee $" << f2(bp ? bp->be_fee : 0, 0) << " (need > $" << f2(sp.prop_fee, 0) << ") |\n\n";

    o << "## Walk-forward (each month uses the variant best over the previous " << sp.train_months << " months)\n\n";
    o << "- " << f2(so.n / years, 1) << " trades/yr · win " << f2(100 * so.win, 0) << "% · gross " << sg(so.gross_avg) << " pts · net " << sg(so.avg)
      << " pts/trade = $" << f2(so.avg * pv, 0) << " per " << sp.symbol << " / $" << f2((so.gross_avg - sp.micro_cost) * sp.micro_pv, 1) << " per " << sp.micro << "\n";
    o << "- picked blind (average variant): " << sg(w.sum.baseline_net, 0) << " pts total · walk-forward: " << sg(w.sum.oos_net, 0) << " · hindsight best: " << sg(w.sum.oracle_net, 0) << "\n";
    o << "- months traded " << w.sum.traded << "/" << w.sum.tested << ", positive " << w.sum.positive << "\n\n";
    o << "| year | trades | net pts | $ per " << sp.symbol << " |\n|---|---|---|---|\n";
    int yrs_pos = 0;
    for (auto& [y, v] : by_year) { o << "| " << y << " | " << v.first << " | " << sg(v.second, 0) << " | " << sg(v.second * pv, 0) << " |\n"; if (v.second > 0) ++yrs_pos; }
    o << "\nYears positive: " << yrs_pos << "/" << by_year.size() << "\n\n";

    // ── monthly view: share of months positive, and how the months move with the earlier ideas
    const auto mon = monthly_net(s, w.trades, cost);
    int mpos = 0, mpos19 = 0, m19 = 0, run = 0, worst_run = 0;
    for (auto& [ym, v] : mon) { mpos += v > 0; if (ym >= 201901) { ++m19; mpos19 += v > 0; } run = v > 0 ? 0 : run + 1; worst_run = std::max(worst_run, run); }
    {
        std::ofstream mf(c.out + "/reports/" + sp.id + ".monthly.csv");
        for (auto& [ym, v] : mon) mf << ym << "," << v << "\n";
    }
    json corr = json::object();
    o << "## Months\n\n- positive " << mpos << "/" << mon.size() << " (" << f2(mon.empty() ? 0 : 100.0 * mpos / mon.size(), 0) << "%), since 2019 "
      << mpos19 << "/" << m19 << " (" << f2(m19 ? 100.0 * mpos19 / m19 : 0, 0) << "%); longest run without a positive month " << worst_run << "\n";
    for (auto& ref : sp.corr_with) {
        if (ref == sp.id) continue;
        std::ifstream rf(c.out + "/reports/" + ref + ".monthly.csv");
        if (!rf) continue;
        std::map<int, double> rm; int ym; double v; char comma;
        while (rf >> ym >> comma >> v) rm[ym] = v;
        std::map<int, double> rm19, mo19;
        for (auto& [k, x] : rm) if (k >= 201901) rm19[k] = x;
        for (auto& [k, x] : mon) if (k >= 201901) mo19[k] = x;
        const double r_all = corr_monthly(mon, rm), r19 = corr_monthly(mo19, rm19);
        int either = 0, both_n = 0;
        for (auto& [k, x] : mo19) { auto it = rm19.find(k); if (it == rm19.end()) continue; ++both_n; either += (x > 0 || it->second > 0); }
        corr[ref] = {{"all", std::isnan(r_all) ? json(nullptr) : json(r_all)}, {"since2019", std::isnan(r19) ? json(nullptr) : json(r19)},
                     {"either_positive_since2019", both_n ? (double)either / both_n : 0}};
        o << "- monthly correlation with `" << ref << "`: " << (std::isnan(r_all) ? "—" : f2(r_all)) << " (since 2019 " << (std::isnan(r19) ? "—" : f2(r19))
          << "); months since 2019 where this or it is positive: " << f2(both_n ? 100.0 * either / both_n : 0, 0) << "%\n";
    }
    o << "\n";

    if (!c.quick) o << "## Controls\nReal net " << sg(net_of(w.trades, cost), 0) << " pts; random direction mean " << sg(ct.dir_mean, 0) << ", random time mean " << sg(ct.time_mean, 0) << ", random day mean " << sg(ct.day_mean, 0) << ".\n\n";
    o << "## Robustness\n\n| check | ok | detail |\n|---|---|---|\n";
    for (auto& r : rb) o << "| " << r.name << " | " << (r.ok ? "yes" : "no") << " | " << r.detail << " |\n";
    o << "\n## Prop (" << sp.prop_plan << ", " << sp.micro << " at " << f2(sp.micro_cost) << " pts cost; eval → funded until breach → new eval)\n\n";
    o << "| " << sp.micro << " | years | evals | passed | payouts | paid $ | payouts/yr | paid $/yr | break-even fee |\n|---|---|---|---|---|---|---|---|---|\n";
    for (auto& p : props)
        o << "| " << p.size << " | " << f2(p.years, 1) << " | " << p.evals << " | " << p.passes << " | " << p.payouts << " | " << f2(p.paid, 0)
          << " | " << f2(p.years > 0 ? p.payouts / p.years : 0, 1) << " | " << f2(p.years > 0 ? p.paid / p.years : 0, 0) << " | " << f2(p.be_fee, 0) << " |\n";
    o << "\n## Top variants in-sample (all history, for orientation — the verdict uses the walk-forward)\n\n| variant | trades | net pts/trade | PF | t |\n|---|---|---|---|---|\n";
    for (size_t k = 0; k < rank.size() && k < 8; ++k)
        o << "| " << vs[rank[k].v].label << " | " << rank[k].st.n << " | " << sg(rank[k].st.avg) << " | " << f2(rank[k].st.pf) << " | " << f2(rank[k].st.t) << " |\n";
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    o << "\n_edge_lab " << (c.quick ? "--quick " : "") << "run in " << f2(secs, 1) << " s_\n";
    md = o.str();

    R["verdict"] = verdict; R["stage_reached"] = stage; R["quick"] = c.quick;
    R["oos"] = {{"n", so.n}, {"avg_pts", so.avg}, {"gross_pts", so.gross_avg}, {"pf", so.pf}, {"t", so.t}, {"trades_per_year", so.n / years},
                {"years_positive", yrs_pos}, {"years", by_year.size()},
                {"months_positive_pct", mon.empty() ? 0 : (double)mpos / mon.size()}, {"months_positive_pct_2019", m19 ? (double)mpos19 / m19 : 0}};
    R["corr"] = corr;
    R["screen"] = {{"variants", vs.size()}, {"best", best ? vs[best->v].label : ""}, {"best_avg", best ? best->st.avg : 0}, {"best_t", best ? best->st.t : 0}, {"t_needed", t_need}, {"plateau", plateau}};
    R["controls"] = {{"dir_pct", ct.dir_pct}, {"time_pct", ct.time_pct}, {"day_pct", ct.day_pct}, {"seeds", ct.seeds}, {"need_pct", sp.control_pct},
                     {"claimed", claimed}, {"real_net", net_of(w.trades, cost)}, {"dir_mean", ct.dir_mean}, {"time_mean", ct.time_mean}, {"day_mean", ct.day_mean}};
    // per-gate detail for the dashboard's research-lab page (2026-10-17): every check with its own pass flag and numbers
    json chk = json::array();
    for (auto& r : rb) chk.push_back({{"name", r.name}, {"ok", r.ok}, {"ran", r.ran}, {"detail", r.detail}});
    json eras = json::array();
    for (int e = 0; e < 3; ++e) eras.push_back({{"era", era_name(e)}, {"n", era_n[e]}, {"avg_pts", era_n[e] ? json(era_net[e] / era_n[e]) : json(nullptr)}});
    R["robust"] = {{"ok", rb_ok}, {"ran", rb_ran}, {"need", rb_need}, {"recent_ok", recent_ok}, {"checks", chk}, {"eras", eras}};
    json yrs = json::array();
    for (auto& [y, v] : by_year) yrs.push_back({{"year", y}, {"n", v.first}, {"net_pts", v.second}});
    R["walk"] = {{"train_months", sp.train_months}, {"blind_net", w.sum.baseline_net}, {"oos_net", w.sum.oos_net}, {"hindsight_net", w.sum.oracle_net},
                 {"months_traded", w.sum.traded}, {"months_tested", w.sum.tested}, {"months_positive", w.sum.positive}, {"win", so.win}, {"by_year", yrs}};
    R["gates"] = json::array({
        {{"gate", "screen"}, {"pass", g1}, {"rule", "best variant with >= min_trades nets > 0 after cost"}},
        {{"gate", "walk_forward"}, {"pass", g2}, {"rule", ">= 30 out-of-sample trades, net > 0, PF >= 1.05"}},
        {{"gate", "controls"}, {"pass", g3}, {"rule", "beats random direction AND (random time or random day) in >= need_pct of seeds"}},
        {{"gate", "robustness"}, {"pass", g4}, {"rule", ">= need of the checks, and the latest era positive"}},
        {{"gate", "prop_fit"}, {"pass", g5}, {"rule", prop_na ? "n/a (no micro contract)" : "break-even eval fee > prop_fee with payouts > 0"}}});
    R["data"] = {{"from", ymd_str(std::max(sp.from_ymd, s.days.front().date))}, {"to", ymd_str(std::min(sp.to_ymd, s.days.back().date))}, {"symbol", sp.symbol}};
    json P = json::array();
    for (auto& p : props) P.push_back({{"mnq", p.size}, {"evals", p.evals}, {"passes", p.passes}, {"payouts", p.payouts}, {"paid", p.paid}, {"years", p.years}, {"be_fee", p.be_fee}});
    R["prop"] = P; R["cost"] = cost; R["seconds"] = secs;
    return R;
}

std::string csv_q(const std::string& s) { std::string o = "\""; for (char ch : s) { if (ch == '"') o += '"'; o += ch; } return o + "\""; }

// ───────────────────────────── meta / exits (meta.hpp) ─────────────────────────────
struct Base { Market* m = nullptr; std::vector<Variant> vs; WfOut w; double cost = 1, pv = 20; };

Base prepare(Ctx& c, const Spec& sp) {
    Base b;
    b.m = get_market(c, sp.symbol);
    if (!b.m) throw std::runtime_error("no market data for " + sp.symbol);
    b.m->pair = nullptr;
    if (!sp.pair.empty()) { Market* pm = get_market(c, sp.pair); if (!pm) throw std::runtime_error("no data for pair " + sp.pair); b.m->pair = &pm->s(); }
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
    for (int q : sp.prop_sizes) { out.push_back(prop_chain(s, tr, sp.prop_plan, q, sp.micro_cost, sp.micro_pv)); best = std::max(best, out.back().be_fee); }
    return {out, best};
}
void prop_table(std::ostringstream& o, const std::string& who, const std::vector<PropOut>& P) {
    for (auto& p : P)
        o << "| " << who << " | " << p.size << " | " << p.evals << " | " << p.passes << " | " << p.payouts << " | "
          << f2(p.years > 0 ? p.paid / p.years : 0, 0) << " | " << f2(p.be_fee, 0) << " |\n";
}

// One full meta evaluation (walk-forward, the five gates, the report body) on a feature set. run_meta calls it once; with
// --page (feature study, 2026-10-18) it is called again on the features that survive permutation importance.
struct MetaEval { std::string md, verdict; int stage = 0; json R; std::vector<double> pg; std::vector<int> T; Line Lb, Lm; };
MetaEval eval_meta(Ctx& c, const Spec& sp, Base& b, const MetaData& d, const std::vector<std::string>& FN, const std::string& sfx, const std::string& title, std::chrono::steady_clock::time_point t0) {
    MetaEval E;
    const Series& s = b.m->s();
    auto gfit = [](const std::vector<std::vector<float>>& X, const std::vector<double>& y) { return gbm_fit(X, y); };
    auto rfit = [](const std::vector<std::vector<float>>& X, const std::vector<double>& y) { return ridge_fit(X, y); };
    const int MINY = 3;
    std::vector<double> pg = meta_walk(d, d.y, MINY, gfit), pr = meta_walk(d, d.y, MINY, rfit);

    std::vector<int> T;   // tested trades (a prediction exists)
    for (size_t i = 0; i < d.tr.size(); ++i) if (!std::isnan(pg[i])) T.push_back((int)i);
    if (T.empty()) throw std::runtime_error(sp.id + ": not enough history for the meta walk-forward");
    {   // per-trade decisions (2026-10-17): which walk-forward trades the model would take — for combining with other filters
        std::ofstream pf(c.out + "/reports/" + sp.id + sfx + ".trades.csv");
        pf << "date,entry_epoch_et,dir,net_pts,pred_gbm,pred_ridge,take\n";
        for (int i : T)
            pf << s.days[d.tr[i].day].date << "," << s.t[d.tr[i].e] << "," << d.tr[i].dir << "," << (d.tr[i].pts - b.cost) << "," << pg[i] << ","
               << (std::isnan(pr[i]) ? 0.0 : pr[i]) << "," << (pg[i] > 0 ? 1 : 0) << "\n";
    }
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
    o << "# " << title << " on " << sp.name << " (`" << sp.id << sfx << "`)\n\n";
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
    o << "\n## Prop (" << sp.prop_plan << ", " << sp.micro << " at " << f2(sp.micro_cost) << " pts)\n\n| trades | " << sp.micro << " | evals | passed | payouts | paid $/yr | break-even fee |\n|---|---|---|---|---|---|---|\n";
    prop_table(o, "base", pb); prop_table(o, "meta", pm);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    o << "\n_edge_lab meta run in " << f2(secs, 1) << " s_\n";
    E.md = o.str();

    json R; R["id"] = sp.id + sfx; R["name"] = title + ": " + sp.name; R["verdict"] = verdict; R["stage_reached"] = stage; R["quick"] = false;
    R["oos"] = {{"n", Lm.n}, {"avg_pts", Lm.avg}, {"pf", Lm.pf}, {"base_n", Lb.n}, {"base_avg_pts", Lb.avg}, {"base_pf", Lb.pf}, {"ridge_avg_pts", Lr.avg}};
    R["controls"] = {{"random_skip_pct", p_skip}, {"shuffled_label_pct", p_lab}};
    R["prop"] = {{"meta_be_fee", bem}, {"base_be_fee", beb}};
    R["seconds"] = secs;
    E.R = R; E.verdict = verdict; E.stage = stage; E.pg = pg; E.T = T; E.Lb = Lb; E.Lm = Lm;
    return E;
}

// Permutation importance (feature study): for each feature, permute its column within every tested year, predict with that
// year's walk-forward model, and measure the drop in out-of-sample net per tested signal (taken when the prediction is > 0).
// Done on the real-label models and on one shuffled-label model: a feature "works" only if it helps in ≥ 2 of 3 eras AND
// more than it "helps" the model trained on shuffled results.
struct FeatImp { int f; double imp = 0, imp_sh = 0; double era[3] = {0, 0, 0}; bool works = false; };
std::vector<FeatImp> permutation_importance(const MetaData& d, const std::vector<double>& ytrain, const Series& s, double cost, int MINY, int seeds) {
    auto gfit = [](const std::vector<std::vector<float>>& X, const std::vector<double>& y) { return gbm_fit(X, y); };
    auto models = meta_walk_models(d, ytrain, MINY, gfit);
    const int F = d.X.empty() ? 0 : (int)d.X[0].size();
    std::vector<FeatImp> out(F);
    std::map<int, std::vector<int>> by_year; for (size_t i = 0; i < d.tr.size(); ++i) by_year[d.year[i]].push_back((int)i);
    auto net = [&](int i) { return d.tr[i].pts - cost; };
    // score = net of taken signals per tested signal, overall and per era
    auto score = [&](const std::function<float(int, int)>& xf, double* era) {
        double tot = 0; int n = 0; double es[3] = {0, 0, 0}; int en[3] = {0, 0, 0};
        for (auto& [Y, m] : models) {
            auto it = by_year.find(Y); if (it == by_year.end()) continue;
            for (int i : it->second) {
                std::vector<float> x = d.X[i]; for (int f = 0; f < F; ++f) x[f] = xf(i, f);
                const double p = m.predict(x); const int e = era_of(s.days[d.tr[i].day].date);
                ++n; ++en[e]; if (p > 0) { tot += net(i); es[e] += net(i); }
            }
        }
        for (int e = 0; e < 3; ++e) era[e] = en[e] ? es[e] / en[e] : NAN;
        return n ? tot / n : NAN;
    };
    double base_era[3]; const double base = score([&](int i, int f) { return d.X[i][f]; }, base_era);
    for (int f = 0; f < F; ++f) {
        out[f].f = f; double acc = 0, acc_e[3] = {0, 0, 0};
        for (int sd = 0; sd < seeds; ++sd) {
            std::mt19937_64 rng(1234567ULL * (sd + 1) + f);
            std::vector<float> perm(d.tr.size());
            for (auto& [Y, idx] : by_year) { std::vector<int> p = idx; std::shuffle(p.begin(), p.end(), rng); for (size_t j = 0; j < idx.size(); ++j) perm[idx[j]] = d.X[p[j]][f]; }
            double e3[3]; const double sc = score([&](int i, int g) { return g == f ? perm[i] : d.X[i][g]; }, e3);
            acc += (base - sc) / seeds; for (int e = 0; e < 3; ++e) acc_e[e] += (std::isnan(e3[e]) || std::isnan(base_era[e]) ? 0 : base_era[e] - e3[e]) / seeds;
        }
        out[f].imp = acc; for (int e = 0; e < 3; ++e) out[f].era[e] = acc_e[e];
    }
    return out;
}

json run_meta(Ctx& c, const Spec& sp, std::string& md) {
    auto t0 = std::chrono::steady_clock::now();
    Base b = prepare(c, sp);
    const Series& s = b.m->s();
    Market* esm = sp.confirm.empty() || sp.confirm == sp.symbol ? nullptr : get_market(c, sp.confirm);
    MetaData d = meta_data(*b.m, esm ? &esm->s() : nullptr, b.w.trades, b.cost, c.meta_book, c.meta_page);
    const auto FN = meta_feature_names(c.meta_book, c.meta_page);
    const std::string sfx = c.meta_page ? (c.meta_book ? "__meta_page_book" : "__meta_page") : (c.meta_book ? "__meta_book" : "__meta");
    const std::string title = std::string("Meta-label (take / skip)") + (c.meta_page ? " + the ORB page's reads" : "") + (c.meta_book ? " with flow / order-book inputs" : "");
    MetaEval E0 = eval_meta(c, sp, b, d, FN, sfx, title, t0);
    if (!c.meta_page) { md = E0.md; return E0.R; }
    {   // every signal's feature row (for the manual-marks comparison and for checking a feature is what it claims to be)
        std::ofstream ff(c.out + "/reports/" + sp.id + sfx + ".features.csv");
        ff << "date,entry_epoch_et,dir,net_pts"; for (auto& n : FN) ff << "," << n; ff << "\n";
        for (size_t i = 0; i < d.tr.size(); ++i) {
            ff << s.days[d.tr[i].day].date << "," << s.t[d.tr[i].e] << "," << d.tr[i].dir << "," << (d.tr[i].pts - b.cost);
            for (float x : d.X[i]) ff << "," << (std::isnan(x) ? "" : std::to_string(x)); ff << "\n";
        }
    }

    // feature study (research/preregistered/2026-10-18_feature_study.md): importance on real and shuffled labels, then refits
    const int MINY = 3, SEEDS = 20;
    std::vector<FeatImp> imp = permutation_importance(d, d.y, s, b.cost, MINY, SEEDS);
    {
        std::mt19937_64 rng(104729ULL);
        std::vector<double> ys = d.y;
        std::map<int, std::vector<int>> yi; for (size_t i = 0; i < d.y.size(); ++i) yi[d.year[i]].push_back((int)i);
        for (auto& [y, idx] : yi) { std::vector<int> p = idx; std::shuffle(p.begin(), p.end(), rng); for (size_t j = 0; j < idx.size(); ++j) ys[idx[j]] = d.y[p[j]]; }
        std::vector<FeatImp> sh = permutation_importance(d, ys, s, b.cost, MINY, std::max(5, SEEDS / 4));
        for (size_t f = 0; f < imp.size(); ++f) imp[f].imp_sh = sh[f].imp;
    }
    std::vector<int> works, top8;
    for (auto& x : imp) { int up = 0; for (double e : x.era) up += e > 0; x.works = up >= 2 && x.imp > x.imp_sh && x.imp > 0; if (x.works) works.push_back(x.f); }
    { std::vector<int> ord(imp.size()); std::iota(ord.begin(), ord.end(), 0); std::sort(ord.begin(), ord.end(), [&](int a, int z) { return imp[a].imp > imp[z].imp; }); for (size_t k = 0; k < ord.size() && k < 8; ++k) top8.push_back(ord[k]); std::sort(top8.begin(), top8.end()); }
    auto names_of = [&](const std::vector<int>& k) { std::vector<std::string> n; for (int f : k) n.push_back(FN[f]); return n; };
    auto join = [&](const std::vector<int>& k) { std::string o; for (int f : k) o += (o.empty() ? "" : ", ") + FN[f]; return o.empty() ? "(none)" : o; };
    std::ostringstream o;
    o << "# Feature study on " << sp.name << " (`" << sp.id << sfx << "`)\n\n";
    o << "Pre-registered 2026-10-18 (research/preregistered/2026-10-18_feature_study.md). Step 1 = the meta walk-forward on all " << FN.size()
      << " features; step 2 = permutation importance (" << SEEDS << " seeds, within-year permutation, drop in out-of-sample net per tested signal) on the\n"
         "real-label models and on a shuffled-label model; a feature works if it helps in ≥ 2 of 3 eras and more than on shuffled labels; step 3 = refits on the\n"
         "features that work and on the top 8. The verdict is the refit on the features that work.\n\n";
    o << "## Step 2 — what works\n\n| feature | importance (pts / signal) | 2010–18 | 2019–22 | 2023–26 | on shuffled labels | works |\n|---|---|---|---|---|---|---|\n";
    { std::vector<int> ord(imp.size()); std::iota(ord.begin(), ord.end(), 0); std::sort(ord.begin(), ord.end(), [&](int a, int z) { return imp[a].imp > imp[z].imp; });
      for (int f : ord) o << "| " << FN[f] << " | " << sg(imp[f].imp, 3) << " | " << sg(imp[f].era[0], 3) << " | " << sg(imp[f].era[1], 3) << " | " << sg(imp[f].era[2], 3) << " | " << sg(imp[f].imp_sh, 3) << " | " << (imp[f].works ? "**yes**" : "no") << " |\n"; }
    o << "\nFeatures that work: " << join(works) << "\nTop 8 by importance: " << join(top8) << "\n\n";
    MetaEval E1, E2;
    if (works.size() >= 2) E1 = eval_meta(c, sp, b, meta_subset(d, works), names_of(works), sfx + "_works", "Refit on the features that work", t0);
    E2 = eval_meta(c, sp, b, meta_subset(d, top8), names_of(top8), sfx + "_top8", "Refit on the top 8", t0);
    const std::string verdict = works.size() < 2 ? "FAIL — fewer than 2 features work" : E1.verdict;
    o << "## Verdict: **" << verdict << "**\n\n| model | taken / signals | net pts/trade | base | verdict |\n|---|---|---|---|---|\n";
    o << "| all features | " << E0.Lm.n << " / " << E0.Lb.n << " | " << sg(E0.Lm.avg) << " | " << sg(E0.Lb.avg) << " | " << E0.verdict << " |\n";
    if (works.size() >= 2) o << "| features that work (" << works.size() << ") | " << E1.Lm.n << " / " << E1.Lb.n << " | " << sg(E1.Lm.avg) << " | " << sg(E1.Lb.avg) << " | " << E1.verdict << " |\n";
    o << "| top 8 | " << E2.Lm.n << " / " << E2.Lb.n << " | " << sg(E2.Lm.avg) << " | " << sg(E2.Lb.avg) << " | " << E2.verdict << " |\n\n---\n\n";
    o << E0.md << "\n---\n\n" << (works.size() >= 2 ? E1.md + "\n---\n\n" : "") << E2.md;
    md = o.str();
    json R = works.size() >= 2 ? E1.R : E0.R;
    R["id"] = sp.id + sfx; R["name"] = "Feature study: " + sp.name; R["verdict"] = verdict; R["stage_reached"] = works.size() >= 2 ? E1.stage : 0;
    R["features"] = {{"all", FN}, {"works", names_of(works)}, {"top8", names_of(top8)}};
    json imps = json::array(); for (auto& x : imp) imps.push_back({{"f", FN[x.f]}, {"imp", x.imp}, {"imp_sh", x.imp_sh}, {"era", {x.era[0], x.era[1], x.era[2]}}, {"works", x.works}});
    R["importance"] = imps;
    R["all_features"] = {{"verdict", E0.verdict}, {"avg_pts", E0.Lm.avg}}; R["top8"] = {{"verdict", E2.verdict}, {"avg_pts", E2.Lm.avg}};
    return R;
}

json run_exits(Ctx& c, const Spec& sp, std::string& md) {
    auto t0 = std::chrono::steady_clock::now();
    Base b = prepare(c, sp);
    const Series& s = b.m->s();
    const auto& tr = b.w.trades;
    if (tr.empty()) throw std::runtime_error(sp.id + ": no walk-forward trades");
    ExitSim sim(*b.m);
    {   // trail_reg gate: the page's regime score at each entry (feature study step 4)
        FeatureMaker fm(*b.m, nullptr, false, true);
        const auto FN = meta_feature_names(false, true);
        const int ri = (int)(std::find(FN.begin(), FN.end(), "reg_score") - FN.begin());
        for (auto& t : tr) { auto f = fm.at(t); sim.set_trail_gate(t, ri < (int)f.size() && f[ri] >= 60); }
    }
    std::vector<char> pgate;   // trail_path gate: path efficiency at entry ≥ 0.55 (2026-10-18_direction_followups)
    for (auto& t : tr) { const double pe = sim.path_eff(t); pgate.push_back(!std::isnan(pe) && pe >= 0.55); sim.set_path_gate(t, pgate.back()); }
    std::vector<std::vector<ExitOut>> res(X__COUNT);
    for (int r = 0; r < X__COUNT; ++r) for (auto& t : tr) res[r].push_back(sim.run(t, r));
    int mismatch = 0; for (size_t i = 0; i < tr.size(); ++i) if (std::fabs(res[X_BASE][i].pts - tr[i].pts) > 1e-3) ++mismatch;   // stop/target are stored as float
    auto nets = [&](int r) { std::vector<double> v; for (auto& x : res[r]) v.push_back(x.pts - b.cost); return v; };
    auto as_trades = [&](int r) { std::vector<Trade> v = tr; for (size_t i = 0; i < v.size(); ++i) { v[i].x = res[r][i].x; v[i].pts = res[r][i].pts; v[i].mae = res[r][i].mae; v[i].mfe = res[r][i].mfe; } return v; };
    Line L[X__COUNT]; for (int r = 0; r < X__COUNT; ++r) L[r] = line_of(nets(r));
    auto [pbase, bebase] = props_of(s, tr, sp);

    struct RuleOut { double p_time = 0, twin_mean = 0; int fired = 0, eras_up = 0, eras_ran = 0; bool latest_up = false; double be = 0; bool pass = false; std::string why; };
    std::vector<RuleOut> ro(X__COUNT);
    double eb[3] = {0, 0, 0}; int nb[3] = {0, 0, 0};
    for (size_t i = 0; i < tr.size(); ++i) { int e = era_of(s.days[tr[i].day].date); eb[e] += res[X_BASE][i].pts - b.cost; ++nb[e]; }
    const double NEED = 0.99;   // X__COUNT − 1 (15) exit rules are tested → the control must clear 99 %, not 95 %
    const int SEEDS = 200;
    for (int r = 1; r < X__COUNT; ++r) {
        RuleOut& q = ro[r];
        std::vector<int> durs; std::vector<size_t> fired;
        for (size_t i = 0; i < tr.size(); ++i) if (res[r][i].thesis) { durs.push_back(res[r][i].x - tr[i].e); fired.push_back(i); }
        q.fired = (int)fired.size();
        // Twin: a path-blind exit with the same exit rate and holding times. Every trade gets a time exit with the
        // probability the rule fired, after a holding time drawn from the rule's own; otherwise it runs to the rule's
        // stop / target / flat. (Re-timing only the trades the rule exited would select them on their future — they
        // are the ones that later went back through the level — and flatter the twin.)
        int beat = 0;
        const double q_fire = tr.empty() ? 0 : (double)fired.size() / tr.size();
        if (exit_moves_stop(r)) {
            // Stop-width twin (2026-10-17): the rule's own stop widths (in each day's ATR) shuffled across the trades, every other
            // part of the rule unchanged — does the stop at the LEVEL beat stops of the same widths placed without it?
            std::vector<double> w; for (auto& t : tr) { double a = s.days[t.day].atr; w.push_back(a > 0 ? sim.mext_stop(t) / a : 0); }
            for (int k = 0; k < SEEDS; ++k) {
                std::mt19937_64 rng(15485863ULL * (k + 1) + r); std::vector<double> p = w; std::shuffle(p.begin(), p.end(), rng);
                double n = 0;
                for (size_t i = 0; i < tr.size(); ++i) { double a = s.days[tr[i].day].atr; n += sim.run(tr[i], r, -1, a > 0 ? std::max((double)tr[i].stop, p[i] * a) : tr[i].stop).pts - b.cost; }
                q.twin_mean += n / SEEDS;
                if (L[r].net > n + 1e-6) ++beat;
            }
        }
        if (exit_moves_target(r)) {
            // Target-distance twin (2026-10-18): the rule's own target distances (in each day's ATR) shuffled across the trades,
            // everything else unchanged — does the target at the LEVEL beat targets of the same sizes placed without it?
            std::vector<double> w;
            for (auto& t : tr) { double a = s.days[t.day].atr; w.push_back(a > 0 ? sim.level_or_fallback(t, r) / a : 0); }
            for (int k = 0; k < SEEDS; ++k) {
                std::mt19937_64 rng(32452843ULL * (k + 1) + r); std::vector<double> p = w; std::shuffle(p.begin(), p.end(), rng);
                double n = 0;
                for (size_t i = 0; i < tr.size(); ++i) { double a = s.days[tr[i].day].atr; n += sim.run(tr[i], r, -1, -1, a > 0 ? p[i] * a : tr[i].tgt).pts - b.cost; }
                q.twin_mean += n / SEEDS;
                if (L[r].net > n + 1e-6) ++beat;
            }
        }
        if (r == X_TRAIL_PATH) {
            // Gate-shuffle twin (2026-10-18_direction_followups): the runner given to the same NUMBER of trades chosen at
            // random — does the straight path pick the right trades to let run?
            for (int k = 0; k < SEEDS; ++k) {
                std::mt19937_64 rng(49979687ULL * (k + 1) + r); std::vector<char> p = pgate; std::shuffle(p.begin(), p.end(), rng);
                double n = 0;
                for (size_t i = 0; i < tr.size(); ++i) { sim.set_path_gate(tr[i], p[i]); n += sim.run(tr[i], r).pts - b.cost; }
                q.twin_mean += n / SEEDS;
                if (L[r].net > n + 1e-6) ++beat;
            }
            for (size_t i = 0; i < tr.size(); ++i) sim.set_path_gate(tr[i], pgate[i]);
        }
        for (int k = 0; k < SEEDS && !durs.empty() && !exit_moves_stop(r) && !exit_moves_target(r) && r != X_TRAIL_PATH; ++k) {
            std::mt19937_64 rng(15485863ULL * (k + 1) + r);
            std::uniform_real_distribution<double> u(0, 1);
            double n = 0;
            for (size_t i = 0; i < tr.size(); ++i) {
                const bool cut = u(rng) < q_fire;
                const int at = cut ? tr[i].e + std::max(1, durs[rng() % durs.size()]) : tr[i].flat + 1;
                n += sim.run(tr[i], r, at).pts - b.cost;
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
         "beats the base exit per trade, beats the random-timing twin (a path-blind time exit with the rule's exit rate and\n"
         "holding times) in ≥ 99 % of " << SEEDS << " seeds (" << X__COUNT - 1 << " rules tested; the mext stop rules face a stop-width twin instead: their own widths shuffled across trades; trail_path a gate-shuffle twin: the runner on the same number of random trades), is better in ≥ 2 of 3 eras including 2023–26, and raises the prop\n"
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
    o << "\n## Prop (" << sp.prop_plan << ", " << sp.micro << " at " << f2(sp.micro_cost) << " pts)\n\n| exit | " << sp.micro << " | evals | passed | payouts | paid $/yr | break-even fee |\n|---|---|---|---|---|---|---|\n";
    prop_table(o, "base", pbase);
    for (int r = 1; r < X__COUNT; ++r) if (ro[r].pass) prop_table(o, exit_key(r), props_of(s, as_trades(r), sp).first);
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

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc >= 3 ? argv[1] : "";
    if (mode != "run" && mode != "meta" && mode != "exits") {
        std::cerr << "usage: edge_lab {run|meta|exits} SPEC.json [...] [--data DIR] [--out DIR] [--threads N] [--quick] [--book] [--page]\n";
        return 2;
    }
    Ctx c;
    std::vector<std::string> specs;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* f) { if (i + 1 >= argc) { std::cerr << f << " needs a value\n"; std::exit(2); } return std::string(argv[++i]); };
        if (a == "--data") c.data = need("--data");
        else if (a == "--out") c.out = need("--out");
        else if (a == "--threads") c.threads = std::max(1, std::stoi(need("--threads")));
        else if (a == "--quick") c.quick = true;
        else if (a == "--book") c.meta_book = true;
        else if (a == "--page") c.meta_page = true;
        else specs.push_back(a);
    }
    {   // FOMC decision days exported by scripts/edge_lab.sh from the calendar table
        std::ifstream f(c.data + "/fomc.txt"); std::string line;
        while (std::getline(f, line)) { int y, m, d; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &d) == 3) c.fomc.insert(y * 10000 + m * 100 + d); }
        if (c.fomc.empty()) std::cerr << "[edge_lab] warning: no FOMC dates in " << c.data << "/fomc.txt — fomc is always 0\n";
    }
    if (!c.vix.load(c.regime)) std::cerr << "[edge_lab] warning: no VIX history in " << c.regime << " — vix* are NaN\n";
    std::error_code mk; std::filesystem::create_directories(c.out + "/reports", mk);
    if (mk) { std::cerr << "[edge_lab] cannot create " << c.out << "/reports: " << mk.message() << "\n"; return 1; }
    int rc = 0;
    for (auto& path : specs) {
        try {
            std::ifstream f(path);
            if (!f) throw std::runtime_error("cannot open " + path);
            json j = json::parse(f);
            Spec sp = parse_spec(j);
            std::string md;
            json R = mode == "meta" ? run_meta(c, sp, md) : mode == "exits" ? run_exits(c, sp, md) : run_idea(c, sp, md);
            R["spec_path"] = path; R["spec"] = j;
            const std::string rid = R["id"].get<std::string>(), rname = R["name"].get<std::string>();
            std::ofstream(c.out + "/reports/" + rid + ".md") << md;
            std::ofstream(c.out + "/reports/" + rid + ".json") << R.dump(2);
            const bool fresh = access((c.out + "/results.csv").c_str(), F_OK) != 0;
            std::ofstream res(c.out + "/results.csv", std::ios::app);
            if (fresh) res << "run_at,id,name,why,verdict,stage_reached,quick,metrics,spec\n";
            char ts[32]; time_t now = time(nullptr); strftime(ts, sizeof ts, "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
            json M = R; M.erase("spec");
            res << ts << "," << csv_q(rid) << "," << csv_q(rname) << "," << csv_q(sp.why) << "," << csv_q(R["verdict"]) << ","
                << R["stage_reached"].get<int>() << "," << (c.quick ? "true" : "false") << "," << csv_q(M.dump()) << "," << csv_q(j.dump()) << "\n";
            std::cout << rid << ": " << R["verdict"].get<std::string>() << "  (oos " << R["oos"]["n"] << " trades, "
                      << f2(R["oos"]["avg_pts"].get<double>()) << " pts/trade net, PF " << f2(R["oos"]["pf"].get<double>()) << ")  → "
                      << c.out << "/reports/" << rid << ".md\n";
        } catch (const std::exception& e) {
            std::cerr << "[edge_lab] " << path << ": " << e.what() << "\n";
            rc = 1;
        }
    }
    return rc;
}
