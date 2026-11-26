#!/usr/bin/env python3
"""Phase A black-box tests for exact, prebuilt Universal executable(s)."""

import argparse
from contextlib import contextmanager
import hashlib
import json
from pathlib import Path
import queue
import re
import subprocess
import sys
import tempfile
import threading
import time
import traceback

sys.path.insert(0, str(Path(__file__).resolve().parent / 'vendor'))
import chess


IDENTITY = 'Revolution-6.40-150926'
NET = 'nn-d1ee674a62ca.nnue'
TIMEOUT = 25
EXPECTED_OPTIONS = {
    'CTG/BIN Book 1 File', 'CTG/BIN Book 2 File', 'Experience Enabled',
    'Experience File', 'Experience Readonly', 'Experience Book', 'EvalFile',
    'SyzygyPath', 'Hash', 'Threads', 'Skill Level', 'MultiPV', 'UCI_Chess960',
    'Ponder', 'nodestime',
}
KIWIPETE = 'r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1'
CASTLE = 'r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1'
CASTLE_960 = '4k3/8/8/8/8/8/8/1R1K2R1 w GB - 0 1'
EP = '4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 1'
EP_PINNED = 'k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1'
EP_DISCOVERED = '8/8/8/r4pPK/8/8/8/4k3 w - f6 0 1'
EP_UNAVAILABLE = 'rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1'
DOUBLE_CHECK = '4k3/8/8/8/1b6/8/4r3/4K3 w - - 0 1'
PROMOTION = '1r2k3/P7/8/8/8/8/7p/4K3 w - - 0 1'
MATE = '7k/5Q2/6K1/8/8/8/8/8 w - - 0 1'
SINGLE_ROOT = '8/8/8/8/8/8/r1k5/K7 w - - 0 1'
SENTINEL_FEN = '4k3/8/8/8/8/8/6P1/4K3 w - - 0 1'
PERFT_OFFICIAL = [
    ('kiwipete', KIWIPETE, 4, 4_085_603, False),
    ('standard-3', '8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1', 5, 674_624, False),
    ('standard-4', 'r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1', 4, 422_333, False),
    ('standard-5', 'rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8', 4, 2_103_487, False),
    ('standard-6', 'r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10', 4, 3_894_594, False),
    ('revolution-7', 'r7/4p3/5p1q/3P4/4pQ2/4pP2/6pp/R3K1kr w Q - 1 3', 5, 11_609_488, False),
    ('chess960-official-1', 'rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w AHah - 0 1', 3, 8_902, True),
    ('chess960-official-2', '1rqbkrbn/1ppppp1p/1n6/p1N3p1/8/2P4P/PP1PPPP1/1RQBKRBN w FBfb - 0 9', 3, None, True),
    ('chess960-official-3', 'rbbqn1kr/pp2p1pp/6n1/2pp1p2/2P4P/P7/BP1PPPP1/R1BQNNKR w HAha - 0 9', 3, None, True),
    ('chess960-official-4', 'rqbbknr1/1ppp2pp/p5n1/4pp2/P7/1PP5/1Q1PPPPP/R1BBKNRN w GAga - 0 9', 3, None, True),
    ('chess960-official-5', '4rrb1/1kp3b1/1p1p4/pP1Pn2p/5p2/1PR2P2/2P1NB1P/2KR1B2 w D - 0 21', 3, None, True),
    ('chess960-official-6', '1rkr3b/1ppn3p/3pB1n1/6q1/R2P4/4N1P1/1P5P/2KRQ1B1 b Dbd - 0 14', 3, None, True),
    ('chess960-official-7', 'qbbnrkr1/p1pppppp/1p4n1/8/2P5/6N1/PPNPPPPP/1BRKBRQ1 b FCge - 1 3', 3, None, True),
    ('chess960-fixture-2', 'rr6/2kpp3/1ppn2p1/p2b1q1p/P4P1P/1PNN2P1/2PP4/1K2R2R b E - 1 20', 2, 1_438, True),
    ('chess960-fixture-3', 'rr6/2kpp3/1ppn2p1/p2b1q1p/P4P1P/1PNN2P1/2PP4/1K2RR2 w E - 0 20', 3, 37_340, True),
    ('chess960-fixture-4b', 'rr6/2kpp3/1ppnb1p1/p2Q1q1p/P4P1P/1PNN2P1/2PP4/1K2RR2 b E - 2 19', 4, 2_237_725, True),
    ('chess960-fixture-4w', 'rr6/2kpp3/1ppnb1p1/p4q1p/P4P1P/1PNN2P1/2PP2Q1/1K2RR2 w E - 1 19', 4, 2_098_209, True),
]
PERFT_SPECIAL = [
    ('legal-ep', EP, 3, None, False),
    ('pinned-ep', EP_PINNED, 3, None, False),
    ('discovered-check-ep', EP_DISCOVERED, 3, None, False),
    ('double-check', DOUBLE_CHECK, 3, None, False),
    ('promotion-and-capture-underpromotion', PROMOTION, 3, None, False),
    ('shredder-castle', CASTLE_960, 3, None, True),
    ('xfen-castle', CASTLE_960.replace('GB', 'KQ'), 3, None, True),
]
START_MOVES = ('e2e4', 'd2d4', 'g1f3', 'c2c4')
REPETITION = ('g1f3', 'g8f6', 'f3g1', 'f6g8')
KEY_FIXTURES = [
    ('startpos', None, False, ()),
    ('startpos-black', chess.STARTING_FEN.replace(' w ', ' b '), False, ()),
    ('four-field-fen', SENTINEL_FEN.rsplit(' ', 2)[0], False, ()),
    *((move, None, False, (move,)) for move in START_MOVES),
    ('castling', CASTLE, False, ()),
    ('white-castled', CASTLE, False, ('e1g1',)),
    ('both-castled', CASTLE, False, ('e1g1', 'e8c8')),
    ('shredder-castle', CASTLE_960, True, ()),
    ('xfen-castle', CASTLE_960.replace('GB', 'KQ'), True, ()),
    ('played-chess960-castle', CASTLE_960, True, ('d1g1',)),
    ('legal-ep', EP, False, ()),
    ('played-ep', EP, False, ('e5d6',)),
    ('pinned-ep', EP_PINNED, False, ()),
    ('discovered-check-ep', EP_DISCOVERED, False, ()),
    ('unavailable-potential-ep', EP_UNAVAILABLE, False, ()),
    ('unavailable-ep-cleared', EP_UNAVAILABLE.replace(' e3 ', ' - '), False, ()),
    ('double-check', DOUBLE_CHECK, False, ()),
    ('promotion', PROMOTION, False, ('a7a8q',)),
    ('underpromotion-capture', PROMOTION, False, ('a7b8n',)),
    ('repetition-cycle', None, False, REPETITION),
    ('repetition-threefold', None, False, REPETITION * 2),
]
INVALID_FENS = [
    ('king-side-no-rook', '4k3/8/8/8/8/8/8/4K3 w K - 0 1', False, '4k3/8/8/8/8/8/8/4K3 w - - 0 1'),
    ('queen-side-no-rook', '4k3/8/8/8/8/8/8/4K3 w q - 0 1', False, '4k3/8/8/8/8/8/8/4K3 w - - 0 1'),
    ('shredder-empty-rook-square', '4k3/8/8/8/8/8/8/4K3 w H - 0 1', True, '4k3/8/8/8/8/8/8/4K3 w - - 0 1'),
    ('shredder-wrong-rook-color', '4k3/8/8/8/8/8/8/3K3r w H - 0 1', True, '4k3/8/8/8/8/8/8/3K3r w - - 0 1'),
    ('shredder-wrong-piece', '4k3/8/8/8/8/8/8/4K2N w H - 0 1', True, '4k3/8/8/8/8/8/8/4K2N w - - 0 1'),
    ('xfen-king-off-home-rank', '4k3/8/8/8/8/8/4K3/7R w K - 0 1', True, '4k3/8/8/8/8/8/4K3/7R w - - 0 1'),
    ('xfen-rook-off-home-rank', '4k3/8/8/8/8/8/7R/4K3 w K - 0 1', True, '4k3/8/8/8/8/8/7R/4K3 w - - 0 1'),
    ('preserve-valid-right-next-to-bad-token', '4k3/8/8/8/8/8/8/1R1K2R1 w GA! - 0 1', True, '4k3/8/8/8/8/8/8/1R1K2R1 w G - 0 1'),
    ('unknown-castling-letter', '4k3/8/8/8/8/8/8/4K2R w Z - 0 1', False, '4k3/8/8/8/8/8/8/4K2R w - - 0 1'),
    ('missing-white-king', '4k3/8/8/8/8/8/8/R7 w Q - 0 1', False, chess.STARTING_FEN),
    ('missing-black-king', '8/8/8/8/8/8/8/4K2R b k - 0 1', False, chess.STARTING_FEN),
    ('overflowing-rank', '4k3/8/8/8/8/8/8/84K3 w - - 0 1', False, chess.STARTING_FEN),
    ('missing-rank', '4k3/8/8/8/8/8/4K3 w - - 0 1', False, chess.STARTING_FEN),
    ('unknown-board-piece', '4k3/8/8/8/8/8/8/4K2Z w - - 0 1', False, chess.STARTING_FEN),
    ('invalid-active-color', '4k3/8/8/8/8/8/8/4K3 x - - 0 1', False, chess.STARTING_FEN),
    ('nine-white-pawns', '4k3/8/8/8/8/P7/PPPPPPPP/4K3 w - - 0 1', False, chess.STARTING_FEN),
    ('promotion-budget-exceeded', '4k3/8/8/8/8/8/PPPPPPPP/3QKQ2 w - - 0 1', False, chess.STARTING_FEN),
    ('other-king-in-check', '4k3/8/8/8/8/8/4R3/4K3 w - - 0 1', False, chess.STARTING_FEN),
    ('out-of-range-halfmove', '4k3/8/8/8/8/8/8/4K3 w - - 32768 1', False, chess.STARTING_FEN),
    ('out-of-range-fullmove', '4k3/8/8/8/8/8/8/4K3 w - - 0 100001', False, chess.STARTING_FEN),
    # UCI pre-existing guard keeps the previous position for back-rank pawns.
    ('back-rank-pawn', '4k3/8/8/8/8/8/8/P3K3 w - - 0 1', False, SENTINEL_FEN),
    ('invalid-ep-square', '4k3/8/8/3pP3/8/8/8/4K3 w - z9 0 1', False, '4k3/8/8/3pP3/8/8/8/4K3 w - - 0 1'),
]


