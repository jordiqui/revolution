# Revolution Dev assets

Place the development build of the Revolution engine and its NNUE evaluation
network in this directory. The automation scripts look for these files:

- `revolution.exe` (or any executable specified via `REVOLUTION_BIN`)
- `nn-1c0000000000.nnue`

You can fetch the NNUE network automatically by running
`src\scripts\prepare_resources.bat`. Engine executables are not included in
this repository because of their size—copy the binary produced by the
Revolution project before starting a match.
