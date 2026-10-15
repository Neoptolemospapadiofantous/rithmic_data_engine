// test_payout_sim — the funded-account payout model in src/paper/payout_sim.hpp.
#include "payout_sim.hpp"

#include <cmath>
#include <cstdio>

static int fails = 0, checks = 0;
#define CHECK(c) do { ++checks; if (!(c)) { ++fails; std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

using payout::Plan;
using payout::Trade;

// Days of plain trades: open P&L never worse / better than the close.
static std::vector<std::vector<Trade>> plain(const std::vector<std::vector<double>>& d) {
    std::vector<std::vector<Trade>> out;
    for (auto& day : d) {
        out.emplace_back();
        for (double p : day) out.back().push_back({p, std::min(p, 0.0), std::max(p, 0.0)});
    }
    return out;
}
static std::vector<const std::vector<Trade>*> ptrs(const std::vector<std::vector<Trade>>& d) {
    std::vector<const std::vector<Trade>*> p;
    for (auto& v : d) p.push_back(v.empty() ? nullptr : &v);
    return p;
}

int main() {
    const Plan flex{"flex", 50000, 2000, 100, 0, 150, 5, 0, 0, 0.5, {3000}, false};

    {   // five winning days → payout of half the profit on day 5
        auto d = plain(std::vector<std::vector<double>>(10, {200}));
        const auto o = payout::run_account(flex, ptrs(d), 0, 10);
        CHECK(o.paid && o.first_pay == 5);
        CHECK(std::fabs(o.paid_sum - (500 + 750)) < 1e-9);   // 1000 profit → 500; then 5 more days → (500+1000)·0.5
        CHECK(o.payouts == 2 && !o.breach);
    }
    {   // a day below the winning threshold does not count
        auto d = plain({{200}, {100}, {200}, {200}, {200}, {200}});
        const auto o = payout::run_account(flex, ptrs(d), 0, 6);
        CHECK(o.paid && o.first_pay == 6);
    }
    {   // closed-trade equity touching the floor breaches mid-day
        auto d = plain({{-1500, -600, 5000}});
        const auto o = payout::run_account(flex, ptrs(d), 0, 5);
        CHECK(o.breach && !o.paid && o.days == 1);
    }
    {   // an open loss (MAE) through the floor breaches even if the trade closes green
        std::vector<std::vector<Trade>> d = {{{300, -2100, 400}}};
        const auto o = payout::run_account(flex, ptrs(d), 0, 1);
        CHECK(o.breach);
    }
    {   // end-of-day floor trails the close and locks at start + 100
        auto d = plain({{3000}, {-2000}, {-1000}});   // peak 53000 → floor min(51000, 50100) = 50100
        const auto o = payout::run_account(flex, ptrs(d), 0, 3);
        CHECK(o.breach && o.days == 3);   // 51000 after day 2, 50000 ≤ 50100 on day 3
    }
    {   // intraday trailing: open profit raises the floor before the trade gives it back
        const Plan g{"g", 25000, 1000, 100, 0, 100, 5, 0, 0, 0.5, {1000}, true};
        std::vector<std::vector<Trade>> d = {{{-50, -50, 900}, {-200, -200, 0}}};   // peak 25900 → floor 24900
        const auto intraday = payout::run_account(g, ptrs(d), 0, 1);
        CHECK(intraday.breach);    // 24950 > 24900 after trade 1; 24750 ≤ 24900 on trade 2 → breach
        Plan eod = g; eod.intraday_trail = false;
        const auto e = payout::run_account(eod, ptrs(d), 0, 1);
        CHECK(!e.breach);          // EOD floor stays at 24000
    }
    {   // soft daily loss limit closes the trade at the limit and skips the rest of the day
        const Plan g{"g", 25000, 1000, 100, 600, 100, 5, 0, 0, 0.5, {1000}, false};
        std::vector<std::vector<Trade>> d = {{{-100, -100, 0}, {-700, -700, 0}, {900, 0, 900}}, {{0, 0, 0}}};
        const auto o = payout::run_account(g, ptrs(d), 0, 2);
        CHECK(!o.breach && o.days == 2);   // day ends at −600: the −700 trade is cut, the +900 never happens
    }
    {   // consistency: one huge day blocks the payout until the cycle evens out
        const Plan c{"c", 25000, 1000, 100, 0, 100, 5, 0, 0.35, 0.5, {1000}, false};
        auto d = plain({{900}, {110}, {110}, {110}, {110}, {700}, {700}});
        const auto o = payout::run_account(c, ptrs(d), 0, 7);
        CHECK(o.paid && o.first_pay == 7);   // 900 ≤ 0.35 × 2740 only on day 7
    }
    {   // min profit and the cap
        const Plan g{"g", 25000, 1000, 100, 0, 100, 5, 1500, 0, 0.5, {1000}, false};
        auto d = plain(std::vector<std::vector<double>>(6, {400}));
        const auto o = payout::run_account(g, ptrs(d), 0, 6);
        CHECK(o.paid && o.first_pay == 5 && std::fabs(o.paid_sum - 1000) < 1e-9);   // 2000 profit · 0.5 = 1000 = cap
    }
    {   // our daily rules: profit stop banks the day, loss stop cuts it, max trades caps it
        auto d = plain({{200, -300, 400}});
        payout::Rules ps{150, 0, 0};
        CHECK(payout::run_account(flex, ptrs(d), 0, 1, ps).days == 1);
        const Plan one{"one", 50000, 2000, 100, 0, 150, 1, 0, 0, 1.0, {3000}, false};   // pays the whole day's profit
        CHECK(std::fabs(payout::run_account(one, ptrs(d), 0, 1, ps).paid_sum - 200) < 1e-9);          // stopped after +200
        CHECK(std::fabs(payout::run_account(one, ptrs(d), 0, 1, payout::Rules{}).paid_sum - 300) < 1e-9);   // all three
        CHECK(std::fabs(payout::run_account(one, ptrs(d), 0, 1, payout::Rules{0, 0, 2}).paid_sum) < 1e-9); // 200 − 300 < 0
        auto l = plain({{-100, -400, 900}, {200}});
        const Plan one2{"one2", 50000, 2000, 100, 0, 150, 1, 0, 0, 1.0, {3000}, false};
        const auto cut = payout::run_account(one2, ptrs(l), 0, 2, payout::Rules{0, 250, 0});   // day 1 ends at −250
        CHECK(!cut.paid && !cut.breach && cut.days == 2);   // −250 + 200 = −50: no profit to pay
    }
    CHECK(std::fabs(payout::to_nq(10, 1.82) - (11.82 * 10 - 1.82)) < 1e-9);
    {   // Select Daily (2026-10-11, help.tradeify.co): payouts any day above the buffer, 2× the cycle profit, ≥ $250, capped
        Plan daily{"daily25", 25000, 1000, 100, 500, 0, 0, 0, 0, 0, {600}, false, 1100, 2, 250, 1};
        auto d = plain({{400}, {400}, {400}, {100}, {50}, {300}, {-100}, {200}});
        auto o = payout::run_account(daily, ptrs(d), 0, 8);
        // day 3: profit 1200 > buffer, min(600, 2×1200, 1200−1100=100) = 100 < 250 → none; day 4: 200 → none;
        // day 5: 250 → paid (balance 26100, cycle resets); day 6: min(600, 600, 300) = 300 paid; day 7: cycle −100 →
        // nothing; day 8: cycle +100 → 200 < 250 → nothing
        CHECK(o.paid && o.first_pay == 5);
        CHECK(o.payouts == 2 && std::fabs(o.paid_sum - 550) < 1e-9);
        auto cut = payout::run_account(daily, ptrs(plain({{-700, 900}})), 0, 1);   // DLL 500: the −700 trade is cut
        CHECK(!cut.breach && !cut.paid && cut.days == 1);
    }
    {   // Evaluation: target, 40 % consistency with a 3-day minimum, live breach at the MAE
        Plan sel{"sel25", 25000, 1000, 100, 0, 100, 5, 0, 0, 0.5, {1250}, false, 0, 0, 0, 1, 1500, 0.40, 3, 0};
        auto e = payout::run_eval(sel, ptrs(plain({{600}, {600}, {500}})), 0, 10);
        CHECK(e.passed && e.days == 3);                                   // 1700 profit, best day 600 = 35 %
        auto big = payout::run_eval(sel, ptrs(plain({{1500}, {100}, {100}, {100}, {100}, {100}})), 0, 10);
        CHECK(!big.passed && !big.failed);                                // 1500 of 2000 = 75 % > 40 %: not passed yet
        auto two = payout::run_eval(sel, ptrs(plain({{800}, {800}})), 0, 10);
        CHECK(!two.passed);                                               // 1600 ≥ target but only 2 days (and 50 %)
        auto dd = payout::run_eval(sel, ptrs(plain({{-1000}})), 0, 1);
        CHECK(dd.failed && dd.days == 1);
        Plan gr = sel; gr.eval_consistency = 0; gr.eval_min_days = 1; gr.eval_daily_loss = 600;
        CHECK(payout::run_eval(gr, ptrs(plain({{1600}})), 0, 1).passed);  // Growth: one day can pass
        // two-stage: 2× until half the target — 400/day at 1× is 800 while below 750 profit, then 400
        auto b2 = payout::run_eval(gr, ptrs(plain({{400}, {400}, {400}, {400}})), 0, 4, payout::Rules{}, 2.0, 750);
        CHECK(b2.passed && b2.days == 3);                                 // 800 + 400 + 400 = 1600 on day 3
        auto b2f = payout::run_eval(gr, ptrs(plain({{-600}})), 0, 1, payout::Rules{}, 2.0, 750);
        CHECK(b2f.failed);                                                // −1200 at 2× breaches the 1000 floor
    }

    std::printf("test_payout_sim: %d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
