// 2026-10-28 combo_book.cpp (founder: "need more trades", after wave_entries.cpp showed no single SD setup gives 2-3 good trades a
// day). Instead of loosening one setup, COMBINE setups that each passed edge_lab's full pipeline on their own, using their
// WALK-FORWARD trades (each month traded with the variant that was best on the months before it — edge_lab run --trades writes
// reports/<id>.wf_trades.csv since 2026-10-28) plus the fixed London 5m 2 SD wave (ema721_wave_search dump, one config, no choice).
// Books, 1 micro per trade (NQ setups on MNQ $2/pt, ES setups on MES $5/pt; points are raw, NOT scaled to today's ATR, so dollar
// figures are read per period, and 2021-24 is the one closest to today's size):
//   ALL       every trade of every listed setup (overlapping setups = separate positions — as if each had its own micro)
//   DISTINCT  one setup per family (no cascade + cascade-noon + strong-open triple count, no gap-retest + gap-breakout pair)
//   FLAT      DISTINCT, but one position per market at a time: a signal is skipped while that market already has a trade open
// Common window 2011-07 → 2024-12 (several specs end there; 2025-26 is the sealed holdout for most and is shown for those that run on).
// Max drawdown on the daily equity curve (sum of trades closed that day). Usage: combo_book LIST  (LIST lines: id,market,family,file,fmt
// with fmt edge | wave). Output stdout + out/combo_book.csv (one row per book × period).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
struct T { int date; long long e, x; int dir; double net, usd; int setup; };
struct Setup { std::string id, mkt, fam; double pv; std::vector<T> tr; };
static long long ep(int ymd, int hh, int mm) { std::tm t{}; t.tm_year = ymd / 10000 - 1900; t.tm_mon = ymd / 100 % 100 - 1; t.tm_mday = ymd % 100; t.tm_hour = hh; t.tm_min = mm; return timegm(&t); }
static int per(int d) { return d < 20110701 ? -1 : d <= 20151231 ? 0 : d <= 20201231 ? 1 : d <= 20241231 ? 2 : 3; }
static const char* PN[4] = {"2011-15", "2016-20", "2021-24", "2025-26"};
int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: combo_book LIST\n"); return 2; }
  std::vector<Setup> S; std::ifstream L(argv[1]); std::string line;
  while (std::getline(L, line)) { if (line.empty() || line[0] == '#') continue; std::stringstream ss(line); Setup s; std::string file, fmt;
    std::getline(ss, s.id, ','); std::getline(ss, s.mkt, ','); std::getline(ss, s.fam, ','); std::getline(ss, file, ','); std::getline(ss, fmt, ',');
    s.pv = s.mkt == "ES" ? 5 : 2; const double cost = s.mkt == "ES" ? 0.5 : 1.5;
    std::ifstream f(file); if (!f) { fprintf(stderr, "missing %s\n", file.c_str()); return 1; } std::string r; std::getline(f, r);
    while (std::getline(f, r)) { std::vector<std::string> c; std::stringstream rs(r); std::string x; while (std::getline(rs, x, ',')) c.push_back(x);
      T t{}; t.setup = (int)S.size();
      if (fmt == "edge") { t.date = std::atoi(c[0].c_str()); t.e = std::atoll(c[1].c_str()); t.x = std::atoll(c[2].c_str()); t.dir = std::atoi(c[3].c_str()); t.net = std::atof(c[5].c_str()); }
      else { t.date = std::atoi(c[0].c_str()); const int hm = std::atoi(c[1].c_str()); t.e = ep(t.date, hm / 100, hm % 100) + 60; t.x = t.e + 60LL * std::atoi(c[8].c_str());
        t.dir = std::atoi(c[2].c_str()); t.net = std::atof(c[5].c_str()) - cost; }
      t.usd = t.net * s.pv; if (per(t.date) >= 0) s.tr.push_back(t); }
    S.push_back(std::move(s)); }
  // trading days = union of every date any NQ setup could trade on is not known here; use weekdays in the window
  auto weekdays = [](int a, int b) { int n = 0; for (long long t = ep(a, 12, 0); t <= ep(b, 12, 0); t += 86400) { std::tm g; time_t tt = t; gmtime_r(&tt, &g); if (g.tm_wday >= 1 && g.tm_wday <= 5) ++n; } return n; };
  const int PA[4] = {20110701, 20160101, 20210101, 20250101}, PB[4] = {20151231, 20201231, 20241231, 20261023};
  FILE* o = std::fopen("research/programs/out/combo_book.csv", "w"); fprintf(o, "book,period,trades,per_day,days_with_trade,days_2plus,net_usd,usd_per_trade,pf,max_dd_usd,worst_day_usd,year_usd\n");
  printf("Setups (walk-forward trades, net of cost, 1 micro): n, trades/yr, $/trade and PF per period 2011-15 | 2016-20 | 2021-24 | 2025-26\n");
  for (auto& s : S) { printf("%-17s %-2s %-9s", s.id.c_str(), s.mkt.c_str(), s.fam.c_str());
    for (int p = 0; p < 4; ++p) { double n = 0, u = 0, gw = 0, gl = 0; for (auto& t : s.tr) if (per(t.date) == p) { ++n; u += t.usd; (t.usd > 0 ? gw : gl) += std::fabs(t.usd); }
      if (n) printf(" | %4.0f %+7.1f %4.2f", n, u / n, gl > 0 ? gw / gl : 99); else printf(" |    —            "); }
    printf("\n"); }
  auto book = [&](const std::string& name, const std::vector<int>& members, bool flat) {
    std::vector<T> all; for (int k : members) for (auto& t : S[k].tr) all.push_back(t);
    std::sort(all.begin(), all.end(), [](const T& a, const T& b) { return a.e < b.e; });
    std::vector<T> taken; std::map<std::string, long long> busy;
    for (auto& t : all) { const std::string& m = S[t.setup].mkt; if (flat && t.e < busy[m]) continue; busy[m] = std::max(busy[m], t.x); taken.push_back(t); }
    printf("\n== %s (%zu setups%s)\n%-8s %6s %6s %6s %6s %9s %7s %5s %8s %8s %9s\n", name.c_str(), members.size(), flat ? ", one position per market" : "",
           "period", "trades", "/day", "≥1 d%", "≥2 d%", "net $", "$/tr", "PF", "maxDD $", "worst d", "$/year");
    for (int p = 0; p < 4; ++p) { std::map<int, double> day; std::map<int, int> cnt; double n = 0, u = 0, gw = 0, gl = 0;
      for (auto& t : taken) if (per(t.date) == p) { ++n; u += t.usd; (t.usd > 0 ? gw : gl) += std::fabs(t.usd); day[t.date] += t.usd; cnt[t.date]++; }
      if (!n) continue; double eq = 0, pk = 0, dd = 0, worst = 0; for (auto& [d, v] : day) { eq += v; pk = std::max(pk, eq); dd = std::max(dd, pk - eq); worst = std::min(worst, v); }
      const int wd = weekdays(PA[p], PB[p]); int d1 = 0, d2 = 0; for (auto& [d, c] : cnt) { ++d1; if (c >= 2) ++d2; }
      const double yrs = wd / 261.0;
      printf("%-8s %6.0f %6.2f %5.0f%% %5.0f%% %+9.0f %+7.1f %5.2f %8.0f %8.0f %+9.0f\n", PN[p], n, n / wd, 100.0 * d1 / wd, 100.0 * d2 / wd, u, u / n, gl > 0 ? gw / gl : 99, dd, worst, u / yrs);
      fprintf(o, "%s,%s,%.0f,%.3f,%.3f,%.3f,%.0f,%.2f,%.3f,%.0f,%.0f,%.0f\n", name.c_str(), PN[p], n, n / wd, (double)d1 / wd, (double)d2 / wd, u, u / n, gl > 0 ? gw / gl : 99, dd, worst, u / yrs); } };
  std::vector<int> every, distinct; std::set<std::string> fams;
  for (size_t k = 0; k < S.size(); ++k) { every.push_back((int)k); if (fams.insert(S[k].mkt + ":" + S[k].fam).second) distinct.push_back((int)k); }
  book("ALL", every, false); book("DISTINCT", distinct, false); book("FLAT", distinct, true);
  std::vector<int> nq; for (int k : distinct) if (S[k].mkt == "NQ") nq.push_back(k); book("DISTINCT_NQ_ONLY", nq, false); book("FLAT_NQ_ONLY", nq, true);
  std::fclose(o); return 0;
}
