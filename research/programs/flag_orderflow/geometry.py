# flag windows (UTC µs) from the wide15 export + 1-minute cache; validates exported prices against bars.
import csv, sys, bisect, datetime as dt
from zoneinfo import ZoneInfo
ET=ZoneInfo("America/New_York"); UTC=dt.timezone.utc
EXP, CACHE, OUT = sys.argv[1:4]
LO, HI = 20230301, 20260318
bars={}  # ET-wall epoch minute -> (h,l)
t0=int(dt.datetime(2023,2,25,tzinfo=UTC).timestamp()); t1=int(dt.datetime(2026,3,20,tzinfo=UTC).timestamp())
with open(CACHE) as f:
    next(f)
    for ln in f:
        p=ln.split(",",5); ts=int(p[0])
        if t0<=ts<=t1: bars[ts//60]=(float(p[2]),float(p[3]))
keys=sorted(bars)
def et_epoch_min(date,tday):  # tday = minutes since 18:00 ET of the previous calendar day
    d=dt.date(date//10000,date//100%100,date%100)-dt.timedelta(days=1)
    w=dt.datetime(d.year,d.month,d.day,18,0)+dt.timedelta(minutes=tday)
    return int(w.replace(tzinfo=UTC).timestamp())//60, w
def to_utc_us(w): return int(w.replace(tzinfo=ET).astimezone(UTC).timestamp()*1e6)
out=open(OUT,"w",newline=""); W=csv.writer(out)
W.writerow(["fid","date","side","tf","grade","book2","x2","x4","r","t_base","t_pole","t_peak","t_cut","t_fill","pole_hi","pole_lo","flag_x","entry","pole_min_found","peak_ok","t_open"])
n=bad=0
for i,r in enumerate(csv.DictReader(open(EXP))):
    d=int(r["date"])
    if r["ok"]!="1" or not LO<=d<=HI: continue
    side=int(r["side"]); tf=int(r["tf"]); reg=int(r["reg_min"]); em=int(r["entry_min"])
    H=float(r["pole_hi"]); L=float(r["pole_lo"]); FX=float(r["flag_lo"])
    cutm,cutw=et_epoch_min(d,reg)          # start of the registration minute (ET-wall epoch min)
    j=bisect.bisect_left(keys,cutm)-1      # last bar before the cut-off
    # peak = most recent bar before cut-off touching the pole extreme; start = most recent bar before peak at the other end
    ext = H if side>0 else L; oth = L if side>0 else H
    pk=None
    for k in range(j, max(j-240,0), -1):
        h,l=bars[keys[k]]
        if (side>0 and h>=ext-0.01) or (side<0 and l<=ext+0.01): pk=k; break
    st=None
    if pk is not None:
        for k in range(pk, max(pk-240,0), -1):
            h,l=bars[keys[k]]
            if (side>0 and l<=oth+0.01) or (side<0 and h>=oth-0.01): st=k; break
    n+=1
    if pk is None or st is None: bad+=1; continue
    tm=lambda k: dt.datetime.fromtimestamp(keys[k]*60,UTC).replace(tzinfo=None)
    pole_w=tm(st); peak_w=tm(pk)+dt.timedelta(minutes=1)    # peak bar inclusive
    fill_w=et_epoch_min(d,em)[1]
    b2 = tf>=4 and float(r["x2"])>=3 and 5<=float(r["x4"])*100<38 and r["grade"] in("1","2")
    W.writerow([i,d,side,tf,r["grade"],int(b2),r["x2"],r["x4"],r["r"],
                to_utc_us(pole_w-dt.timedelta(minutes=30)),to_utc_us(pole_w),to_utc_us(peak_w),to_utc_us(cutw),to_utc_us(fill_w),
                H,L,FX,r["entry"],int((cutm-keys[st])),1,
                to_utc_us(dt.datetime(cutw.year,cutw.month,cutw.day,9,30) - (dt.timedelta(days=1) if cutw.hour*60+cutw.minute<570 else dt.timedelta(0)))])
print("filled flags in window",n,"geometry not found",bad)
