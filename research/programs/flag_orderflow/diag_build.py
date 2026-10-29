# per-trade diagnosis table for BOOK2 (NQ + ES, 2014-2026): exit type, MFE / MAE in R, geometry, timing, context
import csv, sys, datetime as dt, numpy as np, pandas as pd
W, CACHE, OUT = sys.argv[1:4]
def base_epoch(date):
    d=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    return int(dt.datetime(d.year,d.month,d.day,18,0,tzinfo=dt.timezone.utc).timestamp())//60
out=[]
for sym,cost in (("NQ",1.5),("ES",0.5)):
    df=pd.read_csv(f"{CACHE}/{sym}.csv",usecols=[0,1,2,3,4],header=0,names=["ts","o","h","l","c"])
    df=df[df.ts>=int(dt.datetime(2013,12,20,tzinfo=dt.timezone.utc).timestamp())]
    M=(df.ts.values//60).astype(np.int64); H=df.h.values; L=df.l.values; C=df.c.values
    rows=list(csv.DictReader(open(f"{W}/w15_{sym.lower()}.csv")))
    days={}
    for r in rows:
        dte=int(r["date"])
        if dte in days or dte<20131201: continue
        b0=base_epoch(dte); i1,i2=np.searchsorted(M,b0+930),np.searchsorted(M,b0+1320)
        if i2>i1: days[dte]=(H[i1:i2].max()-L[i1:i2].min(), C[i2-1]-df.o.values[i1], H[np.searchsorted(M,b0):i1].max()-L[np.searchsorted(M,b0):i1].min() if i1>np.searchsorted(M,b0) else np.nan)
    dl=sorted(days); rng={d:days[d][0] for d in dl}
    atr={}; 
    for i,d in enumerate(dl): atr[d]=np.mean([rng[x] for x in dl[max(0,i-14):i]]) if i>=5 else np.nan
    for r in rows:
        dte=int(r["date"])
        if r["ok"]!="1" or dte<20140101 or not(int(r["tf"])>=4 and float(r["x2"])>=3 and 5<=float(r["x4"])*100<38 and r["grade"] in("1","2")): continue
        d=int(r["side"]); e,s,t=float(r["entry"]),float(r["stop"]),float(r["target"]); risk=abs(e-s); b0=base_epoch(dte)
        em,xm=int(r["entry_min"]),int(r["exit_min"]); i0,i1=np.searchsorted(M,b0+em),np.searchsorted(M,b0+xm)
        seg=slice(i0,i1+1); mfe=(np.max(H[seg])-e if d>0 else e-np.min(L[seg]))/risk; mae=(e-np.min(L[seg]) if d>0 else np.max(H[seg])-e)/risk
        xp=float(r["exit"]); kind="target" if abs(xp-t)<=0.5 else "stop" if abs(xp-s)<=0.5 else "flat"
        dr,dmove,onr=days.get(dte,(np.nan,np.nan,np.nan))
        out.append(dict(sym=sym,date=dte,year=dte//10000,side=d,R=float(r["r"]),kind=kind,mfe=min(mfe,20),mae=min(mae,20),
            tgtR=abs(t-e)/risk,riskATR=risk/atr.get(dte,np.nan),entry_min=em,hold=xm-em,grade=int(r["grade"]),tf=int(r["tf"]),
            pole_mult=float(r["x2"]),pole_atr=float(r["x3"]),pull=float(r["x4"]),gap=float(r["x5"]),m60=float(r["x6"]),vix=float(r["x30"]) if r["x30"] not in("","nan") else np.nan,
            atr=atr.get(dte,np.nan),day_range_atr=dr/atr.get(dte,np.nan) if atr.get(dte) else np.nan,
            day_trend=d*dmove/dr if dr and dr>0 else np.nan,on_range_atr=onr/atr.get(dte,np.nan) if atr.get(dte) else np.nan))
pd.DataFrame(out).to_csv(OUT,index=False); print(len(out),"trades")
