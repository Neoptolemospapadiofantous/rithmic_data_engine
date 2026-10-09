#!/usr/bin/env bash
# live_rota.sh — trade the signal set LIVE, one window at a time, on the one account (2026-10-08, founder:
# "those strategies MUST execute as we do with live orb … whenever they trigger"). The executor runs one strategy
# per account (account-wide instance lock), so each window hands the account from the ORB instance to the
# window's strategy and back with scripts/strategy_handoff.sh (the path used live on 09-28/29 and 10-08 10:37).
#
#   scripts/live_rota.sh <window> [--check]     window: ldn_e | ldn_l | ny | asia
#     --check: pick + generate + validate only (no hand-off)
#
# Strategy per window is chosen by REGIME at the window start (see "Regime selection" below);
# a window whose regime no strategy trades well is skipped (the ORB instance keeps the account).
# The config is generated from config/tradeify_config.json (account / order fields) + the strategy's params,
# sized from the live drawdown room (see "Size" below), validated with `nq_executor --check-config`, then installed as
# config/tradeify_handoff_config.json.
set -uo pipefail
cd "$(dirname "$0")/.."
W=${1:?window}; CHECK=0; [[ "${2:-}" == "--check" ]] && CHECK=1
ACC=tradeify; ACFG=config/${ACC}_config.json; FLEET=config/paper_fleet.json; OUT=config/${ACC}_handoff_config.json
LOG=data/logs/live_rota.log
say() { if (( CHECK )); then echo "$(date -Is) [$W] (check) $*"; else echo "$(date -Is) [$W] $*" | tee -a "$LOG"; fi; }   # --check runs stay off the log the /live page reads
notify() { "$HOME/.local/bin/grid-notify" "$1" >/dev/null 2>&1 || true; }
case "$W" in   # window → entry start, entry end, flatten (ET HHMM), hand back = flatten + 2 min
  ldn_e) S=0200; E=0500; F=0501 ;;
  ldn_l) S=0700; E=0915; F=0916 ;;
  ny)    S=1030; E=1530; F=1556 ;;
  asia)  S=2000; E=2200; F=2201 ;;
  *) echo "unknown window $W"; exit 2 ;;
esac
BACK=$(printf '%04d' $(( (10#$F / 100) * 100 + 10#$F % 100 + 2 )))

# ── Regime selection (founder 2026-10-08: "based on regimes") ───────────────────────────────────────────
# eff = |last − first open| / (high − low): 0 = chop, 1 = straight line. NY reads it from the 09:30 open
# (the executor's own RegimeState); overnight windows read the 120 minutes before the window opens.
# Evidence: one clean replay of the six sig_* strategies 2026-09-21..10-08 (tight trails, 1 MNQ, $1.82 rt), split by eff at entry:
#   NY 10:30-15:30  clean replay 09-21..10-08 with the tight trails (1 MNQ $): 0.30-0.55 vwap_trend +74 (n12) ·
#                   ≥0.55 rsi2_pullback +11 (n11, worst -1) · <0.30 every NY strategy loses (rsi2 -27, vwap -12) → stand aside
#   asia band_fade  ≥0.55 +$28 (n7, all won; choppy -23, middle -31) — fades a stretched move
#   ldn ema_pullback ≥0.55 +$80 (n12, all won) · ldl donchian ≥0.55 +$177 (n17, worst -3) — trend-following only after a trend
# NY also puts the band into the config (regime_min/max_eff) so entries stop if the day changes character.
RANGE_MAX=0.30; TREND_MIN=0.55
psqlq() { (set -a; . ./.env; set +a; PGPASSWORD="$PG_PASSWORD" psql -X -q -At -h "$PG_HOST" -U "$PG_USER" -d "$PG_DB" -c "$1"); }
if [[ $W == ny ]]; then
  FROM_SQL="(date_trunc('day', now() AT TIME ZONE 'America/New_York') + interval '9 hours 30 minutes') AT TIME ZONE 'America/New_York'"
else
  FROM_SQL="now() - interval '120 minutes'"
fi
EFF=$(psqlq "SELECT round((abs((array_agg(close ORDER BY minute DESC))[1] - (array_agg(open ORDER BY minute))[1])
              / NULLIF(max(high) - min(low), 0))::numeric, 3) FROM bars_1m WHERE symbol='NQ' AND minute >= $FROM_SQL")
[[ -n "$EFF" ]] || { say "no bars to read the regime — standing aside"; notify "⏸ live rota $W: no regime reading — window skipped"; exit 0; }
REG=$(awk -v e="$EFF" -v a=$RANGE_MAX -v b=$TREND_MIN 'BEGIN{print (e<a)?"range":(e<b)?"mid":"trend"}')
GATE='{}'
case "$W" in
  ny)    case $REG in mid)   PICK=sig_ny_trend_vwap_rgx; GATE="{\"regime_min_eff\":$RANGE_MAX,\"regime_max_eff\":$TREND_MIN}" ;;
                    trend) PICK=sig_ny_rsi2_btp; GATE="{\"regime_min_eff\":$TREND_MIN}" ;;
                    *)     PICK= ;; esac ;;   # clean replay 09-21..10-08 (tight trails): choppy NY loses for every NY strategy
  asia)  [[ $REG == trend ]] && PICK=sig_tky_band_fade_5m    || PICK= ;;
  ldn_e) [[ $REG == trend ]] && PICK=sig_ldn_early_ema_pb_1m || PICK= ;;
  ldn_l) [[ $REG == trend ]] && PICK=sig_ldn_donchian_55_5m  || PICK= ;;
