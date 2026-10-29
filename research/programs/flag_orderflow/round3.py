import csv, glob, sys, collections, numpy as np
D=sys.argv[1]; rng=np.random.default_rng(9)
C=collections.defaultdict(lambda: {"B":None,"A":[],"E":[],"date":0})
for f in glob.glob(f"{D}/co/20*.csv"):
    for r in csv.DictReader(open(f)):
        t=C[r["fid"]]; t["date"]=int(r["date"])
        row=(int(r["minute"]),float(r["loc"]),float(r["dr"]),int(r["hadneg"]),float(r["R"]))
        if r["family"]=="B": t["B"]=float(r["R"]) if r["R"] not in("nan","") else np.nan
        else: t[r["family"]].append(row)
for t in C.values(): t["A"].sort(); t["E"].sort()
F=[k for k,t in C.items() if t["B"] is not None and np.isfinite(t["B"])]
early=lambda k: C[k]["date"]<20250101
base=np.array([C[k]["B"] for k in F]); print(f"trades {len(F)}  base mean R {base.mean():.3f} (23-24 {np.mean([C[k]['B'] for k in F if early(k)]):.3f} / 25-26 {np.mean([C[k]['B'] for k in F if not early(k)]):.3f})")
def evaluate(name, pick, pool, metric):
    sel={k:pick(C[k]) for k in F}; sel={k:v for k,v in sel.items() if v is not None}
    keys=list(sel); val=np.array([sel[k][4] for k in keys])
    pools=[[c for c in C[k][pool[0]] if pool[1](c)] for k in keys]
    draws=[]
    for _ in range(2000): draws.append(np.mean([p[rng.integers(len(p))][4] for p in pools]))
    draws=np.array(draws); p=(np.sum(draws>=val.mean())+1)/2001
    e=np.array([early(k) for k in keys]); tw_e=np.mean([np.mean([c[4] for c in pl]) for pl,ee in zip(pools,e) if ee]); tw_l=np.mean([np.mean([c[4] for c in pl]) for pl,ee in zip(pools,e) if not ee])
    return dict(name=name,n=len(keys),mean=val.mean(),win=100*np.mean(val>0),twin=draws.mean(),p=p,e=val[e].mean(),l=val[~e].mean(),twe=tw_e,twl=tw_l,keys=keys,val=val)
res=[]
for x in (0.15,0.30):
    res.append(evaluate(f"ADD on flush (delta <= -{x})", lambda t,x=x: next((c for c in t["A"] if c[2]<=-x),None), ("A",lambda c:True),"add"))
for x in (0.15,0.30):
    res.append(evaluate(f"ADD on flow (delta >= +{x})  [contrast]", lambda t,x=x: next((c for c in t["A"] if c[2]>=x),None), ("A",lambda c:True),"add"))
for x in (0.10,0.20):
    res.append(evaluate(f"ENTRY on exhaustion (>= +{x})", lambda t,x=x: next((c for c in t["E"] if c[1]>=1 and c[2]>=x and c[3]==1),None), ("E",lambda c:c[1]>=1),"entry"))
print(f"\n{'rule':42s} {'n':>4s} {'mean R':>7s} {'win %':>6s} {'twin':>6s} {'p':>6s} {'23-24 rule/twin':>16s} {'25-26 rule/twin':>16s}")
for r in res: print(f"{r['name']:42s} {r['n']:4d} {r['mean']:7.3f} {r['win']:6.1f} {r['twin']:6.3f} {r['p']:6.3f} {r['e']:7.3f}/{r['twe']:6.3f} {r['l']:7.3f}/{r['twl']:6.3f}")
# entry rules vs base on the SAME flags + total R
bm={k:C[k]["B"] for k in F}
for r in res:
    if not r["name"].startswith("ENTRY"): continue
    bsame=np.array([bm[k] for k in r["keys"]]); miss=len(F)-r["n"]
    print(f"  {r['name']}: same flags base {bsame.mean():.3f} vs rule {r['mean']:.3f}; total R rule {r['val'].sum():.1f} on {r['n']} trades vs base {base.sum():.1f} on {len(F)} ({miss} flags with no signal)")
tests=[r for r in res if "contrast" not in r["name"]]
order=sorted(range(len(tests)),key=lambda i:tests[i]["p"]); run=0
for rank,i in enumerate(order): run=max(run,min(1,(len(tests)-rank)*tests[i]["p"])); tests[i]["holm"]=run
print("\nverdicts (pre-registered: twin p<0.05, Holm over 4, better than comparison in both halves):")
for r in tests:
    if r["name"].startswith("ADD"): ok=r["p"]<0.05 and r["holm"]<0.05 and r["e"]>r["twe"] and r["l"]>r["twl"]
    else:
        bs=np.array([bm[k] for k in r["keys"]]); e=np.array([early(k) for k in r["keys"]])
        ok=r["p"]<0.05 and r["holm"]<0.05 and r["e"]>max(r["twe"],bs[e].mean()) and r["l"]>max(r["twl"],bs[~e].mean())
    print(f"  {r['name']:42s} p {r['p']:.3f} Holm {r['holm']:.3f} -> {'PASS' if ok else 'fail'}")
