# Match orchestration scripts

This directory contains Windows batch utilities that automate the setup and
execution of Revolution engine tournaments against Wordfish using the UHO
opening suite.

## Available scripts

- `prepare_resources.bat` downloads the Revolution Dev NNUE network and the
  UHO 40/60 v4 opening suite into the repository layout expected by the match
  runner. Run this script after cloning the repository or whenever the
  resources are missing.
- `run_uho_match.bat` launches a `cutechess-cli` match between Revolution Dev
  and Wordfish. The script relies on the binaries that are already provided by
  the Revolution and Wordfish repositories and never downloads engine
  executables itself.

Both scripts assume that this repository and the Wordfish repository share the
same parent directory. If your setup differs, override the paths by defining
these environment variables before invoking `run_uho_match.bat`:

- `REVOLUTION_BIN` – Full path to the Revolution binary to test.
- `WORDFISH_BIN` – Full path to the Wordfish binary.
- `CUTECHESS` – Location of `cutechess-cli.exe` if it is not in `PATH`.

Additional knobs:

- `GAMES` – Number of games to play (defaults to 20).
- `CONCURRENCY` – Number of concurrent games (defaults to 4).
- `TIME_CONTROL` – Time control in `cutechess-cli` format (defaults to `180+2`).
- `THREADS` – Threads passed to each engine (defaults to 4).

### Download locations

`prepare_resources.bat` fetches:

- Revolution Dev NNUE: <https://tests.stockfishchess.org/api/nn/nn-1c0000000000.nnue>
- UHO 40/60 v4 suite: <https://github.com/official-stockfish/books/blob/master/UHO_4060_v4.epd.zip>

If the download fails (for example, because PowerShell is not available),
download the files manually and place them in these directories:

- `src/engines/revolution dev/nn-1c0000000000.nnue`
- `src/openings/pgn/UHO_4060_v4.epd`

### Generated artefacts

Match PGNs are stored under `src/games/`. Engine binaries (`*.exe`, `*.bin`)
and large resource files should not be committed to version control and are
listed in local `.gitignore` files within their respective directories.
