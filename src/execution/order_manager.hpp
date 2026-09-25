#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    order_manager.hpp — Position state machine + order lifecycle

    State machine:
        FLAT → PENDING_ENTRY → LONG/SHORT → PENDING_EXIT → FLAT

    Thread safety:
        All state transitions are guarded by state_mu_.
        OrderManager methods may be called from:
          - io_context thread (on_signal, check_trail)
          - fill callback thread (on_fill_notification)
        std::mutex ensures no double-entry races.

    Rithmic ORDER_PLANT integration:
        In dry_run=true mode, orders are logged but not sent.
        In live mode, order_send_cb_ is called with a serialised
        RequestNewOrder protobuf (caller handles the WS send).
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_config.hpp"
#include "orb_strategy.hpp"
#include "latency_logger.hpp"
#include "risk_manager.hpp"
#include "log.hpp"
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

// ─── Position states ──────────────────────────────────────────────────────────
enum class PosState { FLAT, PENDING_ENTRY, LONG, SHORT, PENDING_EXIT };

// ─── Open position record ─────────────────────────────────────────────────────
struct Position {
    PosState  state       = PosState::FLAT;
    OrbSignal direction   = OrbSignal::NONE;  // BUY=LONG, SELL=SHORT

    std::string basket_id_entry;
    std::string basket_id_exit;
    std::string basket_id_stop;   // exchange stop order tracking

    double entry_price    = 0.0;
    double exit_price     = 0.0;
    double sl_price       = 0.0;     // current stop price
    double trigger_price  = 0.0;     // ORB breakout level that triggered the entry order
    double fill_price_actual = 0.0;  // actual execution price from fill notification
    double mfe            = 0.0;     // max favourable excursion in points
    double mae            = 0.0;     // max adverse excursion in points
    int    qty            = 1;

    // Trailing state
    bool   be_triggered    = false;  // SL already moved to entry+offset (no delay)
    double be_sl_price     = 0.0;    // price at which BE stop was placed (entry+trail_be_offset); 0 before BE fires
    bool   trailing_active = false;  // price-following trail active (after trail_delay_secs)
    std::chrono::steady_clock::time_point fill_time;

    // Exit info
    std::string exit_reason;
    double      pnl_points = 0.0;
    double      pnl_usd    = 0.0;
};

// ─── Callback types ───────────────────────────────────────────────────────────
// order_type: 2=MKT, 1=LMT, 4=STOP_MARKET
using OrderSendCallback = std::function<bool(
    const std::string& basket_id,
    const std::string& symbol,
    const std::string& exchange,
    int qty,
    int order_type,
    bool is_buy,
    double price,
    const std::string& user_tag
)>;
using OrderCancelCallback = std::function<void(const std::string& basket_id)>;
// ─── OrderManager ─────────────────────────────────────────────────────────────
class OrderManager {
public:
    explicit OrderManager(const OrbConfig& cfg,
                          RiskManager& risk,
                          LatencyLogger& lat)
        : cfg_(cfg), risk_(risk), lat_(lat)
    {}

    void set_order_callback(OrderSendCallback cb)   { order_cb_  = std::move(cb); }
    void set_cancel_callback(OrderCancelCallback cb) { cancel_cb_ = std::move(cb); }

    // DB persistence for cancelled stops — survive process restarts.
    // persist_cb:           called when a stop cancel is sent (basket_id, was_buy_stop)
    // remove_cb:            called when cancel is confirmed or the stop fires (basket_id)
    // persist_server_id_cb: called when the server basket ID is known at cancel time,
    //                       so startup can cancel by server ID instead of client ID
    using CancelPersistCb         = std::function<void(const std::string& basket_id, bool was_buy_stop)>;
    using CancelRemoveCb          = std::function<void(const std::string& basket_id)>;
    using CancelPersistServerIdCb = std::function<void(const std::string& client_id,
                                                        const std::string& server_id)>;
    void set_cancel_persist_callbacks(CancelPersistCb persist, CancelRemoveCb remove,
                                       CancelPersistServerIdCb persist_server = nullptr) {
        cancel_persist_cb_          = std::move(persist);
        cancel_remove_cb_           = std::move(remove);
        cancel_persist_server_id_cb_ = std::move(persist_server);
    }

