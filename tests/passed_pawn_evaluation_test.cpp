#include <gtest/gtest.h>

#include <string>

#include "evaluate.h"
#include "position.h"

using namespace Stockfish;

namespace {

int PressureFromFen(const std::string& fen, Color defender) {
    StateInfo st;
    Position  pos;
    pos.set(fen, false, &st);
    return Eval::detail::passed_pawn_pressure(pos, defender);
}

}  // namespace

TEST(PassedPawnEvaluation, HypnosGame1KingRace) {
    const std::string farKing = "6k1/6P1/5K2/8/8/8/8/8 w - - 0 1";
    const std::string nearKing = "5k2/6P1/5K2/8/8/8/8/8 w - - 0 1";
    const int         penaltyFar  = PressureFromFen(farKing, Color::BLACK);
    const int         penaltyNear = PressureFromFen(nearKing, Color::BLACK);
    EXPECT_GT(penaltyFar, penaltyNear);
    EXPECT_GT(penaltyFar, 0);
}

TEST(PassedPawnEvaluation, BrainlearnGame4RookBlockadeMitigates) {
    const std::string fen = "6k1/6r1/6P1/8/8/8/8/6K1 w - - 0 1";
    const int         blocked = PressureFromFen(fen, Color::BLACK);
    const int         base
        = PressureFromFen("6k1/6P1/5K2/8/8/8/8/8 w - - 0 1", Color::BLACK);
    EXPECT_LT(blocked, base);
}

TEST(PassedPawnEvaluation, ShashChessGame5ConnectedPassersExplodeThreat) {
    const std::string fen = "6k1/7P/6P1/6K1/8/8/8/8 w - - 0 1";
    const int         connected = PressureFromFen(fen, Color::BLACK);
    const int         rookSupport
        = PressureFromFen("6k1/6P1/8/8/8/8/6R1/6K1 w - - 0 1", Color::BLACK);
    EXPECT_GT(connected, rookSupport);
}

TEST(PassedPawnEvaluation, BrainlearnGame6RookSupportBoostsThreat) {
    const std::string fen = "6k1/6P1/8/8/8/8/6R1/6K1 w - - 0 1";
    const int         rookSupport = PressureFromFen(fen, Color::BLACK);
    const int         base
        = PressureFromFen("6k1/6P1/5K2/8/8/8/8/8 w - - 0 1", Color::BLACK);
    EXPECT_GT(rookSupport, base);
}
