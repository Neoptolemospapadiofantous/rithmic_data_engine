// edge_lab.hpp — one pipeline every strategy idea goes through, the same way, before it may reach the paper fleet.
//
//   1 SCREEN       every parameter variant over all history (1-minute bars), net at the REAL cost per contract
//   2 WALK-FORWARD each month trades the variant that was best over the previous `train_months` (never its own month)
//   3 CONTROLS     the walk-forward trades vs the same trades in a random direction and at a random time in the same
//                  window (N seeds each) — the real result must beat `control_pct` of both
//   4 ROBUSTNESS   eras 2010–18 / 2019–22 / 2023–26, cost × 1.5, entry one bar late, confirm market, plateau —
//                  ≥ 5 of 7, and the latest era (2023–26) must be positive
//   5 PROP FIT     the walk-forward trades through a Tradeify plan (payout_sim.hpp) as an eval → funded → re-buy chain
// An idea PASSES only if every gate passes; the report names the first gate that failed and why.
//
// Spec (JSON, research/ideas/*.json):
//   { "id", "name", "why", "symbol": "NQ", "confirm": "ES",
//     "params": {"k": [0.2, 0.25, 0.3]},                        every combination is one variant
//     "checks": [{"at": "09:50", "when": "abs(px-open) >= k*atr", "dir": "sign(px-open)"}, …]   fixed-time decisions, in order
//  or "scan": {"tf": 5, "from": "10:00", "to": "15:00", "when": "…", "dir": "…"}               every tf-bar open in the window
//     "max_trades": 1, "flat": "15:55", "stop": "0.25*atr", "target": "2" (R multiple, 0 = none), "max_bars": 0 }
// Decision at bar i uses only bars before i (px = open of bar i); the entry fills at open[i + delay]. Inside a bar the
// stop is assumed hit before the target (conservative); a bar that opens through the stop fills at its open.
// One position at a time; a later decision waits for the exit. Variables and functions: see bind() / README in
// research/README.md.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <deque>
#include <vector>

#include <nlohmann/json.hpp>

#include "../paper/payout_sim.hpp"
#include "../paper/walk_forward.hpp"
#include "bars.hpp"
#include "expr.hpp"

namespace lab {

using json = nlohmann::json;

// ───────────────────────────── indicators on time-bucketed bars ─────────────────────────────
struct Agg {                       // one timeframe of the continuous series
    int tf = 1;
    std::vector<double> o, h, l, c, v, d;
    std::vector<int> comp;         // 1m bar i → last agg bar COMPLETED before i (−1 none)
};

class Market {
public:
    explicit Market(Series& s) : s_(s) {}
    Series& s() { return s_; }
    const Series& s() const { return s_; }
    const Series* pair = nullptr;   // a second market for xs / xr (spec "pair"), aligned by wall-clock time

    const Agg& agg(int tf) {
        auto it = agg_.find(tf);
        if (it != agg_.end()) return it->second;
        Agg a; a.tf = tf; a.comp.assign(s_.size(), -1);
        const int64_t w = (int64_t)tf * 60;
        int64_t cur = INT64_MIN; int last_done = -1;
        for (size_t i = 0; i < s_.size(); ++i) {
            int64_t b = s_.t[i] / w;
            if (b != cur) {                          // bar i opens a new bucket → the previous one is complete
                if (cur != INT64_MIN) last_done = (int)a.o.size() - 1;
                cur = b;
                a.o.push_back(s_.o[i]); a.h.push_back(s_.h[i]); a.l.push_back(s_.l[i]); a.c.push_back(s_.c[i]);
                a.v.push_back(s_.v[i]); a.d.push_back(delta(i));
            } else {
                a.h.back() = std::max(a.h.back(), (double)s_.h[i]); a.l.back() = std::min(a.l.back(), (double)s_.l[i]);
                a.c.back() = s_.c[i]; a.v.back() += s_.v[i]; a.d.back() += delta(i);
            }
            a.comp[i] = last_done;
        }
        return agg_.emplace(tf, std::move(a)).first->second;
    }

    // Series value per agg bar (inclusive), computed once. fn ∈ ema sma rsi atrb hh ll vsum dsum er acr.
    const std::vector<double>& series(const std::string& fn, int tf, int n) {
        const std::string key = fn + "|" + std::to_string(tf) + "|" + std::to_string(n);
        auto it = ser_.find(key);
        if (it != ser_.end()) return it->second;
        const Agg& a = agg(tf);
        const size_t N = a.c.size();
        std::vector<double> out(N, NAN);
        if (n < 1) throw std::runtime_error(fn + ": period must be ≥ 1");
        if (fn == "ema") {
            double al = 2.0 / (n + 1), e = NAN;
            for (size_t k = 0; k < N; ++k) { e = std::isnan(e) ? a.c[k] : e + al * (a.c[k] - e); if ((int)k >= n - 1) out[k] = e; }
        } else if (fn == "sma" || fn == "vsum" || fn == "dsum") {
            const auto& src = fn == "sma" ? a.c : fn == "vsum" ? a.v : a.d;
            double sum = 0; int nan_in = 0;
            for (size_t k = 0; k < N; ++k) {
                if (std::isnan(src[k])) ++nan_in; else sum += src[k];
                if ((int)k >= n) { if (std::isnan(src[k - n])) --nan_in; else sum -= src[k - n]; }
                if ((int)k >= n - 1) out[k] = nan_in ? NAN : (fn == "sma" ? sum / n : sum);
            }
        } else if (fn == "rsi") {
            double up = 0, dn = 0;
            for (size_t k = 1; k < N; ++k) {
                double ch = a.c[k] - a.c[k - 1], u = std::max(ch, 0.0), w = std::max(-ch, 0.0);
                if ((int)k <= n) { up += u / n; dn += w / n; } else { up = (up * (n - 1) + u) / n; dn = (dn * (n - 1) + w) / n; }
                if ((int)k >= n) out[k] = up + dn > 0 ? 100 * up / (up + dn) : 50;
            }
        } else if (fn == "atrb") {
            double at = NAN;
            for (size_t k = 1; k < N; ++k) {
                double tr = std::max({a.h[k] - a.l[k], std::fabs(a.h[k] - a.c[k - 1]), std::fabs(a.l[k] - a.c[k - 1])});
                at = std::isnan(at) ? tr : (at * (n - 1) + tr) / n;
                if ((int)k >= n) out[k] = at;
            }
        } else if (fn == "hh" || fn == "ll") {
            const bool hi = fn == "hh";
            for (size_t k = (size_t)n - 1; k < N; ++k) {   // first full window ends at bar n − 1
                double m = hi ? -1e18 : 1e18;
                for (int j = 0; j < n; ++j) m = hi ? std::max(m, a.h[k - j]) : std::min(m, a.l[k - j]);
                out[k] = m;
            }
        } else if (fn == "er") {                        // efficiency ratio: |net move| / path over the last n bars (0 chop … 1 straight line)
            for (size_t k = (size_t)n; k < N; ++k) {
                double path = 0; for (int j = 0; j < n; ++j) path += std::fabs(a.c[k - j] - a.c[k - j - 1]);
                out[k] = path > 0 ? std::fabs(a.c[k] - a.c[k - n]) / path : 0;
            }
        } else if (fn == "acr") {                       // lag-1 autocorrelation of the last n bar-to-bar moves (> 0 trending, < 0 reverting)
            for (size_t k = (size_t)n + 1; k < N; ++k) {
                double num = 0, den = 0;
                for (int j = 0; j < n; ++j) { const double r = a.c[k - j] - a.c[k - j - 1], p = a.c[k - j - 1] - a.c[k - j - 2]; num += r * p; den += r * r; }
                out[k] = den > 0 ? num / den : 0;
            }
        } else throw std::runtime_error("unknown indicator " + fn);
        return ser_.emplace(key, std::move(out)).first->second;
    }

