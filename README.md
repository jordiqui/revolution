# revolution-3.50-131125

Revolution UCI Chess Engines is a derivative of Stockfish that develops structural changes and explores new ideas to improve the project while complying with the GNU GPL v3 license. This release identifies itself as **revolution-3.50-131125 - UCI chess engine** developed by Jorge Ruiz and the Stockfish developers (see AUTHORS file).

## Overview

Revolution is a free and strong UCI chess engine derived from Stockfish that analyzes chess positions and computes optimal moves. The project focuses on experimenting with new ideas and structural changes while retaining compatibility with existing UCI-compliant graphical user interfaces (GUIs).

Like Stockfish, Revolution does not include a graphical user interface. To use the engine you need a UCI-compatible GUI such as Fritz, Arena, Cute Chess, or similar. Consult the documentation of your preferred GUI for instructions on how to load Revolution.

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
