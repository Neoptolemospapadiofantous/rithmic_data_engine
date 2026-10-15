// payout_sim — finds strategy + daily-rule combinations that pass a prop funded account's payout rules, chosen on
// the first part of the history and scored on the rest. Input from scripts/payout_sim.sh; model in payout_sim.hpp.
//
//   payout_sim --trades FILE --calendar FILE --csv OUT.csv --report OUT.md
//              [--horizon 60] [--step 2] [--commission 1.82] [--train-frac 0.6] [--reset-cost 150]
//              [--max-blown 0.30] [--portfolio 4] [--pool 25] [--top 15]
//
// trades file: series_id,trading_day YYYY-MM-DD,net $ per MNQ contract (paper fleet point_value 2),mae_pts,mfe_pts,entry_epoch
// calendar:    YYYY-MM-DD per trading day with data (ascending)
//
// Selection, per plan × size (and for a portfolio of up to --portfolio series):
//   1. every daily-rule combination (profit stop × loss stop × max trades, see rule_grid) is run on the TRAIN
//      starts (accounts that live entirely in the first train-frac of the calendar); the best by EV per account
//      is kept if its blown-first rate there is ≤ max-blown;
//   2. that same choice is then run on the TEST starts (accounts that start after the split) — the number reported.
// EV per account = (payouts − blown accounts × reset-cost) / starts, over one horizon-day account life.
#include "payout_sim.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <unordered_map>

