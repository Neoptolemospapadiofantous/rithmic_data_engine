# 2026-10-19 (founder: "test 10 and 10:30 for bear flags and bull flags, for NY"): direction = side of the 09:30 open at the 10:00 close or
# at the 10:30 close; take only flags entered AFTER that check, in that direction; bull and bear separately; + "straight at the check".
# Usage (from inputs_20261019): awk -F, -v CHK=1000 -f ../awk_20261019/flag_1000_1030.awk nq_atr_edge_lab.csv dir1000.csv flagx_k_1.5.csv
FILENAME~/atr/{A[$1]=$2;next}
FILENAME~/dir1000/{ if(CHK==1030){D[$1]=$6; T[$1]=$8; E[$1]=$9} else {D[$1]=$2; T[$1]=$4; E[$1]=$5}; next}
($1 in A)&&($1 in D){a=A[$1]; p=($5-2*a/320)*320/a; e=($1<20190101)?0:($1<20250101)?1:2; side=($4>0)?"bull":"bear";
  if($2 < E[$1]) next;
  if($4==D[$1]) { add(side " flags WITH the direction", p, e); if(T[$1]>=0.55) add(side "   ... and straight at the check", p, e) }
  else add(side " flags AGAINST the direction", p, e) }
function add(g, p, e) { n[g]++; s[g]+=p; w[g]+=(p>0); ne[g,e]++; se[g,e]+=p; if(!(g in ord)) ord[g]=++no; nm[ord[g]]=g }
END{for(i=1;i<=no;i++){g=nm[i]; printf "  %-40s n %4d (%4.2f/day)  win %4.1f%%  pts %+6.2f  | 2010-18 %+6.2f  2019-24 %+6.2f  2025-26 %+6.2f (%d)\n", g, n[g], n[g]/3315, 100*w[g]/n[g], s[g]/n[g], se[g,0]/(ne[g,0]?ne[g,0]:1), se[g,1]/(ne[g,1]?ne[g,1]:1), se[g,2]/(ne[g,2]?ne[g,2]:1), ne[g,2]}}
