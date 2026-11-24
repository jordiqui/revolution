import argparse
from contextlib import contextmanager
import hashlib
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
import traceback

import chess
import chess.polyglot
import chess.syzygy

ROOT = Path(__file__).resolve().parent
TESTDATA = ROOT / 'chess-1.11.2/data'
OPTIONS = (
    'CTG/BIN Book 1 File', 'CTG/BIN Book 2 File', 'Book 1 Depth',
    'Experience Enabled', 'Experience File', 'Experience Readonly',
    'Experience Book', 'EvalFile', 'SyzygyPath',
)
NET = 'nn-d1ee674a62ca.nnue'


class Uci:
    def __init__(self, binary, cwd):
        self.proc = subprocess.Popen(
            [binary], cwd=cwd, stderr=subprocess.STDOUT, stdout=subprocess.PIPE,
            stdin=subprocess.PIPE, text=True, bufsize=1,
        )
        self.lines = queue.Queue()
        threading.Thread(target=self.reader, daemon=True).start()

    def reader(self):
        for line in self.proc.stdout:
            self.lines.put(line.strip())
        self.lines.put(None)

    def send(self, line):
        print('>>>', line)
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()

    def wait(self, pattern, timeout=30):
        deadline = time.monotonic() + timeout
        lines = []
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(pattern)
            try:
                line = self.lines.get(timeout=remaining)
            except queue.Empty as exc:
                raise TimeoutError(pattern) from exc
            if line is None:
                raise RuntimeError(f'Unexpected end of UCI output waiting for {pattern}')
            print(line)
            lines.append(line)
            if re.search(pattern, line):
                return lines

    def expect_no_bestmove(self, duration):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                line = self.lines.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                return
            assert line is not None and not line.startswith('bestmove '), line
            print(line)

    def ready(self):
        self.send('isready')
        return self.wait(r'^readyok$')

    def option(self, name, value):
        self.send(f'setoption name {name} value {value}')

    def newgame(self):
        self.send('ucinewgame')
        self.ready()

    def position(self, board):
        self.send('position fen ' + board.fen(shredder=board.chess960, en_passant='fen'))

    def search(self, board, limits):
        self.position(board)
        self.send('go ' + limits)
        out = self.wait(r'^bestmove ', timeout=60)
        assert_legal(board, out)
        return out

    def perft(self, board, depth):
        self.position(board)
        self.send(f'go perft {depth}')
        lines = self.wait(r'^Nodes searched:\s*\d+', timeout=120)
        found = int(lines[-1].split()[-1])
        roots = {
            match.group(1)
            for line in lines if (match := re.fullmatch(r'([a-h][1-8][a-h][1-8][qrbn]?):\s*\d+', line))
        }
        assert roots == legal_moves(board), (board.fen(), roots, legal_moves(board))
        return found

    def eval(self, board):
        self.position(board)
        self.send('eval')
        out = self.wait(r'^Final evaluation')
        raw, = (
            int(m.group(1))
            for line in out if (m := re.match(r'^NNUE evaluation\s+([+-]?\d+) \(side to move,', line))
        )
        return raw


def legal_moves(board):
    return {board.uci(move, chess960=board.chess960) for move in board.legal_moves}


def assert_legal(board, out):
    assert not any('error' in line.lower() for line in out), out
    assert bestmove(out) in legal_moves(board), (board.fen(), out[-1])
    for line in out:
        parts = line.split()
        if 'pv' not in parts or 'depth' not in parts:
            continue
        pv = parts[parts.index('pv')+1:]
        child = board.copy()
        for move in pv:
            child.push_uci(move)


def bestmove(out):
    return out[-1].split()[1]


def pv_map(lines, depth):
    ans = {}
    for line in lines:
        m = re.match(r'^info depth (\d+).*? multipv (\d+) score (cp|mate) (-?\d+).*? pv (\S+)', line)
        if m is not None and int(m[1]) == depth:
            ans[int(m[2])] = (m[5], m[3], int(m[4]))
    return ans


