#!/usr/bin/env bash
# strategy_scout.sh — keeps the manual/live signal set at the best strategies, every evening (2026-10-08,
# founder: "whenever a better strategy, meaning high win rate, low drawdown, with trail, we should add").
#
#   scripts/strategy_scout.sh --dry     rank + decide, change nothing, print the decisions
#   scripts/strategy_scout.sh           apply: add / replace sig_* copies in config/paper_fleet.json, Telegram
#
# Windows (ET, by entry time): ue 16:00-17:00 · ae 18-20 · asia 20-22 · al 22-02 · ldn_e 02-05 · ldn_m 05-07 ·
# ldn_l 07-09:30 · ny 09:30-16:00. Evidence: the forward paper fleet (label tradeify, last 21 days) for RTH and
# the overnight research replays (labels night, gap) for the other windows. Costs: 1 MNQ ($2/pt, $1.82 RT).
# Bar: >= 15 trades on >= 6 sessions, net > 0, PF >= 1.5, max drawdown <= $60, a trailing exit (no fixed
# target), live-runnable (trend engine, not rs_continuation/hold; no paper-fill overlays __sz/__wait/__bbe).
# Rank: share of trades won by >= 2 pts, then lower drawdown, then net. A window without a signal strategy gets
# its best candidate; a filled window takes a challenger only if it wins >= 5 points more often with no worse
# drawdown. At most 2 per window (the weakest is unpublished). An incumbent whose own evidence turned negative
# (>= 10 trades, net < 0) is unpublished even without a challenger. Changes take effect at the next paper-engine
# restart (never inside RTH — this script restarts only between 16:05 and 17:55 ET when something changed).
set -uo pipefail
cd "$(dirname "$0")/.."
DRY=0; [[ "${1:-}" == "--dry" ]] && DRY=1
FLEET=config/paper_fleet.json; LOG=data/logs/strategy_scout.log; mkdir -p data/scout
set -a; . ./.env; set +a; export PGPASSWORD="$PG_PASSWORD"
psqlq() { psql -X -Atq -h "$PG_HOST" -p "${PG_PORT:-5432}" -U "$PG_USER" -d "$PG_DB" -v ON_ERROR_STOP=1 "$@"; }
say() { echo "$(date -Is) $*" | tee -a "$LOG"; }
MIN_TRADES=15 MIN_DAYS=6 MIN_PF=1.5 MAX_DD=60 WIN_EDGE=5 PER_WINDOW=2

# incumbents: the published sig_* copies and the strategy each one copies
INC=$(jq -c '[.strategies[] | select((.id|startswith("sig_")) and (.params.publish_signals==true)) | {id, src:(.params.signal_source)}]' "$FLEET")
SRC_LIST=$(jq -r '[.[].src] | map("'"'"'" + . + "'"'"'") | join(",")' <<<"$INC")

psqlq -c "
CREATE TEMP TABLE t AS
SELECT p.strategy_id sid, s.engine, s.params_json pj, p.account_label lbl, p.entry_time, p.pnl_pts pts, p.pnl_pts*2 - 1.82 usd,
       ((p.entry_time AT TIME ZONE 'America/New_York') + interval '6 hours')::date d,
       CASE WHEN et >= '16:00' AND et < '17:00' THEN 'ue' WHEN et >= '18:00' AND et < '20:00' THEN 'ae'
            WHEN et >= '20:00' AND et < '22:00' THEN 'asia' WHEN et >= '22:00' OR et < '02:00' THEN 'al'
            WHEN et >= '02:00' AND et < '05:00' THEN 'ldn_e' WHEN et >= '05:00' AND et < '07:00' THEN 'ldn_m'
            WHEN et >= '07:00' AND et < '09:30' THEN 'ldn_l' WHEN et >= '09:30' AND et < '16:00' THEN 'ny' END w
FROM (SELECT p.*, (p.entry_time AT TIME ZONE 'America/New_York')::time et FROM paper_trades p
      WHERE p.exit_time IS NOT NULL
        AND ((p.account_label = 'tradeify' AND p.entry_time > now() - interval '21 days')
          OR (p.account_label IN ('night','gap')))) p
