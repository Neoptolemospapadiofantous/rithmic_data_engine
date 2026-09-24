#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    risk_manager.hpp — Legends 50K risk enforcement in C++

    Rules enforced:
      1. Trailing drawdown cap: equity must not fall more than $2500 below
         the peak equity reached since account inception (rolling).
         Note: this is from the PEAK, not from starting capital.
      2. Consistency cap: no single day can account for more than 30% of
         total cumulative profit.
      3. Max daily trades (handled by OrbStrategy — risk_manager just enforces
         the hard stop on dollar loss).
      4. Max daily loss (implicit via trailing drawdown — once equity drops
         $2500 from peak, halt for the day).

    All state is updated via on_fill_pnl() after each trade closes.
    check_limits() is called before each new signal is accepted.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "orb_config.hpp"
#include "log.hpp"
#include <atomic>
#include <cmath>
#include <mutex>
#include <string>

class RiskManager {
public:
    explicit RiskManager(const OrbConfig& cfg, double starting_equity = 50000.0)
        : cfg_(cfg)
        , equity_(starting_equity)
        , peak_equity_(starting_equity)
        , total_profit_(0.0)
        , daily_pnl_(0.0)
        , halted_(false)
    {}

    // ── Seed historical P&L from DB on startup (call once after DB connect) ──
    void seed_total_profit(double historical_pnl) {
        std::lock_guard<std::mutex> lk(mu_);
        total_profit_ = historical_pnl;
        LOG("[RISK] Seeded total_profit=%.2f from DB", total_profit_);
    }

    // ── Seed today's realized P&L from DB on startup (call once after DB connect) ──
    // Required for intra-day restarts (cycle mode restarts deliberately): without
    // this, a process that already lost money today restarts with daily_pnl_=0 and
    // can lose the full daily limit again. It also keeps the consistency-cap
    // "prior profit" (total_profit_ - daily_pnl_) correct: get_total_pnl() already
    // includes today's trades, so seeding daily_pnl_ excludes them from "prior".
    void seed_daily_pnl(double today_pnl) {
        std::lock_guard<std::mutex> lk(mu_);
        daily_pnl_ = today_pnl;
        LOG("[RISK] Seeded daily_pnl=%.2f from DB", daily_pnl_);
    }

    // ── Called at start of each trading day ───────────────────────────────────
    // Clears the daily P&L accumulator. Halts are cleared ONLY when their reason
    // is daily_loss_limit — a trailing-drawdown or consistency-cap breach is an
    // account-level (prop-firm account-killing) event and must survive midnight
    // until clear_halt() is invoked manually.
    void reset_daily() {
        std::lock_guard<std::mutex> lk(mu_);
        daily_pnl_ = 0.0;
        if (halted_) {
            if (halt_reason_.rfind("daily_loss_limit", 0) == 0) {
                halted_ = false;
                halt_reason_.clear();
                LOG("[RISK] Daily reset — daily_loss_limit halt cleared");
            } else {
                LOG("[RISK] Daily reset — halt persists (%s); manual clear_halt() required",
                    halt_reason_.c_str());
            }
        }
        LOG("[RISK] Daily reset — equity=%.2f peak=%.2f total_profit=%.2f",
            equity_, peak_equity_, total_profit_);
    }

    // ── Manual halt reset (operator action after reviewing a drawdown/consistency halt) ──
    void clear_halt() {
        std::lock_guard<std::mutex> lk(mu_);
        if (!halted_) return;
        LOG("[RISK] Manual halt clear — previous reason: %s", halt_reason_.c_str());
        halted_ = false;
        halt_reason_.clear();
    }

