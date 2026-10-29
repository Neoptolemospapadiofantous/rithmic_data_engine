import csv, glob, sys, math, collections, numpy as np
from scipy.stats import spearmanr, norm
rng=np.random.default_rng(41); D=sys.argv[1]
rows=[]
for f in sorted(glob.glob(f"{D}/orb/o/20*.csv")): rows+=[r for r in csv.DictReader(open(f)) if r["complete"]=="1"]
NAMES={"P_delta":"opening-range delta","P_vol_rel":"opening volume vs pre-open","P_big_delta":"opening large-trade delta","P_imb":"opening book imbalance",
 "P_spr":"opening spread","F_delta":"approach delta","F_last1_delta":"last-minute delta before break","F_big_delta":"approach large-trade delta",
 "F_imb_last":"book imbalance at break","F_micro_last":"microprice lead at break","F_spr_last":"spread at break","F_minutes":"minutes range end → break","S_cumdelta":"session delta since 09:30"}
def v(r,k):
    try: x=float(r[k]); return x if math.isfinite(x) else np.nan
    except: return np.nan
X={k:np.array([v(r,k) for r in rows]) for k in NAMES}; R=np.array([float(r["r"]) for r in rows]); DATE=np.array([int(r["date"]) for r in rows]); LIVE=np.array([r["book2"]=="1" for r in rows])
disc=DATE<20250301; conf=(DATE>=20250301)&LIVE
by=collections.defaultdict(list)
for i,d in enumerate(DATE): by[d].append(i)
def boot(mask,stat,n=800):
    days=np.unique(DATE[mask]); out=[]
    for _ in range(n): p=np.array([i for d in rng.choice(days,len(days)) for i in by[d] if mask[i]]); out.append(stat(p))
    return np.array(out)
print(f"ORB trades {len(rows)} | discovery (all, <2025-03) {disc.sum()} mean ${R[disc].mean():.2f}/MNQ | confirmation (live-like, >=2025-03) {conf.sum()} mean ${R[conf].mean():.2f}/MNQ, win {100*np.mean(R[conf]>0):.1f}%\n")
res=[]
for k,nm in NAMES.items():
    x=X[k]; m=disc&np.isfinite(x)
    if np.nanstd(x[m])==0: continue
    rho=spearmanr(x[m],R[m]).statistic; se=np.std(boot(m,lambda p: spearmanr(x[p],R[p]).statistic,500)); p=2*(1-norm.cdf(abs(rho/se)))
    res.append([k,nm,rho,p])
ps=sorted(range(len(res)),key=lambda i:res[i][3]); q=[0]*len(res); run=1
for rank in range(len(res)-1,-1,-1): i=ps[rank]; run=min(run,res[i][3]*len(res)/(rank+1)); q[i]=run
print(f"{'feature':34s} {'rho':>7s} {'p':>7s} {'BH q':>6s}")
for (k,nm,rho,p),qq in sorted(zip(res,q),key=lambda z:z[0][3]): print(f"{nm:34s} {rho:+7.3f} {p:7.4f} {qq:6.3f}{'  <- shortlist' if qq<0.10 else ''}")
short=[(k,nm,rho) for (k,nm,rho,p),qq in zip(res,q) if qq<0.10]
print(f"\nCONFIRMATION (live-like ORB trades 2025-03 → 2026-03), base ${R[conf].mean():.2f}/MNQ per trade")
out=[]
for k,nm,rho in short:
    x=X[k]; sg=1 if rho>0 else -1; cut=np.nanpercentile(sg*x[disc&LIVE],200/3); keep=np.where(np.isfinite(x),sg*x>=cut,False)
    g=conf&keep; rest=conf&~keep
    bs=boot(conf,lambda p: R[p][keep[p]].mean()-R[p][~keep[p]].mean() if keep[p].any() and (~keep[p]).any() else np.nan,1500); bs=bs[~np.isnan(bs)]
    a=R[g&(DATE<20260101)].mean()-R[rest&(DATE<20260101)].mean(); b=R[g&(DATE>=20260101)].mean()-R[rest&(DATE>=20260101)].mean()
    out.append([nm,g.sum(),R[g].mean(),R[g].mean()-R[rest].mean(),np.percentile(bs,5),(bs<=0).mean(),a,b,100*np.mean(R[g]>0)])
order=sorted(range(len(out)),key=lambda i:out[i][5]); run=0
for rank,i in enumerate(order): run=max(run,min(1,(len(out)-rank)*out[i][5])); out[i].append(run)
for nm,n,mr,df_,lo,pv,a,b,w,h in out:
    print(f"  {nm:34s} n {n:4d} ${mr:6.2f} vs rest diff ${df_:+6.2f} | 90% low {lo:+6.2f} p {pv:.3f} Holm {h:.2f} | 2025 {a:+6.2f} 2026 {b:+6.2f} | win {w:.1f}%  {'PASS' if lo>0 and h<0.10 and a>0 and b>0 else 'fail'}")
if not short: print("  (no feature passed discovery)")