JOIN paper_strategies s USING (strategy_id)
WHERE s.engine = 'trend'
  AND COALESCE(s.params_json->>'mode','') NOT IN ('rs_continuation','hold')
  AND COALESCE(s.params_json->>'overlay','') NOT IN ('sz','wait','bbe')
  AND COALESCE((s.params_json->>'tp_r')::float, 0) = 0 AND COALESCE((s.params_json->>'tp_points')::float, 0) = 0
  AND COALESCE((s.params_json->>'trail_step')::float, 1) > 0
  AND p.strategy_id NOT LIKE 'sig\_%' AND p.strategy_id NOT LIKE 'tt\_%';
CREATE TEMP TABLE a AS
WITH c AS (SELECT *, sum(usd) OVER (PARTITION BY sid, w ORDER BY entry_time) cum FROM t),
     dd AS (SELECT sid, w, max(pk - cum) dd FROM (SELECT *, greatest(0, max(cum) OVER (PARTITION BY sid, w ORDER BY entry_time)) pk FROM c) x GROUP BY 1,2)
SELECT t.sid, t.w, min(t.lbl) lbl, count(*) n, count(DISTINCT t.d) days, avg((t.pts >= 2)::int) won, avg((t.pts <= -2)::int) lost,
       sum(t.usd) net, sum(t.usd) FILTER (WHERE t.usd > 0) / nullif(-sum(t.usd) FILTER (WHERE t.usd < 0), 0) pf, max(dd.dd) dd
FROM t JOIN dd USING (sid, w) GROUP BY 1,2;
SELECT json_build_object('cand', (SELECT coalesce(json_agg(x ORDER BY x.w, x.won DESC, x.dd, x.net DESC), '[]') FROM (
         SELECT sid, w, lbl, n, days, round(won::numeric,3) won, round(lost::numeric,3) lost, round(net::numeric,0) net,
                round(coalesce(pf, 99)::numeric,2) pf, round(dd::numeric,0) dd
         FROM a WHERE n >= $MIN_TRADES AND days >= $MIN_DAYS AND net > 0 AND coalesce(pf, 99) >= $MIN_PF AND dd <= $MAX_DD) x),
       'inc', (SELECT coalesce(json_agg(x), '[]') FROM (
         SELECT sid, w, n, days, round(won::numeric,3) won, round(net::numeric,0) net, round(dd::numeric,0) dd FROM a
         WHERE sid IN (${SRC_LIST:-''}) AND n >= 5) x))" > data/scout/ranking.json || { say "query failed"; exit 1; }

