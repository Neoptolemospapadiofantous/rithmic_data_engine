// 2026-10-18 plan checks (research/preregistered/2026-10-18_plan_checks.md): stacking the setups, news days, quiet-market
// stand-aside — on the REAL ENGINE's all-year trades (paper_engine --dbn-bars, label mom_hist, $ per 1 MNQ after fleet costs).
// Input out/planchk_trades.csv, exported with:
//   \copy (with ss as (select session_date, lag(atr14_pts) over w / nullif(lag(rth_close) over w,0) as atrp from session_stats
//     where symbol='NQ' window w as (order by session_date)), t as (select strategy_id, (entry_time at time zone
//     'America/New_York')::date d, extract(epoch from entry_time)::bigint e0, extract(epoch from exit_time)::bigint e1, case when
//     upper(direction) in ('LONG','BUY','B') then 1 else -1 end dir, pnl_usd from paper_trades where account_label='mom_hist' and
//     exit_time is not null and strategy_id in ('so_cascade_2r_nofomc','gap_or_retest','mom_dir_45_vwap_plan','mom_dir_45_vwap',
//     'mom_dir_45_dip','so_cascade_2r_vwap_stall')) select t.strategy_id, to_char(t.d,'YYYYMMDD'), e0, e1, dir,
//     round(pnl_usd::numeric,2), coalesce(round(ss.atrp::numeric,5)::text,''), <cpi>, <nfp>, <ppi>, <fomc> (exists … calendar kind)
//     from t left join ss on ss.session_date=t.d order by e0) to 'research/programs/out/planchk_trades.csv' csv
// Run: planchk [trades.csv] [VIX_History.csv]   (defaults research/programs/out/planchk_trades.csv, data/regime/VIX_History.csv)
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

struct Tr { std::string sid; int date; long e0, e1; int dir; double usd, atrp, vix; int cpi, nfp, ppi, fomc; };
static int era(int d) { return d < 20190101 ? 0 : d < 20230101 ? 1 : d < 20250101 ? 2 : 3; }
static const char* EN[4] = {"2010-18", "2019-22", "2023-24", "2025-26*"};

// weekday of a yyyymmdd date (0 = Monday) and the next weekday
static int dnum(int d) { int y = d / 10000, m = d / 100 % 100, dd = d % 100; if (m < 3) { y--; m += 12; } return 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + dd; }
static int from_dnum(int n) { // inverse of dnum (civil from days, shifted)
  for (int y = (n / 366); ; ++y) { int lo = dnum((y) * 10000 + 101); int hi = dnum((y + 1) * 10000 + 101); if (n >= lo && n < hi) {
      for (int m = 1; m <= 12; ++m) { int a = dnum(y * 10000 + m * 100 + 1), b = m == 12 ? hi : dnum(y * 10000 + (m + 1) * 100 + 1); if (n >= a && n < b) return y * 10000 + m * 100 + 1 + (n - a); } } } }