class Engine:
    """A UCI session with deadline-controlled reads of stdout and stderr."""

    def __init__(self, executable, cwd, transcript):
        self.process = subprocess.Popen(
            [str(executable)], cwd=cwd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace', bufsize=1,
        )
        self.transcript = transcript
        self.queue = queue.Queue()
        self.reader = threading.Thread(target=self._read_output, daemon=True)
        self.reader.start()

    def _read_output(self):
        assert self.process.stdout is not None
        for raw in self.process.stdout:
            stamped = f'{time.monotonic():.6f} < {raw}'
            self.transcript.write(stamped)
            self.transcript.flush()
            self.queue.put(raw.rstrip('\r\n'))
        self.queue.put(None)

    def send(self, command):
        assert self.process.stdin is not None
        self.transcript.write(f'{time.monotonic():.6f} > {command}\n')
        self.transcript.flush()
        self.process.stdin.write(command + '\n')
        self.process.stdin.flush()

    def until(self, predicate, label, timeout=TIMEOUT):
        collected = []
        deadline = time.monotonic() + timeout
        while True:
            try:
                line = self.queue.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise AssertionError(f'{label} timed out after {timeout}s, last output {collected[-12:]}') from None
            if line is None:
                self.process.wait(timeout=3)
                raise AssertionError(f'{label} received EOF, process exit {self.process.returncode}; last output {collected[-12:]}')
            collected.append(line)
            if predicate(line):
                return collected

    def ready(self, timeout=TIMEOUT):
        self.send('isready')
        return self.until(lambda line: line == 'readyok', 'isready', timeout)

    def option(self, name, value=None):
        suffix = '' if value is None else f' value {value}'
        self.send(f'setoption name {name}{suffix}')

    def configure(self, threads=1, size=64):
        self.option('Threads', threads)
        self.option('Hash', size)
        self.option('Experience Enabled', 'false')
        self.option('Experience Book', 'false')
        self.option('Book 1 Depth', 0)
        self.option('Book 2 Depth', 0)
        self.option('CTG/BIN Book 1 File', '<empty>')
        self.option('CTG/BIN Book 2 File', '<empty>')
        self.ready()

    def position(self, fen=None, chess960=False, moves=()):
        self.option('UCI_Chess960', str(chess960).lower())
        command = 'position startpos' if fen is None else f'position fen {fen}'
        if moves:
            command += ' moves ' + ' '.join(moves)
        self.send(command)

    def shutdown(self, check):
        try:
            if self.process.poll() is None:
                self.send('quit')
                self.process.wait(timeout=5)
            if check:
                assert self.process.returncode == 0, f'quit exit {self.process.returncode}'
        except (BrokenPipeError, subprocess.TimeoutExpired):
            self.process.kill()
            self.process.wait(timeout=5)
            if check:
                raise
        finally:
            self.reader.join(timeout=5)
            if self.process.stdout:
                self.process.stdout.close()
            if self.process.stdin:
                self.process.stdin.close()


