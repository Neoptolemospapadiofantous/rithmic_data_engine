import sys, datetime as dt, numpy as np, pandas as pd
D, CACHE = sys.argv[1], sys.argv[2]
d=pd.read_csv(f"{D}/diag.csv")
# daily trend efficiency per market, trailing 20 sessions (prior days only)
def base_epoch(date):
    x=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    return int(dt.datetime(x.year,x.month,x.day,18,0,tzinfo=dt.timezone.utc).timestamp())//60
eff={}
for sym in ("NQ","ES"):
    df=pd.read_csv(f"{CACHE}/{sym}.csv",usecols=[0,1,2,3,4],header=0,names=["ts","o","h","l","c"]); df=df[df.ts>=int(dt.datetime(2013,10,1,tzinfo=dt.timezone.utc).timestamp())]
    M=(df.ts.values//60).astype(np.int64)
    dates=sorted(set(d[d.sym==sym].date)); allD=[]; 
    # every weekday from 2013-10 to 2026-10 that has RTH bars
    day=dt.date(2013,10,1); vals={}
    while day<=dt.date(2026,10,24):
        k=day.year*10000+day.month*100+day.day; b0=base_epoch(k); i1,i2=np.searchsorted(M,b0+930),np.searchsorted(M,b0+1320)
        if i2-i1>200: rg=df.h.values[i1:i2].max()-df.l.values[i1:i2].min(); vals[k]=abs(df.c.values[i2-1]-df.o.values[i1])/rg if rg>0 else np.nan
        day+=dt.timedelta(days=1)
    ks=sorted(vals)
    for i,k in enumerate(ks): eff[(sym,k)]=np.nanmean([vals[x] for x in ks[max(0,i-20):i]]) if i>=10 else np.nan
d["trend_eff20"]=[eff.get((s,k),np.nan) for s,k in zip(d.sym,d.date)]
d=d.sort_values(["sym","date","entry_min"]).reset_index(drop=True)
d["tgt_rate60"]=d.groupby("sym")["kind"].transform(lambda x:(x=="target").astype(float).shift(1).rolling(60,min_periods=30).mean())
d["atr_rel"]=d.groupby("sym")["atr"].transform(lambda x: x/x.shift(1).rolling(250,min_periods=100).median())
d["on_range_atr"]=np.where(d.entry_min>=930,d.on_range_atr,np.nan)
d["period"]=np.where(d.year<=2022,"2014-22",np.where(d.year<=2024,"2023-24","2025-26"))
d.to_csv(f"{D}/diag2.csv",index=False)
feats=["vix","atr_rel","on_range_atr","trend_eff20","tgt_rate60","entry_min","riskATR","tgtR","pole_atr","pull","gap","m60"]
print("Mean R by third of each pre-trade variable (thirds cut on 2014-22). Bad third = the lower-R third in 2014-22.\n")
print(f"{'variable':14s} {'third':>6s} {'2014-22':>8s} {'2023-24':>8s} {'2025-26':>8s}   target-hit % 14-22/23-24/25-26")
summ=[]
for f in feats:
    x=d[f]; e=d.period=="2014-22"; 
    if x[e].notna().sum()<500: continue
    q1,q2=np.nanpercentile(x[e],[100/3,200/3]); b=np.where(x.isna(),np.nan,np.where(x<=q1,0,np.where(x<q2,1,2)))
    rows=[]
    for k,nm in ((0,"low"),(1,"mid"),(2,"high")):
        m=b==k; r=[d.R[m&(d.period==p)].mean() for p in ("2014-22","2023-24","2025-26")]; t=[100*(d.kind[m&(d.period==p)]=="target").mean() for p in ("2014-22","2023-24","2025-26")]
        rows.append((nm,r,t,[(m&(d.period==p)).sum() for p in ("2014-22","2023-24","2025-26")]))
    worst=min(rows,key=lambda z:z[1][0]); best=max(rows,key=lambda z:z[1][0])
    for nm,r,t,n in rows:
        tag=" <- bad in 14-22" if nm==worst[0] else ""
        print(f"{f:14s} {nm:>6s} {r[0]:8.3f} {r[1]:8.3f} {r[2]:8.3f}   {t[0]:4.1f}/{t[1]:4.1f}/{t[2]:4.1f}  n {n[0]}/{n[1]}/{n[2]}{tag}")
    holds = worst[1][1]<best[1][1] and worst[1][2]<best[1][2] and worst[1][1]==min(z[1][1] for z in rows) and worst[1][2]==min(z[1][2] for z in rows)
    summ.append((f,worst[0],round(best[1][0]-worst[1][0],3),holds)); print()
print("Variables whose 2014-22 worst third is ALSO the worst third in 2023-24 AND 2025-26:")
for f,w,gap,h in summ: print(f"  {f:14s} worst={w:5s} 2014-22 spread {gap:+.3f}  {'HOLDS' if h else '-'}")
print("\nWhat changed in 2025-26 (medians by period):")
print(d.groupby("period")[feats].median().round(3).T.to_string())
