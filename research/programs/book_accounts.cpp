// 2026-10-28 book_accounts.cpp (founder: "we will be trading minis … make a plan for the day … follow strategies that have winning R
// and PF … with 2 rotating accounts. I don't want to keep guessing myself"; answers: accounts not decided, NQ + ES, SPLIT SETUPS —
// account A takes the NY-open setups, account B London / trend / holds; "we also have the trend strategies, the cascade, all the
// others"). Reads the same list as combo_book.cpp (walk-forward trades of every edge_lab PASS setup + the London wave), each trade
// now carrying its stop and that day's ATR, and answers per setup and per account, AT TODAY'S SIZE:
//   SETUP CARD  2016-26 (2011-15 shown apart — almost every setup is flat there): trades/yr, win %, avg win / loss in R, expectancy R,
//               PF, years positive; the stop today = stop_pts / ATR_then × ATR_today, in points and $ per mini (NQ $20, ES $50).
//               R = net points / stop points; setups without a stop (holds) use 1 R = 0.5 ATR and say so.
//   ACCOUNTS    1 mini per trade, points scaled to today's ATR. A and B each one position at a time; a trade is SKIPPED when the other
//               account holds the OPPOSITE direction in NQ or ES at that moment (cross-account hedging is banned — Tradeify FTA §6.7 —
//               and NQ / ES move together). Per account: trades/day, PF, $/year, worst day, max drawdown of the end-of-day equity, and
//               whether it fits a 50K ($2,000 trailing) / 150K ($4,500 trailing) account; also the share of trades whose stop alone
//               is > 40 % of that trailing allowance (those need micros on that account).
// Usage: book_accounts LIST. Output stdout + out/book_accounts.csv. Periods: 2016-01 → 2026-10-23 (cards and accounts), 2011-15 apart.
#include "research/meta.hpp"
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
using namespace lab;
struct Tr { int date; long long e, x; int dir; double net, stop, atr; int k; };
struct Setup { std::string id, mkt, acct; std::vector<Tr> tr; bool nostop = false; };
static long long ep(int ymd, int hh, int mm) { std::tm t{}; t.tm_year = ymd / 10000 - 1900; t.tm_mon = ymd / 100 % 100 - 1; t.tm_mday = ymd % 100; t.tm_hour = hh; t.tm_min = mm; return timegm(&t); }
// account A = the NY-open setups (decided at fixed checks 09:30-11:30); B = London wave, intraday trend / momentum, the day holds
static std::string acct_of(const std::string& id) {
  for (const char* a : {"so_cascade", "cascade_noon", "strong_open", "gap_or_retest", "gap_breakout", "bdh_opt", "scalp_m1_1000", "dir_market", "es_cascade", "es_gap_or_retest"}) if (id == a) return "A";
  return "B"; }
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: book_accounts LIST\n"); return 2; }
  double atrNow[2] = {0, 0};
  for (int m = 0; m < 2; ++m) { std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", m ? "ES" : "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (int di = (int)s.days.size() - 1; di >= 0; --di) if (s.days[di].ok && s.days[di].atr > 0) { atrNow[m] = s.days[di].atr; break; } }
  std::vector<Setup> S; std::ifstream L(argv[1]); std::string line;
  while (std::getline(L, line)) { if (line.empty() || line[0] == '#') continue; std::stringstream ss(line); Setup s; std::string fam, file, fmt;
    std::getline(ss, s.id, ','); std::getline(ss, s.mkt, ','); std::getline(ss, fam, ','); std::getline(ss, file, ','); std::getline(ss, fmt, ',');
    s.acct = acct_of(s.id); const double cost = s.mkt == "ES" ? 0.5 : 1.5;
    std::ifstream f(file); if (!f) { fprintf(stderr, "missing %s\n", file.c_str()); return 1; } std::string r; std::getline(f, r); int zero = 0, n = 0;
    while (std::getline(f, r)) { std::vector<std::string> c; std::stringstream rs(r); std::string x; while (std::getline(rs, x, ',')) c.push_back(x);
      Tr t{}; t.k = (int)S.size(); t.date = std::atoi(c[0].c_str());
      if (fmt == "edge") { t.e = std::atoll(c[1].c_str()); t.x = std::atoll(c[2].c_str()); t.dir = std::atoi(c[3].c_str()); t.net = std::atof(c[5].c_str());
        t.stop = std::atof(c[9].c_str()); t.atr = std::atof(c[10].c_str()); }
      else { const int hm = std::atoi(c[1].c_str()); t.e = ep(t.date, hm / 100, hm % 100) + 60; t.x = t.e + 60LL * std::atoi(c[8].c_str()); t.dir = std::atoi(c[2].c_str());
        t.net = std::atof(c[5].c_str()) - cost; t.stop = std::atof(c[4].c_str()); t.atr = std::atof(c[7].c_str()); }
      if (!(t.atr > 0)) continue; ++n; if (!(t.stop > 0)) ++zero; if (t.date >= 20110701) s.tr.push_back(t); }
    s.nostop = zero > n / 2; S.push_back(std::move(s)); }
  FILE* o = std::fopen("research/programs/out/book_accounts.csv", "w");
  fprintf(o, "setup,market,account,trades_yr,win_pct,avg_win_r,avg_loss_r,exp_r,pf,years_pos,years,pf_2011_15,stop_pts_today,stop_usd_mini\n");
  printf("Today's ATR: NQ %.0f pts, ES %.1f pts. Cards 2016-01 → 2026-10 (walk-forward trades, net of cost). R = net / stop.\n\n", atrNow[0], atrNow[1]);
  printf("%-17s %-2s %-4s %6s %5s %6s %6s %6s %5s %6s %8s | %-15s\n", "setup", "mk", "acct", "tr/yr", "win%", "winR", "lossR", "expR", "PF", "yrs+", "PF11-15", "stop today 1 mini");
  for (auto& s : S) { const int m = s.mkt == "ES"; const double pv = m ? 50 : 20;
    double n = 0, w = 0, sw = 0, sl = 0, nl = 0, gw = 0, gl = 0, e0n = 0, e0w = 0, e0l = 0, sa = 0, na = 0; std::map<int, double> yr; int y0 = 99999, y1 = 0;
    for (auto& t : s.tr) { const double R = s.nostop || !(t.stop > 0) ? 0.5 * t.atr : t.stop, r = t.net / R;
      if (t.date < 20160101) { e0n++; (t.net > 0 ? e0w : e0l) += std::fabs(t.net / t.atr); continue; }
      n++; yr[t.date / 10000] += t.net / t.atr; y0 = std::min(y0, t.date / 10000); y1 = std::max(y1, t.date / 10000);
      if (r > 0) { w++; sw += r; } else { nl++; sl += r; } (t.net > 0 ? gw : gl) += std::fabs(t.net / t.atr);
      sa += R / t.atr; na++; }
    if (!n) continue; int yp = 0; for (auto& [y, v] : yr) yp += v > 0;
    const double years = (y1 - y0) + (y1 == 2026 ? 0.82 : 1.0), stopAtr = sa / na, stopPts = stopAtr * atrNow[m];
    printf("%-17s %-2s %-4s %6.0f %5.0f %+6.2f %+6.2f %+6.3f %5.2f %3d/%-2zu %8.2f | %s%4.0f pts = $%.0f\n", s.id.c_str(), s.mkt.c_str(), s.acct.c_str(), n / years, 100 * w / n, w ? sw / w : 0,
           nl ? sl / nl : 0, (sw + sl) / n, gl > 0 ? gw / gl : 99, yp, yr.size(), e0l > 0 ? e0w / e0l : 99, s.nostop ? "no stop, 1R≈" : "", stopPts, stopPts * pv);
    fprintf(o, "%s,%s,%s,%.1f,%.1f,%.3f,%.3f,%.4f,%.3f,%d,%zu,%.3f,%.1f,%.0f\n", s.id.c_str(), s.mkt.c_str(), s.acct.c_str(), n / years, 100 * w / n, w ? sw / w : 0, nl ? sl / nl : 0,
            (sw + sl) / n, gl > 0 ? gw / gl : 99, yp, yr.size(), e0l > 0 ? e0w / e0l : 99, stopPts, stopPts * pv); }
  // accounts: one setup per family is NOT applied here — every listed setup trades on its account, one position per account at a time
  std::vector<Tr> all; for (auto& s : S) for (auto& t : s.tr) if (t.date >= 20160101) all.push_back(t);
  std::sort(all.begin(), all.end(), [](const Tr& a, const Tr& b) { return a.e < b.e; });
  struct Pos { long long until = 0; int dir = 0; };
  Pos pos[2]; std::map<int, double> day[2]; std::map<int, int> cnt[2]; double gw[2] = {}, gl[2] = {}, risk[2][2] = {}; int ntr[2] = {}, skipHedge = 0, skipBusy = 0;
  for (auto& t : all) { const Setup& s = S[t.k]; const int a = s.acct == "A" ? 0 : 1, b = 1 - a, m = s.mkt == "ES";
    if (t.e < pos[a].until) { ++skipBusy; continue; }
    if (t.e < pos[b].until && pos[b].dir == -t.dir) { ++skipHedge; continue; }
    pos[a] = {t.x, t.dir};
    const double pv = m ? 50 : 20, usd = t.net / t.atr * atrNow[m] * pv;
    const double stopUsd = (s.nostop || !(t.stop > 0) ? 0.5 * t.atr : t.stop) / t.atr * atrNow[m] * pv;
    day[a][t.date] += usd; cnt[a][t.date]++; ntr[a]++; (usd > 0 ? gw[a] : gl[a]) += std::fabs(usd);
    if (stopUsd > 0.4 * 2000) risk[a][0]++; if (stopUsd > 0.4 * 4500) risk[a][1]++; }
  int wd = 0; for (long long t = ep(20160101, 12, 0); t <= ep(20261023, 12, 0); t += 86400) { std::tm g; time_t tt = t; gmtime_r(&tt, &g); if (g.tm_wday >= 1 && g.tm_wday <= 5) ++wd; }
  printf("\nACCOUNTS 2016-01 → 2026-10, 1 mini per trade at today's size (one position per account; %d signals skipped — account busy, %d skipped — opposite to the other account's open trade)\n", skipBusy, skipHedge);
  printf("%-4s %8s %6s %5s %10s %9s %10s %10s %8s %8s\n", "acct", "trades", "/day", "PF", "$/year", "worst d", "max DD", "DD 2025+", "stop>40%", "stop>40%");
  printf("%-4s %8s %6s %5s %10s %9s %10s %10s %8s %8s\n", "", "", "", "", "", "", "", "", "of 50K", "of 150K");
  for (int a = 0; a < 2; ++a) { double eq = 0, pk = 0, dd = 0, worst = 0, eq2 = 0, pk2 = 0, dd2 = 0, tot = 0;
    for (auto& [d, v] : day[a]) { eq += v; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); worst = std::min(worst, v); tot += v;
      if (d >= 20250101) { eq2 += v; pk2 = std::max(pk2, eq2); dd2 = std::max(dd2, pk2 - eq2); } }
    printf("%-4s %8d %6.2f %5.2f %+10.0f %9.0f %10.0f %10.0f %7.0f%% %7.0f%%\n", a ? "B" : "A", ntr[a], (double)ntr[a] / wd, gl[a] > 0 ? gw[a] / gl[a] : 99, tot / (wd / 261.0), worst, dd, dd2,
           100 * risk[a][0] / std::max(1, ntr[a]), 100 * risk[a][1] / std::max(1, ntr[a]));
    fprintf(o, "ACCOUNT_%s,,,%.2f,,,,,%.3f,,,,%.0f,%.0f\n", a ? "B" : "A", (double)ntr[a] / wd, gl[a] > 0 ? gw[a] / gl[a] : 99, dd, tot / (wd / 261.0)); }
  std::fclose(o); return 0;
}
