#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "bitboard.h"
#include "evaluate.h"
#include "nnue/nnue_accumulator.h"
#include "nnue/network.h"
#include "position.h"
#include "types.h"

using namespace Stockfish;

namespace {

Eval::NNUE::Networks load_networks() {
    using namespace Eval::NNUE;

    NetworkBig   big({Eval::EvalFileDefaultNameBig, "None", ""}, EmbeddedNNUEType::BIG);
    NetworkSmall small({Eval::EvalFileDefaultNameSmall, "None", ""}, EmbeddedNNUEType::SMALL);

    const char* rootOverride = std::getenv("REVOLUTION_EVAL_SYMMETRY_NET_ROOT");
    const std::string rootDir = rootOverride ? rootOverride : std::string();

    const char* bigOverride   = std::getenv("REVOLUTION_EVAL_SYMMETRY_BIG");
    const char* smallOverride = std::getenv("REVOLUTION_EVAL_SYMMETRY_SMALL");

    const auto load_net = [&](auto& network, const char* overrideName, const char* defaultName) {
        const std::string filename = overrideName ? overrideName : defaultName;
        network.load(rootDir, filename);
    };

    load_net(big, bigOverride, Eval::EvalFileDefaultNameBig);
    load_net(small, smallOverride, Eval::EvalFileDefaultNameSmall);

    return Networks(std::move(big), std::move(small));
}

Value evaluate_position(const Position& pos, const Eval::NNUE::Networks& networks) {
    Eval::NNUE::AccumulatorStack  accumulators;
    Eval::NNUE::AccumulatorCaches caches(networks);
    return Eval::evaluate(networks, pos, accumulators, caches, 0);
}

void make_mirrored_position(const Position& original, Position& mirrored, StateInfo& state) {
    mirrored.set(original.fen(), original.is_chess960(), &state);
    mirrored.flip();
}

}  // namespace

int main() {
    try
    {
        Bitboards::init();
        Position::init();

        auto networks = load_networks();

        // Each FEN should exercise different evaluation terms. When new
        // heuristics are introduced, add a representative position here so the
        // symmetry check covers them. Keep every entry legal and ensure the
        // side to move is not in check; the test asserts those conditions.
        const std::array<std::string, 9> kTestFens = {
          "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
          "rnbq1rk1/ppp2ppp/3bpn2/2pp4/3P4/2P1PN2/PP1NBPPP/R2QKB1R w KQ - 0 1",
          "4rrk1/ppp2pp1/2n2q1p/3pp3/3PP3/2P2N1P/PPQ2PP1/2KR3R b - - 0 1",
          "r3k2r/pppq1ppp/2npbn2/2b1p3/2B1P3/2NPBN2/PPPQ1PPP/R3K2R w KQkq - 0 1",
          "8/8/3k4/8/8/3K4/8/2R5 w - - 0 1",
          "rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR b KQkq e6 0 2",
          "r1bq1rk1/pppp1ppp/2n2n2/4p3/2B1P3/2NP1N2/PPP2PPP/R1BQ1RK1 w - - 0 1",
          "4r1k1/pp2qppp/2n1bn2/3p4/3P4/2PB1N2/PPQ2PPP/2KR3R w - - 0 1",
          "rn1q1rk1/pbp2ppp/1p2pn2/3p4/3P4/1PN1PN2/PBP1BPPP/R2Q1RK1 w - - 0 9"};

        constexpr int kTolerance = 2;

        for (const auto& fen : kTestFens)
        {
            StateInfo originalState;
            Position  original;
            original.set(fen, false, &originalState);

            if (original.checkers())
            {
                std::cerr << "Invalid test FEN (side to move in check): " << fen << '\n';
                return 1;
            }

            StateInfo mirroredState;
            Position  mirrored;
            make_mirrored_position(original, mirrored, mirroredState);

            if (mirrored.checkers())
            {
                std::cerr << "Mirrored FEN enters check (update test corpus): " << fen << '\n';
                return 1;
            }

            const Value originalEval = evaluate_position(original, networks);
            const Value mirrorEval   = evaluate_position(mirrored, networks);

            const int sum = int(originalEval) + int(mirrorEval);
            if (std::abs(sum) > kTolerance)
            {
                std::cerr << "Eval symmetry violation for FEN: " << fen << '\n'
                          << "    Eval(original) = " << int(originalEval) << '\n'
                          << "    Eval(mirrored) = " << int(mirrorEval) << '\n'
                          << "    Sum = " << sum << '\n';
                return 1;
            }
        }

        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << "Error running eval symmetry test: " << ex.what() << '\n';
        return 1;
    }
}
