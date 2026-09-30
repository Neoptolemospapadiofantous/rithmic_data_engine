#!/usr/bin/env bash
# desk_replay.sh — closes the StarNet desk loop: RESEARCHER proposes paper variants, this script
# (deterministic, no LLM) validates them, registers them under the research label 'desk' and
# replays them over every recorded NQ session, so the next cycle's export → QUANT → SKEPTIC can
# judge them. Nothing here touches the live paper fleet (label 'tradeify'), any config file, or
# a live executor.
#
# Input : every ~/starnet-work/desk/*-RESEARCHER*.md not yet replayed (one per researcher). Each carries ONE fenced
#         ```json block {"variants":[{"id":"desk_…","engine":"orb|trend|mtf_scalper","params":{…}}]}.
# Output: ~/starnet-work/desk/<stamp>-REPLAY.md (accepted, rejected + why, trades per variant) —
#         RESEARCHER reads it next cycle, so a rejected key or a dead variant is fed back.
#
# Guards (each exists because the alternative silently corrupts something):
#   · ids must be NEW and match ^desk_[a-z0-9_]{3,60}$ — paper_strategies.strategy_id is a global
#     key and a registry upsert rewrites account_label, so reusing a fleet id would relabel it.
#   · param keys must already be used by that engine in config/paper_fleet.json — the engine
#     ignores unknown keys, so a typo would otherwise replay the defaults and look like a result.
#   · at most MAX_VARIANTS per run; a variant's earlier 'desk' trades are deleted before its replay
#     (only label 'desk', only those ids), so re-proposing an id never double-counts.
set -euo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
DESK="${DESK_DIR:-$HOME/starnet-work/desk}"
STATE="${DESK_REPLAY_STATE:-$HOME/.local/share/rithmic-desk}"
LABEL="desk"
HOLDOUT_LABEL="desk_ho"
# Sessions on/after this date are HOLDOUT: replayed under desk_ho, never in the data researchers tune on
# (export_research_data.sh keeps desk_ho out of daily_pnl_1full and the desk leaderboard). Every new
# session lands here automatically. Moving this date forward re-exposes held-out days: don't.
HOLDOUT_FROM="${DESK_HOLDOUT_FROM:-2026-09-28}"
MAX_VARIANTS="${DESK_MAX_VARIANTS:-30}"
mkdir -p "$STATE" "$DESK"
log() { echo "[desk-replay] $(date -Is) $*"; }

exec 9>"$STATE/lock"
flock -n 9 || { log "another replay is running — skipping"; exit 0; }

