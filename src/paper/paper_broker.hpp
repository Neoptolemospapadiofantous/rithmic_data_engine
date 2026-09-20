#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_broker.hpp — per-strategy paper fill simulator + risk

    Mirrors the live executor's exit semantics (src/execution/order_manager.hpp
    check_trail_and_stop) with a deterministic tick-driven fill model:

      Entry:   on BUY/SELL signal (flat, not halted, trades_today <
               max_daily_trades, inside session hours, before last_entry_hour)
               the order fills at the FIRST tick after the signal, at
               tick.price ± slippage_ticks*tick_size (adverse).
      Stop:    fills when a tick trades at/through the stop; fill at
               stop ± slippage (adverse).
      Trail:   armed once (elapsed >= trail_delay_secs) AND (MFE >=
               trail_be_trigger). On arming, stop moves to entry ±
               trail_be_offset (breakeven). Thereafter stop ratchets to
               price ∓ trail_step, snapped to the tick grid, never retreating.
      EOD:     FLATTEN_EOD signal exits at the next tick ("signal_flatten");
               reaching eod_flatten_hour:min ET exits immediately ("eod").
      Cooldown: stop_cooldown_secs blocks re-entry after any exit (event time).

    P&L: pnl_usd = pnl_pts * point_value * qty − commission_rt * qty.

    Call order per tick (host): broker.on_tick(t) BEFORE strategy.on_tick(t),
    so a signal emitted by the strategy fills on the NEXT tick.

    All timing uses tick event time (ts_micros) — deterministic under test.
    The PaperStore sink is optional (nullptr in unit tests).
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_config.hpp"
#include "orb_strategy.hpp"
#include "paper_db.hpp"
#include "risk_manager.hpp"
#include "log.hpp"

#include <cmath>
#include <functional>
#include <string>

namespace paper {

class PaperBroker {
public:
    PaperBroker(std::string strategy_id, std::string account_label, std::string symbol,
                const OrbConfig& cfg, double tick_size, int slippage_ticks,
                PaperStore* store)
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

    // Fired after every trade close (host updates paper_daily / paper_account).
    std::function<void(const PaperTradeRow&)> on_trade_closed;

    // ── Signal entry point (wired to OrbStrategy::set_signal_callback) ───────
    void on_signal(OrbSignal sig, double /*price*/, const std::string& reason) {
        if (sig == OrbSignal::FLATTEN_EOD) {
            if (pos_dir_ != 0 && pending_exit_reason_.empty()) {
                pending_exit_reason_ = "signal_flatten";
                LOG("[PAPER %s] FLATTEN_EOD signal (%s) — exit at next tick",
                    strategy_id_.c_str(), reason.c_str());
            }
            return;
        }
        if (sig != OrbSignal::BUY && sig != OrbSignal::SELL) return;
        if (pos_dir_ != 0 || pending_dir_ != 0) return;         // only when flat
        if (halted()) return;
        std::string why;
        if (!risk_.can_trade(why)) {
            LOG("[PAPER %s] Entry blocked by risk: %s", strategy_id_.c_str(), why.c_str());
            return;
        }
        if (entries_today_ >= cfg_.max_daily_trades) {
            LOG("[PAPER %s] Entry blocked: max_daily_trades %d/%d",
                strategy_id_.c_str(), entries_today_, cfg_.max_daily_trades);
            return;
        }
        if (last_tick_us_ > 0 && last_tick_us_ < cooldown_until_us_) return;  // cooldown
        if (!in_entry_window()) {
            LOG("[PAPER %s] Entry blocked: outside entry window", strategy_id_.c_str());
            return;
        }
        pending_dir_ = (sig == OrbSignal::BUY) ? 1 : -1;
        LOG("[PAPER %s] %s signal (%s) — pending entry at next tick",
            strategy_id_.c_str(), pending_dir_ > 0 ? "BUY" : "SELL", reason.c_str());
    }

    // ── Tick entry point — call BEFORE strategy.on_tick() ────────────────────
    void on_tick(const OrbTick& t) {
        last_tick_us_ = t.ts_micros;
        utc_to_et(t.ts_micros, last_et_hour_, last_et_min_);

        // 1. Pending entry fills at first tick after the signal (adverse slip).
        if (pending_dir_ != 0 && pos_dir_ == 0) {
            int dir = pending_dir_;
            pending_dir_ = 0;
            double fill = t.price + dir * slip_;
            enter(dir, fill, t.ts_micros);
        }

        // 2. Pending signal-flatten exits at the next tick (market, adverse slip).
        if (!pending_exit_reason_.empty() && pos_dir_ != 0) {
            std::string reason = pending_exit_reason_;
            pending_exit_reason_.clear();
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, reason);
            return;
        }

