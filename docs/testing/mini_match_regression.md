# Mini-match regression test

The CI pipeline runs a scripted mini-match to make sure the current build
performs on par with a known-good reference binary.  The harness lives in
`tests/testing.py` (`TestNNUEThreadSafety.test_mini_match_against_reference_as_black`).
It fixes the build under test to play Black, plays a short series of fast games
against the reference (Stockfish 16 by default in CI), and verifies that the
score stays within a small tolerance of 50 %.

## Running the test locally

1. Build the engine that you want to test (for example `make -C src ARCH=x86-64-avx2 build`).
2. Download or compile a reference binary and point the test to it via the
   `STOCKFISH_REFERENCE_BINARY` environment variable.
3. Run the test:

   ```bash
   python3 -m unittest tests.testing.TestNNUEThreadSafety.test_mini_match_against_reference_as_black
   ```

The runner understands the following optional environment variables:

| Variable | Meaning | Default |
| --- | --- | --- |
| `STOCKFISH_MINIMATCH_GAMES` | Number of games in the mini-match. | `4` |
| `STOCKFISH_MINIMATCH_MOVETIME` | Move time per side in milliseconds. | `40` |
| `STOCKFISH_MINIMATCH_MAX_PLY` | Maximum plies per game before it is declared a draw. | `80` |
| `STOCKFISH_MINIMATCH_TOLERANCE` | Accepted deviation from 50 % score for the build under test. | `1.5` |

## Updating the tolerance

The tolerance guards against natural variance when playing a small number of
fast games.  When the baseline strength changes (for example when swapping to a
new reference binary), adjust `STOCKFISH_MINIMATCH_TOLERANCE` in the CI
workflow to a value that comfortably covers the expected swing in the
mini-match score.  Document the new value in commit messages/PR descriptions so
future updates can reason about the change.
