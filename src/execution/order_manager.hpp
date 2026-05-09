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
// Atomically changes the trigger_price of an existing STOP_MARKET order.
// Returns true if the modify was sent successfully.
using OrderModifyCallback = std::function<bool(
    const std::string& basket_id,
    double new_trigger_price
)>;

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
    void set_modify_callback(OrderModifyCallback cb) { modify_cb_ = std::move(cb); }

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

    // ── Called by OrbStrategy signal callback ─────────────────────────────────
    void on_signal(OrbSignal sig, double price, const std::string& reason, double orb_boundary = 0.0) {
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
        pos_.state         = PosState::PENDING_ENTRY;
        pos_.direction     = sig;
        pos_.basket_id_entry = basket;
        pos_.qty           = cfg_.qty;
        pos_.trigger_price = price;   // ORB breakout level at time of order submission
        pos_.fill_time     = std::chrono::steady_clock::now(); // placeholder until fill

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
            // Partial-fill guard: fill_qty < qty means the order wasn't fully filled.
            // Multi-lot partial fills are not supported (position tracking is all-or-nothing).
            // At qty=1 a partial fill is physically impossible; this is a safety net.
            if (fill_qty > 0 && fill_qty < pos_.qty) {
                LOG("[OM] WARN: partial ENTRY fill basket=%s fill_qty=%d expected=%d "
                    "— treating as full fill; multi-lot partial fills not supported",
                    basket_id.c_str(), fill_qty, pos_.qty);
            }

            pos_.entry_price       = fill_price;
            pos_.fill_price_actual = fill_price;  // actual fill for slippage calc
            pos_.fill_time         = std::chrono::steady_clock::now();
            pos_.state = (pos_.direction == OrbSignal::BUY)
                         ? PosState::LONG : PosState::SHORT;

            // Clear EOD-cancel race guard — the entry filled normally, no late-fill expected.
            pending_cancel_basket_.clear();
            pending_cancel_was_buy_ = false;

            // Place stop-loss
            double sl = compute_sl(fill_price, pos_.direction);
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

            // Exchange stop filled directly (state still LONG/SHORT) — set exit reason
            if (pos_.state == PosState::LONG || pos_.state == PosState::SHORT) {
                pos_.exit_reason = (basket_id == pos_.basket_id_stop)
                    ? "exchange_stop" : "unknown_exit";
                pos_.state = PosState::PENDING_EXIT;
            }

            if (fill_qty > 0 && fill_qty < pos_.qty) {
                LOG("[OM] WARN: partial EXIT fill basket=%s fill_qty=%d expected=%d "
                    "— treating as full fill; multi-lot partial fills not supported",
                    basket_id.c_str(), fill_qty, pos_.qty);
            }

            pos_.exit_price  = fill_price;
            double pts = (pos_.direction == OrbSignal::BUY)
                         ? fill_price - pos_.entry_price
                         : pos_.entry_price - fill_price;
            pos_.pnl_points = pts;
            pos_.pnl_usd    = pts * cfg_.point_value
                              - 2.0 * MNQ_COMMISSION; // round-turn

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
                    // Clean reverse map too
                    for (auto rit = server_to_client_cancelled_.begin();
                         rit != server_to_client_cancelled_.end(); ) {
                        if (rit->second == basket_id) rit = server_to_client_cancelled_.erase(rit);
                        else ++rit;
                    }
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
                for (const auto& [bid, _] : cancelled_stops_) {
                    bool has_pending_server_cancel = false;
                    for (const auto& [sid, cid] : server_to_client_cancelled_)
                        if (cid == bid) { has_pending_server_cancel = true; break; }
                    if (has_pending_server_cancel) {
                        ++pending_cnt;  // keep in DB; ACK or startup will clean up
                    } else {
                        ++confirmed_cnt;
                        if (cancel_remove_cb_) cancel_remove_cb_(bid);
                    }
                }
                LOG("[OM] FLAT — purged %zu confirmed trail-cancel guard(s) from DB; "
                    "kept %zu with unconfirmed server cancel (server IDs kept for 5s recancel window)",
                    confirmed_cnt, pending_cnt);
                cancelled_stops_.clear();
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
            if (cancel_cb_ && !pos_.basket_id_entry.empty())
                cancel_cb_(pos_.basket_id_entry);
            pos_ = Position{};
            return;
        }
        if (pos_.state == PosState::PENDING_EXIT) {
            LOG("[OM] flatten_now('%s') — exit already pending", reason.c_str());
            return;
        }
        initiate_exit_locked(reason, price);
    }

    // ── Reject notification (entry rejected by exchange) ─────────────────────
    void on_order_rejected(const std::string& basket_id, const std::string& msg) {
        std::lock_guard<std::mutex> lk(state_mu_);
        LOG("[OM] Order rejected basket=%s msg=%s", basket_id.c_str(), msg.c_str());
        if (pos_.basket_id_entry == basket_id && pos_.state == PosState::PENDING_ENTRY) {
            pos_ = Position{};
            LOG("[OM] Reverted to FLAT after entry rejection");
        } else if (pos_.basket_id_exit == basket_id && pos_.state == PosState::PENDING_EXIT) {
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
        } else if (pos_.basket_id_stop == basket_id) {
            // Stop order rejected — clear basket so software SL fallback activates
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

    // ── Modify response from exchange — if rejected, fall back to cancel+resubmit ──
    void on_modify_response(bool accepted, const std::string& rp_code) {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (!pending_modify_) return;
        pending_modify_ = false;
        if (accepted) {
            LOG("[OM] Stop modify ACKed by exchange (new_sl=%.2f)", pending_modify_new_sl_);
            pending_modify_new_sl_ = 0.0;
        } else {
            double new_sl = pending_modify_new_sl_;
            pending_modify_new_sl_ = 0.0;
            LOG("[OM] Stop modify REJECTED (rp_code=%s) — cancel+resubmit at %.2f",
                rp_code.c_str(), new_sl);
            if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT) return;
            cancel_stop_locked();
            submit_stop_order_locked(new_sl);
        }
    }

    bool has_pending_modify() const {
        std::lock_guard<std::mutex> lk(state_mu_);
        return pending_modify_;
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
        return !pos_.basket_id_stop.empty() && pos_.basket_id_stop == basket_id;
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
    OrderModifyCallback modify_cb_;

    mutable std::mutex state_mu_;
    Position           pos_;
    Position           completed_pos_;
    bool               trade_completed_ = false;

    TradeLatency last_entry_lat_;
    TradeLatency last_exit_lat_;

    // Server-assigned basket_id for the current stop order (needed for RequestModifyOrder)
    std::string stop_server_basket_;
    std::chrono::steady_clock::time_point stop_submit_time_{};  // for server-mapping latency log
    // Pending modify state: tracks an in-flight modify so rejection triggers fallback
    bool        pending_modify_      = false;
    double      pending_modify_new_sl_ = 0.0;

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
        pending_modify_      = false;  // clear any stale pending modify
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

    // Cancel stop without replacing (called while state_mu_ held)
    void cancel_stop_locked() {
        if (pos_.basket_id_stop.empty() || !cancel_cb_) return;
        bool was_buy_stop = (pos_.direction == OrbSignal::SELL); // SHORT has BUY stop
        // Save basket so we can detect stale fills after position closes
        last_stop_for_unwind_ = pos_.basket_id_stop;
        last_stop_was_buy_    = was_buy_stop;
        // Also add to persistent map — cancel confirmation may never arrive
        cancelled_stops_[pos_.basket_id_stop] = was_buy_stop;
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
        if (stop_server_basket_.empty()) {
            LOG("[OM] WARN: server ID not yet mapped — using client ID as fallback "
                "(BE fired before first tid=351 notification arrived) client=%s",
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
        pending_modify_      = false;
        pending_modify_new_sl_ = 0.0;
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
        int offset_ticks = (ref_price > 0.0) ? 4 : 50;
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
            LOG("[OM] ERROR: exit order_cb_ returned false basket=%s", basket.c_str());
        }
    }
};

// Static member definition (in header because it's a header-only class)
inline std::atomic<uint64_t> OrderManager::seq_{0};
