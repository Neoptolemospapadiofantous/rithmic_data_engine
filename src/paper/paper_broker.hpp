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
#include "paper_quote.hpp"
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
        const int dir = (sig == OrbSignal::BUY) ? 1 : -1;
        const std::string blocked = qs_.gate(cfg_, dir, last_tick_us_, tick_size_);
        log_signal(dir, reason, blocked);
        if (!blocked.empty()) {
            LOG("[PAPER %s] %s signal (%s) blocked by book gate: %s", strategy_id_.c_str(),
                dir > 0 ? "BUY" : "SELL", reason.c_str(), blocked.c_str());
            return;
        }
        pending_dir_ = dir; pending_since_us_ = last_tick_us_;
        LOG("[PAPER %s] %s signal (%s) — pending entry at next tick",
            strategy_id_.c_str(), pending_dir_ > 0 ? "BUY" : "SELL", reason.c_str());
    }

    // ── Top of book (collector bbo stream) ───────────────────────────────────
    void on_quote(const paper::Quote& q) { qs_.on_quote(q, tick_size_); }
    const paper::QuoteState& quotes() const { return qs_; }

    void log_signal(int dir, const std::string& reason, const std::string& blocked) {
        if (!store_) return;
        PaperSignalRow r;
        r.strategy_id = strategy_id_; r.account_label = account_label_;
        r.ts_us = last_tick_us_; r.direction = dir > 0 ? "LONG" : "SHORT"; r.price = last_price_;
        r.reason = reason; r.decision = blocked.empty() ? "taken" : "blocked:" + blocked;
        if (qs_.fresh(last_tick_us_)) {
            r.spread_ticks = qs_.q.spread_ticks(tick_size_); r.imbalance = qs_.q.imbalance();
            r.microprice_dev_ticks = tick_size_ > 0 ? (qs_.q.microprice() - qs_.q.mid()) / tick_size_ : 0.0;
            r.spread_rel = qs_.spread_ema > 0 ? r.spread_ticks / qs_.spread_ema : -1.0;
        }
        store_->record_signal(r);
    }

    // ── Tick entry point — call BEFORE strategy.on_tick() ────────────────────
    void on_tick(const OrbTick& t) {
        last_tick_us_ = t.ts_micros;
        last_price_   = t.price;
        utc_to_et(t.ts_micros, last_et_hour_, last_et_min_);

        // 1. Pending entry fills at first tick after the signal (adverse slip).
        if (pending_dir_ != 0 && pos_dir_ == 0 &&
            !(cfg_.fill_wait_secs > 0 && t.ts_micros - pending_since_us_ < (int64_t)cfg_.fill_wait_secs * 1'000'000 &&
              !qs_.fill_ready(pending_dir_, t.ts_micros, tick_size_))) {           // fill timing: wait for a better book
            int dir = pending_dir_;
            pending_dir_ = 0;
            const double slip_fill = t.price + dir * slip_;
            const double book_fill = qs_.market_fill(dir, t.ts_micros, slip_fill, t.price);   // never better than the print (stale-quote guard)
            const bool   use_book  = cfg_.fill_model == "bbo" && qs_.fresh(t.ts_micros);
            entry_bbo_    = book_fill;
            spread_entry_ = qs_.fresh(t.ts_micros) ? qs_.q.spread_ticks(tick_size_) : -1.0;
            enter(dir, use_book ? book_fill : slip_fill, t.ts_micros);
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
        sync_placed_stop();
        bool stopped = (pos_dir_ > 0) ? (t.price <= placed_stop_)
                                      : (t.price >= placed_stop_);
        if (stopped) {
            double fill = placed_stop_ - pos_dir_ * slip_;
            exit_position(fill, t.ts_micros, stop_exit_reason());
            return;
        }

        // 5b. Book-driven management (paper_quote.hpp): flip exit / take-profit into
        //     pressure / break-even on flip. Market fills, adverse slip.
        {
            const double pnl_now = (t.price - entry_price_) * pos_dir_;
            const std::string why = qs_.book_exit(cfg_, pos_dir_, t.ts_micros, pnl_now);
            if (!why.empty()) { exit_position(t.price - pos_dir_ * slip_, t.ts_micros, why); return; }
            // Break-even on book flip: only while the trade is already beyond the BE level
            // (price − entry ≥ be_offset for a long). A stop can never be placed on the
            // wrong side of the market — that would fill at a price better than reality.
            if (cfg_.book_be_on_flip && !be_moved_ && qs_.flipped_against(pos_dir_, t.ts_micros) &&
                (t.price - entry_price_) * pos_dir_ > cfg_.trail_be_offset) {
                double be = snap(entry_price_ + pos_dir_ * cfg_.trail_be_offset, pos_dir_);
                if ((pos_dir_ > 0 && be > stop_price_) || (pos_dir_ < 0 && be < stop_price_)) {
                    stop_price_ = be; be_moved_ = true; sl_dirty_ = true; sync_placed_stop();
                    LOG("[PAPER %s] BE move on book flip — stop=%.2f", strategy_id_.c_str(), stop_price_);
                }
            }
        }

        // 6. MFE / BE / trail ratchet.
        double mfe_now = (pos_dir_ > 0) ? (t.price - entry_price_)
                                        : (entry_price_ - t.price);
        if (mfe_now > mfe_) mfe_ = mfe_now;
        if (mfe_now < mae_) mae_ = mfe_now;   // adverse excursion (≤ 0)

        // Same rules as the live executor (order_manager.hpp check_trail_and_stop):
        //   break-even is IMMEDIATE — as soon as MFE ≥ trail_be_trigger the stop moves to
        //   entry ± trail_be_offset (no delay); the trail activates only after
        //   trail_delay_secs AND MFE ≥ trail_be_trigger, independent of BE.
        int64_t elapsed_s = (t.ts_micros - entry_time_us_) / 1'000'000;
        if (!be_moved_ && mfe_ >= cfg_.trail_be_trigger) {
            double be = snap(entry_price_ + pos_dir_ * cfg_.trail_be_offset, pos_dir_);
            if ((pos_dir_ > 0 && be > stop_price_) || (pos_dir_ < 0 && be < stop_price_)) {
                stop_price_ = be;
                sl_dirty_ = true;
                sync_placed_stop();    // live re-places the exchange stop right here (16 pts ≥ trail_step)
                LOG("[PAPER %s] BE move — stop=%.2f (entry%+.1f, mfe=%.2f)",
                    strategy_id_.c_str(), stop_price_, pos_dir_ * cfg_.trail_be_offset, mfe_);
            }
            be_moved_ = true;          // evaluated once, like the live executor's be_triggered
        }
        if (!trail_armed_ && elapsed_s >= cfg_.trail_delay_secs &&
            mfe_ >= cfg_.trail_be_trigger) {
            trail_armed_ = true;
            LOG("[PAPER %s] Trail armed (elapsed=%lds mfe=%.2f)",
                strategy_id_.c_str(), (long)elapsed_s, mfe_);
        }

        if (trail_armed_) {
            // Trail ratchet: stop → price ∓ trail_step, only tightening.
            double cand = t.price - pos_dir_ * cfg_.trail_step;
            cand = snap(cand, pos_dir_);
            if ((pos_dir_ > 0 && cand > stop_price_) ||
                (pos_dir_ < 0 && cand < stop_price_)) {
                if (cand != stop_price_) {
                    stop_price_ = cand;
                    sl_dirty_ = true;
                    sync_placed_stop();  // re-placed only when ≥ trail_step beyond the working stop
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
        placed_stop_   = stop;
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
    double placed_stop()  const { return placed_stop_; }   // the working (exchange-equivalent) stop
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
        qty_           = cfg_.qty + ((cfg_.book_size_agree > 0 && qs_.fresh(ts_us) && qs_.agrees(dir)) ? cfg_.book_size_agree : 0);
        entry_price_   = fill;
        entry_time_us_ = ts_us;
        stop_price_    = snap(fill - dir * cfg_.sl_points, dir);
        placed_stop_   = stop_price_;
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

    // Mirror of the live suppression: the working stop moves only when the in-memory
    // level is ≥ trail_step beyond it (the BE move from −sl_points always qualifies).
    void sync_placed_stop() {
        if (placed_stop_ <= 0.0) { placed_stop_ = stop_price_; return; }
        if (std::fabs(stop_price_ - placed_stop_) >= cfg_.trail_step - 1e-9) placed_stop_ = stop_price_;
    }
    std::string stop_exit_reason() const {
        double be = entry_price_ + pos_dir_ * cfg_.trail_be_offset;
        if (std::fabs(placed_stop_ - snap(be, pos_dir_)) < 1e-9) return "breakeven";
        bool past_be = (pos_dir_ > 0) ? (placed_stop_ > be + 1e-9)
                                      : (placed_stop_ < be - 1e-9);
        return past_be ? "trail" : "stop";
    }

    void exit_position(double fill, int64_t ts_us, const std::string& reason) {
        // Book shadow: a market/stop exit fills at the touch (sell at bid / buy
        // at ask), never better than the stop-based price handed in.
        const bool   fresh    = qs_.fresh(ts_us);
        const double exit_bbo = fresh ? qs_.market_fill(-pos_dir_, ts_us, fill, fill) : fill;
        if (cfg_.fill_model == "bbo" && fresh) fill = exit_bbo;
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
        tr.fill_model    = cfg_.fill_model;
        tr.entry_bbo     = entry_bbo_ > 0.0 ? entry_bbo_ : entry_price_;
        tr.exit_bbo      = exit_bbo;
        tr.pnl_bbo_usd   = (tr.exit_bbo - tr.entry_bbo) * pos_dir_ * cfg_.point_value * qty_ - tr.commission;
        tr.spread_entry_ticks = spread_entry_;
        tr.spread_exit_ticks  = fresh ? qs_.q.spread_ticks(tick_size_) : -1.0;
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
    paper::QuoteState qs_;                       // top of book (paper_quote.hpp)
    double      entry_bbo_ = 0.0, spread_entry_ = -1.0;   // shadow fill of the open leg
    double      last_price_ = 0.0;               // last tick price (signal log)
    // The stop that would be WORKING at the exchange. The live executor moves its resting
    // stop only when the in-memory level is ≥ trail_step beyond the last placed one
    // (cancel/resubmit suppression, order_manager.hpp update_stop_order_locked), so
    // exits are evaluated against this, not against stop_price_ (display value).
    double      placed_stop_ = 0.0;
    int64_t     pending_since_us_ = 0;           // signal time of the pending entry (fill_wait_secs)
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
