"""book2_hmm_regime.py — Gaussian hidden-Markov regime filter for BOOK2 NQ (preregistered 2026-10-24_book2_hmm_regime.md).
usage: python3 book2_hmm_regime.py W15_NQ.csv CACHE_DIR OUT_DIR
Fits on 2010-2019 daily RTH features only; the regime for day d is the PREDICTED state from observations up to d−1 (forward filter
× transition matrix), so nothing after the previous close is used. Writes OUT_DIR/hmm_states_K{K}.csv (date, predicted state, probs).
"""
import csv, sys, collections, datetime as dt, numpy as np, pandas as pd
W, CACHE, OUT = sys.argv[1:4]; rng = np.random.default_rng(71)

# ---------- daily RTH features from 1-minute bars (epoch = ET wall clock)
df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 1, 2, 3, 4], header=0, names=["ts", "o", "h", "l", "c"])
t = pd.to_datetime(df.ts, unit="s"); mins = t.dt.hour * 60 + t.dt.minute
rth = df[(mins >= 570) & (mins < 960)].copy(); rth["d"] = pd.to_datetime(rth.ts, unit="s").dt.strftime("%Y%m%d").astype(int)
D = rth.groupby("d").agg(o=("o", "first"), h=("h", "max"), l=("l", "min"), c=("c", "last"), n=("c", "size"))
D = D[D.n >= 300]                                      # full sessions only
rngd = D.h - D.l; atr = rngd.rolling(20, min_periods=10).mean().shift(1)
F = pd.DataFrame({"ret": np.log(D.c / D.c.shift(1)), "vol": np.log(rngd / atr), "eff": (D.c - D.o).abs() / rngd.replace(0, np.nan),
                  "gap": (D.o - D.c.shift(1)).abs() / atr}).dropna()
dates = F.index.values; X = F.values
fit_mask = dates < 20200101
mu, sd = X[fit_mask].mean(0), X[fit_mask].std(0); Z = (X - mu) / sd

def logpdf(Z, m, v):  # diagonal Gaussian, per state → (T, K)
    return -0.5 * (((Z[:, None, :] - m[None]) ** 2) / v[None] + np.log(2 * np.pi * v[None])).sum(-1)
def forward(Z, pi, A, m, v):
    lp = logpdf(Z, m, v); T, K = lp.shape; a = np.zeros((T, K)); c = np.zeros(T)
    for i in range(T):
        pr = pi if i == 0 else a[i - 1] @ A
        e = np.exp(lp[i] - lp[i].max()) * pr; s = e.sum(); a[i] = e / s; c[i] = np.log(s) + lp[i].max()
    return a, c.sum(), lp
def fit(Z, K, iters=200):
    T, P = Z.shape; best = None
    for _ in range(20):
        idx = rng.choice(T, K, replace=False); m = Z[idx].copy(); v = np.ones((K, P)); A = np.full((K, K), 0.1 / (K - 1)); np.fill_diagonal(A, 0.9); pi = np.full(K, 1 / K); ll_old = -np.inf
        for _ in range(iters):
            a, ll, lp = forward(Z, pi, A, m, v)
            b = np.ones((T, K)); el = np.exp(lp - lp.max(1, keepdims=True))
            for i in range(T - 2, -1, -1):
                x = A @ (el[i + 1] * b[i + 1]); b[i] = x / x.sum()
            g = a * b; g /= g.sum(1, keepdims=True)
            xi = np.zeros((K, K))
            for i in range(T - 1):
                x = a[i][:, None] * A * (el[i + 1] * b[i + 1])[None]; xi += x / x.sum()
            pi = g[0]; A = xi / xi.sum(1, keepdims=True)
            m = (g.T @ Z) / g.sum(0)[:, None]; v = np.maximum((g.T @ Z ** 2) / g.sum(0)[:, None] - m ** 2, 1e-3)
            if ll - ll_old < 1e-4: break
            ll_old = ll
        if best is None or ll > best[0]: best = (ll, pi, A, m, v)
    return best

trades = []
for r in csv.DictReader(open(W)):
    if r["ok"] == "1" and int(r["date"]) >= 20140101 and int(r["tf"]) >= 4 and float(r["x2"]) >= 3 and 5 <= float(r["x4"]) * 100 < 38 and r["grade"] in ("1", "2"):
        trades.append((int(r["date"]), float(r["r"])))
