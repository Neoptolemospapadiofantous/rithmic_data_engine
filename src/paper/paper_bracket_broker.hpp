#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_bracket_broker.hpp — fill simulator for strategies that compute
    their own bracket (e.g. MtfScalperStrategy via cur_stop()/cur_tp()).

    Unlike PaperBroker (ORB exits owned by the broker), here the STRATEGY owns
    exit logic; the broker only simulates the exchange:

      Entry:   on BUY/SELL signal (flat, not halted) the order fills at the
               FIRST tick after the signal at tick.price ± slippage (adverse),
               qty supplied by the host (strategy's qty_calc).
      Stop:    strategy-driven, ratcheted (never loosens); fills when a tick
               trades at/through it, fill at stop ± slippage (adverse),
               reason "stop".
      Target:  limit leg from cur_tp() (NaN = trailing active, no target);
               fills at the limit price, reason "target".
      Flatten: strategy's FLATTEN_EOD signal exits at the next tick with the
               strategy's reason preserved ("Osc reversal", "Time stop",
               "Trend reversal", "Session End / Out of Range", ...).
      EOD:     backstop clock flatten at eod_flatten_hour:min ET (reason
               "eod") — covers restart state loss and tick-sparse periods.
      Risk:    per-strategy RiskManager (daily-loss halt → "daily_loss");
               host-driven flatten() for account halts ("account_halt").

    Restart note: the broker resumes an open paper position from the DB with
    its persisted stop; a NaN/0 strategy stop never overwrites a resumed stop.
    ═══════════════════════════════════════════════════════════════════════════ */

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>

#include "paper_db.hpp"
#include "orb_config.hpp"      // OrbConfig (risk knobs), us_et_offset
#include "risk_manager.hpp"
#include "orb_strategy.hpp"    // OrbTick / OrbSignal
#include "log.hpp"

namespace paper {

class PaperBracketBroker {
public:
    PaperBracketBroker(std::string strategy_id, std::string account_label,
                       std::string symbol, const OrbConfig& cfg,
                       double tick_size, int slippage_ticks, PaperStore* store)
        : strategy_id_(std::move(strategy_id))
        , account_label_(std::move(account_label))
        , symbol_(std::move(symbol))
        , cfg_(cfg)
        , tick_size_(tick_size)
        , slip_(slippage_ticks * tick_size)
        , store_(store)
        , risk_(cfg, cfg.starting_balance)
    {}

    RiskManager& risk() { return risk_; }

    // Fired after every trade close (host updates paper_daily / paper_account
    // and forwards notify_trade_filled to the strategy).
    std::function<void(const PaperTradeRow&)> on_trade_closed;

    // ── Strategy signal (BUY/SELL entry, FLATTEN_EOD = market flatten) ──────
    void on_signal(OrbSignal sig, double /*price*/, const std::string& reason) {
        if (sig == OrbSignal::FLATTEN_EOD) {
            if (pos_dir_ != 0)
                pending_exit_reason_ = reason.empty() ? "signal_flatten" : reason;
            return;
        }
        if (sig != OrbSignal::BUY && sig != OrbSignal::SELL) return;
        if (halted()) return;
        const int dir = (sig == OrbSignal::BUY) ? 1 : -1;
        // Reversal: the strategy permits flips (enters opposite while holding).
        // Exit the current leg at market on the next tick, then enter the new
        // direction on the tick after — by then the strategy's bracket is the
        // new leg's, so the old leg is never ratcheted with the wrong side's
        // values.
        if (pos_dir_ != 0 && dir != pos_dir_) {
            pending_flip_ = dir;
            return;
        }
        if (pos_dir_ != 0 || pending_dir_ != 0) return;   // single position
        pending_dir_ = dir;
    }

