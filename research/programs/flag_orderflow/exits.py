import csv, glob, sys, numpy as np
D=sys.argv[1]; rng=np.random.default_rng(5)
TT={r["fid"]:r for r in csv.DictReader(open(f"{D}/trades.csv"))}
risk={k:abs(float(r["entry"])-float(r["stop"])) for k,r in TT.items()}
flatm={k:int((int(r["t_flat"])-int(r["t_fill"]))//60_000_000) for k,r in TT.items()}
T=[]
for f in sorted(glob.glob(f"{D}/xo/20*.csv")): T+= [r for r in csv.DictReader(open(f)) if r["status"]=="ok"]
names=[k for k in T[0] if k not in("fid","date","side","status","r_export","path") and not k.endswith("_min")]
DATE=np.array([int(r["date"]) for r in T]); early=DATE<20250101
base=np.array([float(r["base"]) for r in T]); ex=np.array([float(r["r_export"]) for r in T])
print(f"trades {len(T)}  | tick replay vs export: mean R {base.mean():.3f} vs {ex.mean():.3f}, corr {np.corrcoef(base,ex)[0,1]:.3f}, same win/loss {100*np.mean((base>0)==(ex>0)):.1f}%")
paths=[np.array([float(x) for x in r["path"].split(";")]) if r["path"] else np.array([]) for r in T]
RK=np.array([risk[r["fid"]] for r in T]); FM=np.array([flatm[r["fid"]] for r in T]); bmin=np.array([int(r["base_min"]) for r in T])
def wr(v): return 100*np.mean(v>0)
mmin={"flip_loss_m2":2,"flip_loss_m5":5,"flip_any_m2":2,"flip_any_m5":5,"stall_m10":10,"stall_m20":20,"book_loss_40":2}
print(f"\n{'rule':18s} {'mean R':>7s} {'win %':>6s} {'23-24':>6s} {'25-26':>6s} {'early exits':>11s} {'twin mean':>9s} {'p(twin)':>7s}")
res=[]
for n in names:
    R=np.array([float(r[n]) for r in T]); xm=np.array([int(r[n+"_min"]) for r in T])
    tw, p = np.nan, np.nan
    key=next((k for k in mmin if n.startswith(k)),None)
    cut=(xm<bmin)&(R!=base)
    if key and n!="base" and cut.any():
        idx=np.where(cut)[0]; m0=mmin[key]; draws=[]
        for _ in range(2000):
            Rt=base.copy()
            for i in idx:
                if FM[i]<=m0: continue
                j=rng.integers(m0,FM[i])        # random minute over the WHOLE possible holding time (no look-ahead)
                if j<bmin[i] and j-1<len(paths[i]): Rt[i]=(paths[i][j-1]-1.5)/RK[i]   # else the trade had already ended: base R
            draws.append(Rt.mean())
        draws=np.array(draws); tw=draws.mean(); p=(np.sum(draws>=R.mean())+1)/(len(draws)+1)
    res.append([n,R.mean(),wr(R),R[early].mean(),R[~early].mean(),int(cut.sum()),tw,p])
for n,m,w,a,b,c,tw,p in res: print(f"{n:18s} {m:7.3f} {w:6.1f} {a:6.3f} {b:6.3f} {c:11d} {tw:9.3f} {p:7.3f}")
b=res[0]; print(f"\nbase: 23-24 {b[3]:.3f}  25-26 {b[4]:.3f}")
cand=[r for r in res[1:] if not np.isnan(r[7])]
order=sorted(range(len(cand)),key=lambda i:cand[i][7]); run=0; holm={}
for rank,i in enumerate(order): run=max(run,min(1,(len(res)-1-rank)*cand[i][7])); holm[cand[i][0]]=run
print("\nPASS check (better than base in BOTH halves, twin p<0.05, Holm<0.05 over 13):")
for n,m,w,a,bb,c,tw,p in res[1:]:
    ok = a>b[3] and bb>b[4] and (n in holm and p<0.05 and holm[n]<0.05)
    print(f"  {n:18s} both halves {'yes' if a>b[3] and bb>b[4] else 'no ':3s}  twin p {p:.3f}  Holm {holm.get(n,float('nan')):.3f}  -> {'PASS' if ok else 'fail'}")
