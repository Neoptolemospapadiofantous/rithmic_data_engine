# daily_plan_live.jq — "am I allowed to trade right now?" for the day plan (scripts/daily_plan.sh), from today's 1-minute bars.
# Input: the plan (.) ; $bars = today's RTH 1-minute bars [{m: minutes after 09:30, o,h,l,c,v,bv}] ; $now = minutes after 09:30 now ;
# $pre2 = the overnight 2-minute closes (18:00 → 09:30, oldest first) that seed the EMA-9 / EMA-21.
# Only the tested rules (research/preregistered/2026-10-17_*): cascade checks 09:50 (0.25 ATR) / 10:30 · 11:00 · 11:30 (0.30 ATR),
# gap retest (gap side, 15-min opening-range edge touched and closed beyond, bars starting 09:45–13:55), box_go (gap side, overnight
# high/low broken, a smaller box beyond it, a close beyond the box). Order flow = RTH cumulative aggressor delta (2·buy − volume).
# Never opposite directions; a long hold on → no shorts.
def hhmm($m): (570 + $m) as $t | "\(($t / 60 | floor) | tostring | if length < 2 then "0" + . else . end):\(($t % 60) | tostring | if length < 2 then "0" + . else . end)";
def px_before($t): [$bars[] | select(.m < $t)] | if length == 0 then null else last.c end;
def cd_before($t): [$bars[] | select(.m < $t) | (2 * .bv - .v)] | add // 0;
def bars5: [$bars | group_by(.m / 5 | floor)[] | {k: (.[0].m / 5 | floor), o: .[0].o, h: (map(.h) | max), l: (map(.l) | min), c: last.c, n: length}]
           | map(select(.n == 5 or ((.k + 1) * 5 <= $now)));
