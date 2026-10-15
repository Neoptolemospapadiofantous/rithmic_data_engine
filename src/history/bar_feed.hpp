#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    bar_feed.hpp — Databento OHLCV-1m files → synthetic replay ticks

    For the years with no tick data (NQ/ES 2010-07 … 2023-02 in the owned history: only
    1-minute bars), each bar becomes four trades so the paper fleet can still run over it
    (founder 2026-10-11: "trade on all the data we have"):

      t+0.1 s  open
      t+20 s   low  then t+40 s high   when close ≥ open  (the bar went down first, then up)
      t+20 s   high then t+40 s low    when close <  open
      t+59.9 s close

    Between those four points the path is walked tick by tick (every 0.25 from open → first
    extreme → second extreme → close, at most `max_per_leg` prints per leg; above that the step
    grows to whole ticks so a leg never exceeds it). Without it a stop between two of the four
    points filled at the NEXT point — up to a whole bar's range past the stop: on NQ 2026-01 bar
    stops averaged −20.4 pts vs −11.2 on ticks, breakevens −6.2 vs +0.2 (scripts/bars_vs_ticks.sh).

    The same intrabar path rule most bar backtesters use. It is an APPROXIMATION and replays
    are labelled apart from tick history (nq_bars, not nq_hist): a stop and a target that sit
    inside one bar resolve by this path, not by what really traded first; 1-minute strategies
    are the least reliable, 15-minute and slower ones the most. No quotes (book-gated
    variants stay blocked, as on Binance history).

    size   = the bar's volume split over all its prints (remainder on the close), ≥ 1 each
    is_buy = the print is at or above the previous print (tape modes get a crude delta only)
    Two records in one minute (roll overlap of the continuous contract) → the first is kept,
    exactly as `dbn_tool bars` loads bars_1m, so the replay and the chart agree.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "history/dbn.hpp"
#include "history/mbo_feed.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace hist {