esac
if [[ -z "$PICK" ]]; then
  say "regime $REG (eff=$EFF) — no strategy has an edge here, standing aside"
  notify "⏸ live rota $W: regime $REG (eff $EFF) — standing aside, no live strategy this window"; exit 0
fi
SID=$(jq -r --arg s "$PICK" '.strategies[] | select(.id==$s and .params.publish_signals==true) | .id' "$FLEET")
say "regime $REG (eff=$EFF) → $PICK"
[[ -n "$SID" ]] || { say "no published strategy for this window — nothing to do"; exit 0; }
P=$(jq -c --arg s "$SID" '.strategies[] | select(.id==$s) | .params | del(.publish_signals, .signal_source, .signal_role, .base_id, .overlay, .family, .night_window, .tt_base)' "$FLEET")
TAG=$(tr '[:lower:]' '[:upper:]' <<<"$SID")

# ── Size (founder 2026-10-09: "1 NQ if we can, if not as many MNQ"; risk per trade = HALF the drawdown room) ──
# room = broker balance − (high-water mark − trailing cap), from the newest [BROKER-HWM] line either live instance
# logged. One full stop (sl_points × $/pt × qty + fees) must fit in RISK_FRAC × room; the executor's own guard
# separately refuses an entry whose stop exceeds the WHOLE room. 1 NQ when it fits, else the most MNQ that fit
# (at most 10 = one NQ), at least 1 MNQ. No room reading → 1 MNQ.
RISK_FRAC=0.5; MNQ_MAX=10
SL=$(jq -r '.sl_points // empty' <<<"$P"); COMM=$(jq -r '.commission_rt // 1.82' "$ACFG")
SFX=$(jq -r '.trade_contract' "$ACFG" | sed -E 's/^M?NQ//')    # contract month, e.g. Z6
ROOM=$(for f in data/logs/nq_executor_${ACC}.log data/logs/nq_executor_${ACC}_handoff.log; do
         grep -a '\[BROKER-HWM\]' "$f" 2>/dev/null | tail -1; done \
       | sed 's/^\[//' | sort -n | tail -1 | grep -o 'room=[-0-9.]*' | cut -d= -f2)
