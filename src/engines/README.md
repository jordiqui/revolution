# Engine binaries

The subdirectories in this folder organise the different Revolution engine
variants that are paired against external engines in scripted matches.

- `revolution dev/` – Drop the development build of Revolution here together
  with its NNUE network. The automation scripts expect the NNUE file
  `nn-1c0000000000.nnue` to live inside this directory.
- `revolution base/` – Store the baseline Revolution binary used for internal
  comparisons.

Binaries are intentionally excluded from version control. Copy the existing
executables from the official Revolution and Wordfish repositories into these
folders before running a match.
