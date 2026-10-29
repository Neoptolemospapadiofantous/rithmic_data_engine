"""book2_partial_matrix.py — BOOK2 NQ partial-profit matrix (preregistered 2026-10-24_book2_high_winrate.md, Part 4).
usage: python3 book2_partial_matrix.py W15_NQ.csv CACHE_DIR OUTDIR  → trade stats per (level, fraction) + one export each."""
import csv, sys, datetime as dt, numpy as np, pandas as pd
W, CACHE, OUT = sys.argv[1:4]; COST = 1.5
LEVELS = (0.3, 0.5, 0.7, 1.0, 1.5); FRACS = (1/3, 1/2, 2/3)
def base_epoch(date):
    x = dt.date(date // 10000, date // 100 % 100, date % 100) - dt.timedelta(days=1)
    return int(dt.datetime(x.year, x.month, x.day, 18, 0, tzinfo=dt.timezone.utc).timestamp()) // 60
df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 1, 2, 3, 4], header=0, names=["ts", "o", "h", "l", "c"])
df = df[df.ts >= int(dt.datetime(2013, 12, 20, tzinfo=dt.timezone.utc).timestamp())]
M = (df.ts.values // 60).astype(np.int64); Hh, Ll, Cc = df.h.values, df.l.values, df.c.values
rows = list(csv.reader(open(W))); hdr = rows[0]; X = {k: i for i, k in enumerate(hdr)}
def isb2(r): return r[X["ok"]] == "1" and int(r[0]) >= 20140101 and int(r[X["tf"]]) >= 4 and float(r[X["x2"]]) >= 3 and 5 <= float(r[X["x4"]]) * 100 < 38 and r[X["grade"]] in ("1", "2")
def sim(d, e, s, t, i0, iflat, L, f):
    """L = partial level in R (None = base), f = fraction taken there; rest: stop to break-even, run to target. Stop first on ties."""
    risk = abs(e - s); stop = s; done = False; legs = []; rem = 1.0
    for k in range(i0, iflat + 1):
        adverse = Ll[k] if d > 0 else Hh[k]; fav = Hh[k] if d > 0 else Ll[k]
        if d * (adverse - stop) <= 0: legs.append((rem, stop)); return legs, k
        if L is not None and not done and d * (fav - (e + d * L * risk)) >= 0:
            legs.append((f, e + d * L * risk)); rem = 1 - f; done = True; stop = e + d * COST * (1 - 0)  # cover the cost on the rest
        if d * (fav - t) >= 0: legs.append((rem, t)); return legs, k
    legs.append((rem, Cc[iflat])); return legs, iflat
CELLS = [("base", None, 1.0)] + [(f"L{L}_f{f:.2f}", L, f) for L in LEVELS for f in FRACS]
stats = {c[0]: [] for c in CELLS}; outs = {c[0]: [hdr] for c in CELLS}
for r in rows[1:]:
    ok = isb2(r)
    if ok:
        dte = int(r[0]); d = int(r[X["side"]]); e, s, t = float(r[X["entry"]]), float(r[X["stop"]]), float(r[X["target"]])
        b0 = base_epoch(dte); i0 = np.searchsorted(M, b0 + int(r[X["entry_min"]])); iflat = np.searchsorted(M, b0 + 1315)
        ok = iflat < len(M) and i0 < iflat
    for name, L, f in CELLS:
        if not ok: outs[name].append(r); continue
        legs, k = sim(d, e, s, t, i0, iflat, L, f)
        pts = sum(w * d * (p - e) for w, p in legs) - COST; R = pts / abs(e - s); px = e + d * (pts + COST)
        stats[name].append((dte, R)); rr = list(r); rr[X["exit"]] = f"{px:.2f}"; rr[X["r"]] = f"{R:.4f}"; rr[X["pts"]] = f"{pts:.4f}"
        rr[X["exit_min"]] = str(int(M[k] - b0)); outs[name].append(rr)
print(f"{'cell':12s} {'win %':>6s} {'mean R':>7s} {'PF':>5s} {'14-19 R':>8s} {'20-26 R':>8s}")
for name, _, _ in CELLS:
    x = np.array([R for _, R in stats[name]]); y = np.array([a for a, _ in stats[name]]); e_ = y < 20200101
    print(f"{name:12s} {100*np.mean(x>0.05):6.1f} {x.mean():7.3f} {x[x>0].sum()/-x[x<0].sum():5.2f} {x[e_].mean():8.3f} {x[~e_].mean():8.3f}")
    with open(f"{OUT}/mx_{name}.csv", "w", newline="") as fo: csv.writer(fo).writerows(outs[name])
