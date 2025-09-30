#!/bin/bash
# Run automated matches between Revolution and another UCI engine using cutechess-cli.
# Usage: scripts/match.sh /path/to/opponent [games] [timecontrol]

set -e
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_DEFAULT="$ROOT_DIR/src/revolution 2.80 300925"
ENGINE="${ENGINE:-$ENGINE_DEFAULT}"
OPPONENT="${1:?Opponent engine path required}"
GAMES="${2:-10}"
TC="${3:-40/0.4+0.4}"

if ! command -v cutechess-cli >/dev/null; then
  echo "cutechess-cli is required but not installed"
  exit 1
fi

cutechess-cli \
  -engine cmd="$ENGINE" name="revolution 2.80 300925" \
  -engine cmd="$OPPONENT" name=Opponent \
  -each proto=uci tc=$TC \
  -games $GAMES -concurrency 2 \
  -pgn "$ROOT_DIR/match.pgn"
