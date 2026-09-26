#!/usr/bin/env bash
# rotate_handoff.sh — weekly rotation of the ORB → hand-off engine, by performance.
#
#   scripts/rotate_handoff.sh [--dry-run] [--config config/rotation.json]
#
# Ranks every paper strategy with strategy_leaderboard(period_days, handoff_only, ...) —
# the same SQL function the /strategies board shows — and, if the best qualifying
# candidate beats the incumbent's own paper twin by beat_incumbent_pct, regenerates
# config/<instance>_config.json from the candidate's paper params (account/order fields
# from config/<account>_config.json, slot fields fixed), validates it with
# `nq_executor --check-config`, archives the old file and installs the new one. The
# hand-off timer (strategy-handoff.timer) then starts whatever config exists next 10:00 ET.
#
# Guardrails: config/rotation.json (min_trades / min_sessions / min_profit_factor /
# beat_incumbent_pct). Never runs while the hand-off instance is active. Never changes
# dry_run — the current config's value is carried across. Every decision (rotate or keep)
# goes to data/logs/rotation.log, data/rotation_last.json and Telegram via grid-notify.
# --dry-run: decide + generate + validate, write nothing, notify nothing.
# --force <paper strategy_id>: manual promotion — skips the guardrails and the incumbent
#   comparison (the candidate must still be live_runnable and pass --check-config).
set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"; cd "$REPO"
DRY=0; RCFG="config/rotation.json"; FORCE=""
while [[ $# -gt 0 ]]; do case "$1" in
  --dry-run) DRY=1 ;; --config) RCFG="$2"; shift ;; --force) FORCE="$2"; shift ;;
  *) echo "unknown arg $1" >&2; exit 2 ;; esac; shift; done

OUT="data/logs/rotation.log"; LAST="data/rotation_last.json"
mkdir -p data/logs config/archived/handoff_history
say()    { echo "[$(date '+%F %T') ET $(TZ=America/New_York date +%H:%M)] $*" | tee -a "$OUT"; }
notify() { [[ "$DRY" == 1 ]] && { say "(dry notify) $1"; return; }
           for b in "$HOME/.local/bin/grid-notify" "$HOME/.config/ecosystem/bin/grid-notify"; do
             [[ -x "$b" ]] && { "$b" "$1" >/dev/null 2>&1 || true; return; }; done; }
finish() { # decision reason best_json incumbent_json
  local d="$1" r="$2" b="${3:-null}" i="${4:-null}"
  [[ "$DRY" == 1 ]] || jq -n --arg d "$d" --arg r "$r" --argjson b "$b" --argjson i "$i" \
     --arg ts "$(date -Is)" --arg inst "$INST" \
     '{ts:$ts, instance:$inst, decision:$d, reason:$r, best:$b, incumbent:$i}' > "$LAST"
  say "decision: $d — $r"; }

[[ -f "$RCFG" ]] || { say "no $RCFG"; exit 1; }
j() { jq -r "$1" "$RCFG"; }
ENABLED=$(j .enabled); ACC=$(j .account); INST=$(j .instance)
DAYS=$(j .period_days); HO=$(j .handoff_only); SS=$(j .slot_start); SE=$(j .slot_end)
MINT=$(j .min_trades); MINS=$(j .min_sessions); MINPF=$(j .min_profit_factor); BEAT=$(j .beat_incumbent_pct)
QTY=$(j .qty); MAXT=$(j .max_daily_trades)
HCFG="config/${INST}_config.json"; ACFG="config/${ACC}_config.json"

set -a; . ./.env 2>/dev/null; . "./.env.${ACC}" 2>/dev/null; set +a; export PGPASSWORD="${PG_PASSWORD:-}"
PSQL=(psql -h "${PG_HOST:-localhost}" -U "${PG_USER:-rithmic_user}" -d "${PG_DB:-rithmic}" -Atq -v ON_ERROR_STOP=1)

say "=== rotation ($ACC → $INST) period=${DAYS}d slot=${SS}-${SE} handoff_only=$HO guards: trades>=$MINT sessions>=$MINS pf>=$MINPF beat=${BEAT}%${DRY:+ [DRY]} ==="
[[ "$ENABLED" == "true" ]] || { finish keep "rotation disabled in $RCFG"; exit 0; }
[[ -f "$HCFG" ]] || { finish keep "no hand-off config $HCFG"; notify "🔴 rotation ($ACC): $HCFG missing — nothing to rotate"; exit 1; }
[[ -f "$ACFG" ]] || { finish keep "no account config $ACFG"; exit 1; }
if [[ "$(systemctl --user is-active "nq-executor-local@${INST}" 2>/dev/null)" == active ]]; then
  finish keep "hand-off instance is running — refusing to touch its config"; notify "🟡 rotation ($ACC): skipped, $INST is live right now"; exit 0; fi

