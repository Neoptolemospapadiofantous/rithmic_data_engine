// meta.hpp — two ways to copy what a good discretionary trader does, on top of a signal that already passed edge_lab:
//
//   META   (take / skip): a second model sees the context at each signal (gap, overnight session, VWAP, room to
//          yesterday's extremes, pullback, VIX, order flow, ES, time of day …) and predicts the trade's result in R.
//          Retrained every January on all EARLIER years only; a signal is taken when the prediction is > 0
//          (pre-registered, never tuned). Primary model: boosted depth-2 trees (captures "these line up together");
//          a ridge regression on the same features is the cross-check.
//   EXITS  (thesis exits): the same trades, but closed when the reason for the trade is gone — price closes a
//          5-minute bar back through VWAP or through the RTH open, no progress after an hour, a runner with no
//          target that is closed on a 15-minute trail, or (2026-10-16) a 5-minute bar whose aggressor delta is
//          against the trade by ≥ 20 % of its volume. The hard stop always stays.
//
// Every claim faces a control built to fake it:
//   META   random skip — drop the same number of trades per year at random (1000 seeds);
//          shuffled labels — retrain on results shuffled within each training set (100 seeds);
//   EXITS  random timing — a path-blind time exit with the rule's exit rate and holding times (200 seeds): does the
//          MOMENT of the exit matter, or only how often and how long trades are held?
// Gates are written in run_meta() / run_exits() and the README; nothing is chosen after seeing the result.
#pragma once
#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "edge_lab.hpp"

namespace lab {

// ───────────────────────────── features at the decision bar ─────────────────────────────
// --page (2026-10-18, research/preregistered/2026-10-18_feature_study.md): the dashboard ORB page's reads as 12 more features,
// computed here with the page's own definitions (5-minute buckets since 18:00, EMA-14 of their closes, swings with 2 bars each
// side, BOS / CHoCH on a close through the last unbroken swing, the 15-minute range's break / failed-break state with a 0.05 ATR
// buffer, and the page's regime / reversal-zone scores).
inline std::vector<std::string> page_feature_names() {
    return {
        "er45",          // straightness of the last 45 one-minute closes (|net| / path)
        "acr60",         // lag-1 autocorrelation of the last 60 one-minute moves
        "sd_pos",        // dir·(px − VWAP)/σ — position in the VWAP SD bands
        "ema_dist",      // dir·(px − EMA14 of 5-min closes)/atr
        "ema_touches",   // completed RTH 5-min bars that straddled the EMA today (first bar of each visit)
        "struct",        // swing structure: +1 aligned with the trade, −1 against, 0 none yet
        "bos_age",       // minutes since the last BOS / CHoCH (NaN = none yet)
        "choch_against", // a CHoCH against the trade in the last 30 minutes (0/1)
        "or15_w",        // 15-minute opening range width / atr (NaN before 09:45)
        "or15_pos",      // dir·(px − range mid) / width
        "reg_score",     // the page's regime score 0–100
        "zone_score",    // the page's reversal-zone score 0–100
    };
}

inline std::vector<std::string> meta_feature_names(bool book = false, bool page = false) {
    std::vector<std::string> n = {
        "move",      // dir·(px − open)/atr          how far the open move has gone
        "gap",       // dir·(open − pc)/atr          gap in the trade's direction (NaN in the roll window)
        "on_rng",    // (onh − onl)/atr               overnight range
        "on_pos",    // dir·(open − mid(on))/(onh − onl)   open inside the overnight range, trade-aligned
        "on_break",  // beyond the overnight extreme in the trade's direction (/atr; < 0 = still inside)
        "vwap",      // dir·(px − vwap)/atr
        "room_pd",   // distance to yesterday's high (long) / low (short) /atr
        "pullback",  // share of today's range given back from the extreme in the trade's direction
        "rng_sofar", // (hod − lod)/atr
        "pret",      // dir·(pc − pc2)/atr           yesterday's move aligned (NaN in the roll window)
        "pibs",      // yesterday's close location, trade-aligned (1 = closed at the extreme we trade toward)
        "trend50",   // dir·(pc − sma50)/atr (NaN in the roll window)
        "prng",      // (ph − pl)/atr                 yesterday's range
        "vix", "vixc1", "vixpct",
        "flow",      // dir·cdelta/cvol               RTH aggressor imbalance so far (2023-03 on, else NaN)
        "es_move",   // dir·(ES px − ES open)/ES atr   does ES confirm? (NaN without ES data)
        "check",     // which check fired (0 = first time slot)
        "dow",
        "event",     // FOMC / NFP / opex day
    };
    if (book) for (const char* x : {
        "flow15",    // dir·(aggressor delta / volume) of the last 15 minutes (2023-03 on)
        "qimb5",     // dir·book qty imbalance within 10 bps, mean of the last 5 minutes (mbo_1m, 2023-03 … 2026-03)
        "cxl5",      // cancel-to-add ratio, mean of the last 5 minutes
    }) n.push_back(x);
    if (page) for (auto& x : page_feature_names()) n.push_back(x);
    return n;
}

class FeatureMaker {
public:
    FeatureMaker(Market& m, const Series* es, bool book = false, bool page = false) : m_(m), ev_(m), es_(es), book_(book), page_(page) {}