    // RTH volume up to (not including) bar i divided by its average at the same minute of the session over the previous
    // 20 ok days (NaN outside RTH, before 20 days of history, or when the baseline is 0). Computed once.
    const std::vector<float>& rvol() {
        if (!rvol_.empty()) return rvol_;
        rvol_.assign(s_.size(), NAN);
        std::deque<std::vector<double>> hist;   // cumulative RTH volume by minute index, last 20 ok days
        for (const Day& d : s_.days) {
            if (d.rth0 < 0) continue;
            std::vector<double> cum; double c = 0;
            for (int i = d.rth0; i < d.rth1; ++i) {
                const size_t m = cum.size();
                if (hist.size() == 20) {
                    double base = 0; int n = 0;
                    for (auto& h : hist) if (m < h.size()) { base += h[m]; ++n; }
                    if (n == 20 && base > 0) rvol_[i] = (float)(c / (base / n));
                }
                cum.push_back(c);                     // volume BEFORE bar i
                c += s_.v[i];
            }
            if (d.ok) { hist.push_back(std::move(cum)); if (hist.size() > 20) hist.pop_front(); }
        }
        return rvol_;
    }

private:
    std::vector<float> rvol_;
    double delta(size_t i) const {
        int d = s_.day_of[i];
        if (d < 0 || !s_.days[d].flow) return NAN;
        return 2.0 * s_.bv[i] - s_.v[i];
    }
    Series& s_;
    std::map<int, Agg> agg_;
    std::map<std::string, std::vector<double>> ser_;
};

// ───────────────────────────── binding + evaluation ─────────────────────────────
enum VarId {
    V_px, V_t, V_open, V_hod, V_lod, V_dhi, V_dlo, V_onh, V_onl, V_vwap, V_vsd, V_cdelta, V_cvol,
    V_atr, V_pc, V_ph, V_pl, V_pc2, V_pibs, V_sma50, V_rsi2, V_gap, V_prng, V_pret,
    V_vix, V_vixc1, V_vixc5, V_vixpct, V_dow, V_tdom, V_tdleft, V_fomc, V_opex, V_nfp, V_rollwin,
    V_year, V_month, V_flow, V_mtd, V_prehol,
    V_ppoc, V_pvah, V_pval, V_pwh, V_pwl, V_pmh, V_pml, V_npoca, V_npocb,
    V_sma100, V_sma200, V_xma, V_xs, V_xr, V_vts, V_vvix, V_skew, V_y10, V_y2, V_y10c, V_usdc, V_earn, V_rvol, V_cpi, V_ppi, V__count
};
inline const std::map<std::string, int>& var_names() {
    static const std::map<std::string, int> m = {
        {"px", V_px}, {"t", V_t}, {"open", V_open}, {"hod", V_hod}, {"lod", V_lod}, {"dhi", V_dhi}, {"dlo", V_dlo},
        {"onh", V_onh}, {"onl", V_onl}, {"vwap", V_vwap}, {"vsd", V_vsd}, {"cdelta", V_cdelta}, {"cvol", V_cvol},
        {"atr", V_atr}, {"pc", V_pc}, {"ph", V_ph}, {"pl", V_pl}, {"pc2", V_pc2}, {"pibs", V_pibs}, {"sma50", V_sma50},
        {"rsi2", V_rsi2}, {"gap", V_gap}, {"prng", V_prng}, {"pret", V_pret}, {"vix", V_vix}, {"vixc1", V_vixc1},
        {"vixc5", V_vixc5}, {"vixpct", V_vixpct}, {"dow", V_dow}, {"tdom", V_tdom}, {"tdleft", V_tdleft},
        {"fomc", V_fomc}, {"opex", V_opex}, {"nfp", V_nfp}, {"rollwin", V_rollwin}, {"year", V_year},
        {"month", V_month}, {"flow", V_flow}, {"mtd", V_mtd}, {"prehol", V_prehol},
        {"ppoc", V_ppoc}, {"pvah", V_pvah}, {"pval", V_pval}, {"pwh", V_pwh}, {"pwl", V_pwl}, {"pmh", V_pmh}, {"pml", V_pml},
        {"npoca", V_npoca}, {"npocb", V_npocb}, {"sma100", V_sma100}, {"sma200", V_sma200}, {"xma", V_xma},
        {"xs", V_xs}, {"xr", V_xr}, {"vts", V_vts}, {"vvix", V_vvix}, {"skew", V_skew}, {"y10", V_y10}, {"y2", V_y2},
        {"y10c", V_y10c}, {"usdc", V_usdc}, {"earn", V_earn}, {"rvol", V_rvol}, {"cpi", V_cpi}, {"ppi", V_ppi}};
    return m;
}

// Per-day running values, rebuilt when the evaluation moves to another day (one per thread).
struct DayCache {
    int day = -1;
    std::vector<double> rhi, rlo, dhi, dlo, vw, vsd, cd, cv;   // value at bar b0+k = over bars [anchor, b0+k)
    std::map<long, std::vector<double>> extra;                 // session-anchored helpers (hi_since, rhi, at …)
    std::map<std::string, std::vector<std::vector<double>>> smc;   // market-structure state per bar ("ob|tf|n", "fvg|tf"), see smc_fn()
};

class Evaluator {
public:
    Evaluator(Market& m) : m_(m) {}

    // Binds names to slots and precomputes every indicator series the tree needs (call before threads start).
    void bind(Node& n) {
        if (n.kind == Node::Var) {
            auto it = var_names().find(n.name);
            if (it == var_names().end()) throw std::runtime_error("unknown variable '" + n.name + "'");
            n.slot = it->second;
            if (n.slot == V_rvol) m_.rvol();                 // build once, before threads start
            return;
        }
        if (n.kind == Node::Call && n.name == "bk") { bind_bk(n); return; }
        for (auto& c : n.a) bind(*c);
        if (n.kind != Node::Call) return;
        static const std::vector<std::string> pure = {"abs", "sign", "min", "max", "hm", "floor", "ceil"};
        if (std::find(pure.begin(), pure.end(), n.name) != pure.end()) return;
        if (n.name == "lag") {
            if (n.a.size() != 2 || n.a[0]->kind != Node::Num) throw std::runtime_error("lag(k, expr): k must be a constant");
            if (n.a[0]->v < 0) throw std::runtime_error("lag(k, expr): k must be ≥ 0 (a negative lag reads the future)");
            return;
        }
        if (n.name == "eqhi" || n.name == "eqlo") {   // equal highs / lows on completed tf bars (see call())
            if (n.a.size() != 3) throw std::runtime_error(n.name + "(tf, n, tol): three constants");
            for (auto& x : n.a) if (x->kind != Node::Num) throw std::runtime_error(n.name + ": arguments must be constants");
            const int tf = (int)n.a[0]->v;
            if (tf < 1 || 1440 % tf != 0 || n.a[1]->v < 2) throw std::runtime_error(n.name + ": tf must divide 1440, n ≥ 2");
            m_.agg(tf);
            return;
        }
        if (is_smc(n.name)) {   // order blocks / fair value gaps / swings on completed tf bars since 18:00 (see smc_fn())
            const bool fvg = n.name.rfind("fvg", 0) == 0;
            if (n.a.size() != (fvg ? 1u : 2u)) throw std::runtime_error(n.name + (fvg ? "(tf)" : "(tf, n)") + ": constants only");
            for (auto& x : n.a) if (x->kind != Node::Num) throw std::runtime_error(n.name + ": arguments must be constants");
            const int tf = (int)n.a[0]->v;
            if (tf < 1 || 1440 % tf != 0 || (!fvg && n.a[1]->v < 1)) throw std::runtime_error(n.name + ": tf must divide 1440, n ≥ 1");
            m_.agg(tf);
            return;
        }
        if (n.name == "dwin" || n.name == "vwin" || n.name == "whi" || n.name == "wlo") {   // window delta / volume / high / low
            if (n.a.size() != 4) throw std::runtime_error(n.name + "(h1, m1, h2, m2): four constants");
            for (auto& x : n.a) if (x->kind != Node::Num) throw std::runtime_error(n.name + ": arguments must be constants");
            return;
        }
        static const std::vector<std::string> day_fn = {"rhi", "rlo", "at", "hi_since", "lo_since"};
        if (std::find(day_fn.begin(), day_fn.end(), n.name) != day_fn.end()) {
            const size_t want = (n.name == "rhi" || n.name == "rlo") ? 1 : 2;
            if (n.a.size() != want) throw std::runtime_error(n.name + ": wrong number of arguments");
            for (auto& x : n.a) if (x->kind != Node::Num) throw std::runtime_error(n.name + ": arguments must be constants");
            if (want == 2) parse_hhmm(std::to_string((int)n.a[0]->v) + ":" + std::to_string((int)n.a[1]->v));
            return;
        }
        static const std::vector<std::string> ser = {"ema", "sma", "rsi", "atrb", "hh", "ll", "vsum", "dsum", "er", "acr"};
        static const std::vector<std::string> at = {"cl", "op", "hi", "lo"};
        const bool is_ser = std::find(ser.begin(), ser.end(), n.name) != ser.end();
        const bool is_at = std::find(at.begin(), at.end(), n.name) != at.end();
        if (!is_ser && !is_at) throw std::runtime_error("unknown function '" + n.name + "'");
        if (n.a.size() != 2 || n.a[0]->kind != Node::Num || n.a[1]->kind != Node::Num)
            throw std::runtime_error(n.name + "(tf, n): both arguments must be constants or parameters");
        int tf = (int)n.a[0]->v, k = (int)n.a[1]->v;
        if (tf < 1 || 1440 % tf != 0) throw std::runtime_error(n.name + ": tf must divide 1440 minutes");
        m_.agg(tf);
        if (is_ser) m_.series(n.name, tf, k);
        else if (k < 1) throw std::runtime_error(n.name + ": k must be ≥ 1");
    }