@contextmanager
def session(binary, directory, name, *, verify_quit=True):
    transcript_path = directory / (name + '.uci.log')
    with tempfile.TemporaryDirectory(prefix='isolated cwd ', dir=directory) as cwd:
        with transcript_path.open('w', encoding='utf-8') as transcript:
            transcript.write(f'Executable: {binary}\nCWD: {cwd}\n')
            engine = Engine(binary, cwd, transcript)
            try:
                yield engine
            except BaseException:
                engine.shutdown(check=False)
                raise
            else:
                engine.shutdown(check=verify_quit)


def board_for(fen=None, chess960=False, moves=()):
    board = chess.Board(fen or chess.STARTING_FEN, chess960=chess960)
    assert board.is_valid(), f'invalid reference fixture {board.fen()}: {board.status()}'
    for move in moves:
        board.push_uci(move)
    return board


def uci_info(engine):
    engine.send('uci')
    lines = engine.until(lambda line: line == 'uciok', 'uci')
    ids = [line for line in lines if line.startswith('id name ')]
    assert ids == [f'id name {IDENTITY}'], ids
    assert any(line.startswith((f'{IDENTITY}-universal by ', f'info string {IDENTITY}-universal by '))
               for line in lines), lines[:10]
    options = {re.match(r'option name (.+?) type ', line).group(1): line
               for line in lines if line.startswith('option name ')}
    assert not (EXPECTED_OPTIONS - options.keys()), sorted(EXPECTED_OPTIONS - options.keys())
    assert options['EvalFile'].endswith(f'default {NET}'), options['EvalFile']
    engine.ready()
    return options


