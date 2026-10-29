// 2026-10-22 flag_engulf.cpp (founder: "so test engulfing candles if that gives us an advantage … on all matrix tests we had"). Runs part E
// of research/preregistered/2026-10-22_book_optimize.md exactly as written before the run. Engulfing (bodies, as edge_lab pat_engulf) on
// the flag's own chart (1-5m, 09:30 grid), mirrored for bear flags, closed bars only:
//   EA filter — the last closed chart bar before a flag-mid fill: bearish engulfing / bullish engulfing / neither
//   EB entry  — next minute's open after the first bullish engulfing bar that closes inside the flag between registration and the flag's
//               end (before 15:00); stop = lowest low since the pole top − 0.1 ATR, target pole top + pole, flat at the 15:55 open
// Edge = trade R − post-registration twin (5 random minutes in the hour after the trade's entry, same stop / target distances).
// Matrix cells: chart × pole multiple × pullback × grade (× side in the marginals). Account variants → OUTDIR/<name>_{nq,es}.csv for
// `flag_prop_ev … mtm`. Usage: flag_engulf W15_NQ.csv W15_ES.csv OUTDIR. Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET.
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr, pc, s200; std::vector<int> pos, TM; std::vector<double> O, H, L, C, V; };
struct Bar { int t0; double o, h, l, c; };
static std::string f2(double v) { char b[64]; snprintf(b, sizeof b, "%.2f", v); return b; }
static std::string f4(double v) { char b[64]; snprintf(b, sizeof b, "%.4f", v); return b; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: flag_engulf W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2];
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; } build_days(s, fomc, vix);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{d.atr, d.pc, d.sma200, std::vector<int>(400, -1), {}, {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]); z.V.push_back(s.v[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; rows.push_back(std::move(r)); } }
  auto write = [&](const std::string& name, const std::vector<Row>& rs) {
    // 2026-10-22 (preregistered/2026-10-22_external_ideas.md): env VARIANT=<name> writes only that variant, to OUTDIR/tmp_{nq,es}.csv
    // (overwritten on every run — the disk is ~100 % full and nothing is deleted)
    const char* only = std::getenv("VARIANT"); if (only && name != only) return; const std::string fname = only ? std::string("tmp") : name;
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + fname + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str()); for (auto& r : rs) if (r.mk == mk) { for (size_t j = 0; j < r.tok.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", r.tok[j].c_str()); fprintf(fo, "\n"); } fclose(fo); } };
  auto book2 = [](const Row& r) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return (int)r.a[2] >= 4 && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.O[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  // per row: mid R + twin, EA class, EB trade (R, twin, fields)
  struct T2 { bool ok = false; int em = 0, xm = 0; double en = 0, sp = 0, tg = 0, xp = 0, net = 0, r = NAN, tw = NAN; };   // a backup trade (mirrored prices)
  struct F { int cls = -1; bool mid = false; double rM = NAN, tM = NAN; bool eb = false; double rE = NAN, tE = NAN; int emE = 0, xmE = 0; double enE = 0, spE = 0, tgE = 0, xpE = 0, netE = 0; T2 brk, brkv, h2; bool dtOk = true; bool midFill = false; };   // midFill: the limit DID fill (ok = 0 only because the chart was busy)
  std::vector<F> X(rows.size());
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; auto it = days[r.mk].find((int)r.a[0]); if (it == days[r.mk].end()) continue; const DB& z = it->second;
    const int sd = (int)r.a[1], tf = (int)r.a[2], reg = (int)r.a[3], end = (int)r.a[4]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL, cost = r.mk ? 0.5 : 1.5;
    if (!(M > 0) || reg < 930 || reg >= 1316) continue;
    auto hi1 = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo1 = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::vector<Bar> b;   // the flag's chart, mirrored
    for (size_t k = 0; k < z.TM.size(); ++k) { const int t0 = 930 + (z.TM[k] - 930) / tf * tf; const double hh = hi1((int)k), ll = lo1((int)k), c = sd * z.C[k], o = sd * z.O[k];
      if (b.empty() || b.back().t0 != t0) b.push_back({t0, o, hh, ll, c}); else { b.back().h = std::max(b.back().h, hh); b.back().l = std::min(b.back().l, ll); b.back().c = c; } }
    int top = -1; for (int j = 0; j < (int)b.size(); ++j) if (b[j].t0 == reg - 3 * tf) { top = j; break; } if (top < 1) continue;
    auto bull = [&](int j) { return j >= 1 && b[j - 1].c < b[j - 1].o && b[j].c > b[j].o && b[j].c >= b[j - 1].o && b[j].o <= b[j - 1].c; };
    auto bear = [&](int j) { return j >= 1 && b[j - 1].c > b[j - 1].o && b[j].c < b[j].o && b[j].c <= b[j - 1].o && b[j].o >= b[j - 1].c; };
    F& x = X[i];
    // 2026-10-22 fix: export ok = 0 also covers a flag-mid fill SKIPPED because its chart already held a trade. Rebuild the page's fill
    // rule (lib/flags.ts: limit (H + flag low at registration) / 2 from the registration close until the pole-top break / the flag's end,
    // before 15:00, filled at min(open, limit), stop = lowest low before the fill − 0.1 ATR, valid only if risk > 0.5 pt and the target
    // is above the fill) — a backup may only trade where the limit truly never filled.
    if (r.a[11] < 0.5) { const int kr0 = z.pos[reg - 930], ke0 = end >= 930 && end < 1320 ? z.pos[end - 930] : -1;
      const double fl0 = sd > 0 ? r.a[10] : -r.a[10], lim = (PH + fl0) / 2; double fl = fl0;
      if (kr0 >= 0) for (int k = kr0; k < (int)z.C.size(); ++k) { if (z.TM[k] >= 1290) break; if (ke0 >= 0 && k > ke0) break; if (ke0 >= 0 && k == ke0 && hi1(k) > PH) break;
        if (lo1(k) <= lim) { const double price = std::min(sd * z.O[k], lim), stp = fl - 0.1 * z.atr; x.midFill = price - stp > 0.5 && PH + M > price; break; } fl = std::min(fl, lo1(k)); } }
    if (r.a[11] > 0.5) { const int em = (int)r.a[12]; int last = -1; for (int j = top + 1; j < (int)b.size(); ++j) { if (b[j].t0 + tf > em) break; last = j; }
      x.cls = last < 0 ? 0 : bull(last) ? 1 : bear(last) ? 2 : 0; x.mid = true; x.rM = r.a[18];
      x.tM = twin(z, sd, em, std::fabs(r.a[14] - r.a[15]), std::fabs(r.a[16] - r.a[14]), cost, (unsigned)(r.a[0] * 13 + em * 7 + tf + r.mk * 99991)); }
    // EB: first bullish engulfing chart bar closing inside the flag after registration, before the flag ends
    if (!x.midFill) for (int j = top + 1; j < (int)b.size(); ++j) { if (b[j].t0 < reg) continue; if (b[j].t0 >= end || b[j].t0 + tf >= 1290) break; if (b[j].h > PH) break;
      if (!bull(j) || !(b[j].c < PH)) continue;
      const int em2 = b[j].t0 + tf; const int k0 = z.pos[em2 - 930]; if (k0 < 0) break; const int kt = z.pos[b[top].t0 - 930]; if (kt < 0) break;
      double mn = 1e18; for (int k = kt; k < k0; ++k) mn = std::min(mn, lo1(k)); const double en = sd * z.O[k0], sp = mn - 0.1 * z.atr, tg = PH + M;
      if (!(en - sp > 0.5) || !(tg > en)) break;
      int xm = z.TM[k0]; double xp = sp;
      if (lo1(k0) > sp) { xp = NAN; for (int k = k0 + 1; k < (int)z.C.size(); ++k) { xm = z.TM[k]; if (lo1(k) <= sp) { xp = sp; break; } if (hi1(k) >= tg) { xp = tg; break; } if (z.TM[k] >= 1315) { xp = sd * z.O[k]; break; } }
        if (std::isnan(xp)) xp = sd * z.C.back(); }
      const double risk = en - sp, net = xp - en - cost; x.eb = true; x.rE = net / risk; x.emE = em2; x.xmE = xm; x.enE = en; x.spE = sp; x.tgE = tg; x.xpE = xp; x.netE = net;
      x.tE = twin(z, sd, em2, risk, tg - en, cost, (unsigned)(r.a[0] * 17 + em2 * 3 + tf + 7)); break; }
    // preregistered/2026-10-22_backup_entries.md: trend context and the backups B1-B3 (only where the limit never filled)
    x.dtOk = !(std::isfinite(z.s200) && std::isfinite(z.pc)) || (sd > 0 ? z.pc > z.s200 : z.pc < z.s200);
    if (r.a[11] < 0.5 && !x.midFill) {
      const int kt = z.pos[b[top].t0 - 930], kr = z.pos[reg - 930]; if (kt < 0 || kr < 0) continue;
      auto lowSince = [&](int k0) { double mn = 1e18; for (int k = kt; k < k0; ++k) mn = std::min(mn, lo1(k)); return mn; };
      auto fill = [&](T2& t, int k0, double en, double slip, unsigned seed) {   // page conventions; stop = lowest low since the pole top − 0.1 ATR
        const double sp = lowSince(k0) - 0.1 * z.atr, tg = PH + M; if (!(en - sp > 0.5) || !(tg > en) || z.TM[k0] >= 1290) return;
        int xm = z.TM[k0]; double xp = sp;
        if (lo1(k0) > sp) { xp = NAN; for (int k = k0 + 1; k < (int)z.C.size(); ++k) { xm = z.TM[k]; if (lo1(k) <= sp) { xp = sp; break; } if (hi1(k) >= tg) { xp = tg; break; } if (z.TM[k] >= 1315) { xp = sd * z.O[k]; break; } }
          if (std::isnan(xp)) xp = sd * z.C.back(); }
        t.ok = true; t.em = z.TM[k0]; t.xm = xm; t.en = en; t.sp = sp; t.tg = tg; t.xp = xp; t.net = xp - en - cost - slip; t.r = t.net / (en - sp);
        t.tw = twin(z, sd, t.em, en - sp, tg - en, cost + slip, seed); };
      // B1 / B2: the pole-top break between registration and the flag's end
      const int ke = end >= 930 && end < 1320 ? z.pos[end - 930] : -1;
      if (ke >= 0) for (int k = kr; k <= ke && k < (int)z.C.size(); ++k) { if (z.TM[k] >= 1290) break; if (hi1(k) > PH) {
          const double en = std::max(PH + 0.25, sd * z.O[k]); fill(x.brk, k, en, 0.25, (unsigned)(r.a[0] * 19 + k * 5 + tf));
          const int k1 = z.pos[b[top].t0 + tf - 930]; double vs = 0; int vn = 0; if (k1 >= 0) for (int q = k1; q < k; ++q) { vs += z.V[q]; vn++; }
          if (vn > 0 && z.V[k] >= 1.5 * vs / vn) x.brkv = x.brk; break; } }
      // B3: Al Brooks High 2 — the 2nd chart bar after the top whose high exceeds the previous bar's high (previous high below the top)
      int cnt = 0;
      for (int j = top + 2; j < (int)b.size(); ++j) { if (b[j - 1].h >= PH || b[j].t0 >= end || b[j].t0 >= 1290) break;
        if (!(b[j].h > b[j - 1].h)) continue; if (++cnt < 2) continue;
        const double L = b[j - 1].h + 0.25; int k0 = -1; for (int t = b[j].t0; t < b[j].t0 + tf; ++t) { const int k = t >= 930 && t < 1320 ? z.pos[t - 930] : -1; if (k >= 0 && hi1(k) >= L) { k0 = k; break; } }
        if (k0 >= 0 && z.TM[k0] >= reg) fill(x.h2, k0, std::max(L, sd * z.O[k0]), 0.25, (unsigned)(r.a[0] * 23 + k0 * 3 + tf));
        break; }
    } }

  struct Acc { double n = 0, r = 0, tw = 0; void add(double rr, double t) { if (std::isnan(rr) || std::isnan(t)) return; n++; r += rr; tw += t; } double R() const { return n ? r / n : NAN; } double e() const { return n ? (r - tw) / n : NAN; } };
  auto half = [](const Row& r) { return (int)r.a[0] < 20200101 ? 0 : 1; };
  auto yr = [](const Row& r) { return (int)r.a[0] / 10000; };
  auto line = [](const char* name, Acc (&a)[2][2]) { printf("  %-44s", name); for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) printf(" %s%s n %5.0f R %+6.3f edge %+6.3f |", mk ? "ES" : "NQ", h ? " 20-26" : " 14-19", a[mk][h].n, a[mk][h].R(), a[mk][h].e()); printf("\n"); };
  for (int set = 0; set < 2; ++set) { printf("\n######## %s (2014-26)\n", set ? "BOOK2 flags" : "every wide15 flag (1-5m)");
    Acc mid[2][2], eb[2][2], ebBoth[2][2], midBoth[2][2], cls[3][2][2], ebOnly[2][2];
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (yr(r) < 2014 || (set && !book2(r))) continue; const F& x = X[i]; const int mk = r.mk, h = half(r);
      if (x.mid) { mid[mk][h].add(x.rM, x.tM); cls[x.cls][mk][h].add(x.rM, x.tM); } if (x.eb) eb[mk][h].add(x.rE, x.tE);
      if (x.mid && x.eb) { midBoth[mk][h].add(x.rM, x.tM); ebBoth[mk][h].add(x.rE, x.tE); } if (x.eb && !x.mid) ebOnly[mk][h].add(x.rE, x.tE); }
    line("flag-mid entry (every fill)", mid); line("EB engulfing entry (every one)", eb);
    line("same flags, both entries: flag-mid", midBoth); line("same flags, both entries: EB", ebBoth); line("EB on flags whose limit never filled", ebOnly);
    const char* CN[3] = {"EA last bar before fill: neither", "EA last bar before fill: BULLISH engulf", "EA last bar before fill: BEARISH engulf"};
    for (int c = 0; c < 3; ++c) line(CN[c], cls[c]); }

  // ── matrix: chart × pole multiple × pullback × grade (BOOK2 grade A/B and C included), EB minus flag-mid edge
  auto mB = [](double m) { return m < 3 ? 0 : m < 4 ? 1 : m < 5 ? 2 : 3; }; auto pB = [](double p) { return p < 25 ? 0 : p < 38 ? 1 : p < 50 ? 2 : 3; };
  const char* MN[4] = {"pole 2-3x", "pole 3-4x", "pole 4-5x", "pole 5x+"}; const char* PN[4] = {"pb 5-25%", "pb 25-38%", "pb 38-50%", "pb 50-70%"}; const char* GN[3] = {"A", "B", "C"};
  printf("\n######## MATRIX marginals (every wide15 flag): flag-mid vs EB edge, 2014-19 | 2020-26, NQ | ES\n");
  auto marg = [&](const char* dim, int nb, std::function<int(const Row&)> key, std::function<std::string(int)> nm) {
    printf("  -- %s\n", dim);
    for (int k = 0; k < nb; ++k) { Acc a[2][2], e[2][2]; for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (yr(r) < 2014 || key(r) != k) continue; const F& x = X[i]; if (x.mid) a[r.mk][half(r)].add(x.rM, x.tM); if (x.eb) e[r.mk][half(r)].add(x.rE, x.tE); }
      printf("    %-12s mid", nm(k).c_str()); for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) printf(" %+6.3f(%4.0f)", a[mk][h].e(), a[mk][h].n);
      printf("  · EB"); for (int mk = 0; mk < 2; ++mk) for (int h = 0; h < 2; ++h) printf(" %+6.3f(%4.0f)", e[mk][h].e(), e[mk][h].n); printf("\n"); } };
  marg("chart", 5, [](const Row& r) { return (int)r.a[2] - 1; }, [](int k) { return std::to_string(k + 1) + "m"; });
  marg("pole multiple", 4, [&](const Row& r) { return mB(r.a[22]); }, [&](int k) { return std::string(MN[k]); });
  marg("pullback", 4, [&](const Row& r) { return pB(r.a[24] * 100); }, [&](int k) { return std::string(PN[k]); });
  marg("grade", 3, [](const Row& r) { return std::max(0, std::min(2, (int)r.a[6] - 1)); }, [&](int k) { return std::string(GN[k]); });
  marg("side", 2, [](const Row& r) { return r.a[1] > 0 ? 0 : 1; }, [](int k) { return std::string(k ? "bear" : "bull"); });
  // cells chosen on 2014-19 (EB edge, n >= 40 on both markets), judged on 2020-26
  { struct C { int k; Acc e[2][2], m[2][2]; }; std::map<int, C> cells;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (yr(r) < 2014) continue; const int k = (((int)r.a[2] - 1) * 4 + mB(r.a[22])) * 4 * 3 + pB(r.a[24] * 100) * 3 + std::max(0, std::min(2, (int)r.a[6] - 1));
      auto& c = cells[k]; c.k = k; const F& x = X[i]; if (x.eb) c.e[r.mk][half(r)].add(x.rE, x.tE); if (x.mid) c.m[r.mk][half(r)].add(x.rM, x.tM); }
    std::vector<C*> v; for (auto& [k, c] : cells) if (c.e[0][0].n >= 40 && c.e[1][0].n >= 40) v.push_back(&c);
    std::sort(v.begin(), v.end(), [](C* a, C* b) { return a->e[0][0].e() + a->e[1][0].e() > b->e[0][0].e() + b->e[1][0].e(); });
    printf("\n######## MATRIX cells (chart × pole × pullback × grade) with n >= 40 EB trades on both markets in 2014-19: %zu; top 5 by 2014-19 EB edge → 2020-26\n", v.size());
    for (size_t q = 0; q < v.size() && q < 5; ++q) { const C& c = *v[q]; const int g = c.k % 3, pb = (c.k / 3) % 4, mb = (c.k / 12) % 4, tf = c.k / 48 + 1;
      printf("  %dm %s %s %s  EB NQ %+6.3f → %+6.3f (n %3.0f) · ES %+6.3f → %+6.3f (n %3.0f) | flag-mid NQ %+6.3f → %+6.3f · ES %+6.3f → %+6.3f\n", tf, MN[mb], PN[pb], GN[g],
        c.e[0][0].e(), c.e[0][1].e(), c.e[0][1].n, c.e[1][0].e(), c.e[1][1].e(), c.e[1][1].n, c.m[0][0].e(), c.m[0][1].e(), c.m[1][0].e(), c.m[1][1].e()); } }

  // ── the /flags page check (bot/frontend/scripts/flag-parity/book-eb.js): every EB trade on a flag whose limit never filled, 4-5m
  { FILE* fo = fopen((out + "/eb_all.csv").c_str(), "w"); if (!fo) { perror("eb_all.csv"); return 1; }
    fprintf(fo, "date,sym,side,tf,reg,em,xm,entry,stop,target,exit,r_net\n");
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const F& x = X[i]; if (r.a[11] > 0.5 || !x.eb || (int)r.a[2] < 4) continue; const int sd = (int)r.a[1];
      fprintf(fo, "%d,%s,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.4f\n", (int)r.a[0], r.mk ? "ES" : "NQ", sd, (int)r.a[2], (int)r.a[3], x.emE, x.xmE, sd * x.enE, sd * x.spE, sd * x.tgE, sd * x.xpE, x.rE); }
    fclose(fo); }
  // ── account variants
  write("base", rows);
  { std::vector<Row> A = rows, B = rows; int na = 0, nb = 0; for (size_t i = 0; i < rows.size(); ++i) { if (!book2(rows[i]) || rows[i].a[11] < 0.5) continue; if (X[i].cls == 2) { A[i].tok[11] = "0"; na++; } if (X[i].cls == 1) { B[i].tok[11] = "0"; nb++; } }
    printf("\naccount variants: EAskipbear drops %d BOOK2 trades, EAskipbull drops %d\n", na, nb); write("EAskipbear", A); write("EAskipbull", B); }
  for (int mode = 0; mode < 2; ++mode) { std::vector<Row> V = rows; int n = 0;
    for (size_t i = 0; i < rows.size(); ++i) { if (!book2(rows[i])) continue; const F& x = X[i]; const int sd = (int)rows[i].a[1];
      if (mode == 1 && rows[i].a[11] > 0.5) continue;   // backup only: keep the limit's trade
      if (!x.eb) { if (mode == 0) V[i].tok[11] = "0"; continue; }
      Row& r = V[i]; r.tok[11] = "1"; r.tok[12] = std::to_string(x.emE); r.tok[13] = std::to_string(x.xmE); r.tok[14] = f2(sd * x.enE); r.tok[15] = f2(sd * x.spE); r.tok[16] = f2(sd * x.tgE); r.tok[17] = f2(sd * x.xpE);
      r.tok[18] = f4(x.rE); r.tok[19] = f4(x.netE); n++; }
    printf("account variant %s: %d BOOK2 flags trade the EB entry\n", mode ? "EBbackup" : "EBentry", n); write(mode ? "EBbackup" : "EBentry", V); }
  // ── preregistered/2026-10-22_backup_entries.md: B1-B5 backups and C1 trend context
  auto ebT = [&](const F& x) { T2 t; if (!x.eb) return t; t.ok = true; t.em = x.emE; t.xm = x.xmE; t.en = x.enE; t.sp = x.spE; t.tg = x.tgE; t.xp = x.xpE; t.net = x.netE; t.r = x.rE; t.tw = x.tE; return t; };
  auto first = [](const T2& a, const T2& c) { if (!a.ok) return c; if (!c.ok) return a; return a.em <= c.em ? a : c; };
  struct BK { const char* name; std::function<T2(const F&)> pick; };
  const BK bks[] = {{"B0EB", [&](const F& x) { return ebT(x); }}, {"B1BRK", [](const F& x) { return x.brk; }}, {"B2BRKVOL", [](const F& x) { return x.brkv; }},
                    {"B3H2", [](const F& x) { return x.h2; }}, {"B4EBorBRK", [&](const F& x) { return first(ebT(x), x.brk); }}, {"B5H2orEB", [&](const F& x) { return first(x.h2, ebT(x)); }}};
  printf("\n######## BACKUP ENTRIES on BOOK2 flags whose limit never filled (2014-26): R and edge over post-registration twins\n");
  for (auto& bk : bks) { Acc a[2][2]; std::vector<Row> V = rows; int n = 0;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r) || r.a[11] > 0.5) continue; const T2 t = bk.pick(X[i]); if (!t.ok) continue;
      if (yr(r) >= 2014) a[r.mk][half(r)].add(t.r, t.tw); const int sd = (int)r.a[1]; Row& w = V[i];
      w.tok[11] = "1"; w.tok[12] = std::to_string(t.em); w.tok[13] = std::to_string(t.xm); w.tok[14] = f2(sd * t.en); w.tok[15] = f2(sd * t.sp); w.tok[16] = f2(sd * t.tg); w.tok[17] = f2(sd * t.xp);
      w.tok[18] = f4(t.r); w.tok[19] = f4(t.net); n++; }
    char nm[96]; snprintf(nm, sizeof nm, "%s (%d trades 2010-26)", bk.name, n); line(nm, a); write(bk.name, V); }
  { std::vector<Row> V; { std::ifstream f0(out + "/B0EB_nq.csv"); } V = rows; int nd = 0;   // C1: the candidate (EB backup) without counter-trend flags
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r)) continue; const T2 t = ebT(X[i]); const int sd = (int)r.a[1];
      if (r.a[11] < 0.5 && t.ok) { Row& w = V[i]; w.tok[11] = "1"; w.tok[12] = std::to_string(t.em); w.tok[13] = std::to_string(t.xm); w.tok[14] = f2(sd * t.en); w.tok[15] = f2(sd * t.sp);
        w.tok[16] = f2(sd * t.tg); w.tok[17] = f2(sd * t.xp); w.tok[18] = f4(t.r); w.tok[19] = f4(t.net); }
      if (!X[i].dtOk && V[i].tok[11] == "1") { V[i].tok[11] = "0"; nd++; } }
    printf("C1DT200: the candidate without %d counter-trend BOOK2 trades\n", nd); write("C1DT200", V); }
  return 0;
}