LB="SELECT row_to_json(r) FROM strategy_leaderboard($DAYS, $HO, NULL, '$SS', '$SE', $MINT, $MINS, $MINPF) r"
if [[ -n "$FORCE" ]]; then
  BEST=$("${PSQL[@]}" -c "$LB WHERE r.source='paper' AND r.strategy_id='$FORCE' LIMIT 1") || { say "leaderboard query failed"; exit 1; }
  [[ -n "$BEST" ]] || { finish keep "--force $FORCE: no trades for it in the period (unknown id?)"; exit 1; }
  jq -e '.live_runnable' <<<"$BEST" >/dev/null || { finish keep "--force $FORCE: not live_runnable in the slot (overlay variant, reference-feed mode or window outside ${SS}-${SE})" "$BEST"; exit 1; }
  say "FORCED promotion of $FORCE — guardrails and incumbent comparison skipped"
else
  # Ranked per CONTRACT: paper mtf variants size 3–16 lots, ORB/trend 1, live is a fixed qty —
  # net P&L would reward size, not edge.
  BEST=$("${PSQL[@]}" -c "$LB WHERE r.source='paper' AND r.qualifies ORDER BY r.net_per_contract DESC NULLS LAST LIMIT 1") || { say "leaderboard query failed"; exit 1; }
fi
INC_SRC=$(jq -r '.paper_source // empty' "$HCFG"); INC_TAG=$(jq -r '.strategy // "?"' "$HCFG")
INC="null"
[[ -n "$INC_SRC" ]] && INC=$("${PSQL[@]}" -c "$LB WHERE r.source='paper' AND r.strategy_id='$INC_SRC' LIMIT 1")
[[ -z "$INC" ]] && INC="null"
say "incumbent: $INC_TAG (paper twin: ${INC_SRC:-none}) $(jq -c '{trades,sessions,avg_qty,net:.net_pnl,per_ct:.net_per_contract,pf:.profit_factor,qualifies}' <<<"$INC" 2>/dev/null)"

if [[ -z "$BEST" ]]; then
  finish keep "no paper strategy clears the guardrails (live_runnable, trades>=$MINT, sessions>=$MINS, pf>=$MINPF, net>0) over ${DAYS}d in the slot" "null" "$INC"
  notify "🟢 rotation ($ACC): keeping $INC_TAG — no candidate clears the guardrails yet (${DAYS}d, ${SS}-${SE} ET)"; exit 0; fi
BID=$(jq -r .strategy_id <<<"$BEST"); BNET=$(jq -r '.net_per_contract | . * 10 | round / 10' <<<"$BEST"); BENG=$(jq -r .engine <<<"$BEST")
say "best qualifying: $BID $(jq -c '{engine,mode,trades,sessions,avg_qty,net:.net_pnl,per_ct:.net_per_contract,pf:.profit_factor}' <<<"$BEST")"

if [[ -z "$FORCE" && "$BID" == "$INC_SRC" ]]; then
  finish keep "incumbent $INC_TAG is the best qualifying candidate" "$BEST" "$INC"
  notify "🟢 rotation ($ACC): keeping $INC_TAG — still the best (\$$BNET/contract over ${DAYS}d)"; exit 0; fi
if [[ -z "$FORCE" && "$INC" != "null" ]] && jq -e '.qualifies' <<<"$INC" >/dev/null; then
  INET=$(jq -r '.net_per_contract | . * 10 | round / 10' <<<"$INC")
  if ! awk -v b="$BNET" -v i="$INET" -v p="$BEAT" 'BEGIN{exit !(i > 0 && b < i * (1 + p/100))}'; then :; else
    finish keep "$BID \$$BNET/contract does not beat incumbent \$$INET/contract by ${BEAT}%" "$BEST" "$INC"
    notify "🟢 rotation ($ACC): keeping $INC_TAG — $BID (\$$BNET/ct) does not beat it (\$$INET/ct) by ${BEAT}%"; exit 0; fi
fi

