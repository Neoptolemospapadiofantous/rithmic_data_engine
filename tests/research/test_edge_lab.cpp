// test_edge_lab — rule language, trade path (stop before target, gap fills, flat), no look-ahead in running values,
// day facts, and the pipeline on a synthetic market with a planted edge (it must pass walk-forward and beat the
// random-direction control) and on one without (it must not).
#include "../../src/research/edge_lab.hpp"
#include "../../src/research/meta.hpp"

#include <cmath>
#include <cstdio>
#include <random>
#include <filesystem>
#include <fstream>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

using namespace lab;

static int64_t et(int ymd, int h, int m) { return timegm_ymd(ymd) - 12 * 3600 + h * 3600 + m * 60; }
static void bar(Series& s, int64_t t, double o, double h, double l, double c, double v = 10) {
    s.t.push_back(t); s.o.push_back((float)o); s.h.push_back((float)h); s.l.push_back((float)l); s.c.push_back((float)c);
    s.v.push_back((float)v); s.bv.push_back(0);
}

// N weekdays from 2020-01-06. Each RTH: flat at `base` until 09:50 except a move of `pre` (sign alternates by day),
// then the price keeps going `post` × that sign by 15:55 (post < 0 = reverses). Overnight bars are flat.
static Series synth(int ndays, double pre, double post, bool alternate = true) {
    Series s; s.sym = "NQ";
    int d = 20200106, made = 0;
    double base = 1000;
    while (made < ndays) {
        int wd = weekday_of(d);
        if (wd >= 1 && wd <= 5) {
            int prev = add_days(d, wd == 1 ? -3 : -1);
            for (int m = 18 * 60; m < 24 * 60; ++m) bar(s, et(prev, m / 60, m % 60), base, base + 0.5, base - 0.5, base);
            for (int m = 0; m < 17 * 60; ++m) {
                double sgn = alternate ? ((made % 2) ? -1 : 1) : 1;
                double p = base;
                if (m >= 9 * 60 + 30 && m < 9 * 60 + 50) p = base + sgn * pre * (m - 570) / 20.0;
                else if (m >= 9 * 60 + 50 && m < 16 * 60) p = base + sgn * (pre + post * (m - 590) / 370.0);
                else if (m >= 16 * 60) p = base + sgn * (pre + post);
                bar(s, et(d, m / 60, m % 60), p, p + 0.5, p - 0.5, p);
            }
            ++made;
        }
        d = add_days(d, 1);
    }
    return s;
}

