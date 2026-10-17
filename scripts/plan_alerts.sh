#!/usr/bin/env bash
# plan_alerts.sh PLAN_JSON NOW_M — Telegram alerts (grid-notify) for the hand-traded sprint, called by daily_plan.sh on today's plan.
# NOW_M = minutes after 09:30 ET (negative before the open). Each alert is sent once per day (state: data/plan/.alerts-<date>).
# Why speed matters (research/preregistered/2026-10-17_manual_execution.md): entering 1 minute late erases the sprint's edge and a
# fill 5 pts worse turns it negative — so the levels go out BEFORE each check, and TRADE NOW goes out the minute it triggers.
# Sizing = the Plan page sprint panel for Tradeify Lightning 25K: $600 risk, ≤ 10 MNQ, a win ≤ $300 (the 20 % consistency cap).
set -uo pipefail
plan_json="$1"; now_m="$2"
[[ -s "$plan_json" ]] || exit 0
date=$(jq -r '.date // empty' "$plan_json"); [[ -n "$date" ]] || exit 0
state="$(dirname "$plan_json")/.alerts-$date"; touch "$state"
notify=$(command -v grid-notify || echo "$HOME/.local/bin/grid-notify")

jq -r --argjson now "$now_m" '
  def r4: (. * 4 | round) / 4;
  def mnq($stop; $tgt): ([((600 / ($stop * 2)) | floor), 10] | min | if . < 1 then 1 else . end) as $n
                        | [range($n; 0; -1) | select(. * $tgt * 2 - . * 3.2 <= 300)] | (first // 1);
  (.numbers.atr) as $atr | (.numbers.open) as $open |
  ([.strategies[] | select((.id == "weak_day_long" or .id == "monday_long") and .on)] | first) as $hold |
  ([.strategies[] | select(.id == "so_cascade")] | first | .levels // []) as $lv |
  ([.strategies[] | select(.id == "gap_or_retest")] | first) as $gor |
  ((.numbers.news // []) | map(.label // .kind) | join(", ")) as $news |
  (.live.sprint // null) as $sp |
  # 1. morning brief, 09:00-09:29
  (if $now < 0 and $atr != null then
     ["brief", (if $hold != null then
        (($atr * 0.25) | r4) as $st | ($st * 0.5 | r4) as $tg | mnq($st; $tg) as $n |
        "NQ SPRINT — \($hold.name) is ON: BUY at the 09:30 open. \($n) MNQ, stop \($st) pts below the fill, target \($tg) pts above. Place the bracket before 09:30; skip it if you are not filled within the first minute."
      else
        "NQ plan — no 09:30 trade today. Cascade check 09:50: LONG if ≥ \($lv[0].long_above // "?"), SHORT if ≤ \($lv[0].short_below // "?"). Gap retest: \($gor.status // "off")." end)
      + (if $news != "" then " NEWS today: \($news)." else "" end)] | @tsv else empty end),
  # 2. one minute before each cascade check, while no sprint trade exists yet
  #    (a level row may cover several checks: "10:30 · 11:00 · 11:30")
  ($lv[] | select($sp.trade == null and $hold == null) | . as $L | (.at | split(" · ")[]) as $at
     | ($at | split(":") | ((.[0] | tonumber) * 60 + (.[1] | tonumber)) - 570) as $m
     | select($now >= $m - 2 and $now < $m)
     | ["pre-\($at)", "NQ cascade check at \($at): LONG if price ≥ \($L.long_above), SHORT if ≤ \($L.short_below). Have the bracket ready."] | @tsv),
  # 3. gap retest armed, 09:45 (the 15-min opening range is known)
  (if $now >= 15 and $now < 20 and $hold == null and ($gor.side // null) != null and $sp.trade == null then
     ["gap", "NQ gap retest armed (09:45–13:55): \($gor.side | ascii_upcase) on a 5-min bar that touches \($gor.level.price // "the opening-range edge") and closes beyond it."] | @tsv else empty end),
  # 4. the trade
  (if $sp != null and $sp.status == "TRADE NOW" and $sp.trade != null then
     ($sp.trade) as $t | mnq($t.stop_pts; $t.target_pts) as $n |
     ["trade-\($t.source)-\($t.at)", "TRADE NOW — NQ \($t.dir) (\($t.source) \($t.at)). Entry ≈ \($t.price), \($n) MNQ, stop \($t.stop_price), target \($t.target_price). Bracket at once; if price already moved > 5 pts, SKIP. One trade today."] | @tsv
   else empty end)
' "$plan_json" | while IFS=$'\t' read -r key msg; do
  [[ -n "$key" ]] || continue
  grep -qxF "$key" "$state" && continue
  echo "$key" >>"$state"
  "$notify" "$msg" >/dev/null 2>&1 || true
  echo "alert $key: $msg"
done