    std::vector<float> at(const Trade& t) {
        const Series& s = m_.s();
        const int i = t.e;             // decision bar = entry bar (delay 0): px = open[i], everything else is before i
        const double d = t.dir;
        const Day& D = s.days[t.day];
        auto v = [&](const char* n) { return value(n, i); };
        const double px = v("px"), op = v("open"), atr = D.atr, onh = v("onh"), onl = v("onl"), vw = v("vwap");
        const double hod = v("hod"), lod = v("lod"), roll = D.rollwin ? NAN : 1.0;
        const double onr = onh - onl, rr = hod - lod, cv = v("cvol");
        std::vector<float> f;
        auto push = [&](double x) { f.push_back(std::isfinite(x) ? (float)x : NAN); };
        push(d * (px - op) / atr);
        push(roll * d * (op - D.pc) / atr);
        push(onr / atr);
        push(onr > 0 ? d * (op - 0.5 * (onh + onl)) / onr : NAN);
        push((d > 0 ? px - onh : onl - px) / atr);
        push(d * (px - vw) / atr);
        push((d > 0 ? D.ph - px : px - D.pl) / atr);
        push(rr > 0 ? (d > 0 ? hod - px : px - lod) / rr : NAN);
        push(rr / atr);
        push(roll * d * (D.pc - D.pc2) / atr);
        push(d > 0 ? D.pibs : 1 - D.pibs);
        push(roll * d * (D.pc - D.sma50) / atr);
        push((D.ph - D.pl) / atr);
        push(D.vix); push(D.vixc1); push(D.vixpct);
        push(D.flow && cv > 0 ? d * v("cdelta") / cv : NAN);
        push(es_move(s.t[i], d));
        push(t.src);
        push(D.dow);
        push(D.fomc || D.nfp || D.opex ? 1 : 0);
        if (book_) {
            const double v15 = v("vsum(1,15)");
            push(D.flow && v15 > 0 ? d * v("dsum(1,15)") / v15 : NAN);
            push(s.qimb.size() == s.size() ? d * v("bk(qimb,5)") : NAN);
            push(s.cxl.size() == s.size() ? v("bk(cxl,5)") : NAN);
        }
        if (page_) {
            const PageReads P = page_reads(t);
            const double er = v("er(1,45)"), ac = v("acr(1,60)"), vsd = v("vsd");
            push(er); push(ac);
            push(vsd > 0 ? d * (px - vw) / vsd : NAN);
            push(std::isnan(P.ema) ? NAN : d * (px - P.ema) / atr);
            push(P.touches);
            push(P.state == 0 ? 0.0 : (P.state == 1 ? 1.0 : -1.0) * d);
            push(P.last_event_t > 0 ? (s.t[i] - P.last_event_t) / 60.0 : NAN);
            push(P.choch_dir != 0 && P.choch_dir != (int)d && s.t[i] - P.choch_t <= 30 * 60 ? 1.0 : 0.0);
            const double w = P.orh - P.orl;
            push(std::isnan(P.orh) ? NAN : w / atr);
            push(std::isnan(P.orh) || !(w > 0) ? NAN : d * (px - 0.5 * (P.orh + P.orl)) / w);
            // regime score (page formula): gap ≥ 0.1 ATR 40 (60 at ≥ 0.3); straight tape ≥ 0.30 on the right side 40 (60 at ≥ 0.45);
            // a cascade check fired +20; gap and tape agreeing +10; capped at 100
            double reg = 0;
            const double gap = D.rollwin || std::isnan(D.pc) ? 0 : (op - D.pc) / atr;
            const int gd = std::fabs(gap) >= 0.1 ? (gap > 0 ? 1 : -1) : 0;
            if (gd) reg += std::fabs(gap) >= 0.3 ? 60 : 40;
            const double c45 = v("cl(1,1)") - v("lag(45, cl(1,1))");
            const int md = c45 > 0 ? 1 : -1;
            const bool sided = !std::isnan(vw) && (md > 0 ? px > vw && px > op : px < vw && px < op);
            const bool mom_on = er >= 0.3 && sided;
            if (mom_on) reg += er >= 0.45 ? 60 : 40;
            if (P.cascade) reg += 20;
            if (gd && mom_on && gd == md) reg += 10;
            push(std::min(100.0, reg));
            // reversal-zone score (page formula): beyond ±3σ 40 (±2σ 25); within 0.05 ATR of a prior-day / overnight high or low
            // +20 each; ≥ 0.25 ATR from the EMA +15; a CHoCH against the trade in the last 30 min +25; a failed range break +15
            double zone = 0;
            if (vsd > 0) { const double z = std::fabs(px - vw) / vsd; zone += z >= 3 ? 40 : z >= 2 ? 25 : 0; }
            for (double lv : {D.ph, D.pl, onh, onl}) if (!std::isnan(lv) && std::fabs(px - lv) <= 0.05 * atr) zone += 20;
            if (!std::isnan(P.ema) && std::fabs(px - P.ema) >= 0.25 * atr) zone += 15;
            if (P.choch_dir != 0 && P.choch_dir != (int)d && s.t[i] - P.choch_t <= 30 * 60) zone += 25;
            if (P.failed) zone += 15;
            push(std::min(100.0, zone));
        }
        return f;
    }

private:
    struct PageReads {
        double ema = NAN;          // EMA-14 of completed 5-min closes since 18:00
        int touches = 0;           // RTH 5-min bars straddling the EMA, first of each visit
        int state = 0;             // swing structure: 1 bull, −1 bear, 0 none
        int64_t last_event_t = 0;  // epoch of the last BOS / CHoCH (bucket end)
        int choch_dir = 0; int64_t choch_t = 0;   // the last CHoCH's direction and time
        double orh = NAN, orl = NAN;              // 15-minute opening range (set from 09:45)
        bool failed = false;       // the range state machine is in RANGE (a break failed)
        bool cascade = false;      // a cascade check fired before the decision bar
    };
    // One scan of the day's bars before the decision bar i, with the ORB page's definitions.
    PageReads page_reads(const Trade& t) {
        const Series& s = m_.s(); const Day& D = s.days[t.day];
        PageReads P;
        const int i = t.e; const int64_t now = s.t[i];
        struct B5 { int64_t t0; double o, h, l, c; };
        std::vector<B5> b; b.reserve(300);
        for (int k = D.b0; k < i; ++k) {
            const int64_t k0 = s.t[k] / 300 * 300;
            if (!b.empty() && b.back().t0 == k0) { B5& x = b.back(); x.h = std::max(x.h, (double)s.h[k]); x.l = std::min(x.l, (double)s.l[k]); x.c = s.c[k]; }
            else b.push_back({k0, (double)s.o[k], (double)s.h[k], (double)s.l[k], (double)s.c[k]});
        }
        while (!b.empty() && b.back().t0 + 300 > now) b.pop_back();   // completed buckets only
        // 15-minute opening range from the 1-minute bars 09:30–09:44, known from 09:45
        if (D.rth0 >= 0 && tday_min(now) >= hm_to_tday(9, 45)) {
            double h = -1e18, l = 1e18;
            for (int k = D.rth0; k < i && tday_min(s.t[k]) < hm_to_tday(9, 45); ++k) { h = std::max(h, (double)s.h[k]); l = std::min(l, (double)s.l[k]); }
            if (h > -1e18) { P.orh = h; P.orl = l; }
        }
        // cascade checks: the close of the minute before 09:50 (≥ 0.25 ATR from the open), 10:30 / 11:00 / 11:30 (≥ 0.30)
        if (D.rth0 >= 0 && !std::isnan(D.ro)) {
            const int chk[4][2] = {{hm_to_tday(9, 50), 25}, {hm_to_tday(10, 30), 30}, {hm_to_tday(11, 0), 30}, {hm_to_tday(11, 30), 30}};
            for (auto& c : chk) {
                if (tday_min(now) < c[0]) break;
                double last = NAN; for (int k = D.rth0; k < i && tday_min(s.t[k]) < c[0]; ++k) last = s.c[k];
                if (!std::isnan(last) && std::fabs(last - D.ro) >= c[1] / 100.0 * D.atr) { P.cascade = true; break; }
            }
        }
        // the 5-minute scan: EMA-14, touches, swings (N = 2) with BOS / CHoCH, the range state machine
        const double a = 2.0 / 15, tol = 0.05 * D.atr;
        double e = NAN; bool away = true;
        struct Sw { double p; bool broken; int64_t t; };
        Sw sh{NAN, true, 0}, sl{NAN, true, 0};
        int rstate = 0;   // 0 wait, 1 trend, 2 range
        const int N = 2;
        for (size_t k = 0; k < b.size(); ++k) {
            const B5& x = b[k]; const int64_t end = x.t0 + 300;
            e = std::isnan(e) ? x.c : e + a * (x.c - e);
            const bool rth = tday_min(x.t0) >= kRthOpen;
            if (rth) { const bool touch = x.l <= e && x.h >= e; if (touch && away) ++P.touches; away = !touch; }
            const int c = (int)k - N;
            if (c >= N) {
                bool ish = true, isl = true;
                for (int j = c - N; j <= c + N; ++j) { if (j == c) continue; if (b[j].h >= b[c].h) ish = false; if (b[j].l <= b[c].l) isl = false; }
                if (ish) sh = {b[c].h, false, b[c].t0};
                if (isl) sl = {b[c].l, false, b[c].t0};
            }
            if (!sh.broken && !std::isnan(sh.p) && x.c > sh.p) { sh.broken = true; const bool ch = P.state == -1; P.state = 1; P.last_event_t = end; if (ch) { P.choch_dir = 1; P.choch_t = end; } }
            if (!sl.broken && !std::isnan(sl.p) && x.c < sl.p) { sl.broken = true; const bool ch = P.state == 1; P.state = -1; P.last_event_t = end; if (ch) { P.choch_dir = -1; P.choch_t = end; } }
            if (!std::isnan(P.orh) && tday_min(x.t0) >= hm_to_tday(9, 45)) {
                if ((rstate == 0 || rstate == 2) && (x.c > P.orh + tol || x.c < P.orl - tol)) rstate = 1;
                else if (rstate == 1 && x.c <= P.orh - tol && x.c >= P.orl + tol) rstate = 2;
            }
        }
        P.ema = e; P.failed = rstate == 2;
        return P;
    }
    double value(const std::string& name, int i) {
        auto it = nodes_.find(name);
        if (it == nodes_.end()) { auto n = parse(name); ev_.bind(*n); it = nodes_.emplace(name, std::move(n)).first; }
        return ev_.eval(*it->second, i, dc_);
    }
    double es_move(int64_t t, double d) const {
        if (!es_ || es_->t.empty()) return NAN;
        auto it = std::upper_bound(es_->t.begin(), es_->t.end(), t);        // the latest ES bar that has started by now
        if (it == es_->t.begin()) return NAN;
        --it;
        const int j = (int)(it - es_->t.begin());
        if (t - *it > 120) return NAN;                                       // none within the last 2 minutes
        const int ed = es_->day_of[j];
        if (ed < 0) return NAN;
        const Day& E = es_->days[ed];
        if (E.rth0 < 0 || j < E.rth0 || !(E.atr > 0)) return NAN;
        return d * (es_->o[j] - E.ro) / E.atr;
    }
    Market& m_;
    Evaluator ev_;
    DayCache dc_;
    const Series* es_;
    bool book_, page_;
    std::map<std::string, NodeP> nodes_;
};

// ───────────────────────────── models ─────────────────────────────
// Boosted depth-2 regression trees on decile bins (histogram splits). NaN sorts below every threshold.
struct Gbm {
    struct Tree { int f0 = -1; float t0 = 0; int f1[2] = {-1, -1}; float t1[2] = {0, 0}; double leaf[4] = {0, 0, 0, 0}; };
    double base = 0, shrink = 0.05;
    std::vector<Tree> trees;
    std::vector<double> gain;   // per feature, summed over splits (for the report)

