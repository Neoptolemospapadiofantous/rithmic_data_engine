import csv, sys, math, random, collections
import numpy as np
from scipy.stats import spearmanr, norm
from sklearn.linear_model import LogisticRegression
rng=np.random.default_rng(11)
rows=[r for r in csv.DictReader(open(sys.argv[1])) if r["complete"]=="1"]
skip={"fid","date","side","tf","grade","book2","x2","x4","r","complete"}
FE=[k for k in rows[0] if k not in skip]
def val(r,k):
    try:
        v=float(r[k]); return v if math.isfinite(v) else np.nan
    except: return np.nan
X=np.array([[val(r,k) for k in FE] for r in rows]); R=np.array([float(r["r"]) for r in rows])
DATE=np.array([int(r["date"]) for r in rows]); B2=np.array([r["book2"]=="1" for r in rows])
disc=DATE<20250101; conf=(DATE>=20250101)&B2
def day_boot(idx, stat, n=1000):
    days=np.unique(DATE[idx]); by={d:np.where(idx&(DATE==d))[0] for d in days}; out=[]
    for _ in range(n):
        pick=np.concatenate([by[d] for d in rng.choice(days,len(days))]); out.append(stat(pick))
    return np.array(out)
print(f"complete flags {len(rows)} | discovery (all flags <2025) {disc.sum()} | confirmation (BOOK2 >=2025) {conf.sum()} | BOOK2 discovery {(disc&B2).sum()}")
print(f"base mean R: discovery all {R[disc].mean():.3f}, BOOK2 discovery {R[disc&B2].mean():.3f}, confirmation BOOK2 {R[conf].mean():.3f}\n")
res=[]
for j,k in enumerate(FE):
    m=disc&~np.isnan(X[:,j])
    if m.sum()<200 or np.nanstd(X[m,j])==0: continue
    rho=spearmanr(X[m,j],R[m]).statistic
    bs=day_boot(m, lambda p: spearmanr(X[p,j],R[p]).statistic, 600)
    se=np.nanstd(bs); z=rho/se if se>0 else 0; p=2*(1-norm.cdf(abs(z)))
    mb=disc&B2&~np.isnan(X[:,j]); rb=spearmanr(X[mb,j],R[mb]).statistic if mb.sum()>50 else np.nan
    res.append([k,j,m.sum(),rho,p,rb])
# Benjamini-Hochberg
ps=sorted(range(len(res)),key=lambda i: res[i][4]); m_=len(res); q=[0]*m_; run=1
for rank in range(m_-1,-1,-1):
    i=ps[rank]; run=min(run,res[i][4]*m_/(rank+1)); q[i]=run
for i in range(m_): res[i].append(q[i])
res.sort(key=lambda x: x[4])
print(f"{'feature':18s} {'n':>5s} {'rho all':>8s} {'p(day)':>8s} {'BH q':>6s} {'rho BOOK2':>9s}")
for k,j,n,rho,p,rb,qq in res: print(f"{k:18s} {n:5d} {rho:8.3f} {p:8.4f} {qq:6.3f} {rb:9.3f}{'  <- shortlist' if qq<0.10 else ''}")
short=[(k,j,1 if rho>0 else -1) for k,j,n,rho,p,rb,qq in res if qq<0.10]
print(f"\nshortlist (BH q<0.10): {[s[0] for s in short]}\n")
def gate_eval(name, keep_conf):   # keep_conf: boolean mask over rows (only conf rows matter)
    g=conf&keep_conf; rest=conf&~keep_conf
    diff=R[g].mean()-R[rest].mean() if g.sum() and rest.sum() else np.nan
    bs=day_boot(conf, lambda p: (R[p][keep_conf[p]].mean()-R[p][~keep_conf[p]].mean()) if keep_conf[p].any() and (~keep_conf[p]).any() else np.nan, 2000)
    bs=bs[~np.isnan(bs)]; lo90=np.percentile(bs,5); p1=(bs<=0).mean()
    y25=conf&keep_conf&(DATE<20260101); y26=conf&keep_conf&(DATE>=20260101)
    b25=R[conf&(DATE<20260101)].mean(); b26=R[conf&(DATE>=20260101)].mean()
    return dict(name=name,n=int(g.sum()),gR=R[g].mean(),base=R[conf].mean(),diff=diff,lo90=lo90,p=p1,
                r25=R[y25].mean() if y25.any() else np.nan,b25=b25,r26=R[y26].mean() if y26.any() else np.nan,n26=int(y26.sum()),b26=b26)
