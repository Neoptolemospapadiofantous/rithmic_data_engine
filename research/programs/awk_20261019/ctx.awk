FILENAME~/atr/{split($0,z,","); atr[z[1]]=z[2]; next}
FILENAME~/nq_sessions/{ if($2!="3ny") next; nd++; D[nd]=$1; O[nd]=$3; C[nd]=$5; R[nd]=$6-$7; idx[$1]=nd; next}
FNR==1{next}
{ split($0,f,","); d=f[1]; i=idx[d]; if(i<7||!(d in atr)) next; A=atr[d]; dir=f[4]; net=f[5]-1
  y=i-1; e=(R[y]>0)?(C[y]-O[y])/R[y]:0; a=(e>=0.5*1)?((dir>0)?"same":"opp"):(e<=-0.5)?((dir<0)?"same":"opp"):"none"
  m=C[i-1]-C[i-6]; b=(m*dir>=0.5*A)?"same":(m*dir<=-0.5*A)?"opp":"small"
  p=(d<20190101)?1:(d<20250101)?2:3; base[p]+=net; nb[p]++
  k="a yday trend " a SUBSEP p; s[k]+=net; n[k]++; k="b 5-day move " b SUBSEP p; s[k]+=net; n[k]++; cells["a yday trend " a]; cells["b 5-day move " b] }
END{ printf "%-22s | %-16s | %-16s | %-16s | rule\n", NAME" base", sprintf("%+.2f (%d)",base[1]/nb[1],nb[1]), sprintf("%+.2f (%d)",base[2]/nb[2],nb[2]), sprintf("%+.2f (%d)",base[3]/nb[3],nb[3])
 for(c in cells){ for(p=1;p<=3;p++){k=c SUBSEP p; v[p]=(n[k]?s[k]/n[k]:0)}
  r=(v[1]>=1.25*base[1]/nb[1] && v[2]>=1.25*base[2]/nb[2] && v[1]>0)?"SIZE-UP":(v[1]<0&&v[2]<0)?"SKIP":"-"
  printf "%-22s | %+7.2f (%4d)   | %+7.2f (%4d)   | %+7.2f (%4d)   | %s\n", c, v[1], n[c SUBSEP 1], v[2], n[c SUBSEP 2], v[3], n[c SUBSEP 3], r } }