    // ── One tick. strat_stop/strat_tp are the strategy's current bracket ────
    // (NaN/0 stop = leave existing stop unchanged; NaN tp = no limit leg).
    void on_tick(const OrbTick& t, double strat_stop, double strat_tp,
                 int strat_qty) {
        last_tick_us_ = t.ts_micros;
        last_price_   = t.price;
        utc_to_et(t.ts_micros, last_et_hour_, last_et_min_);

        // 0. Reversal flip: close the current leg at market and queue the
        //    opposite entry for the next tick. Skips the bracket update so the
        //    old leg is never ratcheted with the new leg's stop.
        if (pending_flip_ != 0) {
            const int nd = pending_flip_;
            pending_flip_ = 0;
            if (pos_dir_ != 0)
                exit_position(t.price - pos_dir_ * slip_, t.ts_micros, "reversal");
            if (!halted())
                pending_dir_ = nd;
            return;
        }

        // 1. Pending entry fills at this tick (signal came earlier).
        if (pending_dir_ != 0 && pos_dir_ == 0) {
            if (!halted())
                enter(pending_dir_, t.price + pending_dir_ * slip_, t.ts_micros,
                      strat_qty, strat_stop, strat_tp);
            pending_dir_ = 0;
        }

        if (pos_dir_ == 0) return;

        // 2. Strategy bracket update — stop ratchets, never loosens.
        update_bracket(strat_stop, strat_tp);

        // 3. Stop: trade-through fills at stop ∓ slippage (adverse).
        if ((pos_dir_ > 0 && t.price <= stop_price_) ||
            (pos_dir_ < 0 && t.price >= stop_price_)) {
            exit_position(stop_price_ - pos_dir_ * slip_, t.ts_micros, "stop");
            return;
        }

        // 4. Target limit leg (NaN = trailing active — no target).
        if (!std::isnan(tp_price_) && tp_price_ > 0.0 &&
            ((pos_dir_ > 0 && t.price >= tp_price_) ||
             (pos_dir_ < 0 && t.price <= tp_price_))) {
            exit_position(tp_price_, t.ts_micros, "target");
            return;
        }

        // 5. Strategy-requested market flatten (osc/time/trend/session exits).
        if (!pending_exit_reason_.empty()) {
            std::string reason = pending_exit_reason_;
            pending_exit_reason_.clear();
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, reason);
            return;
        }

        // 6. Risk halt while in position (daily-loss latched by RiskManager).
        if (risk_.halted()) {
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, "daily_loss");
            return;
        }

        // 7. EOD clock backstop.
        int now_min = last_et_hour_ * 60 + last_et_min_;
        int eod_min = cfg_.eod_flatten_hour * 60 + cfg_.eod_flatten_min;
        if (now_min >= eod_min) {
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, "eod");
            return;
        }

        risk_.update_unrealized(unrealized(t.price));
    }

    // ── Restart recovery: resume an open paper position ─────────────────────
    void resume_position(int dir, int qty, double entry, double stop,
                         int64_t entry_time_us) {
        pos_dir_       = dir;
        qty_           = qty;
        entry_price_   = entry;
        stop_price_    = stop;
        tp_price_      = std::numeric_limits<double>::quiet_NaN();
        entry_time_us_ = entry_time_us;
        LOG("[PAPER %s] Resumed %s qty=%d entry=%.2f stop=%.2f from DB",
            strategy_id_.c_str(), dir > 0 ? "LONG" : "SHORT", qty, entry, stop);
    }

    // ── Host-driven flatten (account halt / shutdown) ───────────────────────
    void flatten(const std::string& reason, int64_t ts_us, double price_hint) {
        pending_dir_ = 0;
        pending_flip_ = 0;
        pending_exit_reason_.clear();
        if (pos_dir_ == 0) return;
        double px = price_hint > 0.0 ? price_hint : entry_price_;
        exit_position(px - pos_dir_ * slip_, ts_us, reason);
    }

    void reset_day() {
        risk_.reset_daily();
        risk_.update_unrealized(0.0);
    }

    // ── State access ─────────────────────────────────────────────────────────
    bool   in_position()  const { return pos_dir_ != 0; }
    int    direction()    const { return pos_dir_; }
    int    qty()          const { return qty_; }
    double entry_price()  const { return entry_price_; }
    double stop_price()   const { return stop_price_; }
    double last_price()   const { return last_price_; }
    bool   halted()       const { return risk_.halted(); }

    double unrealized(double price) const {
        if (pos_dir_ == 0) return 0.0;
        return (price - entry_price_) * pos_dir_ * cfg_.point_value * qty_;
    }

    PaperPositionRow position_row(double last_price) const {
        PaperPositionRow p;
        p.strategy_id    = strategy_id_;
        p.direction      = pos_dir_;
        p.qty            = qty_;
        p.entry_price    = entry_price_;
        p.entry_time_us  = entry_time_us_;
        p.stop_price     = stop_price_;
        p.target_price   = std::isnan(tp_price_) ? 0.0 : tp_price_;
        p.unrealized_pnl = unrealized(last_price);
        return p;
    }

