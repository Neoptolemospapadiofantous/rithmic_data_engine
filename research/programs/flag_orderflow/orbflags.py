# Round 8 B: the day's first 5-minute opening-range break vs BOOK2 flags registered after 09:35 (NQ+ES 2014-2026, cache bars);
# order-flow add-on (NQ 2023-03 →, bars_1m buy volume): opening-range delta agrees with the flag's side.
import csv, sys, os, datetime as dt, collections, numpy as np, pandas as pd, psycopg2
W, CACHE = sys.argv[1], sys.argv[2]; rng=np.random.default_rng(51)
def base_epoch(date):
    x=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    return int(dt.datetime(x.year,x.month,x.day,18,0,tzinfo=dt.timezone.utc).timestamp())//60
c=psycopg2.connect(host=os.environ.get("PG_HOST","localhost"),user=os.environ.get("PG_USER","rithmic_user"),dbname=os.environ.get("PG_DB","rithmic"),password=os.environ["PG_PASSWORD"]); c.set_session(readonly=True); cur=c.cursor()
cur.execute("""select (minute at time zone 'America/New_York')::date, sum(2*buy_volume-volume), sum(volume) from bars_1m where symbol='NQ' and minute>='2023-03-01'
  and buy_volume>0 and (minute at time zone 'America/New_York')::time >= '09:30' and (minute at time zone 'America/New_York')::time < '09:35' group by 1""")
ordelta={int(d.strftime("%Y%m%d")):(float(a)/float(v) if v else np.nan) for d,a,v in cur.fetchall()}
T=[]
for sym in ("NQ","ES"):
    df=pd.read_csv(f"{CACHE}/{sym}.csv",usecols=[0,1,2,3,4],header=0,names=["ts","o","h","l","c"]); df=df[df.ts>=int(dt.datetime(2013,12,20,tzinfo=dt.timezone.utc).timestamp())]
    M=(df.ts.values//60).astype(np.int64); H,L=df.h.values,df.l.values
    for r in csv.DictReader(open(f"{W}/w15_{sym.lower()}.csv")):
        dte=int(r["date"])
        if r["ok"]!="1" or dte<20140101 or not(int(r["tf"])>=4 and float(r["x2"])>=3 and 5<=float(r["x4"])*100<38 and r["grade"] in("1","2")): continue
        reg=int(r["reg_min"])
        if reg<=935: continue
        b0=base_epoch(dte); i0,i1,ir=np.searchsorted(M,b0+930),np.searchsorted(M,b0+935),np.searchsorted(M,b0+reg)
        if i1-i0<3: continue
        hi,lo=H[i0:i1].max(),L[i0:i1].min(); brk=0
        for k in range(i1,ir):
            up,dn=H[k]>hi,L[k]<lo
            if up and not dn: brk=1; break
            if dn and not up: brk=-1; break
            if up and dn: brk=0; break
        side=int(r["side"]); od=ordelta.get(dte,np.nan) if sym=="NQ" else np.nan
        T.append(dict(sym=sym,date=dte,R=float(r["r"]),rel=("agree" if brk==side else "against" if brk==-side else "none"),
                      of=("agree" if np.isfinite(od) and np.sign(od)==side else "against" if np.isfinite(od) and np.sign(od)==-side else "na")))
d=pd.DataFrame(T); d["early"]=d.date<20200101; DAY=(d.sym+d.date.astype(str)).values
print(f"BOOK2 flags registered after 09:35: {len(d)} (NQ+ES 2014-2026), mean R {d.R.mean():.3f}\n")
print(d.groupby(["rel"]).R.agg(["size","mean"]).round(3).to_string())
print("\nby period:"); print(d.pivot_table(index="rel",columns="early",values="R",aggfunc=["mean","size"]).round(3).to_string())
by=collections.defaultdict(list)
for i,x in enumerate(DAY): by[x].append(i)
def diff_ci(mask_a, mask_b, n=1500):
    days=np.unique(DAY); R=d.R.values; out=[]
    for _ in range(n):
        p=np.concatenate([by[x] for x in rng.choice(days,len(days))]); a=mask_a[p]; b=mask_b[p]
        if a.any() and b.any(): out.append(R[p][a].mean()-R[p][b].mean())
    return np.percentile(out,5), np.mean(np.array(out)<=0)
A=(d.rel=="agree").values; B=(d.rel=="against").values
lo,p=diff_ci(A,B); ea=d.R[A&d.early].mean()-d.R[B&d.early].mean(); la=d.R[A&~d.early].mean()-d.R[B&~d.early].mean()
print(f"\nORB break AGREES vs AGAINST the flag: diff {d.R[A].mean()-d.R[B].mean():+.3f} R | 2014-19 {ea:+.3f} | 2020-26 {la:+.3f} | 90% low {lo:+.3f} p {p:.3f} -> {'PASS' if lo>0 and ea>0 and la>0 else 'fail'}")
n=d[d.of!="na"]; print(f"\nOrder-flow add-on, NQ 2023-03 → (opening-range delta vs flag side): {len(n)} flags")
print(n.groupby(["of","rel"]).R.agg(["size","mean"]).round(3).to_string())
A2=(d.of=="agree").values; B2=(d.of=="against").values; lo2,p2=diff_ci(A2,B2)
y=d.date.values; a1=d.R[A2&(y<20250101)].mean()-d.R[B2&(y<20250101)].mean(); a2=d.R[A2&(y>=20250101)].mean()-d.R[B2&(y>=20250101)].mean()
print(f"opening delta AGREES vs AGAINST: diff {d.R[A2].mean()-d.R[B2].mean():+.3f} R | 2023-24 {a1:+.3f} | 2025-26 {a2:+.3f} | 90% low {lo2:+.3f} p {p2:.3f}")
