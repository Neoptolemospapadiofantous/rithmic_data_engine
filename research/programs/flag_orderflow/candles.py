# Round 7: candle behaviour of the pole / flag / registration candle (flag's own tf, aligned to 09:30), BOOK2 NQ+ES 2014-2026.
import csv, sys, datetime as dt, collections, numpy as np, pandas as pd
from scipy.stats import spearmanr, norm
W, CACHE = sys.argv[1], sys.argv[2]; rng=np.random.default_rng(21)
def base_epoch(date):
    x=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    return int(dt.datetime(x.year,x.month,x.day,18,0,tzinfo=dt.timezone.utc).timestamp())//60
rows_out=[]
for sym in ("NQ","ES"):
    df=pd.read_csv(f"{CACHE}/{sym}.csv",usecols=[0,1,2,3,4],header=0,names=["ts","o","h","l","c"]); df=df[df.ts>=int(dt.datetime(2013,12,20,tzinfo=dt.timezone.utc).timestamp())]
    M=(df.ts.values//60).astype(np.int64); O,H,L,C=df.o.values,df.h.values,df.l.values,df.c.values
    for r in csv.DictReader(open(f"{W}/w15_{sym.lower()}.csv")):
        dte=int(r["date"])
        if r["ok"]!="1" or dte<20140101 or not(int(r["tf"])>=4 and float(r["x2"])>=3 and 5<=float(r["x4"])*100<38 and r["grade"] in("1","2")): continue
        d=int(r["side"]); tf=int(r["tf"]); b0=base_epoch(dte); reg=int(r["reg_min"]); PH,PL=float(r["pole_hi"]),float(r["pole_lo"])
        ir=np.searchsorted(M,b0+reg); ext=PH if d>0 else PL; oth=PL if d>0 else PH; pk=st=None
        for k in range(ir-1,max(ir-240,0),-1):
            if (H[k]>=ext-0.01 if d>0 else L[k]<=ext+0.01): pk=k; break
        if pk is None: continue
        for k in range(pk,max(pk-240,0),-1):
            if (L[k]<=oth+0.01 if d>0 else H[k]>=oth-0.01): st=k; break
        if st is None: continue
        # tf candles in our-side prices (shorts mirrored)
        sl=slice(st,ir); mm=M[sl]
        if d>0: o,h,l,c=O[sl],H[sl],L[sl],C[sl]
        else:   o,h,l,c=-O[sl],-L[sl],-H[sl],-C[sl]
        key=(mm-570)//tf; pkkey=(M[pk]-570)//tf
        cand=collections.OrderedDict()
        for i,k in enumerate(key):
            if k not in cand: cand[k]=[o[i],h[i],l[i],c[i]]
            else: z=cand[k]; z[1]=max(z[1],h[i]); z[2]=min(z[2],l[i]); z[3]=c[i]
        P=[v for k,v in cand.items() if k<=pkkey]; F=[v for k,v in cand.items() if k>pkkey]
        def rng_(z): return max(z[1]-z[2],1e-9)
        def body(z): return abs(z[3]-z[0])/rng_(z)
        def clv(z): return (z[3]-z[2])/rng_(z)
        f={}
        if P:
            f["P_n"]=len(P); f["P_body"]=np.mean([body(z) for z in P]); f["P_clv"]=np.mean([clv(z) for z in P]); f["P_green"]=np.mean([z[3]>z[0] for z in P])
            tot=sum(rng_(z) for z in P); f["P_maxshare"]=max(rng_(z) for z in P)/tot; f["P_wick_against"]=np.mean([(z[1]-max(z[0],z[3]))/rng_(z) for z in P])
        if F:
            f["F_n"]=len(F); f["F_red"]=np.mean([z[3]<z[0] for z in F]); f["F_body"]=np.mean([body(z) for z in F]); f["F_clv"]=np.mean([clv(z) for z in F])
            f["F_reject_wick"]=np.mean([(min(z[0],z[3])-z[2])/rng_(z) for z in F])
            f["F_overlap"]=np.mean([max(0,min(a[1],b[1])-max(a[2],b[2]))/rng_(b) for a,b in zip(F,F[1:])]) if len(F)>1 else np.nan
            f["F_contraction"]=rng_(F[-1])/rng_(F[0]) if len(F)>1 else np.nan
            f["F_lower_highs"]=np.mean([b[1]<a[1] for a,b in zip(F,F[1:])]) if len(F)>1 else np.nan
            z=F[-1]; pz=F[-2] if len(F)>1 else P[-1]
            f["R_clv"]=clv(z); f["R_body"]=body(z); f["R_green"]=float(z[3]>z[0])
            f["R_hammer"]=float((min(z[0],z[3])-z[2])>=2*max(abs(z[3]-z[0]),0.25))
            f["R_inside"]=float(z[1]<pz[1] and z[2]>pz[2])
            f["R_engulf"]=float(z[3]>z[0] and pz[3]<pz[0] and z[3]>=pz[0] and z[0]<=pz[3])
        f.update(sym=sym,date=dte,R=float(r["r"])); rows_out.append(f)
d=pd.DataFrame(rows_out); d.to_csv(sys.argv[3],index=False)
DAY=(d.sym+d.date.astype(str)).values; R=d.R.values; disc=(d.date<20200101).values; conf=~disc
feats=[c for c in d.columns if c not in("sym","date","R")]
print(f"BOOK2 trades with candles: {len(d)}  discovery 2014-19 {disc.sum()} (mean R {R[disc].mean():.3f}) | confirmation 2020-26 {conf.sum()} (mean R {R[conf].mean():.3f})\n")
bydays=collections.defaultdict(list)
for i,x in enumerate(DAY): bydays[x].append(i)
def boot(mask, stat, n=600):
    days=np.unique(DAY[mask]); out=[]
    for _ in range(n):
        pick=np.concatenate([[i for i in bydays[x] if mask[i]] for x in rng.choice(days,len(days))]); out.append(stat(np.array(pick)))
    return np.array(out)
res=[]
for f in feats:
    x=d[f].values.astype(float); m=disc&~np.isnan(x)
    if m.sum()<300 or np.nanstd(x[m])==0: continue
    rho=spearmanr(x[m],R[m]).statistic; bs=boot(m,lambda p: spearmanr(x[p],R[p]).statistic); se=np.nanstd(bs); p=2*(1-norm.cdf(abs(rho/se))) if se>0 else 1
    res.append([f,rho,p])
ps=sorted(range(len(res)),key=lambda i:res[i][2]); q=[0]*len(res); run=1
for rank in range(len(res)-1,-1,-1): i=ps[rank]; run=min(run,res[i][2]*len(res)/(rank+1)); q[i]=run
print(f"{'feature':16s} {'rho 14-19':>9s} {'p':>7s} {'BH q':>6s}")
for (f,rho,p),qq in sorted(zip(res,q),key=lambda z:z[0][2]): print(f"{f:16s} {rho:9.3f} {p:7.4f} {qq:6.3f}{'  <- shortlist' if qq<0.10 else ''}")
short=[(f,rho) for (f,rho,p),qq in zip(res,q) if qq<0.10]
print(f"\nCONFIRMATION 2020-2026 (read once), base mean R {R[conf].mean():.3f}")
out=[]
for f,rho in short:
    x=d[f].values.astype(float); sg=1 if rho>0 else -1
    if set(np.unique(x[~np.isnan(x)]))<= {0.0,1.0}: keep=(sg*x>=sg*1) if sg>0 else (x==0)
    else: cut=np.nanpercentile(sg*x[disc],200/3); keep=np.where(np.isnan(x),False,sg*x>=cut)
    g=conf&keep; rest=conf&~keep
    if g.sum()<30 or rest.sum()<30: print(f"  {f}: too few"); continue
    bs=boot(conf,lambda p: R[p][keep[p]].mean()-R[p][~keep[p]].mean() if keep[p].any() and (~keep[p]).any() else np.nan,1500); bs=bs[~np.isnan(bs)]
    lo=np.percentile(bs,5); pv=(bs<=0).mean(); y=d.date.values
    a=R[g&(y<20230101)].mean()-R[rest&(y<20230101)].mean(); b=R[g&(y>=20230101)].mean()-R[rest&(y>=20230101)].mean()
    out.append([f,g.sum(),R[g].mean(),R[g].mean()-R[rest].mean(),lo,pv,a,b,100*(R[g]>0).mean()])
order=sorted(range(len(out)),key=lambda i:out[i][5]); run=0
for rank,i in enumerate(order): run=max(run,min(1,(len(out)-rank)*out[i][5])); out[i].append(run)
print(f"{'feature (better side)':22s} {'n':>5s} {'mean R':>7s} {'diff':>7s} {'90% low':>8s} {'p':>6s} {'Holm':>5s} {'20-22':>7s} {'23-26':>7s} {'win%':>6s}")
for f,n,mr,df_,lo,pv,a,b,w,h in out:
    ok=lo>0 and h<0.10 and a>0 and b>0
    print(f"{f:22s} {n:5d} {mr:7.3f} {df_:+7.3f} {lo:+8.3f} {pv:6.3f} {h:5.2f} {a:+7.3f} {b:+7.3f} {w:6.1f}  {'PASS' if ok else 'fail'}")