def position_info(engine, timeout=TIMEOUT):
    engine.send('d')
    lines = engine.ready(timeout)
    info = {}
    for label in ('Fen', 'Key', 'Checkers'):
        lines_with_label = [line.split(':', 1)[1].strip() for line in lines if line.startswith(label + ':')]
        assert len(lines_with_label) == 1, f'missing/duplicate {label} in {lines}'
        info[label.lower()] = lines_with_label[0]
    assert re.fullmatch(r'[0-9A-F]{16}', info['key']), info
    return info


def collect_keys(engine):
    collected = {}
    for name, fen, chess960, moves in KEY_FIXTURES:
        board = board_for(fen, chess960, moves)
        engine.position(fen, chess960, moves)
        with_history = position_info(engine)
        engine.position(with_history['fen'], chess960)
        reparsed = position_info(engine)
        assert with_history == reparsed, (name, with_history, reparsed)
        assert set(with_history['checkers'].split()) == {chess.square_name(square) for square in board.checkers()}, (name, with_history, board.fen())
        collected[name] = with_history
    assert collected['startpos']['key'] != collected['startpos-black']['key']
    assert collected['shredder-castle'] == collected['xfen-castle']
    assert collected['unavailable-potential-ep'] == collected['unavailable-ep-cleared']
    assert collected['legal-ep']['key'] != collected['played-ep']['key']
    return collected


def python_perft(board, depth):
    if depth == 0:
        return 1
    if depth == 1:
        return board.legal_moves.count()
    count = 0
    for move in board.legal_moves:
        board.push(move)
        count += python_perft(board, depth - 1)
        board.pop()
    return count


def python_divide(board, depth):
    divide = {}
    for move in board.legal_moves:
        uci = board.uci(move)
        board.push(move)
        divide[uci] = python_perft(board, depth - 1)
        board.pop()
    return divide


def engine_divide(engine, depth, timeout=120):
    engine.send(f'go perft {depth}')
    lines = engine.until(lambda line: line.startswith('Nodes searched:'), f'perft {depth}', timeout)
    entries = [match.groups() for line in lines if (match := re.fullmatch(r'([a-h][1-8][a-h][1-8][qrbn]?): (\d+)', line))]
    divide = {move: int(nodes) for move, nodes in entries}
    assert len(divide) == len(entries), f'duplicate roots in perft {depth}'
    total = int(lines[-1].split(':', 1)[1])
    assert total == sum(divide.values()), (total, divide)
    return total, divide


def perft_case(engine, fen, depth, expected_nodes, chess960):
    board = board_for(fen, chess960)
    engine.position(fen, chess960)
    total, divide = engine_divide(engine, depth)
    expected_moves = {board.uci(move) for move in board.legal_moves}
    if depth <= 3 or total != expected_nodes or divide.keys() != expected_moves:
        independent = python_divide(board, depth)
        differences = {move: [independent.get(move), divide.get(move)]
                       for move in independent.keys() | divide.keys() if independent.get(move) != divide.get(move)}
        assert not differences, f'root-divide differences [python-chess, engine]: {differences}'
    assert divide.keys() == expected_moves, (sorted(expected_moves - divide.keys()), sorted(divide.keys() - expected_moves))
    if expected_nodes is not None:
        assert total == expected_nodes, (total, expected_nodes)
    return {'fen': board.fen(shredder=chess960, en_passant='fen'), 'depth': depth, 'nodes': total, 'root_divide': divide}


