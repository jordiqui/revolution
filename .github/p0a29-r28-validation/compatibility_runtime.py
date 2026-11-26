#!/usr/bin/env python3
import argparse
import hashlib
import json
from pathlib import Path
import queue
import re
import struct
import subprocess
import threading
import time

import chess
import chess.polyglot
import chess.syzygy


MASK = (1 << 64) - 1
MIX_CONSTANT = 0x9E3779B97F4A7C15
MAX_WAIT = 20
TABLEBASE_FENS = {
    "kvk": "2k5/8/4K3/8/8/8/8/8 w - - 0 1",
    "kqk": "2k5/Q7/4K3/8/8/8/8/8 w - - 0 1",
    "kqrk": "2k5/Q7/4K3/8/8/R7/8/8 w - - 0 1",
    "rule50": "2k5/Q7/4K3/8/8/8/8/8 w - - 99 70",
}
FIVE_PIECE_TB = "2k5/8/4K3/8/8/R7/1R6/3R4 w - - 0 1"


def tablebase_cases(directory):
    cases = TABLEBASE_FENS.copy()
    if (directory / "KRRRvK.rtbw").is_file() and (directory / "KRRRvK.rtbz").is_file():
        cases["krrrk"] = FIVE_PIECE_TB
    return cases


def log(message):
    print(message, flush=True)


def independent_position_key(board):
    state = 1070372

    def rand64():
        nonlocal state
        state ^= state >> 12
        state ^= (state << 25) & MASK
        state ^= state >> 27
        return (state * 2685821657736338717) & MASK

    key = 0
    for color in (chess.WHITE, chess.BLACK):
        for piece in chess.PIECE_TYPES:
            occupied = board.pieces(piece, color)
            for square in chess.SQUARES:
                piece_key = rand64()
                if square in occupied:
                    key ^= piece_key
    ep_keys = [rand64() for _ in chess.FILE_NAMES]
    if board.has_legal_en_passant():
        key ^= ep_keys[chess.square_file(board.ep_square)]
    castling_keys = [rand64() for _ in range(16)]
    castle_rights = (
        board.has_kingside_castling_rights(chess.WHITE)
        + 2 * board.has_queenside_castling_rights(chess.WHITE)
        + 4 * board.has_kingside_castling_rights(chess.BLACK)
        + 8 * board.has_queenside_castling_rights(chess.BLACK)
    )
    key ^= castling_keys[castle_rights]
    black_to_move_key = rand64()
    if board.turn == chess.BLACK:
        key ^= black_to_move_key
    if board.halfmove_clock >= 14:
        index = (board.halfmove_clock - 14) // 8
        key ^= (6364136223846793005 * index + 1442695040888963407) & MASK
    return key


def independent_experience_key(board):
    key = independent_position_key(board)
    for value in (int(board.turn == chess.BLACK), board.ply(), board.halfmove_clock):
        z = (value + 2 * MIX_CONSTANT + (key << 6) + (key >> 2)) & MASK
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        key ^= z ^ (z >> 31)
    return key


