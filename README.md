# Revolution Chess Engine

**Version 2.42**  
This build identifies as `Revolution 2.42-190825`.

## Project Overview
Revolution is a free, open-source UCI chess engine derived from the world-class Stockfish project. Jorge Ruiz Centelles, working in tandem with ChatGPT Codex tooling, extends the Stockfish codebase to explore modern C++ refinements, improved maintainability, and experimental search ideas while preserving the GPLv3 heritage of the original engine. Revolution ships without a graphical interface and communicates through the UCI protocol, making it compatible with popular desktop chess GUIs that support external engines.

## Latest Updates
- Synchronized with the Stockfish mainline search and evaluation improvements up to August 2025.
- Expanded NNUE integration to support multiple network profiles, including the experimental Falcon network.
- Added faster startup routines for Syzygy tablebases through optional pre-mapping.
- Refined experience learning heuristics that bias move ordering based on past games.

## Key Features
- **Hybrid Evaluation** – Combines classical heuristics with NNUE-style neural networks.
- **Advanced Search** – Implements YBWC parallelism, late move reductions, reverse futility pruning, and selective extensions.
- **Experience Book** – Maintains a lightweight opening and root-move biasing cache governed by UCI options.
- **Experimental MCTS Mode** – Optional Monte Carlo search inspired by Shashin positional classifications.
- **Extensive UCI Control** – Rich option set for tablebases, network selection, and tuning parameters.

## Architecture Highlights
- Modernized C++17 patterns for clarity, safety, and maintainability.
- Clean separation between search, evaluation, and NNUE components in `src/`.
- Optional experience files (`experience.exp`) and neural network weights (`revolution.nnue`, `nn-c01dc0ffeede.nnue`).

## Building from Source
Revolution reuses the well-established Stockfish build system. Typical workflow:

```bash
cd src
make -j ARCH=x86-64-modern
```

Supported `ARCH` targets include `x86-64`, `x86-64-modern`, `armv8`, and `ppc64`. Consult `docs/` for platform-specific notes and advanced configuration tips.

## Testing & Quality Assurance
Each change set is validated with rigorous testing methodologies:

- **Perft Benchmarks** – Verify bitboard move generation accuracy.
- **SPRT Matches** – Measure Elo impact against reference builds using sequential probability ratio testing.
- **Regression Suites** – Continuous matches on OpenBench-style infrastructure to ensure stability across time controls.

Testing evidence drives promotion, refinement, or rollback decisions to keep the engine competitive and reliable.

## Usage
Load Revolution into any UCI-compatible GUI:

```text
uci
setoption name Minimum Thinking Time value 200
setoption name SyzygyPath value /path/to/tablebases
setoption name FalconFile value nn-c01dc0ffeede.nnue
isready
ucinewgame
position startpos
go movetime 5000
```

Syzygy tablebases are automatically probed when `SyzygyPath` is set. Experience learning can be toggled via `Experience Enabled`, while `Experience Book` turns the cache into a soft opening book.

## License & Credits
Revolution is distributed under the [GNU General Public License v3](https://www.gnu.org/licenses/gpl-3.0.en.html). Because the engine incorporates Stockfish code, any redistribution must ship complete corresponding source, retain GPL notices, and document modifications.

**Authors and Credits:**
- Stockfish developers and contributors – creators of the original engine and ongoing upstream innovations.
- Jorge Ruiz Centelles – maintainer of Revolution, curating experimental features and release packaging.
- ChatGPT Codex tooling – assisted code generation, documentation, and experimentation support.

Community discussions mirror Stockfish norms, spanning the Chess Programming Wiki forums, Discord groups, and GitHub discussions. Contributions are welcomed when accompanied by reproducible tests and adherence to clean C++17 practices.
