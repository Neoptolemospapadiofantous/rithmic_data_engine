// 2026-10-17 "how does NQ behave": a descriptive map of NQ 1-minute bars 2010-07 → 2026-10.
// Time of day, session split, weekday, month, calendar events, day-to-day memory, intraday momentum — each with
// t-stat and the three eras (2010-18 / 2019-22 / 2023-26). Returns in basis points (log), roll-window days excluded.
#include "research/bars.hpp"
#include <cstdio>
#include <fstream>
#include <map>
#include <functional>
using namespace lab;

struct St { double m = 0, t = 0, pos = 0, sd = 0; int n = 0; };
static St stat(const std::vector<double>& x) {
  St s; s.n = x.size(); if (s.n < 3) return s;
  double a = 0, b = 0, p = 0; for (double v : x) { a += v; b += v * v; p += v > 0; }
  s.m = a / s.n; s.sd = std::sqrt(std::max(0.0, b / s.n - s.m * s.m)); s.t = s.m / (s.sd / std::sqrt((double)s.n)); s.pos = 100 * p / s.n;
  return s;
}
static int era(int date) { return date < 20190101 ? 0 : date < 20230101 ? 1 : 2; }

struct Row { std::vector<double> all, e[3]; void add(int date, double v) { if (std::isnan(v)) return; all.push_back(v); e[era(date)].push_back(v); } };
static int tests = 0;
static void line(const char* label, const Row& r) {
  ++tests;
  St a = stat(r.all), x = stat(r.e[0]), y = stat(r.e[1]), z = stat(r.e[2]);
  bool same = (x.m > 0) == (y.m > 0) && (y.m > 0) == (z.m > 0) && x.n > 2 && y.n > 2 && z.n > 2;
  const char* flag = std::fabs(a.t) >= 3 && same ? "  ***" : std::fabs(a.t) >= 2 && same ? "  *" : "";
  printf("  %-28s n %5d  mean %+7.2f bp  t %+5.2f  up %4.1f%%  sd %6.1f |  eras %+7.2f %+7.2f %+7.2f%s\n",
         label, a.n, a.m, a.t, a.pos, a.sd, x.m, y.m, z.m, flag);
}
static double corr(const std::vector<double>& a, const std::vector<double>& b) {
  int n = a.size(); if (n < 10) return NAN; double ma = 0, mb = 0; for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; } ma /= n; mb /= n;
  double sab = 0, saa = 0, sbb = 0; for (int i = 0; i < n; ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); }
  return sab / std::sqrt(saa * sbb);
}

