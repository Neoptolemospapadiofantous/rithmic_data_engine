FILENAME~/atr/{split($0,z,","); atr[z[1]]=z[2]; next}
FILENAME~/nq_sessions/{ if($2!="3ny") next; t=mktime(substr($1,1,4)" "substr($1,5,2)" "substr($1,7,2)" 12 0 0"); wk=strftime("%G-%V",t)
  if(!(wk in WO)) WO[wk]=$3; prevC[$1]=lastC[wk]; wso[$1]=(lastC[wk]=="")?"":lastC[wk]-WO[wk]; lastC[wk]=$5; wday[$1]=strftime("%u",t); next }
FNR==1{next}
{ split($0,f,","); d=f[1]; if(!(d in atr)||!(d in wday)) next; A=atr[d]; dir=f[4]; net=f[5]-1; p=(d<20190101)?1:(d<20250101)?2:3
  base[p]+=net; nb[p]++
  split("Mon Tue Wed Thu Fri",N," "); k="day " N[wday[d]]; s[k,p]+=net; n[k,p]++; cells[k]
  if(wso[d]==""){g="week: Monday (no week yet)"} else { m=wso[d]*dir; g=(m>=0.5*A)?"week so far same way":(m<=-0.5*A)?"week so far opposite":"week so far small" }
  s[g,p]+=net; n[g,p]++; cells[g] }
END{ printf "%-28s | %-15s | %-15s | %-15s\n", NAME" base", sprintf("%+.2f (%d)",base[1]/nb[1],nb[1]), sprintf("%+.2f (%d)",base[2]/nb[2],nb[2]), sprintf("%+.2f (%d)",base[3]/nb[3],nb[3])
 for(c in cells){ for(p=1;p<=3;p++) v[p]=(n[c,p]?s[c,p]/n[c,p]:0)
  r=(v[1]>=1.25*base[1]/nb[1]&&v[2]>=1.25*base[2]/nb[2]&&v[1]>0)?"SIZE-UP":(v[1]<0&&v[2]<0)?"SKIP":"-"
  printf "%-28s | %+7.2f (%4d) | %+7.2f (%4d) | %+7.2f (%4d) | %s\n", c, v[1], n[c,1], v[2], n[c,2], v[3], n[c,3], r } }
