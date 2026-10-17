# A trade every day (pre-registered 2026-10-17, before any run)

Founder: "review to see what we can do for me to take daily trades". The plan trades ~4 days a week. The paper cascade
(so_cascade_2r) has a 5th check the tested edge_lab rule never had: 12:00, any move from the open. Question: does that last
check pay, i.e. can the cascade trade (almost) every day without losing its edge?
- cascade_noon: so_cascade exactly, plus a 12:00 check: px != open → follow sign(px − open). Same grid, exits, dates.
- Judged two ways: (1) edge_lab gates on the whole idea; (2) the 12:00 trades on their own (the trades the extra check adds),
  from the walk-forward trades: net pts/trade > 0 after cost in ≥ 2 of 3 eras including 2023-26 → keep the 12:00 check.