        if (pos_dir_ == 0) {
            risk_.update_unrealized(0.0);
            return;
        }

        // 3. EOD wall-clock flatten (ET from tick time).
        int now_min = last_et_hour_ * 60 + last_et_min_;
        int eod_min = cfg_.eod_flatten_hour * 60 + cfg_.eod_flatten_min;
        if (now_min >= eod_min) {
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, "eod");
            return;
        }

        // 4. Risk halt while in position (daily-loss halt latched by RiskManager).
        if (risk_.halted()) {
            exit_position(t.price - pos_dir_ * slip_, t.ts_micros, "daily_loss");
            return;
        }

        // 5. Stop check FIRST — breach evaluated against the stop as of the
        //    previous tick, mirroring check_trail_and_stop ordering.
        bool stopped = (pos_dir_ > 0) ? (t.price <= stop_price_)
                                      : (t.price >= stop_price_);
        if (stopped) {
            double fill = stop_price_ - pos_dir_ * slip_;
            exit_position(fill, t.ts_micros, stop_exit_reason());
            return;
        }

        // 6. MFE / BE / trail ratchet.
        double mfe_now = (pos_dir_ > 0) ? (t.price - entry_price_)
                                        : (entry_price_ - t.price);
        if (mfe_now > mfe_) mfe_ = mfe_now;
        if (mfe_now < mae_) mae_ = mfe_now;   // adverse excursion (≤ 0)

        int64_t elapsed_s = (t.ts_micros - entry_time_us_) / 1'000'000;
        if (!trail_armed_ && elapsed_s >= cfg_.trail_delay_secs &&
            mfe_ >= cfg_.trail_be_trigger) {
            trail_armed_ = true;
            LOG("[PAPER %s] Trail armed (elapsed=%lds mfe=%.2f)",
                strategy_id_.c_str(), (long)elapsed_s, mfe_);
        }

        if (trail_armed_) {
            // Breakeven move: stop → entry ± trail_be_offset (never retreats).
            double be = entry_price_ + pos_dir_ * cfg_.trail_be_offset;
            if ((pos_dir_ > 0 && be > stop_price_) ||
                (pos_dir_ < 0 && be < stop_price_)) {
                stop_price_ = snap(be, pos_dir_);
                be_moved_ = true;
                sl_dirty_ = true;
                LOG("[PAPER %s] BE move — stop=%.2f (entry%+.1f)",
                    strategy_id_.c_str(), stop_price_, pos_dir_ * cfg_.trail_be_offset);
            }
            // Trail ratchet: stop → price ∓ trail_step, only tightening.
            double cand = t.price - pos_dir_ * cfg_.trail_step;
            cand = snap(cand, pos_dir_);
            if ((pos_dir_ > 0 && cand > stop_price_) ||
                (pos_dir_ < 0 && cand < stop_price_)) {
                if (cand != stop_price_) {
                    stop_price_ = cand;
                    sl_dirty_ = true;
                }
            }
        }

        double unreal = unrealized(t.price);
        risk_.update_unrealized(unreal);
        if (sl_dirty_) {
            sl_dirty_ = false;
            persist_position(unreal);
        }
    }

    // ── Resume an open position loaded from paper_positions ──────────────────
    void resume_position(int dir, int qty, double entry, double stop,
                         int64_t entry_time_us) {
        pos_dir_       = dir;
        qty_           = qty;
        entry_price_   = entry;
        stop_price_    = stop;
        entry_time_us_ = entry_time_us;
        // Infer BE/trail state from where the stop sits relative to entry.
        be_moved_    = (dir > 0) ? (stop >= entry) : (stop <= entry);
        trail_armed_ = be_moved_;
        LOG("[PAPER %s] Resumed %s qty=%d entry=%.2f stop=%.2f from DB",
            strategy_id_.c_str(), dir > 0 ? "LONG" : "SHORT", qty, entry, stop);
    }

    // ── Host-driven flatten (account halt / shutdown) ────────────────────────
    void flatten(const std::string& reason, int64_t ts_us, double price_hint) {
        pending_dir_ = 0;
        pending_exit_reason_.clear();
        if (pos_dir_ == 0) return;
        double px = price_hint > 0.0 ? price_hint : entry_price_;
        exit_position(px - pos_dir_ * slip_, ts_us, reason);
    }

    void reset_day() {
        entries_today_ = 0;
        cooldown_until_us_ = 0;
        risk_.reset_daily();
        risk_.update_unrealized(0.0);
    }

    void seed_entries_today(int n) { entries_today_ = n; }

    // ── State access ─────────────────────────────────────────────────────────
    bool   in_position()  const { return pos_dir_ != 0; }
    int    direction()    const { return pos_dir_; }
    int    qty()          const { return qty_; }
    double entry_price()  const { return entry_price_; }
    double stop_price()   const { return stop_price_; }
    int    entries_today() const { return entries_today_; }
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
        p.target_price   = 0.0;
        p.unrealized_pnl = unrealized(last_price);
        return p;
    }

