"""video_markov_recipe_v2.py — re-evaluation of the video's Markov recipe: realistic costs, a parameter grid chosen on the first
period and read once on the second, vol-matched comparison (preregistered 2026-10-24_video_markov_recipe.md, Re-evaluation).
usage: python3 video_markov_recipe_v2.py CACHE_DIR FLOWBARS_SPOT_DIR FLOWBARS_PERP_DIR"""
import sys, glob, itertools, numpy as np, pandas as pd
CACHE, SPOT, PERP = sys.argv[1:4]; rng = np.random.default_rng(111)
def nq_daily():
    df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 4], header=0, names=["ts", "c"])
    t = pd.to_datetime(df.ts, unit="s"); m = t.dt.hour * 60 + t.dt.minute
    r = df[(m >= 570) & (m < 960)].copy(); r["d"] = pd.to_datetime(r.ts, unit="s").dt.normalize()
    g = r.groupby("d").c.agg(["last", "size"]); return g[g["size"] >= 300]["last"]
def btc_daily():
    fs = sorted(glob.glob(f"{SPOT}/BTCUSDT/*.csv")) + sorted(glob.glob(f"{PERP}/BTCUSDT/*.csv"))
    df = pd.concat([pd.read_csv(f, usecols=["minute", "close"]) for f in fs]).drop_duplicates("minute").sort_values("minute")
    d = pd.to_datetime(df.minute, unit="s").dt.normalize(); return df.groupby(d.values).close.last()
def signals(close, L, thr):
    n = len(close); rL = close / close.shift(L) - 1; st = np.where(rL >= thr, 0, np.where(rL <= -thr, 2, 1)); valid = rL.notna().values
    cnt = np.ones((3, 3)); sig = np.zeros(n); mom = np.zeros(n)
    for i in range(n):
        if not valid[i]: continue
        if i > 0 and valid[i - 1]: cnt[st[i - 1], st[i]] += 1
        p = cnt[st[i]] / cnt[st[i]].sum(); sig[i] = p[0] - p[2]; mom[i] = np.sign(rL.iloc[i])
    return sig, mom, valid
def perf(pos, ret, msk, cost, ann):
    pos = np.r_[0, pos[:-1]]; pnl = pos * np.nan_to_num(ret) - cost * np.abs(np.diff(np.r_[0, pos])); x = pnl[msk]
    sd = x.std(); return x.mean() * ann, (x.mean() / sd * np.sqrt(ann)) if sd > 0 else 0.0, sd * np.sqrt(ann)
def run(close, name, costs, p1, p2, ann):
    ret = close.pct_change().values; years = close.index.year.values; n = len(close)
    cache = {}
    for L, thr in itertools.product((10, 20, 60), (0.03, 0.05, 0.10)): cache[(L, thr)] = signals(close, L, thr)
    for cost in costs:
        print(f"\n===== {name}, cost {cost*100:.3f} % per unit of position change")
        rows = []
        for (L, thr), (sig, mom, valid) in cache.items():
            for mode in ("long-short", "long-only", "sign-only"):
                pos = sig if mode == "long-short" else np.maximum(sig, 0) if mode == "long-only" else np.sign(sig)
                m1 = (years >= p1[0]) & (years <= p1[1]) & valid; m2 = (years >= p2[0]) & (years <= p2[1]) & valid
                rows.append((L, thr, mode, perf(pos, ret, m1, cost, ann), perf(pos, ret, m2, cost, ann), pos, mom, valid))
        best = max(rows, key=lambda r: r[3][1]); L, thr, mode, s1, s2, pos, mom, valid = best
        m2 = (years >= p2[0]) & (years <= p2[1]) & valid
        bh = perf(np.ones(n), ret, m2, cost, ann); mo = perf(mom, ret, m2, cost, ann)
        q95 = np.percentile([perf(rng.choice([-1, 1], n) * np.abs(pos), ret, m2, cost, ann)[1] for _ in range(500)], 95)
        print(f"  chosen on {p1[0]}-{p1[1]}: lookback {L} d, threshold ±{int(thr*100)} %, {mode}  (first-period Sharpe {s1[1]:.2f}, annual {100*s1[0]:+.1f} %)")
        print(f"  {p2[0]}-{p2[1]} read once:  {'strategy':14s} {'annual':>8s} {'Sharpe':>7s} {'vol':>6s}")
        for k, v in (("VIDEO RECIPE", s2), ("buy & hold", bh), (f"{L}d momentum", mo)): print(f"  {'':22s} {k:14s} {100*v[0]:+7.1f}% {v[1]:7.2f} {100*v[2]:5.1f}%")
        print(f"  {'':22s} random signs 95th-pct Sharpe {q95:.2f};  recipe scaled to buy & hold's vol: {100*s2[0]*(bh[2]/s2[2]) if s2[2] > 0 else 0:+.1f} %/yr vs buy & hold {100*bh[0]:+.1f} %/yr")
        ok = s2[1] > bh[1] and s2[1] > mo[1] and s2[1] > q95
        print(f"  VERDICT: {'PASS' if ok else 'FAIL'}")
        top = sorted(rows, key=lambda r: -r[4][1])[:3]
        print("  (post-hoc, best three on the SECOND period: " + "; ".join(f"L{r[0]} ±{int(r[1]*100)}% {r[2]} Sharpe {r[4][1]:.2f}" for r in top) + ")")
run(nq_daily(), "NQ", (0.00005, 0.0), (2011, 2019), (2020, 2026), 252)
run(btc_daily(), "BTC", (0.0004, 0.0010), (2018, 2021), (2022, 2026), 365)