int main() {
    // ── rule language
    {
        auto n = parse("1 + 2 * 3 == 7 && !(2 < 1)");
        CHECK(n->kind == Node::Num && n->v == 1);
        auto p = parse("abs(-k) * 2", {{"k", 3}});
        CHECK(p->kind == Node::Num && p->v == 6);
        auto v = parse("px - open > k", {{"k", 1}});
        CHECK(v->kind == Node::Bin);
        CHECK(Parser::apply("<", NAN, 1) == 0 && Parser::apply(">=", NAN, 1) == 0);
        bool threw = false;
        try { parse("1 +"); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    // ── trade path
    {
        Series s;
        bar(s, et(20200106, 10, 0), 100, 101, 99, 100);
        bar(s, et(20200106, 10, 1), 100, 106, 94, 100);    // touches both stop 95 and target 105 → stop first
        bar(s, et(20200106, 10, 2), 100, 100, 100, 100);
        SimOut a = simulate(s, 0, 1, 5, 5, 3, 3, 0);
        CHECK(a.x == 1 && a.pts == -5);
        Series g;
        bar(g, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(g, et(20200106, 10, 1), 90, 91, 89, 90);         // opens through the stop → fills at the open
        SimOut b = simulate(g, 0, 1, 5, 0, 2, 2, 0);
        CHECK(b.x == 1 && b.pts == -10);
        SimOut c = simulate(g, 0, -1, 0, 0, 1, 2, 0);         // no stop, flat at bar 1 → its open
        CHECK(c.x == 1 && c.pts == 10);
        Series t;
        bar(t, et(20200106, 10, 0), 100, 100, 100, 100);
        bar(t, et(20200106, 10, 1), 103, 110, 102, 104);      // opens below target 105, then reaches it
        SimOut d = simulate(t, 0, 1, 5, 5, 2, 2, 0);
        CHECK(d.x == 1 && d.pts == 5);
    }
    // ── managed exits: break-even and partial (2026-10-16)
    {
        Series s;
        bar(s, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(s, et(20200106, 10, 1), 100, 106, 99.6, 105);          // +6 ≥ 1R (5): break-even arms
        bar(s, et(20200106, 10, 2), 104, 104.5, 99, 100);          // back through the entry → exit at 100
        bar(s, et(20200106, 10, 3), 100, 100, 100, 100);
        CHECK(std::fabs(simulate(s, 0, 1, 5, 0, 4, 4, 0, 1, 0).pts - 0.0) < 1e-9);
        CHECK(std::fabs(simulate(s, 0, 1, 5, 0, 4, 4, 0).pts - 0.0) < 1e-9);   // plain: flat at bar 3's open = 100
        Series p;
        bar(p, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(p, et(20200106, 10, 1), 100, 105.5, 99.6, 105);        // half out at +5 (1R), the rest's stop → 100
        bar(p, et(20200106, 10, 2), 104, 104.5, 99.5, 100);        // the rest stopped at the entry
        bar(p, et(20200106, 10, 3), 100, 100, 100, 100);
        CHECK(std::fabs(simulate(p, 0, 1, 5, 10, 4, 4, 0, 0, 1).pts - 2.5) < 1e-9);
        Series q;
        bar(q, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(q, et(20200106, 10, 1), 100, 105.5, 99.6, 105);        // half out at +5
        bar(q, et(20200106, 10, 2), 105, 111, 104, 110);           // the rest reaches the 10-pt target
        bar(q, et(20200106, 10, 3), 110, 110, 110, 110);
        CHECK(std::fabs(simulate(q, 0, 1, 5, 10, 4, 4, 0, 0, 1).pts - 7.5) < 1e-9);
        CHECK(std::fabs(simulate(q, 0, 1, 5, 10, 4, 4, 0, 0, 0).pts - simulate(q, 0, 1, 5, 10, 4, 4, 0).pts) < 1e-12);
    }
    // ── managed exits: add a unit at +1R, trailing stop after +1R (2026-10-16)
    {
        Series a;   // +1R (105) adds a unit; the target (+10) closes both: 10 + (110 − 105) − 1 cost = 14
        bar(a, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(a, et(20200106, 10, 1), 100, 105.5, 99.6, 105);
        bar(a, et(20200106, 10, 2), 105, 111, 104, 110);
        bar(a, et(20200106, 10, 3), 110, 110, 110, 110);
        CHECK(std::fabs(simulate(a, 0, 1, 5, 10, 4, 4, 0, 0, 0, 1, 0, 1).pts - 14.0) < 1e-9);
        Series b;   // the unit is added, then price returns to the entry: both stopped there → 0 + (100 − 105) − 1 = −6
        bar(b, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(b, et(20200106, 10, 1), 100, 105.5, 99.6, 105);
        bar(b, et(20200106, 10, 2), 104, 104.5, 99, 100);
        bar(b, et(20200106, 10, 3), 100, 100, 100, 100);
        CHECK(std::fabs(simulate(b, 0, 1, 5, 10, 4, 4, 0, 0, 0, 1, 0, 1).pts + 6.0) < 1e-9);
        Series t;   // trail 1R (5) after +1R: best 106 → stop 101; best 112 → stop 107; the drop exits at 107
        bar(t, et(20200106, 10, 0), 100, 100.5, 99.5, 100);
        bar(t, et(20200106, 10, 1), 100, 106, 99.6, 105);
        bar(t, et(20200106, 10, 2), 105, 112, 106.5, 111);
        bar(t, et(20200106, 10, 3), 111, 111.5, 106, 106.5);
        bar(t, et(20200106, 10, 4), 106.5, 106.5, 106.5, 106.5);
        CHECK(std::fabs(simulate(t, 0, 1, 5, 0, 5, 5, 0, 0, 0, 0, 1).pts - 7.0) < 1e-9);
    }
    // ── day facts and no look-ahead
    {
        Series s = synth(30, 4, 10);
        paper::VixRegime vix; std::set<int> fomc{20200212};
        build_days(s, fomc, vix);
        int ok = 0; for (auto& d : s.days) ok += d.ok;
        CHECK(ok == 30);
        CHECK(std::isnan(s.days[5].atr) && !std::isnan(s.days[20].atr));
        const Day& D = s.days[20];
        CHECK(D.pc == s.days[19].rc);
        bool found = false; for (auto& d : s.days) if (d.date == 20200212) found = d.fomc;
        CHECK(found);
        Market m(s); Evaluator ev(m); DayCache dc;
        auto hod = parse("hod"); ev.bind(*hod);
        auto pxn = parse("px"); ev.bind(*pxn);
        const int i = first_bar_at(s, D, parse_hhmm("09:50"), D.b0);
        // hod at 09:50 = highest high of 09:30..09:49, never bar i itself
        double want = -1e18; for (int k = D.rth0; k < i; ++k) want = std::max(want, (double)s.h[k]);
        CHECK(std::fabs(ev.eval(*hod, i, dc) - want) < 1e-9);
        CHECK(ev.eval(*pxn, i, dc) == s.o[i]);
        auto e5 = parse("cl(5, 1)"); ev.bind(*e5);
        // last completed 5-minute bar before 09:50 is 09:45–09:49 → its close is bar 09:49's close
        CHECK(std::fabs(ev.eval(*e5, i, dc) - s.c[i - 1]) < 1e-6);
        // opening range / session helpers: known only once they are over, never including bar i
        auto orh = parse("rhi(5)"); ev.bind(*orh);
        const int i5 = first_bar_at(s, D, parse_hhmm("09:35"), D.b0);
        double w5 = -1e18; for (int k = D.rth0; k < i5; ++k) w5 = std::max(w5, (double)s.h[k]);
        CHECK(std::isnan(ev.eval(*orh, i5 - 1, dc)));
        CHECK(std::fabs(ev.eval(*orh, i5, dc) - w5) < 1e-9);
        auto a930 = parse("at(9, 30)"); ev.bind(*a930);
        CHECK(ev.eval(*a930, i, dc) == D.ro && std::isnan(ev.eval(*a930, D.rth0 - 1, dc)));
        auto hs = parse("hi_since(9, 30)"); ev.bind(*hs);
        CHECK(std::fabs(ev.eval(*hs, i, dc) - want) < 1e-9);
        bool threw = false;
        try { auto bad = parse("nosuchvar > 1"); ev.bind(*bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    // ── pipeline: planted edge (the first 20 minutes' direction continues)
    {
        Series s = synth(400, 4, 12);
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); Evaluator ev(m);
        Spec sp = parse_spec(json::parse(R"J({"id":"t","why":"test","params":{"k":[0.1,0.2]},
            "checks":[{"at":"09:50","when":"abs(px-open) >= k*atr","dir":"sign(px-open)"}],"stop":"0","flat":"15:55"})J"));
        std::vector<std::vector<Trade>> vt;
        for (auto& p : grid(sp)) {
            Variant v; v.p = p;
            v.checks.emplace_back(parse(sp.checks[0].when, p), parse(sp.checks[0].dir, p));
            ev.bind(*v.checks[0].first); ev.bind(*v.checks[0].second);
            v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); ev.bind(*v.stop); ev.bind(*v.target);
            vt.push_back(run_variant(ev, s, sp, v, 0));
        }
        CHECK(!vt[0].empty());
        double avg = 0; for (auto& t : vt[0]) avg += t.pts; avg /= std::max<size_t>(1, vt[0].size());
        CHECK(avg > 10);                                         // ≈ 12 pts minus the 09:50→15:55 bar edges
        for (auto& t : vt[0]) CHECK(t.dir == (s.o[t.e] > s.days[t.day].ro ? 1 : -1));
        WfOut w = walk(s, vt, 1.0, 3, 5);
        CHECK(!w.trades.empty() && net_of(w.trades, 1.0) > 0);
        DayCache cdc;
        auto dir_at = [&](const Trade&, int i) { double x = s.o[i] - s.days[s.day_of[i]].ro; return x > 0 ? 1 : x < 0 ? -1 : 0; };
        Controls c = controls(s, w.trades, 1.0, 50, 0, dir_at);
        CHECK(c.dir_pct > 0.95);
        CHECK(c.time_pct > 0.9 && c.time_mean < net_of(w.trades, 1.0));   // a later entry with the rule's own direction earns less
        PropOut po = prop_chain(s, w.trades, "select_daily_50k", 4, 1.5, 2.0);
        CHECK(po.evals >= 1);

        // no edge: the move reverses half the time by construction → the walk-forward must not show a real edge
        Series r = synth(400, 4, -4);
        build_days(r, {}, vix);
        Market mr(r); Evaluator er(mr);
        std::vector<std::vector<Trade>> rt;
        for (auto& p : grid(sp)) {
            Variant v; v.p = p;
            v.checks.emplace_back(parse(sp.checks[0].when, p), parse(sp.checks[0].dir, p));
            er.bind(*v.checks[0].first); er.bind(*v.checks[0].second);
            v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); er.bind(*v.stop); er.bind(*v.target);
            rt.push_back(run_variant(er, r, sp, v, 0));
        }
        WfOut wr = walk(r, rt, 1.0, 3, 5);
        CHECK(net_of(wr.trades, 1.0) <= 0);
    }
    // ── meta models: a planted rule must be learned, from earlier data only
    {
        std::mt19937_64 rng(42); std::normal_distribution<double> nz(0, 0.3); std::uniform_real_distribution<double> u(0, 1);
        std::vector<std::vector<float>> X; std::vector<double> y;
        for (int i = 0; i < 1500; ++i) {
            float a = (float)u(rng), b = (float)u(rng), c = (float)u(rng);
            if (i % 50 == 0) a = NAN;                                  // missing values must not break training
            X.push_back({a, b, c});
            y.push_back(((!std::isnan(a) && a > 0.5f && b > 0.5f) ? 1.0 : -0.5) + nz(rng));   // only "both" pays
        }
        Gbm g = gbm_fit(X, y);
        CHECK(g.predict({0.9f, 0.9f, 0.1f}) > 0.3 && g.predict({0.9f, 0.1f, 0.1f}) < 0 && g.predict({0.1f, 0.9f, 0.9f}) < 0);
        CHECK(g.gain[2] < 0.05 * (g.gain[0] + g.gain[1]));                // the noise feature is (nearly) ignored
        Ridge r = ridge_fit(X, y, 0.01);
        CHECK(r.beta[0] > 0 && r.beta[1] > 0 && std::fabs(r.beta[2]) < 0.5 * r.beta[0]);
        // walk-forward: a year is predicted only by a model fit on earlier years
        MetaData d;
        for (int i = 0; i < 1500; ++i) { d.X.push_back(X[i]); d.y.push_back(y[i]); d.year.push_back(2010 + i / 150); d.tr.push_back(Trade{}); }
        auto fit = [](const std::vector<std::vector<float>>& A, const std::vector<double>& b) { return gbm_fit(A, b); };
        auto p = meta_walk(d, d.y, 3, fit);
        CHECK(std::isnan(p[0]) && std::isnan(p[449]) && !std::isnan(p[450]));
        std::vector<double> ys = d.y; for (int i = 450; i < 1500; ++i) ys[i] = 99;     // future labels poisoned
        auto q = meta_walk(d, ys, 3, fit);
        CHECK(std::fabs(q[450] - p[450]) < 1e-9 && std::fabs(q[599] - p[599]) < 1e-9);  // year 2013 never sees 2013+
    }
    // ── thesis exits: base = the edge_lab path; a VWAP exit leaves a reversing trade early
    {
        Series s = synth(60, 6, -12);                                  // the open move reverses every day
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); Evaluator ev(m);
        Spec sp = parse_spec(json::parse(R"J({"id":"x","why":"test","checks":[{"at":"09:50","when":"1","dir":"sign(px-open)"}],"stop":"20","target":"3","flat":"15:55"})J"));
        Variant v; v.checks.emplace_back(parse(sp.checks[0].when), parse(sp.checks[0].dir));
        ev.bind(*v.checks[0].first); ev.bind(*v.checks[0].second);
        v.stop = parse(sp.stop); v.target = parse(sp.target); ev.bind(*v.stop); ev.bind(*v.target);
        auto tr = run_variant(ev, s, sp, v, 0);
        CHECK(tr.size() > 30);
        ExitSim sim(m);
        int same = 0, earlier = 0; double base = 0, vw = 0;
        for (auto& t : tr) {
            ExitOut b = sim.run(t, X_BASE), w = sim.run(t, X_VWAP);
            same += std::fabs(b.pts - t.pts) < 1e-3 && b.x == t.x;
            earlier += w.thesis && w.x < b.x; base += b.pts; vw += w.pts;
            ExitOut f = sim.run(t, X_VWAP, w.x);                       // a forced exit at the thesis bar = the thesis exit
            CHECK(std::fabs(f.pts - w.pts) < 1e-9);
            CHECK(w.mae <= std::min(0.0, w.pts) + 1e-9);               // the exit fill counts in the adverse excursion
        }
        CHECK(same == (int)tr.size());
        CHECK(earlier > (int)tr.size() / 2 && vw > base);              // out when the open move fails, before the 20-pt stop
    }
    // ── order flow helpers: dwin / vwin (window over before it is known), bk (minutes before the bar), flow exit
    {
        Series s = synth(30, 6, 12, false);                            // every day goes up after the open
        for (size_t k = 0; k < s.size(); ++k) s.bv[k] = 0.75f * s.v[k]; // aggressor delta = +0.5 × volume per bar
        paper::VixRegime vix; build_days(s, {}, vix);
        const Day& D = s.days[20];
        CHECK(D.flow);
        s.qimb.assign(s.size(), NAN); s.cxl.assign(s.size(), NAN); s.bidq.assign(s.size(), NAN); s.askq.assign(s.size(), NAN);
        const int i940 = first_bar_at(s, D, parse_hhmm("09:40"), D.b0), i950 = first_bar_at(s, D, parse_hhmm("09:50"), D.b0);
        for (int k = i940; k < i950; ++k) s.qimb[k] = 0.2f;
        Market m(s); Evaluator ev(m); DayCache dc;
        auto dw = parse("dwin(9,30,9,35)"), vw = parse("vwin(9,30,9,35)"), bq = parse("bk(qimb,5)");
        ev.bind(*dw); ev.bind(*vw); ev.bind(*bq);
        const int i934 = first_bar_at(s, D, parse_hhmm("09:34"), D.b0), i935 = first_bar_at(s, D, parse_hhmm("09:35"), D.b0);
        CHECK(std::isnan(ev.eval(*dw, i934, dc)));                        // the window is not over at 09:34
        CHECK(std::fabs(ev.eval(*dw, i935, dc) - 25.0) < 1e-6 && std::fabs(ev.eval(*vw, i950, dc) - 50.0) < 1e-6);
        CHECK(std::fabs(ev.eval(*bq, i950, dc) - 0.2) < 1e-6);             // 09:45–09:49
        CHECK(std::fabs(ev.eval(*bq, i940 + 2, dc) - 0.2) < 1e-6);         // only 09:40–09:41 have data → their mean
        CHECK(std::isnan(ev.eval(*bq, i940, dc)));                         // no book in the 5 minutes before 09:40
        bool threw = false;
        try { auto bad = parse("bk(nosuch,5)"); ev.bind(*bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
        // flow exit: a long whose following bars are sold aggressively closes at the first completed 5-minute bar
        Series r = s;
        for (size_t k = 0; k < r.size(); ++k) if (tday_min(r.t[k]) >= parse_hhmm("10:00")) r.bv[k] = 0.2f * r.v[k];   // delta −0.6 × volume
        build_days(r, {}, vix);
        Market mr(r); ExitSim sim(mr);
        const Day& R = r.days[20];
        Trade t; t.day = 20; t.e = first_bar_at(r, R, parse_hhmm("10:00"), R.b0); t.dir = 1; t.stop = 50; t.tgt = 0;
        t.flat = first_bar_at(r, R, parse_hhmm("15:55"), R.b0);
        ExitOut base = sim.run(t, X_BASE), fl = sim.run(t, X_FLOW);
        CHECK(fl.thesis && fl.x == first_bar_at(r, R, parse_hhmm("10:05"), R.b0) && base.x == t.flat);
        Trade sh = t; sh.dir = -1;                                        // the same flow is WITH a short: no exit
        CHECK(!sim.run(sh, X_FLOW).thesis);
        // mext (2026-10-17): the stop moves to 1 tick beyond the morning's low, never tighter than the idea's own stop
        CHECK(std::fabs(sim.mext_stop(t) - 50.0) < 1e-6);                 // flat synthetic morning: the own stop is wider
        Series w = r;
        const Day& W = w.days[20];
        const double P = w.o[t.e];
        const int j940 = first_bar_at(w, W, parse_hhmm("09:40"), W.b0), i1030 = first_bar_at(w, W, parse_hhmm("10:30"), W.b0);
        w.l[j940] = (float)(P - 80);                                      // morning low 80 below the entry
        w.l[i1030] = (float)(P - 60);                                     // later dip: through the own stop, not the morning low
        Market mw(w); ExitSim sw(mw);
        CHECK(std::fabs(sw.mext_stop(t) - 80.25) < 1e-6);
        ExitOut wb = sw.run(t, X_BASE), wm = sw.run(t, X_MEXT);
        CHECK(std::fabs(wb.pts + 50.0) < 1e-6 && wb.x == i1030);           // base: stopped at −50 on the 10:30 dip
        CHECK(wm.x == t.flat && !wm.thesis);                              // mext: survives to the flat
        CHECK(std::fabs(sw.run(t, X_MEXT, -1, 50.0).pts + 50.0) < 1e-6);   // stop_dist overrides the width (the twin)
        Trade ns = t; ns.stop = 0;
        CHECK(sw.mext_stop(ns) == 0);                                      // no stop on the idea → none here
    }
    // ── liquidity levels: window high/low, equal highs, volume profile, last week, floor/ceil
    {
        Series s = synth(40, 6, 12, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); Evaluator ev(m); DayCache dc;
        const Day& D = s.days[25];
        auto wh = parse("whi(20,0,3,0)"), wl = parse("wlo(20,0,3,0)"), eq = parse("eqhi(5,24,0.03)"), fl = parse("floor(px/100)*100");
        ev.bind(*wh); ev.bind(*wl); ev.bind(*eq); ev.bind(*fl);
        const int i950 = first_bar_at(s, D, parse_hhmm("09:50"), D.b0), i200 = first_bar_at(s, D, parse_hhmm("02:00"), D.b0);
        CHECK(std::isnan(ev.eval(*wh, i200, dc)));                         // Asia window not over at 02:00
        CHECK(std::fabs(ev.eval(*wh, i950, dc) - 1000.5) < 1e-6 && std::fabs(ev.eval(*wl, i950, dc) - 999.5) < 1e-6);
        CHECK(std::fabs(ev.eval(*eq, i200, dc) - 1000.5) < 1e-6);         // flat overnight: every high is equal
        CHECK(ev.eval(*fl, i950, dc) == 1000);
        // hh / ll: highest high / lowest low of the last n completed tf bars (was always NaN before 2026-10-16)
        auto h3 = parse("hh(5,3)"), l3 = parse("ll(5,3)"); ev.bind(*h3); ev.bind(*l3);
        double wh3 = -1e18, wl3 = 1e18;
        for (int k = i950 - 15; k < i950; ++k) { wh3 = std::max(wh3, (double)s.h[k]); wl3 = std::min(wl3, (double)s.l[k]); }
        CHECK(std::fabs(ev.eval(*h3, i950, dc) - wh3) < 1e-6 && std::fabs(ev.eval(*l3, i950, dc) - wl3) < 1e-6);
        int pk = 24; while (!s.days[pk].ok) --pk;                          // the previous trading day
        CHECK(!std::isnan(s.days[pk].poc) && D.ppoc == s.days[pk].poc && D.pval <= D.ppoc && D.ppoc <= D.pvah);
        // last week's RTH high = the highest RTH high of the previous Monday–Friday
        const int wk = add_days(D.date, -(weekday_of(D.date) - 1));
        double want = -1e18;
        for (auto& x : s.days) if (x.ok && x.date >= add_days(wk, -7) && x.date < wk) want = std::max(want, x.rh);
        CHECK(std::fabs(D.pwh - want) < 1e-9);
    }
    // ── trend-follower crosses (xma) and the pair market (xs / xr)
    {
        Series s; s.sym = "NQ";
        int d = 20200106, made = 0;
        auto day_at = [&](double p_on, double p_rth) {      // flat overnight at p_on, flat RTH at p_rth
            const int wd = weekday_of(d), prev = add_days(d, wd == 1 ? -3 : -1);
            for (int m = 18 * 60; m < 24 * 60; ++m) bar(s, et(prev, m / 60, m % 60), p_on, p_on + 0.5, p_on - 0.5, p_on);
            for (int m = 0; m < 17 * 60; ++m) { const double p = m >= 9 * 60 + 30 ? p_rth : p_on; bar(s, et(d, m / 60, m % 60), p, p + 0.5, p - 0.5, p); }
        };
        while (made < 205) {
            if (weekday_of(d) >= 1 && weekday_of(d) <= 5) { const double p = made == 203 ? 110.0 : 100.0; day_at(made == 204 ? 110.0 : 100.0, p); ++made; }
            d = add_days(d, 1);
        }
        paper::VixRegime vix; build_days(s, {}, vix);
        int last = (int)s.days.size() - 1; while (!s.days[last].ok) --last;
        CHECK(s.days[last].xma == 3 && s.days[last - 1].xma == 0);   // yesterday's 110 close crossed all three averages
        // pair = the same market at 2× the price: its move from the open in its own ATR equals ours; xr = our return
        Series p2 = s; for (auto* v : {&p2.o, &p2.h, &p2.l, &p2.c}) for (auto& x : *v) x *= 2.0f;
        build_days(p2, {}, vix);
        Market m(s); m.pair = &p2; Evaluator ev(m); DayCache dc;
        auto xs = parse("xs"), own = parse("(px - open) / atr"), xr = parse("xr"); ev.bind(*xs); ev.bind(*own); ev.bind(*xr);
        const Day& D = s.days[last];
        const int i = first_bar_at(s, D, parse_hhmm("10:00"), D.b0);
        CHECK(!std::isnan(ev.eval(*xs, i, dc)) && std::fabs(ev.eval(*xs, i, dc) - ev.eval(*own, i, dc)) < 1e-6);
        CHECK(std::fabs(ev.eval(*xr, i, dc) - (s.c[i - 1] / D.pc - 1.0)) < 1e-6);   // since the prior RTH close
        Market solo(s); Evaluator e2(solo); auto xs2 = parse("xs"); e2.bind(*xs2);
        CHECK(std::isnan(e2.eval(*xs2, i, dc)));                                     // no pair → NaN
    }
    // ── macro day facts: the last observation dated BEFORE the day, changes vs the observation before that
    {
        Series s = synth(5, 6, 12, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        const std::string dir = (std::filesystem::temp_directory_path() / "edge_lab_macro_test").string();
        std::filesystem::create_directories(dir);
        {
            std::ofstream f(dir + "/macro.csv");
            f << "20200103,4.00,3.00,100.0,90,120\n";       // Friday
            f << "20200106,4.10,3.05,,95,121\n";            // Monday (no dollar print)
            f << "20200107,4.05,3.00,101.0,,\n";
        }
        CHECK(load_macro(dir, s) > 0);
        const Day* tue = nullptr; for (auto& d : s.days) if (d.date == 20200107) tue = &d;
        CHECK(tue && std::fabs(tue->y10 - 4.10) < 1e-9 && std::fabs(tue->y10c - 10.0) < 1e-6);   // Monday's 4.10 vs 4.00
        CHECK(tue && std::fabs(tue->vvix - 95) < 1e-9 && std::isnan(tue->usdc));   // one dollar print before Tuesday → no change
        const Day* wed = nullptr; for (auto& d : s.days) if (d.date == 20200108) wed = &d;
        CHECK(wed && std::fabs(wed->usdc - 1.0) < 1e-6 && std::fabs(wed->y10c + 5.0) < 1e-6);   // 100 → 101; 4.10 → 4.05
        std::filesystem::remove_all(dir);
    }
    // ── look-ahead canaries: nothing may read a bar that has not opened yet
    {
        Series s = synth(30, 6, 12, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); Evaluator ev(m);
        bool threw = false;
        try { auto bad = parse("lag(-5, px)"); ev.bind(*bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);                                                    // a negative lag is refused
        // pair with a missing minute: xs must use the pair's previous bar, never the next one
        Series p2 = s; build_days(p2, {}, vix);
        const Day& D = s.days[20];
        const int i = first_bar_at(s, D, parse_hhmm("10:00"), D.b0);
        Series hole; hole.sym = "PAIR";
        for (size_t k = 0; k < p2.size(); ++k) if ((int)k != i) bar(hole, p2.t[k], p2.o[k] * 2, p2.h[k] * 2, p2.l[k] * 2, p2.c[k] * 2);
        build_days(hole, {}, vix);
        m.pair = &hole; DayCache dc;
        auto xs = parse("xs"); ev.bind(*xs);
        const Day& H = hole.days[hole.day_of[i - 1]];
        const double want = (hole.o[i - 1] - H.ro) / H.atr;              // the bar before the hole, not the one after it
        CHECK(std::fabs(ev.eval(*xs, i, dc) - want) < 1e-6);
    }
    // ── rvol: RTH volume so far vs its 20-day average at the same minute (identical days → exactly 1)
    {
        Series s = synth(30, 6, 12, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); Evaluator ev(m); DayCache dc;
        auto rv = parse("rvol"); ev.bind(*rv);
        const Day& late = s.days[25]; const Day& early = s.days[5];
        const int i = first_bar_at(s, late, parse_hhmm("10:30"), late.b0), j = first_bar_at(s, early, parse_hhmm("10:30"), early.b0);
        CHECK(std::fabs(ev.eval(*rv, i, dc) - 1.0) < 1e-6);
        CHECK(std::isnan(ev.eval(*rv, j, dc)));                  // fewer than 20 days of history
    }
    // ── er / acr (2026-10-17): efficiency ratio and lag-1 autocorrelation of bar moves
    {
        Series up, zz;
        for (int k = 0; k < 12; ++k) {
            bar(up, et(20240102, 10, k), 100 + k, 100 + k, 100 + k, 100 + k);                 // a straight line up
            const double z = 100 + (k % 2);                                                    // 100, 101, 100, 101 …
            bar(zz, et(20240102, 10, k), z, z, z, z);
        }
        paper::VixRegime vx; build_days(up, {}, vx); build_days(zz, {}, vx);
        Market mu(up), mz(zz);
        const auto& eu = mu.series("er", 1, 4); const auto& au = mu.series("acr", 1, 4);
        const auto& ez = mz.series("er", 1, 4); const auto& az = mz.series("acr", 1, 4);
        CHECK(std::isnan(eu[3]) && std::fabs(eu[4] - 1.0) < 1e-12 && std::fabs(eu[11] - 1.0) < 1e-12);   // straight line: er 1
        CHECK(std::fabs(au[11] - 1.0) < 1e-12);                                                          // equal moves: acr +1
        CHECK(std::fabs(ez[11] - 0.0) < 1e-12 && std::fabs(az[11] + 1.0) < 1e-12);                       // zig-zag: er 0, acr −1
        CHECK(std::isnan(au[4]) && !std::isnan(au[5]));                                                  // acr needs n + 1 moves
    }
    // ── releases.csv: cpi / ppi flags, nfp replaced by the real release days (2026-10-17)
    {
        Series s = synth(10, 6, 12, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        char tmpl[] = "/tmp/test_edge_lab_relXXXXXX"; const std::string dir = mkdtemp(tmpl);
        const int d2 = s.days[2].date, d3 = s.days[3].date, d4 = s.days[4].date;
        FILE* f = std::fopen((dir + "/releases.csv").c_str(), "w");
        std::fprintf(f, "%d,cpi\n%d,ppi\n%d,nfp\n", d2, d3, d4); std::fclose(f);
        for (auto& d : s.days) d.nfp = true;                     // the first-Friday guess must be overwritten
        CHECK(load_events(dir, s) == 2);
        CHECK(s.days[2].cpi && !s.days[2].ppi && !s.days[2].nfp);
        CHECK(s.days[3].ppi && !s.days[3].cpi);
        CHECK(s.days[4].nfp && !s.days[5].nfp && !s.days[0].cpi);
        Market m(s); Evaluator ev(m); DayCache dc;
        auto c = parse("cpi"); ev.bind(*c);
        CHECK(ev.eval(*c, first_bar_at(s, s.days[2], parse_hhmm("10:30"), s.days[2].b0), dc) == 1);
        CHECK(ev.eval(*c, first_bar_at(s, s.days[3], parse_hhmm("10:30"), s.days[3].b0), dc) == 0);
        std::remove((dir + "/releases.csv").c_str()); rmdir(dir.c_str());
    }
    // ── market structure (2026-10-18, LuxAlgo SMC): swings, order blocks, fair value gaps, the ORB 50 % target
    {
        Series s = synth(30, 0, 0, false);                                 // flat 1000 (h 1000.5 / l 999.5), ATR 1
        paper::VixRegime vix; build_days(s, {}, vix);                      // once for the day index, again after the edits
        const int d = 20;
        auto set5 = [&](int h, int m, double o, double hi, double lo, double c) {   // one 5-minute bucket = 5 identical 1m bars
            const Day& D = s.days[d];
            const int k0 = first_bar_at(s, D, hm_to_tday(h, m), D.b0);
            for (int k = k0; k < k0 + 5; ++k) { s.o[k] = (float)o; s.h[k] = (float)hi; s.l[k] = (float)lo; s.c[k] = (float)c; }
        };
        set5(10, 0, 1000, 1010, 999.5, 1005);     // swing high 1010 (pivot, 2 bars each side → confirmed when 10:10 completes)
        set5(10, 15, 1004, 1004.5, 1001, 1002);   // the last down-close bar before the break = the order block [1001, 1004.5]
        set5(10, 20, 1002, 1012, 1001.5, 1011);   // close 1011 > 1010: break of structure
        set5(10, 25, 1011, 1011.5, 1003, 1009);   // trades into the block (low 1003 ≤ 1004.5), closes above its top → the entry
        set5(10, 30, 1009, 1009.5, 999, 1000);    // close 1000 < 1001: the block is mitigated (and a new swing low 999)
        set5(11, 5, 1000, 1006, 1000, 1005);
        set5(11, 10, 1005, 1006, 1002, 1004);     // bar-1 (11:00) high 1000.5 < bar-3 low 1002: bullish FVG [1000.5, 1002]
        set5(11, 15, 1004, 1004.5, 1001, 1003);   // into the gap (low 1001 < 1002), close above its top → the entry
        set5(11, 20, 1003, 1003.5, 999.5, 1000);  // close 1000 < 1000.5: the gap is filled
        build_days(s, {}, vix);
        const Day& D = s.days[d];
        Market m(s); Evaluator ev(m); DayCache dc;
        auto at = [&](int h, int mi) { return first_bar_at(s, D, hm_to_tday(h, mi), D.b0); };
        auto obh = parse("obh(5,2)"), obl = parse("obl(5,2)"), obhs = parse("obhs(5,2)"), swh = parse("swh(5,2)"), swl = parse("swl(5,2)");
        for (auto* n : {&obh, &obl, &obhs, &swh, &swl}) ev.bind(**n);
        CHECK(std::isnan(ev.eval(*swh, at(10, 10), dc)) && std::fabs(ev.eval(*swh, at(10, 15), dc) - 1010) < 1e-6);   // confirmed 2 bars later
        CHECK(std::isnan(ev.eval(*obh, at(10, 20), dc)));                                                               // the breaking bar is not complete yet
        CHECK(std::fabs(ev.eval(*obh, at(10, 25), dc) - 1004.5) < 1e-6 && std::fabs(ev.eval(*obl, at(10, 25), dc) - 1001) < 1e-6);
        CHECK(std::fabs(ev.eval(*obh, at(10, 30), dc) - 1004.5) < 1e-6);                                               // a touch does not mitigate
        CHECK(std::isnan(ev.eval(*obh, at(10, 35), dc)) && std::isnan(ev.eval(*obhs, at(10, 35), dc)));                 // closed below its low
        CHECK(std::fabs(ev.eval(*swl, at(10, 45), dc) - 999) < 1e-6 && std::isnan(ev.eval(*swl, at(10, 40), dc)));
        auto entry = parse("lag(5, obh(5,2)) == obh(5,2) && lo(5,1) <= obh(5,2) && cl(5,1) > obh(5,2)"); ev.bind(*entry);
        CHECK(ev.eval(*entry, at(10, 30), dc) == 1 && ev.eval(*entry, at(10, 25), dc) == 0);                           // never on the breaking bar itself
        auto fvgt = parse("fvgt(5)"), fvgb = parse("fvgb(5)"), fvgts = parse("fvgts(5)");
        for (auto* n : {&fvgt, &fvgb, &fvgts}) ev.bind(**n);
        CHECK(std::isnan(ev.eval(*fvgt, at(11, 10), dc)));
        CHECK(std::fabs(ev.eval(*fvgt, at(11, 15), dc) - 1002) < 1e-6 && std::fabs(ev.eval(*fvgb, at(11, 15), dc) - 1000.5) < 1e-6);
        CHECK(std::fabs(ev.eval(*fvgt, at(11, 20), dc) - 1002) < 1e-6 && std::isnan(ev.eval(*fvgt, at(11, 25), dc)) && std::isnan(ev.eval(*fvgts, at(11, 20), dc)));
        auto fentry = parse("lag(5, fvgt(5)) == fvgt(5) && lo(5,1) < fvgt(5) && cl(5,1) > fvgt(5)"); ev.bind(*fentry);
        CHECK(ev.eval(*fentry, at(11, 20), dc) == 1 && ev.eval(*fentry, at(11, 15), dc) == 0);
        bool threw = false;
        try { auto bad = parse("obh(5)"); ev.bind(*bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
        // orbt: 09:30–09:44 range 999.5–1000.5 (width 1) → long levels 1001 / 1001.5; the nearest ≥ 1R, 2 ticks before it
        ExitSim sim(m);
        Trade t; t.day = d; t.e = at(10, 0); t.dir = 1; t.stop = 0.4f; t.tgt = 0.8f; t.flat = at(15, 55);
        CHECK(std::fabs(sim.orb_target(t) - 0.5) < 1e-6);
        t.stop = 1.2f; CHECK(std::fabs(sim.orb_target(t) - 1.0) < 1e-6);
        t.stop = 2.0f; CHECK(std::isnan(sim.orb_target(t)) && std::fabs(sim.level_or_fallback(t, X_ORBT) - t.tgt) < 1e-6);
        Trade early = t; early.e = at(9, 40); CHECK(std::isnan(sim.orb_target(early)));
        Trade sh = t; sh.dir = -1; sh.stop = 0.4f; CHECK(std::fabs(sim.orb_target(sh) - 0.5) < 1e-6);   // 999 is 1.0 below the 1000 entry
    }
    // ── trail_path gate: path efficiency at entry (2026-10-18_direction_followups) — 09:30→09:50 rises 10 pts, bars ±0.5
    {
        Series s = synth(30, 10, 0, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        Market m(s); ExitSim sim(m);
        const Day& D = s.days[20];
        Trade t; t.day = 20; t.e = first_bar_at(s, D, hm_to_tday(9, 50), D.b0); t.dir = 1; t.stop = 1; t.tgt = 2; t.flat = first_bar_at(s, D, hm_to_tday(15, 55), D.b0);
        CHECK(std::fabs(sim.path_eff(t) - 10.0 / 10.5) < 1e-6);   // |1010 − 1000| / (1010 − 999.5)
        Trade early = t; early.e = D.rth0; CHECK(std::isnan(sim.path_eff(early)));
        CHECK(std::string(exit_key(X_TRAIL_PATH)) == "trail_path");
    }
    // ── Wyckoff phase inputs (2026-10-18_wyckoff_phases.md): a steady uptrend of +1 per session, ATR 1
    {
        Series s = synth(80, 0, 0, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        int sess = 0;   // +1 per ok session (s.days also holds partial days)
        for (size_t k = 0; k < s.days.size(); ++k) {
            for (int i = s.days[k].b0; i < s.days[k].b1; ++i) { s.o[i] += (float)sess; s.h[i] += (float)sess; s.l[i] += (float)sess; s.c[i] += (float)sess; }
            if (s.days[k].ok) ++sess;
        }
        build_days(s, {}, vix);
        const Day& D = s.days[75];
        CHECK(std::fabs(D.e20 - 1) < 1e-6 && std::fabs(D.r20 - 20) < 1e-6 && std::fabs(D.r60 - 40) < 1e-6);
        CHECK(std::fabs(D.uv20 - 1) < 1e-6 && std::fabs(D.p20 - 1) < 1e-6);
        CHECK(std::isnan(s.days[15].e20) && std::isnan(s.days[40].r60) && !std::isnan(s.days[40].e20));   // 21 / 61 sessions needed
        Market m(s); Evaluator ev(m); DayCache dc;
        auto e = parse("e20 + r20 + r60 + uv20 + p20"); ev.bind(*e);
        CHECK(std::fabs(ev.eval(*e, first_bar_at(s, D, hm_to_tday(10, 0), D.b0), dc) - 63) < 1e-6);
    }
    // ── LuxAlgo's own definitions, mode 1 (2026-10-18_luxalgo_faithful.md): the lowest-low block, the filtered / wick-filled gap
    {
        Series s = synth(30, 0, 0, false);
        paper::VixRegime vix; build_days(s, {}, vix);
        const int d = 20;
        auto set5 = [&](int h, int m, double o, double hi, double lo, double c) {
            const Day& D = s.days[d];
            const int k0 = first_bar_at(s, D, hm_to_tday(h, m), D.b0);
            for (int k = k0; k < k0 + 5; ++k) { s.o[k] = (float)o; s.h[k] = (float)hi; s.l[k] = (float)lo; s.c[k] = (float)c; }
        };
        set5(10, 0, 1000, 1010, 999.5, 1005);     // swing high 1010; 10:05 / 10:10 stay flat (low 999.5, the lowest after it)
        set5(10, 15, 1004, 1004.5, 1001, 1002);   // mode 0's block (last down-close); mode 1 takes 10:10 [999.5, 1000.5]
        set5(10, 20, 1002, 1012, 1001.5, 1011);   // break of structure
        set5(10, 25, 1011, 1011.5, 1003, 1009);
        set5(10, 30, 1009, 1009.5, 999.8, 1000);  // closes below mode 0's block, above mode 1's → only mode 0 mitigated
        set5(11, 5, 1000, 1006, 1000, 1005);      // bar 2: closes above bar-1 high 1000.5 with a large body
        set5(11, 10, 1005, 1006, 1002, 1004);     // bar 3: low 1002 > 1000.5 → gap [1000.5, 1002] in both modes
        set5(11, 15, 1004, 1004.5, 1000.2, 1003); // trades below the bottom, closes above: mode 1 filled, mode 0 not
        set5(12, 0, 1000.6, 1000.7, 1000.6, 1000.6);   // bar 2 with no body → LuxAlgo rejects the gap below
        set5(12, 5, 1001.5, 1002, 1001, 1001.5);       // bar 3: low 1001 > 11:55 high 1000.5 → mode 0 gap [1000.5, 1001]
        build_days(s, {}, vix);
        const Day& D = s.days[d];
        Market m(s); Evaluator ev(m); DayCache dc;
        auto at = [&](int h, int mi) { return first_bar_at(s, D, hm_to_tday(h, mi), D.b0); };
        auto obh0 = parse("obh(5,2)"), obh1 = parse("obh(5,2,1)"), obl1 = parse("obl(5,2,1)");
        auto fv0 = parse("fvgt(5)"), fv1 = parse("fvgt(5,1)"), fb1 = parse("fvgb(5,1)");
        for (auto* n : {&obh0, &obh1, &obl1, &fv0, &fv1, &fb1}) ev.bind(**n);
        CHECK(std::fabs(ev.eval(*obh1, at(10, 25), dc) - 1000.5) < 1e-6 && std::fabs(ev.eval(*obl1, at(10, 25), dc) - 999.5) < 1e-6);
        CHECK(std::fabs(ev.eval(*obh0, at(10, 25), dc) - 1004.5) < 1e-6);                      // mode 0 unchanged
        CHECK(std::isnan(ev.eval(*obh0, at(10, 35), dc)) && std::fabs(ev.eval(*obh1, at(10, 35), dc) - 1000.5) < 1e-6);
        CHECK(std::fabs(ev.eval(*fv1, at(11, 15), dc) - 1002) < 1e-6 && std::fabs(ev.eval(*fb1, at(11, 15), dc) - 1000.5) < 1e-6);
        CHECK(std::isnan(ev.eval(*fv1, at(11, 20), dc)) && std::fabs(ev.eval(*fv0, at(11, 20), dc) - 1002) < 1e-6);   // wick fill
        CHECK(std::isnan(ev.eval(*fv1, at(12, 10), dc)) && std::fabs(ev.eval(*fv0, at(12, 10), dc) - 1001) < 1e-6);   // body filter
        bool threw = false;
        try { auto bad = parse("fvgt(5,2)"); ev.bind(*bad); } catch (const std::exception&) { threw = true; }
        CHECK(threw);
    }
    std::printf("test_edge_lab: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
