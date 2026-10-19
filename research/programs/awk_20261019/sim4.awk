# like full/sim2.awk MODE=seq, but each trade carries its own stop multiple in field 10; size = RISK/(k*atr)
FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{if($1>=FROM)ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
$1>=FROM&&($1 in atr)&&atr[$1]>0{d=$1; n=++c[d]; sc=RISK/($10*atr[d]); E[d,n]=$2; X[d,n]=$3; N[d,n]=sc*($5-2); M[d,n]=sc*$7; P1[d,n]=$5-2}
END{ for(d in c){pl=0; lo=0; pp=0; for(i=1;i<=c[d];i++){pl+=N[d,i]; cb=0; for(j=1;j<=c[d];j++) if(X[d,j]<=E[d,i]) cb+=N[d,j]; v=cb+M[d,i]; if(v<lo)lo=v} PL[d]=pl; LO[d]=lo}
 m=asorti(PL,D)
 for(i=1;i<=ns;i++){k=0; for(t=1;t<=m;t++) if(D[t]>=ses[i]){k=t;break} if(!k)continue; e=0;r="O";td=0
  for(t=k;t<=m;t++){td++; d=D[t]; if(e+LO[d]<=-300||LO[d]<=-150){r="F";break} e+=PL[d]; if(e<=-300){r="F";break} if(e>=500){r="P";break}}
  if(r=="O")continue; st++; if(r=="P"){p++; dd[p]=td}}
 asort(dd); printf "%-10s risk $%d | pass %4.1f%% | median %d trade days | fees per pass $%.0f\n",NAME,RISK,100*p/st,dd[int((p+1)/2)],40*st/p}
