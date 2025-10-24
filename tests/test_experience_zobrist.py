import os
import subprocess
import threading
import time
import unittest
from pathlib import Path
from queue import Empty, Queue


REPO_ROOT = Path(__file__).resolve().parent.parent
SRC_DIR = REPO_ROOT / "src"


def build_engine():
    executable = SRC_DIR / ("revolution.exe" if os.name == "nt" else "revolution")
    build_cmd = ["make", "build", "ARCH=x86-64", "COMP=gcc"]
    subprocess.run(build_cmd, cwd=SRC_DIR, check=True)
    return executable


class EngineSession:
    def __init__(self, engine_path: Path):
        self.process = subprocess.Popen(
            [str(engine_path)],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        self._stdout_queue: Queue[str | None] = Queue()
        self._reader = threading.Thread(target=self._enqueue_stdout, daemon=True)
        self._reader.start()

    def send(self, command: str) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()

    def read_until(self, predicate, timeout: float = 5.0):
        end_time = time.time() + timeout
        lines = []

        while time.time() < end_time:
            remaining = end_time - time.time()
            if remaining <= 0:
                break

            try:
                line = self._stdout_queue.get(timeout=remaining)
            except Empty:
                break

            if line is None:
                break

            stripped = line.strip()
            lines.append(stripped)

            if predicate(stripped):
                return lines

        raise AssertionError(f"Timed out waiting for output. Collected lines: {lines}")

    def _enqueue_stdout(self) -> None:
        assert self.process.stdout is not None
        for raw_line in self.process.stdout:
            self._stdout_queue.put(raw_line.rstrip("\n"))
        self._stdout_queue.put(None)

    def close(self):
        if self.process.poll() is None:
            try:
                self.send("quit")
            except BrokenPipeError:
                pass

            try:
                self.process.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.process.kill()

        if self.process.stdin and not self.process.stdin.closed:
            self.process.stdin.close()

        if self.process.stdout and not self.process.stdout.closed:
            self.process.stdout.close()

        if self.process.stderr and not self.process.stderr.closed:
            self.process.stderr.close()

        if self._reader.is_alive():
            self._reader.join(timeout=1.0)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()


class ExperienceAndZobristIntegrationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.engine_path = build_engine()
        if not cls.engine_path.exists():
            raise unittest.SkipTest("Engine binary could not be built")

    def test_learning_and_zobrist_paths_are_available(self):
        with EngineSession(self.engine_path) as engine:
            engine.send("uci")
            uci_lines = engine.read_until(lambda line: line == "uciok", timeout=10.0)

            self.assertTrue(
                any("option name Read only learning" in line for line in uci_lines),
                "Read only learning option missing from UCI output",
            )
            self.assertTrue(
                any("option name Experience Book" in line for line in uci_lines),
                "Experience Book option missing from UCI output",
            )
            self.assertTrue(
                any("option name Concurrent Experience" in line for line in uci_lines),
                "Concurrent Experience option missing from UCI output",
            )

            engine.send("isready")
            engine.read_until(lambda line: line == "readyok")

            engine.send("position startpos")
            engine.send("d")
            board_lines = engine.read_until(lambda line: line.startswith("Checkers:"))
            start_key_line = next((line for line in board_lines if "Key:" in line), None)
            self.assertIsNotNone(start_key_line, "Board dump did not include a Zobrist key")
            start_key = start_key_line.split("Key:")[1].strip()
            self.assertNotEqual(start_key.lower(), "0", "Initial Zobrist key should not be zero")

            engine.send("position startpos moves e2e4")
            engine.send("d")
            board_lines_after_move = engine.read_until(lambda line: line.startswith("Checkers:"))
            move_key_line = next((line for line in board_lines_after_move if "Key:" in line), None)
            self.assertIsNotNone(move_key_line, "Board dump after move did not include a Zobrist key")
            moved_key = move_key_line.split("Key:")[1].strip()
            self.assertNotEqual(
                start_key.lower(), moved_key.lower(), "Zobrist key should change after a move"
            )

            engine.send("showexp")
            exp_lines = engine.read_until(lambda line: line.startswith("info string"))
            self.assertTrue(
                any("experience" in line for line in exp_lines),
                "Experience description was not returned by showexp",
            )

            engine.send("ucinewgame")
            engine.send("position startpos")
            engine.send("go depth 1")
            search_lines = engine.read_until(lambda line: line.startswith("bestmove"), timeout=20.0)
            bestmove_line = next((line for line in search_lines if line.startswith("bestmove")), None)
            self.assertIsNotNone(bestmove_line, "Engine did not return a bestmove")
            self.assertNotIn("(none)", bestmove_line, "Engine reported no best move")


if __name__ == "__main__":
    unittest.main()