    // Re-send cancel for every unconfirmed stop.
    // In-session stops: use server basket ID (required by Rithmic's RequestCancelOrder).
    void recancel_pending_stops() {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!cancel_cb_) return;
        // Build covered set once (O(N)) to avoid O(N²) inner-loop lookups.
        std::unordered_set<std::string> covered;
        for (const auto& [sid, cid] : server_to_client_cancelled_)
            covered.insert(cid);
        std::size_t client_only = 0;
        for (const auto& [cid, _] : cancelled_stops_)
            if (!covered.count(cid)) ++client_only;
        LOG("[OM] RECANCEL: firing — server_entries=%zu client_only_entries=%zu",
            server_to_client_cancelled_.size(), client_only);
        // Primary: cancel by exchange-assigned server basket ID (required by Rithmic).
        for (const auto& [server_id, client_id] : server_to_client_cancelled_) {
            cancel_cb_(server_id);
            LOG("[OM] RECANCEL: cancel re-sent server=%s (client=%s)",
                server_id.c_str(), client_id.c_str());
        }
        // Fallback: startup-seeded entries with no server ID yet — cancel by client basket.
        for (const auto& [client_id, _] : cancelled_stops_) {
            if (!covered.count(client_id)) {
                cancel_cb_(client_id);
                LOG("[OM] RECANCEL: cancel re-sent client=%s (no server ID)", client_id.c_str());
            }
        }
    }

    // Called when the intensive post-close recancel window expires.
    // Clears ONLY last_stop_for_unwind_ (ghost-fill guard for the just-closed trade).
    // server_to_client_cancelled_ is intentionally preserved so the 30s background
    // retry can keep sending cancel requests by server basket ID until ACKs arrive.
    void clear_post_close_recancels() {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!server_to_client_cancelled_.empty())
            LOG("[OM] Intensive window done — %zu server cancel(s) still unconfirmed; "
                "background retry will continue",
                server_to_client_cancelled_.size());
        if (!last_stop_for_unwind_.empty())
            LOG("[OM] Post-close clear: last_stop_for_unwind_=%s cleared",
                last_stop_for_unwind_.c_str());
        last_stop_for_unwind_.clear();
        last_stop_was_buy_ = false;
    }

    // How many server-basket cancel requests are still awaiting ACK from the exchange.
    // Stays non-zero until on_cancel_confirmed / on_cancel_confirmed_by_server_basket
    // erases the entry; used by the executor background retry gate.
    int unconfirmed_server_cancels() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return (int)server_to_client_cancelled_.size();
    }

    // Register an unwind order sent by startup-recon so its fill is recognised
    // as a correction rather than triggering a second GHOST-FILL log.
    void register_unwind_basket(const std::string& basket_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        unwind_baskets_.insert(basket_id);
        LOG("[OM] STARTUP-RECON: registered unwind basket=%s", basket_id.c_str());
    }

    // Called by tid=451 handler when exchange confirms net_qty=0.
    // Clears ghost_halted_ so new entries are re-enabled.
    // Does NOT touch entry_halted_ (exit-rejection halt is independent).
    void confirm_exchange_flat() {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (ghost_halted_) {
            ghost_halted_ = false;
            LOG("[OM] GHOST-HALT CLEARED: tid=451 confirmed exchange FLAT — entries re-enabled");
        }
    }

    // True if any halt (exit-rejection or ghost-fill) is blocking new entries.
    bool is_entry_halted() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return entry_halted_ || ghost_halted_;
    }

    // Called at startup to reload cancelled stops from DB (survive restart).
    void seed_cancelled_stops(const std::string& basket_id, bool was_buy_stop) {
        std::lock_guard<std::mutex> lk(state_mu_);
        cancelled_stops_[basket_id] = was_buy_stop;
        LOG("[OM] STARTUP-RECON: seeded cancelled_stop basket=%s dir=%s",
            basket_id.c_str(), was_buy_stop ? "BUY-stop(SHORT)" : "SELL-stop(LONG)");
    }

    // Seed the server→client reverse map from a DB row that stored both IDs.
    // Called at startup so recancel_pending_stops() can cancel by server basket ID
    // (required by Rithmic — client IDs alone are not accepted by RequestCancelOrder).
    void seed_server_stop_cancel(const std::string& server_id, const std::string& client_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        server_to_client_cancelled_[server_id] = client_id;
        LOG("[OM] STARTUP-RECON: seeded server cancel ID server=%s → client=%s",
            server_id.c_str(), client_id.c_str());
    }

    // A tid=351/352 notification told us the exchange basket id behind one of our
    // stop client ids. Two cases:
    //   • it is the LIVE stop → remember the id for trail cancels (set_stop_server_basket)
    //   • it is a stop we already tried to cancel BY CLIENT ID (server id was not
    //     mapped in time) → that cancel failed at Rithmic and the stop is still
    //     working: record the mapping and re-send the cancel by server id NOW.
    // The executor calls this for every notification that carries both ids.
    void on_stop_server_mapped(const std::string& client_id, const std::string& server_id) {
        if (client_id.empty() || server_id.empty()) return;
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!pos_.basket_id_stop.empty() && client_id == pos_.basket_id_stop) {
            if (stop_server_basket_ != server_id) {
                stop_server_basket_ = server_id;
                // The live stop is tracked in the DB from submit; store its server id too so
                // a restart can cancel it by the id Rithmic routes (not our client id).
                if (cancel_persist_server_id_cb_) cancel_persist_server_id_cb_(client_id, server_id);
                LOG("[OM] Stop server basket_id mapped: client=%s server=%s",
                    client_id.c_str(), server_id.c_str());
            }
            return;
        }
        auto it = cancelled_stops_.find(client_id);
        if (it == cancelled_stops_.end()) return;
        bool already_known = false;
        for (const auto& [sid, cid] : server_to_client_cancelled_)
            if (cid == client_id && sid == server_id) { already_known = true; break; }
        if (already_known) return;
        server_to_client_cancelled_[server_id] = client_id;
        client_only_cancels_.erase(client_id);
        if (cancel_persist_server_id_cb_) cancel_persist_server_id_cb_(client_id, server_id);
        LOG("[OM] LATE-MAP: cancelled stop client=%s now has server=%s — re-sending cancel "
            "by server id (the client-id cancel cannot have been honoured)",
            client_id.c_str(), server_id.c_str());
        if (cancel_cb_) cancel_cb_(server_id);
    }

    // Rithmic answered a cancel with "Cancellation Failed" (tid=351 notify_type=17):
    // the stop is STILL WORKING on the exchange. `id` is whatever the notification
    // carried — our client id (when we cancelled by client id) or the server id.
    //   • still in the trade: re-adopt that stop as the live protective stop. If a
    //     replacement stop was already submitted, cancel it — two working stops for
    //     one position is a double-exit. The trail logic will retry the cancel by
    //     server id once one is mapped.
    //   • already flat: keep the guard; the fill (if it comes) is unwound and the
    //     late server mapping re-sends the cancel.
    void on_cancel_failed(const std::string& id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        std::string client_id = id;
        std::string server_id;
        if (auto s = server_to_client_cancelled_.find(id); s != server_to_client_cancelled_.end()) {
            client_id = s->second; server_id = id;
        }
        auto g = cancelled_stops_.find(client_id);
        if (g == cancelled_stops_.end()) {
            LOG("[OM] CANCEL-FAILED for %s — not a guarded stop, ignoring", id.c_str());
            return;
        }
        // A failure reported against our CLIENT id is only the client-id attempt. If the
        // server id has since mapped, a cancel by server id is already in flight (LATE-MAP
        // or recancel) and is the one that counts — re-adopting here would cancel the
        // replacement while the server-id cancel kills the old stop: no stop at all.
        if (server_id.empty()) {
            for (const auto& [sid, cid] : server_to_client_cancelled_) {
                if (cid == client_id) {
                    LOG("[OM] CANCEL-FAILED for client-id attempt on %s ignored — cancel by server "
                        "id %s is in flight", client_id.c_str(), sid.c_str());
                    return;
                }
            }
        }
        bool in_trade = (pos_.state == PosState::LONG || pos_.state == PosState::SHORT);
        // A stop cancelled for an EARLIER trade (recancels of client-id cancels are re-sent
        // at every close) must never be re-adopted by the current trade: it can point the
        // wrong way after a reversal and would replace the real stop. Keep its guard.
        if (in_trade) {
            auto t = cancelled_stop_trade_.find(client_id);
            if (t == cancelled_stop_trade_.end() || t->second != trade_seq_) {
                LOG("[OM] CRITICAL: CANCEL-FAILED for stop %s from an EARLIER trade — it may still be "
                    "working; guard kept, current stop %s untouched (late mapping re-sends the "
                    "cancel, a fill is unwound by the reconciler)",
                    client_id.c_str(), pos_.basket_id_stop.empty() ? "(none)" : pos_.basket_id_stop.c_str());
                return;
            }
        }
        stop_resubmit_pending_ = false;
        if (!in_trade) {
            LOG("[OM] CRITICAL: CANCEL-FAILED for stop %s while %s — that stop is LIVE on the "
                "exchange with no position behind it; guard kept (fill → unwind, "
                "server mapping → recancel)",
                client_id.c_str(), pos_.state == PosState::FLAT ? "FLAT" : "not in a trade");
            return;
        }
        // Drop the replacement stop, if any, before re-adopting the old one.
        if (!pos_.basket_id_stop.empty() && pos_.basket_id_stop != client_id) {
            LOG("[OM] CANCEL-FAILED: old stop %s still working — cancelling replacement %s "
                "to avoid two working stops",
                client_id.c_str(), pos_.basket_id_stop.c_str());
            cancel_stop_locked();
        }
        double level = 0.0;
        if (auto l = cancelled_stop_levels_.find(client_id); l != cancelled_stop_levels_.end())
            level = l->second;
        cancelled_stops_.erase(g);
        client_only_cancels_.erase(client_id);
        cancelled_stop_levels_.erase(client_id);
        if (last_stop_for_unwind_ == client_id) last_stop_for_unwind_.clear();
        if (!server_id.empty()) server_to_client_cancelled_.erase(server_id);
        if (cancel_remove_cb_) cancel_remove_cb_(client_id);
        pos_.basket_id_stop = client_id;
        stop_server_basket_ = server_id;
        if (level != 0.0) last_exchange_sl_ = level;
        LOG("[OM] CRITICAL: CANCEL-FAILED — re-adopted stop %s (server=%s) as the live "
            "exchange stop at %.2f; in-memory sl=%.2f will be re-applied once the server "
            "id maps",
            client_id.c_str(), server_id.empty() ? "unmapped" : server_id.c_str(),
            last_exchange_sl_, pos_.sl_price);
    }

    // The exchange no longer holds the position we are tracking (manual close in
    // RTrader, broker liquidation). Cancel our protective stop — a stop with no
    // position behind it OPENS a trade when it fires — and record the close so the
    // trade log stays truthful. Never sends an exit: there is nothing to exit.
    void adopt_external_close(double ref_price, const std::string& reason = "external_close") {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT &&
            pos_.state != PosState::PENDING_EXIT) return;
        if (!pos_.basket_id_stop.empty()) cancel_stop_locked();
        double px = ref_price > 0.0 ? ref_price : pos_.entry_price;
        pos_.exit_price  = px;
        pos_.exit_reason = reason;
        double pts = (pos_.direction == OrbSignal::BUY) ? px - pos_.entry_price
                                                        : pos_.entry_price - px;
        pos_.pnl_points = pts;
        pos_.pnl_usd    = pts * cfg_.point_value * pos_.qty - cfg_.commission_rt * pos_.qty;
        LOG("[OM] CRITICAL: position closed EXTERNALLY (%s) — adopting FLAT at ref %.2f "
            "(entry %.2f, %.2fpts); protective stop cancelled",
            reason.c_str(), px, pos_.entry_price, pts);
        completed_pos_   = pos_;
        trade_completed_ = true;
        risk_.on_trade_pnl(pos_.pnl_usd);
        pos_ = Position{};
        server_to_client_orders_.clear();
        prune_processed_fills_locked();   // late duplicate fills must still dedupe (see close path)
    }

    // Does an exchange-reported net quantity agree with what we believe we hold?
    // PENDING states accept both sides of the transition (the fill is in flight).
    bool net_qty_consistent(int exchange_net) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        int held = (pos_.qty > 0 ? pos_.qty : cfg_.qty);
        int dir  = (pos_.direction == OrbSignal::BUY) ? +held
                 : (pos_.direction == OrbSignal::SELL) ? -held : 0;
        switch (pos_.state) {
            case PosState::FLAT:          return exchange_net == 0;
            case PosState::LONG:          return exchange_net == +held;
            case PosState::SHORT:         return exchange_net == -held;
            // An order in flight may be PARTIALLY filled: any net between 0 and the full
            // position, on the position's side, is a legitimate intermediate state (a partial
            // entry must not be unwound while the rest of the entry is still working).
            case PosState::PENDING_ENTRY:
            case PosState::PENDING_EXIT:
                return dir >= 0 ? (exchange_net >= 0 && exchange_net <= dir)
                                : (exchange_net <= 0 && exchange_net >= dir);
        }
        return exchange_net == 0;
    }

    // ── Called by OrbStrategy signal callback ─────────────────────────────────
    // desired_sl_dist: 0.0 (default, ORB/Trend) = use cfg_.sl_points at fill time via
    // compute_sl(). > 0.0 (mtf_scalper) = the strategy's own stop DISTANCE at signal
    // time (BracketSpec::r_unit) — the entry stop is placed at fill_price ∓ this
    // distance instead of cfg_.sl_points, then check_external_stop() ratchets it
    // bar by bar from the strategy's own cur_stop(). A distance, not an absolute
    // price, because the real fill can land a few ticks from the signal price.
    void on_signal(OrbSignal sig, double price, const std::string& reason,
                   double orb_boundary = 0.0, double desired_sl_dist = 0.0) {
        if (sig == OrbSignal::FLATTEN_EOD) {
            flatten_now("eod_flatten", price);
            return;
        }
        if (sig != OrbSignal::BUY && sig != OrbSignal::SELL) return;

        if (entry_halted_ || ghost_halted_) {
            LOG("[OM] Signal rejected — entries halted (%s)",
                entry_halted_ ? "exit-rejection halt" : "ghost-fill halt");
            return;
        }

        // Risk check
        std::string halt_reason;
        if (!risk_.can_trade(halt_reason)) {
            LOG("[OM] Signal rejected by risk: %s", halt_reason.c_str());
            return;
        }

        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::FLAT) {
            LOG("[OM] Signal ignored — not FLAT (state=%d)", (int)pos_.state);
            return;
        }

        bool is_buy = (sig == OrbSignal::BUY);
        std::string basket = new_basket_id();

        LOG("[OM] %s Entry signal: price=%.2f basket=%s reason=%s%s",
            is_buy ? "LONG" : "SHORT", price, basket.c_str(), reason.c_str(),
            cfg_.dry_run ? " [DRY_RUN]" : "");

        lat_.on_signal(basket, price, /*is_entry=*/true, orb_boundary);

        pos_ = Position{};
        ++trade_seq_;
        pos_.state         = PosState::PENDING_ENTRY;
        pos_.direction     = sig;
        pos_.basket_id_entry = basket;
        pos_.qty           = cfg_.qty;
        pos_.trigger_price = price;   // ORB breakout level at time of order submission
        pos_.fill_time     = std::chrono::steady_clock::now(); // placeholder until fill
        pending_entry_sl_dist_ = desired_sl_dist;

        send_market_order(basket, is_buy, price, "entry");
    }

    // ── Fill notification from ORDER_PLANT ────────────────────────────────────
    // Called with state_mu_ already held (dry-run path inside send_market_order)
    void on_fill_notification_locked(const std::string& basket_id,
                                     double fill_price,
                                     int fill_qty,
                                     bool is_entry_fill) {

        if (is_entry_fill) {
            if (pos_.state != PosState::PENDING_ENTRY) {
                // Late fill after EOD cancel: the cancel raced and entry filled anyway.
                // Exchange has an open position; fire an immediate unwind.
                if (!pending_cancel_basket_.empty() && basket_id == pending_cancel_basket_) {
                    bool unwind_is_buy = !pending_cancel_was_buy_; // reverse the entry direction
                    LOG("[OM] CRITICAL: Late fill after EOD cancel basket=%s px=%.2f — "
                        "unwind %s immediately",
                        basket_id.c_str(), fill_price, unwind_is_buy ? "BUY" : "SELL");
                    pending_cancel_basket_.clear();
                    if (order_cb_) {
                        std::string basket = new_basket_id();
                        lat_.on_signal(basket, fill_price, false);
                        constexpr double TICK = 0.25;
                        constexpr int    OFFSET_TICKS = 4;
                        double unwind_px = unwind_is_buy
                            ? fill_price + OFFSET_TICKS * TICK
                            : fill_price - OFFSET_TICKS * TICK;
                        bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                                            cfg_.qty, /*LIMIT=1*/1, unwind_is_buy,
                                            unwind_px, "eod_cancel_race_unwind");
                        if (ok) lat_.on_submit(basket, fill_price);
                    }
                } else {
                    LOG("[OM] Spurious entry fill basket=%s (state=%d)",
                        basket_id.c_str(), (int)pos_.state);
                }
                return;
            }
            if (pos_.basket_id_entry != basket_id) {
                LOG("[OM] Entry fill basket mismatch: got=%s expected=%s",
                    basket_id.c_str(), pos_.basket_id_entry.c_str());
                return;
            }
            // Partial fill (fill_qty = CUMULATIVE quantity so far, < qty): the order is
            // still working. Do not change state — the tid=351 COMPLETE notification
            // carries the full cumulative quantity and completes the transition (the
            // executor's fill dedupe lets a higher cumulative through). Treating a partial
            // as full would place a stop for contracts we do not hold. If the COMPLETE
            // never arrived, the tid=451 NetReconciler resolves the mismatch.
            if (fill_qty > 0 && fill_qty < pos_.qty) {
                LOG("[OM] PARTIAL ENTRY fill basket=%s cumulative=%d/%d px=%.2f — waiting for "
                    "the complete fill", basket_id.c_str(), fill_qty, pos_.qty, fill_price);
                return;
            }

            pos_.entry_price       = fill_price;
            pos_.fill_price_actual = fill_price;  // actual fill for slippage calc
            pos_.fill_time         = std::chrono::steady_clock::now();
            pos_.state = (pos_.direction == OrbSignal::BUY)
                         ? PosState::LONG : PosState::SHORT;

            // Clear EOD-cancel race guard — the entry filled normally, no late-fill expected.
            pending_cancel_basket_.clear();
            pending_cancel_was_buy_ = false;

            // Place stop-loss: the strategy's own distance (mtf_scalper) if it supplied
            // one at signal time, else the config's flat sl_points (ORB/Trend).
            double sl = (pending_entry_sl_dist_ > 0.0)
                ? compute_sl_dist(fill_price, pos_.direction, pending_entry_sl_dist_)
                : compute_sl(fill_price, pos_.direction);
            pending_entry_sl_dist_ = 0.0;
            pos_.sl_price = sl;

            auto lat_rec = lat_.on_fill(basket_id, fill_price);
            LOG("[OM] FILL entry: basket=%s price=%.2f sl=%.2f slippage=%dtick ($%.2f)",
                basket_id.c_str(), fill_price, sl,
                lat_rec.slippage_ticks, lat_rec.slippage_usd);

            last_entry_lat_ = lat_rec;

            // Submit exchange-level stop order immediately after fill
            submit_stop_order_locked(sl);

        } else {
            // Exit fill (market exit or stop fill)
            if (pos_.state != PosState::PENDING_EXIT &&
                pos_.state != PosState::LONG &&
                pos_.state != PosState::SHORT) {
                // State is FLAT (or some unexpected state) — run stale-stop guards
                LOG("[OM] FLAT fill: basket=%s px=%.2f qty=%d — checking stale-stop guards "
                    "(last_stop_for_unwind=%s cancelled_stops=%zu)",
                    basket_id.c_str(), fill_price, fill_qty,
                    last_stop_for_unwind_.empty() ? "(none)" : last_stop_for_unwind_.c_str(),
                    cancelled_stops_.size());
                // Check for stale stop fill: cancel raced and stop fired anyway
                if (!last_stop_for_unwind_.empty() && basket_id == last_stop_for_unwind_) {
                    // last_stop_for_unwind_ guard MATCHED
                    bool unwind_is_buy = !last_stop_was_buy_;
                    LOG("[OM] STALE-STOP-FILL: basket=%s px=%.2f state=FLAT "
                        "— late fire of most-recent cancelled stop, sending unwind %s",
                        basket_id.c_str(), fill_price, unwind_is_buy ? "BUY" : "SELL");
                    last_stop_for_unwind_.clear();
                    cancelled_stops_.erase(basket_id);  // remove from guard too
                    erase_server_cancel_for_client_locked(basket_id);
                    if (cancel_remove_cb_) cancel_remove_cb_(basket_id);
                    if (order_cb_) {
                        std::string basket = new_basket_id();
                        lat_.on_signal(basket, fill_price, false);
                        constexpr double TICK = 0.25;
                        constexpr int    OFFSET_TICKS = 4;
                        double unwind_px = unwind_is_buy
                            ? fill_price + OFFSET_TICKS * TICK
                            : fill_price - OFFSET_TICKS * TICK;
                        LOG("[OM] STALE-STOP-UNWIND: sending %s limit=%.2f basket=%s "
                            "(ghost position from late stop fire)",
                            unwind_is_buy ? "BUY" : "SELL", unwind_px, basket.c_str());
                        bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                                            cfg_.qty, /*LIMIT=1*/1, unwind_is_buy,
                                            unwind_px, "stale_stop_unwind");
                        if (ok) {
                            lat_.on_submit(basket, fill_price);
                            unwind_baskets_.insert(basket);
                        } else LOG("[OM] STALE-STOP-UNWIND: ERROR — order_cb_ failed basket=%s "
                                 "EXCHANGE MAY BE NON-FLAT", basket.c_str());
                    } else {
                        LOG("[OM] STALE-STOP-UNWIND: ERROR — no order_cb_, cannot unwind "
                            "EXCHANGE MAY BE NON-FLAT");
                    }
                } else {
                    // last_stop_for_unwind_ guard did NOT match — try cancelled_stops_
                    LOG("[OM] FLAT fill: last_stop_for_unwind_ guard miss "
                        "(basket=%s != last_unwind=%s) — checking cancelled_stops_",
                        basket_id.c_str(),
                        last_stop_for_unwind_.empty() ? "(none)" : last_stop_for_unwind_.c_str());
                    auto it = cancelled_stops_.find(basket_id);
                    if (it != cancelled_stops_.end()) {
                        // cancelled_stops_ guard MATCHED
                        // A stop we cancelled fired anyway — exchange didn't cancel in time.
                        bool unwind_is_buy = !it->second;
                        cancelled_stops_.erase(it);
                        erase_server_cancel_for_client_locked(basket_id);
                        if (cancel_remove_cb_) cancel_remove_cb_(basket_id);
                        LOG("[OM] CANCELLED-STOP-FIRED: basket=%s px=%.2f state=FLAT "
                            "— sending unwind %s (pending_cancelled now=%zu)",
                            basket_id.c_str(), fill_price,
                            unwind_is_buy ? "BUY" : "SELL",
                            cancelled_stops_.size());
                        if (order_cb_) {
                            std::string basket = new_basket_id();
                            lat_.on_signal(basket, fill_price, false);
                            constexpr double TICK = 0.25;
                            constexpr int    OFFSET_TICKS = 4;
                            double unwind_px = unwind_is_buy
                                ? fill_price + OFFSET_TICKS * TICK
                                : fill_price - OFFSET_TICKS * TICK;
                            LOG("[OM] CANCELLED-STOP-UNWIND: sending %s limit=%.2f basket=%s",
                                unwind_is_buy ? "BUY" : "SELL", unwind_px, basket.c_str());
                            bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                                                cfg_.qty, /*LIMIT=1*/1, unwind_is_buy,
                                                unwind_px, "stale_cancel_unwind");
                            if (ok) {
                                lat_.on_submit(basket, fill_price);
                                unwind_baskets_.insert(basket);
                            } else LOG("[OM] CANCELLED-STOP-UNWIND: ERROR — order_cb_ failed "
                                     "EXCHANGE MAY BE NON-FLAT");
                        } else {
                            LOG("[OM] CANCELLED-STOP-UNWIND: ERROR — no order_cb_ "
                                "EXCHANGE MAY BE NON-FLAT");
                        }
                    } else if (unwind_baskets_.erase(basket_id) > 0) {
                        LOG("[OM] UNWIND-FILL: basket=%s px=%.2f — ghost position corrected, exchange now FLAT",
                            basket_id.c_str(), fill_price);
                        ghost_halted_ = false;  // unwind confirmed flat — entries re-enabled
                    } else if (ghost_halted_) {
                        // Already halted from a prior ghost fill. This is likely a manual
                        // close via RTrader or a startup-recon unwind we didn't register.
                        // Don't escalate — stay halted until tid=451 confirms flat.
                        LOG("[OM] MANUAL-CLOSE-DETECTED: basket=%s px=%.2f qty=%d "
                            "— unknown fill while ghost-halted, likely manual RTrader "
                            "close. Waiting for tid=451 position confirm.",
                            basket_id.c_str(), fill_price, fill_qty);
                    } else {
                        // Completely unknown fill while FLAT — this is a ghost position.
                        // DB-seeded cancelled_stops should have caught this; if we're here
                        // it means a stop survived across multiple restarts without being
                        // persisted. Halt trading and require manual intervention.
                        // Warn if fill_qty is suspiciously large (could be a Rithmic internal
                        // notification or a fill for a different account's position).
                        if (fill_qty > cfg_.qty) {
                            LOG("[OM] GHOST-FILL WARN: fill_qty=%d > expected qty=%d "
                                "— possible Rithmic internal notification or wrong-account fill "
                                "(basket=%s px=%.2f)",
                                fill_qty, cfg_.qty, basket_id.c_str(), fill_price);
                        }
                        // Dump all known basket IDs to aid debugging
                        LOG("[OM] GHOST-FILL: basket=%s px=%.2f qty=%d state=FLAT "
                            "— unknown fill, exchange may be non-flat. "
                            "TRADING HALTED — check RTrader and restart executor.",
                            basket_id.c_str(), fill_price, fill_qty);
                        LOG("[OM] GHOST-FILL known baskets: entry=%s exit=%s stop=%s "
                            "server_stop=%s last_unwind=%s",
                            pos_.basket_id_entry.empty() ? "(none)" : pos_.basket_id_entry.c_str(),
                            pos_.basket_id_exit.empty()  ? "(none)" : pos_.basket_id_exit.c_str(),
                            pos_.basket_id_stop.empty()  ? "(none)" : pos_.basket_id_stop.c_str(),
                            stop_server_basket_.empty()  ? "(none)" : stop_server_basket_.c_str(),
                            last_stop_for_unwind_.empty() ? "(none)" : last_stop_for_unwind_.c_str());
                        if (!cancelled_stops_.empty()) {
                            for (const auto& [cid, buy_stop] : cancelled_stops_)
                                LOG("[OM] GHOST-FILL cancelled_stops: %s (was_buy=%d)",
                                    cid.c_str(), (int)buy_stop);
                        }
                        if (!unwind_baskets_.empty()) {
                            for (const auto& ub : unwind_baskets_)
                                LOG("[OM] GHOST-FILL unwind_baskets: %s", ub.c_str());
                        }
                        ghost_halted_ = true;
                    }
                }
                return;
            }

            // Partial exit fill (cumulative < qty): the stop/exit order still has contracts
            // working and we still hold the rest — going FLAT here left the remainder of a
            // stop working on the exchange with nothing behind it. Wait for the COMPLETE
            // fill; state (and the trail/stop logic) stays as it is.
            if (fill_qty > 0 && fill_qty < pos_.qty) {
                LOG("[OM] PARTIAL EXIT fill basket=%s cumulative=%d/%d px=%.2f — waiting for "
                    "the complete fill", basket_id.c_str(), fill_qty, pos_.qty, fill_price);
                return;
            }

            // A replaced stop of this trade filled before its cancel landed: it is the exit.
            // Drop its guard now — its cancel will come back "Cancellation Failed"/filled
            // and must not keep a false "stop still LIVE" guard afterwards.
            if (cancelled_stops_.erase(basket_id) > 0) {
                client_only_cancels_.erase(basket_id);
                cancelled_stop_levels_.erase(basket_id);
                cancelled_stop_trade_.erase(basket_id);
                for (auto it = server_to_client_cancelled_.begin(); it != server_to_client_cancelled_.end();)
                    it = (it->second == basket_id) ? server_to_client_cancelled_.erase(it) : std::next(it);
                if (last_stop_for_unwind_ == basket_id) last_stop_for_unwind_.clear();
                if (cancel_remove_cb_) cancel_remove_cb_(basket_id);
                LOG("[OM] replaced stop %s filled before its cancel — treating it as the exit",
                    basket_id.c_str());
            }
            // Exchange stop filled directly (state still LONG/SHORT) — set exit reason
            if (pos_.state == PosState::LONG || pos_.state == PosState::SHORT) {
                pos_.exit_reason = (basket_id == pos_.basket_id_stop)
                    ? "exchange_stop" : "unknown_exit";
                pos_.state = PosState::PENDING_EXIT;
            }

            pos_.exit_price  = fill_price;
            double pts = (pos_.direction == OrbSignal::BUY)
                         ? fill_price - pos_.entry_price
                         : pos_.entry_price - fill_price;
            pos_.pnl_points = pts;
            pos_.pnl_usd    = pts * cfg_.point_value * pos_.qty
                              - cfg_.commission_rt * pos_.qty; // round-turn commission

            // Cancel the exchange stop order if it wasn't the one that just filled;
            // if it WAS the one that filled (natural stop fire), remove it from the
            // active-stop DB so startup doesn't try to re-cancel an already-filled order.
            if (!pos_.basket_id_stop.empty()) {
                if (pos_.basket_id_stop != basket_id)
                    cancel_stop_locked();
                else if (cancel_remove_cb_)
                    cancel_remove_cb_(basket_id);
            }

            auto lat_rec = lat_.on_fill(basket_id, fill_price);
            LOG("[OM] FILL exit: basket=%s price=%.2f pnl=%.2fpts ($%.2f) slippage=%dtick",
                basket_id.c_str(), fill_price, pts, pos_.pnl_usd,
                lat_rec.slippage_ticks);

            // If this basket was in the cancelled-stop guard (fired before cancel reached
            // exchange, or was a cross-session stale stop), clean it up now — otherwise it
            // accumulates in pending_stop_cancels DB across restarts and fires again later.
            {
                auto it = cancelled_stops_.find(basket_id);
                if (it != cancelled_stops_.end()) {
                    cancelled_stops_.erase(it);
                    if (cancel_remove_cb_) cancel_remove_cb_(basket_id);
                    erase_server_cancel_for_client_locked(basket_id);
                    LOG("[OM] Cleaned stale cancelled_stop basket=%s on exit fill "
                        "(pending_cancelled now=%zu)", basket_id.c_str(), cancelled_stops_.size());
                }
            }

            last_exit_lat_ = lat_rec;
            completed_pos_ = pos_;
            completed_pos_.exit_price = fill_price;

            risk_.on_trade_pnl(pos_.pnl_usd);

            rejected_exit_count_ = 0;  // successful close — reset rejection counter
            entry_halted_        = false;
            last_exchange_sl_    = 0.0;
            sl_breach_time_      = {};  // clear breach timer on position close
            stop_resubmit_pending_ = false;

            pos_ = Position{};  // back to FLAT
            trade_completed_ = true;
            // Reject-correlation state is scoped to the trade's orders — drop it at close.
            // Fill-dedupe state is NOT: the same exit fill is reported twice (tid=352
            // per-fill, then the tid=351 COMPLETE with the cumulative qty) and the second
            // copy often lands AFTER this close. With the dedupe cleared it looked like an
            // unowned fill on our account → ghost-halt, which stopped the 2026-09-24 01:15
            // ORB test for the rest of its session. Baskets are unique per order, so keep
            // the record (bounded) across trades.
            server_to_client_orders_.clear();
            prune_processed_fills_locked();
            // last_stop_for_unwind_ intentionally NOT cleared here.
            // It persists until clear_post_close_recancels() (5s window) so that if
            // the just-cancelled stop fires late it is recognised as STALE-STOP-FILL
            // rather than triggering GHOST-FILL halt or being silently attributed to
            // the next trade as a spurious exit.  Cleared by clear_post_close_recancels().

            // Purge trail-cancel guards whose cancel was already confirmed (not in
            // server_to_client_cancelled_).  Guards still awaiting a cancel ACK are
            // kept in DB — on_cancel_confirmed / on_cancel_confirmed_by_server_basket
            // will delete them when the ACK arrives.  If the ACK never comes within
            // the 5s recancel window, clear_post_close_recancels() logs WARN and the
            // DB rows survive for next-startup retry via server basket ID.
            // NOTE: server_to_client_cancelled_ and last_stop_for_unwind_ are NOT
            // cleared here — they persist until clear_post_close_recancels().
            if (!cancelled_stops_.empty()) {
                size_t confirmed_cnt = 0, pending_cnt = 0;
                std::unordered_map<std::string, bool> keep;
                for (const auto& [bid, was_buy] : cancelled_stops_) {
                    bool has_pending_server_cancel = false;
                    for (const auto& [sid, cid] : server_to_client_cancelled_)
                        if (cid == bid) { has_pending_server_cancel = true; break; }
                    // A cancel sent by client id was never acknowledged either — the
                    // absence of a server mapping is NOT confirmation. Keep it live in
                    // memory AND in the DB until an ACK, a fill (→ unwind) or a late
                    // server mapping (→ recancel) resolves it.
                    if (has_pending_server_cancel || client_only_cancels_.count(bid)) {
                        ++pending_cnt;
                        keep.emplace(bid, was_buy);
                    } else {
                        ++confirmed_cnt;
                        if (cancel_remove_cb_) cancel_remove_cb_(bid);
                    }
                }
                LOG("[OM] FLAT — purged %zu confirmed trail-cancel guard(s) from DB; "
                    "kept %zu unconfirmed (server-id cancels awaiting ACK + client-id cancels "
                    "awaiting a server mapping) — any of them firing is unwound",
                    confirmed_cnt, pending_cnt);
                cancelled_stops_.swap(keep);
                // server_to_client_cancelled_ intentionally NOT cleared here
            }
            if (!unwind_baskets_.empty()) {
                LOG("[OM] FLAT — discarding %zu unconfirmed unwind basket(s) "
                    "(unwinds that never filled — exchange confirmed flat via exit)",
                    unwind_baskets_.size());
                unwind_baskets_.clear();
            }
        }
    }

    // Public entry point — acquires lock then delegates to _locked variant
    void on_fill_notification(const std::string& basket_id,
                               double fill_price,
                               int fill_qty,
                               bool is_entry_fill) {
        std::lock_guard<std::mutex> lk(state_mu_);
        on_fill_notification_locked(basket_id, fill_price, fill_qty, is_entry_fill);
    }

    // ── Periodic check: trailing stop and SL hit (call every tick or 1s) ─────
    // Returns true if SL price changed (BE or trail move) — caller should flush DB.
    // The stop actually WORKING at the exchange (moves only in ≥ trail_step jumps —
    // see update_stop_order_locked); pos_.sl_price is the in-memory/display value.
    double exchange_stop() const { std::lock_guard<std::mutex> lk(state_mu_); return last_exchange_sl_ != 0.0 ? last_exchange_sl_ : pos_.sl_price; }

    bool check_trail_and_stop(double current_price) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT) return false;

        bool is_long = (pos_.state == PosState::LONG);
        double mfe_now = is_long
            ? current_price - pos_.entry_price
            : pos_.entry_price - current_price;
        double mae_now = is_long
            ? pos_.entry_price - current_price
            : current_price - pos_.entry_price;

        if (mfe_now > pos_.mfe) pos_.mfe = mfe_now;
        if (mae_now > pos_.mae) pos_.mae = mae_now;

        // Software SL: two-tier safety net.
        // Tier 1 — immediate: no exchange stop basket (rejected or not yet submitted).
        // Tier 2 — timeout: exchange stop submitted but hasn't fired after sl_fire_timeout_ms.
        //   Catches silent stop failures and cancel+resubmit race windows.
        //   initiate_exit_locked() cancels the exchange stop first to minimise double-fill risk.
        bool sl_moved = false;

        // When an exchange stop is active, use last_exchange_sl_ for breach detection.
        // pos_.sl_price can diverge from last_exchange_sl_ due to trail suppression:
        // the in-memory SL updates on every tick but the exchange stop only moves when
        // |delta| >= trail_step.  Using pos_.sl_price causes spurious software-SL
        // timeouts when price bounces back through the in-memory SL but has NOT yet
        // reached the actual exchange stop level.  Use pos_.sl_price only when no
        // exchange stop is active (tier-1 software-SL fallback).
        double effective_sl = (!pos_.basket_id_stop.empty() && last_exchange_sl_ != 0.0)
            ? last_exchange_sl_ : pos_.sl_price;
        bool sl_breached = (is_long  && current_price <= effective_sl) ||
                           (!is_long && current_price >= effective_sl);

        LOG("[OM] TRAIL-CHECK: %s price=%.2f sl=%.2f exch_sl=%.2f mfe=%.2f mae=%.2f "
            "be_triggered=%d trailing=%d sl_breached=%d stop=%s",
            is_long ? "LONG" : "SHORT", current_price, pos_.sl_price, effective_sl,
            pos_.mfe, pos_.mae,
            (int)pos_.be_triggered, (int)pos_.trailing_active, (int)sl_breached,
            pos_.basket_id_stop.empty() ? "(none)" : pos_.basket_id_stop.c_str());

        if (sl_breached) {
            if (pos_.basket_id_stop.empty()) {
                LOG("[OM] Software SL hit (%s, no exchange stop): price=%.2f sl=%.2f",
                    is_long ? "LONG" : "SHORT", current_price, pos_.sl_price);
                sl_breach_time_ = {};
                initiate_exit_locked("stop_loss", current_price);
                return false;
            }
            // Stop cancel+resubmit in progress: new stop may not have reached the exchange yet.
            // Fire software SL immediately rather than risk a delayed fill at a far worse price
            // when the new stop arrives at the exchange after price has already moved.
            if (stop_resubmit_pending_) {
                stop_resubmit_pending_ = false;
                sl_breach_time_ = {};
                LOG("[OM] SL breach in stop-resubmit window (new stop in-flight): "
                    "price=%.2f sl=%.2f — firing immediate software SL",
                    current_price, pos_.sl_price);
                initiate_exit_locked("stop_loss_resubmit", current_price);
                return false;
            }
            // Exchange stop active: start or check breach timer.
            if (sl_breach_time_ == std::chrono::steady_clock::time_point{}) {
                sl_breach_time_ = std::chrono::steady_clock::now();
            } else {
                auto breach_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sl_breach_time_).count();
                if (breach_ms >= (int64_t)cfg_.sl_fire_timeout_ms) {
                    LOG("[OM] Software SL timeout (%s, exchange stop unresponsive %ldms): "
                        "price=%.2f sl=%.2f basket=%s",
                        is_long ? "LONG" : "SHORT", (long)breach_ms,
                        current_price, pos_.sl_price, pos_.basket_id_stop.c_str());
                    sl_breach_time_ = {};
                    initiate_exit_locked("stop_loss_timeout", current_price);
                    return false;
                }
            }
        } else {
            sl_breach_time_ = {};
            stop_resubmit_pending_ = false;  // price above new stop — resubmit window safe
        }

        // BE: immediate — no delay. Fires as soon as MFE passes trail_be_trigger.
        if (!pos_.be_triggered && pos_.mfe >= cfg_.trail_be_trigger) {
            pos_.be_triggered = true;
            double be_sl = is_long
                ? pos_.entry_price + cfg_.trail_be_offset
                : pos_.entry_price - cfg_.trail_be_offset;
            LOG("[OM] BE eval: price=%.2f entry=%.2f trigger=%.2f mfe=%.2f "
                "be_triggered=%d be_sl=%.2f current_sl=%.2f",
                current_price, pos_.entry_price, cfg_.trail_be_trigger, pos_.mfe,
                (int)pos_.be_triggered, be_sl, pos_.sl_price);
            if ((is_long && be_sl > pos_.sl_price) ||
                (!is_long && be_sl < pos_.sl_price)) {
                double old_sl = pos_.sl_price;
                pos_.sl_price = be_sl;
                pos_.be_sl_price = be_sl;
                sl_moved = true;
                LOG("[OM] BE triggered — SL moved %.2f → %.2f (entry+%.1fpt)",
                    old_sl, be_sl, cfg_.trail_be_offset);
                update_stop_order_locked(old_sl, be_sl);
            } else {
                LOG("[OM] BE triggered but be_sl=%.2f does not improve current sl=%.2f — no update",
                    be_sl, pos_.sl_price);
            }
        }

        // Trail: activates after trail_delay_secs (independent of BE).
        if (!pos_.trailing_active) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - pos_.fill_time).count();
            LOG("[OM] TRAIL-DELAY eval: elapsed=%lds delay=%ds mfe=%.2f trigger=%.2f "
                "trailing_active=%d",
                (long)elapsed, cfg_.trail_delay_secs, pos_.mfe, cfg_.trail_be_trigger,
                (int)pos_.trailing_active);
            if (elapsed >= cfg_.trail_delay_secs && pos_.mfe >= cfg_.trail_be_trigger) {
                pos_.trailing_active = true;
                LOG("[OM] Trailing activated after %lds (delay=%ds mfe=%.2f)",
                    (long)elapsed, cfg_.trail_delay_secs, pos_.mfe);
            }
        }

        // Update trailing stop
        if (pos_.trailing_active) {
            double trail_sl = is_long
                ? current_price - cfg_.trail_step
                : current_price + cfg_.trail_step;

            if ((is_long && trail_sl > pos_.sl_price) ||
                (!is_long && trail_sl < pos_.sl_price)) {
                double old_sl = pos_.sl_price;
                pos_.sl_price = trail_sl;
                sl_moved = true;
                LOG("[OM] Trail updated: price=%.2f old_sl=%.2f new_sl=%.2f step=%.2f",
                    current_price, old_sl, trail_sl, cfg_.trail_step);
                update_stop_order_locked(old_sl, trail_sl);
            }
        }

        return sl_moved;
    }

    // mtf_scalper's per-tick bracket check. The strategy computes its OWN ratcheting
    // stop (cur_stop()); the host pushes it here every tick instead of deriving a new
    // stop from cfg_.trail_be_trigger/trail_step (that math is check_trail_and_stop's,
    // ORB/Trend only — untouched by this method). strat_stop is an ABSOLUTE price,
    // NaN/<=0 = "no update this tick" (mirrors PaperBracketBroker::update_bracket).
    //
    // Deliberately a SEPARATE method rather than a refactor of check_trail_and_stop:
    // that function carries the tier-1/tier-2 software-SL breach detection hardened by
    // the 2026-09-23 orphaned-stop incident (see project notes), and this keeps that
    // code path for ORB/Trend completely unmodified. The breach-detection block below
    // is intentionally a near-duplicate of check_trail_and_stop's, so a future change
    // to one may need the same change in the other — grep for "TRAIL-CHECK"/"MTF-CHECK".
    bool check_external_stop(double current_price, double strat_stop) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT) return false;

        bool is_long = (pos_.state == PosState::LONG);
        double mfe_now = is_long
            ? current_price - pos_.entry_price
            : pos_.entry_price - current_price;
        double mae_now = is_long
            ? pos_.entry_price - current_price
            : current_price - pos_.entry_price;
        if (mfe_now > pos_.mfe) pos_.mfe = mfe_now;
        if (mae_now > pos_.mae) pos_.mae = mae_now;

        double effective_sl = (!pos_.basket_id_stop.empty() && last_exchange_sl_ != 0.0)
            ? last_exchange_sl_ : pos_.sl_price;
        bool sl_breached = (is_long  && current_price <= effective_sl) ||
                           (!is_long && current_price >= effective_sl);

        LOG("[OM] MTF-CHECK: %s price=%.2f sl=%.2f exch_sl=%.2f strat_stop=%.2f "
            "mfe=%.2f mae=%.2f sl_breached=%d stop=%s",
            is_long ? "LONG" : "SHORT", current_price, pos_.sl_price, effective_sl,
            strat_stop, pos_.mfe, pos_.mae, (int)sl_breached,
            pos_.basket_id_stop.empty() ? "(none)" : pos_.basket_id_stop.c_str());

        if (sl_breached) {
            if (pos_.basket_id_stop.empty()) {
                LOG("[OM] Software SL hit (%s, no exchange stop): price=%.2f sl=%.2f",
                    is_long ? "LONG" : "SHORT", current_price, pos_.sl_price);
                sl_breach_time_ = {};
                initiate_exit_locked("stop_loss", current_price);
                return false;
            }
            if (stop_resubmit_pending_) {
                stop_resubmit_pending_ = false;
                sl_breach_time_ = {};
                LOG("[OM] SL breach in stop-resubmit window (new stop in-flight): "
                    "price=%.2f sl=%.2f — firing immediate software SL",
                    current_price, pos_.sl_price);
                initiate_exit_locked("stop_loss_resubmit", current_price);
                return false;
            }
            if (sl_breach_time_ == std::chrono::steady_clock::time_point{}) {
                sl_breach_time_ = std::chrono::steady_clock::now();
            } else {
                auto breach_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sl_breach_time_).count();
                if (breach_ms >= (int64_t)cfg_.sl_fire_timeout_ms) {
                    LOG("[OM] Software SL timeout (%s, exchange stop unresponsive %ldms): "
                        "price=%.2f sl=%.2f basket=%s",
                        is_long ? "LONG" : "SHORT", (long)breach_ms,
                        current_price, pos_.sl_price, pos_.basket_id_stop.c_str());
                    sl_breach_time_ = {};
                    initiate_exit_locked("stop_loss_timeout", current_price);
                    return false;
                }
            }
        } else {
            sl_breach_time_ = {};
            stop_resubmit_pending_ = false;
        }

        // Adopt the strategy's stop only if it TIGHTENS (never loosens — matches
        // PaperBracketBroker::update_bracket and mtf_scalper_strategy.hpp's own
        // "never-retreat" contract for cur_stop()).
        bool sl_moved = false;
        if (!std::isnan(strat_stop) && strat_stop > 0.0) {
            strat_stop = snap_stop(strat_stop, is_long ? OrbSignal::BUY : OrbSignal::SELL);
            bool improves = is_long ? (strat_stop > pos_.sl_price) : (strat_stop < pos_.sl_price);
            if (improves) {
                double old_sl = pos_.sl_price;
                pos_.sl_price = strat_stop;
                sl_moved = true;
                LOG("[OM] MTF stop update: price=%.2f old_sl=%.2f new_sl=%.2f",
                    current_price, old_sl, strat_stop);
                update_stop_order_locked(old_sl, strat_stop);
            }
        }
        return sl_moved;
    }

    // ── Force flatten (EOD or kill switch) ────────────────────────────────────
    void flatten_now(const std::string& reason, double price = 0.0) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state == PosState::FLAT) {
            LOG("[OM] flatten_now('%s') — already flat", reason.c_str());
            return;
        }
        if (pos_.state == PosState::PENDING_ENTRY) {
            LOG("[OM] flatten_now('%s') — cancelling pending entry basket=%s",
                reason.c_str(), pos_.basket_id_entry.c_str());
            // Save cancel context before resetting: if the fill races the cancel and
            // arrives after pos_ is cleared, the spurious-fill handler will unwind it.
            pending_cancel_basket_  = pos_.basket_id_entry;
            pending_cancel_was_buy_ = (pos_.direction == OrbSignal::BUY);
            cancel_order_locked(pos_.basket_id_entry, "pending entry");
            pos_ = Position{};
            return;
        }
        if (pos_.state == PosState::PENDING_EXIT) {
            LOG("[OM] flatten_now('%s') — exit already pending", reason.c_str());
            return;
        }
        initiate_exit_locked(reason, price);
    }

    // ── Re-drive a stuck PENDING_EXIT ─────────────────────────────────────────
    // The protective stop is cancelled before the exit limit is submitted, so an
    // exit limit that never fills leaves a naked position parked in PENDING_EXIT
    // with the software SL disabled. Called by the executor watchdog: cancel the
    // resting exit order and submit a fresh aggressive limit at the current price.
    // The old exit basket goes into cancelled_stops_ so a late fill after FLAT is
    // unwound like a cancelled stop firing late.
    void retry_stuck_exit(double current_price) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::PENDING_EXIT) return;
        if (cfg_.dry_run) return;  // dry-run exits fill synchronously — never stuck
        bool exit_was_buy = (pos_.direction == OrbSignal::SELL);
        std::string old_basket = pos_.basket_id_exit;
        LOG("[OM] STUCK-EXIT RETRY: cancelling exit basket=%s, re-sending at px=%.2f "
            "(reason=%s)",
            old_basket.empty() ? "(none)" : old_basket.c_str(), current_price,
            pos_.exit_reason.c_str());
        if (!old_basket.empty()) {
            // By server id when mapped (a client-id cancel is refused and the old exit
            // would keep resting). Guard it like a cancelled stop so a late fill is
            // unwound and the post-close recancel keeps retrying it by server id.
            const std::string sid = cancel_id_for_locked(old_basket);
            if (sid != old_basket) server_to_client_cancelled_[sid] = old_basket;
            else                   client_only_cancels_.insert(old_basket);
            cancelled_stop_trade_[old_basket] = trade_seq_;
            cancel_order_locked(old_basket, "stuck exit");
            cancelled_stops_[old_basket] = exit_was_buy;
            if (cancel_persist_cb_) cancel_persist_cb_(old_basket, exit_was_buy);
            // The just-cancelled exit is now the order most likely to late-fill
            // after FLAT (the flat purge clears cancelled_stops_, but the
            // single-slot unwind guard survives until clear_post_close_recancels).
            last_stop_for_unwind_ = old_basket;
            last_stop_was_buy_    = exit_was_buy;
        }
        pos_.basket_id_exit.clear();
        pos_.state = (pos_.direction == OrbSignal::BUY) ? PosState::LONG : PosState::SHORT;
        initiate_exit_locked("stuck_exit_retry", current_price);
    }

    // ── Reject notification (entry/exit/stop rejected by gateway or exchange) ──
    // Gateway rejects (tid=313/315 ResponseNewOrder) carry ONLY the server-assigned
    // basket_id — the proto has no user_tag field — so resolve it through the
    // server→client map populated from tid=351/352 notifications before matching.
    void on_order_rejected(const std::string& basket_id, const std::string& msg) {
        std::lock_guard<std::mutex> lk(state_mu_);
        std::string resolved = basket_id;
        if (auto it = server_to_client_orders_.find(basket_id);
            it != server_to_client_orders_.end()) {
            resolved = it->second;
            LOG("[OM] Reject correlation: server=%s → client=%s",
                basket_id.c_str(), resolved.c_str());
        }
        LOG("[OM] Order rejected basket=%s msg=%s", resolved.c_str(), msg.c_str());
        if (pos_.basket_id_entry == resolved && pos_.state == PosState::PENDING_ENTRY) {
            pos_ = Position{};
            LOG("[OM] Reverted to FLAT after entry rejection");
        } else if (pos_.basket_id_exit == resolved && pos_.state == PosState::PENDING_EXIT) {
            LOG("[OM] CRITICAL: Exit order rejected basket=%s — reverting to %s",
                basket_id.c_str(), pos_.direction == OrbSignal::BUY ? "LONG" : "SHORT");
            pos_.state = (pos_.direction == OrbSignal::BUY) ? PosState::LONG : PosState::SHORT;
            pos_.basket_id_exit.clear();
            ++rejected_exit_count_;
            if (rejected_exit_count_ <= 3) {
                initiate_exit_locked("rejected_exit_retry", 0.0);
            } else {
                LOG("[OM] CRITICAL: Exit rejected %d times — halting new entries. Manual intervention required.",
                    rejected_exit_count_);
                entry_halted_ = true;
            }
        } else if (pos_.basket_id_stop == resolved ||
                   (!stop_server_basket_.empty() && stop_server_basket_ == basket_id)) {
            // Stop order rejected — clear basket so software SL fallback activates.
            // It never worked at the exchange, so it can never fill: drop the DB guard
            // written at submit, or every restart re-cancels it, gets "Cancellation
            // Failed" and logs a false "stop is LIVE" alarm (2026-09-24 overnight test).
            if (cancel_remove_cb_) cancel_remove_cb_(pos_.basket_id_stop);
            pos_.basket_id_stop.clear();
            stop_server_basket_.clear();  // stale server mapping no longer valid
            LOG("[OM] CRITICAL: Exchange stop rejected — software SL fallback now active (sl=%.2f)",
                pos_.sl_price);
        }
    }

    // ── Cancel ACK — stop confirmed cancelled by exchange ────────────────────
    void on_cancel_confirmed(const std::string& basket_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        bool was_guard = cancelled_stops_.erase(basket_id) > 0;
        // Clean reverse map: find entry whose value matches client basket_id
        for (auto it = server_to_client_cancelled_.begin();
             it != server_to_client_cancelled_.end(); ) {
            if (it->second == basket_id) it = server_to_client_cancelled_.erase(it);
            else ++it;
        }
        client_only_cancels_.erase(basket_id);
        cancelled_stop_levels_.erase(basket_id);
        // Always clean DB: flat purge may have preserved this row pending this ACK
        if (cancel_remove_cb_) cancel_remove_cb_(basket_id);
        if (last_stop_for_unwind_ == basket_id) last_stop_for_unwind_.clear();
        LOG("[OM] Cancel ACK basket=%s — confirmed dead%s (pending_cancelled=%zu)",
            basket_id.c_str(),
            was_guard ? " (removed from unwind guard)" : "",
            cancelled_stops_.size());
    }

    // Cancel ACK arrived with empty user_tag (external cancellation via RTrader).
    // Resolve client basket from exchange basket_id via the reverse map.
    void on_cancel_confirmed_by_server_basket(const std::string& server_basket_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        auto it = server_to_client_cancelled_.find(server_basket_id);
        if (it == server_to_client_cancelled_.end()) {
            LOG("[OM] Cancel ACK server=%s — not in cancelled guard (already confirmed or filled)",
                server_basket_id.c_str());
            return;
        }
        const std::string client_id = it->second;
        server_to_client_cancelled_.erase(it);
        cancelled_stops_.erase(client_id);
        client_only_cancels_.erase(client_id);
        cancelled_stop_levels_.erase(client_id);
        // Always clean DB: flat purge may have preserved this row pending this ACK
        if (cancel_remove_cb_) cancel_remove_cb_(client_id);
        if (last_stop_for_unwind_ == client_id) last_stop_for_unwind_.clear();
        LOG("[OM] Cancel ACK server=%s → client=%s confirmed dead (pending_cancelled=%zu)",
            server_basket_id.c_str(), client_id.c_str(), cancelled_stops_.size());
    }

    // How many cancelled stops are still unconfirmed (could still fire from exchange).
    int pending_cancelled_stop_count() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return (int)cancelled_stops_.size();
    }

    // ── Server basket_id for stop order (from tid=351 notifications) ─────────
    // The exchange assigns its own basket_id; RequestModifyOrder needs it, not our user_tag.
    void set_stop_server_basket(const std::string& server_basket_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        stop_server_basket_ = server_basket_id;
        int64_t elapsed_ms = 0;
        if (stop_submit_time_ != std::chrono::steady_clock::time_point{}) {
            elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - stop_submit_time_).count();
        }
        LOG("[OM] Stop server basket_id mapped: client=%s server=%s (took %ldms since submit)",
            pos_.basket_id_stop.c_str(), server_basket_id.c_str(), (long)elapsed_ms);
    }

    // Map a server-assigned basket_id to our client user_tag (tid=351/352 notifications
    // carry both). Gateway rejects (tid=313/315) carry only the server basket_id, so
    // on_order_rejected() resolves them through this map.
    // Public, locking variant — the executor cancels its own unwind orders with it.
    std::string routable_id(const std::string& client_id) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return cancel_id_for_locked(client_id);
    }

    void map_server_basket(const std::string& client_id, const std::string& server_id) {
        if (client_id.empty() || server_id.empty()) return;
        std::lock_guard<std::mutex> lk(state_mu_);
        server_to_client_orders_[server_id] = client_id;
        if (unmapped_order_cancels_.erase(client_id) > 0) {
            if (auto g = cancelled_stops_.find(client_id); g != cancelled_stops_.end()) {
                server_to_client_cancelled_[server_id] = client_id;
                client_only_cancels_.erase(client_id);
            }
            LOG("[OM] LATE-MAP: cancelled order client=%s now has server=%s — re-sending the "
                "cancel by server id", client_id.c_str(), server_id.c_str());
            if (cancel_cb_) cancel_cb_(server_id);
        }
    }

    // Fill dedupe: the same fill can be delivered on tid=351 (cumulative
    // total_fill_size) AND tid=352 (per-event fill_size), and tid=351 can repeat a
    // COMPLETE notification (partial-then-complete sends the running total again).
    // Returns true when a fill for basket_id at this quantity was already processed —
    // the caller must skip it.
    bool fill_already_processed(const std::string& basket_id, int fill_qty) {
        std::lock_guard<std::mutex> lk(state_mu_);
        int& seen = processed_fill_qty_[basket_id];
        if (seen >= fill_qty) return true;
        seen = fill_qty;
        return false;
    }

    // PENDING_ENTRY watchdog: if the entry order has been pending longer than
    // timeout_secs (gateway reject lost or uncorrelatable, fill never delivered),
    // cancel it and revert to FLAT. Returns true when a timeout cancel was issued.
    bool pending_entry_timeout_check(int timeout_secs = 10) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (pos_.state != PosState::PENDING_ENTRY) return false;
        if (cfg_.dry_run) return false;  // dry-run entries fill synchronously
        auto age_s = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - pos_.fill_time).count();
        if (age_s < timeout_secs) return false;
        LOG("[OM] CRITICAL: PENDING_ENTRY timeout (%lds >= %ds) — cancelling entry "
            "basket=%s and reverting to FLAT",
            (long)age_s, timeout_secs, pos_.basket_id_entry.c_str());
        // Same late-fill guard as flatten_now(): if the entry fills after the cancel,
        // the spurious-fill handler unwinds it immediately.
        pending_cancel_basket_  = pos_.basket_id_entry;
        pending_cancel_was_buy_ = (pos_.direction == OrbSignal::BUY);
        cancel_order_locked(pos_.basket_id_entry, "timed-out entry");
        pos_ = Position{};
        return true;
    }

    // ── Read-only position snapshot for DB / UI writes ────────────────────────
    // Returns a copy of the current Position so the caller can build a
    // write_position() call without holding the mutex during DB I/O.
    Position position_snapshot() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return pos_;
    }

    bool is_flat() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return pos_.state == PosState::FLAT;
    }

    bool is_entry_basket(const std::string& basket_id) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return pos_.basket_id_entry == basket_id;
    }

    bool is_stop_basket(const std::string& basket_id) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!pos_.basket_id_stop.empty() && pos_.basket_id_stop == basket_id) return true;
        // A stop of THIS trade whose cancel is still in flight (trail/breakeven replace) is
        // still ours: if it fires, it IS the exit. Routed as unowned it only halted entries
        // while we stayed in the trade and later sent a second exit (net flipped by qty).
        if (pos_.state == PosState::LONG || pos_.state == PosState::SHORT) {
            auto t = cancelled_stop_trade_.find(basket_id);
            return t != cancelled_stop_trade_.end() && t->second == trade_seq_ &&
                   cancelled_stops_.count(basket_id) > 0;
        }
        return false;
    }

    // True when basket_id matches the current market-exit order (from initiate_exit_locked).
    // Used by the tid=351 fill handler to route software-SL and EOD-flatten fills,
    // which Legends delivers as COMPLETE on tid=351 rather than ExchangeOrderNotification.
    bool is_exit_basket(const std::string& basket_id) const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return !pos_.basket_id_exit.empty() && pos_.basket_id_exit == basket_id;
    }

    PosState state() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return pos_.state;
    }

    // Returns true (and clears flag) when a trade completed since last check
    bool pop_trade_completed(Position& out) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!trade_completed_) return false;
        out = completed_pos_;
        trade_completed_ = false;
        return true;
    }

    const TradeLatency& last_entry_lat() const { return last_entry_lat_; }
    const TradeLatency& last_exit_lat()  const { return last_exit_lat_; }

