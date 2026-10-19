FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{if($1>=FROM&&(!TO||$1<TO))ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
$1>=FROM&&(!TO||$1<TO)&&($1 in atr)&&atr[$1]>0{d=$1; n=++c[d]; sc=RISK/($10*atr[d]); E[d,n]=$2; X[d,n]=$3; N[d,n]=sc*($5-COST); M[d,n]=sc*$7}
END{ for(d in c){pl=0; lo=0; for(i=1;i<=c[d];i++){pl+=N[d,i]; cb=0; for(j=1;j<=c[d];j++) if(X[d,j]<=E[d,i]) cb+=N[d,j]; v=cb+M[d,i]; if(v<lo)lo=v} PL[d]=pl; LO[d]=lo}
 m=asorti(PL,D)
 # eval pass rate (same as sim4)
 for(i=1;i<=ns;i++){k=0; for(t=1;t<=m;t++) if(D[t]>=ses[i]){k=t;break} if(!k)continue; st[i]=k }
 for(i=1;i<=ns;i++){ if(!(i in st))continue; k=st[i]; e=0;r="O"
  for(t=k;t<=m;t++){d=D[t]; if(e+LO[d]<=-300||LO[d]<=-150){r="F";break} e+=PL[d]; if(e<=-300){r="F";break} if(e>=500){r="P";break}}
  if(r!="O"){ne++; if(r=="P")np++}
  # funded from the same start
  e=0; paid=0; br=0; for(t=k;t<=m;t++){d=D[t]; if(e+LO[d]<=-300||LO[d]<=-150){br=1;break} e+=PL[d]; if(e<=-300){br=1;break} if(e>=CL){paid+=0.8*e; e=0}}
  nf++; tp+=paid; td+=t-k+1; nb+=br; life[nf]=t-k+1; if(br){nbb++; lb[nbb]=t-k+1} }
 asort(lb); pr=np/ne; mp=tp/nf
 printf "%-14s $%-3d claim>=%-4d | eval pass %4.1f%% | funded: paid $%6.0f/account, breached %4.1f%%, median life %3d days | value per fee $%5.0f | $%5.0f per account-year\n", NAME, RISK, CL, 100*pr, mp, 100*nb/nf, lb[int((nbb+1)/2)], pr*mp-40, tp/td*252 }
