# More trades a day from tested strategies (pre-registered 2026-10-17, before any run)

Founder: "find other tests to do" (goal: 1–3 trades a day, faster payouts on 25K, autopilot).
A. Both directional strategies on the same day: today the plan takes only the first of cascade / gap retest. Variant: take both when
   they agree in direction (never opposite); on NQ, and on NQ + ES. Judged on the 25K plans (Select Daily / Flex), same sizes as before:
   it helps if paid-per-year rises without a higher blown rate.
B. es_weak_day: weak_day_long's exact rule on ES (pibs <= x, vix >= v, 09:30 → 15:55). edge_lab gates as usual.

## Results (2026-10-17)
B. es_weak_day PASS: +3.38 ES pts/trade (690 trades), eras −0.18 / +5.05 / +9.71 — same days as the NQ weak-day long (adds size, not days).
A. Both directional strategies when they agree (2 micros per trade, 25K; "pessimistic" = the day's trades merged so their worst points coincide):
   NQ: Select Daily paid/yr $2,644 → $3,879 (optimistic) / $2,628 (pessimistic); Flex 25K $2,781 → $3,957 / $3,492; blown 53 % → 46 % / 49 %,
   eval pass 43 % → 54 % / 50 %. ADOPTED for NQ (more paid on Flex, fewer blown accounts in both cases).
   NQ + ES: blown 58–61 % → 60–65 %; not adopted on 25K. Correction: NQ + ES "first per market" on 25K is also worse once concurrent
   worst points are counted (Flex paid $2,223 vs NQ-only $2,781, blown 61 % vs 53 %) — on 25K trade NQ only.
