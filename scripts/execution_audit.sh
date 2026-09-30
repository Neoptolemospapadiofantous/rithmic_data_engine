#!/usr/bin/env bash
# execution_audit.sh — post-session EXECUTION audit for a live executor instance (execution-audit.timer,
# 16:10 ET weekdays). Founder rule 2026-09-30: "every day from now on we must validate and audit the
# execution process". It reads the day's executor log block, live_trades / live_order_events and the
# tick feed, and answers one question per check: did the executor do what the config says, on the
# broker, with the numbers reconciling. Verdict lines are FAIL / WARN / OK; the JSON report lands in
# data/execution_audit/<date>.json, one Telegram line goes out via grid-notify, exit 1 on any FAIL.
#
#   scripts/execution_audit.sh [account] [YYYY-MM-DD]      (defaults: tradeify, today's ET date)
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
ACC="${1:-tradeify}"; DAY="${2:-$(TZ=America/New_York date +%F)}"
CFG="config/${ACC}_config.json"; L="data/logs/nq_executor_${ACC}.log"
OUTD="data/execution_audit"; mkdir -p "$OUTD"; OUT="$OUTD/${DAY}.json"
set -a; . ./.env 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -X -Atq -h "${PG_HOST:-127.0.0.1}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}")
notify() { for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
j() { python3 -c "import json,sys; c=json.load(open('$CFG')); v=c.get('$1'); print('' if v is None else v)" 2>/dev/null; }

fails=(); warns=(); oks=()
fail() { fails+=("$1"); printf '  FAIL  %s\n' "$1"; }
warn() { warns+=("$1"); printf '  WARN  %s\n' "$1"; }
ok()   { oks+=("$1");   printf '  ok    %s\n' "$1"; }

echo "=== execution audit $ACC $DAY ($(date -Is))"
[[ -f "$CFG" ]] || { fail "config $CFG missing"; }
[[ -f "$L" ]]   || { fail "executor log $L missing"; }

# ── the day's log block: from the day's first ET 09:00 to 16:05 (epoch bounds), all instance starts inside it
t0=$(TZ=America/New_York date -d "$DAY 09:00" +%s); t1=$(TZ=America/New_York date -d "$DAY 16:05" +%s)
DAYLOG="$OUTD/.${ACC}-${DAY}.log"
awk -F'[][]' -v a="$t0" -v b="$t1" '$2+0 >= a && $2+0 <= b' "$L" 2>/dev/null > "$DAYLOG"
lines=$(wc -l < "$DAYLOG")
(( lines > 0 )) || fail "no executor log lines between 09:00 and 16:05 ET on $DAY"
# live segments only for the signal/fill/stop counts: a dry-run start (Session date … dry_run=TRUE) opens a
# segment whose fills are simulated; they must not be counted as execution. The dry-run itself is a FAIL above.
LIVELOG="$OUTD/.${ACC}-${DAY}.live.log"
awk 'BEGIN{live=1} /Session date: .*dry_run=TRUE/ {live=0} /Session date: .*dry_run=FALSE/ {live=1} live!=0 {print}' "$DAYLOG" > "$LIVELOG"

# ── 1. mode + broker sessions + route + contract
if grep -aq 'dry_run=TRUE' "$DAYLOG"; then fail "executor ran in DRY RUN during the session (no orders reach Rithmic)"; else ok "live mode (dry_run=FALSE)"; fi
grep -aq 'ORDER_PLANT login OK' "$DAYLOG" && ok "ORDER_PLANT login" || fail "no ORDER_PLANT login in the session block"
grep -aq 'PNL_PLANT position subscription OK' "$DAYLOG" && ok "PNL_PLANT subscription" || fail "no PNL_PLANT subscription in the session block"
if grep -aq "Rithmic Order Routing" "$DAYLOG"; then fail "trade route 'Rithmic Order Routing' seen — orders silently cancel on this account"; fi
grep -aq "route='simulator'\|fallback 'simulator'" "$DAYLOG" && ok "route simulator" || warn "no route line seen"
want_ct=$(j trade_contract); got_ct=$(grep -a 'Trading contract:' "$DAYLOG" | tail -1 | sed -E 's/.*Trading contract: *//')
if [[ -n "$want_ct" && "$got_ct" == "$want_ct" ]]; then ok "contract $got_ct = config"; else fail "contract mismatch: log '$got_ct' vs config '$want_ct'"; fi
if [[ -n "$want_ct" ]]; then
  exp=$("${PSQL[@]}" -c "select expiry_date||'|'||coalesce(roll_date::text,'') from contracts where contract='$want_ct'" 2>/dev/null)
  if [[ -z "$exp" ]]; then warn "contract $want_ct not in the contracts table"
  else e=${exp%%|*}; r=${exp##*|}
    [[ "$e" > "$DAY" ]] || fail "contract $want_ct expired $e"
    [[ -n "$r" && ! "$r" > "$DAY" ]] && warn "contract $want_ct past its roll date $r — roll the config"
  fi
fi

# ── 2. signals → orders → fills (every entry signal must end in a fill or a logged cancel/reject)
sig=$(grep -ac 'Entry signal' "$LIVELOG"); dsig=$(grep -a 'Entry signal' "$LIVELOG" | grep -ac 'DRY_RUN')
sent=$(grep -a 'RequestNewOrder sent' "$LIVELOG" | grep -ac 'order_type=1')
fill_e=$(grep -ac 'FILL entry' "$LIVELOG"); fill_x=$(grep -ac 'FILL exit' "$LIVELOG")
tmo=$(grep -ac 'PENDING_ENTRY timeout — entry cancelled' "$LIVELOG"); rej=$(grep -ac 'Order rejected' "$LIVELOG")
printf '  info  entry signals=%s (dry %s) orders sent=%s entry fills=%s exit fills=%s entry timeouts=%s rejects=%s\n' "$sig" "$dsig" "$sent" "$fill_e" "$fill_x" "$tmo" "$rej"
live_sig=$((sig - dsig))
if (( live_sig != fill_e + tmo )); then fail "entry signals ($live_sig) != entry fills ($fill_e) + logged entry timeouts ($tmo) — an entry went nowhere"; else ok "every entry signal ended in a fill or a logged timeout"; fi
(( rej == 0 )) && ok "no order rejects" || fail "$rej order reject(s): $(grep -a 'Order rejected' "$DAYLOG" | head -2 | cut -c1-120 | tr '\n' ';')"
(( tmo == 0 )) || warn "$tmo entry order(s) timed out unfilled (limit ran away) — recorded in live_order_events, absent from live_trades"

# ── 3. DB rows vs fills vs broker
rows=$("${PSQL[@]}" -c "select count(*) from live_trades where account_label='$ACC' and trade_date='$DAY'" 2>/dev/null); rows=${rows:-0}
open_rows=$("${PSQL[@]}" -c "select count(*) from live_trades where account_label='$ACC' and trade_date='$DAY' and exit_time is null" 2>/dev/null); open_rows=${open_rows:-0}
if (( rows == fill_x )); then ok "live_trades rows ($rows) = exit fills"; else fail "live_trades rows ($rows) != exit fills in log ($fill_x)"; fi
(( open_rows == 0 )) && ok "no open live_trades row at audit time" || fail "$open_rows live_trades row(s) still open after the session"
qty_cfg=$(j qty); bad_qty=$("${PSQL[@]}" -c "select count(*) from live_trades where account_label='$ACC' and trade_date='$DAY' and qty <> ${qty_cfg:-0}" 2>/dev/null)
[[ "${bad_qty:-0}" == 0 ]] && ok "every row qty = config qty ($qty_cfg)" || fail "$bad_qty row(s) with qty != config $qty_cfg"
net=$("${PSQL[@]}" -c "select coalesce(round(sum(pnl_usd)::numeric,2),0) from live_trades where account_label='$ACC' and trade_date='$DAY'" 2>/dev/null); net=${net:-0}
cts=$("${PSQL[@]}" -c "select coalesce(sum(qty),0) from live_trades where account_label='$ACC' and trade_date='$DAY'" 2>/dev/null); cts=${cts:-0}
broker=$(grep -a '\[BROKER\]' "$DAYLOG" | tail -1 | grep -oE 'day_pnl=-?[0-9.]+' | cut -d= -f2)
if [[ -n "$broker" ]]; then
  gap=$(python3 -c "print(round(abs(float('$broker')-float('$net')),2))"); tol=$(python3 -c "print(round(1.0*$cts+2.0,2))")   # fees are recorded per contract now; allow $1/ct rounding + $2
  if python3 -c "import sys; sys.exit(0 if $gap <= $tol else 1)"; then ok "broker day_pnl $broker vs live_trades net $net (gap $gap, $cts contract-RT)"; else fail "broker day_pnl $broker vs live_trades net $net — gap $gap over $cts contract-RT (fees wrong or a trade missing)"; fi
else warn "no [BROKER] line in the session block — cannot reconcile P&L"; fi

# ── 4. stop integrity: internal sl vs exchange stop must agree within 15 s; every BE move must be sent
stale=$(awk -F'[][]' '/TRAIL-CHECK/ {
    match($0, /sl=[-0-9.]+ exch_sl=[-0-9.]+/); s=substr($0,RSTART,RLENGTH); split(s,p," "); sl=substr(p[1],4); ex=substr(p[2],9);
    t=$2+0; if (sl!=ex) { if (!start) start=t; if (t-start>mx) mx=t-start } else start=0 }
    END { printf "%.0f", mx }' "$LIVELOG")
if (( ${stale:-0} > 15 )); then fail "exchange stop lagged the internal stop for ${stale}s (max) — BE/trail move not delivered"; else ok "exchange stop tracked the internal stop (max lag ${stale:-0}s)"; fi
be=$(grep -ac 'BE triggered' "$LIVELOG"); sc=$(grep -ac 'STOP-CANCEL' "$LIVELOG")
(( be == 0 || sc >= be )) && ok "BE moves $be, stop replacements $sc" || warn "BE moves $be but only $sc stop replacements — check the trail step filter"
crit=$(grep -a 'CRITICAL' "$DAYLOG" | grep -av 'duplicate delivery' | wc -l)
(( crit == 0 )) && ok "no CRITICAL lines" || fail "$crit CRITICAL line(s): $(grep -a 'CRITICAL' "$DAYLOG" | grep -av 'duplicate delivery' | head -2 | cut -c1-110 | tr '\n' ';')"

# ── 5. risk + windows
mdt=$(j max_daily_trades); (( rows <= ${mdt:-99} )) && ok "trades $rows <= max_daily_trades $mdt" || fail "trades $rows > max_daily_trades $mdt"
leh=$(j last_entry_hour); lem=$(j last_entry_min); lem=${lem:-0}
if [[ -n "$leh" ]]; then late=$("${PSQL[@]}" -c "select count(*) from live_trades where account_label='$ACC' and trade_date='$DAY' and (extract(hour from entry_time at time zone 'America/New_York')*60+extract(minute from entry_time at time zone 'America/New_York')) >= ${leh}*60+${lem}" 2>/dev/null)
  [[ "${late:-0}" == 0 ]] && ok "no entry at/after the ${leh}:$(printf %02d "$lem") ET cutoff" || fail "${late} entry(ies) after the ${leh}:$(printf %02d "$lem") ET cutoff"; fi
halts=$(grep -a 'Trading halted' "$DAYLOG" | sed -E 's/.*Trading halted: //' | grep -vE '^(shutdown|eod)' | sort | uniq -c | awk '{printf "%s×%s ", $2, $1}')
[[ -z "$halts" ]] && ok "no risk/feed halts" || warn "halts: $halts"

# ── 6. feed quality during RTH (upstream gaps blind the executor; stops stay on the exchange)
sym=$(j md_feed_symbol); sym=${sym:-NQ}
gaps=$("${PSQL[@]}" -c "with t as (select ts_event, lag(ts_event) over (order by ts_event) prev from ticks where symbol='$sym' and ts_event >= ('$DAY 09:30'::timestamp at time zone 'America/New_York') and ts_event < ('$DAY 16:00'::timestamp at time zone 'America/New_York')) select count(*)||'|'||coalesce(max(round(extract(epoch from ts_event-prev)::numeric)),0) from t where ts_event-prev > interval '30 seconds'" 2>/dev/null)
gn=${gaps%%|*}; gm=${gaps##*|}
[[ "${gn:-0}" == 0 ]] && ok "no $sym feed gap > 30 s in RTH" || warn "${gn} $sym feed gap(s) > 30 s in RTH (max ${gm}s) — executor blind, exchange stops still armed"

# ── report
nf=${#fails[@]}; nw=${#warns[@]}; no=${#oks[@]}
verdict=$([[ $nf == 0 ]] && echo PASS || echo FAIL)
python3 - "$OUT" "$verdict" "$ACC" "$DAY" "$rows" "$net" "${broker:-}" <<'EOF' "${fails[@]}" "--" "${warns[@]}"
import json, sys
out, verdict, acc, day, rows, net, broker = sys.argv[1:8]; rest = sys.argv[8:]
i = rest.index("--"); fails, warns = rest[:i], rest[i+1:]
json.dump({"verdict": verdict, "account": acc, "date": day, "trades": int(rows), "net_usd": float(net),
           "broker_day_pnl": (float(broker) if broker else None), "fails": fails, "warns": warns}, open(out, "w"), indent=1)
EOF
echo "=== $verdict — $nf fail · $nw warn · $no ok → $OUT"
if [[ $verdict == PASS ]]; then notify "🟢 execution audit $ACC $DAY: PASS — $rows trades, net $net, broker ${broker:-n/a}${nw:+ · $nw warn: $(IFS='; '; echo "${warns[*]}")}"; exit 0
else notify "🔴 execution audit $ACC $DAY: $nf FAIL — $(IFS='; '; echo "${fails[*]}")${nw:+ · warn: $(IFS='; '; echo "${warns[*]}")}"; exit 1; fi
