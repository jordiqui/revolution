# DEV vs BASE (October 2025) Match Review

## Match Overview
- **Configuration:** 10+0.1 fast chess, 1 thread, 64 MB hash, UHO_Lichess_4852_v1.epd opening suite
- **Sample size:** 4,000 games (2,026 points for DEV vs 1,974 for BASE)
- **Observed Elo gain:** +4.52 (±5.39), normalized Elo +9.02 (±10.77)
- **Likelihood of superiority (LOS):** 94.97 %
- **Draw ratio:** 55.5 %
- **Ptnml:** [14, 410, 1110, 442, 24]

The DEV build retains a small but statistically meaningful edge over BASE at the tested time control, but the log-likelihood ratio (LLR = 0.64) has not yet crossed the usual 2.94 accept boundary. Additional games or parameter tuning are recommended before declaring the gain production-ready.

## Qualitative Review of DEV Losses
A focused look at several decisive losses uncovers recurring themes that can guide further tuning:

### 1. Passive king safety and slow counterplay (GameId 2230315267678274)
- Early ...h6 combined with castling queenside allowed BASE to open the g-file with long-term pressure.
- DEV underestimated White's central expansion (f4–f5) and delayed counterplay, leading to a cramped position and material losses in the late middlegame.
- **Potential root cause:** Evaluation function appears to overvalue the bishop pair/structure while undervaluing chronic king exposure and space deficits.

### 2. Over-optimistic exchange sacrifices without compensation (GameId 2230315267678279)
- After ...Re4 and ...Nxf2, DEV gave up material banking on piece activity, but the follow-up was insufficient and White consolidated.
- The engine continued to prefer activity over material even as the attack fizzled, indicating a horizon or evaluation bias toward initiative over concrete defense resources.

### 3. Difficulty neutralizing flank pawn storms (GameId 2230315267678283)
- BASE's kingside pawn storm (h4–h5–g4) and queenside play (b5 break) went largely unchallenged.
- DEV reacted with piece maneuvers rather than pawn breaks (e.g., ...f6/f5 or ...c5 earlier), resulting in long-term weaknesses and time pressure defense.

### 4. Conversion issues in favorable endgames (GameId 2230315289546720)
- As White, DEV achieved a strategically winning ending (extra passed c- and b-pawns) but mismanaged the king walk and coordination, letting BASE's counterplay queen pawns and mate.
- Evaluation did not sufficiently penalize king exposure plus opposite-side passed pawns, potentially due to insufficient king safety heuristics in late stages.

### 5. Time spent on speculative kingside attacks (GameIds 2230315289419730, 2230315289419728, 2230315289358271)
- Multiple losses show DEV launching speculative kingside play (e.g., g4, sacrifices on f2/f7) without completing development or securing its own king.
- BASE punished by consolidating material advantage and converting in long technical wins, suggesting search favors aggressive lines even when material deficit looms.

## Proposed Follow-up Tasks
To solidify the gain and address the above weaknesses, consider the following prioritized tasks:

1. **Rebalance evaluation terms for king safety vs. activity:**
   - Audit weights on king-ring attacks, pawn shelter, and open files. Recent losses show DEV underestimating long-term king danger after pawn pushes.
   - Add or adjust penalties for voluntarily weakening the king (e.g., early ...h6/...g5) when the opposing pieces are poised to attack.

2. **Review exchange sacrifice heuristics:**
   - Introduce additional search pruning or verification when giving up material for initiative, especially when the opponent retains a safe king.
   - Compare SEE (static exchange evaluation) thresholds between DEV and BASE to ensure speculative sacrifices are filtered earlier.

3. **Enhance handling of flank pawn storms and space deficits:**
   - Experiment with evaluation bonuses for counter-pawn breaks or penalties for allowing advanced enemy rook pawns uncontested.
   - Add search extensions or multi-cut triggers for pawn storms approaching the king to ensure deeper defensive lines are explored.

4. **Endgame king safety heuristics:**
   - Ensure the evaluation keeps tracking mating nets even when up material; integrate distance-to-promotion vs. king exposure trade-offs into late endgame scoring.
   - Compare tablebase usage thresholds or implement specialized endgame pruning for opposite-colored bishop or rook endgames with passed pawns.

5. **Data-driven regression testing:**
   - Run focused gauntlets starting from the cited loss positions with minor move variations to confirm whether evaluation tweaks improve defensive choices.
   - Extend SPRT with additional openings/time controls (e.g., 5+0.05 blitz and 15+0.1 rapid) to verify the gain generalizes.

6. **Instrumentation for speculative attacks:**
   - Log statistics on exchange sacrifices (count, success rate) and pawn storms launched by DEV vs. BASE during future matches.
   - Use this telemetry to tune move ordering or pruning strategies that currently over-favor aggressive but unsound continuations.

By iterating on these tasks, we can aim to convert the tentative Elo gain into a robust, statistically significant improvement across diverse positions.