# decisions (jq): per window, incumbents vs the best candidates
DEC=$(jq -c --argjson inc "$INC" --argjson edge "$WIN_EDGE" --argjson per "$PER_WINDOW" '
  . as $r
  | [ $r.cand[].w ] | unique | map(. as $w
      | ($r.inc | map(select(.w == $w))) as $iw
      | ($r.cand | map(select(.w == $w and ((.sid) as $s | ($inc|map(.src)|index($s)) == null)))) as $cw
      | if ($cw|length) == 0 then empty
        elif ($iw|length) == 0 then {w:$w, action:"add", cand:$cw[0], replace:null}
        else ($iw | min_by(.won)) as $weak
          | if ($cw[0].won*100 >= $weak.won*100 + $edge and $cw[0].dd <= $weak.dd)
            then {w:$w, action:(if ($iw|length) < $per then "add" else "replace" end), cand:$cw[0], replace:(if ($iw|length) < $per then null else $weak.sid end)}
            else {w:$w, action:"keep", cand:$cw[0], weakest:$weak} end
        end)' data/scout/ranking.json)
# losers: incumbents with >= 10 trades and net < 0 in their window → drop
DROP=$(jq -c --argjson inc "$INC" '[.inc[] | select(.n >= 10 and .net < 0)]' data/scout/ranking.json)
DEC=$(jq -c --argjson drop "$DROP" '. + ($drop | map({w, action:"drop", cand:null, replace:.sid, loser:.}))' <<<"$DEC")
echo "$DEC" | jq -c '.[]' | while read -r d; do say "decision: $d"; done
CHANGES=$(jq '[.[] | select(.action=="add" or .action=="replace" or .action=="drop")] | length' <<<"$DEC")
if (( DRY )); then say "dry run — $CHANGES change(s) proposed, nothing written"; exit 0; fi
(( CHANGES > 0 )) || { say "no change"; exit 0; }

# apply: copy the candidate's params into a sig_* id (publish_signals), unpublish the replaced incumbent's copy
cp "$FLEET" "data/scout/paper_fleet.$(date +%Y%m%d-%H%M).json"
TMP=$(mktemp); cp "$FLEET" "$TMP"
msg="🧭 Strategy scout — signal set updated (1 MNQ, rank: % won ≥2 pts, then drawdown):"
while read -r d; do
  sid=$(jq -r .cand.sid <<<"$d"); w=$(jq -r .w <<<"$d"); rep=$(jq -r '.replace // empty' <<<"$d")
  newid="sig_${w}_$(tr -c 'a-z0-9_\n' '_' <<<"${sid#nt_}" | cut -c1-40)"
  src_label=$(jq -r .cand.lbl <<<"$d")
  if [[ $src_label == tradeify ]]; then cfgsrc=$FLEET; else cfgsrc=config/$([[ $src_label == night ]] && echo night || echo gap)_fleet.json; fi
  params=$(jq -c --arg s "$sid" '.strategies[] | select(.id==$s) | .params' "$cfgsrc")
  [[ -n "$params" ]] || { say "params for $sid not found in $cfgsrc — skipped"; continue; }
  jq --arg id "$newid" --arg s "$sid" --argjson p "$params" --arg role "scout $(date +%F): window $w" '
     .strategies |= (map(select(.id != $id)) + [{id:$id, engine:"trend", enabled:true,
        params:($p + {publish_signals:true, signal_source:$s, signal_role:$role})}])' "$TMP" > "$TMP.n" && mv "$TMP.n" "$TMP"
  if [[ -n "$rep" ]]; then
    jq --arg s "$rep" '.strategies |= map(if (.id|startswith("sig_")) and .params.signal_source==$s then .params.publish_signals=false else . end)' "$TMP" > "$TMP.n" && mv "$TMP.n" "$TMP"
  fi
  msg+=$'\n'"• $w: $(jq -r '"ADD \(.cand.sid) — \(.cand.n) tr/\(.cand.days)d, \((.cand.won*100)|floor)% won, \((.cand.lost*100)|floor)% lost, PF \(.cand.pf), +$\(.cand.net), DD $\(.cand.dd)"' <<<"$d")${rep:+ (replaces $rep)}"
  say "applied: $newid ← $sid ($w)${rep:+, unpublished $rep}"
done < <(jq -c '.[] | select(.action=="add" or .action=="replace")' <<<"$DEC")
while read -r d; do
  rep=$(jq -r .replace <<<"$d")
  jq --arg s "$rep" '.strategies |= map(if (.id|startswith("sig_")) and .params.signal_source==$s then .params.publish_signals=false else . end)' "$TMP" > "$TMP.n" && mv "$TMP.n" "$TMP"
  msg+=$'\n'"• $(jq -r '"\(.w): DROP \(.loser.sid) — negative over its last \(.loser.n) trades (net $\(.loser.net))"' <<<"$d")"
  say "dropped: $rep"
done < <(jq -c '.[] | select(.action=="drop")' <<<"$DEC")
mv "$TMP" "$FLEET"
"$HOME/.local/bin/grid-notify" "$msg"$'\n'"Takes effect at the next paper-engine restart (after the close)." >/dev/null 2>&1 || true
hm=$((10#$(TZ=America/New_York date +%H%M)))
if (( hm >= 1605 && hm < 1755 )) && ! pgrep -f '[p]aper_engine .*--replay-from' >/dev/null; then
  systemctl --user restart paper-engine-local && say "paper-engine-local restarted with the new set"
else say "outside 16:05-17:55 ET or a replay is running — the new set starts at the next evening restart"; fi