    // bk(field, n): mean of an order-book field (Series::qimb / cxl / bidq / askq) over the n completed minutes before
    // the bar, same trading day, NaN minutes skipped (NaN when none — no book that day).
    void bind_bk(Node& n) {
        static const std::vector<std::string> fields = {"qimb", "cxl", "bidq", "askq"};
        if (n.a.size() != 2 || n.a[0]->kind != Node::Var || std::find(fields.begin(), fields.end(), n.a[0]->name) == fields.end()
            || n.a[1]->kind != Node::Num || n.a[1]->v < 1)
            throw std::runtime_error("bk(field, n): field is qimb / cxl / bidq / askq, n a constant ≥ 1");
        n.a[0]->slot = (int)(std::find(fields.begin(), fields.end(), n.a[0]->name) - fields.begin());
        if (m_.s().qimb.size() != m_.s().size()) throw std::runtime_error("bk(): no order-book data loaded for " + m_.s().sym);
    }

    double eval(const Node& n, int i, DayCache& dc) {
        switch (n.kind) {
            case Node::Num: return n.v;
            case Node::Var: return var(n.slot, i, dc);
            case Node::Neg: return -eval(*n.a[0], i, dc);
            case Node::Not: { double x = eval(*n.a[0], i, dc); return std::isnan(x) ? NAN : (x == 0); }
            case Node::Bin: {
                if (n.op == "&&") { double x = eval(*n.a[0], i, dc); if (!(x != 0 && !std::isnan(x))) return 0; double y = eval(*n.a[1], i, dc); return y != 0 && !std::isnan(y); }
                if (n.op == "||") { double x = eval(*n.a[0], i, dc); if (x != 0 && !std::isnan(x)) return 1; double y = eval(*n.a[1], i, dc); return y != 0 && !std::isnan(y); }
                return Parser::apply(n.op, eval(*n.a[0], i, dc), eval(*n.a[1], i, dc));
            }
            case Node::Call: return call(n, i, dc);
        }
        return NAN;
    }

private:
    double call(const Node& n, int i, DayCache& dc) {
        const std::string& f = n.name;
        if (f == "abs") return std::fabs(eval(*n.a[0], i, dc));
        if (f == "sign") return Parser::sgn(eval(*n.a[0], i, dc));
        if (f == "min") return std::fmin(eval(*n.a[0], i, dc), eval(*n.a[1], i, dc));
        if (f == "max") return std::fmax(eval(*n.a[0], i, dc), eval(*n.a[1], i, dc));
        if (f == "hm") return eval(*n.a[0], i, dc) * 60 + eval(*n.a[1], i, dc);
        if (f == "floor") return std::floor(eval(*n.a[0], i, dc));
        if (f == "ceil") return std::ceil(eval(*n.a[0], i, dc));
        if (f == "lag") {
            int j = i - (int)n.a[0]->v;
            if (j < 0) return NAN;
            const Series& s = m_.s();
            if (s.day_of[j] != s.day_of[i]) return NAN;               // never reach into another trading day
            return eval(*n.a[1], j, dc);
        }
        if (f == "rhi" || f == "rlo" || f == "at" || f == "hi_since" || f == "lo_since") return day_fn(n, i, dc);
        if (is_smc(f)) return smc_fn(n, i, dc);
        if (f == "bk") {
            const Series& s = m_.s();
            const std::vector<float>& src = n.a[0]->slot == 0 ? s.qimb : n.a[0]->slot == 1 ? s.cxl : n.a[0]->slot == 2 ? s.bidq : s.askq;
            const int k = (int)n.a[1]->v, d = s.day_of[i];
            double sum = 0; int c = 0;
            for (int j = i - 1; j >= 0 && j >= i - k && s.day_of[j] == d; --j) if (!std::isnan(src[j])) { sum += src[j]; ++c; }
            return c ? sum / c : NAN;
        }
        if (f == "eqhi" || f == "eqlo") {
            // the highest high (lowest low) of the last n completed tf bars, when another of those bars came within
            // tol × day ATR of it — a pool of equal highs (lows); NaN otherwise
            const Series& s = m_.s();
            const int tf = (int)n.a[0]->v, nn = (int)n.a[1]->v, d = s.day_of[i];
            const double tol = n.a[2]->v;
            const Agg& a = m_.agg(tf);
            const int c = a.comp[i];
            if (c < nn - 1 || d < 0 || !(s.days[d].atr > 0)) return NAN;
            const bool hi = f == "eqhi";
            int best = -1;
            for (int k = c - nn + 1; k <= c; ++k) if (best < 0 || (hi ? a.h[k] > a.h[best] : a.l[k] < a.l[best])) best = k;
            const double lv = hi ? a.h[best] : a.l[best];
            for (int k = c - nn + 1; k <= c; ++k)
                if (k != best && std::fabs((hi ? a.h[k] : a.l[k]) - lv) <= tol * s.days[d].atr) return lv;
            return NAN;
        }
        if (f == "whi" || f == "wlo") {   // high / low of today's bars in [h1:m1, h2:m2), known once the window is over
            const Series& s = m_.s();
            const int d = s.day_of[i];
            if (d < 0) return NAN;
            const Day& D = s.days[d];
            const int a = hm_to_tday((int)n.a[0]->v, (int)n.a[1]->v), b = hm_to_tday((int)n.a[2]->v, (int)n.a[3]->v);
            if (tday_min(s.t[i]) < b) return NAN;
            double m = f == "whi" ? -1e18 : 1e18; int cnt = 0;
            for (int j = first_bar_at(s, D, a, D.b0); j < D.b1 && j < i && tday_min(s.t[j]) < b; ++j, ++cnt)
                m = f == "whi" ? std::max(m, (double)s.h[j]) : std::min(m, (double)s.l[j]);
            return cnt ? m : NAN;
        }
        if (f == "dwin" || f == "vwin") {
            const Series& s = m_.s();
            const int d = s.day_of[i];
            if (d < 0) return NAN;
            const Day& D = s.days[d];
            const int a = hm_to_tday((int)n.a[0]->v, (int)n.a[1]->v), b = hm_to_tday((int)n.a[2]->v, (int)n.a[3]->v);
            if (tday_min(s.t[i]) < b) return NAN;                              // the window is not over yet
            if (f == "dwin" && !D.flow) return NAN;
            double sum = 0;
            for (int j = first_bar_at(s, D, a, D.b0); j < D.b1 && j < i && tday_min(s.t[j]) < b; ++j)
                sum += f == "dwin" ? 2.0 * s.bv[j] - s.v[j] : s.v[j];
            return sum;
        }
        const int tf = (int)n.a[0]->v, k = (int)n.a[1]->v;
        const Agg& a = m_.agg(tf);
        int c = a.comp[i];
        if (f == "cl" || f == "op" || f == "hi" || f == "lo") {
            int j = c - (k - 1);
            if (c < 0 || j < 0) return NAN;
            return f == "cl" ? a.c[j] : f == "op" ? a.o[j] : f == "hi" ? a.h[j] : a.l[j];
        }
        if (c < 0) return NAN;
        return m_.series(f, tf, k)[c];
    }

    // Day-anchored helpers. rhi(m)/rlo(m): RTH opening range of the first m minutes (known once it is over);
    // at(h, m): open of the first bar at/after h:m ET today (known once that bar has opened);
    // hi_since(h, m)/lo_since(h, m): high/low of bars from h:m up to (not including) bar i.
    double day_fn(const Node& n, int i, DayCache& dc) {
        const Series& s = m_.s();
        const int d = s.day_of[i];
        if (d < 0) return NAN;
        if (dc.day != d) fill(d, dc);
        const Day& D = s.days[d];
        const std::string& f = n.name;
        const int from = (f == "rhi" || f == "rlo") ? kRthOpen : hm_to_tday((int)n.a[0]->v, (int)n.a[1]->v);
        const int j0 = first_bar_at(s, D, from, D.b0);
        if (j0 >= D.b1) return NAN;
        if (f == "at") return j0 <= i ? s.o[j0] : NAN;
        if (f == "rhi" || f == "rlo") {
            const int end = kRthOpen + (int)n.a[0]->v;
            if (tday_min(s.t[i]) < end) return NAN;
            double m = f == "rhi" ? -1e18 : 1e18;
            for (int k = j0; k < D.b1 && tday_min(s.t[k]) < end; ++k) m = f == "rhi" ? std::max(m, (double)s.h[k]) : std::min(m, (double)s.l[k]);
            return m;
        }
        const bool hi = f == "hi_since";
        const long key = (long)from * 2 + (hi ? 1 : 0);
        auto it = dc.extra.find(key);
        if (it == dc.extra.end()) {
            std::vector<double> v(D.b1 - D.b0 + 1, NAN);
            double m = hi ? -1e18 : 1e18;
            for (int k = j0; k < D.b1; ++k) {
                m = hi ? std::max(m, (double)s.h[k]) : std::min(m, (double)s.l[k]);
                v[k + 1 - D.b0] = m;                               // value AT bar k+1 = over bars [j0, k]
            }
            it = dc.extra.emplace(key, std::move(v)).first;
        }
        return it->second[i - D.b0];
    }

