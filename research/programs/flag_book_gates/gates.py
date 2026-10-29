import csv, sys, random, math, collections, statistics as st
random.seed(7)
R=[]
for x in csv.DictReader(open(sys.argv[1])):
    f=lambda k: float(x[k]) if x[k] not in ("",None) else None
    R.append(dict(d=x["d"], dir=int(x["dir"]), pnl=float(x["pnl"]), mod=float(x["mod"]),
      imb=f("imb"), bh=f("bh"), ah=f("ah"), spr=f("spr"), v1=f("v1"), bv1=f("bv1"), dl5=f("dl5"),
      qimb=f("qimb"), ctar=f("ctar"), bq=f("bq"), aq=f("aq")))
# sign check for depth imbalance
pairs=[(r["qimb"], r["bq"]-r["aq"]) for r in R if r["qimb"] is not None]
agree=sum(1 for a,b in pairs if a*b>0)/len(pairs); print(f"qty_imbalance sign agrees with bid-ask depth: {agree:.2f}")
si=sorted(r["dir"]*(r["imb"]-0.5) for r in R); I33,I67=si[len(si)//3],si[2*len(si)//3]
sp=sorted(r["spr"] for r in R); S33,S67=sp[len(sp)//3],sp[2*len(sp)//3]
med_ctar=st.median([r["ctar"] for r in R if r["ctar"] is not None])
s=lambda r,v: r["dir"]*v
G={
 "book_with (top third, our side)": lambda r: s(r,r["imb"]-0.5)>=I67,
 "book_against = fade the book (__inv)":   lambda r: s(r,r["imb"]-0.5)<=I33,
 "book_pressure_with (heavy-side share)":   lambda r: s(r,r["bh"]-r["ah"])>0.10,
 "book_pressure_against":                   lambda r: s(r,r["bh"]-r["ah"])<-0.10,
 "spread_tight (lowest third)":             lambda r: r["spr"]<=S33,
 "spread_wide (highest third)":              lambda r: r["spr"]>=S67,
 "flow_1m_with (delta our side)":           lambda r: s(r,2*r["bv1"]-r["v1"])>0,
 "flow_1m_against":                         lambda r: s(r,2*r["bv1"]-r["v1"])<0,
 "flow_5m_with":                            lambda r: s(r,r["dl5"])>0,
 "flow_5m_against":                         lambda r: s(r,r["dl5"])<0,
 "depth_with (top-10bps size our side)":    lambda r: r["qimb"] is not None and s(r,r["qimb"])>0,
 "depth_against":                           lambda r: r["qimb"] is not None and s(r,r["qimb"])<0,
 "cancels_high (cancel/add > median)":      lambda r: r["ctar"] is not None and r["ctar"]>med_ctar,
 "cancels_low":                             lambda r: r["ctar"] is not None and r["ctar"]<=med_ctar,
 "combo: fade book + 5m flow with":         lambda r: s(r,r["imb"]-0.5)<=I33 and s(r,r["dl5"])>0,
}
CUT="2025-07-01"
def stats(T):
    n=len(T); 
    if n==0: return dict(n=0,days=0,win=0,pf=0,avg=0,net=0)
    p=[t["pnl"] for t in T]; gw=sum(v for v in p if v>0); gl=-sum(v for v in p if v<0)
    return dict(n=n,days=len({t["d"] for t in T}),win=100*sum(v>0 for v in p)/n,pf=gw/gl if gl else 99,avg=sum(p)/n,net=sum(p))
def perm_p(T,k,obs,N=4000):
    p=[t["pnl"] for t in T]; c=0
    for _ in range(N):
        if sum(random.sample(p,k))/k>=obs: c+=1
    return (c+1)/(N+1)
bydays=collections.defaultdict(list)
for r in R: bydays[r["d"]].append(r)
days=sorted(bydays)
def cluster_ci(g,N=2000):
    diffs=[]
    for _ in range(N):
        ds=[random.choice(days) for _ in days]; a=[];b=[]
        for d in ds:
            for r in bydays[d]: (a if g(r) else b).append(r["pnl"])
        if a and b: diffs.append(sum(a)/len(a)-sum(b)/len(b))
    diffs.sort(); return diffs[int(.025*len(diffs))], diffs[int(.975*len(diffs))]
# Tradeify-style eval on 1 NQ (= 10 MNQ): target +3000, EOD trailing 1000, day loss capped -500, best day <=30% of profit, 60 sessions
def tradeify(g,N=4000,horizon=60):
    daypnl=[10*sum(r["pnl"] for r in bydays[d] if g(r)) for d in days]
    ok=fail=0
    for _ in range(N):
        bal=hwm=0; best=0
        for i in range(horizon):
            v=max(random.choice(daypnl),-500); bal+=v; best=max(best,v)
            if bal<=hwm-1000: fail+=1; break
            hwm=max(hwm,bal)
            if bal>=3000 and best<=0.30*bal: ok+=1; break
    return 100*ok/N, 100*fail/N
base=stats(R); bis=stats([r for r in R if r["d"]<CUT]); boos=stats([r for r in R if r["d"]>=CUT])
print(f"\nBASE mtf_flags_v5  n={base['n']} sessions={base['days']} win={base['win']:.1f}% PF={base['pf']:.2f} avg=${base['avg']:.2f}/MNQ trade net=${base['net']:.0f}/MNQ")
print(f"  in-sample <{CUT}: n={bis['n']} avg=${bis['avg']:.2f}   holdout: n={boos['n']} avg=${boos['avg']:.2f}")
bp,bf=tradeify(lambda r: True); print(f"  Tradeify 1 NQ, 60 sessions: pass {bp:.1f}%  fail {bf:.1f}%\n")
rows=[]
for name,g in G.items():
    T=[r for r in R if g(r)]; s_=stats(T)
    if s_["n"]<20: continue
    p=perm_p(R,s_["n"],s_["avg"]); lo,hi=cluster_ci(g)
    i=stats([r for r in T if r["d"]<CUT]); o=stats([r for r in T if r["d"]>=CUT])
    tp,tf=tradeify(g)
    rows.append((name,s_,p,lo,hi,i,o,tp,tf))
# Holm
order=sorted(range(len(rows)),key=lambda j: rows[j][2]); m=len(rows); holm={}; run=0
for rank,j in enumerate(order): run=max(run,min(1,(m-rank)*rows[j][2])); holm[j]=run
print(f"{'gate':40s} {'n':>5s} {'win%':>5s} {'PF':>5s} {'avg$':>7s} {'uplift':>7s} {'perm p':>7s} {'Holm':>6s} {'day-CI of gate−rest':>20s} {'IS avg':>7s} {'OOS avg':>8s} {'pass%':>6s}")
for j,(name,s_,p,lo,hi,i,o,tp,tf) in enumerate(rows):
    print(f"{name:40s} {s_['n']:5d} {s_['win']:5.1f} {s_['pf']:5.2f} {s_['avg']:7.2f} {s_['avg']-base['avg']:7.2f} {p:7.4f} {holm[j]:6.3f} [{lo:7.2f},{hi:7.2f}]  {i['avg']:7.2f} {o['avg']:8.2f} {tp:6.1f}")