private:
    void enter(int dir, double fill, int64_t ts_us) {
        pos_dir_       = dir;
        qty_           = cfg_.qty;
        entry_price_   = fill;
        entry_time_us_ = ts_us;
        stop_price_    = snap(fill - dir * cfg_.sl_points, dir);
        mfe_           = 0.0;
        mae_           = 0.0;
        be_moved_      = false;
        trail_armed_   = false;
        ++entries_today_;
        LOG("[PAPER %s] ENTER %s qty=%d @ %.2f stop=%.2f (trade %d/%d)",
            strategy_id_.c_str(), dir > 0 ? "LONG" : "SHORT", qty_, fill,
            stop_price_, entries_today_, cfg_.max_daily_trades);
        persist_position(0.0);
    }

    std::string stop_exit_reason() const {
        if (!be_moved_) return "stop";
        double be = entry_price_ + pos_dir_ * cfg_.trail_be_offset;
        bool past_be = (pos_dir_ > 0) ? (stop_price_ > be + 1e-9)
                                      : (stop_price_ < be - 1e-9);
        return past_be ? "trail" : "breakeven";
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
        // Fold the exit tick into the excursion bounds, then persist them
        const double ex = tr.pnl_pts;
        if (ex > mfe_) mfe_ = ex;
        if (ex < mae_) mae_ = ex;
        tr.mfe_pts       = mfe_;
        tr.mae_pts       = mae_;
        tr.exit_reason   = reason;

        LOG("[PAPER %s] EXIT %s @ %.2f reason=%s pnl=%.2fpts ($%.2f)",
            strategy_id_.c_str(), tr.direction.c_str(), fill, reason.c_str(),
            tr.pnl_pts, tr.pnl_usd);

        pos_dir_ = 0;
        qty_ = 0;
        risk_.update_unrealized(0.0);
        risk_.on_trade_pnl(tr.pnl_usd);
        if (cfg_.stop_cooldown_secs > 0)
            cooldown_until_us_ = ts_us + (int64_t)cfg_.stop_cooldown_secs * 1'000'000;

        if (store_) store_->record_trade(tr);
        persist_position(0.0);  // flat row (qty=0, direction=NULL)
        if (on_trade_closed) on_trade_closed(tr);
    }

    void persist_position(double unreal) {
        if (!store_) return;
        PaperPositionRow p;
        p.strategy_id    = strategy_id_;
        p.direction      = pos_dir_;
        p.qty            = qty_;
        p.entry_price    = entry_price_;
        p.entry_time_us  = entry_time_us_;
        p.stop_price     = stop_price_;
        p.target_price   = 0.0;
        p.unrealized_pnl = unreal;
        store_->save_position(p);
    }

    // Snap a stop price to the tick grid in the adverse direction (long: floor).
    double snap(double px, int dir) const {
        if (tick_size_ <= 0.0) return px;
        double ticks = px / tick_size_;
        return (dir > 0 ? std::floor(ticks + 1e-9) : std::ceil(ticks - 1e-9)) * tick_size_;
    }

    bool in_entry_window() const {
        if (last_tick_us_ <= 0) return true;  // no tick yet — don't block (tests)
        int now_min   = last_et_hour_ * 60 + last_et_min_;
        int open_min  = cfg_.session_open_hour * 60 + cfg_.session_open_min;
        int eod_min   = cfg_.eod_flatten_hour * 60 + cfg_.eod_flatten_min;
        if (now_min < open_min || now_min >= eod_min) return false;
        return last_et_hour_ < cfg_.last_entry_hour;
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
    double  stop_price_ = 0.0;
    int64_t entry_time_us_ = 0;
    double  mfe_ = 0.0;
    double  mae_ = 0.0;
    bool    be_moved_ = false;
    bool    trail_armed_ = false;
    bool    sl_dirty_ = false;

    int     pending_dir_ = 0;     // entry waiting for next tick
    std::string pending_exit_reason_;

    int     entries_today_ = 0;
    int64_t cooldown_until_us_ = 0;
    int64_t last_tick_us_ = 0;
    int     last_et_hour_ = 0;
    int     last_et_min_  = 0;
};

} // namespace paper