@contextmanager
def engine(binary, cwd):
    uci = Uci(binary, cwd)
    try:
        uci.send('uci')
        out = uci.wait(r'^uciok$')
        assert out[0].startswith('Revolution-6.40-150926-universal by '), out[0]
        assert 'id name Revolution-6.40-150926' in out
        optnames = {
            match.group(1)
            for line in out if (match := re.match(r'^option name (.+?) type ', line))
        }
        assert set(OPTIONS) <= optnames, set(OPTIONS) - optnames
        assert f'option name EvalFile type string default {NET}' in out
        uci.option('Experience Enabled', 'false')
        uci.option('Threads', 1)
        uci.option('Hash', 64)
        uci.newgame()
        yield uci
        uci.send('quit')
        assert uci.proc.wait(timeout=20) == 0
    finally:
        if uci.proc.poll() is None:
            uci.proc.kill()
            uci.proc.wait(timeout=20)


def startpos(*moves):
    board = chess.Board()
    for move in moves:
        board.push_uci(move)
    return board


def perft(binary, cwd):
    standard = [
        ('r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1', 4, 4085603),
        ('8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1', 5, 674624),
        ('r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1', 4, 422333),
        ('rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8', 4, 2103487),
        ('r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10', 4, 3894594),
        ('r7/4p3/5p1q/3P4/4pQ2/4pP2/6pp/R3K1kr w Q - 1 3', 5, 11609488),
    ]
    with engine(binary, cwd) as uci:
        for depth, count in enumerate((20, 400, 8902, 197281, 4865609), 1):
            assert uci.perft(startpos(), depth) == count, (depth, count)
        for fen, depth, count in standard:
            assert uci.perft(chess.Board(fen), depth) == count, (fen, depth, count)
    print('PERFT=PASS startpos D1–D5, Kiwipete D4 and five additional official positions')


def move_edges(binary, cwd):
    ep = [
        ('8/8/8/r4pPK/8/8/8/4k3 w - f6 0 1', 'g5f6', False),
        ('4k3/8/8/5pP1/8/8/8/4K3 w - f6 0 1', 'g5f6', True),
        ('k3r3/8/8/3pP3/8/8/8/4K3 w - d6 0 1', 'e5d6', False),
        ('4k3/8/8/3pP3/4K3/8/8/8 w - d6 0 1', 'e5d6', True),
    ]
    with engine(binary, cwd) as uci:
        for fen, move, legal in ep:
            board = chess.Board(fen)
            expected = legal_moves(board)
            assert (move in expected) == legal, (fen, move)
            assert uci.perft(board, 1) == len(expected), fen
        print('EP=PASS four legal, pinned and discovered-check cases')
        promo = chess.Board('4k3/P7/8/8/8/8/7p/4K3 w - - 0 1')
        assert {'a7a8q', 'a7a8r', 'a7a8b', 'a7a8n'} <= legal_moves(promo)
        assert uci.perft(promo, 1) == len(legal_moves(promo))
        print('PROMOTIONS=PASS queen, rook, bishop and knight')
        double = chess.Board('k3r3/8/8/8/1b6/8/8/4K2R w - - 0 1')
        assert len(double.checkers()) == 2
        assert legal_moves(double) and all(m.startswith('e1') for m in legal_moves(double))
        assert uci.perft(double, 1) == len(legal_moves(double))
        print('DOUBLE_CHECK=PASS')
        uci.option('UCI_Chess960', 'true')
        uci.ready()
        for fen, depth, count in [
            ('rr6/2kpp3/1ppn2p1/p2b1q1p/P4P1P/1PNN2P1/2PP4/1K2R2R b E - 1 20', 2, 1438),
            ('rr6/2kpp3/1ppn2p1/p2b1q1p/P4P1P/1PNN2P1/2PP4/1K2RR2 w E - 0 20', 3, 37340),
            ('rr6/2kpp3/1ppnb1p1/p2Q1q1p/P4P1P/1PNN2P1/2PP4/1K2RR2 b E - 2 19', 4, 2237725),
            ('rr6/2kpp3/1ppnb1p1/p4q1p/P4P1P/1PNN2P1/2PP2Q1/1K2RR2 w E - 1 19', 4, 2098209),
        ]:
            board = chess.Board(fen, chess960=True)
            assert uci.perft(board, depth) == count, (fen, depth, count)
        print('CHESS960=PASS four official positions')


