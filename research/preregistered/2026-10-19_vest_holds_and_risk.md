# Pre-registration — multi-day weakness book on Vest, weekend holds, first-year risk, regimes, live slippage (written 2026-10-19, BEFORE any run)

Founder: "do all" (after Vest confirmed overnight / weekend holds are allowed; hourly funding applies). Base = the Vest plan incl. the
overnight hold O3 (2026-10-19_attack_behaviour). Operation rule as before (port5.awk, 2 and 6 pts scaled, 2019–25 and 2010–18; adopt
= mean net/yr up in all four, losing windows not up in any). Funding ≈ 6 pts × ATR/320 per night held (an assumption).
1. Weakness book (docs/strategy_weakness_vix_book.md rules): any weakness at the 16:00 close (IBS < 0.3 OR RSI2 of RTH closes < 20 OR two
   lower closes OR close ≤ open − 1 ATR) AND the fear gate (prior VIX ≥ 18 OR VIX up > 10 % over 5 sessions OR VIX in the top third of
   its last 252 closes); long at the 18:00 reopen; stop 1.5 ATR (fills at the stop or the session open if gapped), sized to the stop.
   W1 = exit next 15:59 close, 1 unit. W2 = W1 with the conviction stack (1 unit, +1 IBS < 0.3, +1 gate, +1 both → 1–4 units, the extra
   units as extra positions). W3 = hold up to 3 sessions, exit at the first RTH close above the previous RTH close, else the 3rd.
   Each REPLACES O3 in the plan; adopt the best one that passes the operation rule against the plan with O3.
2. Weekend: O3's Friday signals (bought 15:59 Friday, held through the Fri 20:00 → Sun lock) vs the same signals bought at Sunday 18:00:
   net per trade and worst trade, 2010–18 / 2019–26. If the Sunday entry is ≥ as good per trade in both eras, O3 skips Friday 15:59 and
   buys Sunday 18:00 instead.
3. First-year risk (description): 2,000 bootstrapped years (months drawn with replacement from the plan's own 2019–25 and 2010–18
   monthly operation net, keeping each month's 10-account state), share of losing years, 5th percentile year, worst 3-month stretch.
4. Regimes (description): the operation's 12-month windows split by the NDX-proxy trend over the window (NQ up > 15 %, flat, down
   > 10 %) and by mean VIX (< 15, 15–20, > 20).
5. Live slippage: + 1.25 pts per entry for the market-order entries of the book (≈ 5 ticks, the 2026-09 Tradeify live measurement) on
   top of the 2 / 6 pt costs; report the operation (no adopt rule — a stress test).

## Results (2026-10-19; research/programs/awk_20261019/weakbook.awk, port_cont.awk)
1. Weakness book per signal (2 pt, no funding; 2010–18 / 2019–24 / 2025–26): W1 any weakness & gate, 18:00 → next close +0.29 / +19.85 / +50.02
   pts (+0.010 / +0.058 / +0.054 R); W2 conviction stack +2.92 / +47.98 / +95.38 (+0.040 / +0.154 / +0.098 R, units incl.); W3 hold ≤ 3
   sessions, exit on strength −1.29 / +27.73 / +146.41 (+0.017 / +0.076 / +0.226 R, win 64–67 %).
   Operation vs the plan with O3 ($16,139 (5) / $11,201 (5) / $12,542 (7) / $7,718 (7)): W1 $16,235 (5) / $11,230 (4) / $12,376 (8) / $7,774 (6);
   W2 $8,354 (26) / $8,164 (11) / $5,594 (29) / $5,564 (20) — the 1–4 unit stack breaches $5k accounts; W3 $16,884 (5) / $11,308 (6) / $12,930 (8) /
   $8,418 (5) (with funding $16,451 / $11,152 / $12,338 / $8,242). **None adopted** (W3 adds losing windows in two cells; W1 mixed). O3 stays.
2. Weekend (O3 Friday signals): buy Fri 15:59 and hold +21.25 (n 15) / +67.58 (n 31) pts/trade vs buy Sun 18:00 +9.35 / +145.97 → mixed by
   era → **rule not met; keep buying at Friday's close.** Worst trade −654 vs −543 pts (2019–26); the weekend gap averaged +14 / −22 pts.
3. First-year risk. Continuous 10-account run, 2 pts: $1,673 / month (2019–25), $1,150 (2010–18). Bootstrapped years (months drawn
   independently): mean $19.7k / $13.5k, P(losing year) 0.2 % / 1.8 %, 5th percentile year +$7.3k / +$2.4k, worst 3 months 5th pct −$1.2k /
   −$2.4k. Independent months understate clustering (2022); the empirical 12-month windows from a fresh start lose in 5 of 82 / 5 of 89
   (6 %) — read that as the realistic first-year loss chance.
4. Regimes (171 windows, 2 pts): NQ up > 15 % $13.4k/yr (6 losing of 107), flat $15.3k (0 of 49), down > 10 % $14.5k (0 of 9); mean VIX
   < 15 $11.3k (5 of 52), 15–20 $16.6k (1 of 62), > 20 $13.7k (0 of 51). Works in falling markets; weakest in calm bull years.
5. Live entry slippage (+1.25 pts per trade on top of 2 / 6 pt costs): $14,714 (6/82) / $9,850 (5/89) / $11,177 (13/82) / $7,199 (9/89) vs $16,139 / $11,201 / $12,542 / $7,718 — the plan survives live-style slippage, about −$0.5–1.5k/yr.