def inspect_search(lines, reference_board, allowed_roots=None):
    bestline = lines[-1].split()
    assert len(bestline) in (2, 4) and bestline[0] == 'bestmove', bestline
    best = bestline[1]
    board = reference_board.copy(stack=False)
    move = board.push_uci(best)
    if allowed_roots is not None:
        assert best in allowed_roots, (best, allowed_roots)
    if len(bestline) == 4:
        assert bestline[2] == 'ponder', bestline
        board.push_uci(bestline[3])
    pv_records = []
    for line in lines:
        if not line.startswith('info depth ') or ' pv ' not in line:
            continue
        before, raw_pv = line.split(' pv ', 1)
        tokens = before.split()
        score = tokens.index('score')
        pv = raw_pv.split()
        assert pv, line
        pv_board = reference_board.copy(stack=False)
        for token in pv:
            pv_board.push_uci(token)
        pv_records.append({
            'depth': int(tokens[tokens.index('depth') + 1]),
            'multipv': int(tokens[tokens.index('multipv') + 1]),
            'score_type': tokens[score + 1], 'score': int(tokens[score + 2]),
            'bound': next((token for token in tokens if token in ('lowerbound', 'upperbound')), None),
            'nodes': int(tokens[tokens.index('nodes') + 1]),
            'hashfull': int(tokens[tokens.index('hashfull') + 1]),
            'pv': pv,
        })
    return {'bestmove': best, 'pvs': pv_records, 'checkmate': board.is_checkmate() if len(bestline) == 2 else False,
            'game_move': move.uci()}


def complete_search(engine, command, board, allowed_roots=None, timeout=TIMEOUT):
    engine.send(command)
    lines = engine.until(lambda line: line.startswith('bestmove '), command, timeout)
    return inspect_search(lines, board, allowed_roots)


def final_pvs(result):
    latest = {}
    for record in result['pvs']:
        latest[record['multipv']] = record
    return latest


def require_main_pv(result):
    principal = final_pvs(result)[1]
    assert principal['pv'][0] == result['bestmove'], (principal, result['bestmove'])
    assert principal['bound'] is None, principal
    return principal


def run_bench(binary, directory, label, repetitions=2):
    positions_by_run = []
    totals = []
    for index in range(1, repetitions + 1):
        with session(binary, directory, f'{label}-bench-{index}') as engine:
            engine.configure()
            engine.send('bench 16 1 8 default depth')
            lines = engine.until(lambda line: line.startswith('Nodes/second'), 'canonical bench', 180)
            matches = [re.fullmatch(r'Nodes searched\s*:\s*(\d+)', line) for line in lines]
            nodes = [int(m[1]) for m in matches if m]
            assert len(nodes) == 1 and nodes[0] > 0, nodes
            totals.append(nodes[0])
            positions = []
            for line in lines:
                if line.startswith('Position: '):
                    positions.append({'label': line, 'last_nodes': None})
                if positions and line.startswith('info depth ') and (match := re.search(r'\bnodes (\d+)', line)):
                    positions[-1]['last_nodes'] = int(match[1])
            positions_by_run.append(positions)
    assert len(set(totals)) == 1, totals
    assert all(positions == positions_by_run[0] for positions in positions_by_run), 'per-position node counts differ across benchmark runs'
    return {'total_nodes': totals, 'positions': positions_by_run[0]}


def search_openings(engine):
    results = {}
    for move in (None, *START_MOVES):
        moves = (move,) if move else ()
        engine.position(moves=moves)
        result = complete_search(engine, 'go depth 6', board_for(moves=moves))
        principal = require_main_pv(result)
        assert principal['depth'] == 6, principal
        results[move or 'startpos'] = {'bestmove': result['bestmove'], 'principal': principal}
    return results


def search_multipv(engine):
    results = {}
    for n in (1, 2, 3):
        engine.option('MultiPV', n)
        engine.position(KIWIPETE)
        result = complete_search(engine, 'go depth 6', board_for(KIWIPETE))
        final = final_pvs(result)
        require_main_pv(result)
        assert set(final) == set(range(1, n + 1)), final
        assert all(record['depth'] == 6 for record in final.values()), final
        assert len({record['pv'][0] for record in final.values()}) == n, final
        results[n] = final
    engine.option('MultiPV', 1)
    return results


def search_skill(engine):
    results = []
    observed_changed = False
    for n in range(12):
        level, multipv = ((0, 1), (0, 4), (1, 4), (5, 4))[n % 4]
        fen = (None, KIWIPETE, PROMOTION)[n % 3]
        engine.send('ucinewgame')
        engine.option('Skill Level', level)
        engine.option('MultiPV', multipv)
        engine.position(fen)
        result = complete_search(engine, 'go depth 8', board_for(fen))
        principal = require_main_pv(result)
        assert principal['depth'] == 8, principal
        principal_lines = [record for record in result['pvs'] if record['multipv'] == 1]
        changed = any(record['depth'] == principal['depth'] and record['pv'][0] != principal['pv'][0]
                      for record in principal_lines[:-1])
        observed_changed |= changed
        previous_scores = {(record['score_type'], record['score']) for record in result['pvs']
                           if record['multipv'] != 1 and record['depth'] == principal['depth']
                           and record['pv'] == principal['pv'] and record['bound'] is None}
        if previous_scores:
            assert (principal['score_type'], principal['score']) in previous_scores, (principal, previous_scores)
        results.append({'trial': n + 1, 'skill': level, 'multipv': multipv, 'fen': fen or chess.STARTING_FEN,
                        'principal': principal, 'bestmove': result['bestmove'], 'changed_root': changed,
                        'observed_matching_secondary_scores': sorted(previous_scores)})
    engine.option('Skill Level', 20)
    engine.option('MultiPV', 1)
    assert observed_changed, 'no late Skill move substitution observed across 12 trials'
    return results


