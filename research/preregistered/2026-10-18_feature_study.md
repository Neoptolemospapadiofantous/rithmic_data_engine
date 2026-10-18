# Pre-registration — feature selection + a trained take/skip + exit model over the ORB-page reads (written 2026-10-18, BEFORE any run)

Founder: "do feature selection to see what works and what doesn't and train a model to find good entries, exits and everything else
we need, like trailing, regime etc."

## Base signals (entries already passing every edge_lab gate; the model decides take / skip and the exit, never the entry)
`so_cascade` (+4.80), `gap_or_retest` (+9.03), `mom_scan_3_pb` (+3.09), `union_2r` (+4.01) — NQ 2010-07 → 2024-12, 1.0 pt cost.

## Features at the decision bar (meta.hpp FeatureMaker; all trade-aligned where a sign applies)
Existing 21: move gap on_rng on_pos on_break vwap room_pd pullback rng_sofar pret pibs trend50 prng vix vixc1 vixpct flow es_move check dow event.
NEW 12 (the ORB page's reads, same definitions as the page's "how things are measured"):
`er45` straightness of the last 45 one-minute closes · `acr60` lag-1 autocorrelation of 1-min moves · `sd_pos` dir·(px − VWAP)/σ (VWAP band
position) · `ema_dist` dir·(px − EMA14 of 5-min closes)/ATR · `ema_touches` completed RTH 5-min bars that straddled EMA14 today ·
`struct` swing structure (N = 2) at the bar: +1 aligned with the trade, −1 against, 0 none · `bos_age` bars since the last BOS/CHoCH ·
`choch_against` a CHoCH against the trade in the last 30 min (0/1) · `or15_w` 15-min range width/ATR · `or15_pos` dir·(px − mid)/width ·
`reg_score` `zone_score` the page's regime and reversal-zone scores (gap/tape/cascade; bands/levels/EMA/CHoCH/failed break).
`dir_score` is not a feature (it is a function of the signal itself).

## Procedure (per base signal; nothing tuned after seeing results)
1. META walk-forward exactly as `scripts/edge_lab.sh meta`: boosted depth-2 trees on all 33 features, retrained every January on earlier
   years, take when predicted R > 0; gates unchanged (better per trade · beats random skip ≥ 95 % of 1000 seeds · beats shuffled labels
   ≥ 95 % of 100 seeds · better in ≥ 2 of 3 eras incl. 2023–26, ridge agrees · higher prop break-even fee).
2. FEATURE SELECTION = permutation importance on the walk-forward predictions: permute one feature within each test year (20 seeds),
   importance = drop in out-of-sample net per trade. A feature "works" only if its importance is > 0 in ≥ 2 of 3 eras AND its
   shuffled-label importance (same permutation on the shuffled-label model) is below it. Report all 33 ranked, per base.
3. REFIT on the features that work (and on the top 8 as a second cut); the refit must pass the gates of step 1 — otherwise the
   selection is reported as not adding anything.
4. EXITS: `scripts/edge_lab.sh exits` on each base with the existing rules (vwap, open, stall, vwap_stall, 15-min trail runner,
   flow) PLUS the trail runner split by `reg_score` ≥ 60 vs < 60 (trailing only in strong regimes). Gates unchanged (path-blind
   timing twin ≥ 99 %, ≥ 2 of 3 eras, higher prop fee).
5. The founder's manual marks (data/manual_marks.jsonl) are reported alongside once ≥ 50 exist: the model's take/skip on the same
   bars vs the marks (agreement rate, net of each), never used to fit.
Verdict per base: features that work (list), take/skip PASS/FAIL, exit PASS/FAIL. 2025+ judged once at the end, frozen.

## Results — steps 1–3 (appended 2026-10-18; `edge_lab meta --page`, reports data/edge_lab/reports/<id>__meta_page.md)
| base | all 33 features | features that work (refit = the verdict) | top 8 refit |
|---|---|---|---|
| so_cascade (+5.70 base) | +7.91 FAIL random skip | 12 work: +9.96 **FAIL robustness** | +9.66 PASS |
| gap_or_retest (+11.00) | +16.11 PASS | 14 work: **+15.66 PASS** (644 of 999 taken) | +16.66 PASS |
| mom_scan_3_pb (+3.78) | +5.51 FAIL random skip | 7 work: +4.77 **FAIL random skip** | +9.37 PASS |
| union_2r (+4.67) | +8.13 FAIL robustness | 11 work: +8.24 **FAIL robustness** | +9.08 FAIL robustness |
Features that work on ≥ 2 bases: gap, on_pos, vwap, trend50, acr60, reg_score (all 4), on_rng, pibs, vixc1, ema_touches, or15_w.
The page reads that carry signal: `reg_score` (every base), `acr60`, `or15_w`, `ema_touches`, `zone_score` (cascade), `sd_pos`/`bos_age`
(union). `struct` and `choch_against` carry none on the cascade and gap retest — at their decision bar the structure already agrees
with the trade 96 % of the time (2,238 of 2,322), so there is nothing to select on; `ema_dist`, `pullback`, `vixc1`, `on_break` HURT.
Verdicts: one PASS (gap retest, +15.7 vs +11.0, 64 % of signals taken). The cascade and momentum refits fail a control; the top-8
refits that pass are a second cut with no fresh data behind the choice. The selection step reads all years, so the refits are
not out-of-sample with respect to WHICH features; the paper fleet's forward results remain the clean test.
Not run yet: step 4 (exit / trail by regime) and step 5 (the founder's marks — none saved yet).
Verified: the unchanged `meta` run reproduces the stored so_cascade__meta report line for line; `edge_lab_validate.sh --fast` 15/15;
`make hermes-fast` 21 PASS / 0 FAIL.

## Step 4 result (appended 2026-10-18): `trail_reg` (15-min trail runner only when reg_score ≥ 60 at entry, else base)
FAIL "not better than base" on all four: cascade +4.42 vs +4.80 base (trailed 1,380 of 2,322), gap retest +4.74 vs +9.03, momentum
+2.66 vs +3.09, union +3.05 vs +4.01. Trailing loses to the fixed 2R target even in strong regimes. Still passing: cascade vwap+stall
(+5.82) and gap retest open-runner (+9.27), unchanged.
