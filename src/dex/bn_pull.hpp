#pragma once
// bn_pull.hpp — the Binance-lead quote pull (2026-10-28, founder: "watch Binance and pull quotes"). Shared by the
// live quoter (src/dex/live/engine.hpp, config engine.strategy.bn_pull_*) and the paper twin (mm_paper --bn-pull,
// label paper_bn), so both make the same decision.
//
// Binance futures moves before Lighter. When Binance's price has fallen more than x_bps over the last w_ms, a
// resting BID is about to be hit by someone who saw it first → pull the bid; a rise → pull the ask. The pull is kept
// for hold_ms after the condition was last true. It is the mm_lab rule
//     [["bn_lead_bps@W","<",-X]] → pull_bid, [["bn_lead_bps@W",">",X]] → pull_ask, hold_ms H
// with the same lead definition (RefTrack::lead_bps): the latest price minus the latest price at or before now − W,
// over that price, in bps; unavailable (no pull) until a sample exists at or before now − W, or when the newest
// sample is older than stale_ms. A hold that started stays held even if the feed then goes stale, as in mm_lab.
// Prices are Binance bookTicker mids stamped with OUR receive time (live) — mm_lab's history uses the trade price
// + the measured 135 ms delay. Pulling never adds exposure: with the feed down the strategy quotes as before.
//
// Chosen 2026-10-28 on mm_lab's in-sample day (26 Oct, all Lighter coins, the live setting, measured live latency
// 315/615 ms, scratch registry — exploratory, not a PASS): w 500 / x 4 / hold 1000 was the best of 18 cells
// (11.6 vs 9.5 bps net); the 18 cells averaged ~+0.5 bps over no rule, so the forward paper twin decides.

#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <utility>

namespace bnpull {

struct Params {
    int64_t w_ms = 0;          // 0 = off
    double x_bps = 0;
    int64_t hold_ms = 0;
    int64_t stale_ms = 5'000;
    bool on() const { return w_ms > 0 && x_bps > 0; }
};

class Lead {
public:
    explicit Lead(Params p = {}) : p_(p) {}
    const Params& params() const { return p_; }

    // one price sample at receive time r_ms (non-decreasing); px ≤ 0 is ignored
    void on_price(int64_t r_ms, double px) {
        if (!(px > 0) || !std::isfinite(px)) return;
        if (!q_.empty() && r_ms < q_.back().first) r_ms = q_.back().first;
        q_.push_back({r_ms, px});
        ++samples_;
    }

    struct Out { bool bid = false, ask = false; double lead_bps = std::numeric_limits<double>::quiet_NaN(); };

    // the pull decision at time now (call with a non-decreasing now)
    Out at(int64_t now) {
        Out o;
        if (!p_.on()) return o;
        while (q_.size() >= 2 && q_[1].first <= now - p_.w_ms) q_.pop_front();
        // the newest sample at or before now: a sample stamped later is the decision's future (paper runs on the
        // venue's exchange clock, which lags our receive clock) and is not used yet
        size_t k = q_.size();
        while (k > 0 && q_[k - 1].first > now) --k;
        const bool fresh = k > 0 && now - q_[k - 1].first <= p_.stale_ms;
        if (fresh && q_.front().first <= now - p_.w_ms) {
            const double base = q_.front().second;
            o.lead_bps = (q_[k - 1].second - base) / base * 1e4;
            if (o.lead_bps < -p_.x_bps) bid_true_ = now;
            if (o.lead_bps > p_.x_bps) ask_true_ = now;
        }
        o.bid = now - bid_true_ <= p_.hold_ms;
        o.ask = now - ask_true_ <= p_.hold_ms;
        if (o.bid && !last_.bid) ++pulls_bid_;
        if (o.ask && !last_.ask) ++pulls_ask_;
        last_ = o;
        return o;
    }

    // newest sample received (any time stamp)
    int64_t last_sample_ms() const { return q_.empty() ? -1 : q_.back().first; }
    int64_t samples() const { return samples_; }
    int64_t pulls_bid() const { return pulls_bid_; }
    int64_t pulls_ask() const { return pulls_ask_; }
    const Out& last() const { return last_; }

private:
    static constexpr int64_t kNever = std::numeric_limits<int64_t>::min() / 4;
    Params p_;
    std::deque<std::pair<int64_t, double>> q_;
    int64_t bid_true_ = kNever, ask_true_ = kNever;
    int64_t samples_ = 0, pulls_bid_ = 0, pulls_ask_ = 0;
    Out last_;
};

}  // namespace bnpull
