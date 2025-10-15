# Revolution 2.81-071025 vs 2.80 SPRT Regression Review

## Test Outcome
- **Match**: DEV (Revolution 2.81-071025) vs BASE (Revolution 2.80), 10+0.1, 1 thread, 64 MB, `UHO_Lichess_4852_v1.epd`.
- **Score**: 1315/2732 (48.13%). Elo -12.98 ± 7.12, nElo -23.76 ± 13.03.
- **SPRT**: Boundaries [0, 5]; LLR = -2.96 → H₀ accepted (regression confirmed).
- **Drawing rate**: 48.61%; decisive games highlighted recurring structural problems rather than random blunders.

## Loss Pattern Highlights
The sample of decisive BASE wins supplied with the match log exposes three recurring themes that line up with the score drop.

### 1. Kingside collapses after premature pawn storms
- In several French/Indian type structures (e.g., Game IDs 2233210052942007, 2233210053007554), DEV advanced the g- and h-pawns only to leave the king stuck on g8. Once White played `g4`/`h4`, the follow-up `hxg5`, `gxf6`, or sacrifices on `h7` broke the cover and the resulting rook lifts (`Ra7`, `Rf7`) forced mates.
- The current `kingside_overextension_penalty()` only fires if both home pawns are pushed deeply and the king remains castled short.【F:src/evaluate.cpp†L196-L258】 Many of the supplied games reach exactly this configuration, yet the penalty is modest (10–25 cp) and does not account for open files created by captures (`...hxg5` opening the h-file).

### 2. Underestimation of outside passers and rook activity without queens
- In long queenless fights (Game IDs 2233210053085382, 2233210053093576, 2233210067157811) BASE consistently pushed outside passers (`a`, `h` pawns) while DEV traded into positions where those passers were supported by rooks from behind. DEV allowed sequences like `cxb3`, `a4–a5`, `Rb4` even when both of its rooks were passive.
- The existing passed-pawn helpers (`forward_passed_mask`, `rook_on_ray`, `connected_passed_neighbor`) do not currently award extra credit for rooks behind passers nor do they penalize the defending side when no rook contests the file.【F:src/evaluate.cpp†L261-L325】 This leads to evaluations that remain optimistic until the passer is already unstoppable.

### 3. Time use and defensive search depth under pressure
- Multiple losses show DEV drifting into zugzwang-like scenarios without seeking counterplay (e.g., Game IDs 2233210053126352, 2233210067420001). Moves such as `...Re8`/`...Rf8` were repeated while the opponent improved freely, hinting at late-move reductions cutting off necessary defensive lines.
- `TimeManagement::init()` scales the "Slow Mover" option by global statistics only (average move budget, total time) and never reacts to tactical urgency or king danger.【F:src/timeman.cpp†L118-L198】 With high pressure on the king, DEV often burned time earlier in the middlegame and entered critical defenses with <10 seconds, amplifying search instability.

## Recommended Tasks
The following items target the three problem areas and can be scheduled for short A/B tests and deeper tuning campaigns.

### Evaluation Adjustments
1. **Strengthen kingside overextension detection**
   - Augment `kingside_overextension_penalty()` to account for open files: add a check for missing pawns on files g/h combined with enemy rook/queen control of those files, and scale the penalty when `defendedFront == 0` (currently a flat +5).【F:src/evaluate.cpp†L196-L248】
   - Gate the penalty on the number of friendly minor pieces defending key dark squares (f6, g7) so that trades like `...Bxf6` trigger an immediate warning instead of waiting for the pawn push count alone.

2. **Reward rooks behind passers / penalize passive defenders**
   - Extend `rook_on_ray()` and `connected_passed_neighbor()` usage so that a rook aligned with a friendly passer adds a bonus, while the defending side gets an explicit malus if no rook appears on the ray opposing the passer.【F:src/evaluate.cpp†L285-L325】
   - Couple this with a search extension or evaluation bonus for advanced outside passers (ranks 5+ with no opposing pawn in the `forward_passed_mask`) to avoid the slow collapse seen in the sample games.【F:src/evaluate.cpp†L261-L275】

3. **Introduce dark-square coverage scoring linked to style indicators**
   - `compute_indicators()` already counts attackers/defenders around the kings and central occupancy.【F:src/evaluate.cpp†L122-L180】 Use these indicators to add a small penalty when the side lacking the dark-squared bishop also has a low `shield` count; this directly targets games where DEV ceded dark-square control after trades (e.g., long maneuvers culminating in `Nd6`, `Ne5`).

### Search and Time Management
1. **Urgency-aware time scaling**
   - Hook a lightweight urgency metric (e.g., maximum enemy passer advancement or king danger from `StyleIndicators`) into `TimeManagement::init()` so that `slowMover` and `moveOverhead` are adjusted upward only when the position is stable.【F:src/evaluate.cpp†L122-L180】【F:src/timeman.cpp†L118-L198】 This should preserve time for late defensive tasks observed in the sample.

2. **Late-move reduction guardrails in defensive nodes**
   - Audit LMR triggers around the defensive scenarios highlighted above. Introduce a guard condition that disables the deepest reductions when `shield` is low or when the best static eval drops sharply move-to-move, preventing the engine from missing slow improving plans in holdable endings.【F:src/evaluate.cpp†L122-L180】【F:src/search.cpp†L1098-L1173】

3. **Endgame pruning review for rook vs passer races**
   - Review pruning/forward pruning near the horizon when only rooks and passers remain. Allow a one-ply extension (or remove a pruning rule) when an outside passer reaches rank 5 with rook support to ensure the engine sees the promotion race rather than assuming fortress stability.【F:src/evaluate.cpp†L261-L325】【F:src/search.cpp†L1098-L1173】

### Testing & Tooling
1. **Replay the provided loss set as a focused regression suite** to verify that the above heuristics address the failure modes before launching a full SPRT.
2. **Tighten SPRT guard bands**: for short TC regressions consider shrinking the lower boundary (e.g., [-1, 5]) so similar -13 Elo drops trigger earlier, reducing wasted compute.
3. **Add time-management stress tests** (e.g., 5+0.05) to the nightly battery so we can see whether urgency-aware scaling preserves defensive resources or causes new issues.

## Next Steps
- Prioritize the kingside overextension and passer-handling tweaks; both can be prototyped with localized evaluation edits and quick gauntlets.
- Run a verifying A/B at 10+0.1 (3k games) once evaluation changes are in place, followed by a shorter 5+0.05 match to measure the time-management adjustments.
- If the score gap closes (< -5 Elo with overlapping confidence), resume the main 2.81 training branch with updated heuristics and re-run SPRT against 2.80.