    // ── Called after each trade closes ────────────────────────────────────────
    // pnl_usd: positive = profit, negative = loss (after commissions)
    void on_trade_pnl(double pnl_usd) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!std::isfinite(pnl_usd)) {
            halt("non_finite_pnl: " + std::to_string(pnl_usd));
            return;
        }

        equity_       += pnl_usd;
        daily_pnl_    += pnl_usd;
        total_profit_ += pnl_usd;

        // Update rolling equity peak
        if (equity_ > peak_equity_) peak_equity_ = equity_;

        // Check daily loss limit (Legends: -$1000/day)
        if (cfg_.daily_loss_limit < 0.0 && daily_pnl_ <= cfg_.daily_loss_limit) {
            halt("daily_loss_limit: daily_pnl $" +
                 std::to_string(daily_pnl_) + " <= limit $" +
                 std::to_string(cfg_.daily_loss_limit));
            return;
        }

        // Check trailing drawdown from peak
        double drawdown = peak_equity_ - equity_;
        if (drawdown >= cfg_.trailing_drawdown_cap) {
            halt("trailing_drawdown_cap: drawdown $" +
                 std::to_string(drawdown) + " >= cap $" +
                 std::to_string(cfg_.trailing_drawdown_cap));
            return;
        }

        // Consistency cap: today's profit must not exceed 30% of prior cumulative profit.
        // Skipped in cycle_mode (each cycle is a short independent session, not a full
        // trading day — the cap is meaningless when daily_pnl_ resets every 20 minutes).
        // Also skipped when prior history is thin (<$500) to avoid test-trade false fires.
        double prior_profit = total_profit_ - daily_pnl_;
        if (!cfg_.cycle_mode && prior_profit >= 500.0 && daily_pnl_ > 0.0) {
            double daily_fraction = daily_pnl_ / prior_profit;
            if (daily_fraction > cfg_.consistency_cap_pct) {
                halt("consistency_cap: today=" + std::to_string(daily_pnl_)
                     + " / prior=" + std::to_string(prior_profit)
                     + " = " + std::to_string(daily_fraction * 100.0)
                     + "% (cap " + std::to_string(cfg_.consistency_cap_pct * 100.0) + "%)");
                return;
            }
        }

        LOG("[RISK] Trade P&L: %.2f | daily=%.2f | equity=%.2f | peak=%.2f | dd=%.2f",
            pnl_usd, daily_pnl_, equity_, peak_equity_, peak_equity_ - equity_);
    }

    // ── Check before accepting a new signal ───────────────────────────────────
    // Returns true if trading is permitted.
    bool can_trade(std::string& reason) const {
        std::lock_guard<std::mutex> lk(mu_);
        if (halted_) {
            reason = halt_reason_;
            return false;
        }
        // Re-check daily loss limit
        if (cfg_.daily_loss_limit < 0.0 && daily_pnl_ <= cfg_.daily_loss_limit) {
            reason = "daily_loss_limit active";
            return false;
        }
        // Re-check drawdown in real time — includes unrealized P&L on any open
        // position fed via update_unrealized(), so a deep open loss trips the
        // trailing-drawdown gate before a new signal is accepted.
        double drawdown = peak_equity_ - (equity_ + unrealized_pnl_);
        if (drawdown >= cfg_.trailing_drawdown_cap) {
            reason = "trailing_drawdown_cap active";
            return false;
        }
        return true;
    }

    bool can_trade() const {
        std::string ignored;
        return can_trade(ignored);
    }

    // ── Feed unrealized P&L of the open position (called from the tick path) ──
    // Moves the effective equity used by can_trade()'s drawdown check without
    // touching realized equity_ or peak_equity_. Call with 0.0 when flat.
    // Does not trigger halts by itself; it gates NEW entries via can_trade().
    void update_unrealized(double unrealized_pnl) {
        std::lock_guard<std::mutex> lk(mu_);
        if (!std::isfinite(unrealized_pnl)) {
            LOG("[RISK] Ignoring non-finite unrealized P&L update");
            return;
        }
        unrealized_pnl_ = unrealized_pnl;
    }

    // ── Update equity directly (e.g. from unrealised P&L on open position) ───
    // Not used for halt checking — only on_trade_pnl() triggers halts.
    void set_equity(double eq) {
        std::lock_guard<std::mutex> lk(mu_);
        equity_ = eq;
        if (eq > peak_equity_) peak_equity_ = eq;
    }

    double equity()       const { std::lock_guard<std::mutex> lk(mu_); return equity_; }
    double peak_equity()  const { std::lock_guard<std::mutex> lk(mu_); return peak_equity_; }
    double daily_pnl()    const { std::lock_guard<std::mutex> lk(mu_); return daily_pnl_; }
    double total_profit() const { std::lock_guard<std::mutex> lk(mu_); return total_profit_; }
    bool   halted()       const { return halted_.load(std::memory_order_acquire); }
    std::string halt_reason() const { std::lock_guard<std::mutex> lk(mu_); return halt_reason_; }

    struct Snapshot { double equity; double peak_equity; double daily_pnl; };
    Snapshot snapshot() const {
        std::lock_guard<std::mutex> lk(mu_);
        return { equity_, peak_equity_, daily_pnl_ };
    }

    // Halt on evidence this manager cannot see itself — the broker's own day P&L
    // (tid=451), which includes positions our trade log never recorded. Persists
    // like any other risk halt (live_sessions.risk_halted).
    void halt_external(const std::string& reason) {
        std::lock_guard<std::mutex> lk(mu_);
        halt(reason);
    }

private:
    void halt(const std::string& reason) {
        halted_      = true;
        halt_reason_ = reason;
        LOG("[RISK] HALTED — %s", reason.c_str());
    }

    OrbConfig   cfg_;
    mutable std::mutex mu_;

    double       equity_;
    double       peak_equity_;
    double       total_profit_;
    double       daily_pnl_;
    double       unrealized_pnl_ = 0.0;
    std::atomic<bool> halted_;
    std::string  halt_reason_;
};
