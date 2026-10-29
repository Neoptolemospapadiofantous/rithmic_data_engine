"""book2_scale_sim.py — day-level prop account simulator with size that SCALES with the account (2026-10-24,
preregistered 2026-10-24_book2_high_winrate.md Part 6). Inputs: per-day dumps of the mtm engine at 1 micro, no own stop
(flag_prop_ev … mtm with FPEV_MTM_Q=1 FPEV_MTM_STOPS=0 FPEV_MTM_DUMP=…): date,pnl,worst ($ at today's ATR).
usage: python3 book2_scale_sim.py W15_NQ.csv DUMP_DIR
"""
import csv, sys, glob, os, numpy as np
W, DD = sys.argv[1], sys.argv[2]
cal = sorted({int(r["date"]) for r in csv.DictReader(open(W)) if int(r["date"]) >= 20140101})
PLANS = {"topstep_50k": dict(dd=2000, lock=0, dll=1000, win_min=150, win_days=5, min_profit=0, share=0.45, cap=5000, max_micros=50, target=3000, cons=0.5, min_days=1),
         "lucid_flex_50k": dict(dd=2000, lock=100, dll=0, win_min=150, win_days=5, min_profit=500, share=0.45, cap=2000, max_micros=40, target=3000, cons=0.5, min_days=2),
         "lucid_daily_50k": dict(dd=2000, lock=100, dll=0, win_min=0, win_days=0, min_profit=500, share=1.0, cap=1e12, max_micros=40, target=3000, cons=0.5, min_days=1, intraday_funded=True, buffer=2100),
         "lucid_daily_50k_OPT": dict(dd=2000, lock=100, dll=0, win_min=0, win_days=0, min_profit=500, share=1.0, cap=1e12, max_micros=40, target=3000, cons=0.5, min_days=1, intraday_funded="after", buffer=2100)}
CAP = 40
def load(path):
    p = {int(r["date"]): (float(r["pnl"]), float(r["worst"]), float(r.get("best") or 0)) for r in csv.DictReader(open(path))}
    g = lambda j, f: np.array([f(p.get(d, (0.0, 0.0, 0.0))[j]) for d in cal])
    return g(0, float), g(1, lambda x: min(0.0, x)), g(2, lambda x: max(0.0, x))
def sizer(rule, bal, floor, start, w95):
    if rule == "F2": return 2
    if rule == "F40": return 40
    if rule.startswith("F") and rule[1:].isdigit(): return int(rule[1:])
    if rule == "SCALE250": return int(min(CAP, 2 + max(0.0, bal - start) // 250))
    if rule == "ROOM": return int(min(CAP, max(1, (0.5 * (bal - floor)) // w95)))
def day(bal, floor, s, pnl1, worst1, P):
    """one day at s micros → (new balance, breached?)"""
    worst = s * worst1; pnl = s * pnl1
    if bal + worst <= floor: return floor, True                     # intraday mark reaches the floor
    if P["dll"] and worst <= -P["dll"]: pnl = -P["dll"]               # soft daily limit: closed at the limit
    return bal + pnl, False
def run(rule, pnl1, worst1, P, w95, i0, eval_days=60, funded_days=120, best1=None):
    start = 50000.0; bal = start; floor = start - P["dd"]; best = 0.0; n = 0; i = i0
    while True:                                                       # ---- evaluation
        if n >= eval_days or i >= len(cal): return dict(passed=False, paid=False, days_first=None, total=0.0)
        s = min(sizer(rule, bal, floor, start, w95), P["max_micros"])
        prev = bal; bal, br = day(bal, floor, s, pnl1[i], worst1[i], P); i += 1; n += 1
        if br: return dict(passed=False, paid=False, days_first=None, total=0.0)
        best = max(best, bal - prev); floor = max(floor, min(bal - P["dd"], start + P["lock"]))
        prof = bal - start
        if prof >= P["target"] and n >= P["min_days"] and best <= P["cons"] * prof: break
    days_eval = n; bal = start; floor = start - P["dd"]; wins = 0; total = 0.0; first = None; npay = 0   # ---- funded
    for k in range(funded_days):
        if i >= len(cal): break
        s = min(sizer(rule, bal, floor, start, w95), P["max_micros"])
        if P.get("intraday_funded") is True:     # floor follows the day's best mark first (conservative), then the worst mark is checked
            floor = max(floor, min(bal + s * best1[i] - P["dd"], start + P["lock"]))
        sod = bal; prev = bal; bal, br = day(bal, floor, s, pnl1[i], worst1[i], P); i += 1
        if br: break
        if P.get("intraday_funded") == "after":  # optimistic bound: worst checked against the old floor, the best mark raises it after
            floor = max(floor, min(sod + s * best1[i] - P["dd"], start + P["lock"]))
        if P.get("buffer"):              # LucidDaily: any day, everything above start + buffer, min $500
            floor = max(floor, min(bal - P["dd"], start + P["lock"]))
            if bal - (start + P["buffer"]) >= P["min_profit"]:
                amt = bal - (start + P["buffer"]); bal -= amt; total += amt; npay += 1
                if first is None: first = days_eval + k + 1
            continue
        if bal - prev >= P["win_min"]: wins += 1
        floor = max(floor, min(bal - P["dd"], start + P["lock"]))
        prof = bal - start
        if wins >= P["win_days"] and prof >= max(P["min_profit"], 1.0):
            amt = min(P["cap"], P["share"] * prof); bal -= amt; total += amt; wins = 0; npay += 1
            if first is None: first = days_eval + k + 1
    return dict(passed=True, paid=total > 0, days_first=first, total=total, npay=npay)
RULES = sys.argv[3].split(",") if len(sys.argv) > 3 else ["F2", "SCALE250", "ROOM", "F40"]
starts = list(range(0, len(cal), 5)); starts = [i for i in starts if cal[i] <= 20250430]
half = np.array([cal[i] < 20200101 for i in starts])
print(f"Starts every 5th trading day 2014-01 → 2025-04: {len(starts)}; eval ≤ 60 days, funded 120; cap 40 micros (4 minis)\n")
for path in sorted(glob.glob(f"{DD}/day_*.csv")):
    name = os.path.basename(path)[4:-4]; pnl1, worst1, best1 = load(path)
    pre = np.array([cal[i] < 20200101 for i in range(len(cal))]); w95 = -np.percentile(worst1[pre & (worst1 < 0)], 5)
    print(f"== trades: {name}   (1-micro day: mean ${pnl1[pnl1!=0].mean():.1f}, sd ${pnl1[pnl1!=0].std():.0f}, 95th-pct worst ${w95:.0f})")
    for pn, P in PLANS.items():
        for rule in RULES:
            R = [run(rule, pnl1, worst1, P, w95, i, best1=best1) for i in starts]
            tot = np.array([r["total"] for r in R]); paid = np.array([r["paid"] for r in R]); ps = np.array([r["passed"] for r in R])
            dfp = [r["days_first"] for r in R if r["days_first"]]; npy = np.mean([r.get("npay", 0) for r in R if r["paid"]]) if paid.any() else 0
            print(f"   {pn:15s} {rule:9s} pass {100*ps.mean():5.1f}%  paid {100*paid.mean():5.1f}%  days to 1st payout {np.median(dfp) if dfp else float('nan'):5.0f}  "
                  f"payouts/paid acct {npy:4.1f}  $/eval {tot.mean():6.0f}  (2014-19 starts {tot[half].mean():6.0f} / 2020-25 {tot[~half].mean():6.0f})")
