import sys, numpy as np
exec(open(sys.argv[1]+"/round3.py").read().split("res=[]")[0])
def tm(name, pick):
    sel={k:pick(C[k]) for k in F}; sel={k:v for k,v in sel.items() if v is not None}; keys=list(sel)
    val=np.array([sel[k][4] for k in keys]); mins=np.array([sel[k][0] for k in keys])
    byk={k:{c[0]:c for c in C[k]["A"]} for k in F}
    # (1) same minute, other trades: the rule's minute index applied to a random OTHER trade still open then
    draws=[]
    for _ in range(1000):
        v=[]
        for m in mins:
            for _t in range(50):
                k=F[rng.integers(len(F))]; c=byk[k].get(m)
                if c: v.append(c[4]); break
        draws.append(np.mean(v))
    draws=np.array(draws); p1=(np.sum(draws>=val.mean())+1)/1001
    # (2) same trade, any OTHER minute within +-2 of the rule's minute
    d2=[]
    for _ in range(1000):
        v=[]
        for k,m in zip(keys,mins):
            near=[c for c in C[k]["A"] if abs(c[0]-m)<=2 and c[0]!=m]
            if near: v.append(near[rng.integers(len(near))][4])
        d2.append(np.mean(v))
    d2=np.array(d2); p2=(np.sum(d2>=val.mean())+1)/1001
    imm=np.mean([C[k]["A"][0][4] for k in keys if C[k]["A"]])
    print(f"{name:40s} n {len(keys):4d} rule {val.mean():.3f} | same-minute other trades {draws.mean():.3f} p {p1:.3f} | same trade ±2 min {d2.mean():.3f} p {p2:.3f} | add at first minute {imm:.3f} | median minute {np.median(mins):.0f}")
for x in (0.15,0.30): tm(f"ADD on flush (delta <= -{x})", lambda t,x=x: next((c for c in t["A"] if c[2]<=-x),None))
for x in (0.15,0.30): tm(f"ADD on flow (delta >= +{x}) [contrast]", lambda t,x=x: next((c for c in t["A"] if c[2]>=x),None))
