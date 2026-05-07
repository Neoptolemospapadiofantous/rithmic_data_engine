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
    // persist_cb: called when a stop cancel is sent (basket_id, was_buy_stop)
    // remove_cb:  called when cancel is confirmed or the stop fires (basket_id)
    using CancelPersistCb = std::function<void(const std::string& basket_id, bool was_buy_stop)>;
    using CancelRemoveCb  = std::function<void(const std::string& basket_id)>;
    void set_cancel_persist_callbacks(CancelPersistCb persist, CancelRemoveCb remove) {
        cancel_persist_cb_ = std::move(persist);
        cancel_remove_cb_  = std::move(remove);
    }

    // Re-send cancel for every stop still in the unconfirmed-cancel map.
    // Call immediately after a trade closes. Safe to call multiple times — exchange
    // returns a harmless "not found" if the order is already gone.
    void recancel_pending_stops() {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (cancelled_stops_.empty() || !cancel_cb_) return;
        LOG("[OM] TRADE-CLOSE: re-cancelling %zu pending stop(s) — belt-and-suspenders "
            "against ghost positions", cancelled_stops_.size());
        for (const auto& [bid, _] : cancelled_stops_) {
            cancel_cb_(bid);
            LOG("[OM] TRADE-CLOSE: cancel re-sent basket=%s", bid.c_str());
        }
    }

    // Called at startup to reload cancelled stops from DB (survive restart).
    void seed_cancelled_stops(const std::string& basket_id, bool was_buy_stop) {
        std::lock_guard<std::mutex> lk(state_mu_);
        cancelled_stops_[basket_id] = was_buy_stop;
        LOG("[OM] STARTUP-RECON: seeded cancelled_stop basket=%s dir=%s",
            basket_id.c_str(), was_buy_stop ? "BUY-stop(SHORT)" : "SELL-stop(LONG)");
    }

    // ── Called by OrbStrategy signal callback ─────────────────────────────────
    void on_signal(OrbSignal sig, double price, const std::string& reason, double orb_boundary = 0.0) {
        if (sig == OrbSignal::FLATTEN_EOD) {
            flatten_now("eod_flatten", price);
            return;
        }
        if (sig != OrbSignal::BUY && sig != OrbSignal::SELL) return;

        if (entry_halted_) {
            LOG("[OM] Signal rejected — entries halted after repeated exit rejections");
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
                // Check for stale stop fill: cancel raced and stop fired anyway
                if (!last_stop_for_unwind_.empty() && basket_id == last_stop_for_unwind_) {
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
                    auto it = cancelled_stops_.find(basket_id);
                    if (it != cancelled_stops_.end()) {
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
                        entry_halted_ = false;  // unwind confirmed flat — safe to resume
                    } else if (entry_halted_) {
                        // Already halted from a prior ghost fill. This is likely a
                        // manual close order (RTrader) or startup-recon unwind that
                        // we didn't register. Don't escalate — stay halted and wait
                        // for tid=451 to confirm exchange is flat, which will clear
                        // the halt via confirm_exchange_flat().
                        LOG("[OM] MANUAL-CLOSE-DETECTED: basket=%s px=%.2f qty=%d "
                            "— unknown fill while already halted, likely manual RTrader "
                            "intervention. Waiting for position confirm to resume.",
                            basket_id.c_str(), fill_price, fill_qty);
                    } else {
                        // Completely unknown fill while FLAT — this is a ghost position.
                        // DB-seeded cancelled_stops should have caught this; if we're here
                        // it means a stop survived across multiple restarts without being
                        // persisted. Halt trading and require manual intervention.
                        LOG("[OM] GHOST-FILL: basket=%s px=%.2f qty=%d state=FLAT "
                            "— unknown fill, exchange may be non-flat. "
                            "TRADING HALTED — check RTrader and restart executor.",
                            basket_id.c_str(), fill_price, fill_qty);
                        entry_halted_ = true;
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

            // Cancel the exchange stop order if it wasn't the one that just filled
            if (!pos_.basket_id_stop.empty() && pos_.basket_id_stop != basket_id) {
                cancel_stop_locked();
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

            pos_ = Position{};  // back to FLAT
            trade_completed_ = true;
            // Clear stale-stop unwind state. last_stop_for_unwind_ was set when we
            // cancelled the exchange stop before sending the market exit. If the old
            // stop fires after this point the position is already FLAT — without this
            // clear the stale-stop branch would match the basket and send an unintended
            // unwind order, effectively re-entering a position.
            last_stop_for_unwind_.clear();
            last_stop_was_buy_ = false;

            // Purge all remaining trail-update cancel guards.  These are stops
            // cancelled during the just-closed trade's trail updates whose cancel
            // ACKs never arrived (common on simulator).  Once we are FLAT the
            // guard is no longer needed — remove them from DB so they don't
            // accumulate across 24x7 cycles.
            if (!cancelled_stops_.empty()) {
                LOG("[OM] FLAT — purging %zu stale trail-cancel guard(s) from DB",
                    cancelled_stops_.size());
                for (const auto& [bid, _] : cancelled_stops_) {
                    if (cancel_remove_cb_) cancel_remove_cb_(bid);
                }
                cancelled_stops_.clear();
                server_to_client_cancelled_.clear();
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
        // Tier 2 — timeout: exchange stop submitted but hasn't fired after kSLFireTimeoutMs.
        //   Catches silent stop failures and cancel+resubmit race windows.
        //   initiate_exit_locked() cancels the exchange stop first to minimise double-fill risk.
        bool sl_moved = false;

        bool sl_breached = (is_long  && current_price <= pos_.sl_price) ||
                           (!is_long && current_price >= pos_.sl_price);

        if (sl_breached) {
            if (pos_.basket_id_stop.empty()) {
                LOG("[OM] Software SL hit (%s, no exchange stop): price=%.2f sl=%.2f",
                    is_long ? "LONG" : "SHORT", current_price, pos_.sl_price);
                sl_breach_time_ = {};
                initiate_exit_locked("stop_loss", current_price);
                return false;
            }
            // Exchange stop active: start or check breach timer.
            if (sl_breach_time_ == std::chrono::steady_clock::time_point{}) {
                sl_breach_time_ = std::chrono::steady_clock::now();
            } else {
                auto breach_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - sl_breach_time_).count();
                if (breach_ms >= kSLFireTimeoutMs) {
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
            sl_breach_time_ = {};  // price recovered above SL — reset timer
        }

        // BE: immediate — no delay. Fires as soon as MFE passes trail_be_trigger.
        if (!pos_.be_triggered && pos_.mfe >= cfg_.trail_be_trigger) {
            pos_.be_triggered = true;
            double be_sl = is_long
                ? pos_.entry_price + cfg_.trail_be_offset
                : pos_.entry_price - cfg_.trail_be_offset;
            if ((is_long && be_sl > pos_.sl_price) ||
                (!is_long && be_sl < pos_.sl_price)) {
                double old_sl = pos_.sl_price;
                pos_.sl_price = be_sl;
                sl_moved = true;
                LOG("[OM] BE triggered — SL moved to entry+%.1fpt: %.2f",
                    cfg_.trail_be_offset, be_sl);
                update_stop_order_locked(old_sl, be_sl, current_price);
            }
        }

        // Trail: activates after trail_delay_secs (independent of BE).
        if (!pos_.trailing_active) {
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - pos_.fill_time).count();
            if (elapsed >= cfg_.trail_delay_secs && pos_.mfe >= cfg_.trail_be_trigger) {
                pos_.trailing_active = true;
                LOG("[OM] Trailing activated after %lds", (long)elapsed);
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
                LOG("[OM] Trail updated: price=%.2f new_sl=%.2f", current_price, trail_sl);
                update_stop_order_locked(old_sl, trail_sl, current_price);
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
        if (was_guard && cancel_remove_cb_) cancel_remove_cb_(basket_id);
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
        bool was_guard = cancelled_stops_.erase(client_id) > 0;
        if (was_guard && cancel_remove_cb_) cancel_remove_cb_(client_id);
        if (last_stop_for_unwind_ == client_id) last_stop_for_unwind_.clear();
        LOG("[OM] Cancel ACK server=%s → client=%s confirmed dead (pending_cancelled=%zu)",
            server_basket_id.c_str(), client_id.c_str(), cancelled_stops_.size());
    }

    // Called after startup-recon sends an unwind order so its fill is recognised
    // instead of triggering a second GHOST-FILL log.
    void register_unwind_basket(const std::string& basket_id) {
        std::lock_guard<std::mutex> lk(state_mu_);
        unwind_baskets_.insert(basket_id);
        LOG("[OM] STARTUP-RECON: registered unwind basket=%s", basket_id.c_str());
    }

    // Called by tid=451 handler when exchange confirms net_qty=0.
    // Clears the ghost-fill halt so new entries are allowed again.
    void confirm_exchange_flat() {
        std::lock_guard<std::mutex> lk(state_mu_);
        if (entry_halted_) {
            entry_halted_ = false;
            LOG("[OM] GHOST-HALT CLEARED: tid=451 confirmed exchange FLAT — entries re-enabled");
        }
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
        LOG("[OM] Stop server basket_id mapped: client=%s server=%s",
            pos_.basket_id_stop.c_str(), server_basket_id.c_str());
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

    CancelPersistCb cancel_persist_cb_;
    CancelRemoveCb  cancel_remove_cb_;

    // Baskets of unwind orders sent to correct ghost positions from late stop fires.
    // When an unwind fill arrives we log it cleanly instead of SPURIOUS-FILL.
    std::unordered_set<std::string> unwind_baskets_;

    // EOD cancel race guard: entry cancel sent but fill arrived after pos_ was reset
    std::string pending_cancel_basket_;     // entry basket that was cancelled at EOD
    bool        pending_cancel_was_buy_ = false; // direction of the cancelled entry

    // Exit rejection retry limit
    int  rejected_exit_count_ = 0;         // incremented each time an exit order is rejected
    bool entry_halted_        = false;      // set after 3 consecutive exit rejections

    // Last SL price submitted to the exchange — used to suppress cancel+resubmit
    // storms: only update the exchange stop when sl moved by >= trail_step.
    double last_exchange_sl_  = 0.0;

    // Breach timer for the software SL timeout tier.
    // Set when price first violates SL while an exchange stop basket is active.
    // Software SL fires if the exchange stop hasn't responded within kSLFireTimeoutMs.
    std::chrono::steady_clock::time_point sl_breach_time_{};
    static constexpr int64_t kSLFireTimeoutMs = 3000;  // ms — well above normal stop latency

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
    void update_stop_order_locked(double /*old_sl*/, double new_sl,
                                  double current_price = 0.0) {
        if (cfg_.dry_run) {
            LOG("[OM] [DRY_RUN] Trail: would update stop to %.2f", new_sl);
            return;
        }
        bool is_long = (pos_.state == PosState::LONG);

        // If price already past new SL, no point placing a stop — exit immediately.
        if (current_price > 0.0) {
            bool already_hit = is_long ? (current_price <= new_sl)
                                       : (current_price >= new_sl);
            if (already_hit) {
                LOG("[OM] Trail: price=%.2f already past new SL=%.2f — exiting immediately",
                    current_price, new_sl);
                pos_.sl_price = new_sl;
                initiate_exit_locked("stop_loss_trail", current_price);
                return;
            }
        }

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
        LOG("[OM] Trail update: cancel+resubmit stop %s trigger=%.2f",
            pos_.basket_id_stop.c_str(), new_sl);
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
        LOG("[OM] STOP-CANCEL: basket=%s sl=%.2f dir=%s "
            "(unwind_guard=%s, pending_cancelled=%zu)",
            pos_.basket_id_stop.c_str(), pos_.sl_price,
            was_buy_stop ? "BUY-stop(SHORT)" : "SELL-stop(LONG)",
            pos_.basket_id_stop.c_str(),
            cancelled_stops_.size());
        // Populate reverse map before clearing so empty-user_tag cancel ACKs
        // (from external RTrader cancellations) can still resolve the client basket.
        if (!stop_server_basket_.empty())
            server_to_client_cancelled_[stop_server_basket_] = pos_.basket_id_stop;
        cancel_cb_(pos_.basket_id_stop);
        pos_.basket_id_stop.clear();
        stop_server_basket_.clear();
        pending_modify_      = false;
        pending_modify_new_sl_ = 0.0;
    }

    void initiate_exit_locked(const std::string& reason, double ref_price) {
        if (pos_.state != PosState::LONG && pos_.state != PosState::SHORT) return;

        sl_breach_time_ = {};  // clear breach timer — we are exiting

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
