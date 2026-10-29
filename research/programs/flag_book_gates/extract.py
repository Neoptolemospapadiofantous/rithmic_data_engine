# Pull every historical base flag trade + order-book / flow features of the minute(s) before entry.
import os, psycopg2, csv, sys
out = sys.argv[1]
c = psycopg2.connect(host=os.environ.get("PG_HOST","localhost"), user=os.environ.get("PG_USER","rithmic_user"),
                     dbname=os.environ.get("PG_DB","rithmic"), password=os.environ["PG_PASSWORD"])
c.set_session(readonly=True)
q = """
with t as (
  select id, entry_time, (entry_time at time zone 'America/New_York')::date d,
         case when direction='LONG' then 1 else -1 end dir, pnl_usd/qty pnl, pnl_pts, exit_reason,
         date_trunc('minute', entry_time) - interval '1 minute' m1
  from paper_trades where account_label='nq_hist' and strategy_id='mtf_flags_v5' and exit_time is not null)
select t.id, t.entry_time, t.d, extract(hour from t.entry_time at time zone 'America/New_York')*60
         + extract(minute from t.entry_time at time zone 'America/New_York') mod, t.dir, t.pnl, t.pnl_pts, t.exit_reason,
       b.avg_imbalance imb, b.bid_heavy_pct bh, b.ask_heavy_pct ah, b.avg_spread_pts spr, b.quotes,
       r1.volume v1, r1.buy_volume bv1,
       (select sum(2*buy_volume - volume) from bars_1m x where x.symbol='NQ' and x.minute between t.m1 - interval '4 minute' and t.m1) dl5,
       (select sum(volume) from bars_1m x where x.symbol='NQ' and x.minute between t.m1 - interval '4 minute' and t.m1) v5,
       o.qty_imbalance qimb, o.order_count_imbalance oimb, o.cancel_to_add_ratio ctar,
       o.bid_qty_top10bps bq, o.ask_qty_top10bps aq
from t
left join book_1m b on b.symbol='NQ' and b.minute = t.m1
left join bars_1m r1 on r1.symbol='NQ' and r1.minute = t.m1
left join mbo_1m o on o.venue='GLBX' and o.symbol='NQ' and o.ts_minute = t.m1
order by t.entry_time
"""
cur = c.cursor(); cur.execute(q)
cols = [d[0] for d in cur.description]
with open(out, "w", newline="") as f:
    w = csv.writer(f); w.writerow(cols); n = 0
    for r in cur: w.writerow(r); n += 1
print(n, "trades written")
