# Fastchess SPRT relaunch checklist

This guide accompanies `scripts/fastchess_sprt_relaunch.bat` and captures the
workflow recommended after the regression seen in the 2.70 vs 2.40 match.

## 1. Pre-SPRT gating match (sanity check)

Run a 200-game gauntlet before investing time in a full SPRT. The batch script
prompts for this step automatically and now inspects the resulting log: if the
score dips below the configurable threshold (`GATING_MIN_SCORE`, default 47 %),
it halts the SPRT unless you explicitly override the warning. Accept the gating
match unless you have already vetted the new network on the same positions.

Recommended expectations:

- Score ≥ 47 % (or at least a non-negative net Elo) before continuing.
- Review the generated PGN quickly for recurring tactical collapses around the
  Stonewall/Leningrado structures reported earlier.

If the gating match fails, stop and revisit the network or search parameters
before proceeding. Lowering the threshold in the BAT file is possible but
should be reserved for exceptional cases where you have other evidence that the
candidate is stable.

## 2. Full SPRT configuration

The script relaunches a 10+0.1 SPRT with the exact book used in the regression
report (`UHO_2024_8mvs_+085_+094.pgn`). Key options baked into the harness:

- `Threads=1`, `Hash=32`, `Ponder=off` to match the previous benchmark.
- Optional `EvalFile` overrides so each engine can point to its dedicated NNUE.
- Automatic adjudication (`movenumber=6`, `score=550`) to trim obviously lost
  games and faster resignations at `-3.5` pawns for four moves.
- Persistent logging (`out/`) with timestamps for both gating and SPRT runs.

Adjust the paths at the top of the BAT file as needed (engines, NNUE, book,
Syzygy tables). Use `choice` defaults to skip the gating run only when you have
fresh evidence that the candidate is stable.

## 3. Monitoring during the run

- Track the `sprt_*.log` file; Fastchess reports the running LLR each interval.
  Abort early if LLR drops below `-1.0` again to save time.
- Every 50 games, check the autosaved PGN for recurring tactical motifs
  (sacrifices on e3/c5, repeated shuffling with rooks). This surfaces whether
  the retraining actually addressed the issues highlighted in the regression
  dataset.

## 4. Post-run follow up

- Archive logs/PGNs alongside the NNUE hash or git commit to keep provenance.
- If LOS remains below 5 %, revisit the training recommendations in
  `docs/training/hyperparameter_schedule.md` before iterating again.
- Upon success (LLR ≥ +2.5), promote the candidate network and update any
  deployment scripts that carry the baseline EvalFile path.

This pipeline ensures each SPRT attempt is backed by a quick tactical sanity
check, reducing wasted cycles when the candidate still exhibits the previous
regressions.
