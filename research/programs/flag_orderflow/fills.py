import csv, glob, sys, numpy as np
D=sys.argv[1]
T={r["fid"]:r for r in csv.DictReader(open(f"{D}/trades2.csv"))}
F=[]
for f in glob.glob(f"{D}/fo/20*.csv"): F+=[r for r in csv.DictReader(open(f)) if r["filled"]=="1" and r["kind"]!="-"]
risk=lambda r: abs(float(T[r["fid"]]["entry"])-float(T[r["fid"]]["stop"]))
R=np.array([float(T[r["fid"]]["r_export"]) for r in F]); kind=np.array([r["kind"] for r in F]); rk=np.array([risk(r) for r in F])
touch_only=np.array([r["touch"]=="1" and r["through"]=="0" for r in F]); slip=np.array([float(r["slip_ticks"]) if r["kind"]=="S" else 0 for r in F])
tgt_to=np.array([r["kind"]=="T" and r["tgt_through"]=="0" for r in F]); hs=np.array([float(r["half_spread"]) if r["kind"]=="F" else 0 for r in F])
print(f"trades {len(F)}: exits stop {np.mean(kind=='S')*100:.1f}% | target {np.mean(kind=='T')*100:.1f}% | 15:55 flat {np.mean(kind=='F')*100:.1f}%")
print(f"ENTRY  flag-mid limit only TOUCHED (never traded through by 1 tick): {touch_only.sum()} trades ({100*touch_only.mean():.1f}%) — their mean R {R[touch_only].mean():.3f} vs through-fills {R[~touch_only].mean():.3f}")
s=slip[kind=='S']
print(f"STOPS  slippage vs the stop price (ticks): mean {s.mean():.2f}, median {np.median(s):.2f}, 90th pct {np.percentile(s,90):.2f}, max {s.max():.1f}; share > 1 tick {100*np.mean(s>1):.1f}%")
print(f"TARGET touched but never traded through: {tgt_to.sum()} of {np.sum(kind=='T')} target exits ({100*tgt_to.sum()/max(1,np.sum(kind=='T')):.1f}%)")
print(f"FLAT   half spread at 15:55: mean {hs[kind=='F'].mean():.3f} pts")
cost_now=1.5
def real(entry_mode, tgt_mode):
    x=R.copy(); x=x-slip*0.25/rk-hs/rk
    if tgt_mode!="filled":
        # a touch-only target that did not fill: the trade is still open — worst case the stop (−1 R − slippage)
        miss=(-1.0-np.mean(s)*0.25/rk)
        x=np.where(tgt_to, (x if tgt_mode=="half" else miss)*(0.5 if tgt_mode=="half" else 1)+(miss*0.5 if tgt_mode=="half" else 0), x)
    keep=np.ones(len(x),bool) if entry_mode=="keep" else ~touch_only
    return x[keep]
print(f"\nmean R per trade (export, cost 1.5 pts): {R.mean():.3f} on {len(R)} trades, total {R.sum():.1f} R")
for em in ("keep","drop"):
    for tm in ("filled","half","missed"):
        x=real(em,tm); print(f"  realistic: touch-only entries {em:4s}, touch-only targets {tm:6s} → mean R {x.mean():.3f} on {len(x)} trades, total {x.sum():.1f} R")
