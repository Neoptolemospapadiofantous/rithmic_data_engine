#!/usr/bin/env bash
# export_research_data.sh — READ-ONLY export of paper-fleet results into the StarNet
# research clone (~/starnet-work/rithmic_engine/research_data/), so the crew can rank
# strategies and judge prop-firm survival without any database access of its own.
#
# The session runs with default_transaction_read_only=on: it cannot write to Postgres.
# Live trade history is deliberately NOT exported (§5 2026-09-30).
#
# Files (all CSV, header row):
#   leaderboard_<7|30|365>d_session.csv / _slot_1000_1200.csv  strategy_leaderboard(), label tradeify
#   leaderboard_backfill_session.csv       same, over the replay label 'backfill'
#   leaderboard_desk_session.csv           same, over the desk's own replays (label 'desk')
#   daily_pnl_1full.csv                    one row per label × strategy × NY session date, P&L
#                                          RESCALED to ONE full-size contract (MNQ→NQ $20/pt,
#                                          MES→ES $50/pt) minus FULL_RT_COMM per trade, plus the
#                                          worst intraday running P&L (daily-loss-limit test)
#   session_stats.csv                      one row per symbol × NY session (range, gap, ATR, day_type)
#   ../../holdout/desk_holdout.csv         holdout verdicts (aggregates only) — OUTSIDE research_data: BRIEF only
#   fills_live.csv / fills_parity.csv      live execution quality + live-vs-paper twin trades (FILLS agent)
#   strategies.csv                         paper_strategies: id, label, engine, enabled, params
#   prop_rules.json                        the live account's risk limits (from tradeify_config.json)
#   EXPORTED_AT                            UTC timestamp of this export
set -euo pipefail
REAL="${RITHMIC_REPO:-$HOME/Desktop/rithmic_engine}"
OUT="${RESEARCH_DATA:-$HOME/starnet-work/rithmic_engine/research_data}"
FULL_RT_COMM="${FULL_RT_COMM:-4.00}"
HOLD_OUT="${DESK_HOLDOUT_DIR:-$HOME/starnet-work/holdout}"   # holdout verdicts: read by the BRIEF only   # assumed round-trip commission per full-size contract, USD

mkdir -p "$OUT"
set -a; . "$REAL/.env"; set +a
export PGHOST="$PG_HOST" PGPORT="$PG_PORT" PGUSER="$PG_USER" PGDATABASE="$PG_DB" PGPASSWORD="$PG_PASSWORD"
export PGOPTIONS='-c default_transaction_read_only=on'
q() { psql -X -q -v ON_ERROR_STOP=1 -c "\\copy ($1) to '$OUT/$2.tmp' with csv header" && mv "$OUT/$2.tmp" "$OUT/$2"; }

for d in 7 30 365; do
  q "select * from strategy_leaderboard(p_days => $d, p_handoff_only => false)" "leaderboard_${d}d_session.csv"
  q "select * from strategy_leaderboard(p_days => $d, p_handoff_only => true, p_slot_start => '10:00', p_slot_end => '12:00')" \
    "leaderboard_${d}d_slot_1000_1200.csv"
done
q "select * from strategy_leaderboard(p_days => 365, p_handoff_only => false, p_paper_label => 'backfill')" "leaderboard_backfill_session.csv"
# the desk's own replays of RESEARCHER variants (scripts/starnet/desk_replay.sh), same shape
q "select * from strategy_leaderboard(p_days => 365, p_handoff_only => false, p_paper_label => 'desk')" "leaderboard_desk_session.csv"

q "with t as (
     select account_label, strategy_id, symbol,
            (entry_time at time zone 'America/New_York')::date as session_date, entry_time,
            pnl_pts, mae_pts, exit_reason,
            pnl_pts * case when symbol = 'MES' then 50 else 20 end - $FULL_RT_COMM as usd
       from paper_trades
      where exit_time is not null and pnl_pts is not null
        and account_label <> 'desk_ho'),
   c as (select *, sum(usd) over (partition by account_label, strategy_id, session_date
                                  order by entry_time rows unbounded preceding) as cum from t)
   select account_label, strategy_id, symbol, session_date,
          count(*) as trades, sum((usd > 0)::int) as wins,
          round(sum(pnl_pts)::numeric, 2) as pnl_pts_per_contract,
          round(sum(usd)::numeric, 2) as net_usd_1full,
          round(least(0, min(cum))::numeric, 2) as worst_intraday_usd_1full,
          round(min(mae_pts)::numeric, 2) as worst_mae_pts
     from c group by 1, 2, 3, 4 order by 1, 2, 4" "daily_pnl_1full.csv"

q "select * from session_stats order by symbol, session_date" "session_stats.csv"