def ordinary_searches(engine):
    results = {}
    board = board_for(MATE)
    independent_mates = set()
    for move in board.legal_moves:
        uci = board.uci(move)
        board.push(move)
        if board.is_checkmate():
            independent_mates.add(uci)
        board.pop()
    assert independent_mates, 'invalid mate-in-one fixture'
    engine.position(MATE)
    mate = complete_search(engine, 'go mate 1', board, independent_mates)
    score = require_main_pv(mate)
    assert (score['score_type'], score['score']) == ('mate', 1), mate
    results['mate-in-one'] = {'bestmove': mate['bestmove'], 'principal': score}

    board = board_for(SINGLE_ROOT)
    roots = {board.uci(move) for move in board.legal_moves}
    assert roots == {'a1a2'}
    engine.position(SINGLE_ROOT)
    single = complete_search(engine, 'go wtime 2500 btime 2500', board, roots)
    results['single-root'] = single['bestmove']

    engine.position()
    restricted = complete_search(engine, 'go depth 5 searchmoves e2e4 c2c4', board_for(), {'e2e4', 'c2c4'})
    assert all(record['pv'][0] in {'e2e4', 'c2c4'} for record in restricted['pvs']), restricted
    results['searchmoves'] = {'bestmove': restricted['bestmove'], 'principal': require_main_pv(restricted)}

    engine.position()
    node_result = complete_search(engine, 'go nodes 6000', board_for())
    last = final_pvs(node_result)[1]
    assert 1_000 <= last['nodes'] <= 15_000, last
    results['nodes'] = {'bestmove': node_result['bestmove'], 'last_info': last}
    return results


def timed_searches(engine):
    results = {}
    for label, moves, command in (
        ('negative-white-time', (), 'go wtime -100 btime 30000'),
        ('negative-black-time', ('e2e4',), 'go wtime 30000 btime -100'),
        ('both-negative-times', (), 'go wtime -100 btime -100'),
        ('negative-movetime', (), 'go movetime -100'),
        ('normal-movetime', (), 'go movetime 45'),
    ):
        engine.position(moves=moves)
        start = time.monotonic()
        result = complete_search(engine, command, board_for(moves=moves), timeout=6)
        results[label] = {'elapsed_seconds': round(time.monotonic() - start, 4), 'bestmove': result['bestmove']}

    engine.option('nodestime', 50)
    engine.send('ucinewgame')
    engine.position()
    commands = 'go wtime 1000 btime 1000 winc 50 binc 50'
    with_node_time = complete_search(engine, commands, board_for(), timeout=12)
    assert with_node_time['pvs'], with_node_time
    results['nodestime'] = {'bestmove': with_node_time['bestmove'], 'last_info': final_pvs(with_node_time)[1]}
    engine.option('nodestime', 0)

    engine.position()
    engine.send('go infinite')
    before_stop = engine.until(lambda line: line.startswith('info depth 4 '), 'infinite search reached depth 4')
    assert not any(line.startswith('bestmove ') for line in before_stop), before_stop
    engine.send('stop')
    after_stop = engine.until(lambda line: line.startswith('bestmove '), 'stop', 6)
    stopped = inspect_search(before_stop + after_stop, board_for())
    results['stop'] = stopped['bestmove']

    engine.option('Ponder', 'true')
    moves = ('e2e4', 'e7e5')
    engine.position(moves=moves)
    engine.send('go ponder depth 5')
    before_hit = engine.until(lambda line: line.startswith('info depth 5 '), 'ponder reached depth 5')
    assert not any(line.startswith('bestmove ') for line in before_hit), before_hit
    try:
        early_line = engine.queue.get(timeout=0.12)
    except queue.Empty:
        pass
    else:
        assert early_line is not None and not early_line.startswith('bestmove '), early_line
        before_hit.append(early_line)
    engine.send('ponderhit')
    after_hit = engine.until(lambda line: line.startswith('bestmove '), 'ponderhit', 6)
    pondered = inspect_search(before_hit + after_hit, board_for(moves=moves))
    results['ponderhit'] = {'bestmove': pondered['bestmove'], 'principal': require_main_pv(pondered)}
    engine.option('Ponder', 'false')
    engine.ready()
    return results


