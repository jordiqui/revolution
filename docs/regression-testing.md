# Regression Testing Playbook

This guide documents the focused gauntlets and statistical testing that should be
run when touching evaluation or search heuristics.

## 1. Focused Gauntlets from Loss Positions

1. Collect the FEN strings that triggered the regression. Save them to a text
   file (one FEN per line).
2. Run short matches that start from each position using the helper script:

   ```bash
   scripts/focused_gauntlet.sh path/to/losses.fen /path/to/baseline 40 5+0.05
   ```

   Environment variables allow additional tweaking:

   - `ENGINE` – override the Revolution binary (`ENGINE=src/revolution`).
   - `PLIES` – number of random book plies Cute Chess should play before the
     engines take over (default: `8`).

   The script randomises the order of the supplied FENs and stores the PGN as
   `focused-gauntlet.pgn`.

3. Inspect the PGN for the defensive choices that previously failed. Compare the
   new score drift to verify that the king safety heuristics now steer the search
   away from the risky continuations.

## 2. Extended SPRT Coverage

For larger verification, run sequential probability ratio tests at multiple time
controls to ensure gains generalise:

```bash
# Fast 5+0.05 blitz SPRT
scripts/match.sh /path/to/baseline 200 "5+0.05" \
  --sprt "elo0=0 elo1=5 alpha=0.05 beta=0.05" --openings losses.fen

# Slower 15+0.1 rapid SPRT
scripts/match.sh /path/to/baseline 120 "15+0.1" \
  --sprt "elo0=0 elo1=3 alpha=0.05 beta=0.05" --openings releases/regression.fen
```

The `--openings` argument accepts either FEN or PGN files. Combine the SPRT
results with the gauntlet statistics to justify promoting the patch to fishtest
or tournament queues.

## 3. Reporting

Summarise the gauntlet outcomes and SPRT confidence bounds in the pull request
notes. Retain the PGNs so that follow-up tuning can reuse the same baselines.