def search(binary, cwd):
    openings = ('e2e4', 'd2d4', 'g1f3', 'c2c4')
    board = startpos()
    with engine(binary, cwd) as uci:
        uci.search(board, 'depth 8')
        for move in openings:
            uci.search(startpos(move), 'depth 6')
        print('LEGAL_SEARCH=PASS startpos and four openings')
        mate = chess.Board('7k/5Q2/6K1/8/8/8/8/8 w - - 0 1')
        output = uci.search(mate, 'depth 6')
        mate.push_uci(bestmove(output))
        assert mate.is_checkmate() and any('score mate 1 ' in line for line in output)
        print('MATE_IN_ONE=PASS')
        for line in (
            'wtime 1000 btime 1000 winc 0 binc 0',
            'wtime -100 btime -200 winc 0 binc 0',
            'movetime -100',
        ):
            uci.newgame()
            uci.search(board, line)
        single = chess.Board('7k/8/6K1/8/8/8/8/7R b - - 0 1')
        assert legal_moves(single) == {'h8g8'}
        for nodestime in (0, 1000):
            uci.option('nodestime', nodestime)
            uci.newgame()
            assert bestmove(uci.search(single, 'wtime 30000 btime 30000 winc 0 binc 0')) == 'h8g8'
        uci.option('nodestime', 0)
        uci.newgame()
        assert bestmove(uci.search(board, 'depth 8 searchmoves e2e4')) == 'e2e4'
        uci.position(board)
        uci.send('go infinite')
        uci.wait(r'^info depth [3-9] ')
        uci.send('stop')
        assert_legal(board, uci.wait(r'^bestmove '))
        uci.position(board)
        uci.send('go ponder depth 4')
        uci.wait(r'^info depth 4 ')
        uci.expect_no_bestmove(0.25)
        uci.send('ponderhit')
        assert_legal(board, uci.wait(r'^bestmove '))
        print('TIMED_SEARCH=PASS negative times, nodestime, single root, searchmoves, stop and ponderhit')
        uci.option('Threads', 2)
        uci.newgame()
        uci.search(board, 'depth 10')
        uci.option('Threads', 1)
        uci.newgame()
        uci.search(board, 'depth 8')
        print('THREADS_SMP=PASS 2 threads and return to 1')


def tt(binary, cwd):
    with engine(binary, cwd) as uci:
        uci.option('Hash', 1)
        uci.newgame()
        for move in ('e2e4', 'd2d4', 'g1f3', 'c2c4'):
            out = uci.search(startpos(move), 'nodes 50000')
            load = [int(m.group(1)) for line in out if (m := re.search(r' hashfull (\d+)', line))]
            assert load and max(load) > 100, (move, load)
        uci.option('Hash', 64)
        uci.newgame()
        pos = startpos('e2e4', 'e7e6')
        a = uci.search(pos, 'nodes 10000')
        uci.search(pos, 'nodes 10000')
        uci.newgame()
        b = uci.search(pos, 'nodes 10000')

    def signature(lines):
        return [
            re.sub(r' (?:nps|time) \d+', '', line)
            for line in lines if line.startswith(('info depth ', 'bestmove '))
        ]

    assert signature(a) == signature(b), (signature(a), signature(b))
    print('TT_HASH=PASS 1 MB pressure, generations, resize and repeatability; SWAR internals not directly instrumented')