private:
    void enter(int dir, double fill, int64_t ts_us, int qty,
               double stop, double tp) {
        pos_dir_       = dir;
        qty_           = std::max(1, qty);
        entry_price_   = fill;
        entry_time_us_ = ts_us;
        pending_exit_reason_.clear();  // never leak a stale exit into a new trade
        stop_price_    = (!std::isnan(stop) && stop > 0.0)
                             ? snap(stop, dir)
                             : snap(fill - dir * cfg_.sl_points, dir);  // fallback
        tp_price_      = tp;
        LOG("[PAPER %s] ENTER %s qty=%d @ %.2f stop=%.2f tp=%.2f (bracket)",
            strategy_id_.c_str(), dir > 0 ? "LONG" : "SHORT", qty_, fill,
            stop_price_, std::isnan(tp_price_) ? 0.0 : tp_price_);
        persist_position();
    }

    void update_bracket(double strat_stop, double strat_tp) {
        if (!std::isnan(strat_stop) && strat_stop > 0.0) {
            if (stop_price_ <= 0.0)
                stop_price_ = strat_stop;
            else if (pos_dir_ > 0)
                stop_price_ = std::max(stop_price_, strat_stop);
            else
                stop_price_ = std::min(stop_price_, strat_stop);
        }
        tp_price_ = strat_tp;   // NaN retires the limit leg (trailing armed)
    }

    void exit_position(double fill, int64_t ts_us, const std::string& reason) {
        PaperTradeRow tr;
        tr.strategy_id   = strategy_id_;
        tr.account_label = account_label_;
        tr.symbol        = symbol_;
        tr.direction     = pos_dir_ > 0 ? "LONG" : "SHORT";
        tr.qty           = qty_;
        tr.entry_time_us = entry_time_us_;
        tr.entry_price   = entry_price_;
        tr.exit_time_us  = ts_us;
        tr.exit_price    = fill;
        tr.pnl_pts       = (fill - entry_price_) * pos_dir_;
        tr.commission    = cfg_.commission_rt * qty_;
        tr.pnl_usd       = tr.pnl_pts * cfg_.point_value * qty_ - tr.commission;
        tr.exit_reason   = reason;

        LOG("[PAPER %s] EXIT %s @ %.2f reason=%s pnl=%.2fpts ($%.2f)",
            strategy_id_.c_str(), tr.direction.c_str(), fill, reason.c_str(),
            tr.pnl_pts, tr.pnl_usd);

        pos_dir_  = 0;
        qty_      = 0;
        tp_price_ = std::numeric_limits<double>::quiet_NaN();
        pending_exit_reason_.clear();  // consumed or moot — never leak forward
        risk_.update_unrealized(0.0);
        risk_.on_trade_pnl(tr.pnl_usd);

        if (store_) store_->record_trade(tr);
        persist_position();  // flat row (qty=0, direction=NULL)
        if (on_trade_closed) on_trade_closed(tr);
    }

    void persist_position() {
        if (!store_) return;
        store_->save_position(position_row(last_price_ > 0.0 ? last_price_
                                                             : entry_price_));
    }

    // Snap a stop price to the tick grid in the adverse direction (long: floor).
    double snap(double px, int dir) const {
        if (tick_size_ <= 0.0) return px;
        double ticks = px / tick_size_;
        return (dir > 0 ? std::floor(ticks + 1e-9) : std::ceil(ticks - 1e-9)) * tick_size_;
    }

    static void utc_to_et(int64_t ts_us, int& h, int& m) {
        int64_t ts_sec = ts_us / 1'000'000;
        time_t tt = static_cast<time_t>(ts_sec);
        struct tm tm_utc;
        gmtime_r(&tt, &tm_utc);
        int64_t et = ts_sec - us_et_offset(tm_utc) * 3600LL;
        h = (int)((et / 3600) % 24);
        if (h < 0) h += 24;
        m = (int)((et % 3600) / 60);
    }

    std::string strategy_id_;
    std::string account_label_;
    std::string symbol_;
    OrbConfig   cfg_;
    double      tick_size_;
    double      slip_;
    PaperStore* store_;   // not owned; may be null
    RiskManager risk_;

    int     pos_dir_ = 0;         // +1 long, -1 short, 0 flat
    int     qty_ = 0;
    double  entry_price_ = 0.0;
    double  stop_price_  = 0.0;
    double  tp_price_    = std::numeric_limits<double>::quiet_NaN();
    int64_t entry_time_us_ = 0;

    int         pending_dir_ = 0; // entry waiting for next tick
    int         pending_flip_ = 0; // reversal: exit current leg, enter opposite
    std::string pending_exit_reason_;

    int64_t last_tick_us_ = 0;
    double  last_price_   = 0.0;
    int     last_et_hour_ = 0;
    int     last_et_min_  = 0;
};

} // namespace paper
