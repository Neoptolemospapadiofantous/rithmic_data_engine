#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
using namespace lab;
static std::vector<Trade> wf_trades(Market& m, Evaluator& ev, const char* path) {
  std::ifstream f(path); Spec sp = parse_spec(json::parse(f)); std::vector<std::vector<Trade>> vt;
  for (auto& p : grid(sp)) { Variant v; v.p = p; for (auto& c : sp.checks) { v.checks.emplace_back(parse(c.when, p), parse(c.dir, p)); ev.bind(*v.checks.back().first); ev.bind(*v.checks.back().second); }
    v.stop = parse(sp.stop, p); v.target = parse(sp.target, p); ev.bind(*v.stop); ev.bind(*v.target); vt.push_back(run_variant(ev, m.s(), sp, v, 0)); }
  return walk(m.s(), vt, 1.0, sp.train_months, std::max(5, sp.min_trades / 20)).trades;
}

// payout::run_account with a pacing policy: at most one payout per calendar month (month of cal day), and leave
// `reserve` $ above the plan's buffer (Select Daily) / above the min profit (Flex) after paying.
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
int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; load_raw("data/edge_lab/cache", "NQ", s, err); build_days(s, fomc, vix);
  Market m(s); Evaluator ev(m); ExitSim sim(m);
  auto cas = wf_trades(m, ev, "research/ideas/so_cascade.json");
  auto wd = wf_trades(m, ev, "research/ideas/weak_day_long.json");
  std::map<int, double> mc, mw; std::map<int, int> nc, nw;
  for (auto& t : cas) { int d = s.days[t.day].date; if (d < 20190101) continue; mc[d / 100] += (sim.run(t, X_VWAP_STALL).pts - 1.5) * 2; ++nc[d / 100]; }   // $ per MNQ
  for (auto& t : wd) { int d = s.days[t.day].date; if (d < 20190101) continue; mw[d / 100] += (t.pts - 1.5) * 2; ++nw[d / 100]; }
  int months = 0, pc = 0, pw = 0, either = 0, both_sum = 0, wd_months = 0;
  std::vector<double> a, b;
  for (int y = 201901; y <= 202610; y = (y % 100 == 12) ? (y / 100 + 1) * 100 + 1 : y + 1) {
    if (y >= 202603 && y <= 202608) continue;            // no NQ data 2026-03-20 … 09-18
    ++months; double c = mc[y], w = mw[y];
    pc += c > 0; pw += w > 0; wd_months += nw[y] > 0; either += (c > 0 || w > 0); both_sum += (c + w) > 0;
    a.push_back(c); b.push_back(w);
  }
  double ma = 0, mb = 0; for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; } ma /= a.size(); mb /= b.size();
  double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < a.size(); ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
  printf("months %d (2019-01 … 2026-10, data gap excluded)\n", months);
  printf("cascade+vwap_stall positive months: %d (%.0f%%), mean $%.0f / MNQ / month, sd $%.0f\n", pc, 100.0 * pc / months, ma, std::sqrt(saa / a.size()));
  printf("weak-day long positive months: %d (%.0f%%), traded in %d months, mean $%.0f, sd $%.0f\n", pw, 100.0 * pw / months, wd_months, mb, std::sqrt(sbb / b.size()));
  printf("either positive (two accounts, one each): %.0f%%   combined (same account) positive: %.0f%%   corr %.2f\n", 100.0 * either / months, 100.0 * both_sum / months, sab / std::sqrt(saa * sbb));
  // longest run of losing months, cascade
  int run = 0, worst = 0; for (double x : a) { run = x > 0 ? 0 : run + 1; worst = std::max(worst, run); }
  // account mix through the prop rules: payout months covered
  auto cal_for = [&](const std::vector<Trade>& tr, int rule, int mnq, std::vector<int>& cal_day, std::vector<std::vector<payout::Trade>>& store, double pv, double cst, ExitSim& xs, const Series& ser) {
    std::map<int, std::vector<payout::Trade>> by;   // keyed by date (markets have their own day indices)
    for (auto& t : tr) { const int dt = ser.days[t.day].date; if (dt < 20190101) continue; ExitOut x = rule >= 0 ? xs.run(t, rule) : ExitOut{t.x, t.pts, t.mae, t.mfe, false};
      by[dt].push_back({(x.pts - cst) * pv * mnq, (x.mae - cst) * pv * mnq, std::max(0.0, x.mfe) * pv * mnq}); }
    store.assign(cal_day.size(), {}); std::vector<const std::vector<payout::Trade>*> cal(cal_day.size(), nullptr);
    for (size_t i = 0; i < cal_day.size(); ++i) { auto it = by.find(s.days[cal_day[i]].date); if (it != by.end()) { store[i] = it->second; cal[i] = &store[i]; } }
    return cal;
  };
  std::vector<int> cal_day; for (int d = 0; d < (int)s.days.size(); ++d) if (s.days[d].ok && s.days[d].date >= 20190101) cal_day.push_back(d);
  const int D = cal_day.size(); const double yrs = D / 252.0;
  struct Acct { const char* name; const std::vector<Trade>* tr; int rule; int mnq; const char* plan; int offset; double pv = 2, cost = 1.5; ExitSim* xs = nullptr; const Series* ser = nullptr; };
  std::vector<int> month_of; for (int d : cal_day) month_of.push_back(s.days[d].date / 100);
  double RES = -1;
  auto run_mix = [&](const char* label, std::vector<Acct> accts) {
    std::set<int> pm; int ev = 0, pays = 0; double paid = 0;
    for (auto& a : accts) {
      const payout::Plan* plan = nullptr; for (auto& p : payout::plans()) if (p.name == a.plan) plan = &p;
      std::vector<std::vector<payout::Trade>> st; auto cal = cal_for(*a.tr, a.rule, a.mnq, cal_day, st, a.pv, a.cost, a.xs ? *a.xs : sim, a.ser ? *a.ser : s);
      int i = a.offset;
      while (i < D) { ++ev; auto e = payout::run_eval(*plan, cal, i, D - i); i += std::max(1, e.days); if (!e.passed) continue;
        auto r = RES < 0 ? payout::run_account(*plan, cal, i, D - i) : run_paced(*plan, cal, i, D - i, RES, month_of);
        for (int k : r.pay_days) { int dd = s.days[cal_day[std::min(D - 1, i + k - 1)]].date / 100; if (!(dd >= 202603 && dd <= 202608)) pm.insert(dd); }
        paid += r.paid_sum; pays += r.payouts; i += std::max(1, r.days); if (!r.breach) break; }
    }
    printf("%-58s payout months %3.0f%%  payouts/yr %4.1f  paid $%6.0f/yr  evals/yr %4.1f\n", label, 100.0 * pm.size() / months, pays / yrs, paid / yrs, ev / yrs);
  };
  Series es; load_raw("data/edge_lab/cache", "ES", es, err); build_days(es, fomc, vix);
  Market mes(es); Evaluator eves(mes); ExitSim esx(mes);
  auto esc = wf_trades(mes, eves, "research/ideas/es_cascade.json");
  for (double res : {-1.0, 0.0}) for (const char* pl : {"select_daily_50k"}) {
    RES = res; printf("== %s, %s\n", pl, res < 0 ? "withdraw max when allowed" : "one withdrawal a month");
    Acct A{"nq", &cas, X_VWAP_STALL, 4, pl, 0}; Acct A2{"nq", &cas, X_VWAP_STALL, 2, pl, 0};
    Acct B{"wd", &wd, -1, 1, pl, 0};
    for (int mes_n : {10, 20}) {
      Acct C{"es", &esc, -1, mes_n, pl, 0, 5.0, 0.6, &esx, &es};
      Acct Cx{"es", &esc, X_VWAP_STALL, mes_n, pl, 0, 5.0, 0.6, &esx, &es};
      char l1[128], l2[128], l3[128];
      snprintf(l1, sizeof l1, "ES cascade alone, %d MES", mes_n); run_mix(l1, {C});
      snprintf(l2, sizeof l2, "ES cascade + vwap/stall alone, %d MES", mes_n); run_mix(l2, {Cx});
      snprintf(l3, sizeof l3, "3 accts: NQ 4 MNQ + weak-day 1 MNQ + ES %d MES (vwap/stall)", mes_n); run_mix(l3, {A, B, Cx});
      snprintf(l3, sizeof l3, "3 accts: NQ 2 MNQ + weak-day 1 MNQ + ES %d MES (vwap/stall)", mes_n); run_mix(l3, {A2, B, Cx});
    }
    run_mix("1 acct: NQ 4 MNQ", {A});
    run_mix("2 accts: NQ 4 MNQ + weak-day 1 MNQ", {A, B});
  }
  printf("cascade longest run of non-positive months: %d\n", worst);
}