    // ───────── market structure on completed tf bars, per trading day since 18:00 (LuxAlgo SMC, 2026-10-18,
    // research/preregistered/2026-10-18_luxalgo_smc.md) ─────────
    //   swh(tf, n) / swl(tf, n)   level of the most recent CONFIRMED swing high / low: a bar whose high (low) is strictly
    //                             above (below) the n bars on each side, all inside the day; known n bars later
    //   obh / obl (tf, n)         top / bottom of the most recent UNMITIGATED bullish order block: on the first close above
    //                             the last unbroken swing high, the last down-close bar between the swing bar and the
    //                             breaking bar; mitigated = a later close below its low. obhs / obls: the bearish mirror
    //                             (last up-close bar before the first close below the last unbroken swing low; mitigated by
    //                             a close above its high)
    //   fvgt / fvgb (tf)          top / bottom of the most recent UNFILLED bullish fair value gap: three bars with bar-1
    //                             high < bar-3 low (gap = [bar-1 high, bar-3 low]); filled = a later close below the bottom.
    //                             fvgts / fvgbs: the bearish mirror (bar-1 low > bar-3 high; filled by a close above the top)
    // Value at bar i uses the tf bars completed before i only; NaN when there is none. One pass per day per (tf, n),
    // cached in DayCache::smc.
    static bool is_smc(const std::string& f) {
        static const std::vector<std::string> names = {"swh", "swl", "obh", "obl", "obhs", "obls", "fvgt", "fvgb", "fvgts", "fvgbs"};
        return std::find(names.begin(), names.end(), f) != names.end();
    }
    double smc_fn(const Node& n, int i, DayCache& dc) {
        const Series& s = m_.s();
        const int d = s.day_of[i];
        if (d < 0) return NAN;
        if (dc.day != d) fill(d, dc);
        const Day& D = s.days[d];
        const std::string& f = n.name;
        const bool fvg = f.rfind("fvg", 0) == 0;
        const int tf = (int)n.a[0]->v, nn = fvg ? 0 : (int)n.a[1]->v;
        const std::string key = (fvg ? "fvg|" : "ob|") + std::to_string(tf) + "|" + std::to_string(nn);
        auto it = dc.smc.find(key);
        if (it == dc.smc.end()) it = dc.smc.emplace(key, fvg ? fvg_day(D, tf) : ob_day(D, tf, nn)).first;
        static const std::map<std::string, int> col = {{"obh", 0}, {"obl", 1}, {"obhs", 2}, {"obls", 3}, {"swh", 4}, {"swl", 5},
                                                       {"fvgt", 0}, {"fvgb", 1}, {"fvgts", 2}, {"fvgbs", 3}};
        return it->second[col.at(f)][i - D.b0];
    }
    struct Box { double top, bot; };
    // Walks the day's completed tf bars once; `step(b)` is called for every tf bar b completed inside the day, in order, and
    // `emit(k)` after the bars completed before 1m bar k are processed (the value AT bar k).
    template <class Step, class Emit>
    void walk_day(const Day& D, int tf, Step step, Emit emit) const {
        const Agg& a = m_.agg(tf);
        int done = a.comp[D.b0];                 // tf bars completed before the day's first bar belong to the previous day
        const int c0 = done + 1;                 // the day's first tf bar
        for (int k = D.b0; k < D.b1; ++k) {
            for (int b = done + 1; b <= a.comp[k]; ++b) step(b, c0);
            done = std::max(done, a.comp[k]);
            emit(k);
        }
    }
    std::vector<std::vector<double>> ob_day(const Day& D, int tf, int N) const {
        const Agg& a = m_.agg(tf);
        std::vector<std::vector<double>> out(6, std::vector<double>(D.b1 - D.b0, NAN));
        std::vector<Box> bull, bear;                    // unmitigated blocks, oldest first
        double sh = NAN, sl = NAN, swh = NAN, swl = NAN; int shi = -1, sli = -1; bool shb = true, slb = true;
        walk_day(D, tf, [&](int b, int c0) {
            const int p = b - N;                        // the bar a pivot at p needs: N bars on its right, the last being b
            if (p - N >= c0) {
                bool ish = true, isl = true;
                for (int j = p - N; j <= p + N; ++j) { if (j == p) continue; if (a.h[j] >= a.h[p]) ish = false; if (a.l[j] <= a.l[p]) isl = false; }
                if (ish) { sh = swh = a.h[p]; shi = p; shb = false; }
                if (isl) { sl = swl = a.l[p]; sli = p; slb = false; }
            }
            bull.erase(std::remove_if(bull.begin(), bull.end(), [&](const Box& x) { return a.c[b] < x.bot; }), bull.end());
            bear.erase(std::remove_if(bear.begin(), bear.end(), [&](const Box& x) { return a.c[b] > x.top; }), bear.end());
            if (!shb && a.c[b] > sh) { shb = true; for (int q = b - 1; q >= shi; --q) if (a.c[q] < a.o[q]) { bull.push_back({a.h[q], a.l[q]}); break; } }
            if (!slb && a.c[b] < sl) { slb = true; for (int q = b - 1; q >= sli; --q) if (a.c[q] > a.o[q]) { bear.push_back({a.h[q], a.l[q]}); break; } }
        }, [&](int k) {
            const int r = k - D.b0;
            if (!bull.empty()) { out[0][r] = bull.back().top; out[1][r] = bull.back().bot; }
            if (!bear.empty()) { out[2][r] = bear.back().top; out[3][r] = bear.back().bot; }
            out[4][r] = swh; out[5][r] = swl;
        });
        return out;
    }
    std::vector<std::vector<double>> fvg_day(const Day& D, int tf) const {
        const Agg& a = m_.agg(tf);
        std::vector<std::vector<double>> out(4, std::vector<double>(D.b1 - D.b0, NAN));
        std::vector<Box> bull, bear;                    // unfilled gaps, oldest first
        walk_day(D, tf, [&](int b, int c0) {
            bull.erase(std::remove_if(bull.begin(), bull.end(), [&](const Box& x) { return a.c[b] < x.bot; }), bull.end());
            bear.erase(std::remove_if(bear.begin(), bear.end(), [&](const Box& x) { return a.c[b] > x.top; }), bear.end());
            if (b - 2 < c0) return;                      // all three bars inside the day
            if (a.h[b - 2] < a.l[b]) bull.push_back({a.l[b], a.h[b - 2]});
            if (a.l[b - 2] > a.h[b]) bear.push_back({a.l[b - 2], a.h[b]});
        }, [&](int k) {
            const int r = k - D.b0;
            if (!bull.empty()) { out[0][r] = bull.back().top; out[1][r] = bull.back().bot; }
            if (!bear.empty()) { out[2][r] = bear.back().top; out[3][r] = bear.back().bot; }
        });
        return out;
    }

