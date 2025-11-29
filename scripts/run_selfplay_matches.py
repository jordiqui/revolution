#!/usr/bin/env python3
"""Run self-play matches with Stockfish and export PGN + performance metrics.

The script drives a UCI engine directly and produces:
- A fishtest-compatible PGN log of the played games.
- JSON and CSV artifacts with aggregate metrics (average nps, evaluation drift,
  and transposition table hit approximation).
"""
from __future__ import annotations

import argparse
import csv
import json
import pathlib
import subprocess
import sys
import time
from typing import Dict, Iterable, List, Optional, Tuple

import chess
import chess.pgn


ENGINE_READY = "readyok"
UCI_OK = "uciok"


class EngineController:
    def __init__(self, binary: str, threads: int, hash_size: int):
        self.process = subprocess.Popen(
            [binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1
        )
        if not self.process.stdin or not self.process.stdout:
            raise RuntimeError("Failed to open engine pipes")
        self._send("uci")
        self._wait_for(UCI_OK)
        if threads:
            self.set_option("Threads", threads)
        if hash_size:
            self.set_option("Hash", hash_size)
        self._send("isready")
        self._wait_for(ENGINE_READY)

    def _send(self, cmd: str) -> None:
        assert self.process.stdin
        self.process.stdin.write(cmd + "\n")
        self.process.stdin.flush()

    def _read_line(self) -> str:
        assert self.process.stdout
        return self.process.stdout.readline().strip()

    def _wait_for(self, keyword: str) -> None:
        while True:
            line = self._read_line()
            if not line:
                continue
            if keyword in line:
                return

    def set_option(self, name: str, value: object) -> None:
        self._send(f"setoption name {name} value {value}")

    def new_game(self) -> None:
        self._send("ucinewgame")
        self._send("isready")
        self._wait_for(ENGINE_READY)

    def bestmove(self, board: chess.Board, go_arguments: str) -> Tuple[chess.Move, Dict[str, float]]:
        self._send(f"position fen {board.fen()}")
        self._send(f"go {go_arguments}")
        info_lines: List[str] = []
        while True:
            line = self._read_line()
            if not line:
                continue
            if line.startswith("info "):
                info_lines.append(line)
            elif line.startswith("bestmove"):
                parts = line.split()
                if len(parts) < 2:
                    raise RuntimeError(f"Unexpected bestmove line: {line}")
                move = chess.Move.from_uci(parts[1])
                if move not in board.legal_moves:
                    raise RuntimeError(f"Engine produced illegal move {move} for {board.fen()}")
                return move, parse_info(info_lines)

    def close(self) -> None:
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.process.kill()


def parse_info(info_lines: Iterable[str]) -> Dict[str, float]:
    stats: Dict[str, float] = {}
    for line in info_lines:
        tokens = line.split()
        for idx, token in enumerate(tokens):
            if token == "nps" and idx + 1 < len(tokens):
                stats["nps"] = float(tokens[idx + 1])
            elif token == "nodes" and idx + 1 < len(tokens):
                stats.setdefault("nodes", 0.0)
                stats["nodes"] = float(tokens[idx + 1])
            elif token == "tthits" and idx + 1 < len(tokens):
                stats.setdefault("tthits", 0.0)
                stats["tthits"] = float(tokens[idx + 1])
            elif token == "hashfull" and idx + 1 < len(tokens):
                stats.setdefault("hashfull", 0.0)
                stats["hashfull"] = float(tokens[idx + 1])
            elif token == "score" and idx + 2 < len(tokens):
                score_type = tokens[idx + 1]
                value = tokens[idx + 2]
                try:
                    if score_type == "cp":
                        stats["eval_cp"] = float(value)
                    elif score_type == "mate":
                        stats["eval_cp"] = 32000.0 if float(value) > 0 else -32000.0
                except ValueError:
                    continue
    return stats


def build_pgn(game_index: int, board: chess.Board, headers: Dict[str, str]) -> str:
    game = chess.pgn.Game()
    for key, value in headers.items():
        game.headers[key] = value
    game.headers["Round"] = str(game_index + 1)
    game.headers["Result"] = board.result(claim_draw=True)
    node = game
    replay = chess.Board()
    for move in board.move_stack:
        node = node.add_variation(move)
        replay.push(move)
    return str(game)


def play_game(
    engine: EngineController,
    game_index: int,
    go_arguments: str,
    headers: Dict[str, str],
    max_plies: int,
) -> Tuple[Dict[str, float], str]:
    board = chess.Board()
    engine.new_game()
    per_move_nps: List[float] = []
    eval_drift: List[float] = []
    nodes_total = 0.0
    tt_hits = 0.0
    hashfull_samples: List[float] = []
    previous_eval: Optional[float] = None
    while not board.is_game_over(claim_draw=True) and len(board.move_stack) < max_plies:
        move, stats = engine.bestmove(board, go_arguments)
        board.push(move)
        if "nps" in stats:
            per_move_nps.append(stats["nps"])
        if "eval_cp" in stats:
            if previous_eval is not None:
                eval_drift.append(abs(stats["eval_cp"] - previous_eval))
            previous_eval = stats["eval_cp"]
        if "nodes" in stats:
            nodes_total += stats["nodes"]
        if "tthits" in stats:
            tt_hits += stats["tthits"]
        if "hashfull" in stats:
            hashfull_samples.append(stats["hashfull"])
        if board.is_game_over(claim_draw=True):
            break
    pgn_text = build_pgn(game_index, board, headers)
    avg_nps = sum(per_move_nps) / len(per_move_nps) if per_move_nps else 0.0
    avg_eval_drift = sum(eval_drift) / len(eval_drift) if eval_drift else 0.0
    if nodes_total > 0 and tt_hits > 0:
        tt_hit_rate = tt_hits / nodes_total
    elif hashfull_samples:
        tt_hit_rate = (sum(hashfull_samples) / len(hashfull_samples)) / 1000.0
    else:
        tt_hit_rate = 0.0
    game_metrics = {
        "game": game_index + 1,
        "plies": len(board.move_stack),
        "result": board.result(claim_draw=True),
        "avg_nps": round(avg_nps, 2),
        "avg_eval_drift_cp": round(avg_eval_drift, 2),
        "tt_hit_rate": round(tt_hit_rate, 4),
    }
    return game_metrics, pgn_text


def write_artifacts(
    output_dir: pathlib.Path, metrics: List[Dict[str, float]], pgns: List[str]
) -> Tuple[pathlib.Path, pathlib.Path, pathlib.Path]:
    output_dir.mkdir(parents=True, exist_ok=True)
    pgn_path = output_dir / "selfplay_games.pgn"
    json_path = output_dir / "selfplay_metrics.json"
    csv_path = output_dir / "selfplay_metrics.csv"

    with pgn_path.open("w", encoding="utf-8") as pgn_file:
        for pgn in pgns:
            pgn_file.write(pgn)
            if not pgn.endswith("\n\n"):
                pgn_file.write("\n\n")

    with json_path.open("w", encoding="utf-8") as jf:
        payload = {
            "generated_at": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "games": metrics,
            "aggregate": {
                "games": len(metrics),
                "avg_nps": round(sum(m["avg_nps"] for m in metrics) / len(metrics), 2)
                if metrics
                else 0.0,
                "avg_eval_drift_cp": round(
                    sum(m["avg_eval_drift_cp"] for m in metrics) / len(metrics), 2
                )
                if metrics
                else 0.0,
                "avg_tt_hit_rate": round(
                    sum(m["tt_hit_rate"] for m in metrics) / len(metrics), 4
                )
                if metrics
                else 0.0,
            },
        }
        json.dump(payload, jf, indent=2)

    with csv_path.open("w", newline="", encoding="utf-8") as cf:
        fieldnames = ["game", "plies", "result", "avg_nps", "avg_eval_drift_cp", "tt_hit_rate"]
        writer = csv.DictWriter(cf, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(metrics)

    return pgn_path, json_path, csv_path


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", default="./stockfish", help="Path to the UCI engine binary")
    parser.add_argument("--games", type=int, default=2, help="Number of games to self-play")
    parser.add_argument("--movetime", type=int, default=500, help="Move time for go command (ms)")
    parser.add_argument("--depth", type=int, default=None, help="Depth for go command (overrides movetime if set)")
    parser.add_argument("--threads", type=int, default=1, help="Threads to pass via UCI option")
    parser.add_argument("--hash", dest="hash_size", type=int, default=16, help="Hash size (MB) for the engine")
    parser.add_argument("--output-dir", default="artifacts", help="Directory to store PGN and metrics")
    parser.add_argument("--max-plies", type=int, default=300, help="Maximum plies before declaring a draw")
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> int:
    args = parse_args(argv)
    output_dir = pathlib.Path(args.output_dir)
    go_parts: List[str] = []
    if args.depth:
        go_parts.extend(["depth", str(args.depth)])
    if args.movetime and not args.depth:
        go_parts.extend(["movetime", str(args.movetime)])
    go_arguments = " ".join(go_parts) if go_parts else "movetime 500"

    engine = EngineController(args.engine, args.threads, args.hash_size)
    metrics: List[Dict[str, float]] = []
    pgns: List[str] = []
    headers = {
        "Event": "fishtest-selfplay",
        "Site": "local",
        "White": pathlib.Path(args.engine).name,
        "Black": pathlib.Path(args.engine).name,
        "Date": time.strftime("%Y.%m.%d", time.gmtime()),
        "TimeControl": go_arguments.replace(" ", "_"),
    }
    try:
        for game_index in range(args.games):
            game_metrics, pgn_text = play_game(
                engine, game_index, go_arguments, headers, args.max_plies
            )
            metrics.append(game_metrics)
            pgns.append(pgn_text)
            print(
                f"Game {game_metrics['game']} finished: {game_metrics['result']} | "
                f"avg nps {game_metrics['avg_nps']}, eval drift {game_metrics['avg_eval_drift_cp']} cp"
            )
    finally:
        engine.close()

    pgn_path, json_path, csv_path = write_artifacts(output_dir, metrics, pgns)
    print(f"Saved PGN to {pgn_path}")
    print(f"Saved metrics to {json_path} and {csv_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
