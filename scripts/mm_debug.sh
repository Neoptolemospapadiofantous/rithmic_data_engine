#!/usr/bin/env bash
# mm_debug.sh — see and debug every step of the Lighter PAPER market maker (src/dex/mm_paper_main.cpp) from a shell:
# for a human, and for an AI operator (--json). READ-ONLY: it never writes to Postgres, never touches a unit, a status
# file or the archive. Every subcommand prints a one-line verdict FIRST ("OK|WARN|FAIL|INFO <subcommand>: …"), then
# details. With --json it prints ONE compact JSON object instead, whose first key is "verdict" (same text).
#
#   status                         every component of each label's status.json (feed, reorder, sim, db, risk — each with
#                                  its `ok` verdict and age of the file) + the systemd unit state + G1..G4 gate status +
#                                  Lighter archive freshness (newest file per market, stale after 5 min).
#   trace fill <uid>               the fill (mm_paper_fills.uid) and everything around it (±30 s): quote rows, fills,
#                                  events, minute equity — in time order.
#   trace market <MKT> --from T --to T
#                                  quotes, fills, events and minute equity of one market in a window, in time order.
#   why <MKT> <ISO time>           the mm_paper_quotes row in force at that time (bid/ask, reasons, the book it saw —
#                                  mid/best bid/best ask — inventory, equity), the next change, and what each reason means.
#   replay <MKT> --from T --to T   runs build/mm_sim --daily --t0-ms/--t1-ms over the archive window with the engine's own
#                                  setting (the hl::Params it publishes as status.json markets[].params — fill model passed as
#                                  MM_FILLS, queue bound from `cancels`; else data/mm_gates/params_lighter.json)
#                                  and compares with mm_paper_fills (maker fills only) for the same window: fill count,
#                                  volume, P&L (paper = Δ minute equity inside the window), with a parity verdict
#                                  (PARITY: fills within max(3, 25 %) and volume within max($300, 25 %)).
#                                  ONLY MEANINGFUL FOR A WINDOW THAT STARTS FLAT: the replay starts with no inventory and no
#                                  orders; the paper row in force at T0 is checked and a non-flat start is flagged.
#                                  Known reasons a live paper window and a replay differ (not bugs):
#                                    1. state at T: the engine carries inventory, resting orders + queue places, a jump pause
#                                       and requote history into the window; the replay starts flat with no orders;
#                                    2. different connections: the archive is lighter_recorder's own websocket, the engine's
#                                       messages arrive at other times (re-order buffer, late trades), reconnects differ;
#                                    3. halts the replay does not see: stale market / feed down / kill / loss stop / day roll;
#                                    4. the replay flattens at T1 (taker, not counted in fills) — paper P&L is mark-to-mid;
#                                    5. an engine restart inside the window resets the paper simulator (see events 'start').
#                                  A window crossing 00:00 UTC is refused (since 2026-10-26 the engine carries inventory
#                                  over the roll and its day equity restarts there; split the window at midnight). With
#                                  inventory carried across days a flat T0 is rarer — the not-flat warning still applies.
#   archive [venue]                per archive file (default: today's UTC files of every market of the venue, default
#                                  lighter): lines, skipped zstd frames, Lighter sequence gaps, snapshots, books, trades,
#                                  hours, longest book silence (build/archive_check). Options --day YYYY-MM-DD, --market M.
#   db                             per label: writer queue / retries / errors / drops / last commit (status.json), the
#                                  persisted drop ledger (db_drops_<label>.json) and rows written in the last hour per table.
#   gates                          data/mm_gates/gates.json: each gate's status + metrics, and why each day is excluded
#                                  (G1: hours of books / max gap below the completeness rule; G2: g2_excluded reasons).
#
# Options (anywhere on the line; environment variable in brackets; relative paths are from the repo root):
#   --json                 machine output (one JSON object, "verdict" first)
#   --label L              [MM_DEBUG_LABEL]       label for trace/why/replay (default paper)
#   --labels A,B           [MM_DEBUG_LABELS]      labels for status/db (default paper,paper_220,paper_hl,paper_opt)
#   --schema S             [MM_DEBUG_SCHEMA]      Postgres search_path (PGOPTIONS) — e.g. mm_selftest; default the server's
#                                                 (must match ^[a-z_]+$)
#   --status-dir D         [MM_DEBUG_STATUS_DIR]  where status files + drop ledgers live (default data/mm_paper); the file of
#                                                 label 'paper' is status.json, any other label status_<label>.json
#   --archive D            [MM_DEBUG_ARCHIVE]     archive root holding <venue>/<MARKET>/<day>.jsonl.zst (default
#                                                 /media/theone/381C48C51C48803C/rithmic_dex)
#   --bin D                [MM_DEBUG_BIN]         where mm_sim + archive_check are (default build)
#   --gates F              [MM_DEBUG_GATES]       gates.json (default data/mm_gates/gates.json)
#   --env F                [MM_DEBUG_ENV]         .env with PG_HOST/PG_PORT/PG_DB/PG_USER/PG_PASSWORD (default .env; read
#                                                 literally, never printed; PG* variables already set win)
#   --from T / --to T      ISO times (anything `date -d` reads; UTC unless the text says otherwise)
#
# Selftest (scripts against the isolated schema, no production data):
#   scripts/mm_debug.sh --schema mm_selftest --status-dir <dir> --labels selftest --label selftest status
set -uo pipefail
cd "$(dirname "$0")/.."

