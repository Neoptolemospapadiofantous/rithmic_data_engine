# Weakness × VIX book (docs/strategy_weakness_vix_book.md) from the session table → trade CSV for the Vest sims. Run with TZ=UTC gawk.
# weakbook_v: + SAME=1 uses the signal day's own VIX close (known before the 18:00 entry); MODE 4 = IBS <= 0.3 & VIX >= 20, 18:00 -> next close.
# MODE 1: any weakness & fear gate, 18:00 → next close.  2: any weakness, conviction stack 1–4 units (k = 1.5/units), 18:00 → next close.
# 3: any weakness & gate, hold ≤ 3 sessions, exit at the first RTH close above the previous one.  FUND = funding pts per night at ATR 320.
# Stop 1.5 ATR. Files: atr (date,atr), VIX_History.csv, sessions table (sess.awk output). Output date = the first session held.
FILENAME~/atr/{split($0,z,","); A[z[1]]=z[2]; next}
FILENAME~/VIX/{ if(FNR>1){split($0,z,","); split(z[1],q,"/"); vd[++nv]=q[3] q[1] q[2]; vc[nv]=z[5]} next }
{ if($2=="1asia"){aO[$1]=$3; aL[$1]=$7; aH[$1]=$6} else if($2=="2london"){lL[$1]=$7; lH[$1]=$6} else { ny[++n]=$1; O[n]=$3; C[n]=$5; H[n]=$6; L[n]=$7 } }
END{ au=0; ad=0; j=1
 for(i=1;i<=n;i++){ if(i>1){ ch=C[i]-C[i-1]; up=(ch>0)?ch:0; dn=(ch<0)?-ch:0; if(i==2){au=up; ad=dn} else {au=(au+up)/2; ad=(ad+dn)/2}; rsi[i]=(ad==0)?100:100-100/(1+au/ad) } }
 for(i=3;i<n-3;i++){ d=ny[i]; e=ny[i+1]; if(!(d in A)||!(e in A)||!(e in aO)) continue; a=A[d]; r=H[i]-L[i]; if(r<=0) continue; ibs=(C[i]-L[i])/r
  weak=(ibs<0.3)||(rsi[i]<20)||(C[i]<C[i-1]&&C[i-1]<C[i-2])||(C[i]<=O[i]-a); if(!weak) continue
  while(j<nv && vd[j+1]<(SAME?d+1:d)) j++; v=vc[j]; v5=(j>5)?vc[j-5]:v; m=0; c=0; for(k=j-251;k<=j;k++) if(k>=1){m++; if(vc[k]<v)c++}
  gate=(v>=18)||(v5>0 && v/v5-1>0.10)||(m>=100 && c/m>=2/3)
  if(MODE==4){ if(!(ibs<=0.3 && v>=20)) continue } else if(MODE!=2 && !gate) continue
  u=1; if(MODE==2){ u=1+(ibs<0.3)+gate+((ibs<0.3)&&gate) }
  ae=A[e]; ent=aO[e]; stop=ent-1.5*ae; lo=1e18; hi=-1e18; X=""; nights=0
  last=(MODE==3)?i+3:i+1
  for(k=i+1;k<=last;k++){ dk=ny[k]; nights++
    for(s=1;s<=3;s++){ if(s==1){ if(k>i+1 && (dk in aO) && aO[dk]<=stop && X==""){X=aO[dk]; break} sl=aL[dk]; sh=aH[dk] } else if(s==2){sl=lL[dk]; sh=lH[dk]} else {sl=L[k]; sh=H[k]}
      if(sl=="")continue; if(sl<lo)lo=sl; if(sh>hi)hi=sh; if(lo<=stop){X=stop; break} }
    if(X!="") break
    if(k==last || (MODE==3 && C[k]>C[k-1])){ X=C[k]; break } }
  g=X-ent-FUND*ae/320*nights; t0=mktime(substr(e,1,4)" "substr(e,5,2)" "substr(e,7,2)" 00 00 00")-21600; dx=ny[k]; t1=mktime(substr(dx,1,4)" "substr(dx,5,2)" "substr(dx,7,2)" 15 59 00")
  printf "%s,%d,%d,1,%.2f,%.2f,%.2f,%.2f,8,%.4f\n", e, t0, t1, g, g-1, (lo<=stop?-1.5*ae:lo-ent), hi-ent, 1.5/u } }
