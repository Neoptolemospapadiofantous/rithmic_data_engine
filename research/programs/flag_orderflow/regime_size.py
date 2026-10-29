import csv, sys, numpy as np, random
W, D, mode, seed = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]); random.seed(seed)
share=[]
for sym in ("nq","es"):
    rows=list(csv.reader(open(f"{W}/w15_{sym}.csv"))); h=rows[0]; H={k:i for i,k in enumerate(h)}
    isb2=lambda r: r[H["ok"]]=="1" and int(r[0])>=20140101 and int(r[H["tf"]])>=4 and float(r[H["x2"]])>=3 and 5<=float(r[H["x4"]])*100<38 and r[H["grade"]] in("1","2")
    idx=sorted([i for i in range(1,len(rows)) if isb2(rows[i])], key=lambda i:(int(rows[i][0]),int(rows[i][H["entry_min"]])))
    hit=[abs(float(rows[i][H["exit"]])-float(rows[i][H["target"]]))<=0.5 for i in idx]
    rate={i:(np.mean(hit[max(0,j-60):j]) if j>=30 else np.nan) for j,i in enumerate(idx)}
    q1=np.percentile([rate[i] for i in idx if int(rows[i][0])<=20221231 and not np.isnan(rate[i])],100/3)
    low={i for i in idx if not np.isnan(rate[i]) and rate[i]<=q1}; p=len(low)/len(idx); share.append(p)
    with open(f"{D}/w15_{sym}_rs.csv","w",newline="") as fo:
        w=csv.writer(fo); w.writerow(h+["szm"])
        for i,r in enumerate(rows[1:],start=1):
            if mode=="regime": s=1 if i in low else 2
            elif mode=="random": s=1 if (i in rate and random.random()<p) else 2
            else: s=int(mode)
            w.writerow(r+[str(s)])
print("low-regime share nq/es:", [round(x,3) for x in share])
