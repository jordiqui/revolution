# DEV vs BASE Regression Review (10 October 2025)

## Test Summary
- **Configuration:** 10+0.1 fast chess, 1 thread, 64 MB hash, UHO_Lichess_4852_v1.epd suite.
- **Sample:** 280 games (131.0 / 280 points, 46.79 % for DEV).
- **SPRT outcome:** Elo -22.4 ±23.9 (normalized -38.2 ±40.7), LOS 3.29 %, draw ratio 42.9 %, pairs ratio 0.63.
- **Conclusion:** Current DEV build underperforms BASE; the negative log-likelihood ratio (-0.22) is consistent with a failing regression at this confidence window.

codex/analyze-test-results-for-revolution-2.81-6u9a23
## Ajustes sugeridos para el control 10+0.1 en gauntlets locales

El control de 10 segundos iniciales + 0.1 segundos de incremento deja un presupuesto medio de ~0.35 s por jugada (suponiendo 40 jugadas restantes). Con los valores por defecto la gestión de tiempo acaba usando solo ~0.32 s porque:

* `Slow Mover = 100` se convierte internamente en un factor de ~0.91 tras las correcciones automáticas para controles rápidos.【F:src/engine.cpp†L112-L118】【F:src/timeman.cpp†L118-L141】
* `Move Overhead = 10` ms se infla a ~12 ms en este control, dejando menos tiempo real en cada jugada.【F:src/timeman.cpp†L118-L141】
* `Minimum Thinking Time = 20` ms apenas impone una barrera y permite decisiones con décimas de segundo cuando el reloj baja.【F:src/engine.cpp†L112-L118】【F:src/timeman.cpp†L250-L261】

Para los gauntlets locales (sin latencia de red) recomendamos los siguientes valores, que suben el consumo efectivo en ~20 % y reducen las jugadas relámpago que vimos en las partidas:

| Opción UCI                  | Valor sugerido | Motivo |
|-----------------------------|----------------|--------|
| `Move Overhead`             | **12 ms**      | Mantiene un colchón mínimo frente al coste de E/S local pero evita recortes adicionales sobre un presupuesto ya ajustado.【F:src/timeman.cpp†L118-L141】 |
| `Slow Mover`                | **120**        | Eleva el factor efectivo a ~1.09 en este control, lo que añade ~70 ms a la jugada media y da margen para tácticas críticas.【F:src/timeman.cpp†L118-L211】 |
| `Minimum Thinking Time`     | **60 ms**      | Garantiza que incluso con poco tiempo se inviertan al menos 0.06 s por jugada, estabilizando la calidad bajo presión.【F:src/timeman.cpp†L118-L261】 |

Aplicación práctica: al lanzar el SPRT local añade `setoption name Move Overhead value 12`, `setoption name Slow Mover value 120` y `setoption name Minimum Thinking Time value 60` antes de cada `go`. Si usas scripts, incluye estas órdenes en la inicialización del motor.

=======
 main
## Key Regression Themes

### 1. Time management underuse in sharp middlegames
DEV repeatedly spent well under a second on pivotal decisions where the evaluation still claimed an advantage. In GameId 2231050739195955, moves such as **25...Qc4?!** (0.82 s) and **30...Nxb4?!** (0.19 s) let BASE activate the queenside majority and swap into a won endgame despite an engine score near -1.2 a few plies earlier. Likewise, in GameId 2231050739257402, the sequence **22...Nxe4 23.axb4 Qxa3** consumed only ~1.0 s combined and left DEV with a technically winning position that later collapsed. The lack of time investment hints that the time-manager is not ramping depth when the search reports non-trivial tactical swings, allowing shallow blunders to slip through.

*Suggested actions:* increase the minimum time-per-move budget in non-forced positions, add "panic time" triggers when the root evaluation changes by >0.4 in consecutive iterations, and re-tune move horizon factors so that long games (>100 plies) still retain a usable main time reserve.

### 2. Horizon effects and shallow verification in material imbalances
In multiple losses DEV accepted lines where immediate material gains masked long-term structural issues. During GameId 2231050739257400 (moves 33–47) the engine entered an equal rook ending with doubled pawns, but failed to foresee BASE's far-advanced passed g-pawn; evaluations remained near -1.0 until the pawn queened. Similarly, GameId 2231050739322948 saw **24...Rh3? 25.Rxh6 Bxe4** and later **33...c6** leading to an endgame where the search ignored White's d-pawn break (56.d7+) until promotion. The shallow verification suggests null-move or futility pruning is trimming the search before confirming the opponent's forcing resources.

*Suggested actions:* tighten SEE/razoring thresholds when ahead material, enable additional quiescence extensions for advanced passers (rank-6/7), and add verification search depth for exchange sac lines that open files against the king.

### 3. Endgame conversion and king safety heuristics
Several games reached clearly winning evaluations (≤ -5.0) only for DEV to allow perpetual threats or direct mates. In GameId 2231050739257402, the converted extra material after **65...f6 66.Ra7** still evaluated around -9.0, but the engine misplayed the king walk (**83...Kf7??**, **86...f7??**) and got mated. GameId 2231050739322944 mirrored this behavior: a two-pawn advantage became a mating net after **82...f5?? 83.f6!** when the king stepped into checks. These collapses point to insufficient penalties for king exposure in simplified positions and missing knowledge about unstoppable passed pawns.

*Suggested actions:* audit endgame king-safety scoring, add scale factors for opposite-wing passers, integrate distance-to-promotion races into the evaluation, and ensure tablebase adjudication (or at least a probe-like heuristic) guides move selection in K+R vs K endings with advanced pawns.

### 4. Tactical blunders in defended positions
The sample also shows outright tactics missed despite ample material parity. GameId 2231050739322946 featured **70...Rd3??** allowing *Bxf4* and a connected pawn avalanche; the evaluation plunged from -2.3 to -3.1 within a move. In GameId 2231050739388488, **55...Re3?** overlooked White's f-pawn advance leading to **67.Kf5!** and forced mate. Such single-move collapses, often with search depths around 12–15 plies (see engine annotations), indicate either inaccurate pruning or late-move reductions that downplay defending resources.

*Suggested actions:* reduce late-move reduction aggressiveness when the side to move is defending, add tactical node counters to trigger search extensions after capture sequences fail, and re-check history-countermove heuristics so critical defensive moves are not over-reduced.

## Recommended Next Steps
1. **Run instrumentation gauntlets** that log per-move time allocation and root depth to confirm whether the manager reacts to evaluation volatility.
2. **Disable aggressive pruning heuristics** (LMP, history-based reductions) in a short gauntlet to see if the tactical oversights disappear, then reintroduce with retuned coefficients.
3. **Construct endgame regression suites** from the cited positions (move 60 of GameIds 2231050739257402 and 2231050739322944) to benchmark conversion quality after evaluation tweaks.
4. **Extend SPRT testing** at a slower control (15+0.1) to determine if the regression narrows when the search has more time; this will separate time-mgmt issues from pure evaluation bugs.
