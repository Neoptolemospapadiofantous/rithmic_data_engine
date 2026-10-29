# Flag quality: daily trend + clean-flag bars + FOMC (written 2026-10-22, BEFORE the run)

Founder: "yes" to testing the daily-trend filter together with the clean-flag bar conditions. Both came from exploratory maps over
2014-26 (research/programs/flag_regime.cpp, flag_bars.cpp), so every year is in-sample for the IDEA; the checks below are built to
catch a fluke: a walk-forward use decision, two out-of-sample halves, a second market, and flags never used to find them.

## Filters (bull written out, bear mirrored)
- DT50: take the flag only if yesterday's close is above the 50-day average (below for a bear flag). DT200: the same with the 200-day.
- CLEAN: the flag meets >= 3 of 4 bar conditions, each cut at the MEDIAN of all EARLIER years' flags of the same market (so the cut
  never sees the test year): C1 pole straightness (height ÷ sum of its bars' ranges) >= median; C2 share of up bars in the pole >= median;
  C3 flag bar size ÷ pole bar size <= median; C4 share of down bars in the flag <= median.
- NOFOMC: skip FOMC days.
- QUALITY = DT50 + CLEAN + NOFOMC (the combination judged first).

## Sets
Main rule (3-5m, A-list + 5m HH/HL + with the 5m break, both sides), bull rule, every 3-5m flag; FRESH = 1-2m flags (flag_hhhl export
tf15, charts 1-2 only — not used to find any of the filters): every flag and A-list.

## Walk-forward
Each test year 2017-2026 uses a filter only if, over all earlier years (2014 → Y−1), the filtered set beat the unfiltered set by >= +0.05R
a trade; otherwise that year trades unfiltered. One position at a time per market.

## Pass (QUALITY; the single filters are reported, not judged)
On BOTH NQ and ES: (1) walk-forward 2017-26 avg R >= unfiltered + 0.05R and PF higher, for the main rule AND every flag; (2) the
improvement is > 0 in both 2017-21 and 2022-26 for every flag; (3) FRESH 1-2m every flag: QUALITY flags beat the rest by >= +0.05R.
All three, or QUALITY is not adopted.

## Results (2026-10-22; research/programs/flag_quality.cpp, out/flag_quality.txt)
**QUALITY is NOT adopted — it fails all three points because ES does not confirm.**
- (1) Walk-forward 2017-26, QUALITY vs no filter: main rule NQ +0.571R PF 1.82 vs +0.432R (+0.139R, 13/yr) but ES +0.178R vs +0.301R
  (−0.123R); every flag NQ +0.298R vs +0.165R (+0.133R) but ES +0.098 vs +0.095 (+0.003R).
- (2) Every flag, ES halves +0.086 / −0.094 — not better in both.
- (3) Fresh 1-2m flags: NQ +0.182R vs −0.001R (+0.184R), ES −0.004 vs −0.022 (+0.018R) — under +0.05R on ES.
Single filters (reported, not judged):
- CLEAN (bar conditions) is the strongest piece: every flag NQ +0.124R, ES +0.088R, both halves on both markets; main rule NQ +0.278R
  (PF 2.17) but ES +0.015R; fresh A-list 1-2m +0.107R NQ / +0.136R ES, fresh every flag +0.054 NQ / −0.005 ES.
- DT200 beats DT50: every flag +0.050 / +0.023R, main rule +0.055 / +0.146R, fresh every flag +0.097 / +0.135R; DT50 is mixed (fresh ES
  −0.035R).
- No FOMC: the walk-forward never switched it on (too few FOMC trades to clear +0.05R over earlier years), but fresh 1-2m FOMC flags lose
  hard (−0.39R NQ, −0.27R ES on 64-65; A-list −0.47 / −0.72R).
- Bull rule (not judged): QUALITY NQ +0.710R PF 2.16 (7.6/yr) vs +0.477R; ES +0.524R PF 1.74 (11.4/yr) vs +0.410R, better in both halves on
  both markets.

## Part 2 — the A+ setup (written 2026-10-22 after Part 1's results, BEFORE its own run)
Founder: "yes". A+ = the bull rule (3-5m, bull, A-list, 5m HH/HL, with the 5m break) AND DT200 (yesterday's close above the 200-day
average) AND CLEAN (>= 3 of 4 bar conditions, earlier-years medians) AND not an FOMC day. Chosen after Part 1 (bull rule + QUALITY looked
good; DT200 replaced DT50 because it held better) — so the bar is higher than Part 1's.
Pass, ALL on BOTH NQ and ES: (1) walk-forward 2017-26 (A+ used in a year only if it beat the bull rule by >= +0.10R over earlier years):
>= +0.10R a trade over the bull rule and PF higher; (2) better than the bull rule in 2017-21 AND 2022-26; (3) >= 5 A+ trades a year;
(4) FRESH bull A-list 1-2m flags (2015-26): A+ conditions (DT200 + CLEAN + no FOMC) beat the rest by >= +0.05R.
Also reported, not judged: the bear mirror (bear rule + DT200 below + CLEAN + no FOMC), A+ by year.

### Part 2 results (2026-10-22; flag_quality.cpp … aplus, out/flag_aplus.txt)
**A+ does NOT pass (fails 1, 2 and 4).**
- NQ walk-forward: A+ +0.738R PF 2.25 (7.6/yr) vs the bull rule +0.477R PF 1.75 — but the whole gain is 2017-21 (+0.457R); 2022-26 −0.086R
  (A+ by year: 2023 −0.38, 2025 −0.51).
- ES walk-forward: −0.029R vs the bull rule (the walk-forward used A+ in only 2 of 10 years); A+ every year +0.416R vs +0.410R — no gain.
- Fresh bull A-list 1-2m flags: NQ −0.080R (A+ worse than the rest), ES +0.152R — the markets disagree.
- Bear mirror (not judged): too few trades (14-17 in 10 years).
Conclusion: the extra conditions sharpen the past on NQ but do not carry into recent years or to ES. The plain bull / main rules stay.
