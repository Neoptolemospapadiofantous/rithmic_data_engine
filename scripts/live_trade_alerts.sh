#!/usr/bin/env bash
# live_trade_alerts.sh — Telegram alerts for the LIVE executor's own trades, in the same format as the
# manual-signal alerts (2026-10-08, founder: "add all telegram alerts correct" — the executor itself only
# sends incident alerts). Read-only: it watches the rows the executor already writes and never touches it.
#
#   entry      = the fill on an order tagged `entry`            (live_order_events, kind exchange_notify)
#   stop moves = each new order tagged `stop_loss`              (the first one is the initial stop)
#   exit       = the completed trade row                        (live_trades, written at the close)
#
#   scripts/live_trade_alerts.sh                 run (systemd user unit live-trade-alerts)
#   scripts/live_trade_alerts.sh --dry FROM_EVT FROM_TRADE   print instead of sending, starting after the given ids
#
# Live accounts = config/*_config.json (not archived/) with dry_run false. On a fresh start it begins at the
# newest rows, so a restart never replays old trades.
set -uo pipefail
cd "$(dirname "$0")/.."
set -a; . ./.env; set +a
export PGPASSWORD="$PG_PASSWORD"
psqlq() { psql -X -At -F $'\x1f' -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 "$@"; }
STATE=data/live_alerts; mkdir -p "$STATE"
DRY=0
if [[ "${1:-}" == "--dry" ]]; then DRY=1; echo "${2:?from event id}" > "$STATE/dry_evt"; echo "${3:?from trade id}" > "$STATE/dry_trade"; fi
EVT_F=$STATE/last_event_id; TRD_F=$STATE/last_trade_id
(( DRY )) && { EVT_F=$STATE/dry_evt; TRD_F=$STATE/dry_trade; }

send() {   # fail-open: an alert that cannot be sent never stops the watcher
  if (( DRY )); then printf '%s\n---\n' "$1"; else "$HOME/.local/bin/grid-notify" "$1" >/dev/null 2>&1 || true; fi
  printf '%s %s\n' "$(date -Is)" "${1%%$'\n'*}" >> data/logs/live_trade_alerts.log
}

