// 2026-10-22 fleet_flag_seq.cpp (founder: "what about testing all the fleet strategies they need to be in sequence with the bull bear flag").
// Every paper-fleet strategy's history trades, split by what the 3-5m flag picture said at the moment of entry:
//   WITH     a flag on the trade's side was alive (registered → flag end / break + 30 min): long inside a bull flag, short inside a bear flag
//   AGAINST  only a flag on the other side was alive
//   NONE     no flag alive
// Flag sets: every flag · A-list flags (walk-forward top third) · A-list in 5m HH/HL (LH/LL for bears) — flag_hhhl.cpp export.
// CONTROL: the same split using the flags of ANOTHER day (5 trading days later) — what time of day alone does; the flag adds only what
// WITH beats its control by. Trades: hist_trades (nq_bars 2021-01 → 2023-02, nq_hist 2024-03 → 2026-10, finished months only), entries
// 09:45-15:00 ET, net $ per MNQ after the fleet's measured costs. Usage: fleet_flag_seq TRADES.csv FLAGS.csv
//   TRADES.csv: strategy_id,dir(1/-1),date(ET),entry ET minute of day,exit ET minute,net $ per MNQ   (psql \copy of hist_trades)
//   FLAGS.csv : flag_hhhl NQ export FILE
// Make TRADES.csv (outside RTH): psql … -c "\copy (select strategy_id, case when direction='LONG' then 1 else -1 end, to_char(entry_time at time zone
//   'America/New_York','YYYYMMDD'), extract(hour from entry_time at time zone 'America/New_York')*60+extract(minute from …entry…), same for exit_time,
//   round((pnl_usd/greatest(qty,1))::numeric,2) from hist_trades where account_label in ('nq_hist','nq_bars') and (entry_time at time zone
//   'America/New_York')::time between '09:45' and '15:00') to 'FILE' csv"   (2026-10-22: 10,879,676 rows, 626 MB — kept out of the repo)
// Read-only. tday = minutes since 18:00 ET = ET minute of day + 360 (09:30 = 930).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>
struct Fl { int side, a, b, grade, hh5; };
enum { NS = 3, NB = 3 };   // flag sets × buckets (0 WITH, 1 AGAINST, 2 NONE)
struct Acc { double n = 0, s = 0, gw = 0, gl = 0, ne[2] = {0, 0}, se[2] = {0, 0}; void add(double x, int era) { n++; s += x; (x > 0 ? gw : gl) += std::fabs(x); ne[era]++; se[era] += x; } };
struct St { std::string id; Acc all, b[NS][NB], c[NS][NB]; };
int main(int argc, char** argv) {
  if (argc < 3) { fprintf(stderr, "usage: fleet_flag_seq TRADES.csv FLAGS.csv\n"); return 1; }
  std::map<int, std::vector<Fl>> fl; std::vector<int> fdays;
  { FILE* f = fopen(argv[2], "r"); if (!f) { perror(argv[2]); return 1; } char ln[256]; fgets(ln, sizeof ln, f);
    while (fgets(ln, sizeof ln, f)) { int d, sd, tf, rg, en, po, gr, hh; if (sscanf(ln, "%d,%d,%d,%d,%d,%d,%d,%d", &d, &sd, &tf, &rg, &en, &po, &gr, &hh) != 8) continue;
      fl[d].push_back({sd, rg, std::max(en, po) + 30, gr, hh}); } fclose(f); }
  auto inSet = [](const Fl& f, int s) { return s == 0 || (f.grade == 1 && (s == 1 || f.hh5)); };
  auto classify = [&](int date, int dir, int tm, int s) { auto it = fl.find(date); if (it == fl.end()) return 2; bool w = false, a = false;
    for (auto& f : it->second) if (inSet(f, s) && tm >= f.a && tm <= f.b) (f.side == dir ? w : a) = true;
    return w ? 0 : a ? 1 : 2; };
  // pass 1: trading dates (for the control day)
  std::vector<St> st; std::unordered_map<std::string, int> sid; std::vector<int> dates;
  struct T { int s, dir, date, tm; float x; }; std::vector<T> tr; tr.reserve(11000000);
  { FILE* f = fopen(argv[1], "r"); if (!f) { perror(argv[1]); return 1; } char ln[512];
    while (fgets(ln, sizeof ln, f)) { char* c = strchr(ln, ','); if (!c) continue; *c = 0; int dir, date, em, xm; double x;
      if (sscanf(c + 1, "%d,%d,%d,%d,%lf", &dir, &date, &em, &xm, &x) != 5) continue;
      auto it = sid.find(ln); int k; if (it == sid.end()) { k = (int)st.size(); sid.emplace(ln, k); st.push_back({}); st.back().id = ln; } else k = it->second;
      tr.push_back({k, dir, date, em + 360, (float)x}); dates.push_back(date); } fclose(f); }
  std::sort(dates.begin(), dates.end()); dates.erase(std::unique(dates.begin(), dates.end()), dates.end());
  std::unordered_map<int, int> ctl; for (size_t i = 0; i < dates.size(); ++i) ctl[dates[i]] = dates[(i + 5) % dates.size()];
  Acc pool[NS][NB], cpool[NS][NB], pall;
  for (auto& t : tr) { const int era = t.date >= 20240101; St& S = st[t.s]; S.all.add(t.x, era); pall.add(t.x, era);
    for (int s = 0; s < NS; ++s) { const int b = classify(t.date, t.dir, t.tm, s), cb = classify(ctl[t.date], t.dir, t.tm, s);
      S.b[s][b].add(t.x, era); S.c[s][cb].add(t.x, era); pool[s][b].add(t.x, era); cpool[s][cb].add(t.x, era); } }
  const char* SN[NS] = {"every flag", "A-list flags", "A-list in 5m HH/HL (LH/LL)"}; const char* BN[NB] = {"WITH the flag", "AGAINST the flag", "no flag alive"};
  auto pf = [](const Acc& a) { return a.gl > 0 ? a.gw / a.gl : 99.0; };
  printf("Fleet strategies in sequence with the 3-5m bull / bear flag — %zu strategies, %zu trades, entries 09:45-15:00 ET, %zu sessions (2021-01 → 2023-02 bars replay, 2024-03 → 2026-10 tick replay).\n", st.size(), tr.size(), dates.size());
  printf("A trade is WITH the flag when a flag on its side was alive at entry (registered → end / break + 30 min). Net $ per MNQ after the fleet's measured costs. CONTROL = the same split with another day's flags (time of day only).\n");
  printf("\nAll trades pooled: %.2f $/trade · PF %.2f\n", pall.s / pall.n, pf(pall));
  for (int s = 0; s < NS; ++s) { printf("\n######## %s\n", SN[s]);
    for (int b = 0; b < NB; ++b) { const Acc& a = pool[s][b]; const Acc& c = cpool[s][b];
      printf("  %-18s %5.1f%% of trades · %+6.2f $/trade PF %.2f · 2021-23 %+6.2f / 2024-26 %+6.2f   | control %5.1f%% of trades %+6.2f $/trade PF %.2f\n", BN[b], 100 * a.n / pall.n, a.s / a.n, pf(a),
             a.ne[0] ? a.se[0] / a.ne[0] : 0, a.ne[1] ? a.se[1] / a.ne[1] : 0, 100 * c.n / pall.n, c.s / c.n, pf(c)); }
    // per strategy: WITH vs the strategy's own average, and vs its control
    int nq = 0, betterAll = 0, betterCtl = 0, posWith = 0, posAll = 0, posBoth = 0; std::vector<double> dAll, dCtl;
    for (auto& S : st) { const Acc& w = S.b[s][0]; const Acc& c = S.c[s][0]; if (w.n < 60 || c.n < 60) continue; nq++;
      const double mw = w.s / w.n, ma = S.all.s / S.all.n, mc = c.s / c.n; dAll.push_back(mw - ma); dCtl.push_back(mw - mc);
      betterAll += mw > ma; betterCtl += mw > mc; posWith += mw > 0; posAll += ma > 0; posBoth += mw > 0 && w.ne[0] >= 20 && w.ne[1] >= 20 && w.se[0] > 0 && w.se[1] > 0; }
    std::sort(dAll.begin(), dAll.end()); std::sort(dCtl.begin(), dCtl.end());
    if (nq) printf("  per strategy (>= 60 WITH trades and 60 control trades: %d): WITH beats the strategy's own average in %.0f%% (median %+.2f $), beats its control in %.0f%% (median %+.2f $)"
                   " · net > 0: all trades %.0f%%, WITH only %.0f%%, WITH > 0 in both eras %.0f%%\n", nq, 100.0 * betterAll / nq, dAll[nq / 2], 100.0 * betterCtl / nq, dCtl[nq / 2], 100.0 * posAll / nq, 100.0 * posWith / nq, 100.0 * posBoth / nq); }
  // the best fleet strategies traded only WITH every flag / the A-list: >= 100 trades, both eras > 0 with >= 30 each, ranked by $/trade, beside their control
  for (int s : {0, 1}) { printf("\n######## Best strategies traded only WITH %s (>= 100 trades, both eras > 0 on >= 30 trades each) — control and the strategy's other trades beside them\n", SN[s]);
    std::vector<int> ix; for (size_t i = 0; i < st.size(); ++i) { const Acc& w = st[i].b[s][0]; if (w.n >= 100 && w.ne[0] >= 30 && w.ne[1] >= 30 && w.se[0] > 0 && w.se[1] > 0) ix.push_back((int)i); }
    std::sort(ix.begin(), ix.end(), [&](int a, int b) { return st[a].b[s][0].s / st[a].b[s][0].n > st[b].b[s][0].s / st[b].b[s][0].n; });
    int nc = 0; for (auto& S : st) { const Acc& c = S.c[s][0]; nc += c.n >= 100 && c.ne[0] >= 30 && c.ne[1] >= 30 && c.se[0] > 0 && c.se[1] > 0; }
    printf("  %zu strategies qualify (with another day's flags — luck alone — %d qualify). Top 30:\n", ix.size(), nc);
    for (size_t r = 0; r < ix.size() && r < 30; ++r) { const St& S = st[ix[r]]; const Acc& w = S.b[s][0]; const Acc& c = S.c[s][0]; Acc o; o.n = S.all.n - w.n; o.s = S.all.s - w.s;
      printf("  %-44s WITH %4.0f tr %+6.2f $ PF %.2f (2021-23 %+6.2f / 2024-26 %+6.2f) · control %4.0f tr %+6.2f $ PF %.2f · rest of its trades %5.0f tr %+6.2f $\n", S.id.c_str(), w.n, w.s / w.n, pf(w),
             w.se[0] / w.ne[0], w.se[1] / w.ne[1], c.n, c.n ? c.s / c.n : 0, pf(c), o.n, o.n ? o.s / o.n : 0); } }
  // by family (strategy id up to the first "__" overlay), WITH every flag vs control, pooled
  printf("\n######## By family (id before the first \"__\"), every flag, pooled over its variants — families with >= 500 WITH trades, sorted by WITH − control\n");
  std::map<std::string, std::array<Acc, 4>> fam;   // WITH, control-WITH, AGAINST, all
  for (auto& S : st) { std::string r = S.id; auto p = r.find("__"); if (p != std::string::npos) r = r.substr(0, p); auto& F = fam[r];
    auto mg = [](Acc& d, const Acc& a) { d.n += a.n; d.s += a.s; d.gw += a.gw; d.gl += a.gl; for (int e = 0; e < 2; ++e) { d.ne[e] += a.ne[e]; d.se[e] += a.se[e]; } };
    mg(F[0], S.b[0][0]); mg(F[1], S.c[0][0]); mg(F[2], S.b[0][1]); mg(F[3], S.all); }
  std::vector<std::pair<double, std::string>> fr; for (auto& kv : fam) if (kv.second[0].n >= 500 && kv.second[1].n >= 500) fr.push_back({kv.second[0].s / kv.second[0].n - kv.second[1].s / kv.second[1].n, kv.first});
  std::sort(fr.rbegin(), fr.rend()); int above = 0; for (auto& x : fr) above += x.first > 0;
  printf("  %zu families, WITH beats control in %d (%.0f%%). Top 25 and bottom 10:\n", fr.size(), above, fr.empty() ? 0.0 : 100.0 * above / fr.size());
  for (size_t r = 0; r < fr.size(); ++r) { if (r >= 25 && r + 10 < fr.size()) continue; const auto& F = fam[fr[r].second];
    printf("  %-36s WITH %6.0f tr %+6.2f $ PF %.2f (2021-23 %+6.2f / 2024-26 %+6.2f) · control %+6.2f $ · AGAINST %+6.2f $ · all %+6.2f $\n", fr[r].second.c_str(), F[0].n, F[0].s / F[0].n, pf(F[0]),
           F[0].ne[0] ? F[0].se[0] / F[0].ne[0] : 0, F[0].ne[1] ? F[0].se[1] / F[0].ne[1] : 0, F[1].s / F[1].n, F[2].n ? F[2].s / F[2].n : 0, F[3].s / F[3].n); }
  return 0;
}