def multipv(binary, cwd):
    with engine(binary, cwd) as uci:
        for num in (1, 2, 3):
            uci.option('MultiPV', num)
            uci.newgame()
            out = uci.search(startpos(), 'depth 6')
            ranks = pv_map(out, 6)
            assert set(ranks) == set(range(1, num+1)), (num, ranks)
            assert len({value[0] for value in ranks.values()}) == num, ranks
            assert ranks[1][0] == bestmove(out), (ranks, out[-1])
        print('MULTIPV=PASS 1/2/3')


def skill(binary, cwd):
    wrong = []
    for i in range(8):
        with engine(binary, cwd) as uci:
            uci.option('Skill Level', 0)
            uci.option('MultiPV', 1)
            uci.newgame()
            pos = startpos() if i % 2 == 0 else startpos('e2e4', 'e7e5')
            out = uci.search(pos, 'depth 8')
            ranks = pv_map(out, 8)
            assert 1 in ranks, ranks
            if bestmove(out) != ranks[1][0]:
                wrong.append((i+1, bestmove(out), ranks[1]))
    assert not wrong, f'Skill Level 0 last main PV does not match the bestmove: {wrong}'
    print('SKILL_LEVEL_PV=PASS eight searches with scored PV matching bestmove')


def transitions(binary, cwd):
    cases = [
        ('4k3/8/8/4q3/4R3/8/8/4K3 w - - 0 1', 'e4e5'),
        ('4k3/8/8/4r3/3B4/8/8/4K3 w - - 0 1', 'd4e5'),
        ('4k3/P7/8/8/8/8/7p/4K3 w - - 0 1', 'a7a8q'),
        ('4k3/P7/8/8/8/8/7p/4K3 w - - 0 1', 'a7a8r'),
        ('4k3/P7/8/8/8/8/7p/4K3 w - - 0 1', 'a7a8n'),
        ('4k3/P7/8/8/8/8/7p/4K3 b - - 0 1', 'h2h1q'),
        ('4k3/P7/8/8/8/8/7p/4K3 b - - 0 1', 'h2h1r'),
        ('4k3/8/2p5/2P5/1P6/8/8/4K3 w - - 0 1', 'b4b5'),
        ('4k3/8/2p5/1pP5/1P6/8/8/4K3 w - b6 0 1', 'c5b6'),
    ]
    with engine(binary, cwd) as uci:
        for fen, move in cases:
            pos = chess.Board(fen)
            assert pos.is_valid(), (fen, pos.status())
            assert move in legal_moves(pos), (fen, move)
            assert bestmove(uci.search(pos, 'depth 8 searchmoves ' + move)) == move
            next_pos = pos.copy()
            next_pos.push_uci(move)
            if legal_moves(next_pos):
                uci.search(next_pos, 'depth 6')
            if not next_pos.is_check():
                raw = uci.eval(next_pos)
                mirrored = uci.eval(next_pos.mirror())
                assert raw == mirrored, (fen, move, raw, mirrored)
        print('NNUE_TRANSITIONS=PASS last rook and queen capture, and rook, queen and knight promotions in search')
        print('PAWN_PAIRS=PASS legal pawn transition searches and raw NNUE color-mirror evaluation')


def polyglot(binary, cwd):
    book = TESTDATA / 'polyglot' / 'performance.bin'
    board = startpos()
    with chess.polyglot.open_reader(book) as reader:
        expected = {entry.move.uci() for entry in reader.find_all(board)}
    assert expected
    with engine(binary, cwd) as uci:
        uci.option('CTG/BIN Book 1 File', book)
        uci.option('Book 1 Depth', 8)
        uci.ready()
        out = uci.search(board, 'depth 4')
        move = bestmove(out)
        assert f'info string Book move: {move}' in out and move in expected, (move, expected, out)
    print(f'BOOK_RUNTIME=PASS BIN/Polyglot fixture bestmove={move}; CTG=NOT_RUN, no CTG fixture available')