// The four synthetic prints of one bar (ts_us = the bar's open minute). Exposed for tests.
inline std::array<Tick, 4> bar_to_ticks(int64_t ts_us, double o, double h, double l, double c, uint64_t vol,
                                        double prev_price) {
    const bool up = c >= o;
    const double mid1 = up ? l : h, mid2 = up ? h : l;
    const int64_t q = vol >= 4 ? (int64_t)(vol / 4) : 1;
    const int64_t last = vol >= 4 ? (int64_t)vol - 3 * q : 1;
    std::array<Tick, 4> t{{
        {ts_us + 100'000,    o,    q,    false},
        {ts_us + 20'000'000, mid1, q,    false},
        {ts_us + 40'000'000, mid2, q,    false},
        {ts_us + 59'900'000, c,    last, false},
    }};
    double p = prev_price > 0.0 ? prev_price : o;
    for (auto& x : t) { x.is_buy = x.price >= p; p = x.price; }
    return t;
}

// The whole walked path of one bar: the four points of bar_to_ticks plus every tick between them.
inline void bar_path(int64_t ts_us, double o, double h, double l, double c, uint64_t vol, double prev_price,
                     double tick, int max_per_leg, std::vector<Tick>& out) {
    const auto key = bar_to_ticks(ts_us, o, h, l, c, vol, prev_price);
    const size_t first = out.size();
    out.push_back(key[0]);
    for (int leg = 0; leg < 3; ++leg) {
        const Tick& a = key[leg];
        const Tick& b = key[leg + 1];
        const double d = b.price - a.price;
        const long ticks = tick > 0 ? std::lround(std::fabs(d) / tick) : 0;
        const long step = (max_per_leg > 0 && ticks > max_per_leg) ? (ticks + max_per_leg - 1) / max_per_leg : 1;
        const long n = ticks > 0 ? (ticks + step - 1) / step : 0;   // prints after a; the n-th is b
        for (long i = 1; i < n; ++i) {
            const double px = a.price + (d > 0 ? 1.0 : -1.0) * tick * (double)(i * step);
            out.push_back({a.ts_us + (b.ts_us - a.ts_us) * i / n, px, 1, false});
        }
        out.push_back(b);
    }
    // volume over every print (remainder on the close), aggressor side from the previous print
    const size_t cnt = out.size() - first;
    const int64_t q = vol >= cnt ? (int64_t)(vol / cnt) : 1;
    double p = prev_price > 0.0 ? prev_price : o;
    for (size_t i = first; i < out.size(); ++i) { out[i].size = q; out[i].is_buy = out[i].price >= p; p = out[i].price; }
    if (vol >= cnt) out.back().size = (int64_t)vol - q * (int64_t)(cnt - 1);
}

class OhlcvBarFeed : public TickFeed {
public:
    // tick: price increment walked between the bar's points (NQ / ES 0.25); max_per_leg caps prints per leg.
    OhlcvBarFeed(std::vector<std::string> files, int64_t from_us, int64_t to_us, double tick = 0.25, int max_per_leg = 40)
        : files_(std::move(files)), from_us_(from_us), to_us_(to_us), tick_(tick), max_per_leg_(max_per_leg) {}

    bool next_batch(std::vector<Tick>& ticks, std::deque<Quote>&, size_t max_ticks) override {
        const size_t t0 = ticks.size();
        while (!done_ && ticks.size() - t0 < max_ticks) {
            if (!r_ && !open_next()) { done_ = true; break; }
            const dbn::RecordHeader* h = r_->next();
            if (!h) { r_.reset(); continue; }
            ++records_;
            const dbn::OhlcvMsg* o = dbn::Reader::as_ohlcv(h);
            if (!o || h->rtype != dbn::kOhlcv1m || o->open == dbn::kUndefPrice || o->close == dbn::kUndefPrice) {
                ++skipped_; continue;
            }
            const int64_t us = (int64_t)(h->ts_event / 1000);
            if (us == last_min_) { ++dup_minutes_; continue; }
            last_min_ = us;
            if (us + 60'000'000 <= from_us_) continue;          // bar ends at/before the window start
            if (us > to_us_) { done_ = true; break; }
            path_.clear();
            bar_path(us, dbn::px(o->open), dbn::px(o->high), dbn::px(o->low), dbn::px(o->close), o->volume, prev_,
                     tick_, max_per_leg_, path_);
            for (const Tick& t : path_) {
                if (t.ts_us <= from_us_ || t.ts_us > to_us_) continue;
                if (t.ts_us < clk_) continue;                    // files are time-ordered; never step back
                clk_ = t.ts_us; prev_ = t.price;
                ticks.push_back(t); ++ticks_;
            }
            ++bars_;
        }
        return ticks.size() > t0;
    }
    std::string summary() const override {
        char b[300];
        std::snprintf(b, sizeof b, "bar feed: %llu records, %llu trades from %llu 1m bars (path walked at %.2f, "
                      "≤ %d prints per leg), %llu duplicate minutes, %llu skipped records, %zu file(s)",
                      (unsigned long long)records_, (unsigned long long)ticks_, (unsigned long long)bars_, tick_, max_per_leg_,
                      (unsigned long long)dup_minutes_, (unsigned long long)skipped_, files_.size());
        return b;
    }

private:
    bool open_next() {
        while (fi_ < files_.size()) {
            const std::string& p = files_[fi_++];
            r_ = std::make_unique<dbn::Reader>(p);
            if (log_) log_("[BARS] reading " + p);
            return true;
        }
        return false;
    }

    std::vector<std::string> files_;
    size_t fi_ = 0;
    std::unique_ptr<dbn::Reader> r_;
    int64_t from_us_, to_us_, clk_ = 0, last_min_ = INT64_MIN;
    double prev_ = 0.0;
    double tick_;
    int max_per_leg_;
    std::vector<Tick> path_;
    bool done_ = false;
    uint64_t records_ = 0, ticks_ = 0, bars_ = 0, dup_minutes_ = 0, skipped_ = 0;
};

} // namespace hist
