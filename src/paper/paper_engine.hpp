#pragma once
/*  ═══════════════════════════════════════════════════════════════════════════
    paper_engine.hpp — account-level risk envelope + session-date helpers

    The AccountEnvelope aggregates realized P&L across all strategies of one
    prop account and enforces the two account-killing rules:
      - daily loss:     day_pnl <= daily_loss_limit        → halt all
      - trailing DD:    equity < peak_equity − cap         → halt all
    Peak equity is tracked from starting_balance (seeded from paper_account on
    restart). Header-only so unit tests can exercise it without the DB.
    ═══════════════════════════════════════════════════════════════════════════ */
#include "log.hpp"
#include "orb_config.hpp"  // us_et_offset

#include <ctime>
#include <string>

namespace paper {

// Session/trade date rolls at 18:00 ET: times before 18:00 ET belong to that
// calendar day, times at/after 18:00 ET belong to the next day.
inline std::string et_trade_date(int64_t ts_us) {
    int64_t ts_sec = ts_us / 1'000'000;
    time_t tt = static_cast<time_t>(ts_sec);
    struct tm tm_utc;
    gmtime_r(&tt, &tm_utc);
    int64_t et = ts_sec - (int64_t)us_et_offset(tm_utc) * 3600;  // ET wall clock as epoch
    int et_hour = (int)((et / 3600) % 24);
    if (et_hour < 0) et_hour += 24;
    if (et_hour >= 18) et += 86400;   // at/after 18:00 ET → belongs to next day
    time_t st = static_cast<time_t>(et);
    struct tm tm_s;
    gmtime_r(&st, &tm_s);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_s);
    return buf;
}

// Start of the current session day (18:00 ET boundary) as micros since epoch.
// All times >= this belong to the current trade_date.
inline int64_t et_day_start_us(int64_t ts_us) {
    int64_t ts_sec = ts_us / 1'000'000;
    time_t tt = static_cast<time_t>(ts_sec);
    struct tm tm_utc;
    gmtime_r(&tt, &tm_utc);
    int off = us_et_offset(tm_utc);
    int64_t et = ts_sec - (int64_t)off * 3600;
    int64_t et_midnight = (et / 86400) * 86400;   // ET day start (in ET-local seconds)
    int et_hour = (int)((et / 3600) % 24);
    if (et_hour < 0) et_hour += 24;
    int64_t boundary_et = et_midnight + 18 * 3600;  // today 18:00 ET
    if (et < boundary_et) boundary_et -= 86400;     // else yesterday 18:00 ET
    return (boundary_et + (int64_t)off * 3600) * 1'000'000;
}

class AccountEnvelope {
public:
    AccountEnvelope(double starting_balance, double daily_loss_limit,
                    double trailing_dd_cap)
        : starting_balance_(starting_balance)
        , daily_loss_limit_(daily_loss_limit)
        , dd_cap_(trailing_dd_cap)
        , total_pnl_(0.0)
        , day_pnl_(0.0)
        , peak_equity_(starting_balance)
    {}

    // Seed from paper_account row / paper_trades history on startup.
    void seed(double total_pnl, double day_pnl, double peak_equity) {
        total_pnl_ = total_pnl;
        day_pnl_   = day_pnl;
        peak_equity_ = peak_equity > starting_balance_ ? peak_equity : starting_balance_;
        double eq = equity();
        if (eq > peak_equity_) peak_equity_ = eq;
        LOG("[ACCOUNT] Seeded total_pnl=%.2f day_pnl=%.2f peak=%.2f equity=%.2f",
            total_pnl_, day_pnl_, peak_equity_, eq);
    }

    // Record a closed trade. Returns a halt reason (non-empty) if this close
    // breached an account-level limit; the caller flattens everything.
    std::string on_trade_close(double pnl_usd) {
        total_pnl_ += pnl_usd;
        day_pnl_   += pnl_usd;
        double eq = equity();
        if (eq > peak_equity_) peak_equity_ = eq;
        return check_limits();
    }

    // Re-evaluate limits against current equity (e.g. periodic check).
    std::string check_limits() const {
        if (daily_loss_limit_ < 0.0 && day_pnl_ <= daily_loss_limit_)
            return "account_daily_loss";
        if (dd_cap_ > 0.0 && equity() < peak_equity_ - dd_cap_)
            return "account_trailing_dd";
        return "";
    }

    void reset_day() {
        day_start_equity_ = equity();
        day_pnl_ = 0.0;
        // daily-loss halt clears at rollover; trailing-DD halt persists.
        if (halt_reason_ == "account_daily_loss") halt_reason_.clear();
        LOG("[ACCOUNT] Day reset — equity=%.2f peak=%.2f", equity(), peak_equity_);
    }

    void set_halt(const std::string& reason) { halt_reason_ = reason; }
    bool halted() const { return !halt_reason_.empty(); }
    const std::string& halt_reason() const { return halt_reason_; }

    double equity()      const { return starting_balance_ + total_pnl_; }
    double peak_equity() const { return peak_equity_; }
    double day_pnl()     const { return day_pnl_; }
    double total_pnl()   const { return total_pnl_; }
    double day_start_equity() const { return day_start_equity_; }
    double starting_balance()   const { return starting_balance_; }

private:
    double starting_balance_;
    double daily_loss_limit_;
    double dd_cap_;
    double total_pnl_;
    double day_pnl_;
    double peak_equity_;
    double day_start_equity_ = 0.0;
    std::string halt_reason_;
};

} // namespace paper
