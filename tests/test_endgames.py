from pathlib import Path

from testing import MiniTestFramework, OrderedClassMembers, Revolution

PATH = Path(__file__).parent.resolve()
ENGINE_PATH = PATH.parent / "src" / "revolution-3.10-051125"


class EndgameEvaluationTests(metaclass=OrderedClassMembers):

    def beforeAll(self):
        self.revolution = None

    def afterAll(self):
        if self.revolution:
            try:
                self.revolution.quit()
            except Exception:
                pass
            finally:
                self.revolution.close()
                self.revolution = None

    def beforeEach(self):
        if self.revolution:
            try:
                self.revolution.quit()
            except Exception:
                pass
            finally:
                self.revolution.close()
                self.revolution = None

    def afterEach(self):
        if self.revolution:
            try:
                self.revolution.quit()
            except Exception:
                pass
            finally:
                self.revolution.close()
                self.revolution = None

    def _launch_engine(self):
        if not ENGINE_PATH.exists():
            raise RuntimeError("Engine binary not found. Build the project before running the tests.")

        if self.revolution:
            return

        self.revolution = Revolution([], str(ENGINE_PATH))
        self.revolution.send_command("uci")
        self.revolution.expect("uciok")
        self.revolution.send_command("isready")
        self.revolution.expect("readyok")

    def _evaluate_fen(self, fen: str) -> int:
        try:
            self._launch_engine()
            self.revolution.send_command(f"position fen {fen}")
            self.revolution.send_command("go depth 1")

            score = None

            def parser(line: str) -> bool:
                nonlocal score
                if "score cp" in line:
                    tokens = line.split()
                    idx = tokens.index("cp")
                    score = int(tokens[idx + 1])
                elif "score mate" in line:
                    tokens = line.split()
                    idx = tokens.index("mate")
                    mate = int(tokens[idx + 1])
                    score = 32000 if mate > 0 else -32000

                return line.startswith("bestmove")

            self.revolution.check_output(parser)

            if score is None:
                raise AssertionError("Engine did not return an evaluation score")

            side_to_move = fen.split()[1]
            if side_to_move == "b":
                score = -score

            return score
        finally:
            if self.revolution:
                try:
                    self.revolution.quit()
                except Exception:
                    pass
                finally:
                    self.revolution.close()
                    self.revolution = None

    def test_passed_pawn_rank_weighting(self):
        base_fen = "7k/8/8/3P4/8/8/1K6/8 w - - 0 1"
        advanced_fen = "7k/8/3P4/8/8/8/1K6/8 w - - 0 1"

        base_score = self._evaluate_fen(base_fen)
        advanced_score = self._evaluate_fen(advanced_fen)

        assert advanced_score >= base_score + 30

    def test_rook_activity_in_rook_endings(self):
        passive_fen = "7r/6k1/8/8/8/8/P5P1/R5K1 w - - 0 1"
        active_fen = "7r/6k1/8/8/8/R7/P5P1/6K1 w - - 0 1"

        passive_score = self._evaluate_fen(passive_fen)
        active_score = self._evaluate_fen(active_fen)

        assert active_score >= passive_score + 5

    def test_king_blockade_preference(self):
        active_defense_fen = "8/8/4k3/4P3/8/6p1/6K1/8 b - - 0 1"
        passive_defense_fen = "8/6k1/8/4P3/8/6p1/6K1/8 b - - 0 1"

        active_score = self._evaluate_fen(active_defense_fen)
        passive_score = self._evaluate_fen(passive_defense_fen)

        assert passive_score >= active_score + 5


if __name__ == "__main__":
    framework = MiniTestFramework()
    failed = framework.run([EndgameEvaluationTests])
    raise SystemExit(1 if failed else 0)