def stockfish_move_code(move):
    assert move.promotion is None
    return (move.from_square << 6) + move.to_square


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_fixtures(directory, tablebases):
    directory.mkdir(parents=True, exist_ok=True)
    start = chess.Board()
    polyglot_key = chess.polyglot.zobrist_hash(start)
    assert polyglot_key == 0x463B96181691FC9C
    test_move = chess.Move.from_uci("a2a3")
    raw_move = stockfish_move_code(test_move)
    book = directory / "startpos.bin"
    book.write_bytes(struct.pack(">QHHI", polyglot_key, raw_move, 10, 0))
    with chess.polyglot.open_reader(book) as reader:
        entry = reader.find(start)
        assert entry.key == polyglot_key and entry.move == test_move and entry.weight == 10
    after_e4 = start.copy()
    after_e4.push_uci("e2e4")
    position_keys = {name: independent_position_key(board) for name, board in
                     (("startpos", start), ("e2e4", after_e4))}
    experience_keys = {name: independent_experience_key(board) for name, board in
                       (("startpos", start), ("e2e4", after_e4))}
    for version in (1, 2):
        legacy = directory / f"hypnos-v{version}.exp"
        raw = (struct.pack("<QIii4x", experience_keys["startpos"], raw_move, 99, 32)
               if version == 1 else
               struct.pack("<QIiiH2x", experience_keys["startpos"], raw_move, 99, 32, 7))
        legacy.write_bytes(f"SugaR Experience version {version}\n".encode() + raw)
    native = directory / "independent-native.exp"
    native.write_text(
        f"# Revolution experience format v1\n"
        f"{experience_keys['startpos']} {raw_move} 95 81 30 5\n"
        f"{experience_keys['e2e4']} {stockfish_move_code(chess.Move.from_uci('e7e5'))} 48 41 28 4\n"
    )
    snapshot = {"polyglot": {"startpos": f"{polyglot_key:016x}", "expected_move": "a2a3",
                             "sha256": sha256(book)},
                "position": {name: f"{key:016x}" for name, key in position_keys.items()},
                "experience": experience_keys}
    if tablebases:
        tablebase_values = {}
        with chess.syzygy.open_tablebase(tablebases) as oracle:
            for label, fen in tablebase_cases(tablebases).items():
                board = chess.Board(fen)
                tablebase_values[label] = [oracle.probe_wdl(board), oracle.probe_dtz(board),
                                           board.can_claim_fifty_moves()]
        snapshot["syzygy"] = tablebase_values
    log(json.dumps(snapshot, sort_keys=True))
    return snapshot


class Uci:
    def __init__(self, executable, cwd):
        self.lines = queue.Queue()
        self.process = subprocess.Popen(
            [str(executable)], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1
        )
        self.reader = threading.Thread(target=self._read, daemon=True)

    def _read(self):
        for line in self.process.stdout:
            self.lines.put(line.rstrip())
        self.lines.put(None)

    def __enter__(self):
        self.reader.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        if self.process.poll() is None:
            self.send("quit")
            try:
                self.process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
                if exc_type is None:
                    raise
        self.reader.join(timeout=1)
        if self.process.returncode != 0 and exc_type is None:
            raise RuntimeError(f"engine exited with {self.process.returncode}")

    def send(self, command):
        log(f"> {command}")
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def until(self, pattern):
        expression = re.compile(pattern)
        deadline = time.monotonic() + MAX_WAIT
        lines = []
        while True:
            try:
                line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty as exc:
                raise TimeoutError(f"waited {MAX_WAIT}s for {pattern}: {lines!r}") from exc
            if line is None:
                raise RuntimeError(f"engine exited waiting for {pattern}: {lines!r}")
            log(f"< {line}")
            lines.append(line)
            if expression.search(line):
                return lines

    def ready(self):
        self.send("isready")
        return self.until("^readyok$")

    def start(self, *, disable_experience=False):
        self.send("uci")
        lines = self.until("^uciok$")
        self.send("setoption name Threads value 1")
        self.send("setoption name Hash value 64")
        if disable_experience:
            self.send("setoption name Experience Enabled value false")
        self.ready()
        return lines

    def position_key(self, position):
        self.send(position)
        self.send("d")
        self.send("isready")
        lines = self.until("^readyok$")
        text = "\n".join(lines)
        return re.search(r"^Key: ([0-9A-Fa-f]{16})$", text, re.M)[1].lower(), lines

    def search(self, command, board):
        self.send(command)
        lines = self.until("^bestmove ")
        move = lines[-1].split()[1]
        assert chess.Move.from_uci(move) in board.legal_moves, lines[-1]
        return move, lines