namespace {

long days_from_civil(int y, unsigned m, unsigned d) {   // H. Hinnant
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long>(doe) - 719468;
}
long parse_day(const char* s) {
    int y = 0; unsigned m = 0, d = 0;
    if (std::sscanf(s, "%d-%u-%u", &y, &m, &d) != 3) return -1;
    return days_from_civil(y, m, d);
}

struct Size { std::string name; bool mnq; int n; };
const std::vector<Size> kSizes = {{"1 MNQ", true, 1}, {"2 MNQ", true, 2}, {"3 MNQ", true, 3},
                                  {"5 MNQ", true, 5}, {"10 MNQ", true, 10}, {"1 NQ", false, 1}, {"2 NQ", false, 2},
                                  {"3 NQ", false, 3}, {"4 NQ", false, 4}};
int minis_of(const Size& s) { return s.mnq ? (s.n + 9) / 10 : s.n; }   // the plan's contract limit counts 10 micros = 1 mini

std::vector<payout::Rules> rule_grid() {
    std::vector<payout::Rules> g;
    for (double ps : {0.0, 150.0, 300.0, 600.0})
        for (double ls : {0.0, 150.0, 300.0, 600.0})
            for (int mt : {0, 1, 2, 3}) g.push_back({ps, ls, mt});
    return g;
}
std::string rules_str(const payout::Rules& r) {
    const std::string ps = r.profit_stop > 0 ? "+" + std::to_string((int)r.profit_stop) : "—";
    const std::string ls = r.loss_stop > 0 ? "−" + std::to_string((int)r.loss_stop) : "—";
    const std::string mt = r.max_trades > 0 ? std::to_string(r.max_trades) : "—";
    return ps + " / " + ls + " / " + mt;
}

struct Raw { double pnl, mae_pts, mfe_pts; long ts; };   // per MNQ contract

struct Score {
    int starts = 0, paid10 = 0, paid20 = 0, blown_first = 0, blown = 0;
    double paid = 0, days = 0;
    int eval_pass = 0, eval_fail = 0; std::vector<int> eval_days;   // evaluation phase from the same starts
    std::vector<int> gaps; double alive_after_first = 0; int payouts_after_first = 0;   // steady-state cadence after the first payout
    double win10 = 0, win10_2 = 0, win10_3 = 0; int win10_n = 0;   // share of 10-day windows with a payout, 1 / 2 / 3 staggered accounts
    double win5 = 0;                                                // share of 5-day (weekly) windows with a payout, one account
    int eval2_pass = 0, eval2_fail = 0; std::vector<int> eval2_days;   // two-stage eval: 2× size until half the target
    double ev(double reset) const { return starts ? (paid - blown * reset) / starts : -1e18; }
    double blown_first_rate() const { return starts ? (double)blown_first / starts : 1; }
};

// Share of non-overlapping 10-trading-day windows, from the first payout of any account to the horizon end, that hold
// at least one payout — for N accounts started `stagger` days apart on the same strategy.
double windows_paid(const payout::Plan& pl, const payout::Rules& r, const std::vector<const std::vector<payout::Trade>*>& ptrs,
                    int s0, int horizon, int n_acct, int stagger, int win = 10) {
    std::vector<int> days;
    for (int a = 0; a < n_acct; ++a) {
        const int st = s0 + a * stagger;
        if (st + horizon > (int)ptrs.size()) break;
        const auto o = payout::run_account(pl, ptrs, st, horizon, r);
        for (int d : o.pay_days) days.push_back(st + d);
    }
    if (days.empty()) return 0;
    std::sort(days.begin(), days.end());
    const int from = days.front(), to = s0 + horizon;
    int wins = 0, total = 0; size_t i = 0;
    for (int w = from; w + win <= to; w += win) {
        ++total; bool hit = false;
        while (i < days.size() && days[i] < w + win) { if (days[i] >= w) hit = true; ++i; }
        if (hit) ++wins;
    }
    return total ? (double)wins / total : 0;
}
Score evaluate(const payout::Plan& pl, const payout::Rules& r, const std::vector<const std::vector<payout::Trade>*>& ptrs,
               const std::vector<int>& starts, int horizon, bool cadence = false) {
    Score s;
    for (int s0 : starts) {
        const auto o = payout::run_account(pl, ptrs, s0, horizon, r);
        if (cadence && o.paid) {
            s.win10 += windows_paid(pl, r, ptrs, s0, horizon, 1, 10); s.win10_2 += windows_paid(pl, r, ptrs, s0, horizon, 2, 10);
            s.win10_3 += windows_paid(pl, r, ptrs, s0, horizon, 3, 10); ++s.win10_n;
            s.win5 += windows_paid(pl, r, ptrs, s0, horizon, 1, 10, 5);
        }
        ++s.starts;
        if (o.paid && o.first_pay <= 10) ++s.paid10;
        if (o.paid && o.first_pay <= 20) ++s.paid20;
        if (o.breach) { ++s.blown; if (!o.paid) ++s.blown_first; }
        s.paid += o.paid_sum; s.days += o.days;
        for (size_t i = 1; i < o.pay_days.size(); ++i) s.gaps.push_back(o.pay_days[i] - o.pay_days[i - 1]);
        if (o.paid) { s.alive_after_first += o.days - o.first_pay; s.payouts_after_first += o.payouts - 1; }
        if (pl.eval_target > 0) {
            const auto e = payout::run_eval(pl, ptrs, s0, horizon, r);
            if (e.passed) { ++s.eval_pass; s.eval_days.push_back(e.days); } else if (e.failed) ++s.eval_fail;
            const auto e2 = payout::run_eval(pl, ptrs, s0, horizon, r, 2.0, pl.eval_target / 2);
            if (e2.passed) { ++s.eval2_pass; s.eval2_days.push_back(e2.days); } else if (e2.failed) ++s.eval2_fail;
        }
    }
    return s;
}
double gap_med(Score s) { if (s.gaps.empty()) return 0; std::sort(s.gaps.begin(), s.gaps.end()); return s.gaps[s.gaps.size() / 2]; }
double gap_le(const Score& s, int n) { if (s.gaps.empty()) return 0; int c = 0; for (int g : s.gaps) if (g <= n) ++c; return 100.0 * c / s.gaps.size(); }
double eval_days_med(Score s) {
    if (s.eval_days.empty()) return 0;
    std::sort(s.eval_days.begin(), s.eval_days.end()); return s.eval_days[s.eval_days.size() / 2];
}
double eval2_days_med(Score s) {
    if (s.eval2_days.empty()) return 0;
    std::sort(s.eval2_days.begin(), s.eval2_days.end()); return s.eval2_days[s.eval2_days.size() / 2];
}

}  // namespace