    void fill(int d, DayCache& dc) {
        const Series& s = m_.s();
        const Day& D = s.days[d];
        const int n = D.b1 - D.b0 + 1;
        dc.day = d; dc.extra.clear(); dc.smc.clear();
        for (auto* v : {&dc.rhi, &dc.rlo, &dc.dhi, &dc.dlo, &dc.vw, &dc.vsd, &dc.cd, &dc.cv}) v->assign(n, NAN);
        double hi = -1e18, lo = 1e18, rhi = -1e18, rlo = 1e18, pv = 0, pv2 = 0, vol = 0, cd = 0, cv = 0;
        int rth = D.rth0;
        for (int k = 0; k < n; ++k) {
            const int i = D.b0 + k;   // values at bar i are over bars before i
            if (k > 0) {
                const int j = i - 1;
                hi = std::max(hi, (double)s.h[j]); lo = std::min(lo, (double)s.l[j]);
                if (rth >= 0 && j == rth) { pv = pv2 = vol = 0; cd = cv = 0; }   // VWAP re-anchors at the RTH open
                const double tp = (s.h[j] + s.l[j] + s.c[j]) / 3.0;
                pv += tp * s.v[j]; pv2 += tp * tp * s.v[j]; vol += s.v[j];
                if (rth >= 0 && j >= rth) {
                    rhi = std::max(rhi, (double)s.h[j]); rlo = std::min(rlo, (double)s.l[j]);
                    cv += s.v[j]; cd += D.flow ? 2.0 * s.bv[j] - s.v[j] : NAN;
                }
            }
            dc.dhi[k] = k > 0 ? hi : NAN; dc.dlo[k] = k > 0 ? lo : NAN;
            dc.rhi[k] = rhi > -1e17 ? rhi : NAN; dc.rlo[k] = rlo < 1e17 ? rlo : NAN;
            if (vol > 0) { double m = pv / vol; dc.vw[k] = m; dc.vsd[k] = std::sqrt(std::max(0.0, pv2 / vol - m * m)); }
            dc.cd[k] = rth >= 0 && i > rth ? cd : NAN; dc.cv[k] = rth >= 0 && i > rth ? cv : NAN;
        }
    }

    double var(int slot, int i, DayCache& dc) {
        const Series& s = m_.s();
        const int d = s.day_of[i];
        if (d < 0) return NAN;
        const Day& D = s.days[d];
        auto run = [&](std::vector<double> DayCache::*f) { if (dc.day != d) fill(d, dc); return (dc.*f)[i - D.b0]; };
        switch (slot) {
            case V_px: return s.o[i];
            case V_t: return (double)((s.t[i] % 86400) / 60);
            case V_open: return D.rth0 >= 0 && i >= D.rth0 ? D.ro : NAN;
            case V_hod: return run(&DayCache::rhi);
            case V_lod: return run(&DayCache::rlo);
            case V_dhi: return run(&DayCache::dhi);
            case V_dlo: return run(&DayCache::dlo);
            case V_onh: return D.rth0 >= 0 ? (i >= D.rth0 ? (dc.day != d ? (fill(d, dc), dc.dhi[D.rth0 - D.b0]) : dc.dhi[D.rth0 - D.b0]) : run(&DayCache::dhi)) : NAN;
            case V_onl: return D.rth0 >= 0 ? (i >= D.rth0 ? (dc.day != d ? (fill(d, dc), dc.dlo[D.rth0 - D.b0]) : dc.dlo[D.rth0 - D.b0]) : run(&DayCache::dlo)) : NAN;
            case V_vwap: return run(&DayCache::vw);
            case V_vsd: return run(&DayCache::vsd);
            case V_cdelta: return run(&DayCache::cd);
            case V_cvol: return run(&DayCache::cv);
            case V_atr: return D.atr;
            case V_pc: return D.pc;
            case V_ph: return D.ph;
            case V_pl: return D.pl;
            case V_pc2: return D.pc2;
            case V_pibs: return D.pibs;
            case V_sma50: return D.sma50;
            case V_rsi2: return D.rsi2;
            case V_gap: return D.atr > 0 ? (D.ro - D.pc) / D.atr : NAN;
            case V_prng: return D.ph - D.pl;
            case V_pret: return D.atr > 0 ? (D.pc - D.pc2) / D.atr : NAN;
            case V_vix: return D.vix;
            case V_vixc1: return D.vixc1;
            case V_vixc5: return D.vixc5;
            case V_vixpct: return D.vixpct;
            case V_dow: return D.dow;
            case V_tdom: return D.tdom;
            case V_tdleft: return D.tdleft;
            case V_fomc: return D.fomc;
            case V_opex: return D.opex;
            case V_nfp: return D.nfp;
            case V_rollwin: return D.rollwin;
            case V_year: return D.date / 10000;
            case V_month: return D.date / 100 % 100;
            case V_flow: return D.flow;
            case V_mtd: return D.mtd;
            case V_prehol: return D.prehol;
            case V_ppoc: return D.ppoc;
            case V_pvah: return D.pvah;
            case V_pval: return D.pval;
            case V_pwh: return D.pwh;
            case V_pwl: return D.pwl;
            case V_pmh: return D.pmh;
            case V_pml: return D.pml;
            case V_npoca: return D.npoca;
            case V_npocb: return D.npocb;
            case V_sma100: return D.sma100;
            case V_sma200: return D.sma200;
            case V_xma: return D.xma;
            case V_vts: return D.vts;
            case V_vvix: return D.vvix;
            case V_skew: return D.skew;
            case V_y10: return D.y10;
            case V_y2: return D.y2;
            case V_y10c: return D.y10c;
            case V_usdc: return D.usdc;
            case V_earn: return D.earn;
            case V_cpi: return D.cpi;
            case V_ppi: return D.ppi;
            case V_rvol: return m_.rvol()[i];
            case V_xs: case V_xr: return pair_var(slot, i, D);
        }
        return NAN;
    }

    // xs: the pair's (price at this minute − its own 09:30 open) / its own day ATR — the same "move from the open" in the
    //     other market: the open of the pair's latest bar that has started by now (≤ 2 minutes old), never a later bar.
    // xr: the pair's return from the time of THIS market's prior RTH close to now (last pair close before the bar).
    double pair_var(int slot, int i, const Day& D) const {
        const Series* p = m_.pair;
        if (!p || p->t.empty()) return NAN;
        const Series& s = m_.s();
        if (slot == V_xs) {
            // the pair's bar that has OPENED by now (start ≤ this bar's start, at most 2 minutes old) — never a later one
            auto it = std::upper_bound(p->t.begin(), p->t.end(), s.t[i]);
            if (it == p->t.begin()) return NAN;
            --it;
            if (s.t[i] - *it > 120) return NAN;
            const int j = (int)(it - p->t.begin()), pd = p->day_of[j];
            if (pd < 0) return NAN;
            const Day& E = p->days[pd];
            if (E.rth0 < 0 || j < E.rth0 || !(E.atr > 0)) return NAN;
            return (p->o[j] - E.ro) / E.atr;
        }
        if (D.pct <= 0) return NAN;
        const int k = (int)(std::lower_bound(p->t.begin(), p->t.end(), s.t[i]) - p->t.begin()) - 1;
        const int q = (int)(std::lower_bound(p->t.begin(), p->t.end(), D.pct) - p->t.begin()) - 1;
        if (k < 0 || q < 0 || s.t[i] - p->t[k] > 1800 || D.pct - p->t[q] > 1800 || !(p->c[q] > 0)) return NAN;
        return p->c[k] / p->c[q] - 1.0;
    }

