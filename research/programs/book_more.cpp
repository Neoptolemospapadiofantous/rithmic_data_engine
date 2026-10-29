// 2026-10-22 book_more.cpp (founder: "overview re evaluate and lets do more tests. give me more ideas" → "test all"). Runs
// research/preregistered/2026-10-22_book_more.md exactly as written before the run (T3 is not a run, T7 / T8 / T9 run in flag_prop_ev /
// flag_types):
//   T1 breakout fallback when the flag-mid limit never filled   T2 re-entry at the pole top after a stop-out   T4 3m charts in BOOK2
//   T5 BOOK2 + CASCADE / WEAK DAY / MONDAY streams on NQ (fixed middle parameters)   T6 overlap of those streams with BOOK2
//   T10 news days (CPI / NFP / big-tech earnings)   T11 resting-book imbalance at the fill (NQ 2023-03 → 2026-03)
// Edge = trade R − its post-registration twin (5 random minutes in the hour after the trade's entry, same stop / target distances).
// Variants for the account check go to OUTDIR/<name>_{nq,es}.csv (judged by `flag_prop_ev A B prob`).
// Usage: book_more W15_NQ.csv W15_ES.csv OUTDIR [book3] (book3: preregistered/2026-10-22_book3.md variants only). Read-only on data/edge_lab/cache. tday = minutes since 18:00 ET (09:30 = 930, 15:55 = 1315).
#include "research/meta.hpp"
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <random>
#include <sstream>
using namespace lab;
struct Row { std::vector<std::string> tok; std::vector<double> a; int mk; };
struct DB { double atr, pibs, vix; int dow; bool news, cpi, nfp, earn; std::vector<int> pos, TM; std::vector<double> O, H, L, C, QI; };
static std::string f2(double v) { char b[64]; snprintf(b, sizeof b, "%.2f", v); return b; }
static std::string f4(double v) { char b[64]; snprintf(b, sizeof b, "%.4f", v); return b; }
int main(int argc, char** argv) {
  if (argc < 4) { fprintf(stderr, "usage: book_more W15_NQ.csv W15_ES.csv OUTDIR\n"); return 1; }
  const std::string out = argv[3]; std::string header; std::vector<Row> rows; std::map<int, DB> days[2]; size_t nbook = 0, nev = 0;
  for (int mk = 0; mk < 2; ++mk) { const char* sym = mk ? "ES" : "NQ";
    std::set<int> fomc; paper::VixRegime vix; vix.load("data/regime"); Series s; std::string err;
    if (!load_raw("data/edge_lab/cache", sym, s, err)) { fprintf(stderr, "%s\n", err.c_str()); return 1; }
    if (!mk) nbook = load_book("data/edge_lab/cache", sym, s); build_days(s, fomc, vix); nev += load_events("data/edge_lab/cache", s);
    for (auto& d : s.days) { if (!d.ok || d.rollwin || !(d.atr > 0) || d.date < 20100101 || d.date > 20261016) continue;
      DB z{d.atr, d.pibs, d.vix, d.dow, d.cpi || d.nfp || d.earn, d.cpi, d.nfp, d.earn, std::vector<int>(400, -1), {}, {}, {}, {}, {}, {}};
      for (int i = d.b0; i < d.b1; ++i) { const int tm = tday_min(s.t[i]); if (tm < 930 || tm >= 1320) continue;
        z.pos[tm - 930] = (int)z.C.size(); z.TM.push_back(tm); z.O.push_back(s.o[i]); z.H.push_back(s.h[i]); z.L.push_back(s.l[i]); z.C.push_back(s.c[i]);
        z.QI.push_back(s.qimb.empty() ? NAN : (double)s.qimb[i]); }
      if (z.C.size() >= 370) days[mk][d.date] = std::move(z); }
    std::ifstream f(argv[1 + mk]); if (!f) { perror(argv[1 + mk]); return 1; } std::string ln; std::getline(f, ln); header = ln;
    while (std::getline(f, ln)) { Row r; r.mk = mk; std::stringstream ss(ln); std::string t; while (std::getline(ss, t, ',')) { r.tok.push_back(t); r.a.push_back(strtod(t.c_str(), nullptr)); }
      if (r.a.size() < 71) continue; rows.push_back(std::move(r)); } }
  printf("book minutes loaded (NQ): %zu · event days flagged: %zu\n", nbook, nev);
  auto write = [&](const std::string& name, const std::vector<Row>& rs) {
    for (int mk = 0; mk < 2; ++mk) { const std::string fn = out + "/" + name + (mk ? "_es.csv" : "_nq.csv"); FILE* fo = fopen(fn.c_str(), "w"); if (!fo) { perror(fn.c_str()); exit(1); }
      fprintf(fo, "%s\n", header.c_str()); for (auto& r : rs) if (r.mk == mk) { for (size_t j = 0; j < r.tok.size(); ++j) fprintf(fo, "%s%s", j ? "," : "", r.tok[j].c_str()); fprintf(fo, "\n"); } fclose(fo); } };
  auto qual = [](const Row& r, int tfmin) { const double mult = r.a[22], pull = r.a[24] * 100; const int g = (int)r.a[6]; return (int)r.a[2] >= tfmin && mult >= 3 && pull >= 5 && pull < 38 && (g == 1 || g == 2); };
  auto book2 = [&](const Row& r) { return r.a[11] > 0.5 && qual(r, 4); };
  auto dayOf = [&](const Row& r) -> const DB* { auto it = days[r.mk].find((int)r.a[0]); return it == days[r.mk].end() ? nullptr : &it->second; };
  // simulate a mirrored long from bar k0 (entry price en, stop sp, target tg, entry-bar rule: low <= stop = loss, target not taken in the entry bar)
  struct Sim { int xm; double gross; double xpx; };
  auto simulate = [](const DB& z, int sd, int k0, double en, double sp, double tg) -> Sim {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    if (lo(k0) <= sp) return {z.TM[k0], sp - en, sp};
    for (int k = k0 + 1; k < (int)z.C.size(); ++k) { if (lo(k) <= sp) return {z.TM[k], sp - en, sp}; if (hi(k) >= tg) return {z.TM[k], tg - en, tg}; if (z.TM[k] >= 1315) return {z.TM[k], sd * z.C[k] - en, sd * z.C[k]}; }
    return {z.TM.back(), sd * z.C.back() - en, sd * z.C.back()}; };
  auto twin = [](const DB& z, int sd, int em, double risk, double tgd, double cost, unsigned seed) {
    auto hi = [&](int k) { return sd > 0 ? z.H[k] : -z.L[k]; }; auto lo = [&](int k) { return sd > 0 ? z.L[k] : -z.H[k]; };
    std::mt19937 g(seed); double tw = 0; int tn = 0; if (!(risk > 0)) return (double)NAN;
    for (int rep = 0; rep < 5; ++rep) { const int tm = em + (int)(g() % 60); if (tm >= 1315) continue; const int k0 = z.pos[tm - 930]; if (k0 < 0) continue; const double en = sd * z.O[k0]; double gross = NAN;
      for (int k = k0; k < (int)z.C.size(); ++k) { if (lo(k) <= en - risk) { gross = -risk; break; } if (hi(k) >= en + tgd) { gross = tgd; break; } if (z.TM[k] >= 1315) { gross = sd * z.C[k] - en; break; } }
      if (std::isnan(gross)) gross = sd * z.C.back() - en; tw += (gross - cost) / risk; tn++; }
    return tn ? tw / tn : (double)NAN; };
  struct Acc { double n = 0, r = 0, tw = 0; void add(double rr, double t) { if (std::isnan(t)) return; n++; r += rr; tw += t; } double R() const { return n ? r / n : NAN; } double e() const { return n ? (r - tw) / n : NAN; } };
  struct Cell { Acc a, b; void add(int y, double rr, double t) { if (y < 2014) return; (y <= 2021 ? a : b).add(rr, t); } };
  auto pc = [](const char* name, const Cell* c) { printf("  %-46s", name); for (int mk = 0; mk < 2; ++mk) printf(" %s 14-21 n %5.0f R %+6.3f edge %+6.3f · 22-26 n %5.0f R %+6.3f edge %+6.3f |", mk ? "ES" : "NQ", c[mk].a.n, c[mk].a.R(), c[mk].a.e(), c[mk].b.n, c[mk].b.R(), c[mk].b.e()); printf("\n"); };
  // twins for every traded row (BOOK2 reference + T10 / T11)
  std::vector<double> tw(rows.size(), NAN);
  for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (r.a[11] < 0.5) continue; const DB* z = dayOf(r); if (!z) continue; const int em = (int)r.a[12]; if (em < 930 || em >= 1315) continue;
    tw[i] = twin(*z, (int)r.a[1], em, std::fabs(r.a[14] - r.a[15]), std::fabs(r.a[16] - r.a[14]), r.mk ? 0.5 : 1.5, (unsigned)(r.a[0] * 13 + em * 7 + r.a[2] + r.mk * 99991)); }
  { Cell c[2]; for (size_t i = 0; i < rows.size(); ++i) if (book2(rows[i])) c[rows[i].mk].add((int)rows[i].a[0] / 10000, rows[i].a[18], tw[i]); printf("\nReference\n"); pc("BOOK2 trades (every qualifying flag)", c); }
  write("base", rows);

  // ── mode book3 (preregistered/2026-10-22_book3.md): BOOK3 confirmation variants with the /flags page's conventions (entry before
  // 15:00, flat at the 15:55 bar's open, risk > 0.5 pt). A variant = the rows that qualify under (tfmin, gmax, pullmax) relabelled so
  // flag_prop_ev's BOOK2 rule takes them (tf → 4, grade 3 → 2, pullback → 0.30), plus the breakout backup on those whose limit never filled.
  if (argc > 4 && std::string(argv[4]) == "book3") {
    auto variant = [&](const char* name, int tfmin, int gmax, double pullmax, bool closeMode, bool parity) {
      std::vector<Row> V = rows; Cell c[2]; int nb = 0, nq = 0;
      for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; const int tf = (int)r.a[2], g = (int)r.a[6]; const double mult = r.a[22], pull = r.a[24] * 100;
        const bool q = tf >= tfmin && mult >= 3 && pull >= 5 && pull < pullmax && g <= gmax && g >= 1; if (!(parity ? tf >= 3 : q)) continue; nq++;
        if (!parity) { if (tf < 4) V[i].tok[2] = "4"; if (g == 3) V[i].tok[6] = "2"; if (pull >= 38) V[i].tok[24] = "0.3"; }
        if (r.a[11] > 0.5) continue; const DB* z = dayOf(r); if (!z) continue;
        const int sd = (int)r.a[1], reg = (int)r.a[3], end = (int)r.a[4]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL, cost = r.mk ? 0.5 : 1.5;
        if (!(M > 0) || reg < 930 || end < reg || end >= 1316) continue; const int kr = z->pos[reg - 930], ke = z->pos[end - 930]; if (kr < 0 || ke < 0) continue;
        auto hi = [&](int k) { return sd > 0 ? z->H[k] : -z->L[k]; }; auto lo = [&](int k) { return sd > 0 ? z->L[k] : -z->H[k]; };
        auto op = [&](int k) { return sd * z->O[k]; }; auto cl = [&](int k) { return sd * z->C[k]; };
        double mn = sd > 0 ? r.a[10] : -r.a[10]; int k0 = -1; double en = NAN;
        for (int k = kr; k <= ke && k < (int)z->C.size(); ++k) {
          if (!closeMode) { if (z->TM[k] >= 1290) break; if (hi(k) > PH) { k0 = k; en = std::max(PH + 0.25, op(k)); break; } }
          else if (cl(k) > PH) { if (k + 1 < (int)z->C.size() && z->TM[k + 1] < 1290) { mn = std::min(mn, lo(k)); k0 = k + 1; en = op(k0); } break; }
          mn = std::min(mn, lo(k)); }
        if (k0 < 0) continue; const double sp = mn - 0.1 * z->atr, tg = PH + M; if (!(en - sp > 0.5) || !(tg > en)) continue;
        int xm = z->TM[k0]; double xp = sp;
        if (lo(k0) > sp) { xp = NAN; for (int k = k0 + 1; k < (int)z->C.size(); ++k) { xm = z->TM[k]; if (lo(k) <= sp) { xp = sp; break; } if (hi(k) >= tg) { xp = tg; break; } if (z->TM[k] >= 1315) { xp = op(k); break; } }
          if (std::isnan(xp)) xp = cl((int)z->C.size() - 1); }
        const double risk = en - sp, net = xp - en - cost - 0.25, rr = net / risk; const int em = z->TM[k0];
        c[r.mk].add((int)r.a[0] / 10000, rr, twin(*z, sd, em, risk, tg - en, cost + 0.25, (unsigned)(r.a[0] * 17 + em * 3 + 11)));
        Row& n = V[i]; n.tok[11] = "1"; n.tok[12] = std::to_string(em); n.tok[13] = std::to_string(xm); n.tok[14] = f2(sd * en); n.tok[15] = f2(sd * sp); n.tok[16] = f2(sd * tg); n.tok[17] = f2(sd * xp);
        n.tok[18] = f4(rr); n.tok[19] = f4(net); nb++; }
      printf("\n%s — %d qualifying flag rows, %d breakout trades added\n", name, nq, nb); pc("breakout trades", c); write(name, V); };
    variant("C0book3", 3, 2, 38, false, false);
    variant("R2tf2", 2, 2, 38, false, false);
    variant("R3gradeC", 3, 3, 38, false, false);
    variant("R4pull50", 3, 2, 50, false, false);
    variant("R6close", 3, 2, 38, true, false);
    variant("P0parity", 3, 3, 101, false, true);   // the page check: breakout on every 3-5m flag whose limit never filled, no relabelling
    return 0; }

  // ── T1 breakout fallback + T2 re-entry after a stop
  for (int test = 1; test <= 2; ++test) { std::vector<Row> V = rows; Cell c[2]; int added = 0;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!qual(r, 4)) continue; const DB* z = dayOf(r); if (!z) continue;
      const int sd = (int)r.a[1]; const double PH = sd > 0 ? r.a[9] : -r.a[8], PL = sd > 0 ? r.a[8] : -r.a[9], M = PH - PL, tick = 0.25, cost = r.mk ? 0.5 : 1.5; if (!(M > 0)) continue;
      auto hi = [&](int k) { return sd > 0 ? z->H[k] : -z->L[k]; }; auto lo = [&](int k) { return sd > 0 ? z->L[k] : -z->H[k]; };
      int k0 = -1; double sp = NAN;
      if (test == 1) { if (r.a[11] > 0.5) continue; const int reg = (int)r.a[3], end = (int)r.a[4]; if (end < 930 || end >= 1315 || reg < 930) continue; const int ke = z->pos[end - 930], kr = z->pos[std::min(reg, end) - 930]; if (ke < 0 || kr < 0) continue;
        if (!(hi(ke) > PH)) continue; double mn = sd > 0 ? r.a[10] : -r.a[10]; for (int k = kr; k <= ke; ++k) { if (hi(k) > PH) { k0 = k; break; } mn = std::min(mn, lo(k)); }   // first minute the buy stop triggers
        sp = mn - 0.1 * z->atr; }
      else { if (!book2(r) || !(r.a[18] < 0) || std::fabs(r.a[17] - r.a[15]) > 1e-6) continue; const int xm = (int)r.a[13]; if (xm < 930 || xm >= 1230) continue; const int kx = z->pos[xm - 930]; if (kx < 0) continue;
        double mn = lo(kx); for (int k = kx + 1; k < (int)z->C.size() && z->TM[k] <= std::min(xm + 60, 1229); ++k) { if (hi(k) > PH) { k0 = k; break; } mn = std::min(mn, lo(k)); } if (k0 < 0) continue; sp = mn - 0.1 * z->atr; }
      const double en = std::max(PH + tick, sd * z->O[k0]), tg = PH + M; if (!(sp < en) || !(tg > en)) continue;   /* a minute opening above the trigger fills at its open */ const Sim s = simulate(*z, sd, k0, en, sp, tg);
      const double risk = en - sp, net = s.gross - cost - 0.25, rr = net / risk; const int em = z->TM[k0];
      c[r.mk].add((int)r.a[0] / 10000, rr, twin(*z, sd, em, risk, tg - en, cost + 0.25, (unsigned)(r.a[0] * 17 + em * 3 + test)));
      Row n = r; n.tok[11] = "1"; n.tok[12] = std::to_string(em); n.tok[13] = std::to_string(s.xm); n.tok[14] = f2(sd * en); n.tok[15] = f2(sd * sp); n.tok[16] = f2(sd * tg); n.tok[17] = f2(sd * s.xpx);
      n.tok[18] = f4(rr); n.tok[19] = f4(net); n.a[11] = 1; if (test == 1) V[i] = n; else V.push_back(n); added++; }
    printf("\n%s — %d trades added on BOOK2-qualifying flags\n", test == 1 ? "T1 BREAKOUT FALLBACK (limit never filled, pole top broke)" : "T2 RE-ENTRY at the pole top within 60 min after a stop-out", added);
    pc("added trades", c); write(test == 1 ? "T1break" : "T2reentry", V); }

  // ── T4 3m in BOOK2
  { std::vector<Row> V = rows; Cell c[2]; int n = 0; for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if ((int)r.a[2] != 3) continue; if (r.a[11] > 0.5 && qual(r, 3)) { c[r.mk].add((int)r.a[0] / 10000, r.a[18], tw[i]); n++; } V[i].tok[2] = "4"; }
    printf("\nT4 3m CHARTS IN BOOK2 — %d 3m trades would join\n", n); pc("3m trades meeting BOOK2's other rules", c); write("T4tf3", V); }

  // ── T5 streams (NQ) + T6 overlap
  { const double cost = 1.5; std::vector<Row> S[3]; const char* SN[3] = {"CASCADE", "WEAKDAY", "MONDAY"}; Cell c[3][2];
    auto mkrow = [&](int date, int sd, int em, int xm, double en, double sp, double tg, double xp, double net) { Row r; r.mk = 0; r.tok.assign(71, "0");
      r.tok[0] = std::to_string(date); r.tok[1] = std::to_string(sd); r.tok[2] = "5"; r.tok[3] = std::to_string(em); r.tok[4] = std::to_string(em); r.tok[5] = "-1"; r.tok[6] = "1"; r.tok[11] = "1";
      r.tok[12] = std::to_string(em); r.tok[13] = std::to_string(xm); r.tok[14] = f2(en); r.tok[15] = f2(sp); r.tok[16] = f2(tg); r.tok[17] = f2(xp); r.tok[18] = f4(net / std::fabs(en - sp)); r.tok[19] = f4(net);
      r.tok[22] = "5"; r.tok[24] = "0.2"; r.tok[47] = "1"; r.a.assign(71, 0); for (int j = 0; j < 71; ++j) r.a[j] = strtod(r.tok[j].c_str(), nullptr); return r; };
    for (auto& [date, z] : days[0]) { if (z.pos[0] < 0) continue; const double op = z.O[z.pos[0]], A = z.atr; const int y = date / 10000;
      // CASCADE: 09:50 > 0.25 ATR, else 10:30 / 11:00 / 11:30 > 0.30 ATR, enter that minute's open, stop 0.25 ATR, 2R
      const int at[4] = {980, 1020, 1050, 1080}; const double kk[4] = {0.25, 0.30, 0.30, 0.30};
      for (int q = 0; q < 4; ++q) { const int kp = z.pos[at[q] - 1 - 930], ka = z.pos[at[q] - 930]; if (kp < 0 || ka < 0) continue; const double px = z.C[kp]; if (!(std::fabs(px - op) > kk[q] * A)) continue;
        const int sd = px > op ? 1 : -1; const double en = sd * z.O[ka], sp = en - 0.25 * A, tg = en + 0.5 * A; const Sim s = simulate(z, sd, ka, en, sp, tg); const double net = s.gross - cost;
        c[0][0].add(y, net / (0.25 * A), twin(z, sd, at[q], 0.25 * A, 0.5 * A, cost, date * 7 + q)); S[0].push_back(mkrow(date, sd, at[q], s.xm, sd * en, sd * sp, sd * tg, sd * s.xpx, net)); break; }
      // WEAK DAY: prior IBS <= 0.3 and VIX >= 20 → long 09:30 → 15:55 (row stop 1 ATR is only a placeholder for the $ / MAE bookkeeping)
      if (z.pibs <= 0.3 && z.vix >= 20) { const int kx = z.pos[1315 - 930]; if (kx >= 0) { const double net = z.C[kx] - op - cost; c[1][0].add(y, net / A, 0.0);
        S[1].push_back(mkrow(date, 1, 930, 1315, op, op - A, op + 10 * A, z.C[kx], net)); } }
      // MONDAY: long 09:30, stop 0.5 ATR, flat 15:55
      if (z.dow == 1) { const Sim s = simulate(z, 1, z.pos[0], op, op - 0.5 * A, op + 10 * A); const double net = s.gross - cost; c[2][0].add(y, net / (0.5 * A), 0.0);
        S[2].push_back(mkrow(date, 1, 930, s.xm, op, op - 0.5 * A, op + 10 * A, s.xpx, net)); } }
    printf("\nT5 STREAMS on NQ (fixed middle parameters; WEAK DAY / MONDAY have no twin — edge column = R)\n");
    for (int q = 0; q < 3; ++q) { Cell cc[2] = {c[q][0], {}}; char nm[64]; snprintf(nm, sizeof nm, "%s (%zu trades 2010-26)", SN[q], S[q].size()); pc(nm, cc); }
    for (int q = 0; q < 3; ++q) { std::vector<Row> V = rows; V.insert(V.end(), S[q].begin(), S[q].end()); write(std::string("T5book_") + SN[q], V);
      std::vector<Row> W; for (auto& r : rows) { Row x = r; x.tok[11] = "0"; W.push_back(x); } W.insert(W.end(), S[q].begin(), S[q].end()); write(std::string("T5alone_") + SN[q], W); }
    { std::vector<Row> V = rows; for (int q = 0; q < 3; ++q) V.insert(V.end(), S[q].begin(), S[q].end()); write("T5book_ALL", V); }
    // T6 overlap: daily net points (NQ-equivalent, today's ATR scale ignored — correlation only) on 2014-26 days
    std::map<int, double> bk; for (auto& r : rows) if (book2(r) && r.a[0] >= 20140101) bk[(int)r.a[0]] += r.a[18];
    printf("\nT6 OVERLAP with BOOK2 (daily sums of R, 2014-26 sessions; every qualifying BOOK2 flag, no booking)\n");
    for (int q = 0; q < 3; ++q) { std::map<int, double> sm; for (auto& r : S[q]) if (r.a[0] >= 20140101) sm[(int)r.a[0]] += r.a[18];
      double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; int bl = 0, both = 0, sl = 0, sboth = 0;
      for (auto& [d, z] : days[0]) { if (d < 20140101) continue; const double x = bk.count(d) ? bk[d] : 0, yv = sm.count(d) ? sm[d] : 0; n++; sx += x; sy += yv; sxx += x * x; syy += yv * yv; sxy += x * yv;
        if (x < 0) { bl++; both += yv < 0; } if (sm.count(d) && yv < 0) { sl++; sboth += x < 0; } }
      const double cor = (n * sxy - sx * sy) / std::sqrt(std::max(1e-12, (n * sxx - sx * sx) * (n * syy - sy * sy)));
      printf("  %-8s daily correlation %+.3f · on BOOK2's %d losing days the stream also lost on %d (%.1f %%) · on the stream's %d losing days BOOK2 also lost on %d (%.1f %%)\n",
        SN[q], cor, bl, both, bl ? 100.0 * both / bl : 0, sl, sboth, sl ? 100.0 * sboth / sl : 0); } }

  // ── T10 news days
  { Cell c[4][2]; const char* NN[4] = {"BOOK2 on CPI days", "BOOK2 on NFP days", "BOOK2 on big-tech earnings days", "BOOK2 on other days"}; std::vector<Row> V = rows; int dropped = 0;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (!book2(r)) continue; const DB* z = dayOf(r); if (!z) continue; const int y = (int)r.a[0] / 10000;
      if (z->cpi) c[0][r.mk].add(y, r.a[18], tw[i]); if (z->nfp) c[1][r.mk].add(y, r.a[18], tw[i]); if (z->earn) c[2][r.mk].add(y, r.a[18], tw[i]); if (!z->news) c[3][r.mk].add(y, r.a[18], tw[i]);
      if (z->news) { V[i].tok[11] = "0"; dropped++; } }
    printf("\nT10 NEWS DAYS — skip variant drops %d BOOK2 trades\n", dropped); for (int q = 0; q < 4; ++q) pc(NN[q], c[q]); write("T10nonews", V); }

  // ── T11 book imbalance at the fill (NQ)
  printf("\nT11 RESTING-BOOK IMBALANCE in the minute before the fill, mirrored to the trade side (NQ, book minutes only)\n");
  for (int set = 0; set < 2; ++set) for (int h = 0; h < 3; ++h) { Acc w, a;
    for (size_t i = 0; i < rows.size(); ++i) { const Row& r = rows[i]; if (r.mk || r.a[11] < 0.5 || (set && !book2(r))) continue; const DB* z = dayOf(r); if (!z) continue; const int em = (int)r.a[12]; if (em <= 930 || em >= 1316) continue;
      const int k = z->pos[em - 1 - 930]; if (k < 0 || std::isnan(z->QI[k])) continue; const bool first = r.a[0] < 20241001; if (h == 1 && !first) continue; if (h == 2 && first) continue;
      (r.a[1] * z->QI[k] < 0 ? a : w).add(r.a[18], tw[i]); }
    printf("  %-12s %-20s with: n %5.0f R %+6.3f edge %+6.3f · against: n %5.0f R %+6.3f edge %+6.3f · against − with %+6.3f\n", set ? "BOOK2" : "every flag", h == 0 ? "all book days" : h == 1 ? "before 2024-10" : "2024-10 →",
      w.n, w.R(), w.e(), a.n, a.R(), a.e(), a.e() - w.e()); }
  return 0;
}
