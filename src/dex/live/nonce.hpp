#pragma once
/*  nonce.hpp — per-API-key nonce manager (lighter_tx_spec.md §5).

    Strict nonces (no SkipNonce): each signed tx takes next++. Outcomes decide whether the nonce was consumed:
      * API-server reject (bad sig, malformed, wrong nonce, 429/405, insufficient margin, ExpiredAt too close)
          → NOT consumed. The same nonce is reused; every LATER in-flight nonce on the key will fail too and must be
            re-signed. If a LATER nonce was already reported consumed, the stream is inconsistent → freeze.
      * Sequencer reject BEFORE applying (ExpiredAt passed, margin changed, market inactive) → NOT consumed and every
        later tx on the key fails → freeze, resync from nextNonce.
      * Sequencer fail WHILE applying (bad size/price, duplicate client index, cancel/modify of an unknown order,
        expiry passed) → CONSUMED (included as a no-op).
      * Matching-rule cancels (post-only would cross, IOC unfilled, self-trade) → CONSUMED (successful tx).
      * Executed → CONSUMED.
      * Unknown (timeout, disconnect after send) or "invalid nonce" (21104) → FREEZE until resync(nextNonce).

    Resync: in-flight nonces BELOW the server's next were applied (their orders go to reconciliation). In-flight
    nonces AT OR ABOVE it are UNCONFIRMED — a nextNonce snapshot cannot prove a tx was not applied (it may still be
    in transit to the sequencer). They stay frozen until an explicit answer arrives from a tx-hash / client-index
    lookup: confirm_applied(n), or confirm_not_applied(n, now) which additionally requires the tx's own signed
    ExpiredAt to have passed (then it can never execute). Until then the only thing allowed on such a nonce is
    re-sending the byte-identical signed tx (resend_bytes) — never different content. After the last unconfirmed
    nonce is answered, one more resync() unfreezes the stream.
    The manager is monotonic: a resync value below a nonce already known consumed throws (wrong key/account).

    Accepted-pending (2026-10-26, mm_live): an HTTP code-200 answer only means "queued for the sequencer". The live
    sender calls on_accepted(n); the nonce stays in flight until the sequencer's result arrives on the account feed
    (account_tx): Executed / MatchingCancel / SequencerFailedApplying settle it (and every accepted nonce below it),
    SequencerRejectedBefore un-consumes it AND every later in-flight nonce (they all fail at the sequencer) and
    freezes until resync. A resync leaves accepted nonces at/above the server's next pending (not unconfirmed): they
    are queued, and their result will come.   */

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lighter::live {

struct NonceError : std::runtime_error { using std::runtime_error::runtime_error; };

enum class TxOutcome {
    Executed,                 // consumed
    MatchingCancel,           // post-only cross, IOC unfilled, self-trade: consumed
    SequencerFailedApplying,  // consumed no-op
    ApiRejected,              // not consumed, reuse nonce
    SequencerRejectedBefore,  // not consumed, later txs fail → freeze + resync
    InvalidNonce,             // 21104 → freeze + resync
    Unknown,                  // timeout / disconnect → freeze + resync
};

inline bool consumes_nonce(TxOutcome o) {
    return o == TxOutcome::Executed || o == TxOutcome::MatchingCancel || o == TxOutcome::SequencerFailedApplying;
}

// Classify a sendTx result. http_status is the HTTP status (0 = no response / timeout); code is the body `code`.
inline TxOutcome classify_send(int http_status, int code) {
    if (http_status == 0) return TxOutcome::Unknown;
    if (http_status == 429 || http_status == 405 || code == 23000 || code == 21506) return TxOutcome::ApiRejected;
    if (code == 21104 || code == 21105) return TxOutcome::InvalidNonce;
    if (http_status == 200 && code == 200) return TxOutcome::Executed;   // accepted; the sequencer result comes later
    if (http_status >= 500) return TxOutcome::Unknown;
    return TxOutcome::ApiRejected;
}

// Classify the sequencer result for a tx accepted with code 200.
//   order_status: Lighter order status code (5 = canceled-post-only, 11 self-trade …), -1 if not an order event.
//   app_error:    the `ae` code (0 = none).
inline TxOutcome classify_sequencer(int order_status, int app_error) {
    if (app_error == 0) {
        if (order_status == 5 || order_status == 11 || order_status == 10 || order_status == 9) return TxOutcome::MatchingCancel;
        return TxOutcome::Executed;
    }
    switch (app_error) {   // applied-as-no-op failures (consume) vs rejected-before-apply (do not)
        case 21701: case 21702: case 21706: case 21624: case 21728: case 21600: case 21715: case 21733: case 21734:
            return TxOutcome::SequencerFailedApplying;
        case 21712: case 21713: case 21714:   // cancel-all rules (queued / tif / time): whether the nonce is consumed is
            return TxOutcome::Unknown;        // UNCONFIRMED → freeze and let nextNonce decide (mm_live, review 2)
        default:
            return TxOutcome::SequencerRejectedBefore;
    }
}

// ExpiredAt (unix ms) of a signer tx_info string, 0 if absent.
inline int64_t tx_expired_at_ms(const std::string& tx_info) {
    auto p = tx_info.find("\"ExpiredAt\":");
    if (p == std::string::npos) return 0;
    p += 12;
    int64_t v = 0;
    while (p < tx_info.size() && tx_info[p] >= '0' && tx_info[p] <= '9') v = v * 10 + (tx_info[p++] - '0');
    return v;
}

class NonceManager {
public:
    explicit NonceManager(int api_key_index) : key_(api_key_index) {}

    int api_key_index() const { return key_; }
    bool synced() const { return synced_; }
    bool frozen() const { return frozen_; }
    int64_t peek_next() const { return next_; }
    int64_t consumed_floor() const { return consumed_floor_; }
    size_t in_flight() const { return inflight_.size(); }
    std::vector<int64_t> unconfirmed() const {
        std::vector<int64_t> v;
        for (auto& [n, f] : inflight_) if (f.unconfirmed) v.push_back(n);
        return v;
    }

    // Initial sync / resync from GET nextNonce.
    //   applied:     in flight, below the server's next → applied (orders → reconciliation)
    //   unconfirmed: in flight, at/above it → still frozen until confirm_applied / confirm_not_applied
    //   not_applied: always empty — a nextNonce snapshot never proves a tx was not applied
    //   accepted:    HTTP-accepted, at/above it → still queued at the sequencer: stay Accepted-pending (not frozen)
    struct Resync { std::vector<int64_t> applied, unconfirmed, not_applied, accepted; };
    Resync resync(int64_t server_next) {
        if (server_next < 0) throw NonceError("negative nextNonce");
        if (server_next < consumed_floor_)
            throw NonceError("nextNonce went backwards (" + std::to_string(server_next) + " < " +
                             std::to_string(consumed_floor_) + "): wrong key or account");
        Resync r;
        for (auto it = inflight_.begin(); it != inflight_.end();) {
            if (it->first < server_next) { r.applied.push_back(it->first); it = inflight_.erase(it); }
            else if (it->second.accepted && !it->second.unconfirmed) { r.accepted.push_back(it->first); ++it; }
            else { it->second.unconfirmed = true; r.unconfirmed.push_back(it->first); ++it; }
        }
        next_ = server_next;
        // accepted-pending txs at/above the server's next are queued at the sequencer: never hand their nonces out again
        if (!inflight_.empty()) next_ = std::max(next_, inflight_.rbegin()->first + 1);
        consumed_floor_ = server_next;
        synced_ = true;
        frozen_ = !r.unconfirmed.empty();
        return r;
    }

    // Reserve the next nonce for a tx about to be signed. Throws while unsynced or frozen.
    int64_t reserve() {
        if (!synced_) throw NonceError("nonce manager not synced (fetch nextNonce first)");
        if (frozen_) throw NonceError("nonce manager frozen: resync from nextNonce");
        if (next_ >= (int64_t{1} << 48) - 1) throw NonceError("nonce space exhausted");
        int64_t n = next_++;
        inflight_[n] = {};
        return n;
    }
    // Reserve `count` consecutive nonces (a batch). All or nothing.
    std::vector<int64_t> reserve_consecutive(size_t count) {
        if (!synced_) throw NonceError("nonce manager not synced (fetch nextNonce first)");
        if (frozen_) throw NonceError("nonce manager frozen: resync from nextNonce");
        std::vector<int64_t> out;
        for (size_t i = 0; i < count; ++i) out.push_back(reserve());
        return out;
    }

    // Remember the exact signed bytes for nonce n (the only content ever allowed on n if its outcome goes unknown).
    void attach(int64_t n, const std::string& tx_info) {
        auto it = inflight_.find(n);
        if (it == inflight_.end()) throw NonceError("attach to nonce " + std::to_string(n) + " that is not in flight");
        if (!it->second.tx_info.empty() && it->second.tx_info != tx_info)
            throw NonceError("different content for nonce " + std::to_string(n));
        it->second.tx_info = tx_info;
        it->second.expired_at_ms = tx_expired_at_ms(tx_info);
    }
    // The byte-identical tx to re-send for an unconfirmed nonce (same nonce, same client index), if attached.
    std::optional<std::string> resend_bytes(int64_t n) const {
        auto it = inflight_.find(n);
        if (it == inflight_.end() || it->second.tx_info.empty()) return std::nullopt;
        return it->second.tx_info;
    }

    // Release nonces that were reserved but NEVER sent (e.g. a signing failure inside a batch), from n upward.
    void release_unsent(int64_t n) {
        for (auto it = inflight_.lower_bound(n); it != inflight_.end(); ++it)
            if (it->second.sent) throw NonceError("release_unsent over a sent nonce " + std::to_string(it->first));
        inflight_.erase(inflight_.lower_bound(n), inflight_.end());
        if (n < consumed_floor_) { frozen_ = true; return; }
        next_ = n;
    }
    void mark_sent(int64_t n) {
        auto it = inflight_.find(n);
        if (it == inflight_.end()) throw NonceError("mark_sent of a nonce not in flight");
        it->second.sent = true;
    }

    // Accepted-pending: the API answered code 200 for nonce n. That is "queued for the sequencer", not "executed": the
    // nonce stays in flight until the sequencer's result is seen on the account feed (on_outcome with Executed /
    // MatchingCancel / SequencerFailedApplying, or SequencerRejectedBefore — which un-consumes it and every later
    // one). A later executed nonce also settles every accepted one below it (one key = one FIFO). An UNCONFIRMED
    // nonce whose byte-identical bytes were re-sent and accepted becomes accepted-pending again (still frozen until
    // the next resync decides the stream).
    void on_accepted(int64_t n) {
        auto it = inflight_.find(n);
        if (it == inflight_.end()) throw NonceError("accepted nonce " + std::to_string(n) + " is not in flight");
        it->second.accepted = true;
        it->second.sent = true;
        it->second.unconfirmed = false;
    }
    bool tracking(int64_t n) const { return inflight_.count(n) > 0; }
    // Our view of the stream is wrong (e.g. the API said "invalid nonce"): nothing is signed until resync().
    void freeze() { frozen_ = true; }
    bool is_accepted(int64_t n) const { auto it = inflight_.find(n); return it != inflight_.end() && it->second.accepted; }
    bool is_unconfirmed(int64_t n) const { auto it = inflight_.find(n); return it != inflight_.end() && it->second.unconfirmed; }
    size_t accepted_pending() const {
        size_t c = 0;
        for (auto& [n, f] : inflight_) c += f.accepted && !f.unconfirmed;
        return c;
    }

    // Report the outcome of nonce n. Returns the nonces whose txs must be re-signed.
    std::vector<int64_t> on_outcome(int64_t n, TxOutcome o) {
        auto it = inflight_.find(n);
        if (it == inflight_.end()) throw NonceError("outcome for nonce " + std::to_string(n) + " that is not in flight");
        if (it->second.unconfirmed && o != TxOutcome::Unknown && !consumes_nonce(o))
            throw NonceError("nonce " + std::to_string(n) + " is unconfirmed: answer it with confirm_*");
        std::vector<int64_t> resign;
        if (consumes_nonce(o)) {
            inflight_.erase(it);
            // one API key is a FIFO at the sequencer: n was processed, so every ACCEPTED tx below it was too
            for (auto j = inflight_.begin(); j != inflight_.end() && j->first < n;)
                j = j->second.accepted && !j->second.unconfirmed ? inflight_.erase(j) : std::next(j);
            if (n + 1 > consumed_floor_) consumed_floor_ = n + 1;
            if (next_ < consumed_floor_) next_ = consumed_floor_;
            return resign;
        }
        if (o == TxOutcome::SequencerRejectedBefore) {
            // Not consumed, and every LATER tx already sent on this key fails too (lighter_tx_spec.md §5): all of them
            // leave the in-flight set (their orders go to reconciliation), the stream freezes until resync(nextNonce).
            for (auto j = it; j != inflight_.end(); ++j) resign.push_back(j->first);
            inflight_.erase(it, inflight_.end());
            frozen_ = true;
            next_ = std::max(n, consumed_floor_);
            return resign;
        }
        if (o == TxOutcome::ApiRejected) {
            if (n < consumed_floor_) {
                // a later nonce is already known consumed, so n cannot be reused: inconsistent stream → resync
                inflight_.erase(it);
                frozen_ = true;
                return resign;
            }
            // Not consumed: n is free again. Every later in-flight nonce on this key fails at the API too.
            for (auto j = std::next(it); j != inflight_.end(); ++j) resign.push_back(j->first);
            inflight_.erase(it, inflight_.end());
            next_ = n;
            return resign;
        }
        frozen_ = true;   // the stream's state is uncertain → nothing is signed until resync(nextNonce)
        if (o == TxOutcome::InvalidNonce) {
            inflight_.erase(it);       // known NOT applied: re-sign after the resync
            resign.push_back(n);
        } else {
            it->second.unconfirmed = true;   // Unknown: only byte-identical resend until confirmed
        }
        return resign;
    }

    // Answers from a tx-hash / client-index lookup for an unconfirmed nonce.
    void confirm_applied(int64_t n) {
        auto it = need_unconfirmed(n);
        inflight_.erase(it);
        if (n + 1 > consumed_floor_) consumed_floor_ = n + 1;
        if (next_ < consumed_floor_) next_ = consumed_floor_;
        after_confirm();
    }
    // An unconfirmed nonce whose sequencer RESULT was already seen (a rejection code whose nonce consumption is
    // unknown, e.g. 21712/21713/21714) and that a later nextNonce shows NOT consumed: it is final — the tx was processed
    // and will not execute again — so its nonce is free. Still frozen until the next resync.
    void confirm_rejected_not_consumed(int64_t n) {
        auto it = need_unconfirmed(n);
        inflight_.erase(it);
        after_confirm();
    }
    // Only once the tx's own signed ExpiredAt has passed can "not applied" be final.
    void confirm_not_applied(int64_t n, int64_t now_ms) {
        auto it = need_unconfirmed(n);
        if (it->second.expired_at_ms == 0 || now_ms <= it->second.expired_at_ms)
            throw NonceError("nonce " + std::to_string(n) + " may still execute until its ExpiredAt; re-send it "
                             "byte-identical or wait");
        inflight_.erase(it);
        after_confirm();
    }

private:
    struct InFlight {
        std::string tx_info;
        int64_t expired_at_ms = 0;
        bool sent = false;
        bool unconfirmed = false;
        bool accepted = false;    // API code 200, sequencer result not seen yet (Accepted-pending)
    };
    std::map<int64_t, InFlight>::iterator need_unconfirmed(int64_t n) {
        auto it = inflight_.find(n);
        if (it == inflight_.end() || !it->second.unconfirmed) throw NonceError("nonce " + std::to_string(n) + " is not unconfirmed");
        return it;
    }
    void after_confirm() { frozen_ = true; }   // still frozen: one fresh resync() decides the next nonce

    int key_;
    bool synced_ = false, frozen_ = false;
    int64_t next_ = 0, consumed_floor_ = 0;
    std::map<int64_t, InFlight> inflight_;
};

}  // namespace lighter::live
