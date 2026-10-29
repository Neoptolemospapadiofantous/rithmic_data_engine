"""book2_markov_matrix.py — visible bull/side/bear Markov chain on NQ (daily and weekly) and its use for BOOK2 flags
(preregistered 2026-10-24_book2_hmm_regime.md Part 2). usage: python3 book2_markov_matrix.py W15_NQ.csv CACHE_DIR"""
import csv, sys, collections, numpy as np, pandas as pd
from scipy.stats import chi2_contingency
W, CACHE = sys.argv[1:3]; rng = np.random.default_rng(81); S = ["bull", "side", "bear"]
df = pd.read_csv(f"{CACHE}/NQ.csv", usecols=[0, 1, 2, 3, 4], header=0, names=["ts", "o", "h", "l", "c"])
t = pd.to_datetime(df.ts, unit="s"); mins = t.dt.hour * 60 + t.dt.minute
rth = df[(mins >= 570) & (mins < 960)].copy(); rth["d"] = pd.to_datetime(rth.ts, unit="s").dt.strftime("%Y%m%d").astype(int)
D = rth.groupby("d").agg(o=("o", "first"), h=("h", "max"), l=("l", "min"), c=("c", "last"), n=("c", "size")); D = D[D.n >= 300]
atr = (D.h - D.l).rolling(20, min_periods=10).mean().shift(1)
mv = (D.c - D.o) / atr
D["st"] = np.where(mv >= 0.3, 0, np.where(mv <= -0.3, 2, 1)); D = D[atr.notna()]
dd = pd.to_datetime(D.index.astype(str)); D["wk"] = dd.to_period("W").astype(str)
Wk = D.groupby("wk").agg(o=("o", "first"), h=("h", "max"), l=("l", "min"), c=("c", "last"), last=("o", lambda x: x.index[-1]))
watr = (Wk.h - Wk.l).rolling(20, min_periods=10).mean().shift(1); wmv = (Wk.c - Wk.o) / watr
Wk["st"] = np.where(wmv >= 0.3, 0, np.where(wmv <= -0.3, 2, 1)); Wk = Wk[watr.notna()]
def matrix(st, years, label):
    m = np.zeros((3, 3), int)
    for a, b in zip(st[:-1], st[1:]): m[a, b] += 1
    p = m / m.sum(1, keepdims=True); chi = chi2_contingency(m); col = m.sum(0) / m.sum()
    print(f"  {label} {years}: (rows = this period, columns = next; % with counts)    chi-square p = {chi.pvalue:.2g}")
    for i in range(3): print("    " + f"{S[i]:>5s} → " + "  ".join(f"{S[j]} {100*p[i,j]:5.1f}% (n={m[i,j]:4d})" for j in range(3)))
    print("    if periods were independent the columns would be: " + "  ".join(f"{S[j]} {100*col[j]:.1f}%" for j in range(3)))
for label, X, key in (("DAILY", D, D.index.values), ("WEEKLY", Wk, Wk["last"].values)):
    print(f"===== {label} states (bull ≥ +0.3 × average range, bear ≤ −0.3 ×, else side)")
    for a, b in ((20100101, 20191231), (20200101, 20261231)):
        msk = (key >= a) & (key <= b); matrix(X.st.values[msk], f"{a//10000}-{b//10000}", label)
    print()
# flags: previous period's state
prev_day = dict(zip(D.index.values[1:], D.st.values[:-1]))
wk_of = dict(zip(D.index.values, D.wk.values)); wk_list = list(Wk.index); prev_wk = {w: Wk.st.iloc[i - 1] for i, w in enumerate(wk_list) if i > 0}
T = []
for r in csv.DictReader(open(W)):
    if r["ok"] == "1" and int(r["date"]) >= 20140101 and int(r["tf"]) >= 4 and float(r["x2"]) >= 3 and 5 <= float(r["x4"]) * 100 < 38 and r["grade"] in ("1", "2"):
        d = int(r["date"]); side = int(r["side"]); R = float(r["r"])
        def rel(s): return None if s is None else ("with" if (s == 0 and side > 0) or (s == 2 and side < 0) else "against" if (s == 0 and side < 0) or (s == 2 and side > 0) else "side")
        T.append((d, R, rel(prev_day.get(d)), rel(prev_wk.get(wk_of.get(d)))))
T = pd.DataFrame(T, columns=["date", "R", "day", "week"])
for col in ("day", "week"):
    x = T.dropna(subset=[col]); late = x.date >= 20200101
    print(f"===== BOOK2 NQ flags vs the PREVIOUS {col}'s state")
    print(x.assign(per=np.where(late, "2020-26", "2014-19")).pivot_table(index=col, columns="per", values="R", aggfunc=["mean", "size"]).round(3).to_string())
    y = x[late]; days = y.date.unique(); by = collections.defaultdict(list)
    for i, d in zip(y.index, y.date): by[d].append(i)
    diffs = []
    for _ in range(3000):
        p = np.concatenate([by[d] for d in rng.choice(days, len(days))]); z = x.loc[p]
        a, b = z.R[z[col] == "with"], z.R[z[col] == "against"]
        if len(a) and len(b): diffs.append(a.mean() - b.mean())
    diffs = np.array(diffs); h1 = y[y.date < 20230101]; h2 = y[y.date >= 20230101]
    d1 = h1.R[h1[col] == "with"].mean() - h1.R[h1[col] == "against"].mean(); d2 = h2.R[h2[col] == "with"].mean() - h2.R[h2[col] == "against"].mean()
    dd_ = y.R[y[col] == "with"].mean() - y.R[y[col] == "against"].mean(); lo = np.percentile(diffs, 5)
    print(f"  2020-26 WITH − AGAINST = {dd_:+.3f} R (90% low {lo:+.3f}) | 2020-22 {d1:+.3f} | 2023-26 {d2:+.3f} -> {'PASS' if lo > 0 and d1 > 0 and d2 > 0 else 'fail'}\n")
