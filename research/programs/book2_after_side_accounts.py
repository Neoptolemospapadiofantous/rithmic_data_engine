# writes w15_{nq,es}_{mode}.csv: mode skip|size, rule=side|random(seed); "side" = previous RTH day |close-open| < 0.3 × ATR20
import csv, sys, random, numpy as np, pandas as pd
W, CACHE, OUT, mode, rule = sys.argv[1:6]; seed = int(sys.argv[6]) if len(sys.argv) > 6 else 0; random.seed(seed)
def prev_side(sym):
    df = pd.read_csv(f"{CACHE}/{sym}.csv", usecols=[0,1,2,3,4], header=0, names=["ts","o","h","l","c"])
    t = pd.to_datetime(df.ts, unit="s"); m = t.dt.hour*60 + t.dt.minute
    r = df[(m >= 570) & (m < 960)].copy(); r["d"] = pd.to_datetime(r.ts, unit="s").dt.strftime("%Y%m%d").astype(int)
    D = r.groupby("d").agg(o=("o","first"), h=("h","max"), l=("l","min"), c=("c","last"), n=("c","size")); D = D[D.n >= 300]
    atr = (D.h-D.l).rolling(20, min_periods=10).mean().shift(1); side = ((D.c-D.o).abs() < 0.3*atr) & atr.notna()
    return dict(zip(D.index.values[1:], side.values[:-1]))
for sym in ("nq", "es"):
    ps = prev_side(sym.upper()); rows = list(csv.reader(open(f"{W}/w15_{sym}.csv"))); h = rows[0]; X = {k: i for i, k in enumerate(h)}
    isb2 = lambda r: r[X["ok"]] == "1" and int(r[0]) >= 20140101 and int(r[X["tf"]]) >= 4 and float(r[X["x2"]]) >= 3 and 5 <= float(r[X["x4"]])*100 < 38 and r[X["grade"]] in ("1","2")
    share = np.mean([bool(ps.get(int(r[0]), False)) for r in rows[1:] if isb2(r)])
    with open(f"{OUT}/w15_{sym}_{mode}.csv", "w", newline="") as fo:
        w = csv.writer(fo); w.writerow(h + ["szm"])
        for r in rows[1:]:
            good = bool(ps.get(int(r[0]), False)) if rule == "side" else (random.random() < share)
            rr = list(r)
            if mode == "skip" and isb2(r) and not good: rr[X["ok"]] = "0"
            w.writerow(rr + [("2" if good else "1") if mode == "size" else "1"])
