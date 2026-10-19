# Inputs behind the 2026-10-19 Vest / behaviour studies

Saved from the session scratchpad so every number in research/preregistered/2026-10-19_*.md can be re-run from the repo.
Trade files are edge_lab `--trades` output (date,entry_epoch,exit_epoch,dir,gross_pts,net_pts,mae,mfe,src); the `_k` files add a 10th
column = the trade's stop in ATR (the Vest simulators size each trade to it).

| file | what |
|---|---|
| cascade_2r / gap_retest_2r / gap_rule_1000_2r .trades.csv | the book's three setups, 0.35 ATR stop, 2R, full history (rebuilt specs: research/ideas via the 10-19 scratch specs, same rules as book F) |
| book_F.trades.csv | book F (side by side) as adopted 2026-10-18 |
| book_F_k / book_first_trade_k / weakness_long_1p5atr_k / overnight_O3_k / vest_funded_plan_k .csv | the Vest plan pieces with stop multiples (funded plan = F + weakness long + O3) |
| nq_atr_edge_lab.csv | edge_lab's own per-day ATR (14 prior RTH ranges) |
| nq_atr_lagged.csv | the ATR file most 10-18/10-19 sims used = edge_lab ATR lagged one day (mean ratio 0.981; results re-checked on the exact ATR, unchanged) |
| sessions_all.csv | RTH session dates |
| nq_sessions.txt | Asia / London / NY session table (date session open price@check close high low), from awk_20261019/sess.awk |

Example — the Vest operation: `awk -v COST=2 -v SCALE=1 -v FROM=20190101 -v TO=20260101 -v LAST=20251101 -v RF=50 -v T=500 -v B=250 -v ER=120 -v MAXB=10 -v SLOTS=10 -F, -f research/programs/awk_20261019/port5.awk research/programs/inputs_20261019/nq_atr_edge_lab.csv research/programs/inputs_20261019/sessions_all.csv research/programs/inputs_20261019/book_first_trade_k.csv research/programs/inputs_20261019/vest_funded_plan_k.csv`
