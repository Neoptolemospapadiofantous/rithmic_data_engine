FILENAME~/VIX_H/{if(FNR>1){split($1,a,"/"); d=a[3] a[1] a[2]; vd[++nv]=d; v[d]=$5}; next}
FILENAME~/VIX3M/{if(FNR>1){split($1,a,"/"); w[a[3] a[1] a[2]]=$5}; next}
FILENAME~/atr/{split($0,z,","); atr[z[1]]=z[2]; next}
$2=="3ny"{ d=$1; if(!(d in atr)||atr[d]<=0) next
  # prior VIX close strictly before d
  while(j<nv && vd[j+1]<d) j++; pv=vd[j]; if(!(pv in v)) next; V=v[pv]; T=(pv in w && w[pv]>0)? V/w[pv] : 0
  O=$3; P=$4; C=$5; H=$6; L=$7; r=H-L; if(r<=0) next; A=atr[d]
  b=(V<15)?"1 VIX<15":(V<20)?"2 VIX15-20":(V<25)?"3 VIX20-25":"4 VIX>=25"; ts=(T==0)?"":(T>=1)?"backward":"contango"
  per=(d<20190101)?"10-18":"19-26"
  for(i=1;i<=2;i++){ g=(i==1? b : (ts==""?"":"5 "ts)); if(g=="")continue; k=g SUBSEP per
   n[k]++; ra[k]+=r/A; eff=(C>O?C-O:O-C)/r; if(eff>=0.5)tr[k]++; if(eff<0.2)ch[k]++
   m1=P-O; if((m1>0?m1:-m1)>=0.15*A){s[k]++; if((m1>0?1:-1)*(C-P)>0)co[k]++} } }
END{ printf "%-14s %-6s %5s | %-10s %-7s %-7s | %s\n","prior close","period","days","range/ATR","trend","chop","strong 10:30 start continues"
 for(k in n){split(k,z,SUBSEP); printf "%-14s %-6s %5d | %6.2f     %4.1f%%   %4.1f%%   | %4.1f%% of %d\n", substr(z[1],3), z[2], n[k], ra[k]/n[k], 100*tr[k]/n[k], 100*ch[k]/n[k], 100*co[k]/s[k], s[k]} }
