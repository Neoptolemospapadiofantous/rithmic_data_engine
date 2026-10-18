#!/usr/bin/env bash
# manual_review.sh — your own hand trades vs the Plan page, and what your fills really cost.
#
#   scripts/manual_review.sh [YYYY-MM-DD] [fills.csv] [--tz ZONE] [--notify]
#
# Fills: drop the platform's fills / order-history export for the day into data/manual_fills/ (any file name containing the
# date, e.g. 2026-10-19.csv or fills_20261019.csv) or pass the file. Columns are found by their header (case-insensitive):
#   time   "fill time" / "time" / "date/time" / "datetime" / "timestamp" / "update time" (or a "date" + "time" pair)
#   side   "side" / "buy/sell" / "b/s" / "action" (B, BUY, BOT, Bought, Long → buy; S, SELL, SLD, Sold, Short → sell)
#   qty    "qty" / "quantity" / "filled qty" / "fill qty" / "fill size" / "size" (a signed qty works without a side column)
#   price  "fill price" / "avg fill price" / "price" / "avg price"
#   symbol "symbol" / "contract" / "instrument" (optional; MNQ… = $2/pt, NQ… = $20/pt, MES $5, ES $50; default MNQ)
# Smallest file that works:  time,side,qty,price  /  2026-10-19 10:31:05,BUY,2,20150.25
# Times are read in --tz (default: this box's zone, or data/manual_fills/tz if that file holds a zone name) — a warning is
# printed when no fill falls inside 09:30–16:00 New York, which usually means the zone is wrong.
#
# For every round trip (flat → position → flat, FIFO-free: VWAP of the opening fills vs VWAP of the closing fills) the report
# replays the Plan page AT YOUR ENTRY MINUTE (scripts/daily_plan_live.jq, the same code the page runs) and says which setup it
# was — cascade / gap retest / box_go (≤ 5 min after its bar closed, same direction, allowed), momentum + VWAP (45-min),
# momentum + EMA-20 (30-min), EMA by hand with permission — or OFF-PLAN and why (the page's own reason). Day rules: more than
# 3 trades, a trade after a loss, a trade against the day's first direction.
# Cost: for setup trades, the fill vs the signal bar's close, signed (positive = worse for you), and how many seconds after the
# bar closed you were filled. The research assumes 1.6 pts round trip; 5 pts worse per trade erased the sprint edge (2026-10-17).
# Writes data/manual_review/<date>.md and one line per trade in data/manual_review/ledger.jsonl (re-running a day replaces it),
# then prints the running totals. --notify sends the day's summary through grid-notify. Read-only on Postgres.
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
day=""; csv=""; tz=""; notify=0
while [[ $# -gt 0 ]]; do case "$1" in
  --tz) tz="$2"; shift 2 ;; --notify) notify=1; shift ;;
  -h|--help) sed -n 2,28p "$0"; exit 0 ;;
  [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]) day="$1"; shift ;;
  *) csv="$1"; shift ;; esac; done
