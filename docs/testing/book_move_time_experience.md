# Manual regression: book move preserves time/experience state

This scenario verifies the regression fixed here that caused the engine to
reset its accumulated timing information and to write a bogus experience entry
when the current move came straight from a book.  The test checks that
triggering a book move still produces the move instantly while keeping the
time-management heuristics and the experience table exactly as they were after
the previous search.

## Prerequisites

* Build the engine binary.  Either use the CMake/Ninja workflow
  (`cmake -S . -B build && cmake --build build`) or run the makefile in
  `src/` (for example `make -C src build ARCH=x86-64`).
* Pick a writable location for a temporary experience file (e.g.
  `/tmp/book-regression.exp`).

## Test procedure

1. Launch the freshly built binary (e.g. `./build/revolution` or
   `./src/revolution_2.70_210925`) and switch to UCI mode by sending `uci` and
   waiting for `uciok`.
2. Enable the logger so that the session can be inspected afterwards:
   `setoption name Debug Log File value book-regression.log`.
3. Point the engine to the temporary experience file and make it writable:
   ```
   setoption name Experience Enabled value true
   setoption name Experience Readonly value false
   setoption name Experience File value /tmp/book-regression.exp
   ```
4. Disable both books for a single baseline search and start a new game:
   ```
   setoption name Book1 value false
   setoption name Book2 value false
   ucinewgame
   position startpos
   ```
5. Run a timed search to populate the time manager and the experience table,
   e.g. `go wtime 120000 btime 120000`.  Note from the log how much time was
   spent and which score was reported for the best line.
6. Without quitting the engine, re-enable the first book so the next move comes
   from it: `setoption name Book1 value true`.
7. Ask the engine to analyse the position that is known to be inside the book:
   ```
   position startpos moves e2e4
   showexp
   ```
   The `showexp` command should print `info string No experience available` for
   that position before the move is played.
8. Trigger the book move with `go depth 20`.  The engine must reply almost
   instantly with `bestmove e7e5` (or another book move, depending on the book)
   and the log must not contain any `info depth ...` search output, confirming
   that no search took place.
9. Immediately call `showexp` again.  The output should still be
   `info string No experience available`, proving that the book move did not
   create or update an entry in the experience table.
10. Finally, request another real search from a non-book position, for example:
    ```
    position startpos moves e2e4 e7e5 g1f3
    go wtime 120000 btime 120000
    ```
    Compare the thinking time reported in the log with the baseline search from
    step&nbsp;5.  The engine should use a comparable amount of time and the
    reported score should start from the same aspiration window, showing that
    the time-management state from the last genuine search has been preserved.

11. Stop the engine with `quit` and remove the temporary experience file and
    log after verifying the behaviour.

## Expected result

* The book move is still played immediately without any search output.
* The experience table remains unchanged for the book position.
* The very next genuine search reuses the previous timing/evaluation context
  instead of resetting to defaults.