out=[]
for k,j,sgn in short:
    md=disc&B2&~np.isnan(X[:,j]); v=sgn*X[md,j]; cut=np.percentile(v,200/3)   # better third, cut fixed on BOOK2 discovery
    keep=np.where(np.isnan(X[:,j]),False,sgn*X[:,j]>=cut)
    if not (0.1 < keep[conf].mean() < 0.9): keep=np.where(np.isnan(X[:,j]),False,sgn*X[:,j]>cut)
    if not (0.05 < keep[conf].mean() < 0.95): print(f"  {k}: untestable as a third (ties) — skipped"); continue
    out.append(gate_eval(f"{k} ({'high' if sgn>0 else 'low'} third)",keep))
# combined score
if short:
    J=[j for _,j,_ in short]; med=np.nanmedian(X[disc][:,J],axis=0)
    def prep(M): M=M[:,J].copy(); 
    Z=X[:,J].copy(); 
    for c in range(Z.shape[1]): Z[np.isnan(Z[:,c]),c]=med[c]
    mu=Z[disc].mean(0); sd=Z[disc].std(0); sd[sd==0]=1; Z=(Z-mu)/sd
    lr=LogisticRegression(C=0.1,max_iter=2000).fit(Z[disc],(R[disc]>0).astype(int))
    s=lr.decision_function(Z); cut=np.median(s[disc&B2])
    out.append(gate_eval("COMBINED score, top half",s>=cut))
    from sklearn.metrics import roc_auc_score
    print(f"combined score AUC (win) on confirmation BOOK2: {roc_auc_score((R[conf]>0).astype(int), s[conf]):.3f}")
# Holm across pre-registered confirmations
order=sorted(range(len(out)),key=lambda i: out[i]["p"]); run=0
for rank,i in enumerate(order): run=max(run,min(1,(len(out)-rank)*out[i]["p"])); out[i]["holm"]=run
print(f"\nCONFIRMATION — BOOK2 trades 2025-01 → 2026-03, read once (base mean R {R[conf].mean():.3f}, n {conf.sum()})")
print(f"{'gate':42s} {'n':>4s} {'mean R':>7s} {'gate−rest':>9s} {'90% low':>8s} {'p':>6s} {'Holm':>5s} {'2025 g/b':>13s} {'2026 g/b (n)':>17s}  verdict")
for o in out:
    ok = o["lo90"]>0 and o["holm"]<0.10 and o["r25"]>o["b25"] and (o["n26"]<15 or o["r26"]>o["b26"])
    print(f"{o['name']:42s} {o['n']:4d} {o['gR']:7.3f} {o['diff']:9.3f} {o['lo90']:8.3f} {o['p']:6.3f} {o['holm']:5.2f} {o['r25']:6.2f}/{o['b25']:5.2f} {o['r26']:6.2f}/{o['b26']:5.2f} ({o['n26']:2d})  {'PASS' if ok else 'fail'}")
# exploratory: every feature's better/worse third on confirmation (NOT pre-registered verdicts)
print("\nEXPLORATORY (not a verdict): every feature on confirmation BOOK2, high third vs low third mean R (cuts from BOOK2 discovery)")
ex=[]
for j,k in enumerate(FE):
    md=disc&B2&~np.isnan(X[:,j])
    if md.sum()<100: continue
    lo,hi=np.percentile(X[md,j],[100/3,200/3]); c=conf&~np.isnan(X[:,j])
    h=c&(X[:,j]>=hi); l=c&(X[:,j]<=lo)
    hd=R[disc&B2&(X[:,j]>=hi)].mean()-R[disc&B2&(X[:,j]<=lo)].mean()
    ex.append((k,R[h].mean()-R[l].mean(),hd,h.sum(),l.sum()))
ex.sort(key=lambda x:-abs(x[1]))
for k,cd,dd,nh,nl in ex[:15]: print(f"  {k:18s} confirm high−low {cd:+.3f}  discovery high−low {dd:+.3f}  (n {nh}/{nl}) {'same sign' if cd*dd>0 else 'FLIPS'}")