private:
    OrbConfig      cfg_;
    RiskManager&   risk_;
    LatencyLogger& lat_;
    OrderSendCallback   order_cb_;
    OrderCancelCallback cancel_cb_;

    mutable std::mutex state_mu_;
    Position           pos_;
    Position           completed_pos_;
    bool               trade_completed_ = false;

    TradeLatency last_entry_lat_;
    TradeLatency last_exit_lat_;

    // Server-assigned basket_id for the current stop order (needed for RequestModifyOrder)
    std::string stop_server_basket_;
    std::chrono::steady_clock::time_point stop_submit_time_{};  // for server-mapping latency log

    // Server-assigned basket_id → client user_tag for entry/exit/stop orders.
    // Populated from tid=351/352 notifications (map_server_basket); used by
    // on_order_rejected() to correlate gateway rejects, which carry no user_tag.
    std::unordered_map<std::string, std::string> server_to_client_orders_;
    // Fill dedupe: basket_id → largest fill quantity already processed.
    // tid=351 reports cumulative total_fill_size; tid=352 per-event fill_size.
    std::unordered_map<std::string, int> processed_fill_qty_;
    // trade each cancelled stop/exit belonged to (on_cancel_failed must not re-adopt a
    // stop from an earlier trade), and entry/exit cancels still waiting for a server id
    uint64_t trade_seq_ = 0;
    std::unordered_map<std::string, uint64_t> cancelled_stop_trade_;
    std::unordered_set<std::string> unmapped_order_cancels_;
    // Bounded: one entry per filled order; a session has at most a few dozen.
    void prune_processed_fills_locked() {
        if (processed_fill_qty_.size() > 512) processed_fill_qty_.clear();
    }

    // Stale stop unwind state (fires when old stop fills after position already closed)
    std::string last_stop_for_unwind_;      // basket of stop sent to cancel at position close
    bool        last_stop_was_buy_ = false; // true = stop was a BUY (SHORT position)

    // All stops ever cancelled this session: basket → was_buy_stop.
    // Exchange cancel is not atomic — the stop can fire after we think it's gone.
    // Any basket in this map that fires while FLAT triggers an immediate unwind.
    // Also persisted to DB so restarts don't lose knowledge of pending cancels.
    std::unordered_map<std::string, bool> cancelled_stops_;
    // Reverse map: exchange basket_id → our client basket_id.
    // Populated in cancel_stop_locked(); used to resolve cancel ACKs that arrive
    // with empty user_tag (external cancellations via RTrader).
    std::unordered_map<std::string, std::string> server_to_client_cancelled_;
    // Stops whose cancel went out by CLIENT id because the server basket was not
    // mapped yet. Rithmic cannot route such a cancel ("Cancellation Failed"), so
    // these stops are still WORKING on the exchange until a server id arrives and
    // the cancel is re-sent. They must never be purged as "confirmed".
    // 2026-09-23: one of these was purged at FLAT, fired 12s later, and left the
    // account long 2 MNQ that the executor knew nothing about.
    std::unordered_set<std::string> client_only_cancels_;
    // Exchange stop level at the moment each cancel was sent (client id → price),
    // so a stop re-adopted after "Cancellation Failed" gets its real level back.
    std::unordered_map<std::string, double> cancelled_stop_levels_;

    CancelPersistCb         cancel_persist_cb_;
    CancelRemoveCb          cancel_remove_cb_;
    CancelPersistServerIdCb cancel_persist_server_id_cb_;

    // Baskets of unwind orders sent to correct ghost positions from late stop fires.
    // When an unwind fill arrives we log it cleanly instead of SPURIOUS-FILL.
    std::unordered_set<std::string> unwind_baskets_;

    // EOD cancel race guard: entry cancel sent but fill arrived after pos_ was reset
    std::string pending_cancel_basket_;     // entry basket that was cancelled at EOD
    bool        pending_cancel_was_buy_ = false; // direction of the cancelled entry

    // Exit rejection retry limit
    int  rejected_exit_count_ = 0;         // incremented each time an exit order is rejected
    bool entry_halted_        = false;      // set after 3 consecutive exit rejections
    bool ghost_halted_        = false;      // set after unknown fill while FLAT; cleared by confirm_exchange_flat()

    // Last SL price submitted to the exchange — used to suppress cancel+resubmit
    // storms: only update the exchange stop when sl moved by >= trail_step.
    double last_exchange_sl_  = 0.0;

    // mtf_scalper only: stop DISTANCE captured at signal time (on_signal's
    // desired_sl_dist), consumed once by the entry fill handler then cleared.
    // 0.0 (default) = ORB/Trend path, unaffected.
    double pending_entry_sl_dist_ = 0.0;

    // Breach timer for the software SL timeout tier.
    // Set when price first violates SL while an exchange stop basket is active.
    // Software SL fires if the exchange stop hasn't responded within cfg_.sl_fire_timeout_ms.
    std::chrono::steady_clock::time_point sl_breach_time_{};

    bool stop_resubmit_pending_ = false;  // set during stop cancel+resubmit; clears when safe

    static std::atomic<uint64_t> seq_;  // monotonic sequence for basket IDs

    std::string new_basket_id() {
        // Format: "NQ-<epoch_ms>-<seq>"
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return cfg_.symbol + "-" + std::to_string(ms) + "-" + std::to_string(seq_.fetch_add(1));
    }

    double compute_sl(double fill_price, OrbSignal dir) const {
        if (dir == OrbSignal::BUY)  return fill_price - cfg_.sl_points;
        if (dir == OrbSignal::SELL) return fill_price + cfg_.sl_points;
        return fill_price;
    }

    // Snap a stop to the NQ/MNQ 0.25 tick grid in the ADVERSE direction (long: floor,
    // short: ceil) — mirrors paper_bracket_broker.hpp snap(). mtf_scalper's stops are
    // ATR-derived decimals; CME rejects an off-increment STOP_MARKET trigger, which would
    // leave the position on the software SL only (naked if the process dies). Floor/ceil
    // are monotone, so the strategy's tighten-only ratchet is preserved.
    static double snap_stop(double px, OrbSignal dir) {
        constexpr double TICK = 0.25;
        const double t = px / TICK;
        return (dir == OrbSignal::BUY ? std::floor(t + 1e-9) : std::ceil(t - 1e-9)) * TICK;
    }

    // Same as compute_sl() but with a caller-supplied distance instead of cfg_.sl_points
    // (mtf_scalper's own ATR-based r_unit, captured at signal time — see on_signal()).
    static double compute_sl_dist(double fill_price, OrbSignal dir, double dist) {
        if (dir == OrbSignal::BUY)  return snap_stop(fill_price - dist, dir);
        if (dir == OrbSignal::SELL) return snap_stop(fill_price + dist, dir);
        return fill_price;
    }

    void send_market_order(const std::string& basket,
                           bool is_buy,
                           double ref_price,
                           const std::string& user_tag) {
        lat_.on_submit(basket, ref_price);

        if (cfg_.dry_run) {
            LOG("[OM] [DRY_RUN] Would send MKT %s qty=%d basket=%s",
                is_buy ? "BUY" : "SELL", cfg_.qty, basket.c_str());
            on_fill_notification_locked(basket, ref_price, cfg_.qty, /*is_entry=*/true);
            return;
        }

        if (!order_cb_) {
            LOG("[OM] ERROR: no order callback set — cannot send order");
            return;
        }

        // Use aggressive limit instead of market: 4 ticks past signal price.
        // Legends prop accounts reject market orders; limit with offset fills immediately.
        constexpr double TICK = 0.25;
        constexpr int    OFFSET_TICKS = 4;
        double limit_px = is_buy ? ref_price + OFFSET_TICKS * TICK
                                 : ref_price - OFFSET_TICKS * TICK;
        bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                             cfg_.qty, /*LIMIT=1*/1, is_buy, limit_px, user_tag);
        if (!ok) {
            LOG("[OM] ERROR: order_cb_ returned false for basket=%s", basket.c_str());
            pos_ = Position{};
        }
    }

    // Submit exchange stop order (called while state_mu_ held)
    void submit_stop_order_locked(double sl_price) {
        if (cfg_.dry_run || !order_cb_) return;
        bool is_long = (pos_.state == PosState::LONG);
        bool stop_is_sell = is_long;
        std::string basket = new_basket_id();
        pos_.basket_id_stop = basket;
        stop_server_basket_.clear();   // new stop — server basket_id not yet known
        stop_submit_time_ = std::chrono::steady_clock::now();
        LOG("[OM] STOP-SUBMIT: %s STOP_MARKET at %.2f basket=%s "
            "(entry=%.2f dist=%.2fpt pending_cancelled=%zu)",
            stop_is_sell ? "SELL" : "BUY", sl_price, basket.c_str(),
            pos_.entry_price, std::abs(sl_price - pos_.entry_price),
            cancelled_stops_.size());
        // Pre-populate latency record so exchange-stop fills get meaningful metrics
        lat_.on_signal(basket, sl_price, /*is_entry=*/false);
        lat_.on_submit(basket, sl_price);
        bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                            cfg_.qty, /*STOP_MARKET=4*/4, !stop_is_sell, sl_price, "stop_loss");
        if (!ok) {
            LOG("[OM] ERROR: stop order send failed — clearing basket, software SL active");
            pos_.basket_id_stop.clear();
        } else {
            last_exchange_sl_ = sl_price;
            // Track in DB from submission — not just from cancel. Any crash between
            // submit and cancel ACK leaves this stop live on the exchange; startup
            // reads this table and fires a cancel for every row.
            if (cancel_persist_cb_) cancel_persist_cb_(basket, !stop_is_sell);
        }
    }

    // Update stop to new_sl via cancel+resubmit (Legends rejects RequestModifyOrder).
    // Before resubmitting, validates stop is still on the correct side of current_price.
    // If price has already blown through the new SL, exits immediately instead.
    //
    // Race window: between RequestCancelOrder and the cancel ACK, the old stop is still
    // live on the exchange. If it fires during this window, on_fill_notification_locked
    // handles it via the existing exit-fill path (pos_.state is still LONG/SHORT, the
    // basket_id won't match pos_.basket_id_stop which already holds the new basket, so
    // exit_reason="unknown_exit" and the new stop is cancelled via cancel_stop_locked).
    // The stale-stop unwind guard (last_stop_for_unwind_) handles the symmetric case
    // where the fill arrives *after* the position has already gone FLAT.
    // Net risk: a trailing move can trigger at the old SL level instead of the new one
    // — effectively a one-trail-step slip. Acceptable given Legends' modify restriction.
    void update_stop_order_locked(double /*old_sl*/, double new_sl) {
        if (cfg_.dry_run) {
            LOG("[OM] [DRY_RUN] Trail: would update stop to %.2f", new_sl);
            return;
        }
        bool is_long = (pos_.state == PosState::LONG);

        // Suppress cancel+resubmit storm: only update the exchange stop when the SL
        // has moved by >= trail_step since the last submitted stop. The in-memory
        // pos_.sl_price is already updated by the caller for accurate DB display.
        if (last_exchange_sl_ != 0.0 &&
            std::abs(new_sl - last_exchange_sl_) < cfg_.trail_step - 1e-9) {
            return;
        }

        if (pos_.basket_id_stop.empty()) {
            submit_stop_order_locked(new_sl);
            return;
        }
        LOG("[OM] Trail update: cancel+resubmit stop client=%s server=%s "
            "old_sl=%.2f new_sl=%.2f",
            pos_.basket_id_stop.c_str(),
            stop_server_basket_.empty() ? "unmapped" : stop_server_basket_.c_str(),
            last_exchange_sl_, new_sl);
        stop_resubmit_pending_ = true;
        cancel_stop_locked();
        submit_stop_order_locked(new_sl);
    }

    // The id Rithmic can act on for one of our orders: its server basket id once a
    // tid=351 mapped it, else our client id (which Rithmic cannot route — the caller must
    // make sure the cancel is re-sent when the mapping arrives).
    std::string cancel_id_for_locked(const std::string& client_id) const {
        for (const auto& [sid, cid] : server_to_client_orders_)
            if (cid == client_id) return sid;
        return client_id;
    }

    // Cancel an entry/exit order (not the stop) by the id Rithmic can route. Unmapped →
    // remember it; map_server_basket() re-sends the cancel by server id when it maps.
    void cancel_order_locked(const std::string& client_id, const char* what) {
        if (client_id.empty() || !cancel_cb_) return;
        const std::string id = cancel_id_for_locked(client_id);
        if (id == client_id) {
            unmapped_order_cancels_.insert(client_id);
            LOG("[OM] WARN: %s %s has no server id yet — cancel sent by client id (Rithmic may "
                "refuse); it is re-sent by server id as soon as one maps", what, client_id.c_str());
        }
        cancel_cb_(id);
    }

    // Cancel stop without replacing (called while state_mu_ held)
    void cancel_stop_locked() {
        if (pos_.basket_id_stop.empty() || !cancel_cb_) return;
        bool was_buy_stop = (pos_.direction == OrbSignal::SELL); // SHORT has BUY stop
        // Save basket so we can detect stale fills after position closes
        last_stop_for_unwind_ = pos_.basket_id_stop;
        last_stop_was_buy_    = was_buy_stop;
        // Also add to persistent map — cancel confirmation may never arrive
        cancelled_stops_[pos_.basket_id_stop] = was_buy_stop;
        cancelled_stop_trade_[pos_.basket_id_stop] = trade_seq_;
        if (cancel_persist_cb_) cancel_persist_cb_(pos_.basket_id_stop, was_buy_stop);
        // Populate reverse map before clearing so cancel ACKs (and post-close recancels)
        // can resolve the client basket by server basket ID.
        if (!stop_server_basket_.empty()) {
            server_to_client_cancelled_[stop_server_basket_] = pos_.basket_id_stop;
            // Persist server ID to DB so next startup can cancel by server basket ID
            // (Rithmic's RequestCancelOrder requires the exchange-assigned basket_id,
            // not our client user_tag — without this, startup recancel silently fails).
            if (cancel_persist_server_id_cb_)
                cancel_persist_server_id_cb_(pos_.basket_id_stop, stop_server_basket_);
        }
        // Use server basket ID for RequestCancelOrder — Rithmic routes the cancel by its
        // own server-assigned basket_id, not our user_tag.  Fall back to client ID only if
        // the server basket hasn't been mapped yet (race: cancel before first notification).
        const std::string& cancel_id = stop_server_basket_.empty()
                                       ? pos_.basket_id_stop : stop_server_basket_;
        cancelled_stop_levels_[pos_.basket_id_stop] =
            last_exchange_sl_ != 0.0 ? last_exchange_sl_ : pos_.sl_price;
        if (stop_server_basket_.empty()) {
            // Rithmic routes cancels by its own basket id; a client-id cancel comes
            // back "Cancellation Failed" and the stop keeps working. Remember that so
            // the guard survives FLAT and the cancel is re-sent when the id maps.
            client_only_cancels_.insert(pos_.basket_id_stop);
            LOG("[OM] WARN: server ID not yet mapped — cancel sent by client ID, which "
                "Rithmic may refuse; stop %s stays guarded until a server ID maps and "
                "the cancel is re-sent (or it fills and is unwound)",
                pos_.basket_id_stop.c_str());
        }
        LOG("[OM] STOP-CANCEL: client=%s server=%s sl=%.2f dir=%s "
            "(cancel_id=%s, pending_cancelled=%zu)",
            pos_.basket_id_stop.c_str(),
            stop_server_basket_.empty() ? "unmapped" : stop_server_basket_.c_str(),
            pos_.sl_price,
            was_buy_stop ? "BUY-stop(SHORT)" : "SELL-stop(LONG)",
            cancel_id.c_str(),
            cancelled_stops_.size());
        cancel_cb_(cancel_id);
        pos_.basket_id_stop.clear();
        stop_server_basket_.clear();
    }

    void initiate_exit_locked(const std::string& reason, double ref_price) {
        if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT) return;

        sl_breach_time_ = {};  // clear breach timer — we are exiting
        stop_resubmit_pending_ = false;

        // Cancel the exchange stop BEFORE submitting market exit to prevent double-fill.
        cancel_stop_locked();

        pos_.state       = PosState::PENDING_EXIT;
        pos_.exit_reason = reason;

        bool exit_is_buy = (pos_.direction == OrbSignal::SELL); // SHORT → exit BUY

        std::string basket = new_basket_id();
        pos_.basket_id_exit = basket;

        LOG("[OM] EXIT-INITIATE: reason=%s ref_price=%.2f basket=%s dir=%s "
            "entry=%.2f sl=%.2f be=%d trailing=%d pending_cancelled=%zu%s",
            reason.c_str(), ref_price, basket.c_str(),
            exit_is_buy ? "BUY(close-SHORT)" : "SELL(close-LONG)",
            pos_.entry_price, pos_.sl_price,
            (int)pos_.be_triggered, (int)pos_.trailing_active,
            cancelled_stops_.size(),
            cfg_.dry_run ? " [DRY_RUN]" : "");

        lat_.on_signal(basket, ref_price, /*is_entry=*/false);

        if (cfg_.dry_run) {
            LOG("[OM] [DRY_RUN] Would send MKT %s qty=%d basket=%s",
                exit_is_buy ? "BUY" : "SELL", pos_.qty, basket.c_str());
            on_fill_notification_locked(basket, ref_price, pos_.qty, /*is_entry=*/false);
            return;
        }

        if (!order_cb_) {
            LOG("[OM] ERROR: no order callback for exit basket=%s", basket.c_str());
            return;
        }

        // Legends prop accounts reject MARKET (type=2). Use an aggressive LIMIT that
        // crosses the spread immediately. 50-tick offset (~12.5 pts) when no ref_price
        // is available (kill signal); 4-tick offset otherwise (same as entry orders).
        constexpr double TICK = 0.25;
        // Kill-signal exits keep the 50-tick offset even with a price: the process is going
        // away and must not leave a resting exit behind.
        int offset_ticks = (ref_price > 0.0 && reason != "kill_signal") ? 4 : 50;
        double limit_px = exit_is_buy
            ? ref_price + offset_ticks * TICK
            : ref_price - offset_ticks * TICK;
        if (ref_price <= 0.0) limit_px = exit_is_buy
            ? pos_.sl_price + offset_ticks * TICK
            : pos_.sl_price - offset_ticks * TICK;

        lat_.on_submit(basket, ref_price > 0.0 ? ref_price : pos_.sl_price);
        bool ok = order_cb_(basket, cfg_.symbol, cfg_.exchange,
                             pos_.qty, /*LIMIT=1*/1, exit_is_buy, limit_px, reason);
        if (!ok) {
            // Send failed (WS down / serialization error). The stop was already
            // cancelled above — staying in PENDING_EXIT would leave the position
            // naked with the software SL dead. Revert to LONG/SHORT and restore
            // the exchange stop; the software SL / watchdog will retry the exit.
            LOG("[OM] ERROR: exit order_cb_ returned false basket=%s — reverting to %s "
                "and restoring protective stop",
                basket.c_str(), pos_.direction == OrbSignal::BUY ? "LONG" : "SHORT");
            pos_.basket_id_exit.clear();
            pos_.state = (pos_.direction == OrbSignal::BUY)
                         ? PosState::LONG : PosState::SHORT;
            ++rejected_exit_count_;
            if (rejected_exit_count_ > 3) {
                LOG("[OM] CRITICAL: exit send failed %d times — halting new entries. "
                    "Manual intervention required.", rejected_exit_count_);
                entry_halted_ = true;
            }
            submit_stop_order_locked(pos_.sl_price);
        }
    }

    // Erase any server→client reverse-map entries pointing at client basket_id.
    // Must be called whenever a cancelled stop is resolved by a FILL rather than
    // a cancel ACK — otherwise unconfirmed_server_cancels() never drains and the
    // 30s background recancel re-sends cancels for an already-filled order forever.
    void erase_server_cancel_for_client_locked(const std::string& client_id) {
        for (auto it = server_to_client_cancelled_.begin();
             it != server_to_client_cancelled_.end(); ) {
            if (it->second == client_id) it = server_to_client_cancelled_.erase(it);
            else ++it;
        }
        client_only_cancels_.erase(client_id);
        cancelled_stop_levels_.erase(client_id);
    }
};

