# Frozen-setting holdout of every winner (2026-10-16)

Founder: "find winning runs and test that in data it has never seen before". The only data never seen at all is the
future (the paper fleet's forward test). The closest honest test on history: choose each idea's setting on a development
window ONLY (best in-sample variant, no walk-forward), freeze it, and test it once on a later window that played no part
in the choice. Two splits: A = choose on 2010–2018, test 2019–2022; B = choose on 2010–2022, test 2023–2026. (The later
windows were seen in earlier full-period runs, so this protects the CHOICE of setting, not the idea itself.)
NQ 1.0 pt / ES 0.35 pt real cost per contract.

| idea | split | frozen setting | dev net/trade (t, n) | HOLDOUT net/trade | PF | t (n) |
|---|---|---|---|---|---|---|
| so_cascade | A | k1 0.3, k2 0.3, 3R, stop 0.2 | +1.23 (2.42, 1,333) | **+6.89** | 1.28 | 2.46 (630) |
| so_cascade | B | k1 0.3, k2 0.25, 3R, stop 0.25 | +3.62 (3.39, 2,191) | **+10.03** | 1.28 | 2.41 (587) |
| strong_open | A | k 0.2, 3R, stop 0.35 | +0.83 (0.90, 806) | +6.03 | 1.17 | 1.24 (415) |
| strong_open | B | k 0.25, 3R, stop 0.25 | +4.05 (2.44, 935) | +12.95 | 1.34 | 1.89 (242) |
| weak_day_long | A | VIX ≥ 20, IBS ≤ 0.5 | +7.95 (1.88, 202) | +21.22 | 1.33 | 1.77 (250) |
| weak_day_long | B | VIX ≥ 20, IBS ≤ 0.5 | +15.29 (2.22, 452) | +67.15 | 2.04 | 1.89 (85) |
| es_cascade | A | k1 0.2, k2 0.35, 3R, stop 0.25 | +0.30 (1.44, 1,241) | +1.27 | 1.18 | 1.57 (584) |
| es_cascade | B | k1 0.2, k2 0.25, 3R, stop 0.35 | +0.55 (1.69, 2,211) | +1.15 | 1.12 | 1.16 (570) |
| ema_cross_vwap | A | 2R, stop 0.15 | −1.14 (−3.77, 1,419) | +0.37 | 1.02 | 0.21 (681) |
| ema_cross_vwap | B | 3R, stop 0.25 | +0.08 (0.08, 2,100) | +8.56 | 1.27 | 2.18 (539) |

Reading: the cascade survives best — a setting frozen on old data did better on the later years in both splits (t ≈
2.4). Weak-day and strong open stay positive at t 1.8–1.9; the ES cascade small and positive. `ema_cross_vwap` had no
edge on its development data in either split; its 2023–26 result is not predicted by anything before it.
Common to all: every edge is bigger after 2019 than before — the 2010–18 market paid little for any of them.