# every RESEARCHER file (one per researcher: *-RESEARCHER*.md) not replayed yet, oldest first
touch "$STATE/processed"
mapfile -t srcs < <(for f in $(ls -1 "$DESK"/*-RESEARCHER*.md 2>/dev/null | sort); do
  grep -qxF "$f:$(stat -c %Y "$f")" "$STATE/processed" || echo "$f"; done)
(( ${#srcs[@]} )) || { log "no new RESEARCHER file"; exit 0; }
mark_done() { for f in "${srcs[@]}"; do echo "$f:$(stat -c %Y "$f")" >> "$STATE/processed"; done; }
src_names=$(for f in "${srcs[@]}"; do printf '`%s` ' "$(basename "$f")"; done)

stamp=$(date +%F-%H%M)
report="$DESK/$stamp-REPLAY.md"
work="$STATE/$stamp"; mkdir -p "$work"

# the one fenced json block of each file, merged (first proposal of an id wins)
: > "$work/proposal.jsonl"; : > "$work/noblock.txt"
for f in "${srcs[@]}"; do
  awk '/^```json[[:space:]]*$/{f=1;next} /^```[[:space:]]*$/{if(f){exit}} f' "$f" > "$work/one.json"
  if jq -e '.variants | type == "array"' "$work/one.json" >/dev/null 2>&1; then
    jq -c --arg s "$(basename "$f")" '.variants[] | . + {_src: $s}' "$work/one.json" >> "$work/proposal.jsonl"
  else
    echo "- \`$(basename "$f")\`: no fenced \`\`\`json block with a \`variants\` array" >> "$work/noblock.txt"
  fi
done
jq -s '{variants: (group_by(.id) | map(.[0]))}' "$work/proposal.jsonl" > "$work/proposal.json"
if [[ "$(jq '.variants | length' "$work/proposal.json")" == 0 ]]; then
  { echo "# REPLAY $stamp — nothing replayed"; echo; echo "Sources: $src_names"; echo;
    cat "$work/noblock.txt"; } > "$report"
  mark_done; log "no variants in ${#srcs[@]} file(s)"; exit 0
fi

set -a; . "$REAL/.env"; set +a
export PGHOST="$PG_HOST" PGPORT="$PG_PORT" PGUSER="$PG_USER" PGDATABASE="$PG_DB" PGPASSWORD="$PG_PASSWORD"
existing=$(psql -X -At -c "select strategy_id from paper_strategies")

# allowed param keys per engine = every key the fleet already uses for that engine
jq '[.strategies[] | {e: .engine, k: (.params // {} | keys)}] | group_by(.e)
    | map({key: .[0].e, value: (map(.k) | add | unique)}) | from_entries' \
  "$REAL/config/paper_fleet.json" > "$work/allowed.json"

accepted=(); : > "$work/rejected.txt"
n=$(jq '.variants | length' "$work/proposal.json")
for ((i = 0; i < n; i++)); do
  v=$(jq -c ".variants[$i]" "$work/proposal.json")
  id=$(jq -r '.id // ""' <<<"$v"); eng=$(jq -r '.engine // ""' <<<"$v")
  why=""
  [[ "$id" =~ ^desk_[a-z0-9_]{3,60}$ ]] || why="id must match ^desk_[a-z0-9_]{3,60}\$"
  [[ -z "$why" && ! "$eng" =~ ^(orb|trend|mtf_scalper)$ ]] && why="engine must be orb, trend or mtf_scalper"
  if [[ -z "$why" ]] && grep -qxF "$id" <<<"$existing"; then
    # an id already registered is only reusable if it is OURS (label desk)
    lab=$(psql -X -At -c "select account_label from paper_strategies where strategy_id='$id'")
    [[ "$lab" == "$LABEL" ]] || why="id already exists under label '$lab' — pick a new desk_ id"
  fi
  if [[ -z "$why" ]]; then
    bad=$(jq -r --argjson a "$(cat "$work/allowed.json")" --arg e "$eng" \
      '(.params // {} | keys) - ($a[$e] // []) | join(", ")' <<<"$v")
    [[ -n "$bad" ]] && why="unknown param keys for $eng: $bad"
  fi
  if [[ -z "$why" ]] && (( ${#accepted[@]} >= MAX_VARIANTS )); then why="over the $MAX_VARIANTS-variant cap"; fi
  if [[ -n "$why" ]]; then echo "- \`${id:-?}\` ($(jq -r ._src <<<"$v")): $why" >> "$work/rejected.txt"; continue; fi
  accepted+=("$id")
  jq -c '{id, engine, enabled: true, params: (.params // {})}' <<<"$v" >> "$work/accepted.jsonl"
done

if (( ${#accepted[@]} == 0 )); then
  { echo "# REPLAY $stamp — nothing replayed"; echo; echo "Sources: $src_names"; echo;
    echo "## Rejected"; cat "$work/rejected.txt" "$work/noblock.txt"; } > "$report"
  mark_done; log "all variants rejected"; exit 0
fi

# replay fleet = the live fleet's settings, label desk, only the accepted variants
jq --slurpfile v "$work/accepted.jsonl" --arg l "$LABEL" '.account_label = $l | .strategies = $v' \
  "$REAL/config/paper_fleet.json" > "$work/fleet.json"

# register (replay mode records trades only; unregistered ids would be dropped by the FK) and
# clear this label's earlier rows for exactly these ids
while read -r row; do
  id=$(jq -r .id <<<"$row"); eng=$(jq -r .engine <<<"$row"); params=$(jq -c .params <<<"$row")
  psql -X -q -v ON_ERROR_STOP=1 -v id="$id" -v eng="$eng" -v p="$params" -v l="$LABEL" <<'SQL'
INSERT INTO paper_strategies (strategy_id, account_label, engine, params_json, enabled)
VALUES (:'id', :'l', :'eng', :'p'::jsonb, false)
ON CONFLICT (strategy_id) DO UPDATE SET engine = EXCLUDED.engine, params_json = EXCLUDED.params_json
  WHERE paper_strategies.account_label = :'l';
DELETE FROM paper_trades WHERE account_label IN (:'l', :'l' || '_ho') AND strategy_id = :'id';
SQL
done < "$work/accepted.jsonl"

# every completed NQ session (today only once the RTH close has passed)
today_et=$(TZ=America/New_York date +%F); now_et=$(TZ=America/New_York date +%H%M)
mapfile -t days < <(psql -X -At -c "select session_date from session_stats where symbol='NQ' order by 1")
(( 10#$now_et >= 1610 )) && ! printf '%s\n' "${days[@]}" | grep -qx "$today_et" && days+=("$today_et")
log "replaying ${#accepted[@]} variant(s) over ${#days[@]} session(s)"
fails=0
for d in "${days[@]}"; do
  [[ "$d" > "$today_et" ]] && continue
  lab="$LABEL"; [[ ! "$d" < "$HOLDOUT_FROM" ]] && lab="$HOLDOUT_LABEL"
  if ! ( cd "$REAL" && nice -n 15 timeout 20m build/paper_engine --config "$work/fleet.json" \
           --account-label "$lab" --replay-from "$d 09:25" --replay-to "$d 16:05" ) \
         >> "$work/engine.log" 2>&1; then
    fails=$((fails + 1)); log "WARN replay of $d failed (see $work/engine.log)"
  fi
done

# per-variant result
{
  echo "# REPLAY $stamp — ${#accepted[@]} variant(s), ${#days[@]} session(s)"
  echo
  echo "Sources: $src_names· label \`$LABEL\` · RTH 09:25–16:05 ET per session · engine failures: $fails"
  echo "Paper fills are simulated: treat every number as optimistic (see the newest FILLS file for real slippage). In-sample ranking: \`leaderboard_desk_session.csv\` (next export)."
  echo
  echo "In-sample only (sessions before $HOLDOUT_FROM, label \`$LABEL\`). Holdout results are kept out of the research loop on purpose."
  echo
  echo "| id | trades | sessions | wins | net per contract \$ (1 full, after \$4 RT) |"
  echo "|---|---|---|---|---|"
  psql -X -At -F ' | ' -c "
    select t.strategy_id, count(*), count(distinct (t.entry_time at time zone 'America/New_York')::date),
           sum((t.pnl_pts > 0)::int),
           round((sum(t.pnl_pts * case when t.symbol='MES' then 50 else 20 end) - 4.0 * count(*))::numeric / greatest(count(*),1), 2)
      from paper_trades t
     where t.account_label = '$LABEL' and t.strategy_id = any(string_to_array('$(IFS=,; echo "${accepted[*]}")', ','))
     group by 1 order by 5 desc" | sed 's/^/| /; s/$/ |/'
  for id in "${accepted[@]}"; do
    psql -X -At -c "select 1 from paper_trades where account_label in ('$LABEL','$HOLDOUT_LABEL') and strategy_id='$id' limit 1" | grep -q 1 \
      || echo "| $id | 0 | 0 | 0 | — (never traded: check the params make the mode fire) |"
  done
  if [[ -s "$work/rejected.txt" || -s "$work/noblock.txt" ]]; then echo; echo "## Rejected"; cat "$work/rejected.txt" "$work/noblock.txt"; fi
} > "$report"

# HOLDOUT verdicts go to a folder the research loop does not read (only the founder's BRIEF does), so the
# loop cannot tune on held-out sessions through the verdicts.
HOLD_DIR="${DESK_HOLDOUT_DIR:-$HOME/starnet-work/holdout}"; mkdir -p "$HOLD_DIR"
{
  echo "# HOLDOUT $stamp — ${#accepted[@]} variant(s) from $src_names"
  echo
  echo "In-sample = sessions before $HOLDOUT_FROM (label \`$LABEL\`). Holdout = $HOLDOUT_FROM onward (label \`$HOLDOUT_LABEL\`), judged here only:"
  echo "PASS = in-sample net > 0 AND holdout ≥5 trades, net > 0, PF ≥ 1 (after \$4 RT, 1 full contract); THIN = fewer than 5 holdout trades."
  echo
  echo "| id | in-sample trades | in-sample net/ct \$ | holdout trades | holdout net/ct \$ | holdout PF | holdout verdict |"
  echo "|---|---|---|---|---|---|---|"
  ids_csv=$(IFS=,; echo "${accepted[*]}")
  psql -X -At -F ' | ' -c "
    with t as (select strategy_id, account_label,
                      pnl_pts * case when symbol='MES' then 50 else 20 end - 4.0 as usd
                 from paper_trades
                where account_label in ('$LABEL','$HOLDOUT_LABEL')
                  and strategy_id = any(string_to_array('$ids_csv', ','))),
         a as (select strategy_id,
                 count(*) filter (where account_label='$LABEL') is_n,
                 round((avg(usd) filter (where account_label='$LABEL'))::numeric, 2) is_npc,
                 count(*) filter (where account_label='$HOLDOUT_LABEL') ho_n,
                 round((avg(usd) filter (where account_label='$HOLDOUT_LABEL'))::numeric, 2) ho_npc,
                 round((sum(usd) filter (where account_label='$HOLDOUT_LABEL' and usd > 0)
                   / nullif(-sum(usd) filter (where account_label='$HOLDOUT_LABEL' and usd < 0), 0))::numeric, 2) ho_pf
               from t group by 1)
    select strategy_id, is_n, coalesce(is_npc::text,'—'), ho_n, coalesce(ho_npc::text,'—'), coalesce(ho_pf::text,'—'),
           case when ho_n < 5 then 'THIN' when ho_npc > 0 and coalesce(ho_pf, 99) >= 1 and is_npc > 0 then 'PASS' else 'FAIL' end
      from a order by (case when ho_n >= 5 and ho_npc > 0 then 0 else 1 end), is_npc desc nulls last" | sed 's/^/| /; s/$/ |/'
} > "$HOLD_DIR/$stamp-HOLDOUT.md"
mark_done
log "report $report"
