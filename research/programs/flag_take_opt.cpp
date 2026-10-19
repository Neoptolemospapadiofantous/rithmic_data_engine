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
  // one position at a time per day for a stream (the trades of `pick` in order of entry; a trade entering before the last exit is skipped)
  bool allTf = false;   // sections 1-5: 3-5m only, as before the 1-2m rows were added
  auto stream = [&](std::function<bool(const Row&)> sel, std::function<const FTrade&(const Row&)> get) { std::vector<std::pair<const Row*, const FTrade*>> out; std::map<int, std::vector<std::pair<const Row*, const FTrade*>>> byDay;
    for (auto& r : rows) if ((allTf || r.tf >= 3) && sel(r)) { const FTrade& t = get(r); if (!std::isnan(t.R)) byDay[r.date].push_back({&r, &t}); }
    for (auto& [dd, v] : byDay) { std::sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.second->kIn < b.second->kIn; }); int lastOut = -1; for (auto& p : v) if (p.second->kIn > lastOut) { out.push_back(p); lastOut = p.second->kOut; } }
    return out; };
  const double yrs = nDays / 252.0;
  auto report = [&](const char* name, const std::vector<std::pair<const Row*, const FTrade*>>& v) { double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, cs = 0, cn = 0, rs = 0, w = 0;
    for (auto& [r, t] : v) { m[r->era] += t->R; nn[r->era]++; w += t->R > 0; if (!std::isnan(t->ctl)) { cs += t->ctl; rs += t->R; cn++; } } const double N = nn[0] + nn[1] + nn[2];
    printf("  %-44s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f) · win %2.0f%% · vs control %+.3f [%.0f/yr]\n", name, nn[0] ? m[0] / nn[0] : NAN, nn[0], nn[1] ? m[1] / nn[1] : NAN, nn[1], nn[2] ? m[2] / nn[2] : NAN, nn[2], N ? 100 * w / N : 0, cn ? (rs - cs) / cn : NAN, N / yrs); };
  auto page = [](const Row& r) -> const FTrade& { return r.ex[EX_MM]; };
  { size_t n35 = 0; for (auto& r : rows) n35 += r.tf >= 3;
  printf("TAKE-rule optimization — NQ NY 3/4/5m flags with the page's rules, 2010-07 → 2026-10, %ld days, %zu flags (+ %zu on 1-2m, section 6 only). R after 2 pts cost, flat 15:55,\n", nDays, n35, rows.size() - n35); }
  printf("one position at a time per day. Cells: avg R (trades) 2010-18 | 2019-24 | 2025-26 · win rate · flag R minus its random-timing control · trades a year.\n");
  static const char* FN[NFEAT] = {"1st pullback", "pole broke a level", "gap with it", "pole >= 5x bar", "before 12:30", "<= 1 smaller 1m flag", "down day", "4/5m chart"};
  for (int side : {1, -1}) {
    const char* S = side > 0 ? "BULL" : "BEAR"; std::vector<int> pool = side > 0 ? std::vector<int>{PB1, BROKE, GAPW, POLE5, B1230, FEWBOX, TF45} : std::vector<int>{PB1, BROKE, GAPW, POLE5, B1230, FEWBOX, DOWNDAY};
    auto ruleOk = [&](const Row& r, int mask) { for (size_t i = 0; i < pool.size(); ++i) if ((mask >> i & 1) && !r.f[pool[i]]) return false; return true; };
    auto maskName = [&](int mask) { std::string sname; for (size_t i = 0; i < pool.size(); ++i) if (mask >> i & 1) { if (!sname.empty()) sname += " + "; sname += FN[pool[i]]; } return sname.empty() ? std::string("all flags") : sname; };
    printf("\n== 1. %s — fixed rules (for reference; chosen with hindsight) and the WALK-FORWARD rule\n", S);
    report("all flags", stream([&](const Row& r) { return r.side == side; }, page));
    report("1st pullback", stream([&](const Row& r) { return r.side == side && r.f[PB1]; }, page));
    if (side > 0) report("1st pullback + before 12:30 (TAKE draft)", stream([&](const Row& r) { return r.side == side && r.f[PB1] && r.f[B1230]; }, page));
    else report("1st pb + down day + <= 1 box (TAKE draft)", stream([&](const Row& r) { return r.side == side && r.f[PB1] && r.f[DOWNDAY] && r.f[FEWBOX]; }, page));
    // walk-forward: per test year, the mask with the best mean R over earlier years (per-flag R, page entry/exit; >= 60 trades)
    std::map<int, int> chosen; std::vector<std::pair<const Row*, const FTrade*>> wf;
    for (int Y = 2013; Y <= 2026; ++Y) { int best = -1; double bm = 0; for (int mask = 0; mask < (1 << pool.size()); ++mask) { double sm = 0, nn = 0; for (auto& r : rows) if (r.side == side && r.tf >= 3 && r.year < Y && ruleOk(r, mask) && !std::isnan(r.ex[EX_MM].R)) { sm += r.ex[EX_MM].R; nn++; } if (nn >= 60 && sm / nn > bm) { bm = sm / nn; best = mask; } }
      chosen[Y] = best; if (best < 0) continue; auto v = stream([&](const Row& r) { return r.side == side && r.year == Y && ruleOk(r, best); }, page); wf.insert(wf.end(), v.begin(), v.end()); }
    report("WALK-FORWARD rule (2013 on)", wf);
    { auto pb = stream([&](const Row& r) { return r.side == side && r.year >= 2013 && r.f[PB1]; }, page); report("  1st pullback, same years (2013 on)", pb); }
    printf("  rules chosen:"); int prev = -2; for (auto& [Y, m] : chosen) { if (m != prev) printf("\n    %d+: %s", Y, m < 0 ? "stand aside" : maskName(m).c_str()); prev = m; } printf("\n");
    printf("\n== 2. %s exits — 1st-pullback flags, the page's entry\n", S);
    for (int x = 0; x < NEX; ++x) report(EXN[x], stream([&](const Row& r) { return r.side == side && r.f[PB1]; }, [x](const Row& r) -> const FTrade& { return r.ex[x]; }));
    printf("\n== 3. %s entries — 1st-pullback flags, measured-move target\n", S);
    for (int e = 0; e < NEN; ++e) report(ENN[e], stream([&](const Row& r) { return r.side == side && r.f[PB1]; }, [e](const Row& r) -> const FTrade& { return r.en[e]; }));
  }
  // 4 flag slope (the founder: "bull flag lines should be opposite") — 1st-pullback flags, the page's entry and exit
  printf("\n== 4. Flag slope at entry (mid-point slope, pole lengths per chart bar; down = against the pole, the textbook flag) — 1st-pullback flags\n");
  for (int side : {1, -1}) for (int g = 0; g < 4; ++g) { static const char* GN[4] = {"down  (< -0.03 pole/bar)", "flat  (-0.03 .. +0.03)", "up    (> +0.03, drifts with the pole)", "up, TAKE rule only"};
    char nm[96]; snprintf(nm, 96, "%s %s", side > 0 ? "bull" : "bear", GN[g]);
    report(nm, stream([&, g, side](const Row& r) { if (r.side != side || !r.f[PB1] || std::isnan(r.slope)) return false;
      if (g == 0) return r.slope < -0.03; if (g == 1) return r.slope >= -0.03 && r.slope <= 0.03; if (g == 2) return r.slope > 0.03;
      return side > 0 && r.slope > 0.03 && r.f[BROKE] && r.f[GAPW] && r.f[POLE5] && r.f[TF45]; }, page)); }
  for (int side : {1}) for (int g = 0; g < 2; ++g) { char nm[96]; snprintf(nm, 96, "bull TAKE rule, slope %s", g ? "> +0.03" : "<= +0.03");
    report(nm, stream([&, g, side](const Row& r) { return r.side == side && r.f[PB1] && r.f[BROKE] && r.f[GAPW] && r.f[POLE5] && r.f[TF45] && !std::isnan(r.slope) && ((r.slope > 0.03) == (bool)g); }, page)); }
  // 5 stop size vs cost (the founder: "anything that mathematically gives us an advantage"): R net = R gross − cost / risk, so a
  //   small stop pays a large share of R in costs. Buckets of risk / cost at entry — 1st-pullback flags and the TAKE rule
  printf("\n== 5. Stop size in units of the round-trip cost (cost share of 1R = 1 / ratio) — the page's entry and exit\n");
  static const double CUT[5] = {0, 6, 10, 15, 1e9}; static const char* CN[4] = {"risk < 6x cost (cost > 17 % of R)", "risk 6-10x cost", "risk 10-15x cost", "risk >= 15x cost (cost < 7 % of R)"};
  for (int side : {1, -1}) for (int g = 0; g < 4; ++g) { char nm[96]; snprintf(nm, 96, "%s 1st pb, %s", side > 0 ? "bull" : "bear", CN[g]);
    report(nm, stream([&, g, side](const Row& r) { return r.side == side && r.f[PB1] && !std::isnan(r.riskCost) && r.riskCost >= CUT[g] && r.riskCost < CUT[g + 1]; }, page)); }
  for (int g = 0; g < 4; ++g) { char nm[96]; snprintf(nm, 96, "bull TAKE rule, %s", CN[g]);
    report(nm, stream([&, g](const Row& r) { return r.side > 0 && r.f[PB1] && r.f[BROKE] && r.f[GAPW] && r.f[POLE5] && r.f[TF45] && !std::isnan(r.riskCost) && r.riskCost >= CUT[g] && r.riskCost < CUT[g + 1]; }, page)); }
  // 6 EDGE BY CHART (founder 2026-10-20: "how much is the edge on each chart?") — the page's entry and exit; 1-2m enter on the pole-top
  //   break (the page shows no 2m trigger there). R, points (R × risk, after cost) and dollars per trade at 1 MNQ ($2 / point), each chart
  //   scored on its own, one position at a time per chart and day.
  allTf = true;
  printf("\n== 6. Edge by chart — avg R (trades) 2010-18 | 2019-24 | 2025-26 · win · vs its random-timing twin · per year; then avg points and $ at 1 MNQ per trade (all years | 2025-26)\n");
  auto chartLine = [&](const char* name, std::function<bool(const Row&)> sel) {
    // one position at a time PER CHART: run the stream per tf and merge
    std::vector<std::pair<const Row*, const FTrade*>> v; for (int tf = 1; tf <= 5; ++tf) { auto w = stream([&, tf](const Row& r) { return r.tf == tf && sel(r); }, page); v.insert(v.end(), w.begin(), w.end()); }
    report(name, v);
    double pts = 0, n = 0, pts2 = 0, n2 = 0; for (auto& [r, t] : v) { const double p = t->R * r->riskPts; pts += p; n++; if (r->era == 2) { pts2 += p; n2++; } }
    printf("  %-44s   points/trade %+.2f (all) | %+.2f (2025-26)  ≈ $%+.0f | $%+.0f per trade at 1 MNQ\n", "", n ? pts / n : NAN, n2 ? pts2 / n2 : NAN, n ? 2 * pts / n : NAN, n2 ? 2 * pts2 / n2 : NAN); };
  for (int side : {1, -1}) for (int tf = 1; tf <= 5; ++tf) {
    char a[96], c[96]; snprintf(a, 96, "%s %dm, all flags", side > 0 ? "BULL" : "BEAR", tf); snprintf(c, 96, "%s %dm, 1st pullback", side > 0 ? "BULL" : "BEAR", tf);
    chartLine(a, [=](const Row& r) { return r.side == side && r.tf == tf; });
    chartLine(c, [=](const Row& r) { return r.side == side && r.tf == tf && r.f[PB1]; });
    if (side > 0 && tf >= 4) { char t[96]; snprintf(t, 96, "BULL %dm, TAKE rule", tf); chartLine(t, [=](const Row& r) { return r.side > 0 && r.tf == tf && r.f[PB1] && r.f[BROKE] && r.f[GAPW] && r.f[POLE5]; }); }
    if (side < 0) { char t[96]; snprintf(t, 96, "BEAR %dm, 1st pb + stop >= 15x cost", tf); chartLine(t, [=](const Row& r) { return r.side < 0 && r.tf == tf && r.f[PB1] && !std::isnan(r.riskCost) && r.riskCost >= 15; }); } }
  return 0;
}