def stress_tt(engine):
    engine.option('Hash', 1)
    engine.send('ucinewgame')
    engine.position(KIWIPETE)
    pressure = []
    for generation, fen in enumerate((KIWIPETE, None, KIWIPETE), start=1):
        engine.position(fen)
        result = complete_search(engine, 'go nodes 200000', board_for(fen), timeout=60)
        maximum = max(record['hashfull'] for record in result['pvs'])
        assert 100 <= maximum <= 1000, (generation, maximum)
        pressure.append({'generation': generation, 'hashfull_pm': maximum, 'bestmove': result['bestmove']})
    engine.option('Hash', 64)
    engine.position()
    resized = complete_search(engine, 'go depth 6', board_for())
    resized_max = max(record['hashfull'] for record in resized['pvs'])
    assert resized_max < max(record['hashfull_pm'] for record in pressure), (resized_max, pressure)
    return {'hash_1_MB': pressure, 'hash_64_MB': resized_max, 'bestmove_after_resize': resized['bestmove']}


def search_960_and_smp(engine):
    engine.option('Threads', 2)
    ready = engine.ready()
    assert any('2 thread' in line or '2/2' in line for line in ready), ready
    board = board_for(CASTLE_960, True)
    castling = {board.uci(move) for move in board.legal_moves if board.is_castling(move)}
    assert castling == {'d1b1', 'd1g1'}, castling
    engine.position(CASTLE_960, True)
    result = complete_search(engine, 'go depth 7 searchmoves d1g1 d1b1', board, castling)
    engine.option('Threads', 1)
    returned = engine.ready()
    assert any('1 thread' in line or '1/1' in line for line in returned), returned
    require_main_pv(result)
    return {'bestmove': result['bestmove'], 'two_threads': ready, 'one_thread_restored': returned}


def perft_tests(engine, tests):
    details = {}
    for label, fen, depth, expected, chess960 in tests:
        details[label] = perft_case(engine, fen, depth, expected, chess960)
    return details


def invalid_fen_test(binary, directory, name, malformed, chess960, safe_fen):
    with session(binary, directory, f'candidate-fen-{name}') as engine:
        engine.configure()
        engine.position(SENTINEL_FEN, chess960)
        engine.position(malformed, chess960)
        actual = position_info(engine, timeout=6)
        engine.position(safe_fen, chess960)
        safe = position_info(engine, timeout=6)
        assert actual == safe, (malformed, actual, safe)
        engine.position(SENTINEL_FEN, chess960)
        engine.position(malformed, chess960)
        _, root_divide = engine_divide(engine, 1, timeout=6)
        reference_moves = python_divide(board_for(safe_fen, chess960), 1)
        assert root_divide == reference_moves, (malformed, root_divide, reference_moves)
        return {'input': malformed, 'chess960': chess960, 'resolved': actual, 'legal_roots': sorted(root_divide)}


def bench_delta(reference, candidate):
    deltas = []
    by_position = {position['label']: position['last_nodes'] for position in reference['positions']}
    for position in candidate['positions']:
        before, after = by_position.get(position['label']), position['last_nodes']
        if before != after:
            deltas.append({'position': position['label'], 'reference': before, 'candidate': after})
    before = reference['total_nodes'][0]
    after = candidate['total_nodes'][0]
    return {'reference': before, 'candidate': after, 'difference': after - before, 'positions_changed': deltas}