int main(int argc, char** argv) {
  const std::string tf = argc > 1 ? argv[1] : "research/programs/out/planchk_trades.csv", vf = argc > 2 ? argv[2] : "data/regime/VIX_History.csv";
  std::map<int, double> vixc; { std::ifstream f(vf); std::string l; std::getline(f, l); while (std::getline(f, l)) { int m, d, y; double o, h, lo, c;
      if (sscanf(l.c_str(), "%d/%d/%d,%lf,%lf,%lf,%lf", &m, &d, &y, &o, &h, &lo, &c) == 7) vixc[y * 10000 + m * 100 + d] = c; } }
  std::vector<Tr> T; { std::ifstream f(tf); std::string l; while (std::getline(f, l)) { l.erase(std::remove(l.begin(), l.end(), '"'), l.end()); std::stringstream ss(l); std::string x[11]; for (auto& s : x) std::getline(ss, s, ',');
      Tr t{x[0], std::stoi(x[1]), std::stol(x[2]), std::stol(x[3]), std::stoi(x[4]), std::stod(x[5]), x[6].empty() ? NAN : std::stod(x[6]), NAN,
           std::stoi(x[7]), std::stoi(x[8]), std::stoi(x[9]), std::stoi(x[10])};
      auto it = vixc.lower_bound(t.date); if (it != vixc.begin()) t.vix = std::prev(it)->second;   // prior VIX close (strictly before the date)
      T.push_back(t); } }
  if (T.empty()) { fprintf(stderr, "no trades in %s\n", tf.c_str()); return 1; }
  std::sort(T.begin(), T.end(), [](const Tr& a, const Tr& b) { return a.e0 < b.e0; });
  const std::string C = "so_cascade_2r_nofomc", G = "gap_or_retest", M = "mom_dir_45_vwap_plan";
  const std::vector<std::string> ALL = {C, G, M, "mom_dir_45_vwap", "mom_dir_45_dip", "so_cascade_2r_vwap_stall"};
  printf("planchk: %zu engine trades (label mom_hist, $ per 1 MNQ after fleet costs), %s … %d. * 2025-26 not clean.\n", T.size(), "20100701", T.back().date);

  auto pick = [&](const std::vector<std::string>& ids, bool one_at_a_time, bool agree = false) { std::set<std::string> s(ids.begin(), ids.end()); std::vector<Tr> v; long busy = 0;
    std::map<int, int> first;   // agree: a later trade is taken only in the direction of the day's first trade (the plan's rule)
    for (auto& t : T) { if (!s.count(t.sid)) continue; if (one_at_a_time && t.e0 < busy) continue; if (agree) { auto it = first.find(t.date); if (it == first.end()) first[t.date] = t.dir; else if (it->second != t.dir) continue; }
      v.push_back(t); busy = std::max(busy, t.e1); } return v; };
  auto daily = [](const std::vector<Tr>& v) { std::map<int, double> d; for (auto& t : v) d[t.date] += t.usd; return d; };

  // ───────────────────────── 1. stacking ─────────────────────────
  printf("\n===== 1. STACKING =====\n correlation of daily $ on days both traded (2010-24), same-direction share of those days:\n");
  { std::map<std::string, std::map<int, std::pair<double, int>>> D; for (auto& t : T) if (t.date < 20250101) { auto& x = D[t.sid][t.date]; if (x.second == 0) x.second = t.dir; x.first += t.usd; }
    auto pair = [&](const std::string& a, const std::string& b, const char* nm) { double sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; int n = 0, same = 0;
      for (auto& [d, x] : D[a]) { auto it = D[b].find(d); if (it == D[b].end()) continue; double p = x.first, q = it->second.first; sx += p; sy += q; sxx += p * p; syy += q * q; sxy += p * q; ++n; same += x.second == it->second.second; }
      const double r = (n * sxy - sx * sy) / std::sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
      printf("  %-8s days both %4d (of %4zu / %4zu)  corr %+.2f  same direction %4.1f%%\n", nm, n, D[a].size(), D[b].size(), r, 100.0 * same / std::max(1, n)); };
    pair(C, G, "C-G"); pair(C, M, "C-M"); pair(G, M, "G-M"); }
  struct Pol { const char* nm; std::vector<std::string> ids; bool one; bool agree = false; };
  const std::vector<Pol> POL = {{"C  cascade", {C}, false}, {"C+G", {C, G}, false}, {"C+G one at a time", {C, G}, true},
                                {"C+G+M every trade", {C, G, M}, false}, {"C+G+M one at a time", {C, G, M}, true},
                                {"C+G agree (extra)", {C, G}, false, true}, {"C+G+M agree (extra)", {C, G, M}, false, true}};
  printf(" policies, 1 MNQ per trade:            era       trades  days   total$  worst_day   p5_day   max_dd  total/dd\n");
  std::map<std::string, std::array<double, 3>> ratio;
  for (auto& p : POL) { auto v = pick(p.ids, p.one, p.agree); auto d = daily(v);
    for (int e = 0; e < 4; ++e) { std::vector<double> days; double eq = 0, pk = 0, dd = 0; int n = 0;
      for (auto& t : v) if (era(t.date) == e) ++n;
      for (auto& [dt, x] : d) if (era(dt) == e) { days.push_back(x); eq += x; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); }
      std::vector<double> s = days; std::sort(s.begin(), s.end()); const double p5 = s.empty() ? NAN : s[s.size() / 20];
      if (e < 3) ratio[p.nm][e] = dd > 0 ? eq / dd : NAN;
      printf("  %-22s %-9s %6d %5zu %8.0f %10.0f %8.0f %8.0f %8.2f\n", e ? "" : p.nm, EN[e], n, days.size(), eq, s.empty() ? NAN : s[0], p5, dd, dd > 0 ? eq / dd : NAN); } }
  // 25K Lightning payout test
  printf(" 25K Lightning, 2 MNQ per trade, every weekday start 2019-01-02 … 2024-12-31, 40 trading days, +$1,500 with best day ≤ 20%% vs $1,000 EOD trailing:\n");
  const int MON = dnum(20190107); auto wd = [&](int n) { return ((n - MON) % 7 + 7) % 7; };   // 0 = Monday (2019-01-07 was a Monday)
  std::map<std::string, double> pmb;
  auto lightning = [&](const Pol& p, bool cons) { auto d = daily(pick(p.ids, p.one, p.agree)); int paid = 0, blown = 0, nei = 0; long dsum = 0;
    for (int s = dnum(20190102); s <= dnum(20241231); ++s) { if (wd(s) >= 5) continue;  
      double bal = 0, pk = 0, best = 0; int k = 0, res = 0, cnt = 0;
      for (int q = s; k < 40; ++q) { if (wd(q) >= 5) continue; ++k; const int dt = from_dnum(q); auto it = d.find(dt); const double x = it == d.end() ? 0 : 2 * it->second;
        bal += x; best = std::max(best, x); pk = std::max(pk, bal); const double floor = std::min(pk - 1000, 100.0); ++cnt;
        if (bal <= floor) { res = -1; break; } if (bal >= 1500 && (!cons || best <= 0.2 * bal)) { res = 1; break; } }
      if (res > 0) { ++paid; dsum += cnt; } else if (res < 0) ++blown; else ++nei; }
    const int n = paid + blown + nei; const double r = 100.0 * (paid - blown) / n;
    printf("  %-22s starts %4d  paid %5.1f%% (mean %4.1f days)  blown %5.1f%%  neither %5.1f%%  paid−blown %+5.1f\n", p.nm, n, 100.0 * paid / n, paid ? (double)dsum / paid : NAN, 100.0 * blown / n, 100.0 * nei / n, r); return r; };
  for (auto& p : POL) pmb[p.nm] = lightning(p, true);
  printf(" (extra) the same WITHOUT the 20%% best-day rule — does the consistency rule decide it?\n");
  for (auto& p : POL) if (p.agree || std::string(p.nm) == "C  cascade") lightning(p, false);
  auto better = [&](const char* a, const char* b) { bool all = true; for (int e = 0; e < 3; ++e) all &= ratio[a][e] > ratio[b][e]; return all && pmb[a] > pmb[b]; };
  printf(" (extra) = NOT pre-registered: the plan's own rule (a second setup only in the direction of the day's first trade), added after the run to match the page; not used for the verdict.\n");
  printf(" VERDICT (adopt only if total/dd higher in all three eras AND paid−blown higher):\n");
  printf("  C+G over C: %s\n", better("C+G", "C  cascade") || better("C+G one at a time", "C  cascade") ? "ADOPT" : "no");
  printf("  C+G+M over C+G: every trade %s, one at a time %s\n", better("C+G+M every trade", "C+G") ? "ADOPT" : "no", better("C+G+M one at a time", "C+G") ? "ADOPT" : "no");

  // ───────────────────────── 2. news days ─────────────────────────
  printf("\n===== 2. NEWS DAYS ($ per trade, n) =====\n");
  struct Ev { const char* nm; std::function<bool(const Tr&)> f; };
  const std::vector<Ev> EV = {{"CPI", [](const Tr& t) { return t.cpi; }}, {"NFP", [](const Tr& t) { return t.nfp; }}, {"PPI", [](const Tr& t) { return t.ppi; }},
                              {"FOMC", [](const Tr& t) { return t.fomc; }}, {"any 08:30 (CPI/NFP/PPI)", [](const Tr& t) { return t.cpi || t.nfp || t.ppi; }},
                              {"no event", [](const Tr& t) { return !t.cpi && !t.nfp && !t.ppi && !t.fomc; }}};
  for (auto& id : ALL) { printf(" %s\n", id.c_str());
    for (auto& ev : EV) { double s[5] = {0}; int n[5] = {0}; for (auto& t : T) if (t.sid == id && ev.f(t)) { const int e = era(t.date); s[e] += t.usd; ++n[e]; if (e < 3) { s[4] += t.usd; ++n[4]; } }
      printf("   %-24s", ev.nm); for (int e = 0; e < 4; ++e) printf(" %s %+7.2f (%4d)", EN[e], n[e] ? s[e] / n[e] : NAN, n[e]); printf(" | 2010-24 %+7.2f (%4d)", n[4] ? s[4] / n[4] : NAN, n[4]);
      if (std::string(ev.nm) != "no event") { int neg = 0; for (int e = 0; e < 3; ++e) neg += n[e] && s[e] < 0; const bool skip = n[4] >= 30 && s[4] < 0 && neg >= 2; printf("  %s", skip ? "→ SKIP these days" : ""); }
      printf("\n"); } }

  // ───────────────────────── 3. quiet-market stand-aside, walk-forward ─────────────────────────
  printf("\n===== 3. QUIET-MARKET STAND-ASIDE (walk-forward by year 2013-24; trade only if filter ≥ threshold) =====\n");
  struct Flt { const char* nm; std::vector<double> th; std::function<double(const Tr&)> f; };
  std::vector<double> vth, ath; for (int x = 11; x <= 22; ++x) vth.push_back(x); for (int x = 6; x <= 20; ++x) ath.push_back(x / 1000.0);
  const std::vector<Flt> FL = {{"prior VIX close", vth, [](const Tr& t) { return t.vix; }}, {"prior ATR-14 / close", ath, [](const Tr& t) { return t.atrp; }}};
  auto wf = [&](const std::vector<Tr>& v, const Flt& fl, bool print) {
    // per year × threshold totals (index 0 = off)
    std::map<int, std::vector<double>> Y; const size_t K = fl.th.size() + 1;
    for (auto& t : v) { auto& r = Y[t.date / 10000]; r.resize(K, 0); const double x = fl.f(t); for (size_t k = 0; k < K; ++k) if (k == 0 || std::isnan(x) || x >= fl.th[k - 1]) r[k] += t.usd; }
    double w[4] = {0}, a[4] = {0};   // 2013-18, 2019-22, 2023-24, total 2013-24
    if (print) printf("  %s:", fl.nm);
    for (int y = 2013; y <= 2024; ++y) { std::vector<double> cum(K, 0); for (auto& [yy, r] : Y) if (yy < y) for (size_t k = 0; k < K; ++k) cum[k] += r[k];
      size_t b = 0; for (size_t k = 1; k < K; ++k) if (cum[k] > cum[b]) b = k;   // ties keep the lower (earlier) threshold, off first
      auto& r = Y[y]; r.resize(K, 0); const int e = y < 2019 ? 0 : y < 2023 ? 1 : 2; w[e] += r[b]; a[e] += r[0]; w[3] += r[b]; a[3] += r[0];
      if (print) { if (b) printf(" %d:≥%g %+.0f/%+.0f", y, fl.th[b - 1], r[b], r[0]); else printf(" %d:off %+.0f", y, r[0]); } }
    if (print) printf("\n");
    return std::array<double, 8>{w[0], w[1], w[2], w[3], a[0], a[1], a[2], a[3]}; };
  auto basket = pick({C, G, M}, true);
  std::map<std::string, double> wtot;
  for (auto& fl : FL) { printf(" basket C+G+M one at a time — %s (year:chosen  walk-forward$/always$):\n", fl.nm); auto r = wf(basket, fl, true);
    const bool pass = r[0] > r[4] && r[3] > r[7] && r[1] >= r[5] - 0.1 * std::fabs(r[5]) && r[2] >= r[6] - 0.1 * std::fabs(r[6]);
    printf("   walk-forward vs always:  2013-18 %+.0f vs %+.0f | 2019-22 %+.0f vs %+.0f | 2023-24 %+.0f vs %+.0f | total %+.0f vs %+.0f  → %s\n",
           r[0], r[4], r[1], r[5], r[2], r[6], r[3], r[7], pass ? "PASS" : "FAIL"); if (pass) wtot[fl.nm] = r[3]; }
  if (wtot.empty()) printf(" VERDICT: no stand-aside filter adopted.\n");
  else { auto b = std::max_element(wtot.begin(), wtot.end(), [](auto& x, auto& y) { return x.second < y.second; }); printf(" VERDICT: adopt the %s filter (walk-forward total %+.0f).\n", b->first.c_str(), b->second); }
  printf(" per strategy (information only; walk-forward / always by era 2013-18 | 2019-22 | 2023-24 | total):\n");
  for (auto& id : ALL) for (auto& fl : FL) { auto r = wf(pick({id}, false), fl, false);
      printf("  %-26s %-20s %+7.0f/%+7.0f | %+7.0f/%+7.0f | %+7.0f/%+7.0f | %+7.0f/%+7.0f\n", id.c_str(), fl.nm, r[0], r[4], r[1], r[5], r[2], r[6], r[3], r[7]); }
  // descriptive: basket $/trade by VIX bucket and ATR% bucket, 2010-24
  printf(" descriptive, basket $/trade 2010-24 by bucket (era 2010-18 / 2019-24):\n");
  auto bucket = [&](const char* nm, std::function<double(const Tr&)> f, std::vector<double> edges) { printf("  %s:", nm);
    for (size_t k = 0; k + 1 < edges.size(); ++k) { double s0 = 0, s1 = 0; int n0 = 0, n1 = 0; for (auto& t : basket) { if (t.date >= 20250101) continue; const double x = f(t); if (!(x >= edges[k] && x < edges[k + 1])) continue; if (t.date < 20190101) { s0 += t.usd; ++n0; } else { s1 += t.usd; ++n1; } }
      printf("  [%g,%g) %+.1f(%d) / %+.1f(%d)", edges[k], edges[k + 1], n0 ? s0 / n0 : NAN, n0, n1 ? s1 / n1 : NAN, n1); } printf("\n"); };
  bucket("VIX", [](const Tr& t) { return t.vix; }, {0, 13, 16, 20, 25, 100});
  bucket("ATR%", [](const Tr& t) { return t.atrp; }, {0, 0.008, 0.010, 0.013, 0.017, 1});
}