read -r SYM QTY PV RISK < <(awk -v room="${ROOM:-}" -v f=$RISK_FRAC -v sl="${SL:-0}" -v c="$COMM" -v mx=$MNQ_MAX 'BEGIN{
  if (room == "" || sl <= 0) { print "MNQ", 1, 2, sl*2+c; exit }
  b = room * f; nq = sl*20 + c; mnq = sl*2 + c
  if (nq <= b) { print "NQ", 1, 20, nq; exit }
  q = int(b / mnq); if (q > mx) q = mx; if (q < 1) q = 1
  print "MNQ", q, 2, q*mnq }')
SIZE=$(jq -nc --arg sym "$SYM" --arg tc "$SYM$SFX" --argjson pv "$PV" --argjson q "$QTY" '{symbol:$sym, trade_contract:$tc, point_value:$pv, qty:$q}')
say "size: $QTY $SYM$SFX (room ${ROOM:-unknown}, risk ≤ ${RISK_FRAC} × room; one stop = \$$(printf '%.2f' "$RISK"))"
NEW=$(jq -n --argjson acc "$(jq '{account_label,order_env_prefix,starting_balance,symbol,trade_contract,exchange,point_value,account_id,trade_route,fcm_id,ib_id,daily_loss_limit,trailing_drawdown_cap,consistency_cap_pct,commission_rt,tick_timeout_s,stop_cooldown_secs}' "$ACFG")" \
  --argjson p "$P" --argjson gate "$GATE" --arg tag "$TAG" --arg sid "$SID" --argjson size "$SIZE" --arg c "LIVE ROTA $(date -Is) window $W: $SID on $QTY $SYM$SFX, entries $S-$E ET, flat $F, back to the ORB instance at $BACK (scripts/live_rota.sh)." \
  --argjson s "$((10#$S))" --argjson e "$((10#$E))" --argjson f "$((10#$F))" '
  {_comment:$c} + $acc + {dry_run:false, orb_minutes:5, trail_be_trigger:3.0, trail_delay_secs:300, trail_be_offset:1.0,
     max_daily_trades:3, qty:1, cycle_mode:false, cycle_start_epoch:0, warmup_minutes:900, engine:"trend",
     strategy:$tag, paper_source:$sid}
  + $p + $gate + $size
  + {win_start:$s, win_end:$e, session_open_hour:(($s/100)|floor), session_open_min:($s%100),
     last_entry_hour:(($e/100)|floor), last_entry_min:($e%100), eod_flatten_hour:(($f/100)|floor), eod_flatten_min:($f%100),
     max_daily_trades:([($p.max_daily_trades // 3), 3] | min)}')
TMP=$(mktemp config/.rota_XXXXXX.json); printf '%s\n' "$NEW" > "$TMP"
if ! out=$(./build/nq_executor --config "$TMP" --check-config 2>&1); then
  say "config for $SID FAILED validation: $(tail -n 2 <<<"$out" | tr '\n' ' ')"; rm -f "$TMP"
  notify "🔴 live rota $W: $SID config rejected by nq_executor — window skipped"; exit 1
fi
say "validated: $SID ($(grep -o 'engine=trend mode=[a-z_0-9]* tf=[0-9]*m window=[0-9-]*' <<<"$out"))"
if (( CHECK )); then jq -c 'del(._comment, .account_id)' "$TMP"; rm -f "$TMP"; exit 0; fi
[[ -f NO_DEPLOY ]] && { say "NO_DEPLOY present — refusing"; rm -f "$TMP"; exit 1; }
mv "$TMP" "$OUT"
notify "▶️ live rota $W: handing the account to $SID ($QTY $SYM$SFX, one stop \$$(printf '%.0f' "$RISK"), room \$${ROOM:-?}) for $S-$E ET; back to the ORB instance at $BACK ET"
HARD=$S; [[ $W == ny ]] && HARD=1200   # NY: an open ORB trade may finish until 12:00
exec scripts/strategy_handoff.sh "$ACC" "$ACC" "${ACC}_handoff" "$S" "$HARD" "$BACK"
