#pragma once
/*  tx_scheduler.hpp — request rate limiter and tx batcher.

    RateLimiter: rolling 60 s window of REQUESTS (one sendTx or one sendTxBatch = one request; that a batch counts as
    one request toward Standard's 60/min is UNCONFIRMED by Lighter, see lighter_tx_spec.md §6). Budget default 54
    (6 under the exchange's 60). The last `safety_reserve` (default 6) tokens are only usable by safety traffic
    (cancel / cancel-all), so quoting can never starve an emergency cancel. HTTP 429/405 → nothing at all is sent
    for 60 s.

    Batcher: every intent is validated (all signer range checks + the order-expiry window) when submitted and an
    invalid one is refused there, so it can never share a batch with — and sink — a cancel. Unsent intents can be
    requeued (a requeued create never overrides a newer one for its slot).
    Priority cancel-all > cancel > modify > create; at most max_txs per batch (default 15, the safe REST
    size); the caller assigns consecutive nonces in batch order. The newest create/modify for a slot replaces a stale
    queued one (only the latest desired quote matters). A create is held back while may_create(slot) is false (the
    slot's previous order is not terminal yet) — never two live orders on one side.                          */

#include "dex/live/signer.hpp"

#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

namespace lighter::live {

class RateLimiter {
public:
    RateLimiter(int budget = 54, int safety_reserve = 6, int64_t window_ms = 60'000, int64_t backoff_ms = 60'000)
        : budget_(budget), reserve_(safety_reserve), window_(window_ms), backoff_(backoff_ms) {
        if (budget_ < 1 || reserve_ < 0 || reserve_ >= budget_) throw std::invalid_argument("bad rate budget");
    }
    int used(int64_t now_ms) {
        while (!sent_.empty() && sent_.front() <= now_ms - window_) sent_.pop_front();
        return int(sent_.size());
    }
    bool backing_off(int64_t now_ms) const { return now_ms < blocked_until_; }
    int64_t blocked_until() const { return blocked_until_; }
    // safety = cancels / cancel-all only.
    bool can_send(int64_t now_ms, bool safety) {
        if (backing_off(now_ms)) return false;
        return used(now_ms) < (safety ? budget_ : budget_ - reserve_);
    }
    void record(int64_t now_ms) { sent_.push_back(now_ms); }
    void on_rate_limited(int64_t now_ms) {   // HTTP 429 / 405
        blocked_until_ = std::max(blocked_until_, now_ms + backoff_);
        ++rate_limited_count_;
    }
    int rate_limited_count() const { return rate_limited_count_; }
    int budget() const { return budget_; }
    // Persistence (mm_live): the requests of the current window and the back-off, so a crash + restart inside the
    // same minute cannot spend the budget twice.
    std::vector<int64_t> window(int64_t now_ms) { used(now_ms); return {sent_.begin(), sent_.end()}; }
    void preload(const std::vector<int64_t>& sent, int64_t blocked_until, int64_t now_ms) {
        for (int64_t t : sent) if (t > now_ms - window_ && t <= now_ms) sent_.push_back(t);
        std::sort(sent_.begin(), sent_.end());
        blocked_until_ = std::max(blocked_until_, blocked_until);
    }

private:
    int budget_, reserve_;
    int64_t window_, backoff_;
    std::deque<int64_t> sent_;
    int64_t blocked_until_ = INT64_MIN;
    int rate_limited_count_ = 0;
};

enum class IntentKind : int { CancelAll = 0, Cancel = 1, Modify = 2, Create = 3 };   // = priority order

struct Intent {
    IntentKind kind = IntentKind::Create;
    uint64_t slot = 0;                 // market × side
    int64_t market = -1;
    int64_t order_ref = 0;             // create: new client index; cancel/modify: client or exchange index
    int64_t base = 0, price = 0;
    bool is_ask = false;
    TimeInForce tif = TimeInForce::PostOnly;
    bool reduce_only = false;
    int64_t expiry_ms = 0;
    CancelAllTif ca_tif = CancelAllTif::Immediate;
    int64_t ca_time_ms = 0;
    uint64_t seq = 0;                  // set by the batcher (FIFO within a priority)
};

inline uint64_t slot_key(int64_t market, bool is_ask) { return (uint64_t(market) << 1) | (is_ask ? 1u : 0u); }

// Every check the signer would make, run BEFORE the batcher accepts an intent, so a bad intent never reaches a
// batch (where it could sink the cancels signed with it). Empty = valid. The signer re-checks at signing time.
inline std::string validate_intent(const Intent& i, int64_t now_ms) {
    try {
        switch (i.kind) {
            case IntentKind::Create:
                check_market_index(i.market); check_client_order_index(i.order_ref); check_base_amount(i.base);
                check_price(i.price); check_order_expiry(i.tif, i.expiry_ms, now_ms);
                break;
            case IntentKind::Modify:
                check_market_index(i.market); check_order_ref(i.order_ref); check_base_amount(i.base); check_price(i.price);
                break;
            case IntentKind::Cancel:
                check_market_index(i.market); check_order_ref(i.order_ref);
                break;
            case IntentKind::CancelAll:
                if (i.market != kNilMarketIndex) {
                    check_market_index(i.market);
                    if (i.ca_tif != CancelAllTif::Immediate) return "per-market cancel-all must be immediate";
                }
                if (i.ca_tif == CancelAllTif::Scheduled ? i.ca_time_ms <= 0 : i.ca_time_ms != 0) return "bad cancel-all time";
                break;
        }
    } catch (const RangeError& e) {
        return e.what();
    }
    return {};
}

class Batcher {
public:
    explicit Batcher(int max_txs = 15) : max_(max_txs) {
        if (max_ < 1 || max_ > 50) throw std::invalid_argument("max txs per batch must be 1..50");
    }

