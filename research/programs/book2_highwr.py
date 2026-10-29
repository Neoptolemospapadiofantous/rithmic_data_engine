"""book2_highwr.py — BOOK2 NQ trade management for a higher win rate (preregistered 2026-10-24_book2_high_winrate.md).
usage: python3 book2_highwr.py W15_NQ.csv CACHE_DIR OUTDIR   → stats + one export per variant (exit/r/pts/exit_min rewritten)."""
import csv, sys, datetime as dt, numpy as np, pandas as pd
W, CACHE, OUT = sys.argv[1:4]; COST = 1.5
def base_epoch(date):
    x = dt.date(date // 10000, date // 100 % 100, date % 100) - dt.timedelta(days=1)
    return int(dt.datetime(x.year, x.month, x.day, 18, 0, tzinfo=dt.timezone.utc).timestamp()) // 60
df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 1, 2, 3, 4], header=0, names=["ts", "o", "h", "l", "c"])
df = df[df.ts >= int(dt.datetime(2013, 12, 20, tzinfo=dt.timezone.utc).timestamp())]
M = (df.ts.values // 60).astype(np.int64); Hh, Ll, Cc = df.h.values, df.l.values, df.c.values
rows = list(csv.reader(open(W))); hdr = rows[0]; X = {k: i for i, k in enumerate(hdr)}
def isb2(r): return r[X["ok"]] == "1" and int(r[0]) >= 20140101 and int(r[X["tf"]]) >= 4 and float(r[X["x2"]]) >= 3 and 5 <= float(r[X["x4"]]) * 100 < 38 and r[X["grade"]] in ("1", "2")
VARS = ["V0_base", "V1_BE1", "V2_PARTIAL1", "V3_PARTIAL1_2R", "V4_TRAIL1", "V5_BE05", "V6_PARTIAL05"]
def sim(d, e, s, t, i0, iflat, v):
    """returns (R, exit_index, weighted exit price). Same-bar stop and target → stop first."""
    risk = abs(e - s); stop = s; half_done = False; best = e; legs = []    # legs: (weight, price)
    tgt = t if v != "V3_PARTIAL1_2R" else e + d * 2 * risk
    for k in range(i0, iflat + 1):
        hi, lo = Hh[k], Ll[k]
        adverse = lo if d > 0 else hi; fav = hi if d > 0 else lo
        if d * (adverse - stop) <= 0:                      # stop (or BE / trail stop) first
            w = 0.5 if half_done else 1.0; legs.append((w, stop)); break
        plv = 0.5 if v == "V6_PARTIAL05" else 1.0
        if v in ("V2_PARTIAL1", "V3_PARTIAL1_2R", "V6_PARTIAL05") and not half_done and d * (fav - (e + d * plv * risk)) >= 0:
            legs.append((0.5, e + d * plv * risk)); half_done = True; stop = e + d * COST / 2   # remaining half to break-even
        if d * (fav - tgt) >= 0:
            w = 0.5 if half_done else 1.0; legs.append((w, tgt)); break
        best = max(best, fav) if d > 0 else min(best, fav)
        if v == "V1_BE1" and d * (best - (e + d * risk)) >= 0: stop = e + d * COST if d * (stop - e) < 0 else stop
        if v == "V5_BE05" and d * (best - (e + d * 0.5 * risk)) >= 0: stop = e + d * COST if d * (stop - e) < 0 else stop
        if v == "V4_TRAIL1" and d * (best - (e + d * risk)) >= 0: stop = best - d * risk if d * ((best - d * risk) - stop) > 0 else stop
    else:
        w = 0.5 if half_done else 1.0; legs.append((w, Cc[iflat])); k = iflat
    px = sum(w * p for w, p in legs) / sum(w for w, _ in legs)
    gross = sum(w * d * (p - e) for w, p in legs)          # weights sum to 1
    return (gross - COST) / risk, k, px, gross - COST
res = {v: [] for v in VARS}; out_rows = {v: [hdr] for v in VARS}; rep = []
for r in rows[1:]:
    if not isb2(r):
        for v in VARS: out_rows[v].append(r)
        continue
    dte = int(r[0]); d = int(r[X["side"]]); e, s, t = float(r[X["entry"]]), float(r[X["stop"]]), float(r[X["target"]])
    b0 = base_epoch(dte); i0 = np.searchsorted(M, b0 + int(r[X["entry_min"]])); iflat = np.searchsorted(M, b0 + 1315)
    if iflat >= len(M) or i0 >= iflat:
        for v in VARS: out_rows[v].append(r)
        continue
    for v in VARS:
        R, k, px, pts = sim(d, e, s, t, i0, iflat, v)
        res[v].append((dte, R))
        rr = list(r); rr[X["exit"]] = f"{px:.2f}"; rr[X["r"]] = f"{R:.4f}"; rr[X["pts"]] = f"{pts:.4f}"
        rr[X["exit_min"]] = str(int(M[k] - b0)); out_rows[v].append(rr)
    rep.append((float(r[X["r"]]), res["V0_base"][-1][1]))
a = np.array(rep); print(f"BOOK2 NQ trades {len(a)} | base replication: mean R export {a[:,0].mean():.3f} vs bar sim {a[:,1].mean():.3f}, corr {np.corrcoef(a[:,0],a[:,1])[0,1]:.3f}, same win/loss {100*np.mean((a[:,0]>0)==(a[:,1]>0)):.1f}%\n")
print(f"{'variant':16s} {'win %':>6s} {'scratch %':>9s} {'mean R':>7s} {'PF':>5s} {'14-19 R':>8s} {'20-26 R':>8s} {'14-19 win':>9s} {'20-26 win':>9s}")
for v in VARS:
    x = np.array([R for _, R in res[v]]); y = np.array([dte for dte, _ in res[v]]); e_ = y < 20200101
    pf = x[x > 0].sum() / -x[x < 0].sum(); sc = np.mean(np.abs(x) < 0.05)
    print(f"{v:16s} {100*np.mean(x>0.05):6.1f} {100*sc:9.1f} {x.mean():7.3f} {pf:5.2f} {x[e_].mean():8.3f} {x[~e_].mean():8.3f} {100*np.mean(x[e_]>0.05):9.1f} {100*np.mean(x[~e_]>0.05):9.1f}")
    with open(f"{OUT}/w15_nq_{v}.csv", "w", newline="") as f: csv.writer(f).writerows(out_rows[v])
