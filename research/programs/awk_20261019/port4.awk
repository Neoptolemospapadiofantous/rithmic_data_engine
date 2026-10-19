# files: atr, sessions, B1 trades (tag f=1), FW trades (tag f=2)
FILENAME~/atr/{atr[$1]=$2;next} FILENAME~/sessions/{if($1>=FROM&&$1<TO)ses[++ns]=$1; next}
FNR==1&&$1=="date"{next}
FNR==1{f++} 
($1 in atr)&&atr[$1]>0&&$1>=FROM&&$1<TO{d=$1; n=++c[f,d]; sc=1/($10*atr[d]); E[f,d,n]=$2; X[f,d,n]=$3; N[f,d,n]=sc*($5-(SCALE?COST*atr[d]/320:COST)); M[f,d,n]=sc*$7}
END{ for(g=1;g<=2;g++) for(i=1;i<=ns;i++){d=ses[i]; pl=0; lo=0; for(a=1;a<=c[g,d];a++){pl+=N[g,d,a]; cb=0; for(b=1;b<=c[g,d];b++) if(X[g,d,b]<=E[g,d,a]) cb+=N[g,d,b]; v=cb+M[g,d,a]; if(v<lo)lo=v} PL[g,i]=pl; LO[g,i]=lo}
 for(s=1;s<=ns;s++){ ym=substr(ses[s],1,6); if(ym in done) continue; if(ses[s]>=LAST) break; done[ym]=1
  delete fe; delete ee; nfu=0; nev=0; fees=0; paid=0; cash=0; worst=0; fsum=0; days=0
  for(i=s;i<=ns && ses[i]<ses[s]+10000;i++){ days++
   # funded
   for(a in fe){ R=RF; if(fe[a]+R*LO[2,i]<=-300||R*LO[2,i]<=-150){delete fe[a]; continue} fe[a]+=R*PL[2,i]; if(fe[a]<=-300){delete fe[a]; continue} if(fe[a]>=T){paid+=0.8*(fe[a]-B); cash+=0.8*(fe[a]-B); fe[a]=B} }
   nf=length(fe); fsum+=nf
   # evals (first-trade at $120)
   newp=0; for(a in ee){ R=ER; if(ee[a]+R*LO[1,i]<=-300||R*LO[1,i]<=-150){delete ee[a]; continue} ee[a]+=R*PL[1,i]; if(ee[a]<=-300){delete ee[a]; continue} if(ee[a]>=500){delete ee[a]; newp++} }
   for(q=1;q<=newp && length(fe)<10;q++) fe["f" i "_" q]=0
   buy=10-length(fe)-length(ee); if(buy>MAXB)buy=MAXB; for(q=1;q<=buy;q++){ee["e" i "_" q]=0; fees+=40; cash-=40}
   if(cash<worst)worst=cash }
  printf "%s fees $%5d paid $%6.0f net $%6.0f worst cash $%5.0f avg funded %4.1f\n", ym, fees, paid, paid-fees, worst, fsum/days } }
