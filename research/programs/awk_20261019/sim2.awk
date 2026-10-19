# MODE=sum: day low = sum of all trades' MAE (strict). MODE=seq: day low = min over trades of (closed-before-entry P&L + own MAE) (lenient).
FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{if($1>=FROM)ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
$1>=FROM{n=++c[$1]; E[$1,n]=$2; X[$1,n]=$3; N[$1,n]=$5-2; M[$1,n]=$7}
END{ for(d in c){ if(!(d in atr)||atr[d]<=0)continue; s=RISK/((SK?SK:0.35)*atr[d]); pl=0; lo=0; sm=0
   for(i=1;i<=c[d];i++){pl+=N[d,i]; sm+=M[d,i]; cb=0; for(j=1;j<=c[d];j++) if(X[d,j]<=E[d,i]) cb+=N[d,j]; v=cb+M[d,i]; if(v<lo)lo=v}
   PL[d]=s*pl; LO[d]=s*(MODE=="sum"?sm:lo) }
 m=asorti(PL,D)
 for(i=1;i<=ns;i++){k=0; for(t=1;t<=m;t++) if(D[t]>=ses[i]){k=t;break} if(!k)continue; e=0;r="O";td=0
  for(t=k;t<=m;t++){td++; d=D[t]; if(e+LO[d]<=-300||LO[d]<=-150){r="F";break} e+=PL[d]; if(e<=-300){r="F";break} if(e>=500){r="P";break}}
  if(r=="O")continue; st++; if(r=="P"){p++; if(td==1)p1++; if(td<=3)p3++; dd[p]=td}}
 asort(dd); printf "%-3s risk $%3d | pass %4.1f%% | day 1 %4.1f%% | within 3 trade days %4.1f%% | median %d trade days | $ fees per pass %.0f\n",MODE,RISK,100*p/st,100*p1/st,100*p3/st,dd[int((p+1)/2)],40*st/p}