T_date = np.array([d for d, _ in trades]); T_R = np.array([x for _, x in trades])
print(f"daily observations {len(dates)} ({dates[0]} → {dates[-1]}), fit on {fit_mask.sum()} days before 2020; BOOK2 NQ trades {len(trades)}\n")
res = []
for K in (2, 3):
    ll, pi, A, m, v = fit(Z[fit_mask], K)
    a, _, _ = forward(Z, pi, A, m, v)                    # filtered with the fitted parameters on ALL days (forward only)
    pred = np.vstack([pi, a[:-1] @ A])                   # predicted state for day i from observations up to i−1
    state = pred.argmax(1); sdate = {d: (s, p) for d, s, p in zip(dates, state, pred)}
    pd.DataFrame({"date": dates, "state": state, **{f"p{k}": pred[:, k] for k in range(K)}}).to_csv(f"{OUT}/hmm_states_K{K}.csv", index=False)
    print(f"===== K = {K}  (log-likelihood {ll:.0f})")
    raw = m * sd + mu
    for k in range(K):
        share = np.mean(state == k); stay = A[k, k]
        print(f"  state {k}: ret {raw[k,0]*100:+.2f}%  range/ATR {np.exp(raw[k,1]):.2f}  efficiency {raw[k,2]:.2f}  gap/ATR {raw[k,3]:.2f} | {100*share:.0f}% of days, expected run {1/(1-stay):.1f} days")
    ts = np.array([sdate.get(d, (-1, None))[0] for d in T_date]); ok = ts >= 0
    e = (T_date < 20200101) & ok; l = (T_date >= 20200101) & ok
    print(f"  {'state':>6s} {'trades 14-19':>12s} {'mean R 14-19':>12s} {'trades 20-26':>12s} {'mean R 20-26':>12s} {'20-22':>7s} {'23-26':>7s}")
    for k in range(K):
        y = T_date
        print(f"  {k:6d} {np.sum(e&(ts==k)):12d} {T_R[e&(ts==k)].mean():12.3f} {np.sum(l&(ts==k)):12d} {T_R[l&(ts==k)].mean():12.3f} {T_R[l&(ts==k)&(y<20230101)].mean():7.3f} {T_R[l&(ts==k)&(y>=20230101)].mean():7.3f}")
    worst = min(range(K), key=lambda k: T_R[e & (ts == k)].mean() if np.any(e & (ts == k)) else 9)
    bad = ts == worst
    days = np.unique(T_date[l]); by = collections.defaultdict(list)
    for i in np.where(l)[0]: by[T_date[i]].append(i)
    diffs = []
    for _ in range(3000):
        p = np.concatenate([by[d] for d in rng.choice(days, len(days))]); b_ = bad[p]
        if b_.any() and (~b_).any(): diffs.append(T_R[p][~b_].mean() - T_R[p][b_].mean())
    diffs = np.array(diffs); lo = np.percentile(diffs, 5); pv = np.mean(diffs <= 0)
    y = T_date; d1 = T_R[l & ~bad & (y < 20230101)].mean() - T_R[l & bad & (y < 20230101)].mean(); d2 = T_R[l & ~bad & (y >= 20230101)].mean() - T_R[l & bad & (y >= 20230101)].mean()
    diff = T_R[l & ~bad].mean() - T_R[l & bad].mean()
    print(f"  worst state on 2014-19 = {worst}; 2020-26: others − worst = {diff:+.3f} R (90% low {lo:+.3f}, p {pv:.3f}) | 2020-22 {d1:+.3f} | 2023-26 {d2:+.3f}\n")
    res.append((K, worst, diff, lo, pv, d1, d2))
order = sorted(range(len(res)), key=lambda i: res[i][4]); run = 0
for rank, i in enumerate(order):
    run = max(run, min(1, (len(res) - rank) * res[i][4])); K, w, diff, lo, pv, d1, d2 = res[i]
    print(f"K={K}: Holm {run:.3f} -> {'PASS' if lo > 0 and run < 0.10 and d1 > 0 and d2 > 0 else 'fail'}")