JSON=0
LABEL=${MM_DEBUG_LABEL:-paper}
LABELS=${MM_DEBUG_LABELS:-paper,paper_220,paper_hl,paper_opt}
SCHEMA=${MM_DEBUG_SCHEMA:-}
STATUS_DIR=${MM_DEBUG_STATUS_DIR:-data/mm_paper}
ARCHIVE=${MM_DEBUG_ARCHIVE:-/media/theone/381C48C51C48803C/rithmic_dex}
BIN=${MM_DEBUG_BIN:-build}
GATES=${MM_DEBUG_GATES:-data/mm_gates/gates.json}
ENVF=${MM_DEBUG_ENV:-.env}
FROM=""; TO=""; DAY=""; MARKET=""
ARGS=()
while [ $# -gt 0 ]; do
  case "$1" in
    --json) JSON=1 ;;
    --label) LABEL=$2; shift ;;
    --labels) LABELS=$2; shift ;;
    --schema) SCHEMA=$2; shift ;;
    --status-dir) STATUS_DIR=$2; shift ;;
    --archive) ARCHIVE=$2; shift ;;
    --bin) BIN=$2; shift ;;
    --gates) GATES=$2; shift ;;
    --env) ENVF=$2; shift ;;
    --from) FROM=$2; shift ;;
    --to) TO=$2; shift ;;
    --day) DAY=$2; shift ;;
    --market) MARKET=$2; shift ;;
    -h|--help) sed -n '2,/^set -uo/p' "$0" | sed '$d'; exit 0 ;;
    *) ARGS+=("$1") ;;
  esac
  shift
done
set -- "${ARGS[@]+"${ARGS[@]}"}"
CMD=${1:-}; shift || true

TMPD=""
trap '[ -n "$TMPD" ] && rm -rf "$TMPD"' EXIT   # replay's scratch dir, removed on every exit path (die included)

die() { echo "FAIL ${CMD:-mm_debug}: $*"; exit 2; }
command -v jq >/dev/null || die "jq not installed"
# The schema goes into PGOPTIONS (libpq parses it as server options): plain lower-case identifiers only.
if [ -n "$SCHEMA" ] && ! [[ "$SCHEMA" =~ ^[a-z_]+$ ]]; then die "--schema / MM_DEBUG_SCHEMA must match ^[a-z_]+$ (got: $SCHEMA)"; fi

# ── Postgres (read-only) ─────────────────────────────────────────────────────────────────────────────────────
envget() { [ -f "$ENVF" ] && grep -m1 "^$1=" "$ENVF" | cut -d= -f2- | sed -e 's/^"\(.*\)"$/\1/' -e "s/^'\(.*\)'$/\1/"; }
pg_setup() {
  export PGHOST=${PGHOST:-$(envget PG_HOST)} PGPORT=${PGPORT:-$(envget PG_PORT)} PGDATABASE=${PGDATABASE:-$(envget PG_DB)}
  export PGUSER=${PGUSER:-$(envget PG_USER)}
  if [ -z "${PGPASSWORD:-}" ]; then PGPASSWORD=$(envget PG_PASSWORD); export PGPASSWORD; fi
  [ -n "$SCHEMA" ] && export PGOPTIONS="-c search_path=$SCHEMA"
  export PGAPPNAME=mm_debug PGTZ=UTC
}
# q SQL [psql -v args...] — SQL read from stdin so :'var' substitution works; one JSON value out (the query must
# return a single json column), "null" when empty.
q() {
  local sql=$1; shift
  local out
  out=$(printf '%s\n' "BEGIN READ ONLY;" "$sql" "COMMIT;" | psql -X -q -A -t -v ON_ERROR_STOP=1 "$@" 2>&1) || { echo "__ERR__ ${out//$'\n'/ }"; return 1; }
  out=$(printf '%s\n' "$out" | grep -v '^$' | tr -d '\n')   # json_agg breaks lines between elements; JSON strings never hold a raw newline
  echo "${out:-null}"
}
ms_of() { date -u -d "$1" +%s%3N 2>/dev/null; }
iso_of_ms() { date -u -d "@$(( $1 / 1000 )).$(printf '%03d' $(( $1 % 1000 )))" +%Y-%m-%dT%H:%M:%S.%3NZ; }
join() { local o=""; for x in "$@"; do o+="${o:+; }$x"; done; printf '%s' "$o"; }
status_file() { if [ "$1" = paper ]; then echo "$STATUS_DIR/status.json"; else echo "$STATUS_DIR/status_$1.json"; fi; }
unit_of() { case "$1" in paper) echo mm-paper-local ;; paper_220) echo mm-paper-fast-local ;; paper_hl) echo mm-paper-hl-local ;; paper_opt) echo mm-paper-opt-local ;; *) echo "" ;; esac; }
# emit VERDICT TEXT_BODY JSON_BODY — text: verdict line + body; json: {"verdict":…} + body object merged
emit() {
  if [ $JSON = 1 ]; then jq -cn --arg v "$1" --argjson b "${3:-{\}}" '{verdict:$v} + $b'
  else echo "$1"; [ -n "$2" ] && printf '%s\n' "$2"; fi
}

