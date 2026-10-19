# 2026-10-19: flag trades split by the book's trade that day, tradeable form (book's FIRST entry time and direction; a flag counts as
# "after the book entry" only if it entered later). Usage (from research/programs/inputs_20261019):
#   awk -F, -f ../awk_20261019/flag_vs_book.awk nq_atr_edge_lab.csv book_F_k.csv flagx_k_1.5.csv
# pts = net NQ points per trade at today's range, 2 pts cost (gross col 5, ATR from the atr file).
FILENAME~/atr/{A[$1]=$2;next}
FILENAME~/book/{if($1+0>0){ if(!($1 in BT) || $2<BT[$1]) {BT[$1]=$2; BD[$1]=$4} } next}
($1 in A){a=A[$1]; p=($5-2*a/320)*320/a;
  if(!($1 in BT)) g="no book trade that day"; else if($2<BT[$1]) g="flag BEFORE the book entry"; else g=(BD[$1]==$4)?"after book entry, same dir":"after book entry, against";
  e=($1<20190101)?0:($1<20250101)?1:2; n[g]++; s[g]+=p; w[g]+=(p>0); ne[g,e]++; se[g,e]+=p}
END{for(g in n){printf "  %-30s n %4d (%4.2f/day)  win %4.1f%%  pts %+6.2f  | 2010-18 %+6.2f  2019-24 %+6.2f  2025-26 %+6.2f (%d)\n", g, n[g], n[g]/3800, 100*w[g]/n[g], s[g]/n[g], se[g,0]/(ne[g,0]?ne[g,0]:1), se[g,1]/(ne[g,1]?ne[g,1]:1), se[g,2]/(ne[g,2]?ne[g,2]:1), ne[g,2]}}
