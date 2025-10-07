#!/bin/bash
# Run a focused gauntlet from a list of FEN positions using cutechess-cli.
# Usage: scripts/focused_gauntlet.sh fen_list opponent [games] [timecontrol]

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_DEFAULT="$ROOT_DIR/src/revolution-2.81-071025"
ENGINE="${ENGINE:-$ENGINE_DEFAULT}"

FEN_LIST="${1:?FEN list required}"
OPPONENT="${2:?Opponent engine path required}"
GAMES="${3:-20}"
TC="${4:-5+0.05}"
PLIES_VALUE="${PLIES:-8}"

if [[ ! -f "$FEN_LIST" ]]; then
  echo "FEN list $FEN_LIST not found" >&2
  exit 1
fi

if ! command -v cutechess-cli >/dev/null; then
  echo "cutechess-cli is required but not installed" >&2
  exit 1
fi

cutechess-cli \
  -engine cmd="$ENGINE" name="revolution-2.81-071025" \
  -engine cmd="$OPPONENT" name=Opponent \
  -each proto=uci tc=$TC \
  -openings "file=$FEN_LIST format=fens order=random plies=$PLIES_VALUE" \
  -games $GAMES -concurrency 2 \
  -pgn "$ROOT_DIR/focused-gauntlet.pgn"
