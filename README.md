# revolution-3.50-131125

<p align="center">
  <img src="assets/revolution-logo.svg" alt="Revolution UCI Chess Engine logo featuring a minimalist French tricolor cockade" width="360" />
</p>

Revolution UCI Chess Engines is a derivative of Stockfish that develops structural changes and explores new ideas to improve the project while complying with the GNU GPL v3 license. This release identifies itself as **revolution-3.50-131125 - UCI chess engine** developed by Jorge Ruiz and the Stockfish developers (see AUTHORS file).

## Overview

Revolution is a free and strong UCI chess engine derived from Stockfish that analyzes chess positions and computes optimal moves. The project focuses on experimenting with new ideas and structural changes while retaining compatibility with existing UCI-compliant graphical user interfaces (GUIs).

Like Stockfish, Revolution does not include a graphical user interface. To use the engine you need a UCI-compatible GUI such as Fritz, Arena, Cute Chess, or similar. Consult the documentation of your preferred GUI for instructions on how to load Revolution.

## BrainLearn experience file integration

Revolution integrates the BrainLearn persistent hash learning system so that the engine can reuse knowledge collected from previous games. The learning data is saved in an `experience.exp` file that stores one or more positions using the following BrainLearn-defined structure:

1. best move
2. board signature (hash key)
3. best move depth
4. best move score
5. best move performance (by default derived from score and depth, and adjustable through learning tools that support this specification)

The `experience.exp` file is loaded into an internal hash table when the engine starts and is updated whenever BrainLearn receives a `quit` or `stop` UCI command. Learning is active from the start of each game and whenever eight or fewer pieces remain on the board. Positions whose best score is found at a depth of at least four plies—following the BrainLearn aspiration window—are written back to the experience file.

At load time Revolution automatically merges every file named according to the convention `<fileType><qualityIndex>.exp`, where `fileType` is `experience` and `qualityIndex` is an integer beginning at 0 (0 represents the best quality as assigned by the user). Legacy `.bin` experience files can be used by renaming them with the `.exp` extension, which frees the `.bin` extension for standard opening books.

### Learning-related UCI options

- **Read only learning** (boolean, default `false`): prevents the engine from updating the experience file.
- **Self Q-learning** (boolean, default `false`): switches the learning algorithm to the BrainLearn Q-learning mode that is optimised for self-play scenarios.
- **Experience Book** (boolean, default `false`): allows the engine to consult the experience file as an opening book. Moves are chosen by prioritising maximum win probability, then internal score, and finally search depth. The `showexp` UCI token can be used to list available moves in the current position.
- **Experience Book Max Moves** (integer, default `100`, minimum `1`, maximum `100`): limits the number of candidate moves read from the experience book.
- **Experience Book Min Depth** (integer, default `4`, minimum `1`, maximum `255`): sets the minimum stored depth for moves to qualify for book selection.
- **quickresetexp** (UCI token): aligns the stored performance values with the defaults when the learning file needs to be reset quickly.

For further documentation and community resources, visit the BrainLearn project page: <https://github.com/amchess/BrainLearn>. Revolution thanks the BrainLearn authors for making the experience-file integration possible.

## Book management

Revolution implements a flexible book management system inspired by Polyfish and BrainLearn, following the loading priority **BIN → CTG → live book**. Two book slots are available so that you can combine different opening sources or keep fallback material handy when your primary book runs out of moves.

### CTG/BIN Book 1 File

The filename of the first book, which can be either a Polyglot (`.bin`) or Chessbase (`.ctg`) book. To disable this book leave the option empty. If the book resides outside the engine directory, specify the full path; for example `C:\Path\To\My\Book.ctg` or `/home/username/path/to/book.bin`.

### Book 1 Width

Sets how many moves are considered from the book in the current position. Use `1` to always pick the top move. When the value is greater than `1`, the engine randomly chooses between the top **n** moves listed in the book entry.

### Book 1 Depth

Defines the maximum number of plies played from the book before switching to search.

### (CTG) Book 1 Only Green

Only applies to `.ctg` books. When `true`, Revolution restricts choices to book moves marked as Green. If no such move exists in the position, the engine falls back to regular search. This option has no effect when the selected book is Polyglot (`.bin`).

### CTG/BIN Book 2 File

