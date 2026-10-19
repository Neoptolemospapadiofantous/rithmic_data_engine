# Flag chart scripts (2026-10-19) — Node, run against the flag-chart page's data (scratch export of NQ 1m, 2023-10 → 2026-10-19)
`scan.js` (the page's own scan offline), `sweep.js` (pole bars × pole size × flag bars × tightness × target 1 / 1.5 / 2R / measured move,
NY, 1m and 5m, net of 1 pt, two halves), `robust.js` (cells positive in both halves with ≥ 75 % of neighbouring settings positive),
`holdout.js` (the chosen general settings once on 2010-07 → 2023-09 from data/edge_lab/cache/NQ.csv). Outputs: research/programs/out/flag_*.txt.
These are JavaScript, not Python; they only read files.
