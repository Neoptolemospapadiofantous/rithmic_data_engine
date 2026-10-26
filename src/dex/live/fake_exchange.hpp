#pragma once
/*  fake_exchange.hpp — in-memory Lighter used by the tests AND by `mm_live --mode offline` (the end-to-end soak).
    Moved here from tests/dex/lighter_fake_exchange.hpp (which now just includes this file) so the binary can use it.
    It never opens a socket. It consumes the signer's tx_info JSON strings exactly as sendTx / sendTxBatch would
    receive them and applies the semantics recorded in lighter_tx_spec.md:
      * per-API-key strict nonces; a wrong nonce is rejected at the API (21104 / 21105 in a batch), NOT consumed;
      * 60 requests per rolling minute (one batch = one request) → HTTP 429, nothing consumed;
      * ExpiredAt must be ≥ now + 5 s, else rejected at the API (not consumed);
      * batch: ≤ 50 txs, one API key. What happens to the REST of a batch after one bad tx is UNCONFIRMED, so it is
        a switch: BatchMode::WholeBatch (nothing applied) or BatchMode::PrefixAccepted (txs before the bad one are
        applied and consume their nonces; the bad one and the rest are not);
      * post-only that would cross → executed tx, order status canceled-post-only (5), nonce consumed;
      * duplicate open client index → sequencer no-op (21728), nonce consumed;
      * below min_base / min notional (set_market) → sequencer no-op (21706), nonce consumed;
      * reduce-only that would grow the position → canceled-reduce-only (6), nonce consumed;
      * IOC: fills at its limit if it crosses (taker), otherwise cancelled (4);
      * Standard account: cancel / modify / immediate cancel-all take effect 300 ms later (the order can fill
        meanwhile); a SCHEDULED cancel-all (TIF 1) fires at its Time; an abort (TIF 2) removes it (a no-op when none
        is queued); a per-market cancel-all (L2TxAttributes {"5": m}) only touches market m; with
        set_min_cancel_all_ms(x) a scheduled time less than x ahead fails in the sequencer (21714, consumed);
      * a fill that happens inside the cancel delay is delivered AFTER the cancel confirm with probability
        `late_fill_prob` (seeded RNG) — the account stream really can do that;
      * inject_reject_before_apply(n): the API accepts nonce n (HTTP 200), then the sequencer rejects it BEFORE
        applying — the nonce is NOT consumed and every later tx of that request fails too (lighter_tx_spec.md §5);
      * exchange order indices start at 2^48, disjoint from client indices (1..2^48-1);
      * partial fills via hit(); every fill, every applied tx and every order is kept for the REST views
        (active/inactive orders, trades, tx results) the in-process venue (fake_venue.hpp) serves.
    No signature verification (the signature is randomized; we only require a non-empty Sig).           */

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace lighter::live::fake {

struct FakeEvent {
    enum Type { Ack, Fill, Cancelled, Modified, TxFailed } type;
    int64_t client_index = 0, order_index = 0, trade_id = 0, qty = 0, price = 0, nonce = -1;
    int status = 0, app_error = 0;
    int64_t market = 0;
    bool is_ask = false;
};

struct SendResult { int http = 200; int code = 200; std::string message; size_t accepted = 0; };

enum class BatchMode { WholeBatch, PrefixAccepted };

// One applied (or sequencer-failed) transaction, as account_tx would report it.
struct TxRecord {
    int64_t nonce = 0;
    int api_key = 0;
    uint8_t type = 0;
    int status = 2;          // 2 executed, 0 failed
    int app_error = 0;       // 0 = none
    int order_status = -1;   // order status of a create (5 = canceled post-only …), -1 n/a
    int64_t time_ms = 0;
};

// One fill of OUR order (we are always the maker here, except IOC orders which take).
struct TradeRecord {
    int64_t trade_id = 0, market = 0, client_index = 0, order_index = 0, qty = 0, price = 0, time_ms = 0;
    bool is_ask = false;     // our side: true = we sold
    bool maker = true;
};

struct OrderView {
    int64_t client_index = 0, order_index = 0, market = 0, base = 0, filled = 0, price = 0;
    bool is_ask = false;
    int status = 2;          // Lighter order status code
    int tif = 2;
    bool reduce_only = false;
    int64_t updated_ms = 0;
};

class FakeExchange {
public:
    explicit FakeExchange(int64_t account_index, int64_t now_ms, uint32_t seed = 1)
        : acct_(account_index), now_(now_ms), rng_(seed) {}

    int64_t account_index() const { return acct_; }
    void set_next_nonce(int key, int64_t n) { next_nonce_[key] = n; }
    int64_t next_nonce(int key) const { auto it = next_nonce_.find(key); return it == next_nonce_.end() ? 0 : it->second; }
    void set_book(int64_t market, int64_t bid, int64_t ask) { book_[market] = {bid, ask}; }
    std::pair<int64_t, int64_t> book(int64_t market) const { auto it = book_.find(market); return it == book_.end() ? std::pair<int64_t, int64_t>{0, 0} : it->second; }
    // min_notional_units is in base×price integer units (e.g. $10.50 → 10.5 × 10^(sd+pd)).
    void set_market(int64_t market, int64_t min_base, int64_t min_notional_units) { mins_[market] = {min_base, min_notional_units}; }
    void set_position(int64_t market, int64_t pos) { pos_[market] = pos; }
    int64_t position(int64_t market) const { auto it = pos_.find(market); return it == pos_.end() ? 0 : it->second; }
    const std::map<int64_t, int64_t>& positions() const { return pos_; }
    void set_batch_mode(BatchMode m) { batch_mode_ = m; }
    void set_late_fill_prob(double p) { late_fill_prob_ = p; }
    void set_min_cancel_all_ms(int64_t ms) { min_cancel_all_ms_ = ms; }
    // What a SCHEDULED cancel-all does while another is queued is UNCONFIRMED (lighter_tx_spec.md §2.7): Replace (the
    // new deadline replaces the old), RejectWhileQueued (21712, the old stays), Stack (both stay; legacy fake).
    enum class ScheduleMode { Replace, RejectWhileQueued, Stack };
    void set_schedule_mode(ScheduleMode m) { schedule_mode_ = m; }
    // Whether a cancel-all rule error (21712 / 21714) consumes the nonce is UNCONFIRMED: default yes.
    void set_cancel_all_errors_consume(bool c) { ca_errors_consume_ = c; }
    void inject_reject_before_apply(int64_t nonce) { reject_before_.push_back(nonce); }
    void set_now(int64_t ms) { if (ms > now_) now_ = ms; apply_due(); }
    void advance(int64_t ms) { set_now(now_ + ms); }
    int64_t now() const { return now_; }

    SendResult send_tx(uint8_t type, const std::string& info) { return send_batch({{type, info}}); }

    SendResult send_batch(const std::vector<std::pair<uint8_t, std::string>>& txs) {
        while (!requests_.empty() && requests_.front() <= now_ - 60'000) requests_.pop_front();
        if (requests_.size() >= 60) return {429, 23000, "Too Many Requests!", 0};
        requests_.push_back(now_);
        if (txs.empty()) return {400, 21500, "empty batch", 0};
        if (txs.size() > 50) return {400, 21514, "maximum 50 transactions allowed per batch", 0};
        std::vector<nlohmann::json> parsed;
        for (auto& [t, s] : txs) {
            auto j = nlohmann::json::parse(s, nullptr, false);
            if (j.is_discarded() || !j.is_object()) return {400, 21000, "malformed tx_info", 0};
            for (const char* k : {"ApiKeyIndex", "AccountIndex", "ExpiredAt", "Nonce"})
                if (!j.contains(k) || !j[k].is_number_integer()) return {400, 21000, "malformed tx_info", 0};
            parsed.push_back(j);
        }
        int key = parsed[0].at("ApiKeyIndex").get<int>();
        for (auto& j : parsed) if (j.at("ApiKeyIndex").get<int>() != key) return {400, 21121, "batch spans api keys", 0};
        int64_t expect = next_nonce(key);
        size_t good = 0;
        SendResult bad{200, 200, "", 0};
        for (; good < parsed.size(); ++good) {
            auto& j = parsed[good];
            if (j.at("AccountIndex").get<int64_t>() != acct_) { bad = {400, 21100, "wrong account", 0}; break; }
            if (j.value("Sig", std::string{}).empty()) { bad = {400, 21120, "missing signature", 0}; break; }
            if (j.at("ExpiredAt").get<int64_t>() < now_ + 5'000) { bad = {400, 21116, "tx expires too soon", 0}; break; }
            if (j.at("Nonce").get<int64_t>() != expect + int64_t(good)) {
                bad = {400, txs.size() > 1 ? 21105 : 21104, "invalid nonce", 0}; break;
            }
        }
        size_t apply_n = good == parsed.size() || batch_mode_ == BatchMode::PrefixAccepted ? good : 0;
        next_nonce_[key] = expect + int64_t(apply_n);       // accepted txs consume their nonces, in order
        for (size_t k = 0; k < apply_n; ++k) {
            int64_t n = parsed[k].at("Nonce").get<int64_t>();
            if (auto it = std::find(reject_before_.begin(), reject_before_.end(), n); it != reject_before_.end()) {
                // sequencer rejects n BEFORE applying: n not consumed, every later tx of this request fails too
                reject_before_.erase(it);
                next_nonce_[key] = n;
                for (size_t r = k; r < apply_n; ++r)
                    txlog_.push_back({parsed[r].at("Nonce").get<int64_t>(), key, txs[r].first, 0, kRejectBeforeApply, -1, now_});
                break;
            }
            not_consumed_ = false;
            apply(txs[k].first, parsed[k]);
            if (not_consumed_) {   // a cancel-all rule error that does NOT consume (set_cancel_all_errors_consume(false))
                next_nonce_[key] = n;
                for (size_t r = k + 1; r < apply_n; ++r)
                    txlog_.push_back({parsed[r].at("Nonce").get<int64_t>(), key, txs[r].first, 0, kRejectBeforeApply, -1, now_});
                break;
            }
        }
        if (apply_n) ++accepted_requests_;
        if (good == parsed.size()) return {200, 200, "", apply_n};
        bad.accepted = apply_n;
        return bad;
    }

    // A taker hits our resting order for qty (partial fills allowed). Works in the cancel-delay window too.
    bool hit(int64_t client_index, int64_t qty) {
        auto* o = find(client_index);
        if (!o || o->status != 2) return false;
        int64_t q = std::min(qty, o->base - o->filled);
        if (q <= 0) return false;
        o->filled += q;
        o->updated = now_;
        pos_[o->market] += o->ask ? -q : q;
        FakeEvent f{FakeEvent::Fill, o->coi, o->oi, ++trade_id_, q, o->price, -1, 2, 0, o->market, o->ask};
        trades_.push_back({f.trade_id, o->market, o->coi, o->oi, q, o->price, now_, o->ask, true});
        if (o->filled == o->base) o->status = 3;
        bool cancel_pending = std::any_of(delayed_.begin(), delayed_.end(), [&](const Delayed& d) {
            return d.type == 15 && (d.tx.at("Index").get<int64_t>() == o->coi || d.tx.at("Index").get<int64_t>() == o->oi);
        });
        if (cancel_pending && std::uniform_real_distribution<double>(0, 1)(rng_) < late_fill_prob_) {
            deferred_[o->coi].push_back(f);   // delivered after the cancel confirm
            ++late_fills_emitted_;
        } else {
            events_.push_back(f);
        }
        return true;
    }

    // Our resting (status open) orders in a market.
    std::vector<OrderView> resting(int64_t market) const {
        std::vector<OrderView> v;
        for (auto& [_, o] : orders_) if (o.status == 2 && o.market == market) v.push_back(view(o));
        return v;
    }
    std::vector<OrderView> active_orders() const {
        std::vector<OrderView> v;
        for (auto& [_, o] : orders_) if (o.status == 2) v.push_back(view(o));
        return v;
    }
    // Terminal orders, most recently updated first (accountInactiveOrders).
    std::vector<OrderView> inactive_orders(size_t limit) const {
        std::vector<OrderView> v;
        for (auto& [_, o] : orders_) if (o.status != 2) v.push_back(view(o));
        std::stable_sort(v.begin(), v.end(), [](const OrderView& a, const OrderView& b) { return a.updated_ms > b.updated_ms; });
        if (v.size() > limit) v.resize(limit);
        return v;
    }
    // An order by client index or exchange index (the open one first), for the venue's order messages.
    std::optional<OrderView> find_view(int64_t idx) const {
        const Ord* best = nullptr;
        if (idx > 0)
            for (auto& [oi, o] : orders_)
                if (o.coi == idx || o.oi == idx) { if (!best || o.status == 2) best = &o; }
        if (!best) return std::nullopt;
        return view(*best);
    }
    const std::vector<TradeRecord>& trades() const { return trades_; }
    const std::vector<TxRecord>& tx_log() const { return txlog_; }

    // Place an order that is NOT ours (e.g. from the Lighter UI) directly on the book — for unowned-order tests.
    int64_t place_foreign(int64_t market, bool is_ask, int64_t base, int64_t price, int64_t client_index = 0) {
        Ord o;
        o.coi = client_index; o.market = market; o.base = base; o.price = price; o.ask = is_ask; o.tif = 1; o.status = 2;
        o.oi = (int64_t{1} << 48) + ++order_seq_;
        o.updated = now_;
        orders_[o.oi] = o;
        return o.oi;
    }

    std::vector<FakeEvent> drain() { auto e = std::move(events_); events_.clear(); return e; }
    int open_orders() const { int n = 0; for (auto& [_, o] : orders_) n += o.status == 2; return n; }
    int order_status(int64_t client_index) { auto* o = find(client_index); return o ? o->status : -1; }
    int64_t order_price(int64_t client_index) { auto* o = find(client_index); return o ? o->price : -1; }
    int accepted_requests() const { return accepted_requests_; }
    int late_fills_emitted() const { return late_fills_emitted_; }
    size_t scheduled_cancel_alls() const { return scheduled_.size(); }
    int64_t scheduled_cancel_all_time() const { return scheduled_.empty() ? 0 : scheduled_.back().due; }

    static constexpr int kRejectBeforeApply = 21116;   // a code classify_sequencer treats as "rejected before apply"

private:
    struct Ord {
        int64_t coi = 0, oi = 0, market = 0, base = 0, filled = 0, price = 0;
        bool ask = false; int tif = 0; int status = 2; bool reduce_only = false; int64_t updated = 0;
    };
    struct Delayed { int64_t due; uint8_t type; nlohmann::json tx; };

    static OrderView view(const Ord& o) {
        return {o.coi, o.oi, o.market, o.base, o.filled, o.price, o.ask, o.status, o.tif, o.reduce_only, o.updated};
    }
    Ord* find(int64_t idx) {
        if (idx <= 0) return nullptr;
        Ord* best = nullptr;
        for (auto& [oi, o] : orders_)
            if (o.coi == idx || o.oi == idx) { if (!best || o.status == 2) best = &o; }
        return best;
    }
    bool crosses(int64_t market, bool ask, int64_t px) const {
        auto it = book_.find(market);
        if (it == book_.end()) return false;
        return ask ? px <= it->second.first : px >= it->second.second;
    }
    static int cancel_all_market(const nlohmann::json& tx) {
        const auto& a = tx.at("L2TxAttributes");
        if (a.is_object() && a.contains("5")) return a.at("5").get<int>();
        return 255;
    }
    void emit_cancel(Ord& o, int64_t nonce, int status = 4) {
        o.status = status;
        o.updated = now_;
        events_.push_back({FakeEvent::Cancelled, o.coi, o.oi, 0, 0, 0, nonce, status, 0, o.market, o.ask});
        if (auto it = deferred_.find(o.coi); it != deferred_.end()) {
            for (auto& f : it->second) events_.push_back(f);
            deferred_.erase(it);
        }
    }
    void cancel_all(int market, int64_t nonce) {
        for (auto& [_, o] : orders_) if (o.status == 2 && (market == 255 || o.market == market)) emit_cancel(o, nonce);
    }
    void log_tx(int64_t nonce, int key, uint8_t type, int app_error, int order_status = -1) {
        txlog_.push_back({nonce, key, type, app_error ? 0 : 2, app_error, order_status, now_});
    }

    void apply(uint8_t type, const nlohmann::json& j) {
        int64_t nonce = j.at("Nonce").get<int64_t>();
        int key = j.at("ApiKeyIndex").get<int>();
        if (type == 14) {
            int64_t coi = j.at("ClientOrderIndex").get<int64_t>();
            if (auto* o = find(coi); o && o->status == 2) {
                events_.push_back({FakeEvent::TxFailed, coi, 0, 0, 0, 0, nonce, 0, 21728});
                log_tx(nonce, key, type, 21728);
                return;
            }
            Ord o;
            o.coi = coi; o.market = j.at("MarketIndex").get<int64_t>(); o.base = j.at("BaseAmount").get<int64_t>();
            o.price = j.at("Price").get<int64_t>(); o.ask = j.at("IsAsk").get<int>() == 1; o.tif = j.at("TimeInForce").get<int>();
            o.updated = now_;
            bool reduce_only = j.at("ReduceOnly").get<int>() == 1;
            o.reduce_only = reduce_only;
            if (auto m = mins_.find(o.market); m != mins_.end() && !(o.tif == 0 && reduce_only)) {
                if (o.base < m->second.first || __int128(o.base) * o.price < m->second.second) {
                    events_.push_back({FakeEvent::TxFailed, coi, 0, 0, 0, 0, nonce, 0, 21706});
                    log_tx(nonce, key, type, 21706);
                    return;
                }
            }
            o.oi = (int64_t{1} << 48) + ++order_seq_;
            if (reduce_only) {
                int64_t pos = position(o.market);
                bool ok = o.ask ? pos > 0 && o.base <= pos : pos < 0 && o.base <= -pos;
                if (!ok) {
                    o.status = 6; orders_[o.oi] = o;
                    events_.push_back({FakeEvent::Cancelled, coi, o.oi, 0, 0, 0, nonce, 6, 0, o.market, o.ask});
                    log_tx(nonce, key, type, 0, 6);
                    return;
                }
            }
            if (o.tif == 2 && crosses(o.market, o.ask, o.price)) {   // post-only would cross: executed, cancelled (5)
                o.status = 5;
                orders_[o.oi] = o;
                events_.push_back({FakeEvent::Cancelled, coi, o.oi, 0, 0, 0, nonce, 5, 0, o.market, o.ask});
                log_tx(nonce, key, type, 0, 5);
                return;
            }
            if (o.tif == 0) {   // IOC: take at the limit if it crosses, else cancel
                if (crosses(o.market, o.ask, o.price)) {
                    o.filled = o.base; o.status = 3; orders_[o.oi] = o;
                    pos_[o.market] += o.ask ? -o.base : o.base;
                    auto bk = book_[o.market];
                    int64_t px = o.ask ? bk.first : bk.second;            // takes the touch (never worse than its limit)
                    events_.push_back({FakeEvent::Ack, coi, o.oi, 0, 0, o.price, nonce, 2, 0, o.market, o.ask});
                    events_.push_back({FakeEvent::Fill, coi, o.oi, ++trade_id_, o.base, px, nonce, 3, 0, o.market, o.ask});
                    trades_.push_back({trade_id_, o.market, coi, o.oi, o.base, px, now_, o.ask, false});
                    log_tx(nonce, key, type, 0, 3);
                } else {
                    o.status = 4; orders_[o.oi] = o;
                    events_.push_back({FakeEvent::Cancelled, coi, o.oi, 0, 0, 0, nonce, 4, 0, o.market, o.ask});
                    log_tx(nonce, key, type, 0, 4);
                }
                return;
            }
            orders_[o.oi] = o;
            events_.push_back({FakeEvent::Ack, coi, o.oi, 0, 0, o.price, nonce, 2, 0, o.market, o.ask});
            log_tx(nonce, key, type, 0, 2);
        } else if (type == 16) {
            int tif = j.at("TimeInForce").get<int>();
            if (tif == 1) {   // dead-man
                int64_t t = j.at("Time").get<int64_t>();
                if (min_cancel_all_ms_ > 0 && t < now_ + min_cancel_all_ms_) { log_tx(nonce, key, type, 21714); not_consumed_ = !ca_errors_consume_; return; }
                if (!scheduled_.empty() && schedule_mode_ == ScheduleMode::RejectWhileQueued) { log_tx(nonce, key, type, 21712); not_consumed_ = !ca_errors_consume_; return; }
                if (schedule_mode_ == ScheduleMode::Replace) scheduled_.clear();
                scheduled_.push_back({t, type, j});
                log_tx(nonce, key, type, 0);
                return;
            }
            if (tif == 2) { scheduled_.clear(); log_tx(nonce, key, type, 0); return; }   // abort (no-op when none queued)
            delayed_.push_back({now_ + 300, type, j});
        } else if (type == 15 || type == 17) {
            delayed_.push_back({now_ + 300, type, j});   // Standard: 300 ms speed bump on cancel / modify
        } else {
            log_tx(nonce, key, type, 21000);
        }
    }

    void apply_due() {
        std::vector<Delayed> keep_s;
        for (auto& d : scheduled_) {
            if (d.due > now_) { keep_s.push_back(d); continue; }
            cancel_all(cancel_all_market(d.tx), d.tx.at("Nonce").get<int64_t>());
            ++dead_man_fired_;
        }
        scheduled_ = std::move(keep_s);
        std::vector<Delayed> keep;
        for (auto& d : delayed_) {
            if (d.due > now_) { keep.push_back(d); continue; }
            int64_t nonce = d.tx.at("Nonce").get<int64_t>();
            int key = d.tx.at("ApiKeyIndex").get<int>();
            if (d.type == 16) { cancel_all(cancel_all_market(d.tx), nonce); log_tx(nonce, key, d.type, 0); continue; }
            int64_t idx = d.tx.at("Index").get<int64_t>();
            Ord* o = find(idx);
            if (!o || o->status != 2) {
                events_.push_back({FakeEvent::TxFailed, idx, 0, 0, 0, 0, nonce, 0, 21600});
                log_tx(nonce, key, d.type, 21600);
                if (o) if (auto it = deferred_.find(o->coi); it != deferred_.end()) {   // order filled meanwhile
                    for (auto& f : it->second) events_.push_back(f);
                    deferred_.erase(it);
                }
                continue;
            }
            log_tx(nonce, key, d.type, 0);
            if (d.type == 15) {
                emit_cancel(*o, nonce);
            } else {
                o->price = d.tx.at("Price").get<int64_t>();
                o->base = std::max(o->filled, d.tx.at("BaseAmount").get<int64_t>());
                o->updated = now_;
                if (o->tif == 2 && crosses(o->market, o->ask, o->price)) emit_cancel(*o, nonce, 5);
                else events_.push_back({FakeEvent::Modified, o->coi, o->oi, 0, 0, o->price, nonce, 2, 0, o->market, o->ask});
            }
        }
        delayed_ = std::move(keep);
    }

public:
    int dead_man_fired() const { return dead_man_fired_; }

private:
    int64_t acct_, now_;
    std::mt19937 rng_;
    double late_fill_prob_ = 0.0;
    int64_t min_cancel_all_ms_ = 0;
    ScheduleMode schedule_mode_ = ScheduleMode::Stack;
    bool ca_errors_consume_ = true, not_consumed_ = false;
    BatchMode batch_mode_ = BatchMode::WholeBatch;
    std::map<int, int64_t> next_nonce_;
    std::map<int64_t, std::pair<int64_t, int64_t>> book_;
    std::map<int64_t, std::pair<int64_t, __int128>> mins_;
    std::map<int64_t, int64_t> pos_;
    std::map<int64_t, Ord> orders_;
    std::vector<Delayed> delayed_, scheduled_;
    std::map<int64_t, std::vector<FakeEvent>> deferred_;
    std::deque<int64_t> requests_;
    std::vector<FakeEvent> events_;
    std::vector<TradeRecord> trades_;
    std::vector<TxRecord> txlog_;
    std::vector<int64_t> reject_before_;
    int64_t order_seq_ = 0, trade_id_ = 0;
    int accepted_requests_ = 0, late_fills_emitted_ = 0, dead_man_fired_ = 0;
};

}  // namespace lighter::live::fake

namespace lighter::live { namespace test = fake; }   // the gateway tests' spelling (test::FakeExchange)