. as $p | ($p.numbers.atr) as $atr | ($p.numbers.open) as $open | ($p.numbers.gap_atr) as $gap | ($p.numbers.overnight) as $on
| ([$p.strategies[] | select((.id == "weak_day_long" or .id == "monday_long") and .on)] | length > 0) as $long_hold
| if ($bars | length) == 0 or $atr == null or $open == null then . + {live: {status: "WAIT", detail: "waiting for the 09:30 open", setups: []}}
  else
  bars5 as $b5
  # ── cascade
  | ([[20, 0.25], [60, 0.30], [90, 0.30], [120, 0.30]] | map(select(.[0] <= $now))
     | map(. as [$t, $k] | (px_before($t)) as $px | select($px != null and (($px - $open) | fabs) >= $k * $atr)
           | {setup: "cascade", at: hhmm($t), dir: (if $px > $open then "LONG" else "SHORT" end), price: $px,
              flow_agrees: ((if $px > $open then 1 else -1 end) * cd_before($t) > 0)}) | first // null) as $cas
  # ── gap retest
  | (if $gap == null or ($gap | fabs) < 0.1 or ($b5 | length) < 4 then null else
       ([$b5[] | select(.k < 3)]) as $or | ($or | map(.h) | max) as $orh | ($or | map(.l) | min) as $orl
       | [$b5[] | select(.k >= 3 and .k <= 52)
          | select(if $gap > 0 then (.l <= $orh and .c > $orh) else (.h >= $orl and .c < $orl) end)] | first
       | if . == null then null else {setup: "gap retest", at: hhmm((.k + 1) * 5), dir: (if $gap > 0 then "LONG" else "SHORT" end), price: .c,
            flow_agrees: ((if $gap > 0 then 1 else -1 end) * cd_before((.k + 1) * 5) > 0)} end end) as $gor
  # ── box_go (overnight high / low, gap side)
  | (if $gap == null or ($gap | fabs) < 0.1 or $on.hi == null then null else
       [range(2; $b5 | length) as $i | $b5[$i - 2] as $b3 | $b5[$i - 1] as $b2 | $b5[$i] as $b1
        | select($b1.k >= 2 and $b1.k <= 52)
        | select(if $gap > 0 then ($b3.c > $on.hi and $b3.o <= $on.hi and $b2.l > $on.hi and ($b2.h - $b2.l) <= 0.75 * ($b3.h - $b3.l) and $b1.c > $b2.h)
                 else ($b3.c < $on.lo and $b3.o >= $on.lo and $b2.h < $on.lo and ($b2.h - $b2.l) <= 0.75 * ($b3.h - $b3.l) and $b1.c < $b2.l) end)
        | {setup: "box_go", at: hhmm(($b1.k + 1) * 5), dir: (if $gap > 0 then "LONG" else "SHORT" end), price: $b1.c,
           stop: (if $gap > 0 then $b2.l - 0.25 else $b2.h + 0.25 end), flow_agrees: ((if $gap > 0 then 1 else -1 end) * cd_before(($b1.k + 1) * 5) > 0)}] | first end) as $box
  | ([$cas, $gor, $box] | map(select(. != null)) | sort_by(.at)) as $all
  | ($all[0].dir // null) as $first_dir
  | ($all | map(. + {allowed: (if $long_hold and .dir == "SHORT" then false elif $first_dir != null and .dir != $first_dir then false else true end),
                     why_not: (if $long_hold and .dir == "SHORT" then "a long hold is on: no shorts" elif $first_dir != null and .dir != $first_dir then "opposite to the day's first trade" else null end)})) as $setups
  | ([$setups[] | select(.allowed)] | last) as $latest
  | (if $now >= 270 then "NO NEW TRADES" elif ($latest != null and ((($now - ($latest.at | split(":") | ((.[0] | tonumber) * 60 + (.[1] | tonumber)) - 570)) | . <= 5))) then "TRADE ALLOWED NOW"
     elif ($setups | length) > 0 then "SETUP TAKEN EARLIER" else "WAIT" end) as $status
  | (if $now < 20 then "next: cascade check 09:50" elif $now < 60 then "next: cascade check 10:30" elif $now < 90 then "next: cascade check 11:00"
     elif $now < 120 then "next: cascade check 11:30" elif $now < 265 then "gap retest / box_go can still trigger until 13:55" else "no new entries after 14:00" end) as $next
  # ── EMA permission for hand trades (research/preregistered/2026-10-17_ema_permission.md + _ema_sweep.md): the day direction =
  #    the first cascade / gap-retest trigger; allowed only on that side of VWAP and of the open, before 13:55. The setup is the
  #    sweep's least-bad version: 2-min bars, a bar within 0.03 ATR of EMA-9 that closes back on the trend side, EMA-9 beyond EMA-21.
  | ([$cas, $gor] | map(select(. != null)) | sort_by(.at) | .[0]) as $pd
  | ([$bars[] | select(.m < $now)]) as $done
  | ([$done[] | (.v // 0) | if . < 1 then 1 else . end]) as $vols
  | (if ($done | length) > 0 then ([range(0; $done | length) as $i | (($done[$i].h + $done[$i].l + $done[$i].c) / 3) * $vols[$i]] | add) / ($vols | add) else null end) as $vwap
  | ([$bars | group_by(.m / 2 | floor)[] | {k: (.[0].m / 2 | floor), h: (map(.h) | max), l: (map(.l) | min), c: last.c, n: length}]
     | map(select(.n == 2 or ((.k + 1) * 2 <= $now)))) as $b2
  | ((($pre2 // []) + [$b2[] | .c]) as $cl | {e9: ($cl | reduce .[] as $x ({e: null, hist: []}; .e = (if .e == null then $x else .e + 0.2 * ($x - .e) end) | .hist += [.e])),
                                               e21: ($cl | reduce .[] as $x ({e: null, hist: []}; .e = (if .e == null then $x else .e + (2 / 22) * ($x - .e) end) | .hist += [.e]))}) as $E
  | ($E.e9.hist | if length >= 2 then .[-2] else null end) as $ema_prev
  | ($E.e21.hist | if length >= 2 then .[-2] else null end) as $ema21_prev
  | ($b2 | last) as $lb
  | ($done | if length > 0 then last.c else $open end) as $px
  | (if $pd == null then null elif $pd.dir == "LONG" then 1 else -1 end) as $d
  | (if $now >= 265 then {ok: false, why: "after 13:55: no new EMA trades (entries after 13:55 lost in 2019-24)"}
     elif $d == null then {ok: false, why: ("no direction yet (" + $next + ")")}
     elif $long_hold and $d < 0 then {ok: false, why: "a long hold is on: no shorts"}
     elif $vwap != null and ($d * ($px - $vwap)) <= 0 then {ok: false, why: ("price is on the wrong side of VWAP " + (($vwap * 4 | round) / 4 | tostring) + ": wait until it is back " + (if $d > 0 then "above" else "below" end))}
     elif ($d * ($px - $open)) <= 0 then {ok: false, why: "price is back through the 09:30 open: wait"}
     else {ok: true, why: ((if $d > 0 then "LONG only: buy" else "SHORT only: sell" end) + " a 2-min bar that comes within 0.03 ATR of EMA-9 and closes back " + (if $d > 0 then "above" else "below" end) + " it, with EMA-9 " + (if $d > 0 then "above" else "below" end) + " EMA-21; stop 0.25 ATR, break-even at +1R, no target; after a loss, done for the day. About breakeven in tests — discretionary.")} end) as $perm
  | ($ema_prev != null and $ema21_prev != null and $lb != null and $d != null
     and (if $d > 0 then ($lb.l <= $ema_prev + 0.03 * $atr and $lb.c > $ema_prev and $ema_prev > $ema21_prev)
          else ($lb.h >= $ema_prev - 0.03 * $atr and $lb.c < $ema_prev and $ema_prev < $ema21_prev) end)) as $touch
  # ── one-week payout sprint (docs/manual_playbook.md): the day's FIRST signal only, target 0.5R; the 09:30 weak-day / Monday long
  #    takes the day when on (tested 2026-10-17: Lightning 25K payout ≤ 7 days 6.7 %, ≤ 20 days 25 %)
  | ([$p.strategies[] | select((.id == "weak_day_long" or .id == "monday_long") and .on)] | first) as $hold
  | (if $hold != null then {source: (if $hold.id == "weak_day_long" then "weak-day long" else "Monday long" end), at: "09:30", dir: "LONG", price: $open, stop_pts: (($atr * 0.25 * 4 | round) / 4)}
     else ([$cas, $gor] | map(select(. != null)) | sort_by(.at) | .[0]
           | if . == null then null else {source: .setup, at: .at, dir: .dir, price: .price, stop_pts: (($atr * (if .setup == "cascade" then 0.25 else 0.35 end) * 4 | round) / 4)} end) end) as $sp
  | ($sp | if . == null then null else (.at | split(":") | ((.[0] | tonumber) * 60 + (.[1] | tonumber)) - 570) end) as $spm
  | (if $sp == null then (if $now >= 265 then "NO TRADE TODAY" else "WAIT" end)
     elif ($now - $spm) <= 5 then "TRADE NOW" else "TAKEN EARLIER — one trade a day" end) as $spst
  # the plan's own result for the sprint trade (stop counted first inside a minute; flat 15:55) — the panel compares it with the log
  | (if $sp == null then null else
       (if $sp.dir == "LONG" then 1 else -1 end) as $sd | ($sp.stop_pts) as $st | ($sp.price - $sd * $st) as $sl | ($sp.price + $sd * $st * 0.5) as $tp
       | ([$bars[] | select(.m >= $spm and .m < 385)] | map(select(if $sd > 0 then (.l <= $sl or .h >= $tp) else (.h >= $sl or .l <= $tp) end)) | first) as $hit
       | if $hit != null then (if (if $sd > 0 then $hit.l <= $sl else $hit.h >= $sl end) then {result: "LOSS", pts: (-$st), at: hhmm($hit.m + 1)}
                               else {result: "WIN", pts: ($st * 0.5), at: hhmm($hit.m + 1)} end)
         elif $now >= 385 then ([$bars[] | select(.m < 385)] | last) as $lb | {result: "FLAT 15:55", pts: ((($sd * ($lb.c - $sp.price)) * 4 | round) / 4), at: "15:55"}
         else {result: "OPEN", pts: (if ($bars | length) > 0 then ((($sd * (($bars | last.c) - $sp.price)) * 4 | round) / 4) else 0 end), at: hhmm($now)} end end) as $spout
  # ── 1-min momentum direction (research/preregistered/2026-10-17_mtf_1m_dir_5m_entry.md, forward-paper candidate): the last 30
  #    one-minute moves went one way fairly straight (|net| / path ≥ 0.3) and price is on that side of VWAP and the open, ≤ 13:55
  | ([$bars[] | select(.m < $now)]) as $mb
  | (if ($mb | length) < 31 then null else
       ($mb[-31:]) as $w | ([range(1; 31) as $i | (($w[$i].c - $w[$i - 1].c) | fabs)] | add) as $path | ($w[30].c - $w[0].c) as $net
       | (if $path > 0 then (($net | fabs) / $path) else 0 end) as $eff
       | (if $eff >= 0.3 then (if $net > 0 then 1 else -1 end) else 0 end) as $md
       | {dir: (if $md > 0 then "LONG" elif $md < 0 then "SHORT" else null end), eff: (($eff * 100 | round) / 100), net_pts: (($net * 4 | round) / 4),
          allowed: ($md != 0 and $now <= 265 and $vwap != null and ($md * ($px - $vwap)) > 0 and ($md * ($px - $open)) > 0 and (($long_hold and $md < 0) | not)),
          why: (if $md == 0 then "last 30 min not straight enough (efficiency \(($eff * 100 | round) / 100) < 0.3)"
                elif $now > 265 then "after 13:55"
                elif ($long_hold and $md < 0) then "a long hold is on: no shorts"
                elif ($md * ($px - $vwap)) <= 0 then "price on the wrong side of VWAP"
                elif ($md * ($px - $open)) <= 0 then "price on the wrong side of the 09:30 open"
                else "enter at a 5-min close in this direction (the EMA-20 touch is a fine place); stop 0.25 ATR, break-even at +1R, hold; ≤ 3 a day, done after a loss" end)} end) as $mom
  | . + {live: {status: $status, at: hhmm($now), next: $next, mom: $mom,
                sprint: {status: $spst, outcome: $spout, trade: (if $sp == null then null else $sp + {target_pts: (($sp.stop_pts * 0.5 * 4 | round) / 4),
                          stop_price: ((($sp.price - (if $sp.dir == "LONG" then 1 else -1 end) * $sp.stop_pts) * 4 | round) / 4),
                          target_price: ((($sp.price + (if $sp.dir == "LONG" then 1 else -1 end) * $sp.stop_pts * 0.5) * 4 | round) / 4)} end)},
                ema: {dir: ($pd.dir // null), since: ($pd.at // null), source: ($pd.setup // null), allowed: $perm.ok, why: $perm.why,
                      ema9: (if $E.e9.e == null then null else ($E.e9.e * 4 | round) / 4 end), ema21: (if $E.e21.e == null then null else ($E.e21.e * 4 | round) / 4 end), vwap: (if $vwap == null then null else ($vwap * 4 | round) / 4 end), open: $open,
                      stop_pts: (($atr * 0.25 * 4 | round) / 4),
                      last_bar: (if $lb == null then null else {at: hhmm(($lb.k + 1) * 2), setup: ($touch and $perm.ok)} end)},
                price: ($bars | last.c), move_atr: ((($bars | last.c) - $open) / $atr * 100 | round / 100),
                flow: (cd_before($now + 1) | if . > 0 then "buyers ahead" elif . < 0 then "sellers ahead" else "even" end),
                flow_delta: cd_before($now + 1), setups: $setups,
                note: "Order flow agreeing is a candidate filter (better on 2023-26 data, paper test running); trade the setup either way, size smaller if flow disagrees."}}
  end
