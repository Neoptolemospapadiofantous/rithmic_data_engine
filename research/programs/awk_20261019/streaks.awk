# Book F streaks: monthly / weekly autocorrelation, P(next month > 0 | this month), and the equity-curve filter (trade only when the
# book's own last-N-trading-day P&L > 0). Input: sessions_all.csv (dates), F trades (date,...,gross in $5). 2 pt cost. Var N (20/60).
FILENAME~/sessions/{ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
{ dp[$1]+=$5-2 }
END{ for(i=1;i<=ns;i++){ d=ses[i]; m=substr(d,1,6); mp[m]+=dp[d]; if(!(m in seen)){seen[m]=1; mo[++nm]=m} }
  for(e=0;e<2;e++){ c=0; sx=sy=sxx=syy=sxy=0; pp=pn=np=nn=0
   for(j=1;j<nm;j++){ m=mo[j]; if((e==0)!=(m<"201901")) continue; x=mp[m]; y=mp[mo[j+1]]; c++; sx+=x; sy+=y; sxx+=x*x; syy+=y*y; sxy+=x*y
    if(x>0){ if(y>0)pp++; else pn++ } else { if(y>0)np++; else nn++ } }
   printf "%s monthly: autocorr %+.3f (n %d) | P(next>0 | this>0) %.1f%% vs P(next>0 | this<=0) %.1f%%\n", (e==0?"2010-18":"2019-26"), (c*sxy-sx*sy)/sqrt((c*sxx-sx^2)*(c*syy-sy^2)), c, 100*pp/(pp+pn), 100*np/(np+nn) }
  # filter
  for(i=1;i<=ns;i++){ d=ses[i]; w=0; for(j=i-N;j<i;j++) if(j>=1) w+=dp[ses[j]]; on=(i>N && w>0)
   p=(d<20190101)?1:(d<20250101)?2:3; all[p]+=dp[d]; n[p]++; if(on){f[p]+=dp[d]; nf[p]++} }
  printf "filter last %d days > 0: pts/session all-days %+.2f / %+.2f / %+.2f -> filtered %+.2f / %+.2f / %+.2f (per session incl. days off); days on %.0f%% / %.0f%% / %.0f%%\n", N, all[1]/n[1], all[2]/n[2], all[3]/n[3], f[1]/n[1], f[2]/n[2], f[3]/n[3], 100*nf[1]/n[1], 100*nf[2]/n[2], 100*nf[3]/n[3] }
