# Overnight long holds from the session table (sess.awk output) → trade CSV for the Vest sims (date = the next trading day, k = 1.5).
# MODE 1: every day close→next open; 2: weak close (IBS ≤ 0.3) close→next open; 3: weak close + prior-day VIX ≥ 20, close→next close.
# Run with TZ=UTC gawk (epochs are ET wall clock, like the 1m cache). Files: atr (date,atr), VIX_History.csv, sessions table.
FILENAME~/atr/{split($0,z,","); A[z[1]]=z[2]; next}
FILENAME~/VIX/{ if(FNR>1){split($0,z,","); split(z[1],q,"/"); vd[++nv]=q[3] q[1] q[2]; vx[q[3] q[1] q[2]]=z[5]} next }
{ if($2=="1asia"){aO[$1]=$3; aL[$1]=$7; aH[$1]=$6} else if($2=="2london"){lL[$1]=$7; lH[$1]=$6} else { ny[++n]=$1; nO[$1]=$3; nC[$1]=$5; nH[$1]=$6; nL[$1]=$7 } }
END{ j=1
 for(i=1;i<n;i++){ d=ny[i]; e=ny[i+1]; if(!(e in aO)||!(e in lL)||!(e in A)) continue; a=A[e]; C=nC[d]; r=nH[d]-nL[d]; if(r<=0) continue; ibs=(C-nL[d])/r
  while(j<nv && vd[j+1]<d) j++; vix=vx[vd[j]]
  if(MODE>=2 && ibs>0.3) continue; if(MODE==3 && !(vix>=20)) continue
  stop=C-1.5*a; lo=aL[e]; if(lL[e]<lo)lo=lL[e]; hi=aH[e]; if(lH[e]>hi)hi=lH[e]
  if(MODE==3){ if(nL[e]<lo)lo=nL[e]; if(nH[e]>hi)hi=nH[e]; X=nC[e] } else { if(nO[e]<lo)lo=nO[e]; X=nO[e] }
  if(lo<=stop){ fill=(aO[e]<stop)?aO[e]:stop; g=fill-C } else g=X-C
  ent=mktime(substr(d,1,4)" "substr(d,5,2)" "substr(d,7,2)" 15 59 0"); ex=mktime(substr(e,1,4)" "substr(e,5,2)" "substr(e,7,2)" "(MODE==3?"15 59":"09 30")" 0")
  printf "%s,%d,%d,1,%.2f,%.2f,%.2f,%.2f,9,1.5\n", e, ent, ex, g, g-1, (lo<=stop?-1.5*a:lo-C), hi-C } }
