static payout::RunOut run_paced(const payout::Plan& p, const std::vector<const std::vector<payout::Trade>*>& day_trades,
                                int s0, int horizon, double reserve, const std::vector<int>& month_of) {
    payout::RunOut o;
    double bal = p.start, peak = p.start, floor = p.start - p.drawdown;
    const double lock = p.start + p.lock_above_start; const double cut = p.daily_loss;
    auto trail = [&](double e) { peak = std::max(peak, e); floor = std::max(floor, std::min(peak - p.drawdown, lock)); };
    int wins = 0; double best = 0, cyc = 0; int last_m = -1;
    for (int k = 0; k < horizon && s0 + k < (int)day_trades.size(); ++k) {
        const auto* tr = day_trades[s0 + k]; double dp = 0;
        if (tr) for (const payout::Trade& t : *tr) {
            if (p.intraday_trail && t.mfe > 0) trail(bal + dp + t.mfe);
            const double worst = std::min(t.mae, t.pnl);
            if (bal + dp + worst <= floor) { o.breach = true; o.days = k + 1; return o; }
            if (cut > 0 && dp + worst <= -cut) { dp = -cut; break; }
            dp += t.pnl; if (p.intraday_trail) trail(bal + dp);
        }
        bal += dp; o.days = k + 1; if (dp >= p.win_day_min) ++wins; cyc += dp; best = std::max(best, dp); trail(bal);
        const double profit = bal - p.start;
        const bool consistent = p.consistency <= 0 || (cyc > 0 && best <= p.consistency * cyc);
        const int mo = month_of[s0 + k];
        if (mo != last_m && wins >= p.win_days_needed && profit > 0 && consistent && (o.payouts == 0 || cyc > 0)) {
            const double cap = p.caps[std::min<size_t>(o.payouts, p.caps.size() - 1)];
            double amt = 0;
            if (p.payout_mult > 0) { if (profit > p.buffer + reserve && cyc > 0) amt = std::min({cap, p.payout_mult * cyc, profit - p.buffer - reserve}); if (amt < p.min_payout) amt = 0; }
            else if (profit >= p.min_profit + reserve) amt = std::min({cap, p.profit_share * profit, profit - reserve});
            if (amt > 0) { bal -= amt; o.paid_sum += amt; ++o.payouts; o.pay_days.push_back(k + 1); last_m = mo; wins = 0; best = 0; cyc = 0; peak = bal; }
        }
    }
    return o;
}
