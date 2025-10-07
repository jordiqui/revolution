#!/bin/bash
# Run automated matches between Revolution and another UCI engine using cutechess-cli.
# Usage: scripts/match.sh /path/to/opponent [games] [timecontrol] [--sprt "args"] [--openings file]

set -e
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_DEFAULT="$ROOT_DIR/src/revolution-2.81-071025"
ENGINE="${ENGINE:-$ENGINE_DEFAULT}"

if [[ $# -lt 1 ]]; then
  echo "Usage: scripts/match.sh /path/to/opponent [games] [timecontrol] [--sprt \"args\"] [--openings file]" >&2
  exit 1
fi

OPPONENT="$1"
shift

GAMES=10
if [[ $# -gt 0 && $1 != --* ]]; then
  GAMES="$1"
  shift
fi

TC="40/0.4+0.4"
if [[ $# -gt 0 && $1 != --* ]]; then
  TC="$1"
  shift
fi

OPENINGS_ARG=()
SPRT_ARG=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --sprt)
      [[ -n ${2:-} ]] || { echo "--sprt requires an argument" >&2; exit 1; }
      SPRT_ARG=(-sprt "$2")
      shift 2
      ;;
    --openings)
      [[ -n ${2:-} ]] || { echo "--openings requires a file" >&2; exit 1; }
      OPEN_FILE="$2"
      if [[ ! -f "$OPEN_FILE" ]]; then
        echo "Opening file $OPEN_FILE not found" >&2
        exit 1
      fi
      FORMAT="pgn"
      [[ "$OPEN_FILE" =~ \.fen$|\.FEN$ ]] && FORMAT="fens"
      PLIES_VALUE="${PLIES:-8}"
      OPENINGS_ARG=(-openings "file=$OPEN_FILE format=$FORMAT order=random plies=$PLIES_VALUE")
      shift 2
      ;;
    *)
      echo "Unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if ! command -v cutechess-cli >/dev/null; then
  echo "cutechess-cli is required but not installed"
  exit 1
fi

cutechess-cli \
  -engine cmd="$ENGINE" name="revolution-2.81-071025" \
  -engine cmd="$OPPONENT" name=Opponent \
  -each proto=uci tc=$TC \
  -games $GAMES -concurrency 2 \
  -pgn "$ROOT_DIR/match.pgn" \
  "${OPENINGS_ARG[@]}" \
  "${SPRT_ARG[@]}"