    static float key(float x) { return std::isnan(x) ? -1e30f : x; }
    double predict(const std::vector<float>& x) const {
        double p = base;
        for (auto& t : trees) {
            if (t.f0 < 0) { p += shrink * t.leaf[0]; continue; }
            int side = key(x[t.f0]) <= t.t0 ? 0 : 1;
            int leaf = side * 2;
            if (t.f1[side] >= 0 && key(x[t.f1[side]]) > t.t1[side]) leaf += 1;
            p += shrink * t.leaf[leaf];
        }
        return p;
    }
};

struct GbmParams { int rounds = 150, min_leaf = 60, bins = 10; double shrink = 0.05; };

inline Gbm gbm_fit(const std::vector<std::vector<float>>& X, const std::vector<double>& y, const GbmParams& P = {}) {
    Gbm g; g.shrink = P.shrink;
    const int n = (int)X.size(), F = n ? (int)X[0].size() : 0;
    g.gain.assign(F, 0);
    if (n == 0) return g;
    g.base = std::accumulate(y.begin(), y.end(), 0.0) / n;
    // thresholds: interior quantiles of each feature (NaN as the lowest key)
    std::vector<std::vector<float>> th(F);
    std::vector<std::vector<uint8_t>> bin(F, std::vector<uint8_t>(n));
    for (int f = 0; f < F; ++f) {
        std::vector<float> col(n);
        for (int i = 0; i < n; ++i) col[i] = Gbm::key(X[i][f]);
        std::vector<float> srt = col; std::sort(srt.begin(), srt.end());
        for (int q = 1; q < P.bins; ++q) {
            float v = srt[(size_t)((double)q / P.bins * (n - 1))];
            if (th[f].empty() || v > th[f].back()) th[f].push_back(v);
        }
        if (!th[f].empty() && th[f].back() >= srt.back()) th[f].pop_back();   // a split must leave something on the right
        for (int i = 0; i < n; ++i) bin[f][i] = (uint8_t)(std::lower_bound(th[f].begin(), th[f].end(), col[i]) - th[f].begin());
    }
    std::vector<double> r(n);
    std::vector<double> pred(n, g.base);
    struct Split { int f = -1, b = -1; double gain = 0; };
    auto best_split = [&](const std::vector<int>& idx) {
        Split best;
        double S = 0; for (int i : idx) S += r[i];
        const int N = (int)idx.size();
        if (N < 2 * P.min_leaf) return best;
        const double base_score = S * S / N;
        std::vector<double> hs; std::vector<int> hc;
        for (int f = 0; f < F; ++f) {
            const int B = (int)th[f].size() + 1;
            if (B < 2) continue;
            hs.assign(B, 0); hc.assign(B, 0);
            for (int i : idx) { hs[bin[f][i]] += r[i]; ++hc[bin[f][i]]; }
            double sl = 0; int cl = 0;
            for (int b = 0; b + 1 < B; ++b) {
                sl += hs[b]; cl += hc[b];
                const int cr = N - cl;
                if (cl < P.min_leaf || cr < P.min_leaf) continue;
                const double sr = S - sl, gn = sl * sl / cl + sr * sr / cr - base_score;
                if (gn > best.gain) best = {f, b, gn};
            }
        }
        return best;
    };
    std::vector<int> all(n); std::iota(all.begin(), all.end(), 0);
    for (int it = 0; it < P.rounds; ++it) {
        for (int i = 0; i < n; ++i) r[i] = y[i] - pred[i];
        Gbm::Tree t;
        Split s0 = best_split(all);
        std::vector<int> part[4];
        if (s0.f < 0) {
            double m = 0; for (int i = 0; i < n; ++i) m += r[i];
            t.leaf[0] = m / n;
            g.trees.push_back(t);
            for (int i = 0; i < n; ++i) pred[i] += g.shrink * t.leaf[0];
            continue;
        }
        t.f0 = s0.f; t.t0 = th[s0.f][s0.b]; g.gain[s0.f] += s0.gain;
        std::vector<int> side[2];
        for (int i : all) side[bin[s0.f][i] <= s0.b ? 0 : 1].push_back(i);
        for (int sd = 0; sd < 2; ++sd) {
            Split s1 = best_split(side[sd]);
            if (s1.f >= 0) {
                t.f1[sd] = s1.f; t.t1[sd] = th[s1.f][s1.b]; g.gain[s1.f] += s1.gain;
                for (int i : side[sd]) part[sd * 2 + (bin[s1.f][i] <= s1.b ? 0 : 1)].push_back(i);
            } else part[sd * 2] = side[sd];
        }
        for (int l = 0; l < 4; ++l) {
            if (part[l].empty()) continue;
            double m = 0; for (int i : part[l]) m += r[i];
            t.leaf[l] = m / part[l].size();
            for (int i : part[l]) pred[i] += g.shrink * t.leaf[l];
        }
        g.trees.push_back(t);
    }
    return g;
}

// Ridge regression on standardised features (NaN → the training mean), intercept unpenalised.
struct Ridge {
    std::vector<double> mu, sd, beta; double y0 = 0;
    double predict(const std::vector<float>& x) const {
        double p = y0;
        for (size_t f = 0; f < beta.size(); ++f) { double z = std::isnan(x[f]) ? 0 : (x[f] - mu[f]) / sd[f]; p += beta[f] * z; }
        return p;
    }
};
inline Ridge ridge_fit(const std::vector<std::vector<float>>& X, const std::vector<double>& y, double lambda_per_n = 1.0) {
    Ridge m;
    const int n = (int)X.size(), F = n ? (int)X[0].size() : 0;
    m.mu.assign(F, 0); m.sd.assign(F, 1); m.beta.assign(F, 0);
    if (!n) return m;
    for (int f = 0; f < F; ++f) {
        double s = 0, ss = 0; int c = 0;
        for (auto& x : X) if (!std::isnan(x[f])) { s += x[f]; ss += x[f] * x[f]; ++c; }
        if (c) { m.mu[f] = s / c; double v = ss / c - m.mu[f] * m.mu[f]; m.sd[f] = v > 1e-12 ? std::sqrt(v) : 1; }
    }
    m.y0 = std::accumulate(y.begin(), y.end(), 0.0) / n;
    std::vector<std::vector<double>> A(F, std::vector<double>(F + 1, 0));
    for (int i = 0; i < n; ++i) {
        std::vector<double> z(F);
        for (int f = 0; f < F; ++f) z[f] = std::isnan(X[i][f]) ? 0 : (X[i][f] - m.mu[f]) / m.sd[f];
        for (int a = 0; a < F; ++a) { for (int b = 0; b < F; ++b) A[a][b] += z[a] * z[b]; A[a][F] += z[a] * (y[i] - m.y0); }
    }
    for (int a = 0; a < F; ++a) A[a][a] += lambda_per_n * n;
    for (int c = 0; c < F; ++c) {   // Gauss–Jordan (A is symmetric positive definite)
        int p = c; for (int r = c + 1; r < F; ++r) if (std::fabs(A[r][c]) > std::fabs(A[p][c])) p = r;
        std::swap(A[c], A[p]);
        const double piv = A[c][c];
        for (int k = c; k <= F; ++k) A[c][k] /= piv;
        for (int r = 0; r < F; ++r) if (r != c && A[r][c] != 0) { double f = A[r][c]; for (int k = c; k <= F; ++k) A[r][k] -= f * A[c][k]; }
    }
    for (int f = 0; f < F; ++f) m.beta[f] = A[f][F];
    return m;
}

// ───────────────────────────── META: yearly walk-forward take/skip ─────────────────────────────
struct MetaData {
    std::vector<Trade> tr;                    // base out-of-sample trades, in time order
    std::vector<std::vector<float>> X;
    std::vector<double> y;                    // net result in R (clipped −2 … +4 for training)
    std::vector<int> year;
};

inline MetaData meta_data(Market& m, const Series* es, const std::vector<Trade>& base, double cost, bool book = false, bool page = false) {
    MetaData d; FeatureMaker fm(m, es, book, page);
    for (const Trade& t : base) {
        const double atr = m.s().days[t.day].atr;
        const double R = t.stop > 0 ? t.stop : 0.25 * atr;   // no-stop ideas: R = 0.25 × ATR
        if (!(R > 0)) continue;
        d.tr.push_back(t);
        d.X.push_back(fm.at(t));
        d.y.push_back(std::clamp((t.pts - cost) / R, -2.0, 4.0));
        d.year.push_back(m.s().days[t.day].date / 10000);
    }
    return d;
}

// The same data with only the columns `keep` (feature selection refits).
inline MetaData meta_subset(const MetaData& d, const std::vector<int>& keep) {
    MetaData o = d;
    for (auto& x : o.X) { std::vector<float> r; r.reserve(keep.size()); for (int f : keep) r.push_back(x[f]); x = r; }
    return o;
}

// The per-year walk-forward models themselves (for permutation importance): fit on years < Y for every tested year Y.
template <class Fit>
auto meta_walk_models(const MetaData& d, const std::vector<double>& ytrain, int min_years, Fit fit) {
    using M = decltype(fit(std::vector<std::vector<float>>{}, std::vector<double>{}));
    std::vector<std::pair<int, M>> out;
    if (d.tr.empty()) return out;
    const int y0 = d.year.front();
    for (int Y = y0 + min_years; Y <= d.year.back(); ++Y) {
        std::vector<std::vector<float>> X; std::vector<double> y;
        for (size_t i = 0; i < d.tr.size(); ++i) if (d.year[i] < Y) { X.push_back(d.X[i]); y.push_back(ytrain[i]); }
        if (X.size() < 200) continue;
        out.emplace_back(Y, fit(X, y));
    }
    return out;
}

// For every year with ≥ min_years of earlier data, fit on years < Y, predict Y. Returns one prediction per trade
// (NaN = not tested). `ytrain` may be a shuffled copy of d.y (label control).
template <class Fit>
std::vector<double> meta_walk(const MetaData& d, const std::vector<double>& ytrain, int min_years, Fit fit) {
    std::vector<double> pred(d.tr.size(), NAN);
    if (d.tr.empty()) return pred;
    const int y0 = d.year.front();
    for (int Y = y0 + min_years; Y <= d.year.back(); ++Y) {
        std::vector<std::vector<float>> X; std::vector<double> y;
        for (size_t i = 0; i < d.tr.size(); ++i) if (d.year[i] < Y) { X.push_back(d.X[i]); y.push_back(ytrain[i]); }
        if (X.size() < 200) continue;
        auto model = fit(X, y);
        for (size_t i = 0; i < d.tr.size(); ++i) if (d.year[i] == Y) pred[i] = model.predict(d.X[i]);
    }
    return pred;
}

// ───────────────────────────── EXITS: thesis exits on the same trades ─────────────────────────────
// X_MEXT / X_MEXT_VS (2026-10-17): the hard stop moves to 1 tick beyond the RTH extreme made against the trade between 09:30 and the
// entry bar ("the morning's opposite extreme" — on a > 0.5 ATR trending morning it holds all day 83 % of the time), never tighter
// than the idea's own stop; target price and everything else unchanged. Their control is a stop-width twin, not a timing twin.
enum ExitRule { X_BASE, X_VWAP, X_OPEN, X_STALL, X_VWAP_STALL, X_VWAP_RUN, X_OPEN_RUN, X_TRAIL_RUN, X_FLOW, X_MEXT, X_MEXT_VS, X_TRAIL_REG, X_LIQ, X_LIQ_RUN, X__COUNT };
inline bool exit_moves_target(int r) { return r == X_LIQ || r == X_LIQ_RUN; }
inline bool exit_moves_stop(int r) { return r == X_MEXT || r == X_MEXT_VS; }
inline const char* exit_name(int r) {
    static const char* n[] = {"base (stop + target + 15:55)", "vwap: 5m close back through VWAP", "open: 5m close back through the RTH open",
                              "stall: < 0.5R in favour after 60 min", "vwap + stall", "vwap runner (no target)", "open runner (no target)",
                              "15m trail runner (no target)", "flow: 5m aggressor delta against ≥ 20 % of its volume",
                              "mext: stop beyond the morning's opposite extreme", "mext + vwap + stall",
                              "trail_reg: 15m trail runner only when the page's regime score ≥ 60 at entry, else base (2026-10-18 feature study, step 4)",
                              "liq: target = the nearest liquidity pool ≥ 1R beyond entry, 2 ticks before it (else the idea's target)", "liq_run: the same, no fallback target"};
    return n[r];
}
inline const char* exit_key(int r) {
    static const char* n[] = {"base", "vwap", "open", "stall", "vwap_stall", "vwap_run", "open_run", "trail_run", "flow", "mext", "mext_vwap_stall", "trail_reg", "liq", "liq_run"};
    return n[r];
}

struct ExitOut { int x = 0; double pts = 0, mae = 0, mfe = 0; bool thesis = false; };

// One position from t.e. The hard stop always applies; the target only when the rule keeps it. Thesis checks run on
// the close of each COMPLETED 5-minute (15 for the trail) bucket that started at or after the entry bucket; the exit
// fills at the open of the next bar. `force_at` > 0 replaces the thesis exit with a plain time exit at that bar
// (random-timing control). `stop_dist` >= 0 replaces the stop distance in points (the stop-width twin of the mext rules).
class ExitSim {
public:
    explicit ExitSim(Market& m) : m_(m), ev_(m) { vw_ = parse("vwap"); ev_.bind(*vw_); op_ = parse("open"); ev_.bind(*op_); }
    // trail_reg: which trades may trail (the page's regime score ≥ 60 at entry), keyed by (day, entry bar)
    void set_trail_gate(const Trade& t, bool ok) { gate_[(int64_t)t.day * 10000000LL + t.e] = ok; }
    bool trail_gate(const Trade& t) const { auto it = gate_.find((int64_t)t.day * 10000000LL + t.e); return it != gate_.end() && it->second; }

