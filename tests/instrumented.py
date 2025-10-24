import argparse
import re
import sys
import subprocess
import pathlib
import os

from testing import (
    EPD,
    TSAN,
    Pullfish as Engine,
    MiniTestFramework,
    OrderedClassMembers,
    Valgrind,
    Syzygy,
)

PATH = pathlib.Path(__file__).parent.resolve()
CWD = os.getcwd()


def get_prefix():
    if args.valgrind:
        return Valgrind.get_valgrind_command()
    if args.valgrind_thread:
        return Valgrind.get_valgrind_thread_command()

    return []


def get_threads():
    if args.valgrind_thread or args.sanitizer_thread:
        return 2
    return 1


def get_path():
    return os.path.abspath(os.path.join(CWD, args.pullfish_path))


def postfix_check(output):
    if args.sanitizer_undefined:
        for idx, line in enumerate(output):
            if "runtime error:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    if args.sanitizer_thread:
        for idx, line in enumerate(output):
            if "WARNING: ThreadSanitizer:" in line:
                # print next possible 50 lines
                for i in range(50):
                    debug_idx = idx + i
                    if debug_idx < len(output):
                        print(output[debug_idx])
                return False

    return True


def Pullfish(*args, **kwargs):
    return Engine(get_prefix(), get_path(), *args, **kwargs)


