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

    The same intrabar path rule most bar backtesters use. It is an APPROXIMATION and replays
    are labelled apart from tick history (nq_bars, not nq_hist): a stop and a target that sit
    inside one bar resolve by this path, not by what really traded first; 1-minute strategies
    are the least reliable, 15-minute and slower ones the most. No quotes (book-gated
    variants stay blocked, as on Binance history).

    size   = the bar's volume split over the four prints (remainder on the close), ≥ 1 each
    is_buy = the print is at or above the previous print (tape modes get a crude delta only)
    Two records in one minute (roll overlap of the continuous contract) → the first is kept,
    exactly as `dbn_tool bars` loads bars_1m, so the replay and the chart agree.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "history/dbn.hpp"
#include "history/mbo_feed.hpp"

#include <array>
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

class OhlcvBarFeed : public TickFeed {
public:
    OhlcvBarFeed(std::vector<std::string> files, int64_t from_us, int64_t to_us)
        : files_(std::move(files)), from_us_(from_us), to_us_(to_us) {}

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
            for (const Tick& t : bar_to_ticks(us, dbn::px(o->open), dbn::px(o->high), dbn::px(o->low),
                                              dbn::px(o->close), o->volume, prev_)) {
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
        std::snprintf(b, sizeof b, "bar feed: %llu records, %llu trades from %llu 1m bars (4 synthetic prints each), "
                      "%llu duplicate minutes, %llu skipped records, %zu file(s)",
                      (unsigned long long)records_, (unsigned long long)ticks_, (unsigned long long)bars_,
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
    bool done_ = false;
    uint64_t records_ = 0, ticks_ = 0, bars_ = 0, dup_minutes_ = 0, skipped_ = 0;
};

} // namespace hist
