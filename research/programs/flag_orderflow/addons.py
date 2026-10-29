import csv, sys, collections, numpy as np
W, F, OUTDIR = sys.argv[1:4]
feat={r["fid"]:r for r in csv.DictReader(open(F)) if r["complete"]=="1"}
b2d=[r for r in feat.values() if r["book2"]=="1" and int(r["date"])<20250101]
cut=lambda k,q: np.percentile([float(r[k]) for r in b2d],q)
IMB_HI=cut("F_imb",200/3); IMB_LO=cut("F_imb",100/3); TST_HI=cut("F_tests",200/3)
print(f"cuts from BOOK2 2023-24: F_imb third edges {IMB_LO:.4f}/{IMB_HI:.4f}, F_tests top third >= {TST_HI:.1f}")
RULES={"base":lambda f:True,
 "skip book-with (F_imb top third)":lambda f: float(f["F_imb"])<IMB_HI,
 "only book-against (F_imb low third)":lambda f: float(f["F_imb"])<=IMB_LO,
 "skip many tests (F_tests top third)":lambda f: float(f["F_tests"])<TST_HI,
 "skip book-with + skip many tests":lambda f: float(f["F_imb"])<IMB_HI and float(f["F_tests"])<TST_HI}
rows=list(csv.reader(open(W))); hdr=rows[0]; data=rows[1:]
H={k:i for i,k in enumerate(hdr)}
isb2=lambda r: r[H["ok"]]=="1" and int(r[H["tf"]])>=4 and float(r[H["x2"]])>=3 and 5<=float(r[H["x4"]])*100<38 and r[H["grade"]] in("1","2")
print(f"\nBOOK2 NQ trades with features (2023-03 → 2026-03), mean R per trade")
print(f"{'rule':40s} {'n':>4s} {'all':>6s} {'2023':>6s} {'2024':>6s} {'2025':>6s} {'2026':>6s}  {'23-24':>6s} {'25-26':>6s}")
for name,f in RULES.items():
    by=collections.defaultdict(list)
    for i,r in enumerate(data):
        fr=feat.get(str(i))
        if fr is None or not isb2(r) or not f(fr): continue
        by[int(r[0])//10000].append(float(r[H["r"]]))
    al=sum(by.values(),[]); a=lambda v: sum(v)/len(v) if v else float("nan")
    print(f"{name:40s} {len(al):4d} {a(al):6.3f} {a(by[2023]):6.3f} {a(by[2024]):6.3f} {a(by[2025]):6.3f} {a(by[2026]):6.3f}  {a(by[2023]+by[2024]):6.3f} {a(by[2025]+by[2026]):6.3f}")
    # export copy with column 72 = size multiplier (0 = skip) for the account model; rows without features unchanged
    tag=name.split(" (")[0].replace(" ","_").replace("+","plus")
    with open(f"{OUTDIR}/w15_nq_{tag}.csv","w",newline="") as fo:
        w=csv.writer(fo); w.writerow(hdr+["szm"])
        for i,r in enumerate(data):
            fr=feat.get(str(i)); keep = fr is None or f(fr)
            w.writerow(r+["1" if keep else "0"])
