# one pass over NQ 1m: sessions Asia 18:00-03:00 (trading date = next day), London 03:00-09:30, NY 09:30-16:00 (ET)
function flush(k,   u,dn) {}
{ t=$1; m=strftime("%H",t)*60+strftime("%M",t)
  if(m>=1080||m<180){s=1; d=strftime("%Y%m%d",t+(m>=1080?21600:0))} else if(m<570){s=2; d=strftime("%Y%m%d",t)} else if(m<960){s=3; d=strftime("%Y%m%d",t)} else next
  if(s!=3 && pend){ pnyH=nyH; pnyL=nyL; pend=0 }
  k=d SUBSEP s
  if(!(k in O)){ O[k]=$2; H[k]=$3; L[k]=$4; ord[++n]=k; first[k]="" }
  # levels for this session, fixed at its start
  if(!(k in set)){ set[k]=1
    if(s==1){ lh[k]=pnyH; ll[k]=pnyL }            # Asia: prior NY RTH
    if(s==2){ lh[k]=aH; ll[k]=aL }                 # London: Asia range
    if(s==3){ lh[k]=(aH>lnH?aH:lnH); ll[k]=(aL<lnL?aL:lnL); lh2[k]=pnyH; ll2[k]=pnyL } } # NY: overnight + prior NY
  if($3>H[k])H[k]=$3; if($4<L[k])L[k]=$4; C[k]=$5
  if(lh[k]!="" && first[k]==""){ if($3>lh[k]&&$4<ll[k])first[k]="both"; else if($3>lh[k])first[k]="up"; else if($4<ll[k])first[k]="down" }
  if(lh2[k]!="" && first2[k]==""){ if($3>lh2[k]&&$4<ll2[k])first2[k]="both"; else if($3>lh2[k])first2[k]="up"; else if($4<ll2[k])first2[k]="down" }
  # maintain running session ranges for the next session's levels
  if(s==1){ if(cur!=k){aH=$3;aL=$4; onH=$3; onL=$4}; if($3>aH)aH=$3; if($4<aL)aL=$4; if($3>onH)onH=$3; if($4<onL)onL=$4 }
  if(s==2){ if(cur!=k){lnH=$3;lnL=$4}; if($3>lnH)lnH=$3; if($4<lnL)lnL=$4 }
  if(s==3){ if(cur!=k){nyH=$3; nyL=$4}; if($3>nyH)nyH=$3; if($4<nyL)nyL=$4; pend=1 }
  if(s!=3 && pend){ pnyH=nyH; pnyL=nyL; pend=0 }
  cur=k }
END{ for(i=1;i<=n;i++){ k=ord[i]; split(k,z,SUBSEP); d=z[1]; s=z[2]
   for(v=1;v<=2;v++){ hi=(v==1)?lh[k]:lh2[k]; lo=(v==1)?ll[k]:ll2[k]; if(hi==""||lo==""||hi<=lo) continue; if(v==2&&s!=3) continue
    fb=(v==1)?first[k]:first2[k]
    bu=(H[k]>hi); bd=(L[k]<lo); out=(bu&&bd)?"both":bu?"up only":bd?"down only":"inside"
    cl=(C[k]>hi)?"above":(C[k]<lo)?"below":"inside"
    print d "|" s "|" v "|" out "|" fb "|" cl "|" ((O[k]>hi)?"open above":(O[k]<lo)?"open below":"open inside") "|" hi "|" lo "|" O[k] "|" C[k] } } }
