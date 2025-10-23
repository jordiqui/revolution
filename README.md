# Revolution 2.90-231025

![Revolution minimal logo](assets/revolution-logo.svg)

## Overview
Revolution is a strong UCI chess engine derived from Stockfish 17.1. The 2.90-231025
release introduces targeted optimisations and tuning curated with help from codex IA
while preserving the battle-tested strength of its Stockfish heritage.

## Highlights
- Modern NNUE-evaluated engine based on the Stockfish 17.1 search framework.
- codex IA assisted optimisations for evaluation speed and practical play strength.
- Fully compatible with UCI graphical interfaces such as Fritz, CuteChess, Arena, and more.

## Book support
Revolution can play directly from Polyglot (`.bin`) and ChessBase (`.ctg/.cto/.ctb`) opening
books. Configure up to two books through the following UCI options:

- `CTG/BIN Book 1/2 File` – absolute or relative path to the first/second book. Use `<empty>`
  to disable a slot.
- `Book 1/2 Width` – number of top moves considered from each book entry. A value of `1` plays
  the best book move deterministically, higher values introduce weighted randomness.
- `Book 1/2 Depth` – maximum number of moves the engine will source from the book.
- `(CTG) Book 1/2 Only Green` – when enabled for CTG books, restricts play to green-recommended
  moves when available.

Issue the custom `book` command in the UCI console to list available book moves for the current
position. CTG support follows the publicly available reverse engineering notes and therefore
inherits their known limitations (no underpromotions, no more than two queens, imperfect
handling of green/red annotations, etc.).

Book parsing is based on the BrainLearn project – many thanks to **amchess** and Khalid Omar for
sharing their work.

## Experience learning support with Q-learning

Revolution 2.90 bundles the BrainLearn experience hash so it shares the same UCI options as
BrainFish while persisting the accumulated data to `experience.exp`. Each entry in the file mirrors
the in-memory BrainLearn transposition table and stores:

- the best move in UCI format,
- the board signature (hash key),
- the best move depth and score, and
- the best move performance derived from the BrainLearn WDL model (or a custom trainer-provided
  value).

During startup the engine automatically merges additional files that follow the
`<fileType><qualityIndex>.exp` naming convention (for example `experience0.exp`, `experience1.exp`,
…) into `experience.exp` and then deletes the merged inputs. Legacy `.bin` learning files can be
reused by renaming the extension to `.exp`.

Learning activates whenever a new game begins or the side to move has eight pieces or fewer.
Entries are updated when a new best score is found at depth ≥ 4 plies according to the BrainLearn
aspiration window. Because data is written on `quit` or `stop`, allow additional time for disk
access when learning is heavily used.

The following UCI controls are provided:

- **Read only learning** – open the experience file without persisting changes.
- **Self Q-learning** – enable the Q-learning update policy for self-play.
- **Experience Book** – treat the experience file as a move book that prioritises win probability,
  internal score, and depth (the `showexp` token lists stored moves for the current position).
- **Experience Book Max Moves** – limit the number of book candidates (default 100).
- **Experience Book Min Depth** – minimum search depth required for book use (default 4).
- **Concurrent Experience** – keep per-instance experience files to avoid write conflicts.
- **quickresetexp** – recompute the stored performance values with the latest BrainLearn WDL
  model.

When performance needs to be realigned, send the UCI token `quickresetexp`. The command reloads
`experience.exp`, recomputes the performance for each move, and persists the updated values for
future sessions.

Many thanks to **amchess** for sharing the BrainLearn experience learning infrastructure that makes
this integration possible.

## Version identity
The engine identifies itself in UCI GUIs as:

```
revolution-2.90-231025 by Jorge Ruiz Centelles and codex IA and the Stockfish developers (see AUTHORS file)
```

The same name is embedded in the executable so that analysis reports from popular GUIs
can attribute results to Revolution 2.90-231025.

## Getting started
Refer to the `README.stockfish.md` file for detailed build and usage instructions. The
commands and options described there apply equally to Revolution.

## Credits
- Jorge Ruiz Centelles and codex IA for the Revolution direction and optimisation work.
- The Stockfish developers (see `AUTHORS`) for the original engine code base and ongoing
  chess engine research.

## License
Revolution remains distributed under the GNU General Public License version 3 (or any
later version). See `Copying.txt` for the full license text.