    // Liquidity pools known at entry (2026-10-18, research/preregistered/2026-10-18_liquidity_exit.md): prior-day high/low, the
    // overnight high/low, the 15-minute range high/low (from 09:45) and every unbroken 5-minute swing high/low since 18:00
    // (2 bars each side, confirmed 2 bars later). Returns the distance (points) to the nearest pool in the trade's direction
    // at least 1R away, less 2 ticks; NaN when there is none.
    double liq_target(const Trade& t) const {
        const Series& s = m_.s(); const Day& D = s.days[t.day];
        const int i = t.e, dir = t.dir; const double P = s.o[i];
        const double R = t.stop > 0 ? t.stop : 0.25 * D.atr;
        std::vector<double> pools;
        if (!std::isnan(D.ph)) pools.push_back(D.ph);
        if (!std::isnan(D.pl)) pools.push_back(D.pl);
        double onh = -1e18, onl = 1e18, orh = -1e18, orl = 1e18;
        for (int k = D.b0; k < i; ++k) {
            const int m = tday_min(s.t[k]);
            if (m < kRthOpen) { onh = std::max(onh, (double)s.h[k]); onl = std::min(onl, (double)s.l[k]); }
            else if (m < hm_to_tday(9, 45)) { orh = std::max(orh, (double)s.h[k]); orl = std::min(orl, (double)s.l[k]); }
        }
        if (onh > -1e18) { pools.push_back(onh); pools.push_back(onl); }
        if (orh > -1e18 && tday_min(s.t[i]) >= hm_to_tday(9, 45)) { pools.push_back(orh); pools.push_back(orl); }
        struct B5 { int64_t t0; double h, l, c; };
        std::vector<B5> b;
        for (int k = D.b0; k < i; ++k) {
            const int64_t k0 = s.t[k] / 300 * 300;
            if (!b.empty() && b.back().t0 == k0) { b.back().h = std::max(b.back().h, (double)s.h[k]); b.back().l = std::min(b.back().l, (double)s.l[k]); b.back().c = s.c[k]; }
            else b.push_back({k0, (double)s.h[k], (double)s.l[k], (double)s.c[k]});
        }
        while (!b.empty() && b.back().t0 + 300 > s.t[i]) b.pop_back();
        std::vector<double> sh, sl;   // unbroken swings
        const int N = 2;
        for (size_t k = 0; k < b.size(); ++k) {
            const int c = (int)k - N;
            if (c >= N) {
                bool ish = true, isl = true;
                for (int j = c - N; j <= c + N; ++j) { if (j == c) continue; if (b[j].h >= b[c].h) ish = false; if (b[j].l <= b[c].l) isl = false; }
                if (ish) sh.push_back(b[c].h);
                if (isl) sl.push_back(b[c].l);
            }
            sh.erase(std::remove_if(sh.begin(), sh.end(), [&](double p) { return b[k].c > p; }), sh.end());
            sl.erase(std::remove_if(sl.begin(), sl.end(), [&](double p) { return b[k].c < p; }), sl.end());
        }
        for (double p : sh) pools.push_back(p);
        for (double p : sl) pools.push_back(p);
        double best = NAN;
        for (double p : pools) { const double d = dir * (p - P); if (d >= R && (std::isnan(best) || d < best)) best = d; }
        return std::isnan(best) ? NAN : best - 0.5;
    }

