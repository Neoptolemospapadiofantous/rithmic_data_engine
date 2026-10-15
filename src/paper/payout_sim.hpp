// payout_sim.hpp — runs a strategy's real trade history through a prop-firm funded account's payout rules.
//
// Question it answers: "if we put strategy X (optionally only on regime days) on a Tradeify 25K/50K funded
// account at size N, how often do we get paid within 1–2 weeks, and how often do we blow the account first?"
//
// Model (per start day, horizon trading days, never across a data gap; days are prop-firm trading days,
// 18:00 → 17:00 ET):
//   - trades are applied in order with their worst open loss (MAE) and best open profit (MFE): the account
//     breaches when equity at a trade's MAE touches the floor; for an intraday-trailing plan the MFE is taken
//     FIRST (it raises the floor before the MAE is tested — the paper rows do not record which came first,
//     so this is the conservative order);
//   - daily loss limit (if > 0) is soft: a trade whose MAE takes the day to −limit is closed there and the
//     day stops;
//   - floor trails the high by `drawdown` (end-of-day balance, or intraday equity incl. open profit when
//     `intraday_trail`) and stops trailing at start + `lock_above_start`;
//   - a winning day is day P&L ≥ `win_day_min`; a payout is requested at the first end of day where
//       winning days since the last payout ≥ `win_days_needed`,
//       balance − start ≥ `min_profit`,
//       and (consistency > 0) best day of the cycle ≤ consistency × cycle profit;
//     amount = min(cap for this payout number, `profit_share` × (balance − start)); the cycle then resets.
// Plan numbers are from third-party summaries of Tradeify's rules (2026-10) plus stated assumptions
// (profit_share 0.5, floor lock at start + 100, Select Flex trails end-of-day, Growth trails intraday as our
// own Growth 25K does) — check them against the dashboard and edit plans().
#pragma once
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace payout {

struct Plan {
    std::string name;
    double start;
    double drawdown;
    double lock_above_start;   // floor stops trailing at start + this
    double daily_loss;         // 0 = none (soft limit: close at the limit, stop for the day)
    double win_day_min;
    int    win_days_needed;
    double min_profit;         // balance − start needed to request
    double consistency;        // 0 = none; best cycle day ≤ consistency × cycle profit
    double profit_share;       // fraction of (balance − start) withdrawable
    std::vector<double> caps;  // cap for payout 1, 2, …; last value repeats
    bool   intraday_trail;     // floor follows intraday equity incl. open profit (else end-of-day balance)
    // Select Daily policy (help.tradeify.co, read 2026-10-11): payouts any day the balance is above start + `buffer`,
    // amount = payout_mult × the cycle's profit, never below the buffer, capped, and at least `min_payout`.
    // payout_mult 0 = the share model above (Flex / Growth).
    double buffer = 0, payout_mult = 0, min_payout = 0;
    int    max_minis = 0;      // contract limit in minis (10 micros = 1 mini); 0 = unlimited
    // Evaluation phase of the same account family (run_eval): profit target, best-day consistency cap (0 = none),
    // minimum trading days, daily loss limit (soft, 0 = none). The drawdown / lock are the plan's.
    double eval_target = 0, eval_consistency = 0; int eval_min_days = 1; double eval_daily_loss = 0;
};

// Rules read from help.tradeify.co on 2026-10-11 (Select Evaluation Accounts; Select Flex and Select Daily Payout
// Policies; Essential Trading Rules Overview). Flex caps are the post-2026-09-01 schedule. All positions must be flat by
// 16:45 ET; a trading day runs 18:00 → 17:00 ET. Growth funded rules stay from the earlier third-party summary.
inline const std::vector<Plan>& plans() {
    static const std::vector<Plan> kPlans = {
        {"select_flex_25k",  25000, 1000, 100, 0,    100, 5, 0,    0,    0.5, {1250}, false, 0,    0, 0,   1, 1500, 0.40, 3, 0},
        {"select_flex_50k",  50000, 2000, 100, 0,    150, 5, 0,    0,    0.5, {2500}, false, 0,    0, 0,   4, 3000, 0.40, 3, 0},
        {"select_daily_25k", 25000, 1000, 100, 500,  0,   0, 0,    0,    0,   {600},  false, 1100, 2, 250, 1, 1500, 0.40, 3, 0},
        {"select_daily_50k", 50000, 2000, 100, 1000, 0,   0, 0,    0,    0,   {1250}, false, 2100, 2, 250, 4, 3000, 0.40, 3, 0},
        {"growth_25k",       25000, 1000, 100, 600,  100, 5, 1500, 0.35, 0.5, {1000}, true,  0,    0, 0,   1, 1500, 0,    1, 600},
        {"growth_50k",       50000, 2000, 100, 1250, 150, 5, 3000, 0.35, 0.5, {1500, 2000, 2500, 3000}, true, 0, 0, 0, 4, 3000, 0, 1, 1250},
        // Other firms, re-read 2026-10-12 from each firm's own help centre (Apex blocks automated reads — its numbers are
        // search excerpts of apextraderfunding.com/help-center pages). Marked ASSUMED where the firm does not state it.
        // MFF Rapid 50K (help.myfundedfutures.com): eval $3K, $2K EOD, 50 % consistency, 5 minis; funded: $2K INTRADAY trail
        // locking at +100, no DLL, no consistency, payouts any day above a $2,100 buffer, min $500, 90/10; cap ASSUMED 2,000.
        {"mff_rapid_50k",    50000, 2000, 100, 0,    0,   0, 0,    0,    0,   {2000}, true,  2100, 100, 500, 5, 3000, 0.50, 1, 0},
        // MFF Rapid EOD 50K: eval $3K, $2K EOD, 30 % consistency, 4 min days, 3 minis; funded EOD trail locking at +100 once
        // +$2,100, $2,100 buffer before the first payout, ≥ $500 per later payout, no DLL; cap ASSUMED 2,000.
        {"mff_rapid_eod_50k",50000, 2000, 100, 0,    0,   0, 0,    0,    0,   {2000}, false, 2100, 100, 500, 3, 3000, 0.30, 4, 0},
        // Apex 50K EOD (help-center "EOD Drawdown Explained" / "EOD Payouts"): $3K target, $2K EOD trail, PA floor stops at
        // $50,100, no eval consistency; payouts: 5 days ≥ $250, balance ≥ $52,600 (min_profit 2,600), best day < 50 % of
        // profit since the last payout; withdrawable share ASSUMED 0.5, cap ASSUMED 2,000. Flat by 16:59 ET.
        {"apex_eod_50k",     50000, 2000, 100, 0,    250, 5, 2600, 0.50, 0.5, {2000}, false, 0,    0, 0,   10, 3000, 0,    1, 0},
        // Apex 50K Intraday: same with the trail following intraday equity and qualifying days ≥ $200.
        {"apex_intraday_50k",50000, 2000, 100, 0,    200, 5, 2600, 0.50, 0.5, {2000}, true,  0,    0, 0,   10, 3000, 0,    1, 0},
        // Topstep 50K (topstep.com/express-funded-account-rules + help.topstep.com): Combine $3K target, $2K MLL trailing the
        // EOD balance and locking at the start, 50 % consistency target, optional $1K DLL (modelled on); XFA: $2K MLL, 5
        // winning days ≥ $150, each payout ≤ 50 % of the balance, cap $5K, 90/10 → share 0.45. Flat by 15:10 CT (16:10 ET).
        {"topstep_50k",      50000, 2000, 0,   1000, 150, 5, 0,    0,    0.45, {5000}, false, 0,    0, 0,   5, 3000, 0.50, 1, 1000},
        // Lucid LucidFlex 50K (support.lucidtrading.com): eval $3K, $2K EOD, 50 % consistency, ~2 min days; funded EOD trail
        // $2K (lock ASSUMED +100), no consistency, no DLL modelled (optional), 5 days ≥ $150 with a positive cycle, each
        // payout ≤ 50 % of profit, cap $2,000, min $500, 90/10 → share 0.45; 4 minis.
        {"lucid_flex_50k",   50000, 2000, 100, 0,    150, 5, 500,  0,    0.45, {2000}, false, 0,    0, 0,   4, 3000, 0.50, 2, 0},
    };
    return kPlans;
}

// One closed trade, already scaled to the size: net P&L (costs in); worst (≤ 0) / best (≥ 0) open P&L.
struct Trade { double pnl; double mae; double mfe; };

struct Result {
    int    starts = 0;
    int    paid_by_5 = 0, paid_by_10 = 0, paid_by_20 = 0;   // first payout within N trading days
    int    breached_before_pay = 0, breached = 0;
    double paid_total = 0;           // summed over starts
    double payouts_total = 0;        // count, summed over starts
    double days_total = 0;           // trading days survived, summed over starts
    std::vector<int> first_pay_day;  // per start that got paid
};

// Our own daily risk rules on top of the plan's (0 = off). Stop for the day once the day is up `profit_stop`
// (a winning day banked) or down `loss_stop` (a trade whose MAE gets there is closed at it), or after
// `max_trades` trades. Later trades that day are not taken.
struct Rules { double profit_stop = 0; double loss_stop = 0; int max_trades = 0; };

struct RunOut { bool paid = false; int first_pay = -1; bool breach = false; double paid_sum = 0; int payouts = 0; int days = 0; std::vector<int> pay_days; };

// One account run from calendar index `s0` for at most `horizon` trading days.
// `day_trades` maps calendar index → that day's trades (nullptr = no trades that day).
inline RunOut run_account(const Plan& p, const std::vector<const std::vector<Trade>*>& day_trades,
                          int s0, int horizon, const Rules& r = Rules{}) {
    RunOut o;
    double cut = p.daily_loss;                                   // the tighter of the plan's limit and ours
    if (r.loss_stop > 0 && (cut <= 0 || r.loss_stop < cut)) cut = r.loss_stop;
    double bal = p.start, peak = p.start, floor = p.start - p.drawdown;
    const double lock = p.start + p.lock_above_start;
    auto trail = [&](double equity) {
        peak = std::max(peak, equity);
        floor = std::max(floor, std::min(peak - p.drawdown, lock));
    };
    int wins = 0; double best = 0, cyc = 0;
    for (int k = 0; k < horizon && s0 + k < (int)day_trades.size(); ++k) {
        const auto* tr = day_trades[s0 + k];
        double dp = 0; int taken = 0;
        if (tr) for (const Trade& t : *tr) {
            if (r.max_trades > 0 && taken >= r.max_trades) break;
            if (r.profit_stop > 0 && dp >= r.profit_stop) break;
            ++taken;
            if (p.intraday_trail && t.mfe > 0) trail(bal + dp + t.mfe);   // open profit first (conservative)
            const double worst = std::min(t.mae, t.pnl);
            if (bal + dp + worst <= floor) { o.breach = true; o.days = k + 1; return o; }
            if (cut > 0 && dp + worst <= -cut) { dp = -cut; break; }
            dp += t.pnl;
            if (p.intraday_trail) trail(bal + dp);
        }
        bal += dp; o.days = k + 1;
        if (dp >= p.win_day_min) ++wins;
        cyc += dp; best = std::max(best, dp);
        trail(bal);
        const double profit = bal - p.start;
        const bool consistent = p.consistency <= 0 || (cyc > 0 && best <= p.consistency * cyc);
        // every payout after the first needs a positive cycle (Tradeify: "shared rule for all subsequent payouts")
        if (wins >= p.win_days_needed && profit > 0 && consistent && (o.payouts == 0 || cyc > 0)) {
            const double cap = p.caps[std::min<size_t>(o.payouts, p.caps.size() - 1)];
            double amt = 0;
            if (p.payout_mult > 0) {                                           // Select Daily
                if (profit > p.buffer && cyc > 0) amt = std::min({cap, p.payout_mult * cyc, profit - p.buffer});
                if (amt < p.min_payout) amt = 0;
            } else if (profit >= p.min_profit) amt = std::min(cap, p.profit_share * profit);
            if (amt > 0) {
                bal -= amt; o.paid_sum += amt; ++o.payouts; o.pay_days.push_back(k + 1);
                if (!o.paid) { o.paid = true; o.first_pay = k + 1; }
                wins = 0; best = 0; cyc = 0; peak = bal;
            }
        }
    }
    return o;
}

// Evaluation phase: EOD-trailing drawdown enforced live at each trade's MAE (Tradeify: "touching it fails the account
// immediately"), floor locks at start + lock_above_start, soft daily loss, pass when balance − start ≥ eval_target on
// ≥ eval_min_days trading days with the best day ≤ eval_consistency × total profit.
struct EvalOut { bool passed = false, failed = false; int days = 0; };
// boost > 1 trades `boost`× the size until the profit reaches boost_until (two-stage eval: bigger early, normal after).
inline EvalOut run_eval(const Plan& p, const std::vector<const std::vector<Trade>*>& day_trades, int s0, int horizon,
                        const Rules& r = Rules{}, double boost = 1.0, double boost_until = 0.0) {
    EvalOut o;
    double cut = p.eval_daily_loss;
    if (r.loss_stop > 0 && (cut <= 0 || r.loss_stop < cut)) cut = r.loss_stop;
    double bal = p.start, peak = p.start, floor = p.start - p.drawdown, best = 0;
    const double lock = p.start + p.lock_above_start;
    for (int k = 0; k < horizon && s0 + k < (int)day_trades.size(); ++k) {
        const auto* tr = day_trades[s0 + k];
        double dp = 0; int taken = 0;
        const double m = (boost > 1.0 && bal - p.start < boost_until) ? boost : 1.0;   // stage decided at the open
        if (tr) for (const Trade& t : *tr) {
            if (r.max_trades > 0 && taken >= r.max_trades) break;
            if (r.profit_stop > 0 && dp >= r.profit_stop) break;
            ++taken;
            const double worst = m * std::min(t.mae, t.pnl);
            if (bal + dp + worst <= floor) { o.failed = true; o.days = k + 1; return o; }
            if (cut > 0 && dp + worst <= -cut) { dp = -cut; break; }
            dp += m * t.pnl;
        }
        bal += dp; o.days = k + 1; best = std::max(best, dp);
        peak = std::max(peak, bal); floor = std::max(floor, std::min(peak - p.drawdown, lock));
        const double profit = bal - p.start;
        if (profit >= p.eval_target && o.days >= p.eval_min_days && (p.eval_consistency <= 0 || best <= p.eval_consistency * profit)) {
            o.passed = true; return o;
        }
    }
    return o;
}

// The paper fleet trades MNQ (point_value 2): its P&L is per MNQ contract. NQ = 10× the gross, one commission.
inline double to_nq(double mnq_pnl, double commission_rt) { return (mnq_pnl + commission_rt) * 10.0 - commission_rt; }

}  // namespace payout