class TestCLI(metaclass=OrderedClassMembers):

    def beforeAll(self):
        pass

    def afterAll(self):
        pass

    def beforeEach(self):
        self.pullfish = None

    def afterEach(self):
        assert postfix_check(self.pullfish.get_output()) == True
        self.pullfish.clear_output()

    def test_eval(self):
        self.pullfish = Pullfish("eval".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_nodes_1000(self):
        self.pullfish = Pullfish("go nodes 1000".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_depth_10(self):
        self.pullfish = Pullfish("go depth 10".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_perft_4(self):
        self.pullfish = Pullfish("go perft 4".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_movetime_1000(self):
        self.pullfish = Pullfish("go movetime 1000".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_wtime_8000_btime_8000_winc_500_binc_500(self):
        self.pullfish = Pullfish(
            "go wtime 8000 btime 8000 winc 500 binc 500".split(" "),
            True,
        )
        assert self.pullfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0(self):
        self.pullfish = Pullfish(
            "go wtime 1000 btime 1000 winc 0 binc 0".split(" "),
            True,
        )
        assert self.pullfish.process.returncode == 0

    def test_go_wtime_1000_btime_1000_winc_0_binc_0_movestogo_5(self):
        self.pullfish = Pullfish(
            "go wtime 1000 btime 1000 winc 0 binc 0 movestogo 5".split(" "),
            True,
        )
        assert self.pullfish.process.returncode == 0

    def test_go_movetime_200(self):
        self.pullfish = Pullfish("go movetime 200".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_go_nodes_20000_searchmoves_e2e4_d2d4(self):
        self.pullfish = Pullfish(
            "go nodes 20000 searchmoves e2e4 d2d4".split(" "), True
        )
        assert self.pullfish.process.returncode == 0

    def test_bench_128_threads_8_default_depth(self):
        self.pullfish = Pullfish(
            f"bench 128 {get_threads()} 8 default depth".split(" "),
            True,
        )
        assert self.pullfish.process.returncode == 0

    def test_bench_128_threads_3_bench_tmp_epd_depth(self):
        self.pullfish = Pullfish(
            f"bench 128 {get_threads()} 3 {os.path.join(PATH,'bench_tmp.epd')} depth".split(
                " "
            ),
            True,
        )
        assert self.pullfish.process.returncode == 0

    def test_d(self):
        self.pullfish = Pullfish("d".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_compiler(self):
        self.pullfish = Pullfish("compiler".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_license(self):
        self.pullfish = Pullfish("license".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_uci(self):
        self.pullfish = Pullfish("uci".split(" "), True)
        assert self.pullfish.process.returncode == 0

    def test_uci_includes_multipv_info(self):
        self.pullfish = Pullfish(["uci"], True)
        assert (
            "info string MultiPV: Sets the number of alternate lines of analysis to display, with a value of 1 showing only the best line."
            in self.pullfish.process.stdout
        )

    def test_export_net_verify_nnue(self):
        current_path = os.path.abspath(os.getcwd())
        self.pullfish = Pullfish(
            f"export_net {os.path.join(current_path , 'verify.nnue')}".split(" "), True
        )
        assert self.pullfish.process.returncode == 0

    # verify the generated net equals the base net

    def test_network_equals_base(self):
        self.pullfish = Pullfish(
            ["uci"],
            True,
        )

        output = self.pullfish.process.stdout

        # find line
        for line in output.split("\n"):
            if "option name EvalFile type string default" in line:
                network = line.split(" ")[-1]
                break

        # find network file in src dir
        network = os.path.join(PATH.parent.resolve(), "src", network)

        if not os.path.exists(network):
            print(
                f"Network file {network} not found, please download the network file over the make command."
            )
            assert False

        diff = subprocess.run(["diff", network, f"verify.nnue"])

        assert diff.returncode == 0


class TestInteractive(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.pullfish = Pullfish()

    def afterAll(self):
        self.pullfish.quit()
        assert self.pullfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.pullfish.get_output()) == True
        self.pullfish.clear_output()

    def test_startup_output(self):
        self.pullfish.starts_with("Pullfish")

    def test_uci_command(self):
        self.pullfish.send_command("uci")
        self.pullfish.equals("uciok")

    def test_set_threads_option(self):
        self.pullfish.send_command(f"setoption name Threads value {get_threads()}")

    def test_ucinewgame_and_startpos_nodes_1000(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go nodes 1000")
        self.pullfish.starts_with("bestmove")

    def test_ucinewgame_and_startpos_moves(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position startpos moves e2e4 e7e6")
        self.pullfish.send_command("go nodes 1000")
        self.pullfish.starts_with("bestmove")

    def test_fen_position_1(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.pullfish.send_command("go nodes 1000")
        self.pullfish.starts_with("bestmove")

    def test_fen_position_2_flip(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position fen 5rk1/1K4p1/8/8/3B4/8/8/8 b - - 0 1")
        self.pullfish.send_command("flip")
        self.pullfish.send_command("go nodes 1000")
        self.pullfish.starts_with("bestmove")

    def test_depth_5_with_callback(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go depth 5")

        def callback(output):
            regex = r"info depth \d+ seldepth \d+ multipv \d+ score cp \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv"
            if output.startswith("info depth") and not re.match(regex, output):
                assert False
            if output.startswith("bestmove"):
                return True
            return False

        self.pullfish.check_output(callback)

    def test_ucinewgame_and_go_depth_9(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("setoption name UCI_ShowWDL value true")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go depth 9")

        depth = 1

        def callback(output):
            nonlocal depth

            regex = rf"info depth {depth} seldepth \d+ multipv \d+ score cp \d+ wdl \d+ \d+ \d+ nodes \d+ nps \d+ hashfull \d+ tbhits \d+ time \d+ pv"

            if output.startswith("info depth"):
                if not re.match(regex, output):
                    assert False
                depth += 1

            if output.startswith("bestmove"):
                assert depth == 10
                return True

            return False

        self.pullfish.check_output(callback)

    def test_clear_hash(self):
        self.pullfish.send_command("setoption name Clear Hash")

    def test_fen_position_mate_1(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 5K2/8/2qk4/2nPp3/3r4/6B1/B7/3R4 w - e6"
        )
        self.pullfish.send_command("go depth 18")

        self.pullfish.expect("* score mate 1 * pv d5e6")
        self.pullfish.equals("bestmove d5e6")

    def test_fen_position_mate_minus_1(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 2brrb2/8/p7/Q7/1p1kpPp1/1P1pN1K1/3P4/8 b - -"
        )
        self.pullfish.send_command("go depth 18")
        self.pullfish.expect("* score mate -1 *")
        self.pullfish.starts_with("bestmove")

    def test_fen_position_fixed_node(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 5K2/8/2P1P1Pk/6pP/3p2P1/1P6/3P4/8 w - - 0 1"
        )
        self.pullfish.send_command("go nodes 500000")
        self.pullfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.pullfish.send_command("go depth 18 searchmoves c6d7")
        self.pullfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.pullfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_mate(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.pullfish.send_command("go mate 2 searchmoves c6d7")
        self.pullfish.expect("* score mate 2 * pv c6d7 *")

        self.pullfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_nodes(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.pullfish.send_command("go nodes 500000 searchmoves c6d7")
        self.pullfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.pullfish.starts_with("bestmove")

    def test_fen_position_depth_27(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen r1b2r1k/pp1p2pp/2p5/2B1q3/8/8/P1PN2PP/R4RK1 w - - 0 18"
        )
        self.pullfish.send_command("go")
        self.pullfish.contains("score mate 1")

        self.pullfish.starts_with("bestmove")

    def test_fen_position_with_mate_go_depth_and_promotion(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7 f2f1q"
        )
        self.pullfish.send_command("go depth 18")
        self.pullfish.expect("* score mate 1 * pv f7f5")
        self.pullfish.starts_with("bestmove f7f5")

    def test_fen_position_with_mate_go_depth_and_searchmoves(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - -"
        )
        self.pullfish.send_command("go depth 18 searchmoves c6d7")
        self.pullfish.expect("* score mate 2 * pv c6d7 * f7f5")

        self.pullfish.starts_with("bestmove c6d7")

    def test_fen_position_with_moves_with_mate_go_depth_and_searchmoves(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command(
            "position fen 8/5R2/2K1P3/4k3/8/b1PPpp1B/5p2/8 w - - moves c6d7"
        )
        self.pullfish.send_command("go depth 18 searchmoves e3e2")
        self.pullfish.expect("* score mate -1 * pv e3e2 f7f5")
        self.pullfish.starts_with("bestmove e3e2")

    def test_verify_nnue_network(self):
        current_path = os.path.abspath(os.getcwd())
        Pullfish(
            f"export_net {os.path.join(current_path , 'verify.nnue')}".split(" "), True
        )

        self.pullfish.send_command("setoption name EvalFile value verify.nnue")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go depth 5")
        self.pullfish.starts_with("bestmove")

    def test_multipv_setting(self):
        self.pullfish.send_command("setoption name MultiPV value 4")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go depth 5")
        self.pullfish.starts_with("bestmove")

    def test_fen_position_with_skill_level(self):
        self.pullfish.send_command("setoption name Skill Level value 10")
        self.pullfish.send_command("position startpos")
        self.pullfish.send_command("go depth 5")
        self.pullfish.starts_with("bestmove")

        self.pullfish.send_command("setoption name Skill Level value 20")


class TestSyzygy(metaclass=OrderedClassMembers):
    def beforeAll(self):
        self.pullfish = Pullfish()

    def afterAll(self):
        self.pullfish.quit()
        assert self.pullfish.close() == 0

    def afterEach(self):
        assert postfix_check(self.pullfish.get_output()) == True
        self.pullfish.clear_output()

    def test_syzygy_setup(self):
        self.pullfish.starts_with("Pullfish")
        self.pullfish.send_command("uci")
        self.pullfish.send_command(
            f"setoption name SyzygyPath value {os.path.join(PATH, 'syzygy')}"
        )
        self.pullfish.expect(
            "info string Found 35 WDL and 35 DTZ tablebase files (up to 4-man)."
        )

    def test_syzygy_bench(self):
        self.pullfish.send_command("bench 128 1 8 default depth")
        self.pullfish.expect("Nodes searched  :*")

    def test_syzygy_position(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position fen 4k3/PP6/8/8/8/8/8/4K3 w - - 0 1")
        self.pullfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.pullfish.check_output(check_output)
        self.pullfish.expect("bestmove *")

    def test_syzygy_position_2(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 w - - 0 1")
        self.pullfish.send_command("go depth 5")

        def check_output(output):
            if "score cp 20000" in output or "score mate" in output:
                return True

        self.pullfish.check_output(check_output)
        self.pullfish.expect("bestmove *")

    def test_syzygy_position_3(self):
        self.pullfish.send_command("ucinewgame")
        self.pullfish.send_command("position fen 8/1P6/2B5/8/4K3/8/6k1/8 b - - 0 1")
        self.pullfish.send_command("go depth 5")

        def check_output(output):
            if "score cp -20000" in output or "score mate" in output:
                return True

        self.pullfish.check_output(check_output)
        self.pullfish.expect("bestmove *")


def parse_args():
    parser = argparse.ArgumentParser(description="Run Pullfish with testing options")
    parser.add_argument("--valgrind", action="store_true", help="Run valgrind testing")
    parser.add_argument(
        "--valgrind-thread", action="store_true", help="Run valgrind-thread testing"
    )
    parser.add_argument(
        "--sanitizer-undefined",
        action="store_true",
        help="Run sanitizer-undefined testing",
    )
    parser.add_argument(
        "--sanitizer-thread", action="store_true", help="Run sanitizer-thread testing"
    )

    parser.add_argument(
        "--none", action="store_true", help="Run without any testing options"
    )
    parser.add_argument("pullfish_path", type=str, help="Path to Pullfish binary")

    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    EPD.create_bench_epd()
    TSAN.set_tsan_option()
    Syzygy.download_syzygy()

    framework = MiniTestFramework()

    # Each test suite will be ran inside a temporary directory
    framework.run([TestCLI, TestInteractive, TestSyzygy])

    EPD.delete_bench_epd()
    TSAN.unset_tsan_option()

    if framework.has_failed():
        sys.exit(1)

    sys.exit(0)