    // The mext stop distance (points): 1 tick beyond the RTH extreme against the trade from 09:30 to the bar before entry,
    // never tighter than the idea's own stop. No stop on the idea → none here either.
    double mext_stop(const Trade& t) const {
        const Series& s = m_.s(); const Day& D = s.days[t.day];
        if (!(t.stop > 0) || D.rth0 < 0 || t.e <= D.rth0) return t.stop;
        double ext = t.dir > 0 ? 1e18 : -1e18;
        for (int k = D.rth0; k < t.e; ++k) ext = t.dir > 0 ? std::min(ext, (double)s.l[k]) : std::max(ext, (double)s.h[k]);
        return std::max((double)t.stop, t.dir * (s.o[t.e] - ext) + 0.25);
    }

    ExitOut run(const Trade& t, int rule, int force_at = -1, double stop_dist = -1, double tgt_dist = -1) {
        const Series& s = m_.s();
        const Day& D = s.days[t.day];
        const int e = t.e, dir = t.dir;
        const double P = s.o[e];
        double stop = exit_moves_stop(rule) ? mext_stop(t) : t.stop;
        if (stop_dist >= 0) stop = stop_dist;
        const bool reg_trail = rule == X_TRAIL_REG && trail_gate(t);
        const bool keep_tgt = !(rule == X_VWAP_RUN || rule == X_OPEN_RUN || rule == X_TRAIL_RUN || reg_trail);
        double tgt = keep_tgt ? t.tgt : 0;
        if (exit_moves_target(rule)) { const double lq = liq_target(t); tgt = !std::isnan(lq) ? lq : (rule == X_LIQ ? t.tgt : 0); }
        if (tgt_dist >= 0) tgt = tgt_dist;
        const double sp = stop > 0 ? P - dir * stop : NAN, tp = tgt > 0 ? P + dir * tgt : NAN;
        const bool use_thesis = force_at < 0 && rule != X_BASE && rule != X_MEXT && !(rule == X_TRAIL_REG && !reg_trail) && !exit_moves_target(rule);
        const bool vw = rule == X_VWAP || rule == X_VWAP_STALL || rule == X_VWAP_RUN || rule == X_MEXT_VS;
        const bool opn = rule == X_OPEN || rule == X_OPEN_RUN;
        const bool stall = rule == X_STALL || rule == X_VWAP_STALL || rule == X_MEXT_VS;
        const bool trail = rule == X_TRAIL_RUN || reg_trail;
        const bool flow = rule == X_FLOW && D.flow;                       // no aggressor data that day → no flow exit
        double b5d = 0, b5v = 0;                                          // the running 5-minute bucket's delta / volume
        const int64_t b5e = s.t[e] / 300, b15e = s.t[e] / 900;
        const int stall_bar = stall ? first_bar_at(s, D, tday_min(s.t[e]) + 60, e) : -1;
        const double stall_r = t.stop > 0 ? t.stop : 0.25 * D.atr;      // the idea's own R (no-stop trades: 0.25 × ATR)
        double mae = 0, mfe = 0, cur_lo = 1e18, cur_hi = -1e18, prev_lo = NAN, prev_hi = NAN;
        const double ro = ev_.eval(*op_, e, dc_);
        for (int k = e; k < t.flat; ++k) {
            const double o = s.o[k], h = s.h[k], l = s.l[k];
            if (k > e) {
                if (force_at > 0 && k >= force_at) { const double x = dir * (o - P); return {k, x, std::min(mae, x), std::max(mfe, x), true}; }
                if (use_thesis) {
                    bool out = false;
                    const int64_t b5 = s.t[k] / 300, b5p = s.t[k - 1] / 300;
                    if (b5 != b5p && b5p >= b5e) {                 // bar k opens a new 5m bucket; k−1 closed one
                        const double c5 = s.c[k - 1];
                        if (vw) { double v = ev_.eval(*vw_, k, dc_); if (!std::isnan(v) && dir * (c5 - v) < 0) out = true; }
                        if (opn && !std::isnan(ro) && dir * (c5 - ro) < 0) out = true;
                        if (flow && b5v > 0 && dir * b5d <= -0.2 * b5v) out = true;
                    }
                    if (stall && k == stall_bar && mfe < 0.5 * stall_r) out = true;
                    if (trail) {
                        const int64_t b15 = s.t[k] / 900, b15p = s.t[k - 1] / 900;
                        if (b15 != b15p && b15p >= b15e) {
                            const double c15 = s.c[k - 1];
                            if (!std::isnan(prev_lo) && (dir > 0 ? c15 < prev_lo : c15 > prev_hi)) out = true;
                            prev_lo = cur_lo; prev_hi = cur_hi; cur_lo = 1e18; cur_hi = -1e18;
                        }
                    }
                    if (out) { const double x = dir * (o - P); return {k, x, std::min(mae, x), std::max(mfe, x), true}; }
                }
            }
            const double adv = dir > 0 ? l - P : P - h, fav = dir > 0 ? h - P : P - l;
            if (stop > 0) {
                if (k > e && (dir > 0 ? o <= sp : o >= sp)) return {k, dir * (o - P), std::min(mae, dir * (o - P)), mfe, false};
                if (dir > 0 ? l <= sp : h >= sp) return {k, -stop, -stop, std::max(mfe, fav), false};
            }
            if (tgt > 0) {
                if (k > e && (dir > 0 ? o >= tp : o <= tp)) return {k, dir * (o - P), mae, std::max(mfe, dir * (o - P)), false};
                if (dir > 0 ? h >= tp : l <= tp) return {k, tgt, std::min(mae, adv), tgt, false};
            }
            mae = std::min(mae, adv); mfe = std::max(mfe, fav);
            cur_lo = std::min(cur_lo, l); cur_hi = std::max(cur_hi, h);
            if (flow) {
                if (k > e && s.t[k] / 300 != s.t[k - 1] / 300) { b5d = 0; b5v = 0; }   // bar k opens a new 5m bucket
                b5d += 2.0 * s.bv[k] - s.v[k]; b5v += s.v[k];
            }
        }
        if (t.flat < D.b1) return {t.flat, dir * (s.o[t.flat] - P), mae, mfe, false};
        return {D.b1 - 1, dir * (s.c[D.b1 - 1] - P), mae, mfe, false};
    }

private:
    Market& m_;
    Evaluator ev_;
    DayCache dc_;
    NodeP vw_, op_;
    std::map<int64_t, bool> gate_;
};

}  // namespace lab
