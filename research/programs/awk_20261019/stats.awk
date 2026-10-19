FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{ses[++ns]=$1; idx[$1]=ns; if($1<20190101)n1++; else if($1<20250101)n2++; else n3++; next}
FNR==1 && $1=="date"{next}
{net=$5-2; p=($1<20190101)?1:($1<20250101)?2:3; tot[p]+=net; nt[p]++; dp[$1]+=net; dm[$1]+=$7}
END{ w=0; for(k in dp) if(k>=20190101&&k<20250101){if(dp[k]>=20)g++; if(dp[k]<w)w=dp[k]}
 n=asorti(dp,ks); for(j=1;j<=n;j++){k=ks[j]; if(k<20190101||k>=20250101) continue; eq+=dp[k]; if(eq>pk)pk=eq; if(pk-eq>mdd)mdd=pk-eq}
 for(j=1;j<=n;j++){k=ks[j]; if(k<20190101||!(k in atr)||atr[k]<=0) continue; s=RISK/(0.35*atr[k]); D[++m]=k; PL[m]=s*dp[k]; MA[m]=s*dm[k]}
 for(i=1;i<=ns;i++){ if(ses[i]<20190101) continue; k=0; for(t=1;t<=m;t++) if(D[t]>=ses[i]){k=t;break} if(!k) continue; e=0; r="O";
  for(t=k;t<=m;t++){ if(e+MA[t]<=-300||MA[t]<=-150){r="F";break} e+=PL[t]; if(e<=-300){r="F";break} if(e>=500){r="P";break} }
  if(r=="O") continue; st++; if(r=="P"){p2++; dd[p2]=idx[D[t]]-i+1} }
 asort(dd); printf "%-4s | pts/session %+5.2f / %+6.2f / %+6.2f | trades/session %.2f | >=+20 %2.0f%% | worst day %4.0f | maxDD19-24 %4.0f | pts/DD %.4f | Vest $%d: pass %4.1f%% median %d days\n", NAME, tot[1]/n1, tot[2]/n2, tot[3]/n3, nt[2]/n2, 100*g/n2, w, mdd, (tot[2]/n2)/mdd, RISK, 100*p2/st, dd[int((p2+1)/2)]}
