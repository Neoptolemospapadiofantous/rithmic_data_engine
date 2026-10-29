# Does what we can measure improve the flag book? Layer-1 (before the day) and layer-2 (intraday) overlays — pre-registered
# 2026-10-23 03:05 Cyprus, BEFORE the run

Founder: "would our flag strategy … be benefited from 1 and 2" (the measurable layers in the forecast battery). Base = BOOK2,
NQ + ES, 2014-01-02 → 2026-10-15, net R from the W15 exports, $ at 1 MNQ / 1 MES at today's price. Every threshold below is a
tercile or median computed on 2014-19 ONLY, then applied unchanged to 2020-26. Already tested and NOT repeated: DT200, NOFOMC,
VIX regime filters, news days, HTF sizing (all failed, 2026-10-22 notes).

Filters (take the flag only when …) and their complements:
- L1a ATR5 not in the lowest tercile (D7 hinted low-ATR days earn least)      - L1b gap today ≤ 0.5 ATR5 (no big-gap day)
- L1c flag side = gap direction on big-gap days (> 0.5 ATR5), all flags on other days (P4 family)
- L2a first-30-min range / ATR5 ≥ 2014-19 median ("hot open")              - L2b its complement ("quiet open")
- L2c room: range-so-far at the flag's fill ≤ 1.0 × F (F = 2.09 × first-30 range) — the day is not exhausted
- L2d flag side = sign(price at fill − 09:30 open) (with the day so far)      - L2e flag side = sign(price at fill − VWAP proxy = mean of RTH closes so far)
- L2f fill before 11:00 (the registration window the 2026-10-22 book_tests found)
Sizing (1 micro otherwise, 2 when …): S1 ATR5 top tercile; S2 hot open (L2a); S3 L2c room AND L2d with the day.

Judged (per overlay): kept share, net R of KEPT vs DROPPED trades in both halves, eval model (Select-50K-like: target $3,000,
EOD trail $2,000, day cap −$1,000, best day ≤ 40 %, fee $165, start every 3rd session) pass % and fee per pass vs the base.
PASS filter = kept net R ≥ base + 0.03R in BOTH halves AND dropped net R ≤ base − 0.03R in both halves AND fee per pass ≤ base.
PASS sizing = fee per pass ≤ base − 10 % AND $/year ≥ base. Holm over the 9 filters. A pass is a paper candidate only.
Output research/programs/out/flag_context_overlays.txt. Nothing changes any config.

## Results (2026-10-23; research/programs/out/flag_context_overlays.txt; 6,320 BOOK2 trades; base net R +0.068 / +0.207, $7,434/yr at 1 MNQ + 1 MES, eval pass 23 %, $723 a pass)
| overlay | kept | kept R 14-19 / 20-26 | dropped R 14-19 / 20-26 | $/yr | pass | fee/pass | verdict |
|---|---|---|---|---|---|---|---|
| L1a ATR5 not in the low tercile | 78 % | +0.140 / +0.199 | −0.076 / +0.264 | 7,586 | 18 % | $900 | fail — separates in 2014-19 only, reverses after |
| L1b no big-gap day | 69 % | −0.007 / +0.150 | +0.242 / +0.330 | 3,865 | 15 % | $1,102 | fail — big-gap days are the BEST flag days |
| L1c with the gap on big-gap days | 85 % | +0.034 / +0.225 | +0.272 / +0.109 | 6,343 | 25 % | $671 | fail — inconsistent |
| L2a hot open (first-30 range ≥ median ATR share) | 49 % | +0.143 / +0.261 | −0.006 / +0.157 | 4,920 | 22 % | $740 | fail on fee/pass (R separation real, both halves) |
| L2b quiet open | 51 % | −0.006 / +0.157 | +0.143 / +0.261 | 2,514 | 16 % | $1,023 | fail |
| L2c room left (range ≤ F) | 70 % | +0.092 / +0.197 | +0.009 / +0.228 | 4,608 | 25 % | $651 | fail — inconsistent |
| L2d with the day / L2e with VWAP | 100 % | — | — | — | — | — | not separable: every BOOK2 flag is already with the day (the export's path rule) |
| **L2f fill before 11:00** | 49 % | **+0.115 / +0.318** | +0.021 / +0.103 | 5,701 | **33 %** | **$499** | **PASS** (Holm-significant separation) |
| S1/S2/S3 size 2 micros on ATR-high / hot open / room+day | | | | 12.0–12.9k | 17 % | $944–955 | fail — every conditional doubling lowers the pass rate; flat 2 micros is worst ($1,126) |

Read: (1) the only overlay that both separates good from bad trades in BOTH halves and improves the eval is the TIME filter —
flags filled before 11:00 (+0.32R in 2020-26 vs +0.10R after 11:00). It is the same finding as book_tests T7b (2026-10-22),
which failed there only on total account EV because half the trades (and the funded-stage payouts they carry) go with it: here
$/yr falls 23 % while the pass rate rises 23 % → 33 % and a pass costs $499 instead of $723. **So it is a stage rule, not a
book rule: trade only the pre-11:00 flags while in an EVAL (cheaper, faster pass), the whole book once FUNDED (more dollars).**
(2) Layer-1 (before the day) adds nothing stable: the ATR regime flips between halves and big-gap days are the book's best
days, so excluding them is the worst thing on the list. (3) Layer-2 "hot open" has a real R separation (both halves) but the
account does not reward it; keep as a sizing hint only if the funded stage ever needs one. (4) Conditional sizing never helps
the eval — the trailing drawdown is in dollars and doubling anything doubles the hits. The range forecast's correct use is
risk (how wide / how many) exactly as the forecast battery said, not entry selection. Nothing changes any config; the
pre-11:00 eval rule is a paper candidate.
