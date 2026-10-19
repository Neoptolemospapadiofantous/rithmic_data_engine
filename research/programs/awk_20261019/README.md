# awk tools behind the 2026-10-19 Vest / behaviour studies

Copied from the session scratchpad so the numbers in research/preregistered/2026-10-19_*.md can be re-run. Inputs: edge_lab
`--trades` CSVs (date,entry,exit,dir,gross,net,mae,mfe,src[,stop multiple]), `atr.csv` (date,ATR — the day's RTH ATR from the
edge_lab cache), `sessions_all.csv` (one RTH date per line), the NQ 1m cache (`data/edge_lab/cache/NQ.csv`, run with
`TZ=America/New_York gawk`) and `data/regime/VIX*_History.csv`.

| file | does |
|---|---|
| sbs_stats → `stats.awk` | book metrics + Vest eval sim (vars NAME, RISK) |
| `sim2.awk` / `sim3.awk` | Vest eval sim, strict (MODE=sum) or lenient (MODE=seq) daily-loss model; sim3 adds TO |
| `sim4.awk` / `sim5.awk` | Vest eval sim with each trade sized to its own stop (field 10); sim5 adds COST |
| `fund3.awk` / `fund4.awk` | eval + funded account (claim at CL), FROM/TO, COST, SCALE (cost × ATR/320) |
| `dyn2.awk` | room-based sizing (eval MODE=eval / funded MODE=fund) with a size floor RMIN |
| `port4.awk` | portfolio of 10 accounts: eval ER, funded RF, claim T keep B, refill MAXB/day, 12-month windows |
| `sess.awk`, `week.awk`, `bookweek.awk`, `levels.awk`, `ctx.awk`, `gex.awk` | session table, weekly/weekday profile, book by weekday, pre-session levels, multi-day context, VIX-regime ranges |