# holdout VERDICTS for the desk's variants (sessions from DESK_HOLDOUT_FROM on, label desk_ho). Only the
# aggregate per variant is published — never the holdout days themselves — so QUANT/SKEPTIC can judge
# out-of-sample without the researchers tuning on those sessions.
q "with t as (select strategy_id, account_label,
                     pnl_pts * case when symbol = 'MES' then 50 else 20 end - $FULL_RT_COMM as usd
                from paper_trades where account_label in ('desk', 'desk_ho'))
   select strategy_id,
          count(*) filter (where account_label = 'desk')    as in_sample_trades,
          round((avg(usd) filter (where account_label = 'desk'))::numeric, 2)    as in_sample_net_per_ct,
          count(*) filter (where account_label = 'desk_ho') as holdout_trades,
          round((avg(usd) filter (where account_label = 'desk_ho'))::numeric, 2) as holdout_net_per_ct,
          round((sum(usd) filter (where account_label = 'desk_ho' and usd > 0)
                 / nullif(-sum(usd) filter (where account_label = 'desk_ho' and usd < 0), 0))::numeric, 2) as holdout_pf,
          case when count(*) filter (where account_label = 'desk_ho') < 5 then 'THIN'
               when avg(usd) filter (where account_label = 'desk_ho') > 0
                and coalesce(sum(usd) filter (where account_label = 'desk_ho' and usd > 0)
                 / nullif(-sum(usd) filter (where account_label = 'desk_ho' and usd < 0), 0), 99) >= 1
                and avg(usd) filter (where account_label = 'desk') > 0 then 'PASS'
               else 'FAIL' end as holdout_verdict
     from t group by 1 order by 1" "desk_holdout.csv"
mkdir -p "$HOLD_OUT"; mv -f "$OUT/desk_holdout.csv" "$HOLD_OUT/desk_holdout.csv"   # out of research_data on purpose

# LIVE fills (for the FILLS agent — added 2026-09-30 on the founder's go; the 09-30 morning rule kept live
# history out, this exports only execution-quality columns, no account ids). Slippage columns are ticks.
q "select strategy, instrument, trade_date, direction, entry_time, exit_time, entry_price, exit_price, qty,
          pnl_points, pnl_usd, exit_reason, trigger_price, fill_price, entry_slippage_ticks, exit_slippage_ticks,
          entry_true_slip_ticks, entry_price_chase_ticks, signal_to_submit_us, submit_to_fill_ms
     from live_trades where account_label = 'tradeify' order by entry_time" "fills_live.csv"

# live trade vs its paper twin (same strategy id lower-cased, label tradeify, same direction, nearest entry
# within 10 min). ORB's live tag has no paper id of the same name: match it from strategies.csv yourself.
q "select l.strategy, l.trade_date, l.direction, l.entry_time as live_entry_time, l.entry_price as live_entry,
          l.exit_price as live_exit, l.pnl_points as live_pnl_pts, l.exit_reason as live_exit_reason,
          p.strategy_id as paper_id, p.entry_time as paper_entry_time, p.entry_price as paper_entry,
          p.exit_price as paper_exit, p.pnl_pts as paper_pnl_pts, p.exit_reason as paper_exit_reason,
          round(extract(epoch from (p.entry_time - l.entry_time))::numeric, 1) as entry_gap_s
     from live_trades l
     left join lateral (select * from paper_trades p
                         where p.account_label = 'tradeify' and p.strategy_id = lower(l.strategy)
                           and p.direction = l.direction
                           and p.entry_time between l.entry_time - interval '10 min' and l.entry_time + interval '10 min'
                         order by abs(extract(epoch from (p.entry_time - l.entry_time))) limit 1) p on true
    where l.account_label = 'tradeify' order by l.entry_time" "fills_parity.csv"

q "select strategy_id, account_label, engine, enabled, params_json::text as params_json
     from paper_strategies order by account_label, strategy_id" "strategies.csv"

python3 - "$REAL/config/tradeify_config.json" "$OUT/prop_rules.json" "$FULL_RT_COMM" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))
keys = ["symbol", "point_value", "qty", "daily_loss_limit", "trailing_drawdown_cap",
        "consistency_cap_pct", "max_daily_trades", "sl_points"]
rules = {k: c.get(k) for k in keys}
rules["assumed_full_rt_commission_usd"] = float(sys.argv[3])
rules["note"] = ("Limits of the live Tradeify account (1 NQ). daily_pnl_1full.csv is already scaled to "
                 "one full-size contract; paper fills are simulated, so treat results as optimistic.")
json.dump(rules, open(sys.argv[2], "w"), indent=1)
PY
date -u +%FT%TZ > "$OUT/EXPORTED_AT"
echo "[export] $(date -Is) research_data -> $OUT ($(wc -l < "$OUT/daily_pnl_1full.csv") daily rows)"
