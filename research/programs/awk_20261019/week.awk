FILENAME~/atr/{split($0,z,","); atr[z[1]]=z[2]; next}
$2!="3ny"{next}
{ d=$1; if(!(d in atr)||atr[d]<=0) next; t=mktime(substr(d,1,4)" "substr(d,5,2)" "substr(d,7,2)" 12 0 0"); wd=strftime("%u",t); wk=strftime("%G-%V",t)
  O=$3; C=$5; H=$6; L=$7; A=atr[d]; per=(d<20190101)?"2010-18":"2019-26"
  # daily by weekday
  r=H-L; if(r>0){ k=wd SUBSEP per; dn[k]++; dm[k]+=(C-O)/A; if(C>O)du[k]++; dr[k]+=r/A; e=(C>O?C-O:O-C)/r; if(e>=0.5)dt[k]++; if(e<0.2)dc[k]++ }
  if(!(wk in WO)){WO[wk]=O; WH[wk]=H; WL[wk]=L; WA[wk]=0; WN[wk]=0; ord[++nw]=wk; WP[wk]=per; MO[wk]=""; }
  if(H>=WH[wk]){WH[wk]=H; HD[wk]=wd} if(L<=WL[wk]){WL[wk]=L; LD[wk]=wd}
  WC[wk]=C; WA[wk]+=A; WN[wk]++; if(wd==1){MC[wk]=C; MOo[wk]=O} if(wd==2) TC[wk]=C; FW[wk]=wd }
END{
 print "== B. DAILY by weekday (RTH): mean move ATR, up %, range/ATR, trend days %, chop days %"
 split("Mon Tue Wed Thu Fri",N," ")
 for(w=1;w<=5;w++) for(p=1;p<=2;p++){per=(p==1)?"2010-18":"2019-26"; k=w SUBSEP per; printf "  %s %s n %4d | move %+6.3f | up %4.1f%% | range %.2f | trend %4.1f%% | chop %4.1f%%\n",N[w],per,dn[k],dm[k]/dn[k],100*du[k]/dn[k],dr[k]/dn[k],100*dt[k]/dn[k],100*dc[k]/dn[k]}
 print "== A. WEEKLY"
 for(i=1;i<=nw;i++){wk=ord[i]; if(WN[wk]<4) continue; p=WP[wk]; A=WA[wk]/WN[wk]; rr=WH[wk]-WL[wk]; if(rr<=0)continue
  n[p]++; R[p]+=rr/A; mv=(WC[wk]-WO[wk])/A; M[p]+=mv; if(mv>0)U[p]++; e=(WC[wk]>WO[wk]?WC[wk]-WO[wk]:WO[wk]-WC[wk])/rr; if(e>=0.5)T[p]++; if(e<0.2)Ch[p]++
  hd[p,HD[wk]]++; ld[p,LD[wk]]++
  if(wk in MC){ m1=MC[wk]-MOo[wk]; if((m1>0?m1:-m1)>=0.3*A){mn[p]++; if(m1*(WC[wk]-MC[wk])>0)mc[p]++; if(m1*(WC[wk]-MOo[wk])>0)mw[p]++}}
  if((wk in MC)&&(wk in TC)){ m2=TC[wk]-MOo[wk]; if((m2>0?m2:-m2)>=0.5*A){tn[p]++; if(m2*(WC[wk]-TC[wk])>0)tc[p]++}}
  # next week continuation after a trend week
  if(i<nw){nx=ord[i+1]; if(WN[nx]>=4 && e>=0.5){ xn[p]++; if((WC[wk]-WO[wk])*(WC[nx]-WO[nx])>0)xc[p]++ }}
  if(e>=0.5) lastT[p]++ }
 for(p in n){ printf "  %s weeks %d | range %.2f daily ATR | open->close %+.3f ATR, up %.1f%% | trend weeks %.1f%% | chop weeks %.1f%%\n",p,n[p],R[p]/n[p],M[p]/n[p],100*U[p]/n[p],100*T[p]/n[p],100*Ch[p]/n[p]
  printf "     week HIGH set on: Mon %.0f%% Tue %.0f%% Wed %.0f%% Thu %.0f%% Fri %.0f%% | week LOW set on: Mon %.0f%% Tue %.0f%% Wed %.0f%% Thu %.0f%% Fri %.0f%%\n",100*hd[p,1]/n[p],100*hd[p,2]/n[p],100*hd[p,3]/n[p],100*hd[p,4]/n[p],100*hd[p,5]/n[p],100*ld[p,1]/n[p],100*ld[p,2]/n[p],100*ld[p,3]/n[p],100*ld[p,4]/n[p],100*ld[p,5]/n[p]
  printf "     Monday moved >= 0.3 ATR (%d weeks): Tue->Fri continued %.1f%%, week closed on Monday's side %.1f%%\n",mn[p],100*mc[p]/mn[p],100*mw[p]/mn[p]
  printf "     Mon+Tue moved >= 0.5 ATR (%d weeks): Wed->Fri continued %.1f%%\n",tn[p],100*tc[p]/tn[p]
  printf "     after a trend week (%d): next week same direction %.1f%%\n",xn[p],100*xc[p]/xn[p] } }