# ── status ───────────────────────────────────────────────────────────────────────────────────────────────────
cmd_status() {
  local now_ms; now_ms=$(date +%s%3N)
  local labels_json='[]' problems=() text=""
  IFS=, read -ra LS <<< "$LABELS"
  for L in "${LS[@]}"; do
    local f; f=$(status_file "$L")
    local unit; unit=$(unit_of "$L")
    local ustate="n/a"
    [ -n "$unit" ] && ustate=$(systemctl --user is-active "$unit" 2>/dev/null || true)
    [ -z "$ustate" ] && ustate=unknown
    if [ ! -f "$f" ]; then
      problems+=("$L: no status file ($f)")
      labels_json=$(jq -c --arg l "$L" --arg u "$ustate" --arg f "$f" '. + [{label:$l, unit:$u, status_file:$f, error:"missing"}]' <<< "$labels_json")
      text+="[$L] status file missing: $f (unit $ustate)"$'\n'
      continue
    fi
    local age_s; age_s=$(( ( $(date +%s) - $(stat -c %Y "$f") ) ))
    local s; s=$(jq -c --argjson age "$age_s" --arg l "$L" --arg u "$ustate" --arg f "$f" '{label:$l, unit:$u, status_file:$f, age_s:$age, updated, day, equity_usd,
        gross_inventory_usd, halted, fills, quote_rows, components,
        markets_quoting: ([.markets[]? | select(.bid_reason=="quoting" or .bid_reason=="kept" or .ask_reason=="quoting" or .ask_reason=="kept")] | length),
        markets_total: ([.markets[]?] | length),
        reasons_now: ([.markets[]? | .bid_reason, .ask_reason | select(. != null)] | group_by(.) | map({(.[0]): length}) | add // {})}' "$f")
    labels_json=$(jq -c --argjson s "$s" '. + [$s]' <<< "$labels_json")
    [ "$age_s" -gt 60 ] && problems+=("$L: status.json is ${age_s}s old")
    if [ -n "$unit" ] && [ "$ustate" != active ]; then problems+=("$L: unit $unit $ustate"); fi
    local bad; bad=$(jq -r '.components // {} | to_entries[] | select(.value.ok != true) | .key' <<< "$s" | paste -sd, -)
    [ "$(jq '.components == null' <<< "$s")" = true ] && problems+=("$L: status.json has no components (old binary?)")
    [ -n "$bad" ] && problems+=("$L: not ok: $bad")
    text+=$(jq -r '"[\(.label)] unit \(.unit), status \(.age_s)s old, day \(.day), equity $\(.equity_usd*100|round/100), gross $\(.gross_inventory_usd|round), fills \(.fills), quote rows \(.quote_rows // "—"), quoting \(.markets_quoting)/\(.markets_total) markets, halted \(.halted)",
        "    reasons now: \(.reasons_now | to_entries | map("\(.key) \(.value)") | join(", "))",
        (.components // {} | to_entries[] | "    \(.key): \(if .value.ok then "ok" else "NOT OK" end)  \(.value | del(.ok) | to_entries | map("\(.key)=\(.value)") | join(" "))")' <<< "$s")$'\n'
  done
  # gates
  local gates='null'
  if [ -f "$GATES" ]; then
    gates=$(jq -c '{generated, gates: [.gates[] | {id, status, progress}]}' "$GATES")
    text+="gates ($(jq -r .generated <<< "$gates")): $(jq -r '[.gates[] | "\(.id) \(.status) \(.progress.done // "-")/\(.progress.needed // "-")"] | join(", ")' <<< "$gates")"$'\n'
    jq -e '[.gates[] | select(.status=="fail")] | length > 0' <<< "$gates" >/dev/null && problems+=("a gate FAILED")
  else problems+=("no gates file $GATES"); fi
  # archive freshness (Lighter: the paper engine's markets)
  local arch='[]'
  if [ -d "$ARCHIVE/lighter" ]; then
    local today; today=$(date -u +%F)
    for d in "$ARCHIVE"/lighter/*/; do
      local m; m=$(basename "$d")
      case $m in _*) continue;; esac   # _meta/ = the daily orderBookDetails snapshots, not a market
      local newest; newest=$(ls -1t "$d" 2>/dev/null | head -1)
      [ -z "$newest" ] && continue
      local age=$(( $(date +%s) - $(stat -c %Y "$d/$newest") ))
      arch=$(jq -c --arg m "$m" --arg f "$newest" --argjson a "$age" '. + [{market:$m, file:$f, age_s:$a}]' <<< "$arch")
    done
    local stale; stale=$(jq -r '[.[] | select(.age_s > 300) | .market] | join(",")' <<< "$arch")
    [ -n "$stale" ] && problems+=("archive stale >5 min: $stale")
    text+="archive $ARCHIVE/lighter: $(jq 'length' <<< "$arch") markets, newest file age max $(jq '[.[].age_s] | max // 0' <<< "$arch") s$( [ -n "$stale" ] && echo ", STALE: $stale")"$'\n'
    text+="    recorder unit lighter-recorder-local: $(systemctl --user is-active lighter-recorder-local 2>/dev/null || echo unknown)"$'\n'
  else problems+=("archive dir $ARCHIVE/lighter missing"); fi
  local v
  if [ ${#problems[@]} -eq 0 ]; then v="OK status: every component ok on ${LABELS}"; else v="WARN status: $(join "${problems[@]}")"; fi
  emit "$v" "${text%$'\n'}" "$(jq -cn --argjson l "$labels_json" --argjson g "$gates" --argjson a "$arch" '{labels:$l, gates:$g, archive:$a}')"
}

# ── trace ────────────────────────────────────────────────────────────────────────────────────────────────────
TRACE_SQL="
SELECT coalesce(json_agg(x ORDER BY x.ts, x.kind), '[]'::json) FROM (
  SELECT ts, 'quote' AS kind, market,
         format('bid %s (%s) ask %s (%s) | book %s/%s mid %s | inv %s ($%s) eq $%s',
                coalesce(bid_px::text,'—'), bid_reason, coalesce(ask_px::text,'—'), ask_reason, best_bid, best_ask, mid,
                round(inv::numeric,6), round(inv_usd::numeric,2), round(equity::numeric,4)) AS detail
    FROM mm_paper_quotes WHERE label = :'L' AND market = :'M' AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz
  UNION ALL
  SELECT ts, CASE WHEN taker THEN 'flatten' ELSE 'fill' END, market,
         format('%s %s @ %s (mid %s) inv after %s uid %s', CASE WHEN side > 0 THEN 'BUY' ELSE 'SELL' END, qty, price, mid,
                round(inv_after::numeric,6), coalesce(uid,'—'))
    FROM mm_paper_fills WHERE label = :'L' AND market = :'M' AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz
  UNION ALL
  SELECT ts, 'event:' || kind, market, detail
    FROM mm_paper_events WHERE label = :'L' AND (market = :'M' OR market = '') AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz
  UNION ALL
  SELECT ts, 'equity', market, format('equity $%s inv %s fills %s halted %s', round(equity::numeric,4), round(inv::numeric,6), fills, halted)
    FROM mm_paper_equity WHERE label = :'L' AND market = :'M' AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz
) x;"
trace_print() {   # $1 = rows json, $2 = header verdict
  local rows=$1
  local n; n=$(jq 'length' <<< "$rows")
  local counts; counts=$(jq -c 'group_by(.kind) | map({(.[0].kind): length}) | add // {}' <<< "$rows")
  local v="$2 — $n rows ($(jq -r 'to_entries | map("\(.key) \(.value)") | join(", ")' <<< "$counts"))"
  [ "$n" = 0 ] && v="WARN ${v#INFO }"
  emit "$v" "$(jq -r '.[] | "\(.ts)  \(.kind | . + "          " | .[0:14]) \(.market)  \(.detail)"' <<< "$rows")" \
       "$(jq -cn --argjson r "$rows" --argjson c "$counts" '{counts:$c, rows:$r}')"
}
cmd_trace() {
  pg_setup
  local what=${1:-}; shift || true
  case "$what" in
    fill)
      local uid=${1:-}; [ -z "$uid" ] && die "trace fill <uid>"
      local f; f=$(q "SELECT row_to_json(f) FROM (SELECT ts, market, label, side, price, qty, mid, taker, inv_after, uid, run_id FROM mm_paper_fills WHERE uid = :'U' LIMIT 1) f;" -v U="$uid") || die "$f"
      [ "$f" = null ] && { emit "FAIL trace: no fill with uid $uid" "" '{}'; exit 1; }
      local ts m l; ts=$(jq -r .ts <<< "$f"); m=$(jq -r .market <<< "$f"); l=$(jq -r .label <<< "$f")
      local t0 t1; t0=$(date -u -d "$ts - 30 seconds" +%FT%T.%3NZ); t1=$(date -u -d "$ts + 30 seconds" +%FT%T.%3NZ)
      local rows; rows=$(q "$TRACE_SQL" -v L="$l" -v M="$m" -v T0="$t0" -v T1="$t1") || die "$rows"
      if [ $JSON = 1 ]; then
        jq -cn --arg v "INFO trace fill $uid: $m $(jq -r '"\(if .side>0 then "BUY" else "SELL" end) \(.qty) @ \(.price) at \(.ts)"' <<< "$f") — $(jq length <<< "$rows") rows ±30 s" \
           --argjson f "$f" --argjson r "$rows" '{verdict:$v, fill:$f, rows:$r}'
      else
        echo "INFO trace fill $uid: $m $(jq -r '"\(if .side>0 then "BUY" else "SELL" end) \(.qty) @ \(.price) (mid \(.mid), taker \(.taker)) at \(.ts), label \(.label)"' <<< "$f") — ±30 s:"
        jq -r '.[] | "\(.ts)  \(.kind | . + "          " | .[0:14]) \(.market)  \(.detail)"' <<< "$rows"
      fi ;;
    market)
      local m=${1:-}; [ -z "$m" ] && die "trace market <MKT> --from T --to T"
      [ -z "$FROM" ] || [ -z "$TO" ] && die "trace market needs --from and --to"
      local t0 t1; t0=$(date -u -d "$FROM" +%FT%T.%3NZ) || die "bad --from"; t1=$(date -u -d "$TO" +%FT%T.%3NZ) || die "bad --to"
      local rows; rows=$(q "$TRACE_SQL" -v L="$LABEL" -v M="$m" -v T0="$t0" -v T1="$t1") || die "$rows"
      trace_print "$rows" "INFO trace market $m [$LABEL] $t0 → $t1" ;;
    *) die "trace fill <uid> | trace market <MKT> --from T --to T" ;;
  esac
}

# ── why ──────────────────────────────────────────────────────────────────────────────────────────────────────
reason_text() {
  case "$1" in
    quoting) echo "allowed: an order sits (or is being sent) at the strategy's target price" ;;
    kept) echo "allowed: the resting order is kept at its old price — the target moved ≤ requote_ticks, so the queue place is worth more" ;;
    no_book) echo "no valid book yet (just started / reconnected / day rolled)" ;;
    halted) echo "risk control: kill switch, daily loss stop, feed down or this market stale — every quote pulled" ;;
    spread_narrow) echo "the market's spread is below min_spread_ticks (4 HL ticks): too tight to earn the width" ;;
    jump_pause) echo "the mid jumped ≥ pause_move_ticks (2) — both quotes pulled for pause_ms (2 s)" ;;
    imbalance_pull) echo "top-of-book imbalance ≥ imb_pull (0.6) against this side: it is about to be run over" ;;
    inv_cap) echo "one more fill on this side would exceed the inventory cap (max_inv_units × order_usd)" ;;
    *) echo "?" ;;
  esac
}
cmd_why() {
  pg_setup
  local m=${1:-} t=${2:-}
  [ -z "$m" ] || [ -z "$t" ] && die "why <MKT> <ISO time>"
  local ti; ti=$(date -u -d "$t" +%FT%T.%3NZ) || die "bad time $t"
  local row; row=$(q "SELECT row_to_json(r) FROM (SELECT * FROM mm_paper_quotes WHERE label = :'L' AND market = :'M' AND ts <= :'T'::timestamptz ORDER BY ts DESC LIMIT 1) r;" -v L="$LABEL" -v M="$m" -v T="$ti") || die "$row"
  local nxt; nxt=$(q "SELECT row_to_json(r) FROM (SELECT * FROM mm_paper_quotes WHERE label = :'L' AND market = :'M' AND ts > :'T'::timestamptz ORDER BY ts LIMIT 1) r;" -v L="$LABEL" -v M="$m" -v T="$ti") || die "$nxt"
  local ctx; ctx=$(q "SELECT coalesce(json_agg(x ORDER BY ts DESC), '[]'::json) FROM (SELECT ts, kind, market, detail FROM mm_paper_events WHERE label = :'L' AND (market = :'M' OR market = '') AND ts <= :'T'::timestamptz AND ts > :'T'::timestamptz - interval '1 hour' ORDER BY ts DESC LIMIT 5) x;" -v L="$LABEL" -v M="$m" -v T="$ti") || die "$ctx"
  local lastfill; lastfill=$(q "SELECT row_to_json(r) FROM (SELECT ts, side, price, qty, taker, inv_after, uid FROM mm_paper_fills WHERE label = :'L' AND market = :'M' AND ts <= :'T'::timestamptz ORDER BY ts DESC LIMIT 1) r;" -v L="$LABEL" -v M="$m" -v T="$ti") || die "$lastfill"
  if [ "$row" = null ]; then
    emit "WARN why $m $ti [$LABEL]: no quote row at or before this time (engine not running then, or before mm_paper_quotes existed)" "" '{}'
    return
  fi
  local age_s; age_s=$(jq -r --arg t "$ti" '((($t | sub("\\.[0-9]+Z$";"Z") | fromdate) - (.ts | sub("\\.[0-9]+";"") | sub("\\+00:00$";"Z") | sub("\\+00$";"Z") | fromdate)))' <<< "$row" 2>/dev/null || echo "?")
  local br ar; br=$(jq -r .bid_reason <<< "$row"); ar=$(jq -r .ask_reason <<< "$row")
  local v="INFO why $m $ti [$LABEL]: bid $(jq -r '.bid_px // "none"' <<< "$row") ($br), ask $(jq -r '.ask_px // "none"' <<< "$row") ($ar), book $(jq -r '"\(.best_bid)/\(.best_ask)"' <<< "$row"), row ${age_s}s old"
  if [ "$age_s" != "?" ] && [ "${age_s%.*}" -gt 61 ] 2>/dev/null; then v="WARN${v#INFO} — older than the 60 s keepalive: the engine was probably not writing then"; fi
  local txt
  txt=$(jq -r '"row in force: \(.ts)\n  book: best bid \(.best_bid)  best ask \(.best_ask)  mid \(.mid)  spread \(((.best_ask-.best_bid)/.mid*1e4*100|round/100)) bps\n  bid: \(.bid_px // "none")  reason \(.bid_reason)\n  ask: \(.ask_px // "none")  reason \(.ask_reason)\n  inventory \(.inv) coins ($\(.inv_usd*100|round/100)), equity today $\(.equity*10000|round/10000)"' <<< "$row")
  txt+=$'\n'"  bid reason: $(reason_text "$br")"$'\n'"  ask reason: $(reason_text "$ar")"
  [ "$nxt" != null ] && txt+=$'\n'"next change: $(jq -r '"\(.ts): bid \(.bid_px // "none") (\(.bid_reason)), ask \(.ask_px // "none") (\(.ask_reason))"' <<< "$nxt")"
  [ "$lastfill" != null ] && txt+=$'\n'"last fill before: $(jq -r '"\(.ts) \(if .side>0 then "BUY" else "SELL" end) \(.qty) @ \(.price)\(if .taker then " (flatten)" else "" end) inv after \(.inv_after) uid \(.uid)"' <<< "$lastfill")"
  [ "$(jq length <<< "$ctx")" != 0 ] && txt+=$'\n'"events in the hour before:"$'\n'"$(jq -r '.[] | "  \(.ts) \(.kind) \(.market) \(.detail)"' <<< "$ctx")"
  emit "$v" "$txt" "$(jq -cn --argjson r "$row" --argjson n "$nxt" --argjson f "$lastfill" --argjson e "$ctx" --arg bt "$(reason_text "$br")" --arg at "$(reason_text "$ar")" \
       '{row:$r, bid_reason_text:$bt, ask_reason_text:$at, next_change:$n, last_fill:$f, events:$e}')"
}

# ── replay ───────────────────────────────────────────────────────────────────────────────────────────────────
cmd_replay() {
  pg_setup
  local m=${1:-}; [ -z "$m" ] && die "replay <MKT> --from T --to T"
  [ -z "$FROM" ] || [ -z "$TO" ] && die "replay needs --from and --to"
  [ -x "$BIN/mm_sim" ] || die "no $BIN/mm_sim (build it, or --bin DIR)"
  local t0 t1; t0=$(ms_of "$FROM") || die "bad --from"; t1=$(ms_of "$TO") || die "bad --to"
  [ -z "$t0" ] || [ -z "$t1" ] && die "bad --from/--to"
  local d0 d1; d0=$(date -u -d "@$((t0/1000))" +%F); d1=$(date -u -d "@$((t1/1000))" +%F)
  # Since 2026-10-26 the engine carries inventory across 00:00 UTC and its per-day equity restarts at the roll, so a
  # window crossing a roll mixes two days' baselines in the paper P&L (Δ minute equity) — refuse it; a window may END
  # exactly at 00:00:00.000.
  local d1x; d1x=$(date -u -d "@$(( (t1 - 1) / 1000 ))" +%F)
  [ "$t1" -gt "$t0" ] || die "--to must be after --from"
  [ "$d0" = "$d1x" ] || die "window $FROM → $TO crosses a 00:00 UTC day roll — split it at midnight (the paper engine's day equity restarts there)"
  local i0 i1; i0=$(iso_of_ms "$t0"); i1=$(iso_of_ms "$t1")
  TMPD=$(mktemp -d); local tmp=$TMPD           # removed by the EXIT trap, also when die() leaves early
  # The engine's own setting for this market: the exact hl::Params it publishes in status.json (markets[].params), else the
  # gate job's params_lighter.json (the frozen setting, fill model = mm_sim's default).
  local sf; sf=$(status_file "$LABEL")
  local src="" cfg=chosen/pess fills="" pnotes=()
  if [ -f "$sf" ] && jq -e --arg m "$m" '.markets[]? | select(.market==$m) | has("params")' "$sf" >/dev/null 2>&1; then
    jq --arg m "$m" '(.markets[] | select(.market==$m) | .params) as $p |
      {venue:"lighter", maker_bps:$p.maker_fee_bps, taker_bps:$p.taker_fee_bps, latency_ms:$p.latency_ms, order_usd:$p.order_usd, max_inv:$p.max_inv_units,
       coins:{($m): ($p | {half_width_ticks, skew_ticks, fair, min_spread_ticks, imb_pull, pause_move_ticks, pause_ms, requote_ticks,
                           venue_tick, tick_rule, hl_sz_decimals} + {size_step: (.size_step // 0), min_base: (.min_base // 0), min_quote_usd: (.min_quote_usd // 0)})}}' "$sf" > "$tmp/params.json"
    src="$sf (markets[].params)"
    local p; p=$(jq -c --arg m "$m" '.markets[] | select(.market==$m) | .params' "$sf")
    [ "$(jq -r .cancels <<< "$p")" = optimistic ] && cfg=chosen/opt
    fills=$(jq -r .fills <<< "$p")
    if [ "$(jq -r '.place_latency_ms' <<< "$p")" != -1 ] || [ "$(jq -r '.cancel_latency_ms' <<< "$p")" != -1 ]; then
      pnotes+=("engine splits place/cancel latency, mm_sim uses latency_ms for both")
    fi
  elif [ -f data/mm_gates/params_lighter.json ] && jq -e --arg m "$m" '.coins[$m]' data/mm_gates/params_lighter.json >/dev/null; then
    jq --arg m "$m" '.coins = {($m): .coins[$m]}' data/mm_gates/params_lighter.json > "$tmp/params.json"
    src=data/mm_gates/params_lighter.json
    pnotes+=("setting from params_lighter.json, not the engine's own status.json (old binary?)")
  else die "no setting for $m (no markets[].params in $sf and no data/mm_gates/params_lighter.json entry)"; fi
  MM_FILLS=${fills:-${MM_FILLS:-}} "$BIN/mm_sim" --dir "$ARCHIVE/lighter" --coins "$m" --from "$d0" --to "$d1" --params "$tmp/params.json" \
     --t0-ms "$t0" --t1-ms "$t1" --daily "$tmp/daily.json" > "$tmp/sim.log" 2>&1 || die "mm_sim failed: $(tail -2 "$tmp/sim.log")"
  local sim; sim=$(jq -c --arg cfg "$cfg" '[.rows[] | select(.cfg==$cfg)] | {fills: (map(.fills)|add // 0), volume_usd: (map(.volume_usd)|add // 0),
       pnl_usd: (map(.pnl_usd)|add // 0), books: (map(.books)|add // 0), days: length}' "$tmp/daily.json")
  local paper; paper=$(q "SELECT json_build_object(
      'fills', (SELECT count(*) FROM mm_paper_fills WHERE label=:'L' AND market=:'M' AND NOT taker AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz),
      'volume_usd', (SELECT coalesce(sum(price*qty),0) FROM mm_paper_fills WHERE label=:'L' AND market=:'M' AND NOT taker AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz),
      'flattens', (SELECT count(*) FROM mm_paper_fills WHERE label=:'L' AND market=:'M' AND taker AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz),
      'pnl_usd', (SELECT (SELECT equity FROM mm_paper_equity WHERE label=:'L' AND market=:'M' AND ts <= :'T1'::timestamptz AND ts >= :'T0'::timestamptz ORDER BY ts DESC LIMIT 1)
                       - (SELECT equity FROM mm_paper_equity WHERE label=:'L' AND market=:'M' AND ts >= :'T0'::timestamptz AND ts <= :'T1'::timestamptz ORDER BY ts LIMIT 1)),
      'restarts', (SELECT count(*) FROM mm_paper_events WHERE label=:'L' AND kind='start' AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz),
      'halts', (SELECT count(*) FROM mm_paper_events WHERE label=:'L' AND kind IN ('stale','feed_down','kill','loss_stop','day_roll') AND (market=:'M' OR market='') AND ts BETWEEN :'T0'::timestamptz AND :'T1'::timestamptz));" \
      -v L="$LABEL" -v M="$m" -v T0="$i0" -v T1="$i1") || die "$paper"
  # Was the paper engine flat at T0? (the quote row in force then; the replay always starts flat with no orders)
  local at0; at0=$(q "SELECT row_to_json(r) FROM (SELECT ts, inv, bid_px, ask_px FROM mm_paper_quotes WHERE label=:'L' AND market=:'M' AND ts <= :'T0'::timestamptz ORDER BY ts DESC LIMIT 1) r;" -v L="$LABEL" -v M="$m" -v T0="$i0" 2>/dev/null) || at0=null
  case "$at0" in __ERR__*) at0=null ;; esac
  local cmp; cmp=$(jq -cn --argjson s "$sim" --argjson p "$paper" '
      def tol(a; b; abs_; rel): ((a - b) | fabs) <= ([abs_, rel * ([a, b] | max)] | max);
      {fills_ok: tol($s.fills; $p.fills; 3; 0.25), volume_ok: tol($s.volume_usd; $p.volume_usd; 300; 0.25),
       fills_diff: ($p.fills - $s.fills), volume_diff_usd: ($p.volume_usd - $s.volume_usd),
       pnl_diff_usd: (if $p.pnl_usd == null then null else $p.pnl_usd - $s.pnl_usd end)}')
  local ok; ok=$(jq -r 'if .fills_ok and .volume_ok then "PARITY" else "DIVERGE" end' <<< "$cmp")
  local notes=()
  [ "$(jq .restarts <<< "$paper")" != 0 ] && notes+=("engine restarted inside the window (reason 5)")
  [ "$(jq .halts <<< "$paper")" != 0 ] && notes+=("halt/day-roll events inside the window (reason 3)")
  [ "$(jq .books <<< "$sim")" = 0 ] && notes+=("archive has no books in this window")
  if [ "$at0" = null ]; then notes+=("flat start unknown (no quote row before T0)")
  else
    jq -e '(.inv | fabs) > 1e-12' <<< "$at0" >/dev/null && notes+=("paper NOT flat at T0 (inv $(jq -r .inv <<< "$at0")) — parity not meaningful (reason 1)")
    jq -e '.bid_px != null or .ask_px != null' <<< "$at0" >/dev/null && notes+=("paper had resting orders at T0 (queue places the replay does not have, reason 1)")
  fi
  [ ${#pnotes[@]} -gt 0 ] && notes+=("${pnotes[@]}")
  local v; v="$( [ "$ok" = PARITY ] && echo OK || echo WARN ) replay $m [$LABEL] $i0 → $i1: $ok — paper $(jq -r '"\(.fills) fills $\(.volume_usd|round)"' <<< "$paper") vs replay $(jq -r '"\(.fills) fills $\(.volume_usd|round)"' <<< "$sim")$( [ ${#notes[@]} -gt 0 ] && echo " ($(join "${notes[@]}"))")"
  local txt
  txt=$(printf '%-12s %8s %12s %10s\n' "" fills "volume \$" "P&L \$"
        printf '%-12s %8s %12.0f %10s\n' paper "$(jq .fills <<< "$paper")" "$(jq .volume_usd <<< "$paper")" "$(jq -r 'if .pnl_usd==null then "—" else (.pnl_usd*100|round/100|tostring) end' <<< "$paper")"
        printf '%-12s %8s %12.0f %10s\n' replay "$(jq .fills <<< "$sim")" "$(jq .volume_usd <<< "$sim")" "$(jq -r '.pnl_usd*100|round/100' <<< "$sim")"
        echo "WARNING: replay parity is only meaningful for a window that starts FLAT (no inventory, no resting orders) — pick T0 where the paper engine was flat."
        echo "setting from: $src; archive $ARCHIVE/lighter/$m $d0..$d1; paper flattens in window: $(jq .flattens <<< "$paper")"
        echo "known reasons a live window and a replay differ: state carried in at T0 (inventory, orders, queue place, pause);"
        echo "  different websocket connection and arrival times; halts the replay cannot see; the replay's taker flatten at T1;"
        echo "  an engine restart inside the window (see scripts/mm_debug.sh --help)")
  emit "$v" "$txt" "$(jq -cn --argjson s "$sim" --argjson p "$paper" --argjson c "$cmp" --arg src "$src" --arg r "$ok" --argjson a "$at0" --arg w "replay parity is only meaningful for windows that start flat" '{result:$r, warning:$w, paper_at_t0:$a, paper:$p, replay:$s, compare:$c, setting_from:$src}')"
}

# ── archive ──────────────────────────────────────────────────────────────────────────────────────────────────
cmd_archive() {
  local venue=${1:-lighter}
  [ -x "$BIN/archive_check" ] || die "no $BIN/archive_check (build it, or --bin DIR)"
  local day=${DAY:-$(date -u +%F)}
  local files=()
  for d in "$ARCHIVE/$venue"/*/; do
    local m; m=$(basename "$d")
    [ -n "$MARKET" ] && [ "$m" != "$MARKET" ] && continue
    [ -f "${d%/}/$day.jsonl.zst" ] && files+=("${d%/}/$day.jsonl.zst")
  done
  [ ${#files[@]} -eq 0 ] && { emit "WARN archive $venue $day: no files under $ARCHIVE/$venue" "" '{"files":[]}'; return; }
  local rows; rows=$(nice "$BIN/archive_check" "${files[@]}" | jq -sc '.')
  local bad; bad=$(jq -r '[.[] | select(.skipped_frames > 0 or .seq_gaps > 0 or .bad_lines > 0 or .max_gap_s > 900 or .error != null) | .market] | join(",")' <<< "$rows")
  local v
  if [ -z "$bad" ]; then v="OK archive $venue $day: ${#files[@]} files, no skipped frames, no sequence gaps, no book silence > 15 min"
  else v="WARN archive $venue $day: ${#files[@]} files; $(jq -r '[(([.[] | select(.skipped_frames > 0)] | length) as $n | if $n > 0 then "\($n) with skipped zstd frames" else empty end), (([.[] | select(.seq_gaps > 0)] | length) as $n | if $n > 0 then "\($n) with sequence gaps" else empty end), (([.[] | select(.max_gap_s > 900)] | length) as $n | if $n > 0 then "\($n) with a book silence > 15 min" else empty end), (([.[] | select(.bad_lines > 0 or .error != null)] | length) as $n | if $n > 0 then "\($n) with unreadable lines/files" else empty end)] | join(", ")' <<< "$rows") — $bad"; fi
  local txt
  txt=$(printf '%-8s %9s %6s %7s %5s %9s %7s %6s %9s %5s\n' market lines frames seqgaps snaps books trades hours maxgap_s gaps
        jq -r '.[] | [.market, .lines, .skipped_frames, .seq_gaps, .snapshots, .books, .trades, (.hours*10|round/10), (.max_gap_s|round), .gaps_over] | @tsv' <<< "$rows" |
          while IFS=$'\t' read -r a b c d e f g h i j; do printf '%-8s %9s %6s %7s %5s %9s %7s %6s %9s %5s\n' "$a" "$b" "$c" "$d" "$e" "$f" "$g" "$h" "$i" "$j"; done
        echo "frames = damaged zstd frames skipped (data lost); seqgaps = Lighter nonce gaps (book untrusted until the next snapshot);"
        echo "maxgap_s = longest silence between two books; gaps = silences > 60 s")
  emit "$v" "$txt" "$(jq -cn --argjson r "$rows" --arg d "$day" --arg ven "$venue" '{venue:$ven, day:$d, files:$r}')"
}

# ── db ───────────────────────────────────────────────────────────────────────────────────────────────────────
cmd_db() {
  pg_setup
  local out='[]' problems=() txt=""
  IFS=, read -ra LS <<< "$LABELS"
  for L in "${LS[@]}"; do
    local f; f=$(status_file "$L")
    local comp='null'
    [ -f "$f" ] && comp=$(jq -c '.components.db // {queue: .db_queue, retries: .db_retries, errors: .db_errors, dropped: .db_dropped, dropped_today: .db_dropped_today}' "$f")
    local led="$STATUS_DIR/db_drops_$L.json" ledger='null'
    [ -f "$led" ] && ledger=$(jq -c . "$led")
    local qled="$STATUS_DIR/db_quote_drops_$L.json" qledger='null'   # quote rows' own ledger — informational, the G2 gate never reads it
    [ -f "$qled" ] && qledger=$(jq -c . "$qled")
    local counts; counts=$(q "SELECT json_build_object(
        'fills_1h', (SELECT count(*) FROM mm_paper_fills WHERE label=:'L' AND ts > now() - interval '1 hour'),
        'events_1h', (SELECT count(*) FROM mm_paper_events WHERE label=:'L' AND ts > now() - interval '1 hour'),
        'equity_1h', (SELECT count(*) FROM mm_paper_equity WHERE label=:'L' AND ts > now() - interval '1 hour'),
        'quotes_1h', (SELECT CASE WHEN to_regclass('mm_paper_quotes') IS NULL THEN NULL ELSE (SELECT count(*) FROM mm_paper_quotes WHERE label=:'L' AND ts > now() - interval '1 hour') END),
        'last_equity', (SELECT max(ts) FROM mm_paper_equity WHERE label=:'L'),
        'last_quote', (SELECT CASE WHEN to_regclass('mm_paper_quotes') IS NULL THEN NULL ELSE (SELECT max(ts) FROM mm_paper_quotes WHERE label=:'L') END));" -v L="$L") || { problems+=("$L: query failed: $counts"); counts='null'; }
    out=$(jq -c --arg l "$L" --argjson c "$comp" --argjson g "$ledger" --argjson n "$counts" --argjson qg "$qledger" '. + [{label:$l, writer:$c, ledger:$g, quote_ledger:$qg, rows:$n}]' <<< "$out")
    [ "$comp" = null ] && problems+=("$L: no status file")
    [ "$(jq -r '.ok // true' <<< "$comp")" = false ] && problems+=("$L: db component not ok")
    [ "$(jq -r '.dropped_today // 0' <<< "$comp")" != 0 ] && problems+=("$L: rows dropped today")
    [ "$ledger" != null ] && [ "$(jq -r --arg d "$(date -u +%F)" '.days[$d] // 0' <<< "$ledger")" != 0 ] && problems+=("$L: drop ledger has drops today")
    if [ "$counts" != null ] && [ "$(jq -r '.equity_1h' <<< "$counts")" = 0 ]; then problems+=("$L: no minute equity row in the last hour"); fi
    txt+="[$L] writer: $(jq -r 'if . == null then "—" else to_entries | map("\(.key)=\(.value)") | join(" ") end' <<< "$comp")"$'\n'
    txt+="    ledger: $(jq -r 'if . == null then "none" else "since \(.since), total \(.total), days \(.days | tostring)" end' <<< "$ledger")"$'\n'
    txt+="    quote ledger (not read by the gate): $(jq -r 'if . == null then "none" else "since \(.since), total \(.total), days \(.days | tostring)" end' <<< "$qledger")"$'\n'
    txt+="    rows last hour: $(jq -r 'if . == null then "—" else "fills \(.fills_1h), events \(.events_1h), equity \(.equity_1h), quotes \(.quotes_1h // "no table"); last equity \(.last_equity), last quote \(.last_quote)" end' <<< "$counts")"$'\n'
  done
  local v; if [ ${#problems[@]} -eq 0 ]; then v="OK db: no drops, writers healthy on $LABELS"; else v="WARN db: $(join "${problems[@]}")"; fi
  emit "$v" "${txt%$'\n'}" "$(jq -cn --argjson o "$out" '{labels:$o}')"
}

# ── gates ────────────────────────────────────────────────────────────────────────────────────────────────────
cmd_gates() {
  [ -f "$GATES" ] || die "no $GATES (scripts/mm_gates.sh writes it)"
  local mh mg   # the completeness rule, read from scripts/mm_gates.sh so the two never disagree
  mh=$(sed -n 's/^G1_MIN_HOURS=\([0-9.]*\).*/\1/p' scripts/mm_gates.sh); mg=$(sed -n 's/.*G1_MAX_GAP_MIN=\([0-9.]*\).*/\1/p' scripts/mm_gates.sh)
  mh=${mh:-23.5}; mg=${mg:-15}
  local sum; sum=$(jq -c --argjson MH "$mh" --argjson MG "$mg" '{generated,
      gates: [.gates[] | {id, name, status, progress, metrics}],
      g1_excluded: [.g1_daily[]? | select(.complete | not) | {day, reasons: ([
          (if (.hours // 0) < $MH then "only \((.hours // 0)*10|round/10) h of books on the busiest market (< \($MH))" else empty end),
          (if .max_gap_min == null then "max gap unknown" elif .max_gap_min > $MG then "a \(.max_gap_min|round) min silence between books (> \($MG))" else empty end)]
          | if length == 0 then ["not complete (today, or the busiest market lacks hours/gap data)"] else . end)}],
      g2_excluded: (.g2_excluded // [])}' "$GATES")
  local st; st=$(jq -r '[.gates[] | "\(.id) \(.status) \(.progress.done // "-")/\(.progress.needed // "-")"] | join(", ")' <<< "$sum")
  local v; if jq -e '[.gates[] | select(.status=="fail")] | length > 0' <<< "$sum" >/dev/null; then v="FAIL gates: $st"; else v="INFO gates: $st"; fi
  local txt
  txt=$(jq -r '"generated \(.generated)",
      (.gates[] | "\(.id) \(.name): \(.status)", (.metrics[]? | "    \(.name): \(.value // "—")  rule \(.rule)  \(if .ok == true then "ok" elif .ok == false then "NOT OK" else "" end)")),
      (if (.g1_excluded | length) > 0 then "G1 days not counted:", (.g1_excluded[] | "    \(.day): \(.reasons | join("; "))") else "G1: no excluded days" end),
      (if (.g2_excluded | length) > 0 then "G2 days not counted:", (.g2_excluded[] | "    \(.day): \(.reasons | join("; "))") else "G2: no excluded days" end)' <<< "$sum")
  emit "$v" "$txt" "$sum"
}

case "$CMD" in
  status) cmd_status "$@" ;;
  trace) cmd_trace "$@" ;;
  why) cmd_why "$@" ;;
  replay) cmd_replay "$@" ;;
  archive) cmd_archive "$@" ;;
  db) cmd_db "$@" ;;
  gates) cmd_gates "$@" ;;
  *) echo "usage: scripts/mm_debug.sh [--json] [options] status|trace fill <uid>|trace market <MKT> --from T --to T|why <MKT> <T>|replay <MKT> --from T --to T|archive [venue]|db|gates  (--help)"; exit 2 ;;
esac