declare -A ACC_CFG ENTRY_SIDE ENTRY_STRAT OPEN_STRAT OPEN_DIR OPEN_ENTRY OPEN_STOP OPEN_NOTE OPEN_RISK OPEN_SENT
load_accounts() {
  ACC_CFG=()
  for f in config/*_config.json; do
    [[ "$(jq -r '.dry_run // false' "$f")" == "false" ]] || continue
    local a; a=$(jq -r '.account_label // empty' "$f"); [[ -n "$a" ]] || a=$(basename "$f" _config.json)
    ACC_CFG[$a]=$f
  done
}
pv() { case "$1" in MNQ*) echo 2 ;; NQ*) echo 20 ;; MES*) echo 5 ;; ES*) echo 50 ;; *) echo 2 ;; esac; }
bracket() {
  jq -r '"BRACKET: stop \(.sl_points|tostring) pts · auto-breakeven at +\(.trail_be_trigger|tostring) → entry +\(.trail_be_offset|tostring) · trail \(.trail_step|tostring) pts"
         + (if (.trail_delay_secs // 0) > 0 then " (trail starts after \((.trail_delay_secs/60)|floor) min)" else "" end)' "$1"
}
cfg_for() {   # account, strategy tag → the config file that trades it (falls back to the account's config)
  local f; for f in config/*_config.json; do
    [[ "$(jq -r '.account_label // empty' "$f")" == "$1" && "$(jq -r '.strategy // "ORB"' "$f")" == "$2" ]] && { echo "$f"; return; }
  done; echo "${ACC_CFG[$1]}"
}
acc_list() { local s="" a; for a in "${!ACC_CFG[@]}"; do s+="${s:+,}'$a'"; done; echo "$s"; }

entry_alert() {   # account
  local a=$1 dir=${OPEN_DIR[$1]} e=${OPEN_ENTRY[$1]} st=${OPEN_STOP[$1]:-} sym=${OPEN_NOTE[$1]}
  local p; p=$(pv "$sym")
  local risk="" line
  if [[ -n "$st" ]]; then risk=$(awk -v e="$e" -v s="$st" -v d="$dir" 'BEGIN{r=(e-s)*d; if(r<0)r=-r; printf "%.2f", r}'); OPEN_RISK[$a]=$risk; fi
  line=$(awk -v d="$dir" -v sym="$sym" -v a="$a" -v n="${OPEN_STRAT[$1]:-ORB}" -v e="$e" -v st="$st" -v r="$risk" -v p="$p" 'BEGIN{
    printf "%s %s — %s LIVE (%s executor) · 1 contract · entry %.2f", (d>0?"🟢 LONG / BUY":"🔴 SHORT / SELL"), sym, n, a, e;
    if (st!="") printf " · stop %.2f (1R %.2f pts = $%.0f on 1 %s)", st, r, r*p, sym }')
  send "$line"$'\n'"$(bracket "$(cfg_for "$a" "${OPEN_STRAT[$1]:-ORB}")")"$'\n'"The executor moves its stop to break-even within seconds of the fill — mirror it with the bracket, not by hand."
  OPEN_SENT[$a]=1
}

poll() {
  local last_evt last_trd accs
  last_evt=$(cat "$EVT_F" 2>/dev/null); last_trd=$(cat "$TRD_F" 2>/dev/null)
  if [[ -z "$last_evt" || -z "$last_trd" ]]; then   # first start: begin at the newest rows
    psqlq -c "SELECT coalesce(max(id),0) FROM live_order_events" > "$EVT_F"
    psqlq -c "SELECT coalesce(max(id),0) FROM live_trades" > "$TRD_F"; return
  fi
  accs=$(acc_list); [[ -n "$accs" ]] || return
  # 1) order stream: entry orders, entry fills, stop orders
  while IFS=$'\x1f' read -r id acc strat kind basket tag side qty price fpx fqty; do
    [[ -n "$id" ]] || continue
    if [[ "$kind" == new_order_sent && "$tag" == entry ]]; then ENTRY_SIDE[$basket]=$side; ENTRY_STRAT[$basket]=$strat
    elif [[ "$kind" == exchange_notify && "${fqty:-0}" -gt 0 && -n "${ENTRY_SIDE[$tag]:-}" ]]; then
      [[ "${ENTRY_SIDE[$tag]}" == BUY ]] && OPEN_DIR[$acc]=1 || OPEN_DIR[$acc]=-1
      OPEN_ENTRY[$acc]=$fpx; OPEN_STOP[$acc]=""; OPEN_SENT[$acc]=0; OPEN_STRAT[$acc]=${ENTRY_STRAT[$tag]:-$strat}
      unset "ENTRY_SIDE[$tag]" "ENTRY_STRAT[$tag]"
      OPEN_NOTE[$acc]=$(jq -r '.trade_contract // .symbol' "${ACC_CFG[$acc]}" | sed 's/[FGHJKMNQUVXZ][0-9]$//')   # MNQZ6 → MNQ
    elif [[ "$kind" == new_order_sent && "$tag" == stop_loss && -n "${OPEN_DIR[$acc]:-}" ]]; then
      if [[ "${OPEN_SENT[$acc]:-0}" == 0 ]]; then OPEN_STOP[$acc]=$price; entry_alert "$acc"; OPEN_STOP[$acc]=$price
      else
        local d=${OPEN_DIR[$acc]} e=${OPEN_ENTRY[$acc]} old=${OPEN_STOP[$acc]} r=${OPEN_RISK[$acc]:-0} sym=${OPEN_NOTE[$acc]}
        local msg
        msg=$(awk -v d="$d" -v e="$e" -v old="$old" -v new="$price" -v r="$r" -v sym="$sym" -v a="$acc" -v n="${OPEN_STRAT[$acc]:-ORB}" 'BEGIN{
          mv=(new-old)*d; step=(r/4>2?r/4:2); first=((old-e)*d < 0 && (new-e)*d >= 0);
          if (mv <= 0 || (mv < step - 1e-9 && !first)) exit;
          lk=(new-e)*d;
          printf "🔒 MOVE STOP — %s %s · %s LIVE (%s) · new stop %.2f (%s %+.2f pts%s)", (d>0?"LONG":"SHORT"), sym, n, a, new,
                 (lk>=0?"locks":"risk now"), lk, (r>0?sprintf(" / %+.2fR", lk/r):"") }')
        [[ -n "$msg" ]] && { send "$msg"; OPEN_STOP[$acc]=$price; }
      fi
    fi
    echo "$id" > "$EVT_F"
  done < <(psqlq -c "SELECT id, account_label, strategy, kind, coalesce(basket_id,''), coalesce(user_tag,''), coalesce(side,''), coalesce(qty,0),
                            coalesce(price,0), coalesce(fill_price,0), coalesce(fill_qty,0)
                     FROM live_order_events WHERE id > $last_evt AND account_label IN ($accs)
                       AND kind IN ('new_order_sent','exchange_notify') ORDER BY id LIMIT 2000")
  # 2) completed trades → exit
  while IFS=$'\x1f' read -r id acc inst dir e x pts reason strat; do
    [[ -n "$id" ]] || continue
    local d; [[ "$dir" == LONG ]] && d=1 || d=-1
    [[ -n "${OPEN_DIR[$acc]:-}" && "${OPEN_SENT[$acc]:-0}" == 0 ]] && entry_alert "$acc"
    local r=${OPEN_RISK[$acc]:-0} p; p=$(pv "$inst")
    send "$(awk -v d="$d" -v inst="$inst" -v a="$acc" -v n="$strat" -v x="$x" -v pts="$pts" -v r="$r" -v p="$p" -v why="$reason" 'BEGIN{
      rest=(why ~ /stop/ || why=="breakeven" || why=="trail");
      printf "%s %s %s — %s LIVE (%s) · exit %.2f · %+.2f pts = %s$%.0f on 1 %s%s · %s%s",
        (rest?"⚪ STOPPED OUT":"🟡 EXIT NOW — CLOSE"), (d>0?"LONG (SELL to close)":"SHORT (BUY to close)"), inst, n, a, x, pts,
        (pts<0?"-":"+"), (pts<0?-pts:pts)*p, inst, (r>0?sprintf(" (%+.2fR)", pts/r):""), why,
        (rest?" — your stop order should have filled":"") }')"
    unset "OPEN_DIR[$acc]" "OPEN_ENTRY[$acc]" "OPEN_STOP[$acc]" "OPEN_SENT[$acc]" "OPEN_RISK[$acc]"
    echo "$id" > "$TRD_F"
  done < <(psqlq -c "SELECT id, account_label, instrument, direction, entry_price, exit_price, round(pnl_points::numeric,2), coalesce(exit_reason,''), strategy
                     FROM live_trades WHERE id > $last_trd AND exit_time IS NOT NULL AND account_label IN ($accs) ORDER BY id")
}

load_accounts
if (( DRY )); then poll; exit 0; fi
n=0
while true; do
  poll
  (( ++n % 100 == 0 )) && load_accounts      # pick up config changes every ~5 min
  sleep 3
done