    // Returns the rejection reason (empty = queued). An invalid intent is refused here, never queued.
    std::string submit(Intent in, int64_t now_ms = wall_now_ms()) {
        std::string why = validate_intent(in, now_ms);
        if (!why.empty()) { ++rejected_; return why; }
        enqueue(std::move(in));
        return {};
    }
    // Put back intents that were taken into a batch but not sent (they keep their priority: cancels first).
    // A requeued create/modify never overrides a NEWER queued one for the same slot.
    void requeue(const std::vector<Intent>& ins) {
        for (const auto& i : ins) {
            bool newer = (i.kind == IntentKind::Create || i.kind == IntentKind::Modify) &&
                std::any_of(queue_.begin(), queue_.end(), [&](const Intent& q) { return q.kind == i.kind && q.slot == i.slot; });
            if (!newer) enqueue(i);
        }
    }
    uint64_t rejected() const { return rejected_; }

private:
    void enqueue(Intent in) {
        in.seq = ++seq_;
        if (in.kind == IntentKind::Create || in.kind == IntentKind::Modify) {
            for (auto& q : queue_)
                if (q.kind == in.kind && q.slot == in.slot) { ++merged_; q = in; return; }
        } else if (in.kind == IntentKind::Cancel) {
            for (auto& q : queue_) if (q.kind == IntentKind::Cancel && q.order_ref == in.order_ref) { ++merged_; return; }
        } else {
            for (auto& q : queue_)
                if (q.kind == IntentKind::CancelAll && q.ca_tif == in.ca_tif && q.market == in.market) { ++merged_; q = in; return; }
        }
        queue_.push_back(in);
    }

public:
    // Drop queued creates/modifies (e.g. on halt); cancels stay.
    void drop_quoting() {
        std::erase_if(queue_, [](const Intent& i) { return i.kind == IntentKind::Create || i.kind == IntentKind::Modify; });
    }

    // Build the next batch. Records one request on the limiter when non-empty.
    std::vector<Intent> next_batch(int64_t now_ms, RateLimiter& rl, const std::function<bool(uint64_t)>& may_create) {
        std::vector<Intent> out;
        if (queue_.empty() || rl.backing_off(now_ms)) return out;
        bool quoting_ok = rl.can_send(now_ms, false), safety_ok = rl.can_send(now_ms, true);
        if (!safety_ok) return out;
        std::stable_sort(queue_.begin(), queue_.end(), [](const Intent& a, const Intent& b) {
            return a.kind != b.kind ? int(a.kind) < int(b.kind) : a.seq < b.seq;
        });
        std::vector<Intent> keep;
        for (auto& i : queue_) {
            bool safety = i.kind == IntentKind::Cancel || i.kind == IntentKind::CancelAll;
            bool fits = int(out.size()) < max_;
            if (fits && safety) { out.push_back(i); continue; }
            if (fits && quoting_ok && (i.kind == IntentKind::Modify || may_create(i.slot))) {
                // a create for a slot whose cancel is in this batch waits (old order not terminal yet)
                bool cancel_same_slot = i.kind == IntentKind::Create && std::any_of(out.begin(), out.end(), [&](const Intent& o) {
                    return (o.kind == IntentKind::Cancel && o.slot == i.slot) || o.kind == IntentKind::CancelAll;
                });
                if (!cancel_same_slot) { out.push_back(i); continue; }
            }
            keep.push_back(i);
        }
        queue_ = std::move(keep);
        if (!out.empty()) rl.record(now_ms);
        return out;
    }

    size_t queued() const { return queue_.size(); }
    uint64_t merged() const { return merged_; }

private:
    int max_;
    uint64_t seq_ = 0, merged_ = 0, rejected_ = 0;
    std::vector<Intent> queue_;
};

}  // namespace lighter::live