def experience(binary, cwd):
    exp = cwd / 'learned.exp'
    board = startpos()
    with engine(binary, cwd) as uci:
        uci.option('Experience File', exp)
        uci.option('Experience Book Min Depth', 4)
        uci.option('Experience Enabled', 'true')
        uci.ready()
        uci.search(board, 'depth 8')
        uci.newgame()
        assert exp.is_file() and exp.stat().st_size > 0, exp
    original = hashlib.sha256(exp.read_bytes()).hexdigest()
    with engine(binary, cwd) as uci:
        uci.option('Experience File', exp)
        uci.option('Experience Book Min Depth', 4)
        uci.option('Experience Readonly', 'true')
        uci.option('Experience Enabled', 'true')
        uci.option('Experience Book', 'true')
        uci.ready()
        uci.send('uci')
        status = uci.wait(r'^uciok$')
        assert any('Total positions: 1.' in line for line in status), status
        uci.search(startpos('e2e4'), 'depth 8')
        uci.newgame()
    assert hashlib.sha256(exp.read_bytes()).hexdigest() == original
    print('EXPERIENCE_RUNTIME=PASS search generated a persisted file, read-only reload leaves SHA256 unchanged')
    print('LEARN_RUNTIME=PASS experience persistence and read-only protection for the learning hook')


def syzygy(binary, cwd):
    data = TESTDATA / 'syzygy' / 'regular'
    with engine(binary, cwd) as uci, chess.syzygy.open_tablebase(data) as oracle:
        uci.option('SyzygyPath', data)
        uci.option('SyzygyProbeDepth', 1)
        uci.newgame()
        for fen in (
            '7k/8/6K1/8/8/8/8/R7 w - - 0 1',
            '8/8/4k3/8/3P4/8/4K3/1R6 w - - 0 1',
        ):
            pos = chess.Board(fen)
            expected = oracle.probe_wdl(pos)
            assert expected > 0
            out = uci.search(pos, 'depth 8')
            pos.push_uci(bestmove(out))
            assert -oracle.probe_wdl(pos) == expected, (fen, out[-1])
            assert any(m and int(m[1]) for line in out if (m := re.search(r' tbhits (\d+)', line))), out
    print('SYZYGY_RUNTIME=PASS 3/4 piece probes; 5/6 piece NOT_RUN, no corresponding fixture available')


def bench(binary, cwd):
    results = []
    for _ in range(2):
        commands = 'setoption name Experience Enabled value false\nbench 16 1 8 default depth\nquit\n'
        print(commands, end='')
        result = subprocess.run(
            [binary], cwd=cwd, input=commands, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, timeout=300,
        )
        print(result.stdout, end='')
        assert result.returncode == 0, result.returncode
        count, = re.findall(r'Nodes searched\s*:\s*(\d+)', result.stdout)
        assert len(re.findall(r'Position: \d+/51', result.stdout)) == 51
        results.append(int(count))
    print(f'BENCH_CANDIDATE_NODES_1={results[0]} BENCH_CANDIDATE_NODES_2={results[1]}')
    assert results == [114876, 114876], results
    print('BENCH_REPEATABILITY=PASS')


CASES = {
    f.__name__: f for f in (
        perft, move_edges, search, tt, multipv, skill, transitions, polyglot, experience, syzygy, bench,
    )
}
if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('--cases', nargs='+', choices=CASES, default=list(CASES))
    parser.add_argument('--outdir', type=Path, required=True)
    args = parser.parse_args()
    assert args.binary.is_file()
    args.outdir.mkdir(parents=True, exist_ok=False)
    results = {}
    for name in args.cases:
        print(f'===== {name} =====', flush=True)
        case_dir = args.outdir / name
        case_dir.mkdir()
        try:
            CASES[name](str(args.binary.resolve()), case_dir)
        except Exception as failure:
            traceback.print_exc()
            results[name] = f'FAIL {type(failure).__name__}: {failure}'
        else:
            results[name] = 'PASS'
    for name, result in results.items():
        print(name, result, sep='\t')
    raise SystemExit(any(result != 'PASS' for result in results.values()))
