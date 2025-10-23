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
