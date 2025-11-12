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
