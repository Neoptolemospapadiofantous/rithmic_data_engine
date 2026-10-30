#!/usr/bin/env python3
# 2026-10-29 ema79_daily_stop.py (founder: "we dont have to take trades ALL the time. once we get some money … we trade next day").
# Reads the per-trade CSV from `DUMP=<f> ema79_vwap_sd NQ|ES` (cond,exit,window,date,tday_min,net,control) and applies DAILY rules
# fixed before the run: take the cell's trades in time order each trading day (18:00 → 17:00) and STOP for the day when
#   G   day net >= +G pts (G = 3, 5, 10, 20)         · W1 after the first winning trade
#   L   also stop when day net <= -L pts (L = none, 10, 20)  · N max N trades a day (1, 2, 3)
# The same rule runs on each trade's random-other-day control, so "rule beats control" means the rule, not the market, made it.
# PASS: net/day > 0 in 2010-15, 2016-20, 2021-24 AND 2025-26, beats its control in total, ≥ 100 trading days, same rule+cell > 0 on ES.
import csv, sys, collections
COND = {1: "B1 beyond 1SD", 2: "B2 beyond 2SD", 4: "T2 2SD touched"}
EXIT = ["X1 VWAP", "X2 1R", "X3 2R", "X4 opp cross"]
WIN = ["all", "Asia", "London", "NYopen", "NYmid", "NYpm"]
def era(d): y = d // 10000; return 0 if y <= 2015 else 1 if y <= 2020 else 2 if y <= 2024 else 3
def load(f):
    cells = collections.defaultdict(lambda: collections.defaultdict(list))
    for r in csv.reader(open(f)):
        cc, x, w, d, tm = map(int, r[:5]); cells[(cc, x, w)][d].append((tm, float(r[5]), float(r[6])))
    return cells
RULES = [("none", None, None, None, False)] + [(f"G{g}{'/L'+str(l) if l else ''}", g, l, None, False) for g in (3, 5, 10, 20) for l in (None, 10, 20)] \
      + [("W1", None, None, None, True), ("W1/L10", None, 10, None, True)] + [(f"N{n}", None, None, n, False) for n in (1, 2, 3)]
def run(days, rule, col):
    _, G, L, N, W1 = rule; out = collections.defaultdict(list); trades = 0
    for d, ts in days.items():
        s = 0.0; k = 0
        for tm, net, ctl in sorted(ts, key=lambda t: t[0] if t[0] >= 0 else t[0]):
            v = net if col == 0 else ctl; s += v; k += 1; trades += 1
            if (G is not None and s >= G) or (L is not None and s <= -L) or (N is not None and k >= N) or (W1 and v > 0): break
        out[era(d)].append(s)
    return out, trades
def summ(out):
    allv = [v for e in out.values() for v in e]
    return sum(allv) / len(allv), [sum(out[e]) / len(out[e]) if out.get(e) else None for e in range(4)], 100 * sum(v > 0 for v in allv) / len(allv), min(allv), len(allv)
nq, es = load(sys.argv[1]), load(sys.argv[2])
rows = []
for key, days in nq.items():
    for rule in RULES:
        o, t = run(days, rule, 0); c, _ = run(days, rule, 1)
        m, eras, green, worst, nd = summ(o); mc = summ(c)[0]
        esm = summ(run(es[key], rule, 0)[0])[0] if key in es and es[key] else None
        rows.append((key, rule[0], m, eras, green, worst, nd, t, mc, esm))
P = [r for r in rows if r[6] >= 100 and all(e is not None and e > 0 for e in r[3]) and r[2] > r[8] and (r[9] or -1) > 0]
print(f"{len(rows)} cell×rule combos (NQ points per DAY, $20/pt for 1 NQ, $2 for 1 MNQ). PASS: {len(P)}")
for r in P: print("  PASS", r)
base = {r[0]: r for r in rows if r[1] == "none"}
print("\nDoes a daily rule help? avg over all 72 cells, NQ pts/day:  rule  |  strategy  control  |  % green days  |  worst day")
for rule in RULES:
    rs = [r for r in rows if r[1] == rule[0]]
    print(f"  {rule[0]:8s} {sum(r[2] for r in rs)/len(rs):+6.2f} {sum(r[8] for r in rs)/len(rs):+6.2f}   {sum(r[4] for r in rs)/len(rs):5.1f}%   {sum(r[5] for r in rs)/len(rs):7.1f}")
print("\nBest 10 by net/day (≥100 days): cell | rule | net/day | 2010-15 2016-20 2021-24 2025-26 | green% | worst | days | control | ES")
for r in sorted([r for r in rows if r[6] >= 100], key=lambda r: -r[2])[:10]:
    (cc, x, w) = r[0]; e = " ".join(f"{v:+6.2f}" if v is not None else "   —  " for v in r[3])
    print(f"  {COND[cc]:15s} {EXIT[x]:12s} {WIN[w]:7s} {r[1]:8s} {r[2]:+6.2f} | {e} | {r[4]:4.0f}% {r[5]:7.1f} {r[6]:5d} | ctl {r[8]:+6.2f} | ES {r[9] if r[9] is None else round(r[9],2)}")
