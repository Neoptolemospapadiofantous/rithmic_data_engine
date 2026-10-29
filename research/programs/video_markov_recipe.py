"""video_markov_recipe.py — the video's bull/side/bear Markov method, walk-forward (preregistered 2026-10-24_video_markov_recipe.md).
usage: python3 video_markov_recipe.py CACHE_DIR FLOWBARS_SPOT_DIR FLOWBARS_PERP_DIR"""
import sys, glob, numpy as np, pandas as pd
CACHE, SPOT, PERP = sys.argv[1:4]; rng = np.random.default_rng(101)

def nq_daily():
    df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 4], header=0, names=["ts", "c"])
    t = pd.to_datetime(df.ts, unit="s"); m = t.dt.hour * 60 + t.dt.minute
    r = df[(m >= 570) & (m < 960)].copy(); r["d"] = pd.to_datetime(r.ts, unit="s").dt.normalize()
    g = r.groupby("d").c.agg(["last", "size"]); return g[g["size"] >= 300]["last"]
def btc_daily():
    fs = sorted(glob.glob(f"{SPOT}/BTCUSDT/*.csv")) + sorted(glob.glob(f"{PERP}/BTCUSDT/*.csv"))
    df = pd.concat([pd.read_csv(f, usecols=["minute", "close"]) for f in fs]).drop_duplicates("minute").sort_values("minute")
    d = pd.to_datetime(df.minute, unit="s").dt.normalize(); return df.groupby(d.values).close.last()

def run(close, cost, name, splits):
    ret = close.pct_change().values; c = close.values; n = len(c)
    r20 = close / close.shift(20) - 1; st = np.where(r20 >= 0.05, 0, np.where(r20 <= -0.05, 2, 1)); valid = r20.notna().values
    # walk-forward expanding transition counts → signal for the NEXT day
    cnt = np.ones((3, 3)); sig = np.zeros(n); mom = np.zeros(n)
    for i in range(n):
        if not valid[i]: continue
        if i > 0 and valid[i - 1]: cnt[st[i - 1], st[i]] += 1
        p = cnt[st[i]] / cnt[st[i]].sum(); sig[i] = p[0] - p[2]; mom[i] = np.sign(r20.iloc[i])
    def perf(pos, msk):
        pos = np.r_[0, pos[:-1]]                                    # position decided at close i applies to day i+1
        pnl = pos * np.nan_to_num(ret) - cost * np.abs(np.diff(np.r_[0, pos]))
        x = pnl[msk]; eq = np.cumsum(x); dd = (np.maximum.accumulate(eq) - eq).max() if len(eq) else 0
        return x.mean() * 252 if name == "NQ" else x.mean() * 365, (x.mean() / x.std() * np.sqrt(252 if name == "NQ" else 365)) if x.std() > 0 else 0, dd
    years = close.index.year.values
    # stickiness: overlapping daily states vs non-overlapping 20-day blocks
    m1 = np.zeros((3, 3)); m2 = np.zeros((3, 3)); vs = np.where(valid)[0]
    for a, b in zip(vs[:-1], vs[1:]): m1[st[a], st[b]] += 1
    blk = vs[::20]
    for a, b in zip(blk[:-1], blk[1:]): m2[st[a], st[b]] += 1
    d1 = np.diag(m1 / m1.sum(1, keepdims=True)); d2 = np.diag(m2 / m2.sum(1, keepdims=True))
    print(f"\n===== {name}: {close.index[0].date()} → {close.index[-1].date()}, {n} days, cost {cost*100:.2f} % per unit of position change")
    print(f"  stickiness (same state next step) bull/side/bear — overlapping daily states: {100*d1[0]:.0f}% / {100*d1[1]:.0f}% / {100*d1[2]:.0f}%   "
          f"non-overlapping 20-day blocks: {100*d2[0]:.0f}% / {100*d2[1]:.0f}% / {100*d2[2]:.0f}%")
    print(f"  corr(recipe signal, plain momentum sign) = {np.corrcoef(sig[valid], mom[valid])[0,1]:.2f}")
    print(f"  {'period':10s} {'strategy':16s} {'annual':>8s} {'Sharpe':>7s} {'max DD':>7s}")
    out = {}
    for lab, (y0, y1) in splits.items():
        msk = (years >= y0) & (years <= y1) & valid
        rows = {"buy & hold": perf(np.ones(n), msk), "20d momentum": perf(mom, msk), "VIDEO RECIPE": perf(sig, msk)}
        rs = [perf(rng.choice([-1, 1], n) * np.abs(sig), msk)[1] for _ in range(500)]; q95 = np.percentile(rs, 95)
        for k, (a, s, dd) in rows.items(): print(f"  {lab:10s} {k:16s} {100*a:+7.1f}% {s:7.2f} {100*dd:6.1f}%")
        print(f"  {lab:10s} random signs: Sharpe 95th percentile {q95:.2f}")
        out[lab] = (rows["VIDEO RECIPE"][1], rows["buy & hold"][1], rows["20d momentum"][1], q95)
    ok = all(v[0] > v[1] and v[0] > v[2] and v[0] > v[3] for v in out.values())
    print(f"  VERDICT {name}: {'PASS' if ok else 'FAIL'} (needs Sharpe above buy & hold, momentum and the random 95th pct in every period)")
run(nq_daily(), 0.0002, "NQ", {"2011-19": (2011, 2019), "2020-26": (2020, 2026)})
run(btc_daily(), 0.0010, "BTC", {"2018-21": (2018, 2021), "2022-26": (2022, 2026)})
