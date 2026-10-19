# per session: open, price at checkpoint, close, hi, lo (ET). Asia 18:00-03:00 (check 20:00), London 03:00-09:30 (check 04:00), NY 09:30-16:00 (check 10:30)
{ t=$1; hm=strftime("%H %M %u",t); split(hm,a," "); m=a[1]*60+a[2]
  if(m>=1080||m<180){s="1asia"; ck=1200; d=strftime("%Y%m%d",t+(m>=1080?21600:0))}
  else if(m<570){s="2london"; ck=240; d=strftime("%Y%m%d",t)}
  else if(m<960){s="3ny"; ck=630; d=strftime("%Y%m%d",t)} else next
  k=d" "s
  if(!(k in O)){O[k]=$2; H[k]=$3; L[k]=$4; ord[++n]=k}
  if($3>H[k])H[k]=$3; if($4<L[k])L[k]=$4; C[k]=$5
  mm=(m>=1080)?m-1440:m; cc=(ck>=1080)?ck-1440:ck; if(mm<cc) P[k]=$5 }
END{ for(i=1;i<=n;i++){k=ord[i]; if(k in P) print k, O[k], P[k], C[k], H[k], L[k]} }
