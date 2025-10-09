#!/usr/bin/env python3
"""Utility script to run a lightweight Texel-style tuning session.

The script parses a PGN (or simple FEN list) and extracts positions together with
game results. For each position it recomputes the handcrafted evaluation terms
implemented in the engine and performs a logistic regression to obtain scaling
weights that best fit the observed results. The final weights are written to
stdout and can optionally be exported to JSON for further processing.

The implementation intentionally mirrors the heuristics used in the C++
evaluation so that the suggested weights can be plugged into
`Eval::ManualEvalWeights` without additional transformations.

Examples
--------

```
python3 scripts/run_texel.py --input recent_games.pgn --max-games 2000 \
    --output weights.json
```

Requirements
------------

The script relies on the `python-chess` package for PGN parsing and board
manipulation. Install it with `pip install python-chess` if it is not already
available in your environment.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from typing import Iterable, List, Sequence, Tuple

try:
    import chess
    import chess.pgn
except ImportError as exc:  # pragma: no cover - handled at runtime
    raise SystemExit(
        "python-chess is required to run Texel tuning. Install it with"
        " 'pip install python-chess' and retry."
    ) from exc


ResultValue = float


def relative_rank(color: chess.Color, square: chess.Square) -> int:
    rank = chess.square_rank(square)
    return rank if color == chess.WHITE else 7 - rank


def forward_span(color: chess.Color, square: chess.Square) -> List[chess.Square]:
    span: List[chess.Square] = []
    direction = 1 if color == chess.WHITE else -1
    rank = chess.square_rank(square) + direction

    while 0 <= rank <= 7:
        for file_offset in (-1, 0, 1):
            file = chess.square_file(square) + file_offset
            if 0 <= file <= 7:
                span.append(chess.square(file, rank))
        rank += direction

    return span


def is_passed_pawn(board: chess.Board, color: chess.Color, square: chess.Square) -> bool:
    enemy_pawns = board.pieces(chess.PAWN, not color)
    return all(target not in enemy_pawns for target in forward_span(color, square))


def king_shield_squares(color: chess.Color, square: chess.Square) -> List[chess.Square]:
    shield: List[chess.Square] = []
    direction = 1 if color == chess.WHITE else -1

    for step in (1, 2):
        rank = chess.square_rank(square) + direction * step
        if 0 <= rank <= 7:
            for df in (-1, 0, 1):
                file = chess.square_file(square) + df
                if 0 <= file <= 7:
                    shield.append(chess.square(file, rank))

    return shield


def king_safety_for(board: chess.Board, color: chess.Color) -> int:
    king_square = board.king(color)
    if king_square is None:
        return 0

    pawns = board.pieces(chess.PAWN, color)
    penalty = 0

    shield_count = sum(1 for sq in king_shield_squares(color, king_square) if sq in pawns)
    if shield_count < 2:
        penalty += (2 - shield_count) * 10

    adjacent_files = {
        chess.square_file(king_square) + offset
        for offset in (-1, 0, 1)
        if 0 <= chess.square_file(king_square) + offset <= 7
    }
    for pawn_sq in pawns:
        if chess.square_file(pawn_sq) not in adjacent_files:
            continue
        rel = relative_rank(color, pawn_sq)
        if rel > 3:
            penalty += 6 * (rel - 3)

    forward_file = [sq for sq in forward_span(color, king_square) if chess.square_file(sq) == chess.square_file(king_square)]
    if not any(sq in pawns for sq in forward_file):
        penalty += 18

    enemy_sliders = board.pieces(chess.ROOK, not color) | board.pieces(chess.QUEEN, not color)
    for slider in enemy_sliders:
        if chess.square_file(slider) != chess.square_file(king_square):
            continue
        between = chess.BB_BETWEEN[slider][king_square]
        if between and chess.popcount(between & board.occupied_co[color]) == 0:
            penalty += 12
            break

    flank_patterns = [
        (chess.parse_square("g4"), chess.parse_square("h4")),
        (chess.parse_square("g5"), chess.parse_square("h6")),
    ]
    for base_a, base_b in flank_patterns:
        sq_a = chess.square_mirror(base_a) if color == chess.BLACK else base_a
        sq_b = chess.square_mirror(base_b) if color == chess.BLACK else base_b
        if sq_a in pawns and sq_b in pawns:
            penalty += 14
            break

    king_ring = chess.SquareSet(chess.BB_KING_ATTACKS[king_square])
    attackers = sum(1 for sq in king_ring if board.piece_at(sq) and board.color_at(sq) != color)
    if attackers == 0 and penalty > 0:
        penalty = max(0, penalty - 6)

    return penalty


def passed_pawn_score(board: chess.Board, color: chess.Color) -> int:
    rank_bonus = [0, 0, 12, 28, 44, 68, 110, 0]
    score = 0

    for sq in board.pieces(chess.PAWN, color):
        if not is_passed_pawn(board, color, sq):
            continue

        rel_rank = relative_rank(color, sq)
        bonus = rank_bonus[rel_rank]
        block_sq = sq + (8 if color == chess.WHITE else -8)
        if chess.A1 <= block_sq <= chess.H8:
            attackers_us = len(board.attackers(color, block_sq))
            attackers_them = len(board.attackers(not color, block_sq))
            piece = board.piece_at(block_sq)
            if piece is None:
                bonus += 6 + 2 * rel_rank + max(attackers_us - attackers_them, 0) * 4
            elif piece.color == color:
                bonus += 4
            else:
                bonus -= 6
                diff = attackers_us - attackers_them
                bonus += diff * 5

            if attackers_us - attackers_them > 1:
                bonus += (attackers_us - attackers_them) * 3

        enemy_king = board.king(not color)
        if enemy_king is not None and rel_rank >= 5:
            if chess.square_distance(enemy_king, sq) > 3:
                bonus += 18

        score += bonus

    return score


def knight_mobility(board: chess.Board, color: chess.Color) -> int:
    central = {chess.parse_square(sq) for sq in ("d4", "e4", "d5", "e5")}
    score = 0
    for sq in board.pieces(chess.KNIGHT, color):
        moves = list(board.attacks(sq))
        mobility = sum(1 for dst in moves if not board.color_at(dst) == color)
        penalty = max(0, 3 - mobility) * 6
        if mobility <= 1:
            penalty += 10

        file_edge = chess.square_file(sq) in (0, 7)
        rank_edge = relative_rank(color, sq) in (0, 7)
        if file_edge or rank_edge:
            penalty += 6

        if sq in central:
            bonus = 14
            pawn_support = any(board.piece_at(dst) and board.piece_at(dst).piece_type == chess.PAWN and board.piece_at(dst).color == color for dst in board.attackers(color, sq))
            if pawn_support:
                bonus += 6
            if not board.attackers(not color, sq):
                bonus += 4
            penalty -= bonus

        score -= penalty

    return score


def pawn_endgame_feature(board: chess.Board) -> int:
    non_pawn = board.pieces(chess.BISHOP, chess.WHITE) | board.pieces(chess.BISHOP, chess.BLACK)
    non_pawn |= board.pieces(chess.KNIGHT, chess.WHITE) | board.pieces(chess.KNIGHT, chess.BLACK)
    non_pawn |= board.pieces(chess.ROOK, chess.WHITE) | board.pieces(chess.ROOK, chess.BLACK)
    non_pawn |= board.pieces(chess.QUEEN, chess.WHITE) | board.pieces(chess.QUEEN, chess.BLACK)
    if non_pawn:
        return 0

    king_w = board.king(chess.WHITE)
    king_b = board.king(chess.BLACK)
    if king_w is None or king_b is None:
        return 0

    def center_bonus(king: chess.Square) -> int:
        df = min(abs(chess.square_file(king) - 3), abs(chess.square_file(king) - 4))
        dr = min(abs(chess.square_rank(king) - 3), abs(chess.square_rank(king) - 4))
        return 18 - 6 * (df + dr)

    score = center_bonus(king_w) - center_bonus(king_b)

    for sq in board.pieces(chess.PAWN, chess.WHITE):
        if is_passed_pawn(board, chess.WHITE, sq):
            dist = chess.square_distance(king_w, sq)
            score += max(0, 6 - dist) * 4
            score -= chess.square_distance(king_b, sq) * 6

    for sq in board.pieces(chess.PAWN, chess.BLACK):
        if is_passed_pawn(board, chess.BLACK, sq):
            dist = chess.square_distance(king_b, sq)
            score -= max(0, 6 - dist) * 4
            score += chess.square_distance(king_w, sq) * 6

    same_file = chess.square_file(king_w) == chess.square_file(king_b)
    same_rank = chess.square_rank(king_w) == chess.square_rank(king_b)
    if (same_file or same_rank) and chess.square_distance(king_w, king_b) == 2:
        score += -12 if board.turn == chess.WHITE else 12

    return score


def feature_vector(board: chess.Board) -> Tuple[int, int, int, int]:
    king = king_safety_for(board, chess.WHITE) - king_safety_for(board, chess.BLACK)
    passed = passed_pawn_score(board, chess.WHITE) - passed_pawn_score(board, chess.BLACK)
    mobility = knight_mobility(board, chess.WHITE) - knight_mobility(board, chess.BLACK)
    endings = pawn_endgame_feature(board)
    return king, passed, mobility, endings


def orientation(board: chess.Board, features: Sequence[int]) -> Tuple[int, int, int, int]:
    if board.turn == chess.WHITE:
        return features
    return tuple(-value for value in features)


def result_for_side_to_move(board: chess.Board, game_result: str) -> ResultValue | None:
    if game_result not in ("1-0", "0-1", "1/2-1/2"):
        return None

    if game_result == "1/2-1/2":
        return 0.5

    white_won = game_result == "1-0"
    return 1.0 if (white_won and board.turn == chess.WHITE) or (not white_won and board.turn == chess.BLACK) else 0.0


def extract_positions(stream: Iterable[str], max_games: int | None) -> List[Tuple[Tuple[int, int, int, int], ResultValue]]:
    dataset: List[Tuple[Tuple[int, int, int, int], ResultValue]] = []
    games_parsed = 0

    while True:
        game = chess.pgn.read_game(stream)
        if game is None:
            break

        result = game.headers.get("Result", "*")
        board = game.board()
        for move in game.mainline_moves():
            value = result_for_side_to_move(board, result)
            if value is not None:
                feats = orientation(board, feature_vector(board))
                dataset.append((feats, value))
            board.push(move)

        games_parsed += 1
        if max_games is not None and games_parsed >= max_games:
            break

    return dataset


@dataclass
class TrainingConfig:
    temperature: float = 400.0
    learning_rate: float = 1e-4
    epochs: int = 200
    l2: float = 1e-6


def train_weights(dataset: Sequence[Tuple[Tuple[int, int, int, int], ResultValue]], config: TrainingConfig) -> List[float]:
    if not dataset:
        raise ValueError("Dataset is empty; cannot tune weights.")

    weights = [1.0, 1.0, 1.0, 1.0]
    eps = 1e-9

    for _ in range(config.epochs):
        grad = [0.0, 0.0, 0.0, 0.0]
        for features, result in dataset:
            evaluation = sum(w * f for w, f in zip(weights, features))
            scaled = max(-2000.0, min(2000.0, evaluation / config.temperature))
            probability = 1.0 / (1.0 + math.exp(-scaled))
            diff = probability - result
            for i, feature in enumerate(features):
                grad[i] += diff * feature / config.temperature

        for i in range(len(weights)):
            grad[i] = grad[i] / len(dataset) + config.l2 * weights[i]
            weights[i] -= config.learning_rate * grad[i]

    return weights


def main() -> None:
    parser = argparse.ArgumentParser(description="Run a Texel-style tuning session for manual eval terms.")
    parser.add_argument("--input", required=True, help="Path to a PGN file or a text file with FEN; the script currently expects PGN.")
    parser.add_argument("--max-games", type=int, default=None, help="Maximum number of games to process (useful for quick experiments).")
    parser.add_argument("--output", type=str, default=None, help="Optional path to write the resulting weights as JSON.")
    parser.add_argument("--temperature", type=float, default=400.0, help="Logistic scaling factor (Texel temperature in centipawns).")
    parser.add_argument("--learning-rate", type=float, default=1e-4, help="Gradient descent learning rate.")
    parser.add_argument("--epochs", type=int, default=200, help="Number of gradient descent iterations.")

    args = parser.parse_args()

    config = TrainingConfig(temperature=args.temperature, learning_rate=args.learning_rate, epochs=args.epochs)

    with open(args.input, "r", encoding="utf-8", errors="ignore") as handle:
        dataset = extract_positions(handle, args.max_games)

    if not dataset:
        raise SystemExit("No valid positions were extracted from the provided input.")

    weights = train_weights(dataset, config)
    scaled_weights = [int(round(w * 100)) for w in weights]

    summary = {
        "king_safety": scaled_weights[0],
        "passed_pawns": scaled_weights[1],
        "minor_mobility": scaled_weights[2],
        "pawn_endgames": scaled_weights[3],
        "samples": len(dataset),
        "temperature": config.temperature,
        "learning_rate": config.learning_rate,
        "epochs": config.epochs,
    }

    print("Suggested manual evaluation weights (percent scaling):")
    for key in ("king_safety", "passed_pawns", "minor_mobility", "pawn_endgames"):
        print(f"  {key.replace('_', ' ').title():<18}: {summary[key]:>4}")
    print(f"Processed positions: {summary['samples']}")

    if args.output:
        with open(args.output, "w", encoding="utf-8") as out:
            json.dump(summary, out, indent=2)


if __name__ == "__main__":  # pragma: no cover - entry point
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(1)
