# day P&L / low computed at $1 risk, scaled by the day's chosen risk.
# MODE=eval: risk = min(RMAX, (300+e)/DE) (DE=0 -> RMAX fixed). MODE=fund: claim when e>=T to buffer B; risk = DF? clamp((300+e)/DF,20,80) : RF
FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{if($1>=FROM)ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
$1>=FROM&&($1 in atr)&&atr[$1]>0{d=$1; n=++c[d]; sc=1/($10*atr[d]); E[d,n]=$2; X[d,n]=$3; N[d,n]=sc*($5-COST); M[d,n]=sc*$7}
END{ for(d in c){pl=0; lo=0; for(i=1;i<=c[d];i++){pl+=N[d,i]; cb=0; for(j=1;j<=c[d];j++) if(X[d,j]<=E[d,i]) cb+=N[d,j]; v=cb+M[d,i]; if(v<lo)lo=v} PL[d]=pl; LO[d]=lo}
 m=asorti(PL,D)
 for(i=1;i<=ns;i++){k=0; for(t=1;t<=m;t++) if(D[t]>=ses[i]){k=t;break} if(!k)continue
  if(MODE=="eval"){ e=0;r="O";td=0; for(t=k;t<=m;t++){td++; d=D[t]; R=(DE>0)?(300+e)/DE:RMAX; if(R>RMAX)R=RMAX; if(R<RMIN)R=RMIN
     if(e+R*LO[d]<=-300||R*LO[d]<=-150){r="F";break} e+=R*PL[d]; if(e<=-300){r="F";break} if(e>=500){r="P";break}}
    if(r=="O")continue; st++; if(r=="P"){p++; dd[p]=td} }
  else { e=0; paid=0; br=0; for(t=k;t<=m;t++){d=D[t]; R=RF; if(DF>0){R=(300+e)/DF; if(R<20)R=20; if(R>80)R=80}
     if(e+R*LO[d]<=-300||R*LO[d]<=-150){br=1;break} e+=R*PL[d]; if(e<=-300){br=1;break} if(e>=T){paid+=0.8*(e-B); e=B}}
    nf++; tp+=paid; nb+=br; tdays+=t-k+1 } }
 if(MODE=="eval"){asort(dd); printf "%s pass %.1f%% median %d days\n",NAME,100*p/st,dd[int((p+1)/2)]}
 else printf "%s paid $%.0f/account breached %.1f%% $%.0f per account-year\n",NAME,tp/nf,100*nb/nf,tp/tdays*252 }
