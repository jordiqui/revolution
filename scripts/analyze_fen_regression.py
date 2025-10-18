#!/usr/bin/env python3
"""Small helper to summarize problematic regression games from FEN traces."""
from __future__ import annotations

import argparse
from collections import Counter
from pathlib import Path
from typing import Iterable, List, Tuple


def load_fens(path: Path) -> List[str]:
    text = path.read_text().strip().splitlines()
    return [line.strip() for line in text if line.strip()]


def compute_consecutive_streaks(fens: Iterable[str]) -> List[Tuple[str, int]]:
    streaks: List[Tuple[str, int]] = []
    current_fen = None
    current_len = 0
    for fen in fens:
        if fen != current_fen:
            if current_fen is not None:
                streaks.append((current_fen, current_len))
            current_fen = fen
            current_len = 1
        else:
            current_len += 1
    if current_fen is not None:
        streaks.append((current_fen, current_len))
    return streaks


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "path",
        type=Path,
        nargs="?",
        default=Path("docs/regression/rev281_regression_fens.txt"),
        help="Path to the newline separated FEN list",
    )
    args = parser.parse_args()

    fens = load_fens(args.path)
    total = len(fens)
    counter = Counter(fens)
    streaks = compute_consecutive_streaks(fens)
    max_streak_fen, max_streak_len = max(streaks, key=lambda item: item[1])

    print(f"Loaded {total} positions from {args.path}")
    print(f"Unique FENs: {len(counter)}")
    print("Most frequent positions:")
    for fen, freq in counter.most_common(5):
        print(f"  x{freq:<3} {fen}")

    print("\nLongest consecutive repetition:")
    print(f"  x{max_streak_len} {max_streak_fen}")

    repeated = [s for s in streaks if s[1] > 1]
    print(f"\nNumber of repeated streaks: {len(repeated)}")
    if repeated:
        avg_repeat = sum(length for _, length in repeated) / len(repeated)
        print(f"Average length of non-trivial streaks: {avg_repeat:.2f}")

    early_repeat = next(((fen, length, idx) for idx, (fen, length) in enumerate(streaks) if length > 2), None)
    if early_repeat:
        fen, length, idx = early_repeat
        prefix_positions = sum(length for _, length in streaks[:idx])
        print(
            f"\nFirst problematic repetition longer than 2 occurs after {prefix_positions} moves:"
            f" streak x{length} at index {idx}"
        )
        print(f"  {fen}")


if __name__ == "__main__":
    main()
