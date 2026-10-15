#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_levels.hpp — price levels shared by the whole paper fleet (2026-10-11)

    Founder: "lets implement levels into our strategies" — every level family, every use.
    One LevelState per paper_engine process (host-owned, like RegimeState), built from
    1-minute bars: seeded from bars_1m before the start, then fed every tick (the current
    minute is aggregated and folded in when it closes). Levels therefore update once a
    minute, which is all a gate/stop/target decision needs, and the same code runs on
    Postgres ticks, Databento MBO, 1m-bar history and Binance crypto.

    Level families (bit mask, config key `lv_set`: "all" or a comma list of the names):
      day    prior RTH high / low / close, today's RTH open
      on     overnight high / low (18:00 → 09:30 ET of the current globex day)
      or     opening-range high / low after 5, 15 and 30 minutes
      vwap   today's RTH VWAP ± 1σ / 2σ, and the prior day's closing VWAP
      vp     prior RTH day's volume profile: POC, value-area high / low (70 %)
      npoc   naked POCs: earlier days' POCs price has not traded through since (last 10)
      wk     prior week high / low (RTH sessions)        mo  prior month high / low
      round  round numbers: multiples of half of 10^(floor(log10 price) − 2)
             (NQ ~20,000 → every 50; BTC ~100,000 → every 500; ETH ~2,500 → every 5)

    Uses (OrbConfig lv_* keys, applied by PaperBroker; distances are fractions of the prior-day
    ATR14 so one setting means the same on 2010 NQ, 2026 NQ and BTC — session_stats' ATR14 when known, else
    the mean RTH range of the sessions this LevelState has seen; with neither (no completed session yet) every
    lv feature is off and the variant trades exactly like its base):
      lv_room_atr     entry gate: block when the next level AHEAD is closer than x · ATR
      lv_near_atr     entry gate (confluence): enter only within x · ATR of a level
      lv_target       exit at the next level ahead ≥ lv_tgt_min_atr · ATR away (resting limit)
      lv_stop         initial stop just beyond the nearest level BEHIND the entry
                      (≥ lv_stop_min_atr · ATR away, + lv_stop_buf_atr · ATR), never wider than sl_points
    Paper only: the live executor refuses a config carrying lv_* keys.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace paper {

enum LevelKind : int {
    LV_DAY = 1, LV_ON = 2, LV_OR = 4, LV_VWAP = 8, LV_VP = 16, LV_NPOC = 32, LV_WK = 64, LV_MO = 128, LV_ROUND = 256,
    LV_ALL = 511
};

// "all" | "day,on,or,vwap,vp,npoc,wk,mo,round" (any subset) → mask; unknown names are ignored, empty → all.
inline int parse_level_set(const std::string& s) {
    if (s.empty() || s == "all") return LV_ALL;
    static const std::pair<const char*, int> names[] = {
        {"day", LV_DAY}, {"on", LV_ON}, {"or", LV_OR}, {"vwap", LV_VWAP}, {"vp", LV_VP},
        {"npoc", LV_NPOC}, {"wk", LV_WK}, {"mo", LV_MO}, {"round", LV_ROUND}};
    int m = 0; size_t i = 0;
    while (i <= s.size()) {
        size_t j = s.find(',', i); if (j == std::string::npos) j = s.size();
        const std::string tok = s.substr(i, j - i);
        for (const auto& [n, b] : names) if (tok == n) m |= b;
        i = j + 1;
    }
    return m ? m : LV_ALL;
}

inline bool any_level_use(const OrbConfig& c) {
    return c.lv_room_atr > 0.0 || c.lv_near_atr > 0.0 || c.lv_target || c.lv_stop;
}

class LevelState {
public:
    struct Level { double px; int kind; };

    // One closed 1-minute bar (minute_us = the minute's start, UTC µs). Bars must arrive in time order.
    void on_bar(int64_t minute_us, double o, double h, double l, double c, double v) {
        if (!(h >= l) || c <= 0.0 || minute_us <= last_bar_min_) return;   // time order; a re-fed minute counts once
        last_bar_min_ = minute_us;
        Et e = et(minute_us);
        const int hm = e.h * 100 + e.m;
        const long gday = e.day + (e.h >= 18 ? 1 : 0);              // globex day: 18:00 ET opens the next one
        const bool rth = hm >= 930 && hm < 1600;
        if (gday != on_gday_) { on_gday_ = gday; onh_ = -kInf; onl_ = kInf; }
        if (!rth && (e.h >= 18 || hm < 930)) { onh_ = std::max(onh_, h); onl_ = std::min(onl_, l); }   // overnight
        // naked POCs: a level dies once a later bar trades through it (checked before today's
        // session is folded, so a POC born at this session start is judged from its first bar on)
        if (!npocs_.empty())
            npocs_.erase(std::remove_if(npocs_.begin(), npocs_.end(),
                                        [&](double p) { return l <= p && p <= h; }), npocs_.end());
        if (rth_active_ && (e.day != rth_day_ || (!rth && hm >= 1600))) close_rth();
        if (rth) {
            if (!rth_active_) open_rth(e);
            if (rth_open_ <= 0.0) rth_open_ = o > 0.0 ? o : c;
            rth_hi_ = std::max(rth_hi_, h); rth_lo_ = std::min(rth_lo_, l); rth_close_ = c;
            const double tp = (h + l + c) / 3.0, vv = v > 0.0 ? v : 1.0;
            pv_ += tp * vv; pv2_ += tp * tp * vv; vol_ += vv;
            add_profile(l, h, vv);
            const int mins = (e.h * 60 + e.m) - (9 * 60 + 30);         // 0 = the 09:30 bar
            for (int k = 0; k < 3; ++k) {
                if (mins < kOr[k]) { or_hi_[k] = std::max(or_hi_[k], h); or_lo_[k] = std::min(or_lo_[k], l); }
                if (mins >= kOr[k] - 1 && or_hi_[k] > -kInf) or_ready_[k] = true;   // its last minute has closed
            }
        }
        last_px_ = c;
    }

    // Feed every tick (UTC µs). The minute in progress is folded in when the next minute starts.
    void on_tick(int64_t ts_us, double price, double size) {
        const int64_t mu = ts_us - (ts_us % 60'000'000LL);
        if (mu <= last_bar_min_) return;                                // minute already folded in (seeded bars / warm-up re-feed)
        if (mu != cur_min_) {
            if (cur_min_ > 0) on_bar(cur_min_, co_, ch_, cl_, cc_, cv_);
            cur_min_ = mu; co_ = ch_ = cl_ = cc_ = price; cv_ = 0.0;
        }
        ch_ = std::max(ch_, price); cl_ = std::min(cl_, price); cc_ = price; cv_ += size;
        last_px_ = price;
    }

    // All current levels of the families in `mask` (round numbers around `px`), unsorted.
    std::vector<Level> levels(int mask, double px) const {
        std::vector<Level> out;
        auto add = [&](double p, int k) { if ((mask & k) && std::isfinite(p) && p > 0.0) out.push_back({p, k}); };
        if (have_prev_) { add(pdh_, LV_DAY); add(pdl_, LV_DAY); add(pdc_, LV_DAY); }
        if (rth_day_ >= 0 && rth_open_ > 0.0) add(rth_open_, LV_DAY);
        if (onh_ > onl_) { add(onh_, LV_ON); add(onl_, LV_ON); }
        for (int k = 0; k < 3; ++k) if (or_ready_[k]) { add(or_hi_[k], LV_OR); add(or_lo_[k], LV_OR); }
        if (vol_ > 0.0) {
            const double vw = pv_ / vol_, var = std::max(0.0, pv2_ / vol_ - vw * vw), sd = std::sqrt(var);
            add(vw, LV_VWAP);
            if (sd > 0.0) { add(vw + sd, LV_VWAP); add(vw - sd, LV_VWAP); add(vw + 2 * sd, LV_VWAP); add(vw - 2 * sd, LV_VWAP); }
        }
        if (have_prev_ && prev_vwap_ > 0.0) add(prev_vwap_, LV_VWAP);
        if (have_vp_) { add(poc_, LV_VP); add(vah_, LV_VP); add(val_, LV_VP); }
        for (double p : npocs_) add(p, LV_NPOC);
        if (pwh_ > pwl_) { add(pwh_, LV_WK); add(pwl_, LV_WK); }
        if (pmh_ > pml_) { add(pmh_, LV_MO); add(pml_, LV_MO); }
        if ((mask & LV_ROUND) && px > 0.0) {
            const double step = round_step(px);
            const double base = std::floor(px / step) * step;
            for (int i = -2; i <= 3; ++i) add(base + i * step, LV_ROUND);
        }
        return out;
    }

    // Distance from px to the nearest level strictly AHEAD in `dir` (> eps), +inf when none.
    double room_ahead(int mask, double px, int dir, double eps = 1e-9) const {
        double best = kInf;
        for (const auto& lv : levels(mask, px)) { const double d = dir * (lv.px - px); if (d > eps && d < best) best = d; }
        return best;
    }
    // The nearest level BEHIND px (dir · (px − L) ≥ min_dist); 0 when none.
    double level_behind(int mask, double px, int dir, double min_dist) const {
        double best = 0.0, bd = kInf;
        for (const auto& lv : levels(mask, px)) { const double d = dir * (px - lv.px); if (d >= min_dist && d < bd) { bd = d; best = lv.px; } }
        return best;
    }
    // The nearest level AHEAD at least min_dist away; 0 when none.
    double level_ahead(int mask, double px, int dir, double min_dist) const {
        double best = 0.0, bd = kInf;
        for (const auto& lv : levels(mask, px)) { const double d = dir * (lv.px - px); if (d >= min_dist && d < bd) { bd = d; best = lv.px; } }
        return best;
    }
    // Distance from px to the nearest level on either side, +inf when none.
    double nearest_dist(int mask, double px) const {
        double best = kInf;
        for (const auto& lv : levels(mask, px)) best = std::min(best, std::fabs(lv.px - px));
        return best;
    }

    // Entry gate for one config ("" = pass). Fail-open without an ATR (the variant = its base).
    std::string gate(const OrbConfig& c, int dir, double px, double atr) const {
        if (atr <= 0.0 || px <= 0.0) return "";
        const int mask = c.lv_mask ? c.lv_mask : LV_ALL;
        if (c.lv_room_atr > 0.0 && room_ahead(mask, px, dir) < c.lv_room_atr * atr) return "lv_room";
        if (c.lv_near_atr > 0.0 && nearest_dist(mask, px) > c.lv_near_atr * atr) return "lv_far";
        return "";
    }

    static double round_step(double px) {
        if (px <= 0.0) return 0.0;
        return 0.5 * std::pow(10.0, std::floor(std::log10(px)) - 2.0);
    }
    // Fallback ATR: mean RTH high−low of up to the last 14 completed sessions it has seen (0 before the
    // first one). The broker prefers session_stats' ATR14 (RegimeState) and uses this only when that is
    // unknown — e.g. the first days after a data gap, where session_stats' bounded baselines start fresh.
    double atr() const {
        if (ranges_.empty()) return 0.0;
        double s = 0.0; for (double r : ranges_) s += r; return s / (double)ranges_.size();
    }
    double last_price() const { return last_px_; }
    bool   have_prior_day() const { return have_prev_; }

private:
    static constexpr double kInf = std::numeric_limits<double>::infinity();
    static constexpr int kOr[3] = {5, 15, 30};
    struct Et { long day; int h, m, wday, mon_key; };

    // ET calendar parts of a UTC µs time, memoised per minute (every call in a minute agrees).
    Et et(int64_t us) const {
        const int64_t mk = us / 60'000'000LL;
        if (mk == et_key_) return et_cache_;
        time_t tt = (time_t)(us / 1'000'000LL); struct tm g; gmtime_r(&tt, &g);
        const time_t te = tt - (time_t)us_et_offset(g) * 3600; struct tm e; gmtime_r(&te, &e);
        et_key_ = mk;
        et_cache_ = Et{(long)(te / 86400), e.tm_hour, e.tm_min, e.tm_wday, (e.tm_year + 1900) * 12 + e.tm_mon};
        return et_cache_;
    }

    // A new RTH session opens: roll week / month when it belongs to a later period, reset the day.
    void open_rth(const Et& e) {
        rth_day_ = e.day; rth_wk_ = e.day - ((e.wday + 6) % 7); rth_mo_ = e.mon_key;   // week key = its Monday
        roll(wk_key_, wk_hi_, wk_lo_, pwh_, pwl_, rth_wk_);
        roll(mo_key_, mo_hi_, mo_lo_, pmh_, pml_, rth_mo_);
        rth_active_ = true;
        rth_hi_ = -kInf; rth_lo_ = kInf; rth_close_ = 0.0; rth_open_ = 0.0;
        pv_ = pv2_ = vol_ = 0.0; prof_.clear();
        for (int k = 0; k < 3; ++k) { or_hi_[k] = -kInf; or_lo_[k] = kInf; or_ready_[k] = false; }
    }
    // The RTH session ends (first bar at/after 16:00, or the next day's data): it becomes the prior day.
    void close_rth() {
        rth_active_ = false;
        if (!(rth_hi_ > rth_lo_)) return;
        pdh_ = rth_hi_; pdl_ = rth_lo_; pdc_ = rth_close_; have_prev_ = true;
        ranges_.push_back(rth_hi_ - rth_lo_); if (ranges_.size() > 14) ranges_.erase(ranges_.begin());
        prev_vwap_ = vol_ > 0.0 ? pv_ / vol_ : 0.0;
        finalize_profile();
        if (wk_key_ != rth_wk_) { wk_key_ = rth_wk_; wk_hi_ = -kInf; wk_lo_ = kInf; }
        wk_hi_ = std::max(wk_hi_, rth_hi_); wk_lo_ = std::min(wk_lo_, rth_lo_);
        if (mo_key_ != rth_mo_) { mo_key_ = rth_mo_; mo_hi_ = -kInf; mo_lo_ = kInf; }
        mo_hi_ = std::max(mo_hi_, rth_hi_); mo_lo_ = std::min(mo_lo_, rth_lo_);
        // today's session-only levels end with the session
        rth_open_ = 0.0; pv_ = pv2_ = vol_ = 0.0;
        for (int k = 0; k < 3; ++k) or_ready_[k] = false;
    }
    static void roll(long& key, double& hi, double& lo, double& prev_hi, double& prev_lo, long new_key) {
        if (key >= 0 && key != new_key) { if (hi > lo) { prev_hi = hi; prev_lo = lo; } key = new_key; hi = -kInf; lo = kInf; }
    }

    void add_profile(double l, double h, double v) {
        const double b = bucket(), lo = std::floor(l / b), hi = std::floor(h / b);
        const double n = hi - lo + 1.0, per = v / n;
        for (double k = lo; k <= hi; k += 1.0) prof_[(long long)k] += per;
    }
    double bucket() const { const double s = round_step(rth_close_ > 0 ? rth_close_ : last_px_); return s > 0 ? s / 50.0 : 1.0; }
    void finalize_profile() {
        if (prof_.empty()) return;
        const double b = bucket();
        double total = 0.0, pv = -1.0; long long poc = prof_.begin()->first;
        for (const auto& [k, v] : prof_) { total += v; if (v > pv) { pv = v; poc = k; } }
        auto it_lo = prof_.find(poc), it_hi = it_lo; double inside = pv;
        while (inside < 0.7 * total) {
            auto up = std::next(it_hi); const bool can_up = up != prof_.end();
            const bool can_dn = it_lo != prof_.begin();
            if (!can_up && !can_dn) break;
            const double vu = can_up ? up->second : -1.0, vd = can_dn ? std::prev(it_lo)->second : -1.0;
            if (vu >= vd) { it_hi = up; inside += vu; } else { it_lo = std::prev(it_lo); inside += vd; }
        }
        poc_ = (poc + 0.5) * b; vah_ = (it_hi->first + 1) * b; val_ = it_lo->first * b; have_vp_ = true;
        npocs_.push_back(poc_); if (npocs_.size() > 10) npocs_.erase(npocs_.begin());   // naked from birth until traded through
    }

    // RTH session in progress
    long rth_day_ = -1, rth_wk_ = -1, rth_mo_ = -1; bool rth_active_ = false;
    double rth_hi_ = -kInf, rth_lo_ = kInf, rth_close_ = 0.0, rth_open_ = 0.0;
    double pv_ = 0.0, pv2_ = 0.0, vol_ = 0.0;
    std::map<long long, double> prof_;
    double or_hi_[3] = {-kInf, -kInf, -kInf}, or_lo_[3] = {kInf, kInf, kInf}; bool or_ready_[3] = {false, false, false};
    // prior day
    bool have_prev_ = false; double pdh_ = 0, pdl_ = 0, pdc_ = 0, prev_vwap_ = 0;
    bool have_vp_ = false; double poc_ = 0, vah_ = 0, val_ = 0;
    std::vector<double> npocs_;
    std::vector<double> ranges_;                                   // completed RTH high−low, newest last (≤ 14)
    // overnight
    long on_gday_ = -1; double onh_ = -kInf, onl_ = kInf;
    // week / month
    long wk_key_ = -1, mo_key_ = -1;
    double wk_hi_ = -kInf, wk_lo_ = kInf, mo_hi_ = -kInf, mo_lo_ = kInf;
    double pwh_ = 0, pwl_ = 0, pmh_ = 0, pml_ = 0;
    // current minute (tick feed)
    int64_t cur_min_ = 0; double co_ = 0, ch_ = 0, cl_ = 0, cc_ = 0, cv_ = 0;
    double last_px_ = 0.0;
    int64_t last_bar_min_ = INT64_MIN;
    mutable int64_t et_key_ = INT64_MIN; mutable Et et_cache_{};

public:
    // test hooks
    double pdh() const { return pdh_; } double pdl() const { return pdl_; } double pdc() const { return pdc_; }
    double onh() const { return onh_; } double onl() const { return onl_; }
    double poc() const { return poc_; } double vah() const { return vah_; } double val() const { return val_; }
    double pwh() const { return pwh_; } double pwl() const { return pwl_; }
    bool or_ready(int k) const { return or_ready_[k]; } double or_hi(int k) const { return or_hi_[k]; } double or_lo(int k) const { return or_lo_[k]; }
    double rth_open() const { return rth_open_; }
    const std::vector<double>& npocs() const { return npocs_; }
};

} // namespace paper
