"""btc_flow_strategies.py — standalone order-flow strategies on Binance 1-minute flow bars (2026-10-24).
Pre-registered: research/preregistered/2026-10-24_btc_orderflow_strategies.md.
usage: python3 btc_flow_strategies.py FLOWBARS_DIR SYMBOL [--chosen chosen.json]   (FLOWBARS_DIR/SYMBOL/*.csv from flow_bars.cpp)
Entry at the next bar's open, exit at the close H bars later, one position at a time, cost per round trip as a share of price.
"""
import sys, glob, json, math, itertools, datetime as dt
import numpy as np, pandas as pd
from numba import njit
from zoneinfo import ZoneInfo

D, SYM = sys.argv[1], sys.argv[2]
CHOSEN = sys.argv[sys.argv.index("--chosen") + 1] if "--chosen" in sys.argv else None
TAKER, MAKER = 0.0010, 0.0004
rng = np.random.default_rng(61)

# ---------- bars on a complete minute grid
df = pd.concat([pd.read_csv(f) for f in sorted(glob.glob(f"{D}/{SYM}/*.csv"))]).drop_duplicates("minute").sort_values("minute")
grid = np.arange(df.minute.iloc[0], df.minute.iloc[-1] + 60, 60)
df = df.set_index("minute").reindex(grid)
gap = df.close.isna().values
for c in ("close",): df[c] = df[c].ffill()
for c in ("open", "high", "low"): df[c] = df[c].fillna(df.close)
for c in ("vol", "buy_vol", "trades", "big_buy", "big_sell", "max_notional"): df[c] = df[c].fillna(0)
O, H, L, C = (df[c].values for c in ("open", "high", "low", "close"))
V, BV = df.vol.values, df.buy_vol.values
NB = len(df); T = grid
DELTA = 2 * BV - V
with np.errstate(invalid="ignore", divide="ignore"): DR = np.where(V > 0, DELTA / V, 0.0)
s = pd.Series
VMED = s(V).shift(1).rolling(60, min_periods=30).median().values
ATR = s(H - L).shift(1).rolling(60, min_periods=30).mean().values
CVD60 = s(DELTA).rolling(60).sum().values; CVD30 = s(DELTA).rolling(30).sum().values
CVD240 = s(DELTA).rolling(240).sum().values; RET240 = C - s(C).shift(240).values
WHALE15 = s((df.big_buy.values - df.big_sell.values) * C).rolling(15).sum().values
day = 1440
def trailing_q(x, q, win=30 * day, step=60):
    """trailing quantile over the previous `win` bars, evaluated every `step` bars and held (past data only)."""
    xs = s(x); out = np.full(len(x), np.nan)
    idx = np.arange(win, len(x), step)
    vals = xs.rolling(win, min_periods=win // 2).quantile(q).shift(1).values
    out[idx] = vals[idx]; return s(out).ffill().values
WH_HI = {p: trailing_q(WHALE15, 1 - p) for p in (0.02, 0.05)}; WH_LO = {p: trailing_q(WHALE15, p) for p in (0.02, 0.05)}
ABS240 = np.abs(CVD240); CVD_Q80 = trailing_q(ABS240, 0.80)
CLV = np.where(H > L, (C - L) / np.where(H > L, H - L, 1), 0.5)
MOVE5 = C - s(C).shift(5).values; DR5 = s(DELTA).rolling(5).sum().values / np.maximum(s(V).rolling(5).sum().values, 1e-9)
year = pd.to_datetime(T, unit="s").year.values; hour = pd.to_datetime(T, unit="s").hour.values
gapnear = s(gap.astype(float)).rolling(300, min_periods=1).max().values > 0   # no signals within 5 h after missing data

@njit(cache=True)
def divergence(C, CVD, n):
    """+1: close is the n-bar low while CVD is above its value at the previous n-bar low (bullish); −1 mirrored."""
    out = np.zeros(len(C)); last_hi = -1; last_lo = -1
    for i in range(n, len(C)):
        w = C[i - n + 1:i + 1]
        if C[i] >= w.max():
            if last_hi >= 0 and CVD[i] < CVD[last_hi]: out[i] = -1
            last_hi = i
        elif C[i] <= w.min():
            if last_lo >= 0 and CVD[i] > CVD[last_lo]: out[i] = 1
            last_lo = i
    return out

@njit(cache=True)
def run(sig, O, C, h, start, end):
    """one position at a time; entry i+1 open, exit close at i+h; returns entry indices and gross returns."""
    ent = []; ret = []; i = start; n = len(C)
    while i < end:
        if sig[i] != 0 and i + h < n and i + h < end + h:
            e = O[i + 1]; x = C[i + h]
            ent.append(i + 1); ret.append(sig[i] * (x - e) / e); i = i + h
        else: i += 1
    return np.array(ent), np.array(ret)

# ---------- signals (+1 long / −1 short / 0)
def sig_burst(x, k, fade):
    t = (np.abs(DR) >= x) & (V >= k * VMED); g = np.sign(DR) * (-1 if fade else 1); return np.where(t, g, 0)
def sig_div(n): return divergence(C, CVD30 if n == 30 else CVD60, n)
def sig_abs(y):
    hv = V >= 3 * VMED; return np.where(hv & (DR <= -y) & (CLV >= 0.6), 1, np.where(hv & (DR >= y) & (CLV <= 0.4), -1, 0))
def sig_whale(p): return np.where(WHALE15 >= WH_HI[p], 1, np.where(WHALE15 <= WH_LO[p], -1, 0)) * (np.abs(WHALE15) > 0)
def sig_cascade(m, follow):
    up = (MOVE5 >= m * ATR) & (DR5 >= 0.3); dn = (MOVE5 <= -m * ATR) & (DR5 <= -0.3)
    g = np.where(up, 1, np.where(dn, -1, 0)); return g if follow else -g
def sig_cvdtrend():
    on = (np.arange(NB) % 240 == 0) & (np.sign(CVD240) == np.sign(RET240)) & (ABS240 >= CVD_Q80); return np.where(on, np.sign(CVD240), 0)
NY = ZoneInfo("America/New_York")
def sig_session():
    out = np.zeros(NB); dts = pd.to_datetime(T, unit="s", utc=True)
    mins = (dts.hour * 60 + dts.minute).values
    # NY open 09:30 ET → +30 min = 10:00 ET; Asia 00:00 UTC → +30 min
    edt = np.array([bool(NY.dst(dt.datetime(d.year, d.month, d.day, 12))) for d in dts[::day].to_pydatetime()])
    dst = np.repeat(edt, day)[:NB]
    ny_ok = np.where(dst, mins == 14 * 60, mins == 15 * 60); asia_ok = mins == 30
    d30 = s(DELTA).rolling(30).sum().values
    out[ny_ok | asia_ok] = np.sign(d30[ny_ok | asia_ok]); return out
VARIANTS = {}
for x, k in itertools.product((0.30, 0.50), (3, 5)):
    VARIANTS[f"S1_burst_follow_x{x}_k{k}"] = ("S1", lambda x=x, k=k: sig_burst(x, k, False))
    VARIANTS[f"S2_burst_fade_x{x}_k{k}"] = ("S2", lambda x=x, k=k: sig_burst(x, k, True))
for n in (30, 60): VARIANTS[f"S3_divergence_n{n}"] = ("S3", lambda n=n: sig_div(n))
for y in (0.30, 0.50): VARIANTS[f"S4_absorption_y{y}"] = ("S4", lambda y=y: sig_abs(y))
for p in (0.02, 0.05): VARIANTS[f"S5_whale_follow_p{p}"] = ("S5", lambda p=p: sig_whale(p))
for m in (3, 5):
    VARIANTS[f"S6_cascade_fade_m{m}"] = ("S6", lambda m=m: sig_cascade(m, False))
    VARIANTS[f"S6b_cascade_follow_m{m}"] = ("S6b", lambda m=m: sig_cascade(m, True))
VARIANTS["S7_cvd_trend"] = ("S7", sig_cvdtrend)
VARIANTS["S8_session_open"] = ("S8", sig_session)
HS = (15, 60, 240)
def span(y0, y1):
    a = np.searchsorted(year, y0); b = np.searchsorted(year, y1 + 1); return a, b
PER = {"choose": span(2020, 2022), "validate": span(2023, 2024), "holdout": span(2025, 2026)}
PRE = "--prehistory" in sys.argv
if PRE: PER = {"prehistory": span(2017, 2019)}

def twin_stats(ent, ret_gross, sig, h, a, b, n_draw=2000):
    """random entries: same count, same direction mix, same hour-of-day mix, inside [a, b)."""
    if len(ent) < 10: return np.nan, np.nan
    hrs = hour[ent - 1]; dirs = sig[ent - 1]
    pool = {hh: np.where((hour[a:b - h - 1] == hh) & ~gapnear[a:b - h - 1])[0] + a for hh in np.unique(hrs)}
    means = []
    for _ in range(n_draw):
        r = []
        for hh in np.unique(hrs):
            m = hrs == hh; idx = rng.choice(pool[hh], m.sum()); d = dirs[m]
            r.append(d * (C[idx + h] - O[idx + 1]) / O[idx + 1])
        means.append(np.concatenate(r).mean())
    means = np.array(means); return means.mean(), (np.sum(means >= ret_gross.mean()) + 1) / (n_draw + 1)

rows = []; blocks = {}
for name, (fam, f) in VARIANTS.items():
    sig0 = np.nan_to_num(f()).astype(np.float64); sig0[gapnear] = 0
    for h in HS:
        rec = dict(variant=f"{name}_H{h}", family=fam)
        for per, (a, b) in PER.items():
            ent, g = run(sig0, O, C, h, a, b - h - 1)
            rec[f"{per}_n"] = len(g); rec[f"{per}_gross"] = g.mean() if len(g) else np.nan
            rec[f"{per}_taker"] = (g - TAKER).mean() if len(g) else np.nan; rec[f"{per}_maker"] = (g - MAKER).mean() if len(g) else np.nan
            if per != "choose":
                tw, p = twin_stats(ent, g, sig0, h, a, b, 500 if PRE else 2000)
                rec[f"{per}_twin"] = tw; rec[f"{per}_p"] = p
        if PRE:
            rows.append(rec); print(f"{rec['variant']:34s} pre-history n {rec['prehistory_n']:5d} net {1e4*rec['prehistory_taker']:+7.1f}bp twin {1e4*(rec['prehistory_twin']-TAKER) if rec['prehistory_twin']==rec['prehistory_twin'] else float('nan'):+7.1f}bp p {rec['prehistory_p']:.3f}", flush=True); continue
        # CSCV blocks: 16 equal time blocks over 2020-2024, net taker return per block (sum)
        a, b = PER["choose"][0], PER["validate"][1]; ent, g = run(sig0, O, C, h, a, b - h - 1)
        edges = np.linspace(a, b, 17).astype(int); blk = np.zeros(16)
        for e_, r_ in zip(ent, g - TAKER): blk[min(15, np.searchsorted(edges, e_, side="right") - 1)] += r_
        blocks[rec["variant"]] = blk; rows.append(rec)
        print(f"{rec['variant']:34s} choose n {rec['choose_n']:5d} net {1e4*rec['choose_taker']:+7.1f}bp | validate n {rec['validate_n']:5d} net {1e4*rec['validate_taker']:+7.1f}bp | holdout n {rec['holdout_n']:5d} net {1e4*rec['holdout_taker']:+7.1f}bp", flush=True)
R = pd.DataFrame(rows)
if PRE:
    R.to_csv(f"{D}/{SYM}_prehistory.csv", index=False); sys.exit(0)
R.to_csv(f"{D}/{SYM}_results.csv", index=False)
np.savez(f"{D}/{SYM}_blocks.npz", **blocks)

# ---------- CSCV probability of backtest overfitting over all variants (2020-2024)
M = np.array([blocks[v] for v in R.variant]).T          # 16 x N
combs = list(itertools.combinations(range(16), 8)); lam = []
for cmb in combs:
    ins = list(cmb); oos = [i for i in range(16) if i not in cmb]
    best = np.argmax(M[ins].sum(0)); r = M[oos].sum(0); rank = (r < r[best]).sum() / (len(r) - 1)
    lam.append(math.log(max(rank, 1e-6) / max(1 - rank, 1e-6)))
pbo = np.mean(np.array(lam) <= 0)
print(f"\nCSCV over {len(R)} variants (taker net, 16 blocks 2020-2024): PBO = {pbo:.3f}")
json.dump({"pbo": pbo}, open(f"{D}/{SYM}_pbo.json", "w"))
