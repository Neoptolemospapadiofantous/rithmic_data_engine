# Pre-registration — which components of the every-day book earn their place (written 2026-10-18, BEFORE any run)

Founder: "more tests". The every-day book (2R / 0.35 ATR, 1 trade a day; 2026-10-18_high_winrate_book "current") pays best per
prop account (2026-10-18_prop_daily_stop). Five components, in priority order: W weakness long (09:30, no stop), G 10:00 gap rule,
M 10:00 momentum rule, C cascade (09:50 / 10:30 / 11:00 / 11:30), R gap retest. Leave-one-out: the book without each component
(5 books) plus the full book, everything else unchanged.

Measured per book: pts/session at the 2.0 pt cost in 2010–18, 2019–24, 2025–26 (screen); per-day max drawdown and worst day
2019–26 (`--trades`); payout_sim test EV per account on select_daily_50k, topstep_50k, select_flex_50k (same settings as
2026-10-18_prop_daily_stop: 60-session accounts, train 2019-01 → 2023-05, test → 2026-10, $150 reset, blow-ups allowed).
Rule (fixed now): a component is DROPPED only if the book without it has higher test EV on at least 2 of the 3 plans AND
pts/session ≥ the full book's minus 0.5 in 2010–18 and in 2019–24. Otherwise it stays.

## Results
(appended after the runs — rules above unchanged)
Run 2026-10-18 (build-exp/edge_lab `--trades` + build-exp/payout_sim, same settings as prop_daily_stop; 2 pt cost; sessions
2010–18 2,057 · 2019–24 1,472 · 2025–26 314).

| book | pts/session 2010–18 / 2019–24 / 2025–26 | max DD 2019–26 | worst day | Select Daily 50K | Topstep 50K | Select Flex 50K |
|---|---|---|---|---|---|---|
| full | +0.39 / +8.86 / +21.34 | 1,279 | −550 | $1,801 | $2,237 | $1,806 |
| no W (weakness long) | −0.22 / +6.91 / +4.26 | 1,731 | −278 | $1,643 | $2,205 | $1,672 |
| no G (10:00 gap rule) | +0.35 / +9.06 / +21.52 | 1,312 | −550 | $1,845 | $2,393 | $1,859 |
| no M (10:00 momentum) | +0.46 / +9.04 / +21.17 | 1,448 | −550 | $1,889 | $2,413 | $1,871 |
| no C (cascade) | +0.54 / +9.12 / +25.20 | 1,235 | −550 | $1,319 | $1,296 | $1,584 |
| no R (gap retest) | +0.63 / +7.30 / +25.40 | 1,269 | −550 | $1,142 | $1,291 | $1,120 |
| no G and no M (check, not in the rule) | +0.41 / +9.23 / +21.35 | 1,481 | −550 | $1,931 | $2,534 | $1,930 |

Verdict by the rule: W, C and R STAY (removing each lowers EV on all three plans; without W 2025–26 falls to +4.26 and the
drawdown rises). G and M are DROPPED from the every-day book (higher EV on all three plans, points unchanged); dropping both
together also holds. Likely why (not measured): the 10:00 rules mostly fire on days the 09:50 cascade already traded — inside a
one-position book they are redundant, while standalone they remain the confirmed 10:00 rules (and the Plan page's "Today" read).
The EV gains are 7–13 % on a test period that overlaps the confirmation years — a simplification, not a new edge.
Book from here: weakness long 09:30 · cascade 09:50 / 10:30 / 11:00 / 11:30 · gap retest; 2R, 0.35 ATR, 1 trade a day.
