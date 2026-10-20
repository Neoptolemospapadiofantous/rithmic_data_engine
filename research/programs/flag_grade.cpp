// 2026-10-20 flag_grade.cpp (founder: "can we optimize per chart to possibly get more trades and also categorize each trade based on
// kpis" → "do the test to see if numbers go up"). Detection, features, entries and exits are flag_take_opt.cpp's (copied verbatim up to
// the reports; parity-checked against the /flags page). Two walk-forward tests, NY, 1-5m, bull and bear separately:
//  A PER-CHART RULE  for each chart, each test year 2013..2026 trades the subset of the 7 checklist items with the best mean R over all
//                    EARLIER years on that chart (>= 40 trades there; stand aside if <= 0). Compared with the fixed TAKE rule (bull 4/5m).
//  B GRADE           each flag gets a score = Σ over the checklist items it has of that item's edge (mean R with it − mean R without),
//                    the weights and the A/B/C cut points (thirds of the training scores) learned on EARLIER years only, 2-5m pooled;
//                    then each grade's record in the test years. One position at a time per chart. R after 2 pts × ATR/320.
// (original header of flag_take_opt.cpp follows)
// 2026-10-20 (founder: "just gotta take the good ones" → "run all three optimizations"): what is the best way to TAKE the /flags
// page's flags, chosen without hindsight. Flags = the page's exact rules (NY 3/4/5m, bull + bear mirrored, pole >= 4x the session's
// average bar, 2 flag bars, pullback 15-50 %, registered 09:45-14:00, open context: above the 09:30 open and VWAP, straightness >= 0.55).
// Features = the page's checklist: 1st pullback since the low/high of day, pole broke a level (prior day, overnight, opening range
// after 10:00, round 100), gap with the flag > 0.1 ATR, pole >= 5x bar, before 12:30, <= 1 smaller 1m flag in the pole (flag_inner),
// bear down day (gap down + prior close below SMA50 + VIX up the day before), chart 4/5m.
// 1 WALK-FORWARD RULE  per side, every subset of 7 checklist items (128 rules); each test year 2013..2026 trades the rule with the best
//                     mean R over all EARLIER years (>= 60 trades there; stand aside if that best is <= 0). Compared with fixed rules.
// 2 EXITS             on the TAKE candidates (1st pullback): measured move (the page) | 1.5R | 2R | 3R | measured move + stop to
//                     break-even after +1R | 2R + break-even. Each against its random-timing twin (same day, same context, 10:00-14:00).
// 3 ENTRIES           on the same candidates, measured-move target: 2m trigger else pole-top stop (the page) | pole-top stop only |
//                     2m trigger only | limit halfway back into the flag (H − 0.5·(H − flag low), placed at registration, valid until the
//                     flag breaks out, fails or 12 bars; stop = flag low at fill − 1 tick).
// One position at a time per day for every stream (a flag whose entry comes before the previous trade's exit is skipped). R after
// 2 pts cost scaled to the day, flat 15:55. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <random>
using namespace lab;
struct FB { double o, h, l, c, v; int k0, k; };
enum { PB1, BROKE, GAPW, POLE5, B1230, FEWBOX, DOWNDAY, TF45, NFEAT };
enum { EX_MM, EX_15, EX_2, EX_3, EX_MMBE, EX_2BE, NEX };
enum { EN_PAGE, EN_POLE, EN_2M, EN_LIMIT, NEN };
static const char* EXN[NEX] = {"measured move (the page)", "1.5R target", "2R target", "3R target", "measured move + break-even at +1R", "2R + break-even at +1R"};
static const char* ENN[NEN] = {"2m trigger, else pole-top stop (the page)", "pole-top stop order only", "2m trigger only", "limit halfway into the flag"};
struct FTrade { double R = NAN, ctl = NAN; int kIn = -1, kOut = -1; };
struct Row { int date, year, era, side, tf; double slope = NAN, riskCost = NAN, riskPts = NAN, cost = NAN; bool f[NFEAT]; FTrade ex[NEX], en[NEN]; };
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  std::mt19937 rng(20261020), rng12(20261021); /* rng12: the 1-2m controls (section 6) — keeps the 3-5m draws unchanged */ std::vector<Row> rows; long nDays = 0;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1320) I.push_back(i); } if (I.size() < 370) continue;
    nDays++; const double A = d.atr, cost = 2 * A / 320.0; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    double onh = -1e18, onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { if (tday_min(s.t[i]) >= 930) break; onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < 960; ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n); std::vector<int> TM(n); std::vector<char> loose(n), strict(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      const double so = O[0];
      { double pv = 0, vv = 0, hi = -1e18, lo = 1e18;
        for (size_t k = 0; k < n; ++k) { pv += (Hh[k] + Lw[k] + Cc[k]) / 3.0 * V[k]; vv += V[k]; hi = std::max(hi, Hh[k]); lo = std::min(lo, Lw[k]);
          const double vw = vv > 0 ? pv / vv : Cc[k]; loose[k] = Cc[k] > so && Cc[k] > vw; strict[k] = loose[k] && hi > lo && (Cc[k] - so) / (hi - lo) >= 0.55; } }
      const double pc = side * d.pc, gapA = std::isnan(pc) ? 0 : (so - pc) / A;
      const bool trend50 = !std::isnan(pc) && !std::isnan(d.sma50) && pc - side * d.sma50 > 0, vixUp = !std::isnan(d.vixc1) && d.vixc1 * (side > 0 ? -1 : 1) > 0;
      // exit simulation: entry at minute k0 (fillMin: the entry minute itself can stop out), returns R and exit minute
      auto sim = [&](size_t k0, double e, double st, double tg, bool be, bool fillMin, int& kOut) -> double { const double risk = e - st; double stop = st, px = NAN; kOut = n - 1;
        if (fillMin && Lw[k0] <= st) { px = st; kOut = k0; }
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= stop) { px = stop; kOut = k; break; } if (Hh[k] >= tg) { px = tg; kOut = k; break; } if (TM[k] >= 1315) { px = O[k]; kOut = k; break; } if (be && Hh[k] >= e + risk) stop = std::max(stop, e); }
        if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / risk; };
      auto exitTarget = [&](int x, double e, double risk, double mm) { return x == EX_MM || x == EX_MMBE ? mm : x == EX_15 ? e + 1.5 * risk : x == EX_2 || x == EX_2BE ? e + 2 * risk : e + 3 * risk; };
      auto bars = [&](int tf) { std::vector<FB> b; for (size_t k = 0; k < n; ++k) { if (b.empty() || (TM[k] - 930) / tf != (TM[b.back().k0] - 930) / tf) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], (int)k, (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.k = k; } } return b; };
      struct Det { int kPole, kTop, q, l, h; double H, L, M, ar; };
      auto detect = [&](const std::vector<FB>& b, int tf, bool inner) { std::vector<Det> out; int busy = -1;
        for (int q = 6; q < (int)b.size(); ++q) { if (q <= busy) continue; const int minute = (TM[b[q].k0] - 930) + tf; if (minute < 15 || minute >= 270) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < (inner ? 2 : 4) * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const int kc = b[q].k; if (inner ? (!loose[kc] || (strict[kc] && M >= 4 * ar)) : !strict[kc]) continue;
          int nb = 0; for (size_t kk = b[q].k + 1; kk < n; ++kk) { if (Hh[kk] > H) break; if ((TM[kk] - 930) % tf == tf - 1) { nb++; if (Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
          out.push_back({b[l].k0, b[h].k0, q, l, h, H, L, M, ar}); busy = q + nb; }
        return out; };
      const auto b1 = bars(1); std::vector<int> tops1; for (auto& x : detect(b1, 1, false)) tops1.push_back(x.kTop); for (auto& x : detect(b1, 1, true)) tops1.push_back(x.kTop);
      std::vector<size_t> ctxPool; for (size_t k = 0; k < n; ++k) if (TM[k] >= 960 && TM[k] < 1200 && strict[k]) ctxPool.push_back(k);
      // tf 1-2 only feed section 6 (sections 1-5 are 3-5m)
      for (int tf = 1; tf <= 5; ++tf) { const auto b = bars(tf); const auto fl = detect(b, tf, false); std::vector<int> regK; double lod = 1e18; size_t lk = 0, scan = 0;
        for (auto& f : fl) { const size_t kq = b[f.q].k; for (; scan <= kq; ++scan) if (Lw[scan] < lod) { lod = Lw[scan]; lk = scan; }
          Row r{}; r.date = d.date; r.year = d.date / 10000; r.era = era; r.side = side; r.tf = tf;
          int c = 0; for (int rk : regK) if (rk > (int)lk) c++; regK.push_back(kq);
          const double H = f.H, L = f.L, M = f.M; const int minute = (TM[b[f.q].k0] - 930) + tf;
          std::vector<double> lv = {side > 0 ? d.ph : -d.pl, side > 0 ? onh : -onl, side > 0 ? d.pl : -d.ph, side > 0 ? onl : -onh};
          if (TM[kq] + 1 >= 960) { lv.push_back(side > 0 ? orh : -orl); lv.push_back(side > 0 ? orl : -orh); }
          bool broke = false; for (double x : lv) if (!std::isnan(x) && x > L && x <= H) broke = true;
          { const double rawH = side * H, rawL = side * L; if (std::floor(std::max(rawH, rawL) / 100) * 100 > std::min(rawH, rawL)) broke = true; }
          int pauses = 0; for (int t : tops1) if (t >= f.kPole && t < f.kTop) pauses++;
          r.f[PB1] = c == 0; r.f[BROKE] = broke; r.f[GAPW] = gapA > 0.1; r.f[POLE5] = M >= 5 * f.ar; r.f[B1230] = minute < 180; r.f[FEWBOX] = pauses <= 1;
          r.f[DOWNDAY] = gapA > 0.1 && trend50 && vixUp; r.f[TF45] = tf >= 4;
          // follow the flag: 2m trigger, pole-top break, limit fill
          const double fl0 = std::min(b[f.h + 1].l, b[f.q].l), lim = H - 0.5 * (H - fl0);
          double flo = fl0, hbl = 1e18, p2h = NAN, b2h = -1e18, runLow = fl0; bool lowerHigh = false; int nb = 0;
          size_t kPole = 0, k2m = 0, kLim = 0; double sPole = NAN, e2m = NAN, s2m = NAN, sLim = NAN;
          for (size_t kk = kq + 1; kk < n; ++kk) { const int t2 = TM[kk]; if (t2 >= 1260) break;
            if (!kLim && !kPole && Lw[kk] <= lim) { kLim = kk; sLim = std::min(runLow, Lw[kk]) - 0.25; }
            if (!kPole && Hh[kk] > H) { kPole = kk; sPole = std::min(flo, hbl) - 0.25; }
            runLow = std::min(runLow, Lw[kk]); b2h = std::max(b2h, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
            if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[kk]; if (!std::isnan(p2h) && b2h <= H && !k2m) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(flo, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) { k2m = kk; e2m = c2; s2m = fln - 0.25; } } p2h = b2h; b2h = -1e18; }
            if ((t2 + 1 - 930) % tf == 0) { nb++; flo = std::min(flo, hbl); hbl = 1e18; if (Hh[kk] > H || Cc[kk] < H - 0.5 * M || nb >= 12) break; } }
          const double mm = H + M; int ko;
          auto fill = [&](FTrade& t, size_t k0, double e, double st, double tg, bool be, bool fillMin) { if (!(e - st > 0.25)) return; t.R = sim(k0, e, st, tg, be, fillMin, ko); t.kIn = k0; t.kOut = ko;
            if (!ctxPool.empty()) { double sm = 0; for (int z = 0; z < 20; ++z) { size_t k = ctxPool[(tf >= 3 ? rng() : rng12()) % ctxPool.size()]; int k2; const double ce = Cc[k], rk = e - st; sm += sim(k, ce, ce - rk, ce + (tg - e), be, false, k2); } t.ctl = sm / 20; } };
          // the page's entry (2m trigger, else pole-top) with each exit
          const bool use2m = tf >= 3 && k2m && (!kPole || k2m <= kPole);   // the page has no 2m trigger on 1-2m charts
          const size_t kE = use2m ? k2m : kPole; const double eE = use2m ? e2m : H + 0.25, sE = use2m ? s2m : sPole;
          if (kE) { r.riskCost = (eE - sE) / cost; r.riskPts = eE - sE; r.cost = cost; }   // 5: the stop in units of the round-trip cost (cost share of R = 1 / this)
          // 4 flag slope at entry: least-squares slope of the chart bars' mid-points from the first flag bar to the last bar CLOSED
          //   before the entry minute, in pole lengths per bar (what the page's channel shows; > 0 = the flag drifts WITH the pole)
          if (kE) { std::vector<double> ys; for (int j = f.h + 1; j < (int)b.size() && b[j].k < (int)kE; ++j) ys.push_back((b[j].h + b[j].l) / 2);
            if (ys.size() >= 2) { const double nn = ys.size(), mx = (nn - 1) / 2; double my = 0; for (double y : ys) my += y; my /= nn; double sxy = 0, sxx = 0; for (size_t j = 0; j < ys.size(); ++j) { sxy += (j - mx) * (ys[j] - my); sxx += (j - mx) * (j - mx); } r.slope = sxy / sxx / M; } }
          if (kE) for (int x = 0; x < NEX; ++x) fill(r.ex[x], kE, eE, sE, exitTarget(x, eE, eE - sE, mm), x == EX_MMBE || x == EX_2BE, !use2m);
          r.en[EN_PAGE] = r.ex[EX_MM];
          if (kPole) fill(r.en[EN_POLE], kPole, H + 0.25, sPole, mm, false, true);
          if (k2m) fill(r.en[EN_2M], k2m, e2m, s2m, mm, false, false);
          if (kLim) fill(r.en[EN_LIMIT], kLim, lim, sLim, mm, false, true);
          rows.push_back(r); } } } }
  // one position at a time per day for a stream (as flag_take_opt)
  auto stream = [&](std::function<bool(const Row&)> sel) { std::vector<const Row*> out; std::map<std::pair<int,int>, std::vector<const Row*>> byDayTf;
    for (auto& r : rows) if (sel(r) && !std::isnan(r.ex[EX_MM].R)) byDayTf[{r.date, r.tf}].push_back(&r);
    for (auto& [k, v] : byDayTf) { std::sort(v.begin(), v.end(), [](auto a, auto b) { return a->ex[EX_MM].kIn < b->ex[EX_MM].kIn; }); int lastOut = -1; for (auto* r : v) if (r->ex[EX_MM].kIn > lastOut) { out.push_back(r); lastOut = r->ex[EX_MM].kOut; } }
    return out; };
  const double yrs13 = 0; (void)yrs13;
  auto report = [&](const char* name, const std::vector<const Row*>& v, double years) { double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, w = 0, p2 = 0, n2 = 0, b2 = 0;
    for (auto* r : v) { const double R = r->ex[EX_MM].R; m[r->era] += R; nn[r->era]++; w += R > 0; if (r->era == 2) { p2 += R * r->riskPts; n2++; b2 += R - (1.5 - r->cost) / r->riskPts; } }
    const double N = nn[0] + nn[1] + nn[2];
    printf("  %-46s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f) · win %2.0f%% · %.0f/yr · 2025-26 %+.1f pts/trade, %+.3fR at a fixed 1.5-pt cost\n", name,
           nn[0] ? m[0] / nn[0] : NAN, nn[0], nn[1] ? m[1] / nn[1] : NAN, nn[1], nn[2] ? m[2] / nn[2] : NAN, nn[2], N ? 100 * w / N : 0, N / years, n2 ? p2 / n2 : NAN, n2 ? b2 / n2 : NAN); };
  const double YRS = 2026.8 - 2013.0;   // test years 2013 → 2026-10
  printf("Per-chart rules and flag grades, walk-forward — NQ NY flags (page rules), %ld days. Test years 2013-2026 only (2013-18 | 2019-24 | 2025-26).\n", nDays);
  printf("R after 2 pts × ATR/320, page entry and measured-move exit, one position at a time per chart; per year = over the 13.8 test years.\n");
  static const char* FN[NFEAT] = {"1st pullback", "pole broke a level", "gap with it", "pole >= 5x bar", "before 12:30", "<= 1 smaller 1m flag", "down day", "4/5m chart"};
  for (int side : {1, -1}) {
    const char* S = side > 0 ? "BULL" : "BEAR";
    std::vector<int> pool = side > 0 ? std::vector<int>{PB1, BROKE, GAPW, POLE5, B1230, FEWBOX} : std::vector<int>{PB1, BROKE, GAPW, POLE5, B1230, FEWBOX, DOWNDAY};
    auto ruleOk = [&](const Row& r, int mask) { for (size_t i = 0; i < pool.size(); ++i) if ((mask >> i & 1) && !r.f[pool[i]]) return false; return true; };
    auto maskName = [&](int mask) { std::string sn; for (size_t i = 0; i < pool.size(); ++i) if (mask >> i & 1) { if (!sn.empty()) sn += " + "; sn += FN[pool[i]]; } return sn.empty() ? std::string("all flags") : sn; };
    printf("\n================ %s ================\n== reference (fixed rules, test years)\n", S);
    report("all flags, 1-5m", stream([&](const Row& r) { return r.side == side && r.year >= 2013; }), YRS);
    report("1st pullback, 3-5m", stream([&](const Row& r) { return r.side == side && r.year >= 2013 && r.tf >= 3 && r.f[PB1]; }), YRS);
    if (side > 0) report("TAKE rule (4/5m, fixed)", stream([&](const Row& r) { return r.side > 0 && r.year >= 2013 && r.tf >= 4 && r.f[PB1] && r.f[BROKE] && r.f[GAPW] && r.f[POLE5]; }), YRS);
    // A per-chart walk-forward rule
    printf("== A. per-chart walk-forward rule\n");
    std::vector<const Row*> all;
    for (int tf = 1; tf <= 5; ++tf) {
      std::vector<const Row*> got; std::map<int, int> chosen;
      for (int Y = 2013; Y <= 2026; ++Y) { int best = -1; double bm = 0;
        for (int mask = 0; mask < (1 << pool.size()); ++mask) { double sm = 0, nn = 0; for (auto& r : rows) if (r.side == side && r.tf == tf && r.year < Y && ruleOk(r, mask) && !std::isnan(r.ex[EX_MM].R)) { sm += r.ex[EX_MM].R; nn++; } if (nn >= 40 && sm / nn > bm) { bm = sm / nn; best = mask; } }
        chosen[Y] = best; if (best < 0) continue;
        auto v = stream([&](const Row& r) { return r.side == side && r.tf == tf && r.year == Y && ruleOk(r, best); }); got.insert(got.end(), v.begin(), v.end()); }
      char nm[64]; snprintf(nm, 64, "%dm chart, its own walk-forward rule", tf); report(nm, got, YRS); all.insert(all.end(), got.begin(), got.end());
      printf("      rule from 2021 on: %s\n", chosen[2021] < 0 ? "stand aside" : maskName(chosen[2021]).c_str());
      printf("      rule in 2026:      %s\n", chosen[2026] < 0 ? "stand aside" : maskName(chosen[2026]).c_str());
    }
    report("ALL charts' walk-forward rules together", all, YRS);
    { std::vector<const Row*> v; for (auto* r : all) if (r->tf >= 3) v.push_back(r); report("  3-5m charts' rules together", v, YRS); }
    // B grades (2-5m pooled), walk-forward weights and cut points
    printf("== B. grades A/B/C from the checklist (weights + cut points learned on earlier years), 2-5m\n");
    std::vector<const Row*> gA, gB, gC; std::vector<std::vector<const Row*>> gByTf(6);
    for (int Y = 2013; Y <= 2026; ++Y) {
      std::vector<double> wgt(pool.size(), 0.0);
      for (size_t i = 0; i < pool.size(); ++i) { double s1 = 0, n1 = 0, s0 = 0, n0 = 0; for (auto& r : rows) if (r.side == side && r.tf >= 2 && r.year < Y && !std::isnan(r.ex[EX_MM].R)) { if (r.f[pool[i]]) { s1 += r.ex[EX_MM].R; n1++; } else { s0 += r.ex[EX_MM].R; n0++; } }
        wgt[i] = (n1 >= 30 && n0 >= 30) ? s1 / n1 - s0 / n0 : 0.0; }
      auto score = [&](const Row& r) { double sc = 0; for (size_t i = 0; i < pool.size(); ++i) if (r.f[pool[i]]) sc += wgt[i]; return sc; };
      std::vector<double> tr; for (auto& r : rows) if (r.side == side && r.tf >= 2 && r.year < Y && !std::isnan(r.ex[EX_MM].R)) tr.push_back(score(r));
      if (tr.size() < 90) continue; std::sort(tr.begin(), tr.end()); const double c1 = tr[tr.size() / 3], c2 = tr[2 * tr.size() / 3];
      for (char g : {'A', 'B', 'C'}) { auto v = stream([&](const Row& r) { if (r.side != side || r.tf < 2 || r.year != Y) return false; const double sc = score(r); return g == 'A' ? sc > c2 : g == 'B' ? (sc > c1 && sc <= c2) : sc <= c1; });
        (g == 'A' ? gA : g == 'B' ? gB : gC).insert((g == 'A' ? gA : g == 'B' ? gB : gC).end(), v.begin(), v.end()); }
      if (Y == 2026) { printf("      2026 weights:"); for (size_t i = 0; i < pool.size(); ++i) printf(" %s %+.2f ·", FN[pool[i]], wgt[i]); printf(" cuts %.2f / %.2f\n", c1, c2); }
    }
    report("grade A (top third of scores)", gA, YRS); report("grade B", gB, YRS); report("grade C (bottom third)", gC, YRS);
    { std::vector<const Row*> ab = gA; ab.insert(ab.end(), gB.begin(), gB.end()); report("grades A + B", ab, YRS); }
    for (int tf = 2; tf <= 5; ++tf) { std::vector<const Row*> v; for (auto* r : gA) if (r->tf == tf) v.push_back(r); char nm[64]; snprintf(nm, 64, "  grade A on %dm", tf); report(nm, v, YRS); }
  }
  return 0;
}
