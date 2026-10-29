// 2026-10-28 daily_book.cpp (founder: "which ever I can use get an edge on a daily basis and also where I can trade 1-3 trades a
// day. as many as we can obviously"). Books made only of setups that were profitable in EVERY period at today's size and the
// live-measured micro cost (2011-15, 2016-20, 2021-24, 2025-26), one per family; walk-forward trades from scripts/readiness.sh.
// Every trade: points scaled to today's ATR (NQ / ES read from the edge_lab cache), minus the live micro round trip (NQ 4.2 pts,
// ES 1.27 pts), on 1 micro (MNQ $2, MES $5). Trades are taken in entry-time order; a signal is skipped while that account already
// has a position open or the day's cap is reached. Account B (the day holds) and account A never hold opposite sides of NQ / ES at
// the same time: A skips a signal against B's open hold (cross-account hedging is banned).
// SELECTION NOTE: the setups were picked with 2025-26 visible, so this is not a clean test of the book — only future sessions are.
// Usage: daily_book LIST (id,market,file,fmt — same files as readiness). Output stdout + out/daily_book.csv.
#include "research/meta.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
using namespace lab;
struct Tr { int date; long long e, x; int dir; double usd; int k; };
static long long ep(int ymd, int hh, int mm) { std::tm t{}; t.tm_year = ymd / 10000 - 1900; t.tm_mon = ymd / 100 % 100 - 1; t.tm_mday = ymd % 100; t.tm_hour = hh; t.tm_min = mm; return timegm(&t); }
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: daily_book LIST\n"); return 2; }
  double atrNow[2] = {0, 0};
  for (int m = 0; m < 2; ++m) { std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", m ? "ES" : "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (int di = (int)s.days.size() - 1; di >= 0; --di) if (s.days[di].ok && s.days[di].atr > 0) { atrNow[m] = s.days[di].atr; break; } }
  std::vector<std::string> ids; std::vector<std::vector<Tr>> T; std::vector<std::string> mk;
  std::ifstream L(argv[1]); std::string line;
  while (std::getline(L, line)) { if (line.empty() || line[0] == '#') continue; std::stringstream ss(line); std::string id, m, file, fmt;
    std::getline(ss, id, ','); std::getline(ss, m, ','); std::getline(ss, file, ','); std::getline(ss, fmt, ',');
    const int es = m == "ES"; const double mic = es ? 1.27 : 4.2, pv = es ? 5 : 2;
    std::ifstream f(file); if (!f) { fprintf(stderr, "missing %s\n", file.c_str()); return 1; } std::string r; std::getline(f, r); std::vector<Tr> v;
    while (std::getline(f, r)) { std::vector<std::string> c; std::stringstream rs(r); std::string x; while (std::getline(rs, x, ',')) c.push_back(x);
      Tr t{}; t.k = (int)ids.size(); t.date = std::atoi(c[0].c_str()); double g, atr;
      if (fmt == "edge") { t.e = std::atoll(c[1].c_str()); t.x = std::atoll(c[2].c_str()); t.dir = std::atoi(c[3].c_str()); g = std::atof(c[4].c_str()); atr = std::atof(c[10].c_str()); }
      else { const int hm = std::atoi(c[1].c_str()); t.e = ep(t.date, hm / 100, hm % 100) + 60; t.x = t.e + 60LL * std::atoi(c[8].c_str()); t.dir = std::atoi(c[2].c_str()); g = std::atof(c[5].c_str()); atr = std::atof(c[7].c_str()); }
      if (!(atr > 0) || t.date < 20110701 || t.date > 20261023) continue;
      t.usd = (g / atr * atrNow[es] - mic) * pv; v.push_back(t); }
    ids.push_back(id); mk.push_back(m); T.push_back(std::move(v)); }
  auto idx = [&](const std::string& s) { for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == s) return (int)i; fprintf(stderr, "no %s\n", s.c_str()); std::exit(1); };
  int wdays = 0; for (long long t = ep(20110701, 12, 0); t <= ep(20261023, 12, 0); t += 86400) { std::tm g; time_t tt = t; gmtime_r(&tt, &g); if (g.tm_wday >= 1 && g.tm_wday <= 5) ++wdays; }
  FILE* o = std::fopen("research/programs/out/daily_book.csv", "w");
  fprintf(o, "book,trades,per_day,days_traded_pct,pf,usd_year,max_dd,worst_day,years_pos,months_pos_pct,p1,p2,p3,p4\n");
  // A = intraday setups, cap per day, one position at a time; B = holds, one at a time; A never against B's open hold
  auto run = [&](const char* name, std::vector<std::string> A, std::vector<std::string> B, int cap) {
    std::vector<Tr> all; std::vector<int> acct(ids.size(), -1);
    for (auto& s : A) { const int k = idx(s); acct[k] = 0; for (auto& t : T[k]) all.push_back(t); }
    for (auto& s : B) { const int k = idx(s); acct[k] = 1; for (auto& t : T[k]) all.push_back(t); }
    std::sort(all.begin(), all.end(), [](const Tr& a, const Tr& b) { return a.e < b.e; });
    long long until[2] = {0, 0}; int dirOpen[2] = {0, 0}; std::map<int, int> taken[2]; std::map<int, double> day; std::map<int, int> cnt;
    std::map<int, double> mon, yr; double gw = 0, gl = 0; int n = 0; double per[5] = {}; int pn[5] = {};
    for (auto& t : all) { const int a = acct[t.k];
      if (t.e < until[a]) continue;
      if (a == 0 && taken[0][t.date] >= cap) continue;
      if (a == 0 && t.e < until[1] && dirOpen[1] == -t.dir) continue;
      until[a] = t.x; dirOpen[a] = t.dir; taken[a][t.date]++;
      day[t.date] += t.usd; cnt[t.date]++; mon[t.date / 100] += t.usd; yr[t.date / 10000] += t.usd; (t.usd > 0 ? gw : gl) += std::fabs(t.usd); ++n;
      const int p = t.date <= 20151231 ? 1 : t.date <= 20201231 ? 2 : t.date <= 20241231 ? 3 : 4; per[p] += t.usd; pn[p]++; }
    double eq = 0, pk = 0, dd = 0, worst = 0, tot = 0; for (auto& [d, v] : day) { eq += v; tot += v; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); worst = std::min(worst, v); }
    int yp = 0, mp = 0; for (auto& [y, v] : yr) yp += v > 0; for (auto& [m, v] : mon) mp += v > 0;
    const double years = wdays / 261.0;
    printf("\n== %s\n   %d trades, %.2f a day, a trade on %.0f%% of weekdays · PF %.2f · $%+.0f a year on 1 micro · max drawdown $%.0f · worst day $%.0f · %d/%zu years and %.0f%% of months positive\n",
           name, n, n / (double)wdays, 100.0 * cnt.size() / wdays, gl > 0 ? gw / gl : 99, tot / years, dd, worst, yp, yr.size(), 100.0 * mp / std::max<size_t>(1, mon.size()));
    printf("   $/trade 2011-15 %+.1f · 2016-20 %+.1f · 2021-24 %+.1f · 2025-26 %+.1f\n", pn[1] ? per[1] / pn[1] : 0, pn[2] ? per[2] / pn[2] : 0, pn[3] ? per[3] / pn[3] : 0, pn[4] ? per[4] / pn[4] : 0);
    fprintf(o, "%s,%d,%.3f,%.1f,%.3f,%.0f,%.0f,%.0f,%d,%.1f,%.2f,%.2f,%.2f,%.2f\n", name, n, n / (double)wdays, 100.0 * cnt.size() / wdays, gl > 0 ? gw / gl : 99, tot / years, dd, worst, yp,
            100.0 * mp / std::max<size_t>(1, mon.size()), pn[1] ? per[1] / pn[1] : 0, pn[2] ? per[2] / pn[2] : 0, pn[3] ? per[3] / pn[3] : 0, pn[4] ? per[4] / pn[4] : 0); };
  printf("1 micro per trade, today's size (ATR NQ %.0f / ES %.1f), live micro cost. Selection used every period incl. 2025-26 — forward sessions are the clean test.\n", atrNow[0], atrNow[1]);
  const std::vector<std::string> core = {"wave_ldn5", "so_cascade", "gap_or_retest", "mom_scan_3_pb", "scalp_m1_1000"};
  std::vector<std::string> wide = core; for (auto s : {"bdh_opt", "es_cascade"}) wide.push_back(s);
  const std::vector<std::string> holds = {"monday_long", "weak_day_long"};
  for (int cap : {1, 2, 3}) { char nm[96]; std::snprintf(nm, sizeof nm, "A CORE intraday, max %d a day", cap); run(nm, core, {}, cap); }
  run("A WIDE intraday (+ bdh_opt, es_cascade), max 3 a day", wide, {}, 3);
  run("B HOLDS only (Monday long, weak-day long)", {}, holds, 9);
  run("A CORE max 3 + B HOLDS (no opposite positions)", core, holds, 3);
  run("A WIDE max 3 + B HOLDS (no opposite positions)", wide, holds, 3);
  std::fclose(o); return 0;
}