int main(int argc, char** argv) {
    std::string trades_path, cal_path, csv_path, report_path;
    int horizon = 60, step = 2, top = 15, portfolio = 4, pool_n = 25;
    double commission = 1.82, train_frac = 0.6, reset = 150, max_blown = 0.30;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { std::cerr << "missing value for " << a << "\n"; std::exit(2); } return argv[++i]; };
        if (a == "--trades") trades_path = next();
        else if (a == "--calendar") cal_path = next();
        else if (a == "--csv") csv_path = next();
        else if (a == "--report") report_path = next();
        else if (a == "--horizon") horizon = std::atoi(next().c_str());
        else if (a == "--step") step = std::max(1, std::atoi(next().c_str()));
        else if (a == "--top") top = std::atoi(next().c_str());
        else if (a == "--portfolio") portfolio = std::atoi(next().c_str());
        else if (a == "--pool") pool_n = std::atoi(next().c_str());
        else if (a == "--commission") commission = std::atof(next().c_str());
        else if (a == "--train-frac") train_frac = std::atof(next().c_str());
        else if (a == "--reset-cost") reset = std::atof(next().c_str());
        else if (a == "--max-blown") max_blown = std::atof(next().c_str());
        else { std::cerr << "unknown arg " << a << "\n"; return 2; }
    }
    if (trades_path.empty() || cal_path.empty() || csv_path.empty() || report_path.empty()) {
        std::cerr << "usage: payout_sim --trades F --calendar F --csv OUT --report OUT\n"; return 2;
    }

    // Calendar with data-gap segments; an account never spans a gap.
    std::vector<long> cal; std::vector<int> seg; std::vector<std::string> cal_str;
    std::unordered_map<long, int> cal_idx;
    {
        std::ifstream f(cal_path);
        if (!f) { std::cerr << "cannot open " << cal_path << "\n"; return 1; }
        std::string line; int s = 0;
        while (std::getline(f, line)) {
            const long d = parse_day(line.c_str());
            if (d < 0) continue;
            if (!cal.empty() && d - cal.back() > 7) ++s;
            cal_idx[d] = (int)cal.size(); cal.push_back(d); seg.push_back(s); cal_str.push_back(line.substr(0, 10));
        }
    }
    if ((int)cal.size() < horizon + 2) { std::cerr << "calendar too short\n"; return 1; }
    const int split = (int)(cal.size() * train_frac);
    std::vector<int> train_starts, test_starts;
    for (int i = 0; i + horizon - 1 < (int)cal.size(); i += step) {
        if (seg[i] != seg[i + horizon - 1]) continue;
        if (i + horizon - 1 < split) train_starts.push_back(i);
        else if (i >= split) test_starts.push_back(i);   // test accounts never touch a train day
    }
    if (train_starts.empty() || test_starts.empty()) { std::cerr << "no train or test starts\n"; return 1; }

    std::map<std::string, std::vector<std::vector<Raw>>> series;
    {
        std::ifstream f(trades_path);
        if (!f) { std::cerr << "cannot open " << trades_path << "\n"; return 1; }
        std::string line; long bad = 0;
        while (std::getline(f, line)) {
            const auto c1 = line.find(','), c2 = line.find(',', c1 + 1);
            if (c1 == std::string::npos || c2 == std::string::npos) { ++bad; continue; }
            const auto it = cal_idx.find(parse_day(line.c_str() + c1 + 1));
            if (it == cal_idx.end()) { ++bad; continue; }
            Raw r{0, 0, 0, 0};
            std::sscanf(line.c_str() + c2 + 1, "%lf,%lf,%lf,%ld", &r.pnl, &r.mae_pts, &r.mfe_pts, &r.ts);
            r.mae_pts = std::min(r.mae_pts, 0.0); r.mfe_pts = std::max(r.mfe_pts, 0.0);
            auto& days = series[line.substr(0, c1)];
            if (days.empty()) days.resize(cal.size());
            days[it->second].push_back(r);
        }
        if (bad) std::cerr << "[payout_sim] skipped " << bad << " unparsable/off-calendar rows\n";
    }
    std::cerr << "[payout_sim] " << series.size() << " series, " << cal.size() << " days (train " << cal_str.front() << " … "
              << cal_str[split - 1] << ", test " << cal_str[split] << " … " << cal_str.back() << "), starts train "
              << train_starts.size() << " / test " << test_starts.size() << "\n";

    const auto grid = rule_grid();
    // Scale a set of series (merged by entry time within each day) to a size.
    auto build = [&](const std::vector<const std::vector<std::vector<Raw>>*>& parts, const Size& sz,
                     std::vector<std::vector<payout::Trade>>& out, std::vector<const std::vector<payout::Trade>*>& ptrs) {
        out.assign(cal.size(), {}); ptrs.assign(cal.size(), nullptr);
        std::vector<Raw> day;
        const double pv = sz.mnq ? 2.0 : 20.0;
        for (size_t i = 0; i < cal.size(); ++i) {
            day.clear();
            for (auto* p : parts) day.insert(day.end(), (*p)[i].begin(), (*p)[i].end());
            if (parts.size() > 1) std::stable_sort(day.begin(), day.end(), [](const Raw& a, const Raw& b) { return a.ts < b.ts; });
            for (const Raw& r : day)
                out[i].push_back({sz.n * (sz.mnq ? r.pnl : payout::to_nq(r.pnl, commission)), sz.n * r.mae_pts * pv, sz.n * r.mfe_pts * pv});
            if (!out[i].empty()) ptrs[i] = &out[i];
        }
    };
    struct Best { payout::Rules r; Score train, test; bool ok = false; };
    auto choose = [&](const payout::Plan& pl, const std::vector<const std::vector<payout::Trade>*>& ptrs) {
        Best b;
        for (const auto& r : grid) {
            const Score s = evaluate(pl, r, ptrs, train_starts, horizon);
            if (s.blown_first_rate() > max_blown) continue;
            if (!b.ok || s.ev(reset) > b.train.ev(reset)) { b.r = r; b.train = s; b.ok = true; }
        }
        if (b.ok) b.test = evaluate(pl, b.r, ptrs, test_starts, horizon, true);
        return b;
    };

    struct Row { std::string series, plan, size; Best b; };
    std::vector<Row> rows;
    std::vector<std::vector<payout::Trade>> scaled; std::vector<const std::vector<payout::Trade>*> ptrs;
    for (const auto& [sid, days] : series)
        for (const auto& sz : kSizes) {
            build({&days}, sz, scaled, ptrs);
            for (const auto& pl : payout::plans()) {
                if (pl.max_minis > 0 && minis_of(sz) > pl.max_minis) continue;   // over the plan's contract limit
                Best b = choose(pl, ptrs);
                if (b.ok) rows.push_back({sid, pl.name, sz.name, b});
            }
        }

    // Portfolios: per plan × size, greedily add series (pool = the best single series by TRAIN EV, one per base
    // strategy, overlays and filters stripped) while the combined account's train EV improves. Members and rules
    // never see the test period.
    struct Port { std::string plan, size; std::vector<std::string> members; Best b; };
    std::vector<Port> ports;
    if (portfolio > 1)
        for (const auto& pl : payout::plans())
            for (const auto& sz : kSizes) {
                if (pl.max_minis > 0 && minis_of(sz) > pl.max_minis) continue;
                std::vector<const Row*> cand;
                for (const auto& r : rows) if (r.plan == pl.name && r.size == sz.name && r.b.train.ev(reset) > 0) cand.push_back(&r);
                std::sort(cand.begin(), cand.end(), [&](auto* a, auto* b) { return a->b.train.ev(reset) > b->b.train.ev(reset); });
                std::vector<std::string> pool; std::set<std::string> bases;
                for (auto* c : cand) {
                    // one member per ROOT strategy: overlays (__tr25, __rvol15, …) and filters of one root mostly make
                    // the same trades, so stacking them is size, not diversification
                    const std::string sid = c->series.substr(0, c->series.find(" · "));
                    const std::string base = sid.substr(0, sid.find("__"));
                    if (bases.insert(base).second) pool.push_back(c->series);
                    if ((int)pool.size() >= pool_n) break;
                }
                if (pool.size() < 2) continue;
                Port cur{pl.name, sz.name, {}, {}};
                std::vector<const std::vector<std::vector<Raw>>*> parts;
                // the plan's contract limit: every member can be in a position at once, so members × size must fit
                const int max_members = pl.max_minis > 0 ? std::max(1, pl.max_minis / std::max(1, minis_of(sz))) : portfolio;
                while ((int)cur.members.size() < std::min(portfolio, max_members)) {
                    std::string pick; Best pick_b;
                    for (const auto& s : pool) {
                        if (std::find(cur.members.begin(), cur.members.end(), s) != cur.members.end()) continue;
                        auto trial = parts; trial.push_back(&series.at(s));
                        build(trial, sz, scaled, ptrs);
                        Best b = choose(pl, ptrs);
                        if (b.ok && (!pick_b.ok || b.train.ev(reset) > pick_b.train.ev(reset))) { pick = s; pick_b = b; }
                    }
                    if (pick.empty() || (cur.b.ok && pick_b.train.ev(reset) <= cur.b.train.ev(reset))) break;
                    cur.members.push_back(pick); parts.push_back(&series.at(pick)); cur.b = pick_b;
                }
                if (cur.members.size() >= 2) ports.push_back(cur);
            }

    {
        std::ofstream f(csv_path);
        f << "series,plan,size,rule_profit_stop,rule_loss_stop,rule_max_trades,train_ev,train_blown_first_pct,"
             "test_starts,test_ev,test_paid_10d_pct,test_paid_20d_pct,test_blown_first_pct,test_blown_pct,test_paid_per_month,"
             "eval_pass_pct,eval_fail_pct,eval_days_med,gap_med,gap_le10_pct,gap_le20_pct,payouts_per_month_after_first,"
             "win10_1acct_pct,win10_2acct_pct,win10_3acct_pct,eval2x_pass_pct,eval2x_fail_pct,eval2x_days_med,win5_pct\n";
        char buf[1024];
        auto line = [&](const std::string& name, const std::string& plan, const std::string& size, const Best& b) {
            const Score& t = b.test;
            std::snprintf(buf, sizeof buf, "%s,%s,%s,%.0f,%.0f,%d,%.0f,%.1f,%d,%.0f,%.1f,%.1f,%.1f,%.1f,%.0f,%.1f,%.1f,%.0f", name.c_str(),
                          plan.c_str(), size.c_str(), b.r.profit_stop, b.r.loss_stop, b.r.max_trades, b.train.ev(reset),
                          100 * b.train.blown_first_rate(), t.starts, t.ev(reset), t.starts ? 100.0 * t.paid10 / t.starts : 0,
                          t.starts ? 100.0 * t.paid20 / t.starts : 0, 100 * t.blown_first_rate(),
                          t.starts ? 100.0 * t.blown / t.starts : 0, t.days > 0 ? t.paid / t.days * 21 : 0,
                          t.starts ? 100.0 * t.eval_pass / t.starts : 0, t.starts ? 100.0 * t.eval_fail / t.starts : 0, eval_days_med(t));
            f << buf;
            std::snprintf(buf, sizeof buf, ",%.0f,%.0f,%.0f,%.2f,%.0f,%.0f,%.0f,%.1f,%.1f,%.0f,", gap_med(t), gap_le(t, 10), gap_le(t, 20),
                          t.alive_after_first > 0 ? 21.0 * t.payouts_after_first / t.alive_after_first : 0,
                          t.win10_n ? 100.0 * t.win10 / t.win10_n : 0, t.win10_n ? 100.0 * t.win10_2 / t.win10_n : 0,
                          t.win10_n ? 100.0 * t.win10_3 / t.win10_n : 0,
                          t.starts ? 100.0 * t.eval2_pass / t.starts : 0, t.starts ? 100.0 * t.eval2_fail / t.starts : 0, eval2_days_med(t));
            f << buf;
            std::snprintf(buf, sizeof buf, "%.0f\n", t.win10_n ? 100.0 * t.win5 / t.win10_n : 0);
            f << buf;
        };
        for (const auto& r : rows) line(r.series, r.plan, r.size, r.b);
        for (const auto& p : ports) {
            std::string n = "PORTFOLIO[";
            for (size_t i = 0; i < p.members.size(); ++i) n += (i ? " + " : "") + p.members[i];
            line(n + "]", p.plan, p.size, p.b);
        }
    }

    std::ofstream md(report_path);
    md << "# Payout simulator — chosen on the past, scored on the future\n\n"
       << "Train " << cal_str.front() << " … " << cal_str[split - 1] << " (" << train_starts.size() << " account starts), **test "
       << cal_str[split] << " … " << cal_str.back() << "** (" << test_starts.size() << " starts, never seen when choosing). "
       << "Each account runs " << horizon << " trading days from a start every " << step << " days. Daily rules "
          "(profit stop / loss stop / max trades) are chosen on train among " << grid.size() << " combinations, keeping only "
          "those with ≤ " << (int)(max_blown * 100) << "% blown-before-payout on train. Breaches at each trade's MAE; Growth "
          "trails intraday (MFE first). **EV/account** = (payouts − blown × $" << (int)reset << " reset) / starts. "
          "Plan rules and assumptions: src/paper/payout_sim.hpp. P&L per MNQ from the paper fleet; 1 NQ = 10× gross.\n\n";
    char buf[2048];
    auto header = [&](const char* first) {
        md << "| " << first << " | size | rules (+profit / −loss / trades) | train EV | **test EV** | paid ≤10d | paid ≤20d | blown first | blown in "
           << horizon << "d | $/month alive | eval pass | eval fail | eval days |\n|---|---|---|---|---|---|---|---|---|---|---|---|---|\n";
    };
    auto put = [&](const std::string& name, const std::string& size, const Best& b) {
        const Score& t = b.test;
        std::snprintf(buf, sizeof buf, "| %s | %s | %s | %.0f | **%.0f** | %.0f%% | %.0f%% | %.0f%% | %.0f%% | %.0f | %.0f%% | %.0f%% | %.0f |\n",
                      name.c_str(), size.c_str(), rules_str(b.r).c_str(), b.train.ev(reset), t.ev(reset),
                      t.starts ? 100.0 * t.paid10 / t.starts : 0, t.starts ? 100.0 * t.paid20 / t.starts : 0,
                      100 * t.blown_first_rate(), t.starts ? 100.0 * t.blown / t.starts : 0, t.days > 0 ? t.paid / t.days * 21 : 0,
                      t.starts ? 100.0 * t.eval_pass / t.starts : 0, t.starts ? 100.0 * t.eval_fail / t.starts : 0, eval_days_med(t));
        md << buf;
    };
    for (const auto& pl : payout::plans()) {
        md << "## " << pl.name << "\n\n### Best single strategies (by test EV)\n\n";
        std::vector<const Row*> v;
        for (const auto& r : rows) if (r.plan == pl.name) v.push_back(&r);
        std::sort(v.begin(), v.end(), [&](auto* a, auto* b) { return a->b.test.ev(reset) > b->b.test.ev(reset); });
        header("strategy · filter");
        for (int i = 0; i < (int)v.size() && i < top; ++i) put(v[i]->series, v[i]->size, v[i]->b);
        if (v.empty()) md << "| _nothing kept blown-first ≤ " << (int)(max_blown * 100) << "% on train_ | | | | | | | | | | | | |\n";
        md << "\n### 1 NQ only\n\n";
        header("strategy · filter");
        int k = 0;
        for (auto* r : v) if (r->size == "1 NQ" && k < 8) { put(r->series, r->size, r->b); ++k; }
        if (k == 0) md << "| _no strategy passes at 1 NQ_ | | | | | | | | | | | | |\n";
        md << "\n### Portfolios (members chosen on train)\n\n";
        header("members");
        std::vector<const Port*> pv;
        for (const auto& p : ports) if (p.plan == pl.name) pv.push_back(&p);
        std::sort(pv.begin(), pv.end(), [&](auto* a, auto* b) { return a->b.test.ev(reset) > b->b.test.ev(reset); });
        for (auto* p : pv) {
            std::string n;
            for (size_t i = 0; i < p->members.size(); ++i) n += (i ? " + " : "") + p->members[i];
            put(n, p->size, p->b);
        }
        if (pv.empty()) md << "| _no portfolio_ | | | | | | | | | | | | |\n";
        md << "\n";
    }
    std::cerr << "[payout_sim] " << rows.size() << " single + " << ports.size() << " portfolio results → " << report_path << "\n";
    return 0;
}