// ─── Exchange net-position reconciliation ──────────────────────────────────────
// Pure timing state for "does the exchange agree with us?". The executor feeds
// every AccountPnLPositionUpdate (tid=451) through observe(); a mismatch that
// persists past the grace window (in-flight fills need a moment to settle) is
// acted on exactly once — unwind the difference and halt entries — and the
// state re-arms only after the exchange and the order manager agree again.
struct NetReconciler {
    enum class Verdict { OK, MISMATCH_WAIT, MISMATCH_ACT };
    int64_t mismatch_since_ms = 0;   // 0 = currently consistent
    bool    acted             = false;
    int64_t acted_ms          = 0;   // when we last acted (retry clock)

    // retry_ms > 0: a mismatch that SURVIVES an action (the unwind rested, was rejected or
    // failed to send) is acted on again every retry_ms — the caller cancels the previous
    // unwind first. 0 = act once per mismatch (the old behaviour).
    Verdict observe(bool consistent, int64_t now_ms, int grace_ms, int retry_ms = 0) {
        if (consistent) {
            mismatch_since_ms = 0;
            acted = false;
            acted_ms = 0;
            return Verdict::OK;
        }
        if (mismatch_since_ms == 0) mismatch_since_ms = now_ms;
        if (acted) {
            if (retry_ms > 0 && now_ms - acted_ms >= retry_ms) { acted_ms = now_ms; return Verdict::MISMATCH_ACT; }
            return Verdict::MISMATCH_WAIT;
        }
        if (now_ms - mismatch_since_ms < grace_ms) return Verdict::MISMATCH_WAIT;
        acted = true;
        acted_ms = now_ms;
        return Verdict::MISMATCH_ACT;
    }
};

// Static member definition (in header because it's a header-only class)
inline std::atomic<uint64_t> OrderManager::seq_{0};
