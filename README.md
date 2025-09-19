# Revolution Chess Engine

**Version 2.42**

This build identifies as `Revolution 2.42-190825`.

## Credits and Authors

- **Original Engine:** Stockfish Project Team and Contributors – creators and maintainers of the foundational open-source engine.
- **Derivative Work:** Jorge Ruiz Centelles, with implementation assistance and code generation support from ChatGPT's Codex models, exploring modern C++ refinements and experimental ideas on top of Stockfish.

## Overview

**Revolution** is a free, open-source UCI chess engine derived from **Stockfish**. Jorge Ruiz Centelles, supported by ChatGPT Codex tooling, adapts the engine to prototype new approaches while modernizing C++ syntax and structure. The project aims to investigate search innovations, refined evaluation pipelines, and maintainability improvements without diverging from Stockfish's GPLv3 heritage.

Revolution integrates state-of-the-art search algorithms with neural network evaluation techniques. As a UCI-compliant engine, it runs without a built-in graphical interface and communicates with GUIs such as Arena, Scid vs. PC, Cute Chess, and other compatible tools.

## Technical Architecture

Revolution currently explores the following architectural components:

- Hybrid evaluation combining classical heuristics with NNUE-style neural networks.
- SMP parallelization via the Young Brothers Wait Concept (YBWC).
- Advanced pruning techniques including reverse futility pruning, late move reductions, and selective extensions.
- Modernized C++17 code patterns to improve readability, safety, and maintainability.
- Optional experience book support that records root move statistics for future searches.

## Files

The repository includes:

- `README.md` – project overview (this document).
- `COPYING.txt` – the GNU GPLv3 license text.
- `AUTHORS` – acknowledgements for Stockfish developers and additional contributors.
- `src/` – C++ source code and build scripts.
- Neural network weights (`revolution.nnue` or compatible NNUE files).

## Development and Testing

### Collaborative Workflow

Development decisions emerge from an iterative dialogue between Jorge Ruiz Centelles and ChatGPT assistance. Proposed changes are discussed, implemented, and reviewed collaboratively to ensure alignment with project goals.

### Testing Strategy

Every change is validated with rigorous testing:

- **Perft benchmarks** verify move generation accuracy.
- **SPRT (Sequential Probability Ratio Test)** sessions measure Elo impact against reference builds.
- **Regression matches** on OpenBench-style frameworks assess stability and performance across various time controls.

Evaluation results determine whether a modification is promoted, refined, or reverted, maintaining a data-driven development cycle.

## Community and Contribution Guidelines

Although Revolution is experimental, contributions should respect the standards established by the Stockfish community:

- Follow clean, well-documented C++17 code practices.
- Ensure compatibility with the UCI protocol and existing build systems.
- Provide reproducible testing evidence (perft logs, SPRT statistics, match data) with each change proposal.

Communication channels mirror those used by Stockfish contributors—Chess Programming forums, Discord communities, and GitHub discussions are ideal venues for sharing insights and feedback.

## Compilation

Compile Revolution from source using the provided Makefiles:

```bash
cd src
make -j ARCH=x86-64-modern
```

Supported architectures include:

- `x86-64` – modern x86 processors.
- `armv8` – ARMv8+ CPUs.
- `ppc64` – PowerPC systems.

See the documentation in `docs/` for additional build configurations and platform-specific tips.

## Syzygy Tablebases

Revolution can probe [Syzygy](https://github.com/syzygy1) endgame tablebases when a directory is supplied via the `SyzygyPath` UCI option. The engine also provides a `SyzygyPremap` boolean option. When enabled, `Tablebases::init` pre-maps available WDL and DTZ tables during initialization, reducing probe latency at the cost of extra startup time and memory.

## Experience Learning

Revolution includes a text-based cache that stores root moves and evaluations from previous games. Instead of forcing book moves, the stored information biases root move ordering during search. The following UCI options control the system:

- `Experience Enabled` – toggles the experience feature (default `true`).
- `Experience File` – filename for storing experience data (default `experience.exp`; legacy `.bin` files are converted automatically).
- `Experience Readonly` – prevents updates when set to `true`.
- `Experience Prior` – biases root move ordering using stored experience.
- `Experience Width` – number of principal moves considered (1–20).
- `Experience Eval Weight` – weighting of evaluation when ordering moves (0–10).
- `Experience Min Depth` – minimum depth required to store a move (4–64).
- `Experience Max Moves` – maximum moves saved per position (1–100).
- `Experience Book` – uses the experience file as an opening book when `true`.
- `Experience Book Max Moves` – move limit for opening book usage (1–100, default 100).
- `Experience Book Min Depth` – minimum depth for book moves (1–255, default 4).

The cache loads at startup and updates after each game if `Experience Readonly` is disabled.

## Monte Carlo Tree Search (Experimental)

Revolution exposes experimental Monte Carlo Tree Search options inspired by Shashin's positional classifications. For configuration details, refer to [`docs/mcts.md`](docs/mcts.md).

## UCI Options

### Minimum Thinking Time

The `Minimum Thinking Time` option ensures the engine spends at least a specified number of milliseconds searching for a move, even if a satisfactory move is found immediately. This avoids extremely fast, low-quality replies.

```
setoption name Minimum Thinking Time value <milliseconds>
```

### Falcon Net

Revolution can switch to an alternative neural network using the `FalconFile` option. If an `nn-c01dc0ffeede.nnue` file is present in the engine directory it will be embedded automatically; otherwise, the engine falls back to standard networks. To load the alternative network, send:

```
setoption name FalconFile value nn-c01dc0ffeede.nnue
```

## License

Revolution is distributed under the **[GNU General Public License v3][gpl-link]** (GPLv3). It integrates source code from:

- [Stockfish](https://github.com/official-stockfish/Stockfish)

Because Stockfish is GPLv3, any distribution of Revolution must also comply with GPLv3. When redistributing, you must:

1. Include the original license text (`COPYING.txt`).
2. Provide complete corresponding source code.
3. Disclose all modifications under GPLv3.

For a summary of GPLv3 obligations, visit <https://www.gnu.org/licenses/quick-guide-gplv3.html>.

## Acknowledgements

Revolution benefits from:

- Neural networks trained on the [Lichess open database][lichess-db].
- Testing infrastructure inspired by the [Computer Chess Club (CCC)][ccc-link] community.
- Research shared on the [Chess Programming Wiki][chesswiki-link].

## Useful Links

- [Project Documentation][doc-link]
- [OpenBench Testing Framework][openbench-link]
- [Revolution Test Worker][worker-link]
- [Revolution Test Suite][testsuite-link]
- [Community Discord][discord-link]
- [GitHub Discussions][discussions-link]

<!-- Reference Links -->
[gpl-link]: https://www.gnu.org/licenses/gpl-3.0.en.html
[openbench-link]: https://github.com/official-stockfish/Stockfish/wiki/Benchmarks
[worker-link]: https://github.com/official-stockfish/Stockfish/tree/master/worker
[testsuite-link]: https://github.com/official-stockfish/Stockfish/wiki/Testing
[discord-link]: https://discord.gg/stockfish
[discussions-link]: https://github.com/official-stockfish/Stockfish/discussions
[chesswiki-link]: https://www.chessprogramming.org
[lichess-db]: https://database.lichess.org/
[doc-link]: docs/