    Market& m_;
};

// ───────────────────────────── spec → variants ─────────────────────────────
struct Check { int at = 0; std::string when, dir; };
struct Spec {
    std::string id, name, why, symbol = "NQ", confirm = "ES", pair;   // pair: second market for xs / xr
    std::vector<std::pair<std::string, std::vector<double>>> params;
    std::vector<Check> checks;
    bool scan = false; int scan_tf = 1, scan_from = 0, scan_to = 0; std::string scan_when, scan_dir = "1";
    int max_trades = 1, flat = parse_hhmm("15:55"), max_bars = 0;
    std::string stop = "0", target = "0", be_r = "0", partial_r = "0", add_r = "0", trail_r = "0";   // managed exits, in R
    int from_ymd = 20100701, to_ymd = 99991231, train_months = 12, min_trades = 100, control_seeds = 200;   // to: last day used (holdout after it)
    double control_pct = 0.95;
    // Controls that gate. Empty (default) = random direction AND (random time OR random day): the rule's direction must
    // be real and at least one of its timing / its day filter must add something. A spec may name stricter claims.
    std::vector<std::string> claims;
    std::map<std::string, double> cost{{"NQ", 1.0}, {"ES", 0.35}};          // real round trip, points per contract
    std::string prop_plan = "select_daily_50k"; std::vector<int> prop_sizes{2, 4, 6}; double prop_fee = 150;
    // The prop chain trades the symbol's micro: MNQ $2/pt at 1.5 pts real round trip, MES $5/pt at 0.6 pts.
    std::string micro = "MNQ"; double micro_pv = 2, micro_cost = 1.5;
    // Monthly view (diversification): ideas whose month-by-month net is compared with these earlier ideas' (their
    // reports/<id>.monthly.csv). Information for building a book of accounts, not a gate.
    std::vector<std::string> corr_with{"so_cascade", "weak_day_long"};
    json raw;
};

inline Spec parse_spec(const json& j) {
    Spec s; s.raw = j;
    s.id = j.at("id").get<std::string>();
    s.name = j.value("name", s.id); s.why = j.value("why", "");
    if (s.why.empty()) throw std::runtime_error(s.id + ": every idea needs a \"why\" — who is forced to trade against us");
    s.symbol = j.value("symbol", "NQ"); s.confirm = j.value("confirm", "ES"); s.pair = j.value("pair", "");
    if (j.contains("params")) for (auto& [k, v] : j["params"].items()) {
        std::vector<double> vals;
        if (v.is_array()) for (auto& x : v) vals.push_back(x.get<double>()); else vals.push_back(v.get<double>());
        if (vals.empty()) throw std::runtime_error(s.id + ": param " + k + " has no values");
        s.params.emplace_back(k, vals);
    }
    auto str = [](const json& x) { return x.is_string() ? x.get<std::string>() : x.dump(); };
    if (j.contains("checks")) for (auto& c : j["checks"]) s.checks.push_back({parse_hhmm(c.at("at")), str(c.at("when")), c.contains("dir") ? str(c["dir"]) : "1"});
    if (j.contains("scan")) {
        const auto& c = j["scan"]; s.scan = true;
        s.scan_tf = c.value("tf", 1); s.scan_from = parse_hhmm(c.at("from")); s.scan_to = parse_hhmm(c.at("to"));
        s.scan_when = str(c.at("when")); s.scan_dir = c.contains("dir") ? str(c["dir"]) : "1";
        if (s.scan_tf < 1 || 1440 % s.scan_tf) throw std::runtime_error(s.id + ": scan.tf must divide 1440");
    }
    if (s.checks.empty() == !s.scan) throw std::runtime_error(s.id + ": give exactly one of \"checks\" or \"scan\"");
    s.max_trades = j.value("max_trades", 1); s.max_bars = j.value("max_bars", 0);
    if (j.contains("flat")) s.flat = parse_hhmm(j["flat"]);
    if (j.contains("stop")) s.stop = str(j["stop"]);
    if (j.contains("target")) s.target = str(j["target"]);
    if (j.contains("be_r")) s.be_r = str(j["be_r"]);
    if (j.contains("partial_r")) s.partial_r = str(j["partial_r"]);
    if (j.contains("add_r")) s.add_r = str(j["add_r"]);
    if (j.contains("trail_r")) s.trail_r = str(j["trail_r"]);
    if (j.contains("from")) { int y, m, d; if (std::sscanf(j["from"].get<std::string>().c_str(), "%d-%d-%d", &y, &m, &d) == 3) s.from_ymd = y * 10000 + m * 100 + d; }
    if (j.contains("to")) { int y, m, d; if (std::sscanf(j["to"].get<std::string>().c_str(), "%d-%d-%d", &y, &m, &d) == 3) s.to_ymd = y * 10000 + m * 100 + d; }
    s.train_months = j.value("train_months", 12); s.min_trades = j.value("min_trades", 100);
    s.control_seeds = j.value("control_seeds", 200); s.control_pct = j.value("control_pct", 0.95);
    if (j.contains("claims")) {
        s.claims = j["claims"].get<std::vector<std::string>>();
        for (auto& c : s.claims) if (c != "dir" && c != "time" && c != "day") throw std::runtime_error(s.id + ": claims are dir / time / day");
        if (std::find(s.claims.begin(), s.claims.end(), "dir") == s.claims.end()) throw std::runtime_error(s.id + ": every idea claims dir");
    }
    if (j.contains("cost")) for (auto& [k, v] : j["cost"].items()) s.cost[k] = v.get<double>();
    if (s.symbol == "ES") { s.micro = "MES"; s.micro_pv = 5; s.micro_cost = 0.6; }
    else if (s.symbol != "NQ") s.micro.clear();   // no micro on our prop firm: the prop gate is reported n/a
    if (j.contains("prop")) {
        const auto& p = j["prop"];
        s.prop_plan = p.value("plan", s.prop_plan); s.prop_fee = p.value("fee", s.prop_fee);
        if (p.contains("sizes")) s.prop_sizes = p["sizes"].get<std::vector<int>>();
        s.micro_cost = p.value("micro_cost", s.micro_cost);
    }
    if (j.contains("corr_with")) s.corr_with = j["corr_with"].get<std::vector<std::string>>();
    return s;
}

struct Variant {
    std::map<std::string, double> p;
    std::string label;
    std::vector<std::pair<NodeP, NodeP>> checks;   // when, dir
    NodeP scan_when, scan_dir, stop, target, be, part, add, trail;   // be / part / add / trail may be null (= 0)
};

inline std::vector<std::map<std::string, double>> grid(const Spec& s) {
    std::vector<std::map<std::string, double>> out(1);
    for (auto& [k, vals] : s.params) {
        std::vector<std::map<std::string, double>> nx;
        for (auto& base : out) for (double v : vals) { auto m = base; m[k] = v; nx.push_back(m); }
        out.swap(nx);
    }
    return out;
}

inline std::string label_of(const std::map<std::string, double>& p) {
    std::ostringstream o; bool first = true;
    for (auto& [k, v] : p) { if (!first) o << ' '; first = false; o << k << '=' << v; }
    return first ? "(no params)" : o.str();
}

// ───────────────────────────── simulation ─────────────────────────────
struct Trade {
    int day = 0, e = 0, x = 0, dir = 0, w0 = 0, w1 = 0, flat = 0;   // entry / exit bar, entry window bars, flat bar
    int var = 0, src = -1;                                         // variant index; check index (−1 = scan)
    float stop = 0, tgt = 0;                                       // points (0 = none)
    float be = 0, part = 0, add_r = 0, trail_r = 0;               // break-even / partial / add-a-unit / trail, in R (0 = off)
    float add_cost = 0;                                            // the added unit's round-trip cost (points)
    double pts = 0, mae = 0, mfe = 0;                              // gross points per contract
};

struct SimOut { int x; double pts, mae, mfe; };


// Path from entry bar e (fill at open[e]) to the stop / target / flat bar / max_bars.
// Managed exits (all in R = the initial stop distance; all need a stop):
//   be    > 0: once price has gone be × R in favour, the stop moves to the entry (from the next bar)
//   part  > 0: half the position exits at part × R in favour, the other half's stop moves to the entry (next bar);
//              the result is the average of the two halves
//   add_r > 0: one more unit is bought (sold) at add_r × R in favour; both units' stops move to the entry (next bar)
//              and both exit together; the added unit pays add_cost; the result is the SUM of the units
//   trail_r > 0: once +1R is reached, the stop trails the best price by trail_r × R (moves only in favour, next bar)
// With all four 0 the plain path below is used.
inline SimOut simulate_managed(const Series& s, int e, int dir, double stop, double tgt, int flat, int b1, int max_bars,
                               double be, double part, double add_r = 0, double trail_r = 0, double add_cost = 0) {
    const double P = s.o[e];
    double sp = P - dir * stop, pend = NAN, half_pts = 0, P2 = NAN, best = P;
    const double tp = tgt > 0 ? P + dir * tgt : NAN, pl = part > 0 ? P + dir * part * stop : NAN,
                 al = add_r > 0 ? P + dir * add_r * stop : NAN;
    bool half = false, be_on = false, added = false;
    double mae = 0, mfe = 0;
    auto protect = [&](double lvl) {                    // a new stop applies from the next bar, the most protective wins
        if (dir * (lvl - sp) <= 0) return;
        if (std::isnan(pend) || dir * (lvl - pend) > 0) pend = lvl;
    };
    auto out = [&](int k, double px) {
        const double x = dir * (px - P);
        double r = half ? 0.5 * half_pts + 0.5 * x : x;
        if (added) r += dir * (px - P2) - add_cost;
        return SimOut{k, r, std::min(mae, r), std::max(mfe, r)};
    };
    const int last = max_bars > 0 ? std::min(flat, e + max_bars) : flat;
    for (int k = e; k < last; ++k) {
        const double o = s.o[k], h = s.h[k], l = s.l[k];
        if (!std::isnan(pend)) { sp = pend; pend = NAN; }
        const double adv = dir > 0 ? l - P : P - h, fav = dir > 0 ? h - P : P - l;
        if (k > e && (dir > 0 ? o <= sp : o >= sp)) return out(k, o);
        if (dir > 0 ? l <= sp : h >= sp) { mae = std::min(mae, adv); return out(k, sp); }
        if (part > 0 && !half) {
            if (k > e && (dir > 0 ? o >= pl : o <= pl)) { half = true; half_pts = dir * (o - P); protect(P); }
            else if (dir > 0 ? h >= pl : l <= pl) { half = true; half_pts = part * stop; protect(P); }
        }
        if (add_r > 0 && !added) {
            if (k > e && (dir > 0 ? o >= al : o <= al)) { added = true; P2 = o; protect(P); }
            else if (dir > 0 ? h >= al : l <= al) { added = true; P2 = al; protect(P); }
        }
        if (tgt > 0) {
            if (k > e && (dir > 0 ? o >= tp : o <= tp)) { mfe = std::max(mfe, fav); return out(k, o); }
            if (dir > 0 ? h >= tp : l <= tp) { mae = std::min(mae, adv); mfe = std::max(mfe, fav); return out(k, tp); }
        }
        if (be > 0 && !be_on && fav >= be * stop) { be_on = true; protect(P); }
        best = dir > 0 ? std::max(best, (double)h) : std::min(best, (double)l);
        if (trail_r > 0 && dir * (best - P) >= stop) protect(best - dir * trail_r * stop);
        mae = std::min(mae, adv); mfe = std::max(mfe, fav);
    }
    if (last < b1) return out(last, s.o[last]);
    return out(b1 - 1, s.c[b1 - 1]);
}

inline SimOut simulate(const Series& s, int e, int dir, double stop, double tgt, int flat, int b1, int max_bars,
                       double be = 0, double part = 0, double add_r = 0, double trail_r = 0, double add_cost = 0) {
    if (stop > 0 && (be > 0 || part > 0 || add_r > 0 || trail_r > 0))
        return simulate_managed(s, e, dir, stop, tgt, flat, b1, max_bars, be, part, add_r, trail_r, add_cost);
    const double P = s.o[e];
    const double sp = stop > 0 ? P - dir * stop : NAN, tp = tgt > 0 ? P + dir * tgt : NAN;
    double mae = 0, mfe = 0;
    const int last = max_bars > 0 ? std::min(flat, e + max_bars) : flat;
    for (int k = e; k < last; ++k) {
        const double o = s.o[k], h = s.h[k], l = s.l[k];
        const double adv = dir > 0 ? l - P : P - h, fav = dir > 0 ? h - P : P - l;
        if (stop > 0) {
            if (k > e && (dir > 0 ? o <= sp : o >= sp)) return {k, dir * (o - P), std::min(mae, dir * (o - P)), mfe};
            if (dir > 0 ? l <= sp : h >= sp) return {k, -stop, -stop, std::max(mfe, fav)};
        }
        if (tgt > 0) {
            if (k > e && (dir > 0 ? o >= tp : o <= tp)) return {k, dir * (o - P), mae, std::max(mfe, dir * (o - P))};
            if (dir > 0 ? h >= tp : l <= tp) return {k, tgt, std::min(mae, adv), tgt};
        }
        mae = std::min(mae, adv); mfe = std::max(mfe, fav);
    }
    if (last < b1) return {last, dir * (s.o[last] - P), mae, mfe};
    return {b1 - 1, dir * (s.c[b1 - 1] - P), mae, mfe};
}

// All trades of one variant. delay = bars between the decision and the fill.
inline std::vector<Trade> run_variant(Evaluator& ev, const Series& s, const Spec& sp, const Variant& v, int delay) {
    std::vector<Trade> out;
    DayCache dc;
    for (int d = 0; d < (int)s.days.size(); ++d) {
        const Day& D = s.days[d];
        if (!D.ok || std::isnan(D.atr) || D.date < sp.from_ymd || D.date > sp.to_ymd) continue;
        const int flat = first_bar_at(s, D, sp.flat, D.b0);
        int free_from = D.b0, taken = 0;
        auto try_at = [&](int i, const Node& when, const Node& dirn, int w0, int w1, int src) -> bool {
            if (i >= D.b1 || i < free_from) return false;
            double ok = ev.eval(when, i, dc);
            if (!(ok != 0 && !std::isnan(ok))) return false;
            double dr = ev.eval(dirn, i, dc);
            int dir = dr > 0 ? 1 : dr < 0 ? -1 : 0;
            if (dir == 0) return false;
            int e = i + delay;
            if (e >= flat || e >= D.b1) return false;
            double st = ev.eval(*v.stop, i, dc), tg = ev.eval(*v.target, i, dc);
            if (std::isnan(st) || st < 0) return false;
            if (std::isnan(tg) || tg < 0) tg = 0;
            const double tgp = st > 0 ? tg * st : 0;          // target is in R; with no stop there is no R target
            double be = v.be ? ev.eval(*v.be, i, dc) : 0, pt = v.part ? ev.eval(*v.part, i, dc) : 0;
            double ad = v.add ? ev.eval(*v.add, i, dc) : 0, tr = v.trail ? ev.eval(*v.trail, i, dc) : 0;
            if (!(be > 0)) be = 0;
            if (!(pt > 0)) pt = 0;
            if (!(ad > 0)) ad = 0;
            if (!(tr > 0)) tr = 0;
            const double ac = ad > 0 ? (sp.cost.count(s.sym) ? sp.cost.at(s.sym) : 1.0) : 0.0;
            SimOut r = simulate(s, e, dir, st, tgp, flat, D.b1, sp.max_bars, be, pt, ad, tr, ac);
            Trade t; t.day = d; t.e = e; t.x = r.x; t.dir = dir; t.w0 = w0; t.w1 = w1; t.flat = flat; t.src = src;
            t.stop = (float)st; t.tgt = (float)tgp; t.be = (float)be; t.part = (float)pt; t.add_r = (float)ad; t.trail_r = (float)tr;
            t.add_cost = (float)ac; t.pts = r.pts; t.mae = r.mae; t.mfe = r.mfe;
            out.push_back(t); ++taken; free_from = r.x + 1;
            return true;
        };
        if (!sp.scan) {
            // random-time control window: the checks' span, at least 2 h from the first check (a lone 09:50 check
            // must still be compared with other times of the same session)
            const int w0 = first_bar_at(s, D, sp.checks.front().at, D.b0),
                      w1 = first_bar_at(s, D, std::max(sp.checks.back().at, sp.checks.front().at + 120), D.b0);
            for (size_t c = 0; c < sp.checks.size() && taken < sp.max_trades; ++c) {
                int i = first_bar_at(s, D, sp.checks[c].at, D.b0);
                if (i >= D.b1 || tday_min(s.t[i]) > sp.checks[c].at + 5) continue;   // no bar near the check time
                try_at(i, *v.checks[c].first, *v.checks[c].second, w0, w1, (int)c);
            }
        } else {
            const int w0 = first_bar_at(s, D, sp.scan_from, D.b0), w1 = first_bar_at(s, D, sp.scan_to, D.b0);
            for (int i = w0; i <= w1 && i < D.b1 && taken < sp.max_trades; ++i) {
                int tm = tday_min(s.t[i]);
                if (tm > sp.scan_to) break;
                if ((s.t[i] / 60) % sp.scan_tf != 0) continue;
                try_at(i, *v.scan_when, *v.scan_dir, w0, w1, -1);
            }
        }
    }
    return out;
}

// ───────────────────────────── statistics ─────────────────────────────
struct Stat {
    int n = 0; double net = 0, avg = 0, pf = 0, t = 0, win = 0, gross_avg = 0;
};
inline Stat stat_of(const std::vector<double>& x, const std::vector<double>& gross = {}) {
    Stat s; s.n = (int)x.size();
    if (!s.n) return s;
    double w = 0, l = 0, ss = 0; int wins = 0;
    for (double v : x) { s.net += v; if (v > 0) { w += v; ++wins; } else l -= v; }
    s.avg = s.net / s.n;
    for (double v : x) ss += (v - s.avg) * (v - s.avg);
    double sd = s.n > 1 ? std::sqrt(ss / (s.n - 1)) : 0;
    s.t = sd > 0 ? s.avg / (sd / std::sqrt((double)s.n)) : 0;
    s.pf = l > 0 ? w / l : (w > 0 ? 99 : 0);
    s.win = (double)wins / s.n;
    if (!gross.empty()) { double g = 0; for (double v : gross) g += v; s.gross_avg = g / gross.size(); }
    return s;
}
inline int month_idx(int ymd) { return (ymd / 10000 - 2000) * 12 + (ymd / 100 % 100 - 1); }
inline int era_of(int ymd) { int y = ymd / 10000; return y <= 2018 ? 0 : y <= 2022 ? 1 : 2; }
inline const char* era_name(int e) { static const char* n[] = {"2010-18", "2019-22", "2023-26"}; return n[e]; }

struct WfOut {
    wf::Summary sum;
    std::vector<Trade> trades;       // the out-of-sample trades actually taken
    std::vector<int> pick_variant;   // per OOS trade
    int first_month = 0, months = 0;
};

// Walk-forward over variant trade lists at a given cost.
inline WfOut walk(const Series& s, const std::vector<std::vector<Trade>>& vt, double cost, int train, int min_train) {
    WfOut o;
    int lo = INT32_MAX, hi = INT32_MIN;
    for (auto& v : vt) for (auto& t : v) { int m = month_idx(s.days[t.day].date); lo = std::min(lo, m); hi = std::max(hi, m); }
    if (lo > hi) return o;
    o.first_month = lo; o.months = hi - lo + 1;
    wf::Group g; g.key = "x";
    for (size_t v = 0; v < vt.size(); ++v) {
        g.variants.push_back(std::to_string(v));
        g.cells.emplace_back(o.months);
        for (auto& t : vt[v]) { auto& c = g.cells[v][month_idx(s.days[t.day].date) - lo]; ++c.n; c.net += t.pts - cost; }
    }
    o.sum = wf::walk_forward(g, o.months, train, min_train);
    for (auto& p : o.sum.picks)
        for (auto& t : vt[p.variant])
            if (month_idx(s.days[t.day].date) - lo == p.month) { o.trades.push_back(t); o.pick_variant.push_back(p.variant); }
    return o;
}

// Calendar months from the first to the last trade month (YYYYMM), months without trades = 0; months inside a data
// hole (no ok day at all) are left out, so a gap is never a "flat month".
inline std::map<int, double> monthly_net(const Series& s, const std::vector<Trade>& tr, double cost) {
    std::map<int, double> m;
    if (tr.empty()) return m;
    std::set<int> have;
    for (auto& d : s.days) if (d.ok) have.insert(d.date / 100);
    const int a = s.days[tr.front().day].date / 100, b = s.days[tr.back().day].date / 100;
    for (int y = a; y <= b; y = (y % 100 == 12) ? (y / 100 + 1) * 100 + 1 : y + 1) if (have.count(y)) m[y] = 0;
    for (auto& t : tr) m[s.days[t.day].date / 100] += t.pts - cost;
    return m;
}
inline double corr_monthly(const std::map<int, double>& a, const std::map<int, double>& b) {
    std::vector<double> x, y;
    for (auto& [k, v] : a) { auto it = b.find(k); if (it != b.end()) { x.push_back(v); y.push_back(it->second); } }
    if (x.size() < 12) return NAN;
    double mx = 0, my = 0; for (size_t i = 0; i < x.size(); ++i) { mx += x[i]; my += y[i]; } mx /= x.size(); my /= y.size();
    double sxy = 0, sxx = 0, syy = 0;
    for (size_t i = 0; i < x.size(); ++i) { sxy += (x[i] - mx) * (y[i] - my); sxx += (x[i] - mx) * (x[i] - mx); syy += (y[i] - my) * (y[i] - my); }
    return sxx > 0 && syy > 0 ? sxy / std::sqrt(sxx * syy) : NAN;
}

inline double net_of(const std::vector<Trade>& v, double cost) { double n = 0; for (auto& t : v) n += t.pts - cost; return n; }

// Controls — the out-of-sample trades against three kinds of luck (N seeds each; the real net must beat a share of them):
//   dir   same entries and exits, random direction                         → is the direction rule worth anything?
//   time  same day, a random bar in the entry window, the rule's OWN direction evaluated at that bar (never the
//         direction the real trade learned later)                           → does the trigger's timing matter?
//   day   a random ok day of the same month, same time of day, the rule's own direction there, stop scaled by ATR
//                                                                           → does the day filter (the "when") matter?
// `dir_at(trade, i)` evaluates the trade's variant's direction rule at bar i (0 = no trade there).
struct Controls { double dir_pct = 0, time_pct = 0, day_pct = 0, dir_mean = 0, time_mean = 0, day_mean = 0; int seeds = 0; };
inline Controls controls(const Series& s, const std::vector<Trade>& tr, double cost, int seeds, int max_bars,
                         const std::function<int(const Trade&, int)>& dir_at) {
    Controls c; c.seeds = seeds;
    if (tr.empty() || seeds <= 0) return c;
    const double real = net_of(tr, cost);
    std::map<int, std::vector<int>> by_month;   // same calendar month: the walk-forward's month choice stays out of it
    for (int d = 0; d < (int)s.days.size(); ++d)
        if (s.days[d].ok && !std::isnan(s.days[d].atr)) by_month[s.days[d].date / 100].push_back(d);
    int beat_d = 0, beat_t = 0, beat_y = 0;
    for (int k = 0; k < seeds; ++k) {
        std::mt19937_64 rng(1000003ULL * (k + 1));
        double nd = 0, nt = 0, ny = 0;
        for (const Trade& t : tr) {
            const Day& D = s.days[t.day];
            int dir = (rng() & 1) ? 1 : -1;
            SimOut a = simulate(s, t.e, dir, t.stop, t.tgt, t.flat, D.b1, max_bars, t.be, t.part, t.add_r, t.trail_r, t.add_cost);
            nd += a.pts - cost;

            int hi = std::min(t.w1, t.flat - 1);
            int e = hi > t.w0 ? t.w0 + (int)(rng() % (uint64_t)(hi - t.w0 + 1)) : t.e;
            int dt = dir_at(t, e);
            if (dt != 0 && e < t.flat) nt += simulate(s, e, dt, t.stop, t.tgt, t.flat, D.b1, max_bars, t.be, t.part, t.add_r, t.trail_r, t.add_cost).pts - cost;

            const auto& pool = by_month[D.date / 100];
            int od = pool[rng() % pool.size()];
            const Day& O = s.days[od];
            int oe = first_bar_at(s, O, tday_min(s.t[t.e]), O.b0);
            int of = t.flat < D.b1 ? first_bar_at(s, O, tday_min(s.t[t.flat]), O.b0) : O.b1;
            if (oe < of && oe < O.b1) {
                int dy = dir_at(t, oe);
                double r = D.atr > 0 ? O.atr / D.atr : 1;
                if (dy != 0) ny += simulate(s, oe, dy, t.stop * r, t.tgt * r, of, O.b1, max_bars, t.be, t.part, t.add_r, t.trail_r, t.add_cost).pts - cost;
            }
        }
        c.dir_mean += nd / seeds; c.time_mean += nt / seeds; c.day_mean += ny / seeds;
        if (real > nd + 1e-6) ++beat_d;   // a tie is not a win
        if (real > nt + 1e-6) ++beat_t;
        if (real > ny + 1e-6) ++beat_y;
    }
    c.dir_pct = (double)beat_d / seeds; c.time_pct = (double)beat_t / seeds; c.day_pct = (double)beat_y / seeds;
    return c;
}

// Prop chain: eval → funded until breach → buy a new eval; trades in the symbol's micro × size ($ per point, real cost).
struct PropOut { int size = 0; int evals = 0, passes = 0, payouts = 0; double paid = 0, years = 0, be_fee = 0; };
inline PropOut prop_chain(const Series& s, const std::vector<Trade>& tr, const std::string& plan_name, int size, double micro_cost, double micro_pv) {
    PropOut o; o.size = size;
    const payout::Plan* plan = nullptr;
    for (auto& p : payout::plans()) if (p.name == plan_name) plan = &p;
    if (!plan || tr.empty()) return o;
    std::map<int, std::vector<payout::Trade>> by_day;
    for (const Trade& t : tr) {
        const double k = micro_pv * size;
        by_day[t.day].push_back({(t.pts - micro_cost) * k, (t.mae - micro_cost) * k, std::max(0.0, t.mfe) * k});
    }
    // calendar = every ok day from the first OOS trade's day to the last
    const int d0 = tr.front().day, d1 = tr.back().day;
    std::vector<const std::vector<payout::Trade>*> cal;
    for (int d = d0; d <= d1; ++d) {
        if (!s.days[d].ok) continue;
        auto it = by_day.find(d);
        cal.push_back(it == by_day.end() ? nullptr : &it->second);
    }
    const int D = (int)cal.size();
    o.years = D / 252.0;
    int i = 0;
    while (i < D) {
        ++o.evals;
        payout::EvalOut e = payout::run_eval(*plan, cal, i, D - i);
        i += std::max(1, e.days);
        if (!e.passed) continue;
        ++o.passes;
        payout::RunOut r = payout::run_account(*plan, cal, i, D - i);
        o.paid += r.paid_sum; o.payouts += r.payouts;
        i += std::max(1, r.days);
        if (!r.breach) break;
    }
    o.be_fee = o.evals ? o.paid / o.evals : 0;
    return o;
}

}  // namespace lab