int main() {
  std::set<int> fomc; { std::ifstream f("data/edge_lab/cache/fomc.txt"); std::string l; while (std::getline(f, l)) { int y, mo, d; if (sscanf(l.c_str(), "%d-%d-%d", &y, &mo, &d) == 3) fomc.insert(y * 10000 + mo * 100 + d); } }
  paper::VixRegime vix; vix.load("data/regime");
  Series s; std::string err; if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const int B = 46;                                    // 30-minute buckets of the trading day 18:00 → 17:00
  struct DR { int k, date; double on, rth, cc, b[B], vol[B], rng[B], first30, last30, r1000_1530, rngatr; bool full; };
  std::vector<DR> R;
  double prev_cl = NAN; int prev_k = -1;
  for (int k = 0; k < (int)s.days.size(); ++k) {
    const Day& d = s.days[k];
    if (!d.ok || d.rollwin || d.date > 20261016) { if (d.ok) { prev_cl = s.c[d.b1 - 1]; prev_k = k; } continue; }
    DR r{}; r.k = k; r.date = d.date;
    double px[B + 1]; for (auto& p : px) p = NAN;
    for (int j = 0; j < B; ++j) { r.vol[j] = 0; r.rng[j] = 0; }
    double bh[B], bl[B]; for (int j = 0; j < B; ++j) { bh[j] = -1e18; bl[j] = 1e18; }
    double last = NAN; int nb = 0;
    // price at each boundary = last close before it (forward fill); open of the day for boundary 0
    for (int i = d.b0; i < d.b1; ++i) {
      int tm = tday_min(s.t[i]); int j = tm / 30; if (j >= B) continue;
      if (std::isnan(px[0])) px[0] = s.o[i];
      for (int q = 1; q <= j; ++q) if (std::isnan(px[q])) px[q] = std::isnan(last) ? s.o[i] : last;
      last = s.c[i]; r.vol[j] += s.v[i]; bh[j] = std::max(bh[j], (double)s.h[i]); bl[j] = std::min(bl[j], (double)s.l[i]); ++nb;
    }
    for (int q = 1; q <= B; ++q) if (std::isnan(px[q])) px[q] = last;
    r.full = nb > 900;
    for (int j = 0; j < B; ++j) { r.b[j] = 1e4 * std::log(px[j + 1] / px[j]); r.rng[j] = bh[j] > bl[j] && d.atr > 0 ? (bh[j] - bl[j]) / d.atr : NAN; }
    bool chain = prev_k >= 0 && !std::isnan(d.pc) && (timegm_ymd(d.date) - timegm_ymd(s.days[prev_k].date)) / 86400 <= 4;
    r.on = chain ? 1e4 * std::log(d.ro / d.pc) : NAN;
    r.rth = 1e4 * std::log(d.rc / d.ro);
    r.cc = chain && !std::isnan(prev_cl) ? 1e4 * std::log(s.c[d.b1 - 1] / prev_cl) : NAN;   // 17:00 → 17:00
    // 09:30-10:00 incl. overnight (Gao et al. first half hour = prior close → 10:00), and 15:30-16:00
    r.first30 = chain ? 1e4 * std::log(px[32] / d.pc) : NAN;     // boundary 32 = 10:00 (18:00 + 16h)
    r.last30 = 1e4 * std::log(px[44] / px[43]);                    // 15:30 → 16:00
    r.r1000_1530 = 1e4 * std::log(px[43] / px[32]);
    r.rngatr = d.atr > 0 ? (d.rh - d.rl) / d.atr : NAN;
    R.push_back(r);
    prev_cl = s.c[d.b1 - 1]; prev_k = k;
  }
  printf("NQ behaviour map — %zu sessions %d → %d (roll windows excluded), bp = log return x 1e4\n", R.size(), R.front().date, R.back().date);
  printf("flags: * |t| >= 2 and same sign in all three eras, *** |t| >= 3 and same sign in all eras. eras = 2010-18 / 2019-22 / 2023-26\n\n");

  printf("== 1. Where the return comes from (per session)\n");
  { Row on, rth, cc; for (auto& r : R) { on.add(r.date, r.on); rth.add(r.date, r.rth); cc.add(r.date, r.cc); }
    line("overnight 16:00 -> 09:30", on); line("RTH 09:30 -> 16:00", rth); line("close-to-close 17:00", cc);
    for (int y = 2010; y <= 2026; ++y) { double a = 0, b = 0; for (auto& r : R) if (r.date / 10000 == y) { if (!std::isnan(r.on)) a += r.on; b += r.rth; }
      printf("    %d  overnight sum %+6.0f bp   RTH sum %+6.0f bp\n", y, a, b); } }

  printf("\n== 2. Time of day (30-minute buckets, ET start time): mean return, and range as a share of the day's ATR\n");
  for (int j = 0; j < B; ++j) { Row x; std::vector<double> rg, vo; for (auto& r : R) { x.add(r.date, r.b[j]); if (!std::isnan(r.rng[j])) rg.push_back(r.rng[j]); if (r.date >= 20190101) vo.push_back(r.vol[j]); }
    int m = (1080 + j * 30) % 1440; char lab[40]; snprintf(lab, sizeof lab, "%02d:%02d  rng %.3f atr  vol %5.0f", m / 60, m % 60, stat(rg).m, stat(vo).m); line(lab, x); }

  printf("\n== 3. Weekday (RTH / overnight before it / range in ATR)\n");
  const char* dn[] = {"", "Mon", "Tue", "Wed", "Thu", "Fri"};
  for (int w = 1; w <= 5; ++w) { Row a, b, c; for (auto& r : R) if (s.days[r.k].dow == w) { a.add(r.date, r.rth); b.add(r.date, r.on); c.add(r.date, r.rngatr); }
    char l1[40]; snprintf(l1, sizeof l1, "%s RTH", dn[w]); line(l1, a); snprintf(l1, sizeof l1, "%s overnight", dn[w]); line(l1, b);
    printf("      %s range %.2f ATR\n", dn[w], stat(c.all).m); }

  printf("\n== 4. Month of year (close-to-close, per session)\n");
  const char* mn[] = {"", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  for (int mo = 1; mo <= 12; ++mo) { Row a; for (auto& r : R) if (r.date / 100 % 100 == mo) a.add(r.date, r.cc); line(mn[mo], a); }

  printf("\n== 5. Calendar events (close-to-close of that session; RTH in brackets below)\n");
  auto ev = [&](const char* label, std::function<bool(const Day&)> f) { Row a, b; for (auto& r : R) if (f(s.days[r.k])) { a.add(r.date, r.cc); b.add(r.date, r.rth); }
    line(label, a); char l2[48]; snprintf(l2, sizeof l2, "  (RTH) %s", label); line(l2, b); };
  ev("all sessions", [](const Day&) { return true; });
  ev("turn of month (last 1 + first 3)", [](const Day& d) { return d.tdleft == 0 || d.tdom <= 3; });
  ev("not turn of month", [](const Day& d) { return !(d.tdleft == 0 || d.tdom <= 3); });
  ev("first trading day", [](const Day& d) { return d.tdom == 1; });
  ev("last trading day", [](const Day& d) { return d.tdleft == 0; });
  ev("mid-month tdom 9-15", [](const Day& d) { return d.tdom >= 9 && d.tdom <= 15; });
  ev("OPEX Friday", [](const Day& d) { return d.opex; });
  ev("OPEX week Mon-Thu", [](const Day& d) { int f = d.date % 100 + (5 - d.dow); return d.dow >= 1 && d.dow <= 4 && f >= 15 && f <= 21; });
  ev("week after OPEX", [](const Day& d) { int f = d.date % 100 - d.dow - 2; return f >= 15 && f <= 21; });
  ev("FOMC day", [](const Day& d) { return d.fomc; });
  ev("NFP Friday", [](const Day& d) { return d.nfp; });
  ev("pre-holiday", [](const Day& d) { return d.prehol; });
  ev("big-tech earnings session", [](const Day& d) { return d.earn; });

  printf("\n== 6. Memory from yesterday (today's RTH return, conditioned on yesterday)\n");
  { std::map<std::string, Row> c;
    for (size_t i = 1; i < R.size(); ++i) { const Day& d = s.days[R[i].k]; if (std::isnan(d.pc) || std::isnan(d.pc2) || std::isnan(d.atr)) continue;
      double pr = (d.pc - d.pc2) / d.atr; double v = R[i].rth;
      c[pr > 1 ? "yday up > 1 ATR" : pr > 0.3 ? "yday up 0.3-1 ATR" : pr > -0.3 ? "yday flat" : pr > -1 ? "yday down 0.3-1 ATR" : "yday down > 1 ATR"].add(R[i].date, v);
      c[d.pibs < 0.2 ? "yday closed bottom 20%" : d.pibs > 0.8 ? "yday closed top 20%" : "yday closed middle"].add(R[i].date, v);
      double g = (d.ro - d.pc) / d.atr;
      c[g > 0.3 ? "gap up > 0.3 ATR" : g < -0.3 ? "gap down > 0.3 ATR" : "gap small"].add(R[i].date, v);
      c[d.vix >= 25 ? "VIX >= 25" : d.vix >= 20 ? "VIX 20-25" : d.vix >= 15 ? "VIX 15-20" : "VIX < 15"].add(R[i].date, v);
      c[d.pc > d.sma50 ? "above 50-day" : "below 50-day"].add(R[i].date, v);
    }
    for (auto& [k, v] : c) line(k.c_str(), v); }

  printf("\n== 7. Intraday momentum / reversal (correlations, per era) — does an earlier move predict a later one?\n");
  auto cr = [&](const char* label, std::function<double(const DR&)> a, std::function<double(const DR&)> b) {
    std::vector<double> xa[4], xb[4];
    for (auto& r : R) { double u = a(r), v = b(r); if (std::isnan(u) || std::isnan(v)) continue; for (int e : {era(r.date), 3}) { xa[e].push_back(u); xb[e].push_back(v); } }
    printf("  %-44s corr all %+.3f (n %zu, t %+.1f) | eras %+.3f %+.3f %+.3f\n", label, corr(xa[3], xb[3]), xa[3].size(),
           corr(xa[3], xb[3]) * std::sqrt((double)xa[3].size()), corr(xa[0], xb[0]), corr(xa[1], xb[1]), corr(xa[2], xb[2])); ++tests; };
  cr("overnight -> RTH", [](const DR& r) { return r.on; }, [](const DR& r) { return r.rth; });
  cr("prior close->10:00 -> 15:30-16:00 (Gao)", [](const DR& r) { return r.first30; }, [](const DR& r) { return r.last30; });
  cr("10:00-15:30 -> 15:30-16:00", [](const DR& r) { return r.r1000_1530; }, [](const DR& r) { return r.last30; });
  cr("09:30-10:00 -> 10:00-16:00", [](const DR& r) { return r.b[31]; }, [](const DR& r) { return 1e4 * 0 + r.r1000_1530 + r.last30; });
  cr("prior close->10:00 -> 10:00-15:30", [](const DR& r) { return r.first30; }, [](const DR& r) { return r.r1000_1530; });
  { std::vector<double> a[4], b[4];
    for (size_t i = 1; i < R.size(); ++i) { if (std::isnan(R[i].on)) continue; for (int e : {era(R[i].date), 3}) { a[e].push_back(R[i - 1].rth); b[e].push_back(R[i].on); } }
    printf("  %-44s corr all %+.3f (n %zu) | eras %+.3f %+.3f %+.3f\n", "RTH -> next overnight (real)", corr(a[3], b[3]), a[3].size(), corr(a[0], b[0]), corr(a[1], b[1]), corr(a[2], b[2]));
    for (auto& v : a) v.clear(); for (auto& v : b) v.clear();
    for (size_t i = 1; i < R.size(); ++i) for (int e : {era(R[i].date), 3}) { a[e].push_back(R[i - 1].rth); b[e].push_back(R[i].rth); }
    printf("  %-44s corr all %+.3f (n %zu) | eras %+.3f %+.3f %+.3f\n", "RTH -> next RTH", corr(a[3], b[3]), a[3].size(), corr(a[0], b[0]), corr(a[1], b[1]), corr(a[2], b[2])); }
  // each 30-min bucket vs the next, RTH
  printf("  consecutive RTH half hours (bucket -> next bucket), all-years corr:\n   ");
  for (int j = 31; j < 44; ++j) { std::vector<double> a, b; for (auto& r : R) { a.push_back(r.b[j]); b.push_back(r.b[j + 1]); }
    int m = (1080 + j * 30) % 1440; printf(" %02d:%02d %+.3f", m / 60, m % 60, corr(a, b)); }
  printf("\n\n== 8. Gao intraday momentum as a trade: last 30 min in the direction of prior close -> 10:00 (per trade, bp, before cost)\n");
  { Row x, big; for (auto& r : R) { if (std::isnan(r.first30) || std::isnan(s.days[r.k].atr)) continue; double sgn = r.first30 > 0 ? 1 : -1; x.add(r.date, sgn * r.last30);
      double atrbp = 1e4 * s.days[r.k].atr / s.days[r.k].ro; if (std::fabs(r.first30) > 0.5 * atrbp) big.add(r.date, sgn * r.last30); }
    line("all days", x); line("first move > 0.5 ATR", big); }
  printf("\n== 9. Follow-ups\n");
  for (int j : {29, 33, 37, 39, 40, 41, 42}) { std::vector<double> a[4], b[4]; for (auto& r : R) for (int e : {era(r.date), 3}) { a[e].push_back(r.b[j]); b[e].push_back(r.b[j + 1]); }
    int m = (1080 + j * 30) % 1440; printf("  bucket %02d:%02d -> next  corr %+.3f | eras %+.3f %+.3f %+.3f\n", m / 60, m % 60, corr(a[3], b[3]), corr(a[0], b[0]), corr(a[1], b[1]), corr(a[2], b[2])); }
  printf("  Monday RTH by year (bp sum):");
  for (int y = 2010; y <= 2026; ++y) { double a = 0; for (auto& r : R) if (r.date / 10000 == y && s.days[r.k].dow == 1) a += r.rth; printf(" %d:%+.0f", y, a); }
  printf("\n  OPEX Friday RTH by year:");
  for (int y = 2010; y <= 2026; ++y) { double a = 0; for (auto& r : R) if (r.date / 10000 == y && s.days[r.k].opex) a += r.rth; printf(" %d:%+.0f", y, a); }
  printf("\n");
  { Row a, b; for (auto& r : R) { const Day& d = s.days[r.k]; if (d.dow != 1) continue; if (d.pibs < 0.5) a.add(r.date, r.rth); else b.add(r.date, r.rth); } line("Mon RTH, Fri closed low half", a); line("Mon RTH, Fri closed high half", b); }
  { Row a, b; for (auto& r : R) { const Day& d = s.days[r.k]; if (d.dow != 1 || std::isnan(d.atr)) continue; double g = (d.ro - d.pc) / d.atr; if (g < 0) a.add(r.date, r.rth); else b.add(r.date, r.rth); } line("Mon RTH, gap down", a); line("Mon RTH, gap up", b); }
  { Row a; for (auto& r : R) if (s.days[r.k].dow == 1) a.add(r.date, r.b[31] + r.b[32] + r.b[33]); line("Mon 09:30-11:00", a); }
  { Row a; for (auto& r : R) if (s.days[r.k].dow == 1) { double x = 0; for (int j = 34; j < 44; ++j) x += r.b[j]; a.add(r.date, x); } line("Mon 11:00-16:00", a); }
  { Row a; for (auto& r : R) if (s.days[r.k].dow != 1) { double x = 0; for (int j = 34; j < 44; ++j) x += r.b[j]; a.add(r.date, x); } line("Tue-Fri 11:00-16:00", a); }
  printf("\n(%d lines tested — with this many, expect ~%d |t|>2 by luck; the era-consistency flag is the filter)\n", tests, (int)(tests * 0.05));
}
