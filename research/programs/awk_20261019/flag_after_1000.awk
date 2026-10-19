# 2026-10-19 (founder: "wait after 10 for direction then look for flags"): flags split by the 10:00 direction (side of the 09:30 open at
# the 10:00 close) and by entry before / after 10:00, plus "10:00 move >= 0.25 ATR" and "straight at 10:00 (>= 0.55)" variants.
# Usage (from inputs_20261019): awk -F, -f ../awk_20261019/flag_after_1000.awk nq_atr_edge_lab.csv dir1000.csv flagx_k_1.5.csv
FILENAME~/atr/{A[$1]=$2;next}
FILENAME~/dir1000/{D[$1]=$2; M[$1]=$3; T[$1]=$4; E[$1]=$5; next}
($1 in A)&&($1 in D){a=A[$1]; p=($5-2*a/320)*320/a; e=($1<20190101)?0:($1<20250101)?1:2;
  if($2 < E[$1]) add("entered before 10:00", p, e);
  else { if($4==D[$1]) { add("after 10:00, WITH the 10:00 direction", p, e); if(M[$1]>=0.25) add("  ... and 10:00 move >= 0.25 ATR", p, e); if(T[$1]>=0.55) add("  ... and straight at 10:00 (>= 0.55)", p, e); if(M[$1]>=0.25&&T[$1]>=0.55) add("  ... and both", p, e) }
         else add("after 10:00, AGAINST the 10:00 direction", p, e) } }
function add(g, p, e) { n[g]++; s[g]+=p; w[g]+=(p>0); ne[g,e]++; se[g,e]+=p; if(!(g in ord)) ord[g]=++no; nm[ord[g]]=g }
END{for(i=1;i<=no;i++){g=nm[i]; printf "  %-42s n %4d (%4.2f/day)  win %4.1f%%  pts %+6.2f  | 2010-18 %+6.2f  2019-24 %+6.2f  2025-26 %+6.2f (%d)\n", g, n[g], n[g]/3315, 100*w[g]/n[g], s[g]/n[g], se[g,0]/(ne[g,0]?ne[g,0]:1), se[g,1]/(ne[g,1]?ne[g,1]:1), se[g,2]/(ne[g,2]?ne[g,2]:1), ne[g,2]}}
