# Stop for the day once a profit is reached (pre-registered 2026-10-17, before any run)

Founder: "lets say we reach a certain amount on the day we dont trade more. test this too".
Rule: the day's position(s) are closed as soon as the day's open profit reaches X dollars; no more trades that day.
Plans: the working plan on the 25K accounts — NQ cascade + gap retest (one trade a day: exact, from each trade's maximum favourable
excursion: if it ever reached X the day ends at X less costs) and NQ + ES (two trades a day: approximate, the two maxima are added, an
optimistic upper bound). X ∈ {none, $100, $150, $200, $300, $500}, sizes 2 and 3 micros per market, Select Daily 25K and Select Flex 25K.
It "works" if a lock level improves paid-per-year AND lowers the blown rate versus no lock, at the same size, on both plans.

## Results (2026-10-17) — NOT adopted
The lock cuts yearly profit at every level (NQ only, 2 micros: none $8,044/yr → $100 lock $1,521, $300 lock $4,041, $500 lock
$6,840) because the edge lives in the big winners (wins ~40 % of trades) while losers stay full size. Paid-per-year falls in almost
every case (Select Daily 25K NQ 2 micros: $2,644 → $581–2,195); eval pass and blown rates barely move. The only cell meeting the rule
(Flex 25K NQ + ES 2 micros, $300: $3,095 vs $3,038 paid, blown 53.7 % vs 57.5 %) is the approximate, optimistic one, and the exact NQ-only
plan fails at the same lock. Some locks pass the eval in fewer weeks on Flex NQ + ES ($150–200: 12.4–12.6 vs 18.2 wks to 1st payout)
but pay less over the year.
