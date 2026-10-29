# Round 4 liquidity tests on BOOK2 (NQ + ES, 2014-2026), 1-minute cache bars. See the preregistered note, "Round 4".
import csv, sys, datetime as dt, collections, numpy as np, pandas as pd
W, CACHE = sys.argv[1], sys.argv[2]; rng=np.random.default_rng(13)
def load(sym):
    df=pd.read_csv(f"{CACHE}/{sym}.csv",usecols=[0,1,2,3,4],header=0,names=["ts","o","h","l","c"])
    df=df[df.ts>=int(dt.datetime(2013,12,20,tzinfo=dt.timezone.utc).timestamp())]
    return {"m":(df.ts.values//60).astype(np.int64),"o":df.o.values,"h":df.h.values,"l":df.l.values,"c":df.c.values}
def base_epoch(date):   # ET-wall epoch minute of 18:00 on the calendar day before the trading date
    d=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    return int(dt.datetime(d.year,d.month,d.day,18,0,tzinfo=dt.timezone.utc).timestamp())//60
TR=[]; DAYLVL={}
import os
MK=[(m,float(c)) for m,c in (x.split(":") for x in os.environ.get("MKTS","NQ:1.5,ES:0.5").split(","))]
FEE=float(os.environ.get("PCT_COST","0"))
for sym,cost in MK:
    B=load(sym); M=B["m"]
    idx=lambda em: np.searchsorted(M,em)
    # daily levels: RTH 09:30-16:00 (tday 930..1319) and overnight 18:00-09:30 (tday 0..929)
    rows=[r for r in csv.DictReader(open(f"{W}/w15_{sym.lower() if sym in ('NQ','ES') else sym}.csv")) if r["ok"]=="1" and int(r["date"])>=20140101
          and int(r["tf"])>=4 and float(r["x2"])>=3 and 5<=float(r["x4"])*100<38 and r["grade"] in("1","2")]
    dates=sorted({int(r["date"]) for r in rows}|set())
    alld=sorted({int(r["date"]) for r in csv.DictReader(open(f"{W}/w15_{sym.lower() if sym in ('NQ','ES') else sym}.csv"))})
    lv={}
    for dte in alld:
        b0=base_epoch(dte); i0,i1,i2=idx(b0),idx(b0+930),idx(b0+1320)
        if i2<=i1: continue
        lv[dte]=dict(rl=B["l"][i1:i2].min(),rh=B["h"][i1:i2].max(),ol=B["l"][i0:i1].min() if i1>i0 else np.nan,oh=B["h"][i0:i1].max() if i1>i0 else np.nan)
    prev={d:alld[i-1] for i,d in enumerate(alld) if i>0}
    for r in rows:
        dte=int(r["date"]); d=int(r["side"]); b0=base_epoch(dte)
        e,s,t=float(r["entry"]),float(r["stop"]),float(r["target"]); risk=abs(e-s); cost=FEE*e if FEE>0 else cost; fx=float(r["flag_lo"]); R=(int(r["side"])*(float(r["exit"])-float(r["entry"]))/abs(float(r["entry"])-float(r["stop"])) - FEE*float(r["entry"])/abs(float(r["entry"])-float(r["stop"]))) if FEE>0 else float(r["r"])
        reg,xm=int(r["reg_min"]),int(r["exit_min"]); H,L=float(r["pole_hi"]),float(r["pole_lo"])
        o=dict(sym=sym,date=dte,side=d,R=R,risk=risk,cost=cost)
        # --- LQ1: stop hunt + sweep-reclaim re-entry
        o["stopped"]=R<-0.95
        if o["stopped"]:
            ix=idx(b0+xm); iflat=idx(b0+1315)
            o["hunt"]= bool(ix<iflat and (np.max(B["h"][ix+1:iflat+1])>=t if d>0 else np.min(B["l"][ix+1:iflat+1])<=t))
            ext=B["l"][ix] if d>0 else B["h"][ix]; cand=[]; sig=None
            for k in range(ix+1,min(ix+16,iflat)):
                ext=min(ext,B["l"][k]) if d>0 else max(ext,B["h"][k]); c=B["c"][k]; st=ext-0.25*d
                if d*(c-st)>0 and d*(t-c)>0: cand.append((k,c,st))
                if sig is None and d*(c-fx)>0 and d*(c-st)>0 and d*(t-c)>0: sig=(k,c,st)
            def sim(k,c,st):
                for j in range(k+1,iflat+1):
                    if (B["l"][j]<=st if d>0 else B["h"][j]>=st): return (d*(st-c)-cost)/risk
                    if (B["h"][j]>=t if d>0 else B["l"][j]<=t): return (d*(t-c)-cost)/risk
                return (d*(B["c"][iflat]-c)-cost)/risk
            o["re"]=sim(*sig) if sig else None
            o["re_twin"]=[sim(*cnd) for cnd in cand] if cand else []
        # --- flag window: pole peak = last bar touching the pole extreme before registration
        ir=idx(b0+reg); ext_=H if d>0 else L; pk=None
        for k in range(ir-1,max(ir-240,0),-1):
            if (B["h"][k]>=ext_-0.01 if d>0 else B["l"][k]<=ext_+0.01): pk=k; break
        sw=False
        if pk is not None and ir-pk>=4:
            lo_=B["l"] if d>0 else -B["h"]; cl=B["c"] if d>0 else -B["c"]
            for k in range(pk+1,ir-2):
                if lo_[k]<lo_[k-1] and lo_[k]<=lo_[k+1]:           # an earlier swing low inside the flag
                    for j in range(k+2,ir):
                        if lo_[j]<lo_[k]-0.25:                    # swept by >= 1 tick
                            if any(cl[q]>lo_[k] for q in range(j,ir)): sw=True
                            break
                    if sw: break
        o["swept_in_flag"]=sw
        # --- LQ3: flag extreme through the prior RTH extreme or the overnight extreme, entry back on our side
        P=lv.get(prev.get(dte)); Z=lv.get(dte)
        pool=False
        if P is not None:
            lvl=P["rl"] if d>0 else P["rh"]; pool |= d*(lvl-fx)>0 and d*(e-lvl)>0
        if Z is not None and reg>=930 and np.isfinite(Z["ol"]):
            lvl=Z["ol"] if d>0 else Z["oh"]; pool |= d*(lvl-fx)>0 and d*(e-lvl)>0
        o["pool_sweep"]=pool
        TR.append(o)
early=np.array([o["date"]<int(os.environ.get("SPLIT","20200101")) for o in TR]); R=np.array([o["R"] for o in TR]); DAY=np.array([f'{o["sym"]}{o["date"]}' for o in TR])
print(f"BOOK2 trades NQ+ES 2014-2026: {len(TR)}  mean R {R.mean():.3f} (2014-19 {R[early].mean():.3f} / 2020-26 {R[~early].mean():.3f})\n")
def cmp(name, mask):
    def ci(m):
        days=np.unique(DAY); by=collections.defaultdict(list)
        for i,dd in enumerate(DAY): by[dd].append(i)
        out=[]
        for _ in range(1000):
            pick=np.concatenate([by[x] for x in rng.choice(days,len(days))]); mm=m[pick]; rr=R[pick]
            if mm.any() and (~mm).any(): out.append(rr[mm].mean()-rr[~mm].mean())
        return np.percentile(out,5)
    a=R[mask&early].mean()-R[~mask&early].mean(); b=R[mask&~early].mean()-R[~mask&~early].mean()
    lo=ci(mask); ok=a>0 and b>0 and lo>0
    print(f"{name:34s} n {mask.sum():5d} ({100*mask.mean():.0f}%)  mean R {R[mask].mean():.3f} vs rest {R[~mask].mean():.3f} | 2014-19 diff {a:+.3f}  2020-26 diff {b:+.3f} | 90% low {lo:+.3f} -> {'PASS' if ok else 'fail'}")
    return ok
# LQ1
S=[o for o in TR if o["stopped"]]; se=np.array([o["date"]<int(os.environ.get("SPLIT","20200101")) for o in S])
print(f"LQ1 stopped trades {len(S)} ({100*len(S)/len(TR):.0f}% of trades); target reached later the same session: {100*np.mean([o['hunt'] for o in S]):.1f}%")
re=[(o["re"],o["date"]<int(os.environ.get("SPLIT","20200101")),o["re_twin"]) for o in S if o["re"] is not None]
rv=np.array([x[0] for x in re]); re_e=np.array([x[1] for x in re])
draws=np.array([np.mean([x[2][rng.integers(len(x[2]))] for x in re]) for _ in range(2000)]); p=(np.sum(draws>=rv.mean())+1)/2001
ok1 = rv[re_e].mean()>0 and rv[~re_e].mean()>0 and p<0.05
print(f"    sweep-reclaim re-entries {len(rv)} ({100*len(rv)/len(S):.0f}% of stops): mean R {rv.mean():.3f} win {100*np.mean(rv>0):.1f}% | 2014-19 {rv[re_e].mean():.3f}  2020-26 {rv[~re_e].mean():.3f} | random re-entry twin {draws.mean():.3f} p {p:.3f} -> {'PASS' if ok1 else 'fail'}")
print(f"    added R if every re-entry is taken: {rv.sum():.1f} on top of base {R.sum():.1f}\n")
ok2=cmp("LQ2 flag swept an earlier flag low", np.array([o["swept_in_flag"] for o in TR]))
ok3=cmp("LQ3 flag swept prior-day/overnight", np.array([o["pool_sweep"] for o in TR]))
