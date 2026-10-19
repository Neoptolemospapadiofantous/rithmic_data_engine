// 2026-10-19 (founder: "what else can we do to find bear and bull flags, the good ones" → "test all"): the second flag review.
// Same structural flags as flag_review.cpp (NY 3/4/5m, bull + bear on the mirrored session, pole ≥ 3× the session's average bar,
// pullback 15-50 %, 2 flag bars, registered 09:45-14:00, one at a time per chart and side), now with:
//  2 LEVELS      room from the pole top to the next STRUCTURAL level in the trade's direction (prior-day high/low, overnight high/low, opening
//                range 09:30-10:00 once it is complete, round 100s — VWAP bands left out, they put a level above nearly every flag), in pole lengths; and whether the pole
//                broke a level on its way
//  3 PULLBACK #  how many flags of this side and chart came before it since the session's opposite extreme (the low of day for a
//                bull flag) — 1 = the first pullback of the leg; and whether the pole top is a new high of day
//  4 ORDER FLOW  aggressor delta (buy − sell volume) share in the pole and in the flag, trend side + (flow days: 2023-03+)
//  5 ES          ES on the same side of its own 09:30 open and VWAP with a straight path (ES confirms), and NQ's move since the open
//                minus ES's, each in its own ATR (NQ relative strength)
//  6 HIGHER TF   the last 60 minutes' move in ATR (trend side +), and the last close vs the prior session's close
//  7 FAILED FLAG when a flag fails (a chart close below 50 % of the pole), trade the other way at that close: stop 1 tick beyond the
//                pole top, target the pole start
//  8 BEAR REGIME bear flags on down days only (gap down, prior close below SMA50, VIX up the day before)
//  9 NEWS        FOMC and NFP days apart
// Then a STACKED rule per side: starting from all flags, add one condition at a time — the one that most raises the 2010-18 mean R
// (with ≥ 150 trades left in 2010-18) — and report each step's 2019-24 and 2025-26 result, which the selection never saw.
// Entry: the 2m trigger (first 2m close above the previous 2m high after a lower high inside the flag, stop = flag low), else the
// pole-top stop order; target pole top + pole length; flat 15:55; 2 pts cost scaled to the day. Read-only on data/edge_lab/cache.
#include "research/meta.hpp"
#include <cstdio>
#include <unordered_map>
#include <fstream>
using namespace lab;
struct FB { double o, h, l, c, v, bv; int k; };
enum F { POLEX, POLEATR, MINUTE, CTXOK, GAP, ROOM, BROKE, PBNO, HOD, PDELTA, FDELTA, ESOK, RELSTR, MOM60, VSPC, FOMC, NFP, TREND50, VIXUP, NF };
struct Row { int date, era, tf, side; bool flow, esHave; double f[NF]; double R, Rfail; };
static double entryR(const Row& r) { return r.R; }
int main() {
  std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s, es; std::string err;
  { std::ifstream ff("data/edge_lab/cache/fomc.txt"); std::string line; while (std::getline(ff, line)) { int y, m, dd; if (std::sscanf(line.c_str(), "%d-%d-%d", &y, &m, &dd) == 3) fomc.insert(y * 10000 + m * 100 + dd); } }
  if (!load_raw("data/edge_lab/cache", "NQ", s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
  build_days(s, fomc, vix);
  const bool haveES = load_raw("data/edge_lab/cache", "ES", es, err); if (haveES) build_days(es, fomc, vix);
  std::unordered_map<int64_t, int> esIx; std::map<int, const Day*> esDay;
  if (haveES) { for (size_t i = 0; i < es.t.size(); ++i) esIx[es.t[i]] = i; for (auto& d : es.days) esDay[d.date] = &d; }
  std::vector<Row> rows;
  for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date > 20261016) continue; std::vector<int> I; for (int i = d.b0; i < d.b1; ++i) { int tm = tday_min(s.t[i]); if (tm >= 930 && tm < 1315) I.push_back(i); } if (I.size() < 300) continue;
    const double A = d.atr, sc = A / 320.0, cost = 2 * sc; const size_t n = I.size(); const int era = d.date < 20190101 ? 0 : d.date < 20250101 ? 1 : 2;
    double onh = -1e18, onl = 1e18; for (int i = d.b0; i < d.b1; ++i) { if (tday_min(s.t[i]) >= 930) break; onh = std::max(onh, (double)s.h[i]); onl = std::min(onl, (double)s.l[i]); }
    double orh = -1e18, orl = 1e18; for (size_t k = 0; k < n && tday_min(s.t[I[k]]) < 960; ++k) { orh = std::max(orh, (double)s.h[I[k]]); orl = std::min(orl, (double)s.l[I[k]]); }
    // ES session for this date
    const Day* ed = haveES && esDay.count(d.date) ? esDay[d.date] : nullptr; std::vector<int> EI(n, -1); double eso = NAN;
    if (ed && ed->atr > 0) for (size_t k = 0; k < n; ++k) { auto it = esIx.find(s.t[I[k]]); if (it != esIx.end()) { EI[k] = it->second; if (std::isnan(eso)) eso = es.o[it->second]; } }
    // raw session VWAP and its σ, ES VWAP / path, per minute
    std::vector<double> vwR(n), sdR(n), eVw(n, NAN), eHi(n, NAN), eLo(n, NAN);
    { double pv = 0, vv = 0, p2 = 0; for (size_t k = 0; k < n; ++k) { int i = I[k]; double tp = (s.h[i] + s.l[i] + s.c[i]) / 3.0; pv += tp * s.v[i]; vv += s.v[i]; p2 += tp * tp * s.v[i]; vwR[k] = vv > 0 ? pv / vv : s.c[i]; sdR[k] = vv > 0 ? std::sqrt(std::max(0.0, p2 / vv - vwR[k] * vwR[k])) : 0; } }
    if (ed) { double pv = 0, vv = 0, hi = -1e18, lo = 1e18; for (size_t k = 0; k < n; ++k) { if (EI[k] < 0) { if (k) { eVw[k] = eVw[k - 1]; eHi[k] = eHi[k - 1]; eLo[k] = eLo[k - 1]; } continue; } int j = EI[k]; pv += (es.h[j] + es.l[j] + es.c[j]) / 3.0 * es.v[j]; vv += es.v[j]; eVw[k] = vv > 0 ? pv / vv : es.c[j]; hi = std::max(hi, (double)es.h[j]); lo = std::min(lo, (double)es.l[j]); eHi[k] = hi; eLo[k] = lo; } }
    for (int side = 1; side >= -1; side -= 2) {
      std::vector<double> O(n), Hh(n), Lw(n), Cc(n), V(n), BV(n), vw(n), hiS(n), loS(n); std::vector<int> TM(n), lodK(n);
      for (size_t k = 0; k < n; ++k) { int i = I[k]; TM[k] = tday_min(s.t[i]); V[k] = s.v[i]; BV[k] = side > 0 ? s.bv[i] : s.v[i] - s.bv[i];
        if (side > 0) { O[k] = s.o[i]; Hh[k] = s.h[i]; Lw[k] = s.l[i]; Cc[k] = s.c[i]; } else { O[k] = -s.o[i]; Hh[k] = -s.l[i]; Lw[k] = -s.h[i]; Cc[k] = -s.c[i]; } }
      { double hi = -1e18, lo = 1e18; int lk = 0; for (size_t k = 0; k < n; ++k) { vw[k] = side * vwR[k]; hi = std::max(hi, Hh[k]); if (Lw[k] < lo) { lo = Lw[k]; lk = k; } hiS[k] = hi; loS[k] = lo; lodK[k] = lk; } }
      const double so = O[0], pc = side * d.pc;
      // levels in mirrored units
      std::vector<double> lv = {side > 0 ? d.ph : -d.pl, side > 0 ? onh : -onl, side > 0 ? d.pl : -d.ph, side > 0 ? onl : -onh};
      auto exitR = [&](size_t k0, double e, double st, double tg, bool fillMin) -> double { double px = NAN; if (fillMin && Lw[k0] <= st) px = st;
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Lw[k] <= st) { px = st; break; } if (Hh[k] >= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return (px - e - cost) / (e - st); };
      // short (against the mirrored direction) for the failed-flag reversal
      auto exitShort = [&](size_t k0, double e, double st, double tg) -> double { double px = NAN;
        for (size_t k = k0 + 1; k < n && std::isnan(px); ++k) { if (Hh[k] >= st) { px = st; break; } if (Lw[k] <= tg) { px = tg; break; } if (TM[k] >= 1315) { px = O[k]; break; } }
        if (std::isnan(px)) px = Cc[n - 1]; return (e - px - cost) / (st - e); };
      for (int tf = 3; tf <= 5; ++tf) { std::vector<FB> b; int busy = -1; std::vector<int> regK;
        for (size_t k = 0; k < n; ++k) { const int tm = TM[k];
          if ((tm - 930) % tf == 0 || b.empty()) b.push_back({O[k], Hh[k], Lw[k], Cc[k], V[k], BV[k], (int)k}); else { FB& z = b.back(); z.h = std::max(z.h, Hh[k]); z.l = std::min(z.l, Lw[k]); z.c = Cc[k]; z.v += V[k]; z.bv += BV[k]; z.k = k; }
          if ((tm + 1 - 930) % tf) continue; const int q = b.size() - 1; if (q < 6 || q <= busy || tm < 944 || tm >= 1260) continue;
          const int h = q - 2; const double H = b[h].h; if (b[h + 1].h > H || b[q].h > H) continue;
          int l = -1; double L = 1e18; for (int r = std::max(0, h - 15); r <= h - 2; ++r) if (b[r].l < L) { L = b[r].l; l = r; } if (l < 0) continue;
          bool ok = true; for (int r = l; r <= h; ++r) if (b[r].l < L) ok = false; if (!ok) continue;
          double ar = 0; for (int r = 0; r <= q; ++r) ar += b[r].h - b[r].l; ar /= q + 1; const double M = H - L; if (M < 3 * ar) continue;
          const double fl0 = std::min(b[h + 1].l, b[q].l), pull = (H - fl0) / M; if (pull < 0.15 || pull > 0.5) continue;
          const size_t kq = b[q].k; const double cl = b[q].c;
          Row r{}; r.date = d.date; r.era = era; r.tf = tf; r.side = side; r.flow = d.flow; r.R = NAN; r.Rfail = NAN;
          r.f[POLEX] = M / ar; r.f[POLEATR] = M / A; r.f[MINUTE] = tm + 1 - 930; r.f[CTXOK] = cl > so && cl > vw[kq] && hiS[kq] > loS[kq] && (cl - so) / (hiS[kq] - loS[kq]) >= 0.55;
          r.f[GAP] = std::isnan(pc) ? 0 : (so - pc) / A;
          // 2 levels: next level above the pole top (mirrored), once known at the flag
          std::vector<double> L2 = lv; if (tm + 1 >= 960) { L2.push_back(side > 0 ? orh : -orl); L2.push_back(side > 0 ? orl : -orh); }
          { double raw = side > 0 ? H : -H; double up = side > 0 ? std::ceil((raw + 0.01) / 100) * 100 : std::floor((raw - 0.01) / 100) * 100; L2.push_back(side * up); }
          double nxt = 1e18; bool broke = false; for (double x : L2) { if (std::isnan(x)) continue; if (x > H + 0.25 && x < nxt) nxt = x; if (x > L && x <= H) broke = true; }
          r.f[ROOM] = nxt < 1e17 ? (nxt - H) / M : 9; r.f[BROKE] = broke;
          // 3 pullback number in the leg since the low of day, and pole top = new high of day
          { int lodAt = lodK[kq]; int cnt = 0; for (int rk : regK) if (rk > lodAt) cnt++; r.f[PBNO] = cnt + 1; double hiBefore = -1e18; for (size_t kk = 0; kk < (size_t)b[l].k; ++kk) hiBefore = std::max(hiBefore, Hh[kk]); r.f[HOD] = H >= hiBefore; }
          // 4 order flow
          { double pv = 0, pbv = 0, fv = 0, fbv = 0; for (int x = l; x <= h; ++x) { pv += b[x].v; pbv += b[x].bv; } for (int x = h + 1; x <= q; ++x) { fv += b[x].v; fbv += b[x].bv; } r.f[PDELTA] = pv > 0 ? (2 * pbv - pv) / pv : 0; r.f[FDELTA] = fv > 0 ? (2 * fbv - fv) / fv : 0; }
          // 5 ES
          r.esHave = ed && EI[kq] >= 0 && !std::isnan(eso) && ed->atr > 0;
          if (r.esHave) { const double ec = es.c[EI[kq]], ev = eVw[kq], emove = side * (ec - eso); r.f[ESOK] = emove > 0 && side * (ec - ev) > 0 && eHi[kq] > eLo[kq] && emove / (eHi[kq] - eLo[kq]) >= 0.55; r.f[RELSTR] = (cl - so) / A - emove / ed->atr; }
          // 6 higher timeframe
          r.f[MOM60] = kq >= 60 ? (Cc[kq] - Cc[kq - 60]) / A : (Cc[kq] - so) / A; r.f[VSPC] = std::isnan(pc) ? 0 : (cl - pc) / A;
          // 8 / 9
          r.f[FOMC] = d.fomc; r.f[NFP] = d.nfp; r.f[TREND50] = (std::isnan(pc) || std::isnan(d.sma50)) ? 0 : (pc - side * d.sma50) / A; r.f[VIXUP] = std::isnan(d.vixc1) ? 0 : d.vixc1 * (side > 0 ? -1 : 1);
          // follow the flag: pole-top stop, 2m trigger, failure
          double fl = fl0, hbh = -1e18, hbl = 1e18, p2h = NAN, b2h = -1e18; bool lowerHigh = false, doneF = false; int nb = 0; double Rp = NAN;
          for (size_t kk = kq + 1; kk < n && !doneF; ++kk) { const int t2 = TM[kk]; if (t2 >= 1260) break;
            if (std::isnan(Rp) && Hh[kk] > H) Rp = exitR(kk, H + 0.25, std::min(fl, hbl) - 0.25, H + M, true);
            b2h = std::max(b2h, Hh[kk]); hbh = std::max(hbh, Hh[kk]); hbl = std::min(hbl, Lw[kk]);
            if ((t2 + 1 - 930) % 2 == 0) { const double c2 = Cc[kk]; if (!std::isnan(p2h) && b2h <= H && std::isnan(r.R)) { if (b2h < p2h) lowerHigh = true; const double fln = std::min(fl, hbl); if (lowerHigh && c2 > p2h && c2 - fln > 0.5) r.R = exitR(kk, c2, fln - 0.25, H + M, false); } p2h = b2h; b2h = -1e18; }
            if ((t2 + 1 - 930) % tf == 0) { nb++; fl = std::min(fl, hbl);
              if (Cc[kk] < H - 0.5 * M && hbh <= H) { r.Rfail = exitShort(kk, Cc[kk], H + 0.25, L); doneF = true; }
              else if (hbh > H || nb >= 12) doneF = true; hbh = -1e18; hbl = 1e18; } }
          if (std::isnan(r.R)) r.R = Rp;
          busy = q + nb; regK.push_back(kq); rows.push_back(r); } } } }
  printf("Flag review 2 — NY 3/4/5m structural flags, bull + bear, NQ 2010-07 → 2026-10: %zu flags (ES %s). R after 2 pts cost (2m trigger, else pole-top stop).\n", rows.size(), haveES ? "loaded" : "MISSING");
  auto per = [&](const std::vector<const Row*>& a, double out[3], double nn[3]) { for (int e = 0; e < 3; ++e) { out[e] = 0; nn[e] = 0; } for (auto* r : a) { double R = entryR(*r); if (std::isnan(R)) continue; out[r->era] += R; nn[r->era]++; } for (int e = 0; e < 3; ++e) out[e] = nn[e] ? out[e] / nn[e] : NAN; };
  auto base = [](const Row& r) { return r.f[CTXOK] > 0 && r.f[POLEX] >= 4; };
  struct C { const char* name; std::function<bool(const Row&)> f; };
  std::vector<C> conds = {
    {"room to next level >= 1 pole (target clear)", [](const Row& r) { return r.f[ROOM] >= 1; }}, {"room 0.5-1 pole", [](const Row& r) { return r.f[ROOM] >= 0.5 && r.f[ROOM] < 1; }}, {"room < 0.5 pole (level right above)", [](const Row& r) { return r.f[ROOM] < 0.5; }},
    {"pole broke a level", [](const Row& r) { return r.f[BROKE] > 0; }}, {"pole broke no level", [](const Row& r) { return r.f[BROKE] == 0; }},
    {"1st pullback of the leg", [](const Row& r) { return r.f[PBNO] == 1; }}, {"2nd pullback", [](const Row& r) { return r.f[PBNO] == 2; }}, {"3rd+ pullback", [](const Row& r) { return r.f[PBNO] >= 3; }},
    {"pole top = new high of day", [](const Row& r) { return r.f[HOD] > 0; }}, {"pole top below the high of day", [](const Row& r) { return r.f[HOD] == 0; }},
    {"ES confirms (same side, straight)", [](const Row& r) { return r.esHave && r.f[ESOK] > 0; }}, {"ES does not confirm", [](const Row& r) { return r.esHave && r.f[ESOK] == 0; }},
    {"NQ stronger than ES (rel > +0.1 ATR)", [](const Row& r) { return r.esHave && r.f[RELSTR] > 0.1; }}, {"NQ weaker than ES (rel < -0.1)", [](const Row& r) { return r.esHave && r.f[RELSTR] < -0.1; }},
    {"60-min move >= 0.3 ATR with the flag", [](const Row& r) { return r.f[MOM60] >= 0.3; }}, {"60-min move < 0.3 ATR", [](const Row& r) { return r.f[MOM60] < 0.3; }},
    {"beyond the prior close by >= 0.5 ATR", [](const Row& r) { return r.f[VSPC] >= 0.5; }}, {"within 0.5 ATR of the prior close", [](const Row& r) { return r.f[VSPC] < 0.5; }},
    {"FOMC day", [](const Row& r) { return r.f[FOMC] > 0; }}, {"NFP day", [](const Row& r) { return r.f[NFP] > 0; }}, {"no FOMC / NFP", [](const Row& r) { return r.f[FOMC] == 0 && r.f[NFP] == 0; }},
    {"gap with the flag > 0.1 ATR", [](const Row& r) { return r.f[GAP] > 0.1; }}, {"prior close beyond SMA50 with the flag", [](const Row& r) { return r.f[TREND50] > 0; }},
    {"VIX moved with the flag yesterday", [](const Row& r) { return r.f[VIXUP] > 0; }},
    {"down-day regime (gap + SMA50 + VIX) [bears]", [](const Row& r) { return r.f[GAP] > 0.1 && r.f[TREND50] > 0 && r.f[VIXUP] > 0; }} };
  for (int side : {1, -1}) { printf("\n== %s flags with the context on and pole >= 4x bar — avg R (n) 2010-18 | 2019-24 | 2025-26\n", side > 0 ? "BULL" : "BEAR");
    { std::vector<const Row*> a; for (auto& r : rows) if (r.side == side && base(r)) a.push_back(&r); double m[3], nn[3]; per(a, m, nn); printf("  %-46s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)\n", "ALL (reference)", m[0], nn[0], m[1], nn[1], m[2], nn[2]); }
    for (auto& c : conds) { std::vector<const Row*> a; for (auto& r : rows) if (r.side == side && base(r) && c.f(r)) a.push_back(&r); double m[3], nn[3]; per(a, m, nn); printf("  %-46s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)\n", c.name, m[0], nn[0], m[1], nn[1], m[2], nn[2]); }
    // order flow terciles (flow days)
    for (int fi : {PDELTA, FDELTA}) { std::vector<const Row*> a; for (auto& r : rows) if (r.side == side && base(r) && r.flow && !std::isnan(r.R)) a.push_back(&r); if (a.size() < 90) continue; std::vector<double> v; for (auto* r : a) v.push_back(r->f[fi]); std::sort(v.begin(), v.end()); double c1 = v[v.size() / 3], c2 = v[2 * v.size() / 3];
      printf("  %s delta share terciles (flow days, cuts %+.3f %+.3f):", fi == PDELTA ? "pole" : "flag", c1, c2); for (int g = 0; g < 3; ++g) { double sm = 0, nn = 0; for (auto* r : a) { int gg = r->f[fi] < c1 ? 0 : r->f[fi] < c2 ? 1 : 2; if (gg == g) { sm += r->R; nn++; } } printf("  %s %+.3f (%.0f)", g == 0 ? "low" : g == 1 ? "mid" : "high", nn ? sm / nn : 0, nn); } printf("\n"); }
  }
  // 7 failed flags traded the other way
  printf("\n== 7. Failed flags traded the other way (close below 50 %% of the pole → enter against the flag; stop beyond the pole top; target the pole start) — avg R (n) per period\n");
  for (int side : {1, -1}) for (int ctx = 0; ctx < 2; ++ctx) { std::vector<const Row*> a; for (auto& r : rows) if (r.side == side && !std::isnan(r.Rfail) && (!ctx || base(r))) a.push_back(&r); double m[3] = {0, 0, 0}, nn[3] = {0, 0, 0}, w = 0; for (auto* r : a) { m[r->era] += r->Rfail; nn[r->era]++; w += r->Rfail > 0; }
    printf("  failed %s flag → %s %-22s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f) | win %2.0f%%\n", side > 0 ? "bull" : "bear", side > 0 ? "SHORT" : "LONG ", ctx ? "(context was on)" : "(all)", nn[0] ? m[0] / nn[0] : 0, nn[0], nn[1] ? m[1] / nn[1] : 0, nn[1], nn[2] ? m[2] / nn[2] : 0, nn[2], a.size() ? 100 * w / a.size() : 0); }
  // stacked rule, chosen on 2010-18 only
  printf("\n== Stacked rule per side — each step adds the condition that most raises the 2010-18 mean (>= 150 trades left in 2010-18). Later periods are the test.\n");
  std::vector<C> pool = conds; pool.push_back({"pole >= 5x bar", [](const Row& r) { return r.f[POLEX] >= 5; }}); pool.push_back({"pole >= 0.4 ATR", [](const Row& r) { return r.f[POLEATR] >= 0.4; }}); pool.push_back({"before 12:30", [](const Row& r) { return r.f[MINUTE] < 180; }}); pool.push_back({"before 11:00", [](const Row& r) { return r.f[MINUTE] < 90; }});
  // news days are left out of the stacking pool — too few days to select on
  pool.erase(std::remove_if(pool.begin(), pool.end(), [](const C& c) { return std::string(c.name).find("FOMC") != std::string::npos || std::string(c.name).find("NFP") != std::string::npos; }), pool.end());
  for (int side : {1, -1}) { printf("  -- %s\n", side > 0 ? "bull" : "bear"); std::vector<const Row*> cur; for (auto& r : rows) if (r.side == side && base(r)) cur.push_back(&r); std::vector<int> used;
    { double m[3], nn[3]; per(cur, m, nn); printf("    start: context + pole >= 4x                    %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)\n", m[0], nn[0], m[1], nn[1], m[2], nn[2]); }
    for (int step = 0; step < 5; ++step) { int best = -1; double bm = -1e9; for (size_t ci = 0; ci < pool.size(); ++ci) { if (std::find(used.begin(), used.end(), (int)ci) != used.end()) continue; std::vector<const Row*> a; for (auto* r : cur) if (pool[ci].f(*r)) a.push_back(r); double m[3], nn[3]; per(a, m, nn); if (nn[0] >= 150 && m[0] > bm) { bm = m[0]; best = ci; } }
      if (best < 0) break; double m0[3], n0[3]; per(cur, m0, n0); if (bm <= m0[0] + 0.01) break; used.push_back(best); std::vector<const Row*> a; for (auto* r : cur) if (pool[best].f(*r)) a.push_back(r); cur = a;
      double m[3], nn[3]; per(cur, m, nn); printf("    + %-44s %+.3f (%4.0f) | %+.3f (%4.0f) | %+.3f (%3.0f)\n", pool[best].name, m[0], nn[0], m[1], nn[1], m[2], nn[2]); } }
  // how often: the first-pullback filter as a daily rule (either side), and with the other filters that held
  printf("\n== First-pullback flags as a daily rule (context on, pole >= 4x bar, 3-5m, both sides). Flags a day (all charts) | days with one | avg R per period\n");
  { std::set<int> allDays; for (auto& r : rows) allDays.insert(r.date); double perEra[3] = {0, 0, 0}; for (int d0 : allDays) perEra[d0 < 20190101 ? 0 : d0 < 20250101 ? 1 : 2]++; const double ND = perEra[0] + perEra[1] + perEra[2];
    std::vector<C> rl = { {"1st pullback, both sides", [](const Row& r) { return r.f[PBNO] == 1; }}, {"1st or 2nd pullback, both sides", [](const Row& r) { return r.f[PBNO] <= 2; }},
      {"1st pullback, pole broke a level, no FOMC", [](const Row& r) { return r.f[PBNO] == 1 && r.f[BROKE] > 0 && r.f[FOMC] == 0; }},
      {"1st pullback, bull only", [](const Row& r) { return r.f[PBNO] == 1 && r.side > 0; }} };
    for (auto& c : rl) { std::vector<const Row*> a; std::set<int> dd; for (auto& r : rows) if (base(r) && c.f(r) && !std::isnan(r.R)) { a.push_back(&r); dd.insert(r.date); } double m[3], nn[3]; per(a, m, nn);
      // first one of the day only
      std::map<int, const Row*> first; for (auto* r : a) { auto it = first.find(r->date); if (it == first.end() || r->f[MINUTE] < it->second->f[MINUTE]) first[r->date] = r; } double fm[3] = {0, 0, 0}, fn[3] = {0, 0, 0}; for (auto& [d0, r] : first) { fm[r->era] += r->R; fn[r->era]++; }
      printf("  %-44s %.2f/day | days %3.0f%% | all %+.3f / %+.3f / %+.3f | first of the day %+.3f / %+.3f / %+.3f\n", c.name, a.size() / ND, 100.0 * dd.size() / ND, m[0], m[1], m[2], fn[0] ? fm[0] / fn[0] : 0, fn[1] ? fm[1] / fn[1] : 0, fn[2] ? fm[2] / fn[2] : 0); } }
  return 0;
}