Second book slot with the same behaviour as **Book 1 File**. Use it to layer a wider or specialised book after the main one.

### Book 2 Width

Same behaviour as **Book 1 Width**, but for the second book slot.

### Book 2 Depth

Same behaviour as **Book 1 Depth**, but for the second book slot.

### (CTG) Book 2 Only Green

Same behaviour as **(CTG) Book 1 Only Green**, but for the second book slot.

### Book-related UCI commands

Revolution supports every UCI command provided by BrainLearn; the full list is available in the upstream documentation. In addition, the following commands expose book functionality:

- `book`: prints all moves available in the currently configured books along with statistics.
- `position startpos` followed by `poly`: displays Polyglot-style book information for the starting position (or the position you supply).

Example output from the starting position:

```
book
position startpos
poly

 +---+---+---+---+---+---+---+---+
 | r | n | b | q | k | b | n | r | 8
 +---+---+---+---+---+---+---+---+
 | p | p | p | p | p | p | p | p | 7
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 6
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 5
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 4
 +---+---+---+---+---+---+---+---+
 |   |   |   |   |   |   |   |   | 3
 +---+---+---+---+---+---+---+---+
 | P | P | P | P | P | P | P | P | 2
 +---+---+---+---+---+---+---+---+
 | R | N | B | Q | K | B | N | R | 1
 +---+---+---+---+---+---+---+---+
   a   b   c   d   e   f   g   h

Fen: rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1
Key: 8F8F01D4562F59FB
Checkers:

Polyglot book 1: MyNarrowBook.bin
1 : e2e4 , count: 8822
2 : d2d4 , count: 6644

Polyglot book 2: MyWideBook.bin
1 : e2e4 , count: 9768
2 : d2d4 , count: 5347
3 : g1f3 , count: 1034
4 : c2c4 , count: 965
5 : b2b3 , count: 99
6 : f2f4 , count: 94
7 : g2g3 , count: 76
8 : b2b4 , count: 43
9 : e2e3 , count: 32
10: b1c3 , count: 32
11: d2d3 , count: 13
12: c2c3 , count: 12
13: a2a3 , count: 10
14: g2g4 , count: 9
15: h2h3 , count: 3
16: h2h4 , count: 3
17: a2a4 , count: 1
18: g1h3 , count: 1
19: b1a3 , count: 1
20: f2f3 , count: 1
```

### Notes about CTG books

Chessbase does not publish official specifications for CTG books. Revolution therefore relies on community reverse engineering, primarily the documentation available at <https://github.com/amchess/BrainLearn>. The current implementation can probe CTG books for moves, but it is not identical to Chessbase products. Known limitations include:

- Underpromotion is unsupported; every promotion defaults to a queen.
- Positions with more than two queens are unsupported.
- Green/Red classification is not fully accurate, so some green moves might be skipped.
- Certain move annotations and engine recommendations can be read, but not all.
- Move weights are reconstructed heuristically from available statistics (wins, draws, losses, annotations, and recommendations).

Despite these limitations, the engine selects the best move in the vast majority of cases and offers a practical way to reuse existing CTG material. Use CTG support at your discretion.

## Files

This distribution of Revolution consists of the following files:

- `README.md`: the file you are currently reading.
- `Copying.txt`: the GNU General Public License version 3.
- `AUTHORS`: the list of authors for the project.
- `src`: the full source code, including a Makefile for Unix-like systems.
- `.nnue` network files that provide the neural network evaluation (embedded in binary builds).

## Compiling

Revolution supports 32-bit and 64-bit CPUs and the same hardware instruction sets as Stockfish. On Unix-like systems you can compile the engine from the `src` directory with:

```
cd src
make -j profile-build
```

Run `make help` inside `src` to see all available build targets and configuration options.

## Contributing

Contributions follow the Stockfish development process. Please review `CONTRIBUTING.md` for guidelines and use the public testing infrastructure (Fishtest) when applicable.

## License

Revolution is free software distributed under the **GNU General Public License version 3**. When redistributing Revolution you must include the license and the complete corresponding source code for the binaries you provide. Changes based on this code must also remain under the GPL v3.

Revolution acknowledges the Stockfish project and all of its contributors. The engine remains compatible with the original Stockfish license requirements and credits the Stockfish developers in the AUTHORS file.
