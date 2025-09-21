# Manual regression: quiescence PV entries are reusable

This scenario verifies that a principal variation discovered inside quiescence
search is written back to the transposition table with an exact bound.  The
regression prevented the PV from being reused on a subsequent visit, so the
second search would expand the full capture tree again even though the result
was already known.  The check below confirms that we now record quiescence
results with an exact bound whenever they improve the window without failing
high.

## Prerequisites

* Build the engine binary.  Either use the CMake/Ninja workflow
  (`cmake -S . -B build && cmake --build build`) or run the makefile in
  `src/` (for example `make -C src build ARCH=x86-64`).
* Ensure the hash table is small enough that it can be cleared quickly during
  the experiment (e.g. `setoption name Hash value 4`).

## Test procedure

1. Launch the freshly built binary (e.g. `./build/revolution`) and switch to
   UCI mode with `uci`, waiting for `uciok`.
2. Limit the search to a single thread and shrink the hash to ease inspection:
   ```
   setoption name Threads value 1
   setoption name Hash value 4
   setoption name Debug Log File value qsearch-pv.log
   ucinewgame
   ```
3. Reach a position that forces the main search to rely on quiescence capture
   lines.  One convenient choice is:
   ```
   position fen 8/8/8/8/4k3/8/3R4/4K3 w - - 0 1
   ```
   From here White must rely on the quiescence line `Rd4+ Ke5 2.Re4+ Kxe4` to
   hold the evaluation.
4. Run a shallow search to seed the table:
   ```
   go depth 1
   ```
   Note in the log that the PV reported for depth 1 includes the full capture
   sequence from quiescence search.
5. Without touching the hash table, repeat the exact same search:
   ```
   go depth 1
   ```
   Because the quiescence PV was stored with an exact bound, the second search
   should now hit the transposition table immediately.  The depth-1 line should
   appear almost instantly (typically within a millisecond) and the node count
   should stay at or below a handful of nodes.
6. Inspect `qsearch-pv.log` after quitting the engine.  Both depth-1 searches
   must report the same PV, and the second invocation should not re-expand the
   full capture tree—its timing and node count are minimal thanks to the exact
   quiescence entry.

## Expected result

* The first `go depth 1` call produces a PV containing the quiescence capture
  line.
* The second `go depth 1` reuses that PV straight from the table: the log shows
  the same PV reported immediately with a negligible node count.
* Clearing the hash (`setoption name Clear Hash`) removes the effect, proving
  that the speedy second search comes from the stored quiescence entry.