def check_book(executable, cwd, fixtures):
    with Uci(executable, cwd) as uci:
        uci.start(disable_experience=True)
        uci.send(f"setoption name CTG/BIN Book 1 File value {fixtures / 'startpos.bin'}")
        uci.send("setoption name Book 1 Depth value 255")
        uci.ready()
        uci.send("position startpos")
        move, lines = uci.search("go depth 2", chess.Board())
        hit = any(line == "info string Book move: a2a3" for line in lines) and move == "a2a3"
        uci.send("poly")
        uci.send("isready")
        poly_lines = uci.until("^readyok$")
        uci.send("setoption name CTG/BIN Book 1 File value")
        uci.send(f"setoption name CTG/BIN Book 2 File value {fixtures / 'simple.ctg'}")
        uci.send("setoption name Book 2 Depth value 255")
        uci.ready()
        uci.send("position startpos")
        ctg_move, ctg_lines = uci.search("go depth 2", chess.Board())
        ctg_hit = any(line == f"info string Book move: {ctg_move}" for line in ctg_lines) and ctg_move in ("e2e4", "d2d4")
        uci.send(f"setoption name CTG/BIN Book 1 File value {fixtures / 'missing.bin'}")
        uci.send(f"setoption name CTG/BIN Book 2 File value {fixtures / 'missing.ctg'}")
        uci.send("setoption name Book 2 Depth value 255")
        uci.ready()
        uci.send("position startpos")
        fallback_move, fallback_lines = uci.search("go depth 2 searchmoves d2d4", chess.Board())
    fallback = fallback_move == "d2d4" and not any("Book move:" in line for line in fallback_lines)
    log(f"BOOK_VALID_BIN={'PASS' if hit else 'FAIL'}; BOOK_VALID_CTG={'PASS' if ctg_hit else 'FAIL'}; "
        f"BOOK_MISSING_FALLBACK={'PASS' if fallback else 'FAIL'}; "
        f"ENGINE_POLYGLOT_COMMAND={'ABSENT' if any('Unknown command' in line for line in poly_lines) else 'PRESENT'}")
    return hit and ctg_hit and fallback


def check_experience(executable, cwd, fixtures, snapshot):
    saved = cwd / "learned.exp"
    if saved.exists():
        raise RuntimeError(f"Refusing to overwrite {saved}")
    with Uci(executable, cwd) as uci:
        uci.start()
        uci.send(f"setoption name Experience File value {saved}")
        uci.send("setoption name Experience Book Min Depth value 4")
        uci.send("setoption name Experience Book value true")
        uci.ready()
        start, _ = uci.position_key("position startpos")
        after_e4, _ = uci.position_key("position startpos moves e2e4")
        assert start == snapshot["position"]["startpos"] and after_e4 == snapshot["position"]["e2e4"]
        uci.send("position startpos")
        move, _ = uci.search("go depth 5 searchmoves e2e4", chess.Board())
        assert move == "e2e4"
        uci.send("ucinewgame")
        uci.ready()
        records = [list(map(int, line.split())) for line in saved.read_text().splitlines()
                   if line and not line.startswith("#")]
        assert len(records) == 1, records
        key, raw_move, _, _, depth, visits = records[0]
        assert key == snapshot["experience"]["startpos"]
        assert raw_move == stockfish_move_code(chess.Move.from_uci("e2e4")) and depth >= 4 and visits == 1
    saved_before = sha256(saved)
    saved_stat = saved.stat()
    with Uci(executable, cwd) as uci:
        uci.start()
        uci.send("setoption name Experience Readonly value true")
        uci.send(f"setoption name Experience File value {saved}")
        uci.send("setoption name Experience Book Min Depth value 4")
        uci.send("setoption name Experience Book value true")
        uci.send("uci")
        lines = uci.until("^uciok$")
        assert any("Total moves: 1. Total positions: 1." in line for line in lines), lines
        uci.send("position startpos")
        move, _ = uci.search("go depth 5 searchmoves d2d4", chess.Board())
        assert move == "d2d4"
        uci.send("ucinewgame")
        uci.ready()
    assert saved_before == sha256(saved) and saved_stat.st_mtime_ns == saved.stat().st_mtime_ns
    with Uci(executable, cwd) as uci:
        uci.start()
        uci.send("setoption name Experience Readonly value true")
        for filename, count in (("hypnos-v1.exp", 1), ("hypnos-v2.exp", 1), ("independent-native.exp", 2)):
            fixture = fixtures / filename
            fingerprint = sha256(fixture)
            uci.send(f"setoption name Experience File value {fixture}")
            uci.send("uci")
            lines = uci.until("^uciok$")
            assert any(f"Total moves: {count}. Total positions: {count}." in line
                       for line in lines), lines
            assert fingerprint == sha256(fixture)
    log(f"EXPERIENCE_PERSISTENCE=PASS; READONLY=PASS; INDEPENDENT_NATIVE_HYPNOS_LOAD=PASS; "
        f"EXPERIENCE_STARTPOS_KEY={key}; LEARNING_RECORD={records[0]}")