def sha256_file(path):
    with path.open('rb') as handle:
        return hashlib.file_digest(handle, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, help='Phase A executable')
    parser.add_argument('--r28', type=Path, help='frozen R28 baseline executable')
    parser.add_argument('--p0a28', type=Path, help='historical P0A28 comparison executable, if built during this run')
    parser.add_argument('--output', type=Path, required=True, help='output directory for runtime logs and results')
    args = parser.parse_args()
    if not (args.candidate or args.r28):
        parser.error('provide --candidate, --r28, or both')
    if args.candidate and not args.r28:
        parser.error('--candidate needs --r28 for genuine golden position keys and benchmark comparison')
    for label in ('candidate', 'r28', 'p0a28'):
        path = getattr(args, label)
        if path:
            setattr(args, label, path.resolve(strict=True))
    args.output.mkdir(parents=True, exist_ok=True)
    args.output = args.output.resolve()
    results = {'metadata': {'python_chess': chess.__version__, 'reference_history': {'p0a28_nodes': 114_876}}, 'tests': {}}
    for label, path in (('candidate', args.candidate), ('r28', args.r28), ('p0a28', args.p0a28)):
        if path:
            results['metadata'][label] = {'path': str(path), 'size': path.stat().st_size, 'sha256': sha256_file(path)}

    def execute(name, function, *func_args):
        start = time.monotonic()
        entry = {}
        try:
            details = function(*func_args)
            entry.update(status='PASS', details=details)
        except Exception as error:
            entry.update(status='FAIL', reason=f'{type(error).__name__}: {error}', traceback=traceback.format_exc())
        entry['elapsed_seconds'] = round(time.monotonic() - start, 4)
        results['tests'][name] = entry
        print(f'{name}={entry["status"]} ({entry["elapsed_seconds"]:.3f}s)', flush=True)
        if 'reason' in entry:
            print(entry['reason'], flush=True)
        return entry.get('details')

    def with_engine(binary, name, function, *func_args, configure=True):
        with session(binary, args.output, name) as engine:
            if configure:
                engine.configure()
            return function(engine, *func_args)

    base_options = execute('R28_UCI', with_engine, args.r28, 'r28-uci', uci_info)
    base_keys = execute('R28_ZOBRIST_KEYS', with_engine, args.r28, 'r28-position-keys', collect_keys)
    base_bench = execute('R28_BENCH', run_bench, args.r28, args.output, 'r28')
    historical_bench = None
    if args.p0a28:
        historical_bench = execute('P0A28_BENCH', run_bench, args.p0a28, args.output, 'p0a28')
    else:
        results['tests']['P0A28_BENCH'] = {'status': 'NOT_RUN', 'reason': 'Exact historical executable not supplied; 114876 is reference only'}

    if args.candidate:
        execute('UCI', with_engine, args.candidate, 'candidate-uci', uci_info)
        actual_options = results['tests']['UCI'].get('details')
        if base_options is not None and actual_options is not None:
            if base_options == actual_options:
                results['tests']['OPTIONS_UNCHANGED'] = {'status': 'PASS'}
            else:
                changed = {name: [base_options.get(name), actual_options.get(name)]
                           for name in base_options.keys() | actual_options.keys()
                           if base_options.get(name) != actual_options.get(name)}
                results['tests']['OPTIONS_UNCHANGED'] = {'status': 'FAIL', 'differences': changed}
        else:
            results['tests']['OPTIONS_UNCHANGED'] = {'status': 'NOT_RUN', 'reason': 'UCI exchange failed'}

        actual_keys = execute('CANDIDATE_ZOBRIST_KEYS', with_engine, args.candidate, 'candidate-position-keys', collect_keys)
        if base_keys is not None and actual_keys is not None:
            if base_keys == actual_keys:
                results['tests']['ZOBRIST_GOLDEN_KEYS'] = {'status': 'PASS', 'fixture_count': len(base_keys)}
            else:
                changed = {name: [base_keys.get(name), actual_keys.get(name)]
                           for name in base_keys.keys() | actual_keys.keys() if base_keys.get(name) != actual_keys.get(name)}
                results['tests']['ZOBRIST_GOLDEN_KEYS'] = {'status': 'FAIL', 'differences': changed}
        else:
            results['tests']['ZOBRIST_GOLDEN_KEYS'] = {'status': 'NOT_RUN', 'reason': 'One or both key collection runs failed'}

        tests = [
            ('LEGAL_SEARCH', search_openings, ()),
            ('MULTIPV', search_multipv, ()),
            ('SKILL_PV', search_skill, ()),
            ('TACTICAL_SEARCH_NODES_AND_SEARCHMOVES', ordinary_searches, ()),
            ('TIMED_SEARCH_NODES_TIME_STOP_PONDER', timed_searches, ()),
            ('TT_PRESSURE_RESIZE', stress_tt, ()),
            ('THREADS_SMP_CHESS960', search_960_and_smp, ()),
            ('PERFT_START', perft_tests, ([(f'startpos-{i}', None, i, n, False)
                                         for i, n in enumerate((20, 400, 8_902, 197_281, 4_865_609), start=1)],)),
            ('PERFT_OFFICIAL', perft_tests, (PERFT_OFFICIAL,)),
            ('PERFT_EP_DOUBLE_CHECK_PROMOTIONS_CASTLE', perft_tests, (PERFT_SPECIAL,)),
        ]
        for label, function, function_args in tests:
            execute(label, with_engine, args.candidate, 'candidate-' + label.lower(), function, *function_args)

        for name, malformed, chess960, safe in INVALID_FENS:
            execute('FEN_INVALID_' + name, invalid_fen_test, args.candidate, args.output, name, malformed, chess960, safe)

        candidate_bench = execute('CANDIDATE_BENCH', run_bench, args.candidate, args.output, 'candidate')
        if base_bench and candidate_bench:
            results['r28_bench_comparison'] = bench_delta(base_bench, candidate_bench)
        if historical_bench and candidate_bench:
            results['p0a28_bench_comparison'] = bench_delta(historical_bench, candidate_bench)
    else:
        results['tests']['CANDIDATE_RUNTIME'] = {'status': 'NOT_RUN', 'reason': 'Exact candidate executable not supplied'}

    path = args.output / 'runtime-results.json'
    path.write_text(json.dumps(results, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    failures = [name for name, entry in results['tests'].items() if entry['status'] == 'FAIL']
    print(f'Failures: {failures}')
    print(f'Report: {path}')
    sys.exit(bool(failures))


if __name__ == '__main__':
    main()