# ── generate the new hand-off config ──────────────────────────────────────────
PARAMS=$("${PSQL[@]}" -c "SELECT params_json FROM paper_strategies WHERE strategy_id='$BID'")
[[ -n "$PARAMS" ]] || { finish keep "no params for $BID" "$BEST" "$INC"; exit 1; }
TAG=$(tr '[:lower:]' '[:upper:]' <<<"$BID" | tr -c 'A-Z0-9_\n' '_')
[[ "$TAG" == "ORB" ]] && TAG="ORB_HANDOFF"
DRYRUN=$(jq '.dry_run' "$HCFG")   # carried across, never decided here
SLOT_S=$(tr -d ':' <<<"$SS"); SLOT_E=$(tr -d ':' <<<"$SE")
WB=$(jq -r '.warmup_bars // 250' <<<"$PARAMS"); WARM=$(( WB + 50 > 300 ? WB + 50 : 300 ))
COMMENT="HAND-OFF ENGINE, rotated $(date -Is) by scripts/rotate_handoff.sh from paper strategy $BID ($BENG): ${DAYS}d in the ${SS}-${SE} ET slot: $(jq -r '"\(.trades) trades / \(.sessions) sessions, net $\(.net_pnl|tostring), PF \(.profit_factor|tostring)"' <<<"$BEST"). Takes over from ORB via scripts/strategy_handoff.sh at ${SS} ET (only when flat); flattens at ${SE} ET, executor backstop +1 min; qty ${QTY}, max ${MAXT} entries. Sizing FIXED via qty (strategy-side sizing not wired live). Previous config in config/archived/handoff_history/."
NEW=$(jq -n --argjson acc "$(jq '{account_label,order_env_prefix,starting_balance,symbol,trade_contract,exchange,point_value,account_id,trade_route,fcm_id,ib_id,daily_loss_limit,trailing_drawdown_cap,consistency_cap_pct,commission_rt,tick_timeout_s,stop_cooldown_secs}' "$ACFG")" \
   --argjson p "$PARAMS" --arg eng "$BENG" --arg tag "$TAG" --arg src "$BID" --arg c "$COMMENT" \
   --argjson dry "$DRYRUN" --argjson qty "$QTY" --argjson maxt "$MAXT" --argjson warm "$WARM" \
   --argjson ss "$SLOT_S" --argjson se "$SLOT_E" '
  ($p | del(.base_id, .overlay, .book_size_agree, .fill_wait_secs, .book_be_on_flip, .fill_model, .slippage_ticks)) as $pp
  | {_comment:$c} + $acc + {
      dry_run:$dry, orb_minutes:5,
      sl_points:($pp.sl_points // 8.0), trail_step:(if $eng=="mtf_scalper" then 1.0 else ($pp.trail_step // 6.0) end),
      trail_be_trigger:3.0, trail_delay_secs:300, trail_be_offset:1.0,
      max_daily_trades:$maxt, last_entry_hour:23,
      eod_flatten_hour:(($se/100)|floor), eod_flatten_min:(($se%100)+1),
      session_open_hour:9, session_open_min:30, qty:$qty, cycle_mode:false, cycle_start_epoch:0,
      warmup_minutes:$warm, engine:$eng, strategy:$tag, paper_source:$src }
  + (if $eng=="trend" then ($pp + {win_start:$ss, win_end:$se})
     else ($pp + {session_window:("\($ss|tostring|.[0:2]):\($ss|tostring|.[2:4])"|gsub(":";"")+"-"+($se|tostring)),
                  use_session:true, flat_at_session_end:true, use_max_trades:true,
                  allow_flips:false, qty_max:$qty, min_qty:$qty}) end)')
# session_window must be HHMM-HHMM; a paper variant's own max_daily_trades may be lower than
# the account cap (it was ranked with it) but never higher.
NEW=$(jq --arg sw "${SLOT_S}-${SLOT_E}" --argjson maxt "$MAXT" '
  (if .engine=="mtf_scalper" then .session_window=$sw else . end)
  | .max_daily_trades = ([(.max_daily_trades // $maxt), $maxt] | min)' <<<"$NEW")
TMP=$(mktemp "$REPO/config/.rotate_XXXXXX.json"); trap 'rm -f "$TMP"' EXIT
printf '%s\n' "$NEW" > "$TMP"
if ! out=$(./build/nq_executor --config "$TMP" --check-config 2>&1); then
  finish keep "generated config for $BID FAILED validation: $(tail -n 2 <<<"$out" | tr '\n' ' ')" "$BEST" "$INC"
  notify "🔴 rotation ($ACC): $BID generated a config nq_executor rejects — keeping $INC_TAG. See $OUT"; exit 1; fi
say "generated config validated (engine=$BENG tag=$TAG dry_run=$DRYRUN)"
if [[ "$DRY" == 1 ]]; then say "[DRY] would install $HCFG:"; jq -c 'del(._comment)' "$TMP" | tee -a "$OUT"; finish rotate "[DRY] $INC_TAG → $TAG ($BID)" "$BEST" "$INC"; exit 0; fi
cp "$HCFG" "config/archived/handoff_history/$(date +%Y%m%d-%H%M)_${INC_TAG}.json"
mv "$TMP" "$HCFG"; trap - EXIT
finish rotate "$INC_TAG → $TAG ($BID, \$$BNET/contract, $(jq -r .trades <<<"$BEST") trades)" "$BEST" "$INC"
notify "🔁 rotation ($ACC): hand-off engine is now $TAG ($BID, $BENG) — \$$BNET/contract over $(jq -r .trades <<<"$BEST") trades, ${DAYS}d in ${SS}-${SE} ET; replaces $INC_TAG from the next 10:00 ET hand-off. dry_run=$DRYRUN"