day="${day:-$(TZ=America/New_York date +%F)}"
[[ -z "$tz" && -s data/manual_fills/tz ]] && tz=$(head -1 data/manual_fills/tz | tr -d '[:space:]')
tz="${tz:-$(timedatectl show -p Timezone --value 2>/dev/null || echo UTC)}"
if [[ -z "$csv" ]]; then
  compact="${day//-/}"
  csv=$(ls data/manual_fills/*"$day"* data/manual_fills/*"$compact"* 2>/dev/null | head -1)
fi
OUT=data/manual_review; mkdir -p "$OUT"
notify_bin=$(command -v grid-notify || echo "$HOME/.local/bin/grid-notify")
if [[ -z "$csv" || ! -s "$csv" ]]; then
  echo "manual_review $day: no fills file (data/manual_fills/*$day*.csv) — nothing to review"
  exit 0
fi
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
Q() { psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -c "$1"; }

# ── 1. fills → epoch,side,qty,price,symbol (one per line, time order)
fills=$(gawk -v FPAT='([^,]*)|("[^"]*")' -v IGNORECASE=1 '
  function clean(x) { gsub(/^"|"$/, "", x); gsub(/^[ \t]+|[ \t\r]+$/, "", x); return x }
  NR == 1 { for (i = 1; i <= NF; i++) { h = tolower(clean($i))
      if (h ~ /^(fill time|time|date\/time|datetime|date time|timestamp|update time|exec time|filled time|fill date\/time)$/ && !ti) ti = i
      else if (h == "date" && !di) di = i
      else if (h ~ /^(side|buy\/sell|b\/s|action|direction)$/ && !si) si = i
      else if (h ~ /^(qty|quantity|filled qty|fill qty|fill size|size|filled|qty filled)$/ && !qi) qi = i
      else if (h ~ /^(fill price|avg fill price|price|avg price|exec price|average price)$/ && !pi) pi = i
      else if (h ~ /^(symbol|contract|instrument)$/ && !yi) yi = i }
    if (!ti || !qi || !pi) { print "ERR missing column (need time, qty, price; found time=" ti " qty=" qi " price=" pi ")"; exit }
    next }
  { t = clean($ti); if (di && t !~ /[0-9]{4}-|[0-9]+\/[0-9]+\/[0-9]+/) t = clean($di) " " t
    q = clean($qi); gsub(/,/, "", q); p = clean($pi); gsub(/[,$]/, "", p); s = si ? toupper(clean($si)) : ""
    sym = yi ? toupper(clean($yi)) : "MNQ"; if (q == "" || p == "" || q + 0 == 0) next
    sg = 0; if (s ~ /^(B|BUY|BOT|BOUGHT|LONG)/) sg = 1; else if (s ~ /^(S|SELL|SLD|SOLD|SHORT)/) sg = -1; else sg = (q < 0 ? -1 : 1)
    if (q < 0) q = -q
    print t "\t" sg "\t" q "\t" p "\t" sym }' "$csv")
if [[ "$fills" == ERR* ]]; then echo "manual_review: $csv — ${fills#ERR }"; exit 2; fi
rows=""
while IFS=$'\t' read -r t sg q p sym; do
  [[ -n "$t" ]] || continue
  e=$(TZ="$tz" date -d "$t" +%s 2>/dev/null) || { echo "manual_review: cannot read the time '$t' — skipped"; continue; }
  rows+="$e"$'\t'"$sg"$'\t'"$q"$'\t'"$p"$'\t'"$sym"$'\n'
done <<<"$fills"
rows=$(printf '%s' "$rows" | sort -n -k1,1)
[[ -n "$rows" ]] || { echo "manual_review $day: $csv has no readable fills"; exit 2; }
inrth=$(while IFS=$'\t' read -r e _; do m=$(( 10#$(TZ=America/New_York date -d "@$e" +%H) * 60 + 10#$(TZ=America/New_York date -d "@$e" +%M) )); (( m >= 570 && m < 960 )) && echo x; done <<<"$rows" | wc -l)
warn=""; (( inrth == 0 )) && warn="WARNING: no fill falls inside 09:30–16:00 New York when read as $tz — pass --tz (or write the zone to data/manual_fills/tz)."

# ── 2. round trips (flat → flat; a fill through zero closes and opens)
trips=$(gawk -F'\t' '
  function flush() { if (eq > 0) printf "%d\t%d\t%d\t%.2f\t%.2f\t%d\t%s\n", t0, t1, dir, ev / eq, xv / xq, maxq, sym; eq = ev = xq = xv = maxq = 0 }
  { e = $1; s = $2; q = $3; p = $4; sym = $5
    while (q > 0) {
      if (pos == 0) { dir = s; t0 = e }
      if (s == dir) { pos += s * q; ev += q * p; eq += q; if (pos * dir > maxq) maxq = pos * dir; q = 0 }
      else { c = (q < pos * dir) ? q : pos * dir; pos += s * c; xv += c * p; xq += c; q -= c; t1 = e; if (pos == 0) flush() } } }
  END { if (pos != 0) printf "OPEN\t%d\t%d\n", pos, t0 }' <<<"$rows")

# ── 3. the plan at each entry minute
[[ -s "data/plan/$day.json" ]] || scripts/daily_plan.sh "$day" >/dev/null 2>&1
plan="data/plan/$day.json"
[[ -s "$plan" ]] || { echo "manual_review $day: no plan file and it could not be built"; exit 1; }
p=$(date -d "$day -1 day" +%F)
bars=$(Q "select coalesce(json_agg(json_build_object('m',(extract(epoch from (minute at time zone 'America/New_York')::time)/60)::int-570,'o',open,'h',high,'l',low,'c',close,'v',volume,'bv',buy_volume) order by minute),'[]') from bars_1m where symbol='NQ' and minute >= ('$day 09:30'::timestamp at time zone 'America/New_York') and minute < ('$day 16:00'::timestamp at time zone 'America/New_York')")
pre1=$(Q "select coalesce(json_agg(close order by minute),'[]') from bars_1m where symbol='NQ' and minute >= ('$day 09:30'::timestamp at time zone 'America/New_York') - interval '60 minutes' and minute < ('$day 09:30'::timestamp at time zone 'America/New_York')")
pre5=$(Q "select coalesce(json_agg(c order by bk),'[]') from (select floor(extract(epoch from minute)/300) bk,(array_agg(close order by minute desc))[1] c from bars_1m where symbol='NQ' and minute >= ('$p 18:00'::timestamp at time zone 'America/New_York') and minute < ('$day 09:30'::timestamp at time zone 'America/New_York') group by 1) t")
pre2=$(Q "select coalesce(json_agg(c order by bk),'[]') from (select floor(extract(epoch from minute)/120) bk,(array_agg(close order by minute desc))[1] c from bars_1m where symbol='NQ' and minute >= ('$p 18:00'::timestamp at time zone 'America/New_York') and minute < ('$day 09:30'::timestamp at time zone 'America/New_York') group by 1) t")
[[ "$bars" == "[]" ]] && { echo "manual_review $day: no NQ bars for the day"; exit 1; }
open_e=$(TZ=America/New_York date -d "$day 09:30" +%s)

report="$OUT/$day.md"; ledger="$OUT/ledger.jsonl"; touch "$ledger"
grep -v "\"date\":\"$day\"" "$ledger" > "$ledger.tmp" || true
{
  echo "# Hand trades vs the plan — $day"
  echo
  echo "Fills: \`$csv\` (times read as $tz). $warn"
  echo
  echo "| # | entry ET | exit ET | dir | qty | entry | exit | pts | \$ | plan says | signal close | fill vs signal (pts, + = worse) | seconds after the bar |"
  echo "|---|---|---|---|---|---|---|---|---|---|---|---|---|"
} > "$report"
n=0; prev_pnl=""; first_dir=""; follow_n=0; off_n=0; follow_usd=0; off_usd=0; slips=(); summary=()
while IFS=$'\t' read -r t0 t1 dir ep xp q sym; do
  [[ -n "$t0" ]] || continue
  if [[ "$t0" == OPEN ]]; then echo; echo "Still open at the end of the file: position $t1 since $(TZ=America/New_York date -d "@$dir" +%H:%M:%S) ET." >> "$report"; continue; fi
  n=$((n + 1))
  pv=2; case "$sym" in MNQ*) pv=2 ;; NQ*) pv=20 ;; MES*) pv=5 ;; ES*) pv=50 ;; esac
  pts=$(awk -v d="$dir" -v a="$ep" -v b="$xp" 'BEGIN{printf "%.2f", d * (b - a)}')
  usd=$(awk -v x="$pts" -v q="$q" -v pv="$pv" 'BEGIN{printf "%.2f", x * q * pv}')
  m=$(( (t0 - open_e) / 60 ))     # minutes after 09:30 at the entry
  dname=$([[ $dir -gt 0 ]] && echo LONG || echo SHORT)
  view=$(jq -c --argjson bars "$(jq -c --argjson n "$m" '[.[] | select(.m < $n)]' <<<"$bars")" --argjson pre2 "$pre2" --argjson pre5 "$pre5" --argjson pre1 "$pre1" --argjson now "$m" \
           -f scripts/daily_plan_live.jq "$plan" 2>/dev/null | jq -c '.live')
  verdict=$(jq -r --arg d "$dname" --argjson m "$m" '
    def mins: split(":") | ((.[0] | tonumber) * 60 + (.[1] | tonumber)) - 570;
    def near: (.at | mins) as $a | ($m - $a) >= 0 and ($m - $a) <= 5;
    ([.setups[]? | select(near and .dir == $d and .allowed)] | last) as $s
    | if $m < 0 or $m >= 390 then "OFF-PLAN\toutside 09:30–16:00\t"
      elif $s != null then "\($s.setup)\t\($s.setup) \($s.at) \($s.dir)\t\($s.price)\t\($s.at)"
      elif (.mom45.setup // null) != null and (.mom45.setup | near) and .mom45.dir == $d then "momentum+VWAP\tmomentum + VWAP \(.mom45.setup.at)\(if .mom45.plan_agrees then " (plan agrees)" else " (plan NOT agreeing — the weak version)" end)\t\(.mom45.setup.price)\t\(.mom45.setup.at)"
      elif (.mom.setup // null) != null and .mom.allowed and (.mom.setup | near) and .mom.dir == $d then "momentum+EMA20\tmomentum + EMA-20 \(.mom.setup.at)\t\(.mom.setup.price)\t\(.mom.setup.at)"
      elif (.ema.allowed // false) and .ema.dir == $d then "EMA by hand\tEMA by hand, permitted (\(.ema.dir) since \(.ema.since // "?")) — ≈ breakeven in tests\t"
      else "OFF-PLAN\t\(([.setups[]? | select(near and .dir == $d and (.allowed | not)) | .why_not] | first) // (if (.ema.dir // null) != null and .ema.dir != $d then "against the day direction (\(.ema.dir))" else (.ema.why // .status // "no setup at this minute") end))\t" end' <<<"$view")
  IFS=$'\t' read -r kind says sigpx sigat <<<"$verdict"
  notes=""
  [[ -n "$prev_pnl" ]] && awk -v x="$prev_pnl" 'BEGIN{exit !(x < 0)}' && notes+=" · after a loss (plan: done for the day)"
  (( n > 3 )) && notes+=" · trade #$n (plan: at most 3)"
  [[ -n "$first_dir" && "$first_dir" != "$dname" ]] && notes+=" · against your first trade today ($first_dir)"
  [[ -z "$first_dir" ]] && first_dir="$dname"
  slip=""; secs=""
  if [[ -n "$sigpx" && -n "$sigat" ]]; then
    slip=$(awk -v d="$dir" -v f="$ep" -v s="$sigpx" 'BEGIN{printf "%.2f", d * (f - s)}')
    bar_close=$(( open_e + ($(jq -rn --arg a "$sigat" '$a | split(":") | ((.[0] | tonumber) * 60 + (.[1] | tonumber)) - 570')) * 60 ))
    secs=$(( t0 - bar_close )); slips+=("$slip")
  fi
  if [[ "$kind" == OFF-PLAN ]]; then off_n=$((off_n + 1)); off_usd=$(awk -v a="$off_usd" -v b="$usd" 'BEGIN{print a + b}')
  else follow_n=$((follow_n + 1)); follow_usd=$(awk -v a="$follow_usd" -v b="$usd" 'BEGIN{print a + b}'); fi
  printf '| %d | %s | %s | %s | %s | %s | %s | %s | %s | %s%s | %s | %s | %s |\n' "$n" "$(TZ=America/New_York date -d "@$t0" +%H:%M:%S)" "$(TZ=America/New_York date -d "@$t1" +%H:%M:%S)" \
    "$dname" "$q $sym" "$ep" "$xp" "$pts" "$usd" "$says" "$notes" "${sigpx:--}" "${slip:--}" "${secs:--}" >> "$report"
  jq -cn --arg date "$day" --argjson n "$n" --arg kind "$kind" --arg says "$says$notes" --arg dir "$dname" --argjson q "$q" --arg sym "$sym" \
     --argjson entry "$ep" --argjson exit "$xp" --argjson pts "$pts" --argjson usd "$usd" --arg slip "$slip" --arg secs "$secs" \
     '{date: $date, n: $n, kind: $kind, plan: $says, dir: $dir, qty: $q, symbol: $sym, entry: $entry, exit: $exit, pts: $pts, usd: $usd,
       slip_pts: (if $slip == "" then null else ($slip | tonumber) end), secs_after_bar: (if $secs == "" then null else ($secs | tonumber) end)}' >> "$ledger.tmp"
  summary+=("#$n $dname $q $sym $(TZ=America/New_York date -d "@$t0" +%H:%M) → $pts pts \$$usd · $kind${slip:+ · fill ${slip} pts vs signal}$notes")
  prev_pnl="$pts"
done <<<"$trips"
mv "$ledger.tmp" "$ledger"
avg_slip=$( [[ ${#slips[@]} -gt 0 ]] && printf '%s\n' "${slips[@]}" | awk '{s += $1} END{printf "%.2f", s / NR}' || echo "-")
tot=$(awk -v a="$follow_usd" -v b="$off_usd" 'BEGIN{printf "%.2f", a + b}')
{
  echo
  echo "**Day:** $n trades, \$$tot — on plan $follow_n (\$$follow_usd), off plan $off_n (\$$off_usd). Average fill vs signal on setup trades: ${avg_slip} pts (research assumes ≈ 0.6 entry + 1.0 cost)."
  echo
  echo "**Running (data/manual_review/ledger.jsonl):** $(jq -rs '
     (map(select(.kind != "OFF-PLAN"))) as $on | (map(select(.kind == "OFF-PLAN"))) as $off | ([.[] | .slip_pts | select(. != null)]) as $s
     | ([.[] | .secs_after_bar | select(. != null)] | sort) as $sec
     | "\(length) trades on \(map(.date) | unique | length) days — on plan \($on | length) (\($on | map(.usd) | add // 0 | . * 100 | round / 100) $), off plan \($off | length) (\($off | map(.usd) | add // 0 | . * 100 | round / 100) $); fill vs signal \(if ($s | length) > 0 then ($s | add / length | . * 100 | round / 100 | tostring) + " pts over " + ($s | length | tostring) + " setup trades" else "no setup trades yet" end)\(if ($sec | length) > 0 then ", median " + ($sec[($sec | length / 2 | floor)] | tostring) + " s after the bar" else "" end)."' "$ledger")"
  echo
  echo "Setup trades are matched when you entered ≤ 5 minutes after the signal bar closed, in its direction, while the page allowed it. The page is replayed at your entry minute with the same code it runs live (scripts/daily_plan_live.jq)."
} >> "$report"
cat "$report"
if (( notify )) && [[ -x "$notify_bin" ]]; then
  msg="Hand trades $day: $n, \$$tot — on plan $follow_n (\$$follow_usd), off plan $off_n (\$$off_usd); fill vs signal ${avg_slip} pts."$'\n'"$(printf '%s\n' "${summary[@]}")"
  [[ -n "$warn" ]] && msg+=$'\n'"$warn"
  "$notify_bin" "$msg" >/dev/null 2>&1 || true
fi
