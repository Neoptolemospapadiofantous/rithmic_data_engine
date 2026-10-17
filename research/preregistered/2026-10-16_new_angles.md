# Pre-registration — six new angles (written 2026-10-16, BEFORE any run)

Founder: "keep trying to find new strategies to add to our system". Owned data only, NQ 2010–26, 1.0 pt cost.
Family of 6 → controls at 99.2 % (0.05 / 6), 400 seeds; every pass then faces the frozen-setting holdout (choose on
2010–18 → test 2019–22; on 2010–22 → test 2023–26).

## Filters on the cascade (adopted only if: gates pass AND frozen holdout beats the plain cascade in both splits AND the
## trades the filter removes lose money — `<id>_compl` run alongside)
1. `cascade_es_agree` — ES has moved the same way from its own open by ≥ 0.15 × ES ATR at the decision minute (`xs`).
   Why: a move both indices share is broad institutional flow, not one sector's noise.
2. `cascade_rvol` — RTH volume so far ≥ 1.2 × its 20-day average at the same minute (new variable `rvol`). Why: heavy
   participation at the open means the big orders are actually there.
3. `cascade_gap_agree` — the open move has the same sign as the overnight gap (|gap| ≥ 0.1 ATR). Why: overnight
   repositioning continuing into the cash session.
4. `cascade_pibs_agree` — yesterday closed in the top 30 % of its range for a long (bottom 30 % for a short). Why:
   multi-day institutional programs continue across sessions.
## New standalone strategies (fleet candidates only if: gates pass AND both frozen holdouts positive AND monthly
## correlation with the cascade < 0.5)
5. `pm_drive` — anchor 12:00; at 13:00 / 13:30 / 14:00 if |px − price at 12:00| > k × ATR follow it to 15:55;
   k ∈ {0.15, 0.2}, stop {0.15, 0.2} × ATR, 2R. Why: afternoon pension / market-on-close programs start after lunch and
   run into the close.
6. `exhaustion_fade` — at 14:00 if |px − open| > m × ATR, m ∈ {1.0, 1.3}, fade it to 15:55; stop {0.2, 0.3} × ATR, 1.5R.
   Why: on extreme days late shorts / longs and dealers' hedging rebalance into the close against the day's move.

## Results (2026-10-16, run as registered; costs NQ 1.0 pt round trip)
| test | verdict | pts/trade | complement (removed trades) |
|---|---|---|---|
| cascade_es_agree | FAIL | +4.35 | +4.94 (removed trades better) |
| cascade_rvol | FAIL | +4.55 | +4.92 |
| cascade_gap_agree | **PASS** (897 trades) | **+8.59** | +3.35 (still positive) |
| cascade_pibs_agree | FAIL | +1.20 | — |
| pm_drive | FAIL | −0.65 | — |
| exhaustion_fade | FAIL controls | +4.49 on 125 trades | — |

- **cascade_gap_agree** — frozen holdout: chosen on 2010–18 (+4.30, t 3.6) → **+16.35 on 2019–22 (t 2.85)**; chosen on
  2010–22 (+9.01, t 5.1) → **+14.32 on 2023–26 (t 2.01)**. Beats the plain cascade (+6.89 / +10.03) in both splits.
  Eras +3.2 / +15.1 / +10.6; monthly correlation 0.57 with the cascade. The trades it removes still make +3.35, so by
  the rule above it is NOT adopted as a filter on the core cascade — it goes to paper as a sizing / forward candidate
  (`so_cascade_2r_gapagree`, TrendConfig `drive_gap_agree 0.1`).
- **cascade_pibs_agree** failed; its complement (`_compl`: cascade when yesterday did NOT close at its extreme in the
  trade's direction) passed the gates at +7.49, holdouts 2010–18 pick +1.97 (t 3.1) → +9.60 (t 2.8), 2010–22 pick +5.49
  (t 4.0) → +14.68 (t 2.6). This is the OPPOSITE of the registered prediction — a finding, not a confirmation. Paper
  forward test only (`so_cascade_2r_notext`, `drive_prev_extreme 0.7`).
- Both run on paper from 2026-10-19 under the working-fleet candidate rule (≥ 40 forward trades, avg > 0 and PF ≥ 1.1 →
  edge_lab re-test with the new data; else retire).