def check_syzygy(executable, cwd, tablebases, snapshot):
    with Uci(executable, cwd) as uci:
        uci.start(disable_experience=True)
        uci.send(f"setoption name SyzygyPath value {tablebases}")
        uci.ready()
        for label, fen in tablebase_cases(tablebases).items():
            board = chess.Board(fen)
            oracle_wdl, oracle_dtz, oracle_rule50 = snapshot["syzygy"][label]
            _, lines = uci.position_key(f"position fen {fen}")
            text = "\n".join(lines)
            for name, expected in (("WDL", oracle_wdl), ("DTZ", oracle_dtz)):
                probe = re.search(rf"^Tablebases {name}:\s+(-?\d+) \(([-\d]+)\)", text, re.M)
                assert probe is not None and int(probe[1]) == expected and int(probe[2]) != 0, text
            move, search = uci.search("go depth 5", board)
            if oracle_rule50:
                assert any(re.search(r"\bscore cp 0\b", line) for line in search), search
            board.push_uci(move)
            if oracle_rule50:
                assert board.is_stalemate() or board.can_claim_fifty_moves()
            else:
                with chess.syzygy.open_tablebase(tablebases) as oracle:
                    assert -oracle.probe_wdl(board) == oracle_wdl
        uci.send("setoption name SyzygyPath value")
        uci.ready()
        fen = TABLEBASE_FENS["kqk"]
        _, fallback_lines = uci.position_key(f"position fen {fen}")
        assert not any("Tablebases WDL:" in line for line in fallback_lines), fallback_lines
        uci.search("go depth 5", chess.Board(fen))
    labels = ",".join(tablebase_cases(tablebases))
    log(f"SYZYGY_WDL_DTZ=PASS cases={labels}; LEGAL_SEARCH=PASS; RULE50=PASS; NO_TABLEBASE_FALLBACK=PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--fixture-dir", required=True, type=Path)
    parser.add_argument("--tablebase-dir", type=Path)
    parser.add_argument("--workspace", type=Path)
    parser.add_argument("--engine", type=Path)
    args = parser.parse_args()
    snapshot = make_fixtures(args.fixture_dir.resolve(), args.tablebase_dir)
    if args.engine is None:
        return
    if args.workspace is None:
        parser.error("--workspace is required with --engine")
    args.workspace.mkdir(parents=True, exist_ok=True)
    engine = args.engine.resolve()
    cwd = args.workspace.resolve()
    log(f"ENGINE={engine}; SHA256={sha256(engine)}; CWD={cwd}; COMMAND_TIMEOUT_S={MAX_WAIT}")
    failed = []
    tests = {
        "Book": lambda: check_book(engine, cwd, args.fixture_dir.resolve()),
        "Experience": lambda: check_experience(engine, cwd, args.fixture_dir.resolve(), snapshot),
    }
    if args.tablebase_dir is not None:
        tests["Syzygy"] = lambda: check_syzygy(engine, cwd, args.tablebase_dir.resolve(), snapshot)
    for test, run in tests.items():
        try:
            if run() is False:
                failed.append(test)
        except (AssertionError, OSError, RuntimeError, TimeoutError) as error:
            log(f"{test}=FAIL: {error}")
            failed.append(test)
    log(f"FAILED={','.join(failed) if failed else 'none'}")
    raise SystemExit(bool(failed))


if __name__ == "__main__":
    main()
